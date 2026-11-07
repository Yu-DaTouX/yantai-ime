#include "engine/quanpin/quanpin_dictionary.h"
#include "engine/quanpin/quanpin_query.h"
#include "engine/quanpin/query_status.h"
#include "engine/quanpin/latin_words.h"
#include "tests/includes/test_framework.h"
#include "tests/includes/test_utf8_path.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <thread>

namespace
{
class QueryDatabase
{
  public:
    QueryDatabase()
    {
        path = test::TempRoot() /
               ("query-failure-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
        REQUIRE_EQ(sqlite3_open(test::Utf8(path).c_str(), &db), SQLITE_OK);
        execute("CREATE TABLE tbl_1_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "CREATE INDEX n_weight ON tbl_1_n(weight DESC);"
                "INSERT INTO tbl_1_n VALUES('ni','n','你',100),('ni','n','尼',90);"
                "CREATE TABLE latin_words(key TEXT,value TEXT,weight INTEGER,exact_only INTEGER);"
                "INSERT INTO latin_words VALUES('ka''la''o''k','卡拉OK',100,0);");
    }
    ~QueryDatabase()
    {
        for (const auto &entry : statements)
            sqlite3_finalize(entry.second);
        sqlite3_close(db);
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    void execute(const char *sql)
    {
        REQUIRE_EQ(sqlite3_exec(db, sql, nullptr, nullptr, nullptr), SQLITE_OK);
    }
    sqlite3 *db = nullptr;
    std::filesystem::path path;
    std::unordered_map<std::string, sqlite3_stmt *> statements;
};

struct Fault
{
    int calls = 0;
    bool enabled = true;
};

void FailSecondRow(sqlite3_context *context, int, sqlite3_value **values)
{
    auto &fault = *static_cast<Fault *>(sqlite3_user_data(context));
    ++fault.calls;
    if (fault.enabled && fault.calls == 2)
        sqlite3_result_error(context, "injected query failure", -1);
    else
        sqlite3_result_value(context, values[0]);
}
} // namespace

TEST_CASE(quanpin_query_discards_partial_rows_and_reuses_failed_statement)
{
    QueryDatabase fixture;
    Fault fault;
    REQUIRE_EQ(sqlite3_create_function(fixture.db, "fail_second", 1, SQLITE_UTF8, &fault, FailSecondRow,
                                       nullptr, nullptr), SQLITE_OK);
    fixture.execute("CREATE VIEW faulty AS SELECT key,fail_second(value) AS value,weight FROM tbl_1_n;");
    // Verify the fault really arrives after a row was available, not during
    // prepare or the sort. The weight index makes this a streaming scan.
    sqlite3_stmt *raw = nullptr;
    REQUIRE_EQ(sqlite3_prepare_v2(fixture.db,
                                  "SELECT key,value,weight FROM faulty WHERE key>=?1 AND key<?2 ORDER BY weight DESC LIMIT 10",
                                  -1, &raw, nullptr), SQLITE_OK);
    {
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> probe(raw, sqlite3_finalize);
        REQUIRE_EQ(sqlite3_bind_text(raw, 1, "ni", -1, SQLITE_STATIC), SQLITE_OK);
        REQUIRE_EQ(sqlite3_bind_text(raw, 2, "nj", -1, SQLITE_STATIC), SQLITE_OK);
        REQUIRE_EQ(sqlite3_step(raw), SQLITE_ROW);
        REQUIRE_EQ(sqlite3_step(raw), SQLITE_ERROR);
    }
    fault.calls = 0;
    const quanpin::QueryCheckpoint failed;
    REQUIRE(quanpin::query_key_range_keyed(fixture.db, fixture.statements, "faulty", "ni", "nj", 10).empty());
    REQUIRE_EQ(fault.calls, 2);
    REQUIRE(failed.failed());
    for (const auto &entry : fixture.statements)
        REQUIRE_EQ(sqlite3_stmt_busy(entry.second), 0);

    fault.enabled = false;
    const quanpin::QueryCheckpoint retry;
    REQUIRE_EQ(quanpin::query_key_range_keyed(fixture.db, fixture.statements, "faulty", "ni", "nj", 10).size(),
               size_t{2});
    REQUIRE(!retry.failed());
}

TEST_CASE(quanpin_query_distinguishes_successful_miss_from_schema_and_bind_errors)
{
    QueryDatabase fixture;
    const quanpin::QueryCheckpoint miss;
    REQUIRE(quanpin::query_key_range_keyed(fixture.db, fixture.statements, "tbl_1_n", "x", "y", 10).empty());
    REQUIRE(!miss.failed());
    REQUIRE(quanpin::query_initial(fixture.db, "x", 10).empty()); // Optional missing partition.
    REQUIRE(!miss.failed());

    fixture.execute("CREATE VIEW malformed AS SELECT nonexistent FROM tbl_1_n;");
    const quanpin::QueryCheckpoint schema;
    REQUIRE(quanpin::query_key_range_keyed(fixture.db, fixture.statements, "malformed", "ni", "nj", 10).empty());
    REQUIRE(schema.failed());

    const int old_limit = sqlite3_limit(fixture.db, SQLITE_LIMIT_LENGTH, 1);
    const quanpin::QueryCheckpoint binding;
    const std::string oversized(1024, 'n');
    REQUIRE(quanpin::query_key_range_keyed(fixture.db, fixture.statements, "tbl_1_n", oversized, oversized + "z", 10)
                .empty());
    REQUIRE(binding.failed());
    sqlite3_limit(fixture.db, SQLITE_LIMIT_LENGTH, old_limit);
}

TEST_CASE(quanpin_query_checkpoints_are_nested_and_thread_local)
{
    const quanpin::QueryCheckpoint parent;
    std::thread other([] { quanpin::note_query_failure(); });
    other.join();
    REQUIRE(!parent.failed());
    {
        const quanpin::QueryCheckpoint child;
        quanpin::note_query_failure();
        REQUIRE(child.failed());
    }
    REQUIRE(parent.failed());
    const quanpin::QueryCheckpoint next;
    REQUIRE(!next.failed());
}

TEST_CASE(quanpin_dictionary_does_not_cache_locked_query_as_miss)
{
    QueryDatabase fixture;
    QuanpinDictionary dictionary(test::Utf8(fixture.path));
    fixture.execute("BEGIN EXCLUSIVE;");
    const quanpin::QueryCheckpoint locked;
    const auto unavailable = dictionary.query("ni", "ni");
    fixture.execute("ROLLBACK;");
    REQUIRE(unavailable.empty());
    REQUIRE(locked.failed());
    const quanpin::QueryCheckpoint retry;
    const auto available = dictionary.query("ni", "ni");
    REQUIRE(!retry.failed());
    REQUIRE(std::any_of(available.begin(), available.end(), [](const WordItem &item) { return item.word == "你"; }));
}

TEST_CASE(latin_words_failed_refresh_preserves_previous_snapshot_and_retries)
{
    QueryDatabase fixture;
    bool stale = true;
    sqlite3_int64 version = -1;
    auto words = quanpin::refresh_latin_words(nullptr, stale, version, fixture.db, fixture.statements);
    REQUIRE(!words->empty());
    Fault fault;
    REQUIRE_EQ(sqlite3_create_function(fixture.db, "fail_second", 1, SQLITE_UTF8, &fault, FailSecondRow,
                                       nullptr, nullptr), SQLITE_OK);
    fixture.execute("ALTER TABLE latin_words RENAME TO latin_source;"
                    "INSERT INTO latin_source VALUES('a''q','阿Q',50,0);"
                    "CREATE VIEW latin_words AS SELECT key,fail_second(value) AS value,weight,exact_only FROM latin_source;");
    stale = true;
    const auto failed = quanpin::refresh_latin_words(words, stale, version, fixture.db, fixture.statements);
    REQUIRE(failed == words);
    REQUIRE(stale);
    fault.enabled = false;
    const auto recovered = quanpin::refresh_latin_words(words, stale, version, fixture.db, fixture.statements);
    REQUIRE_EQ(recovered->all().size(), size_t{2});
    REQUIRE(!stale);
}
