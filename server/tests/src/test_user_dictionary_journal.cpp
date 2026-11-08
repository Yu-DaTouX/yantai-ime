#include "tests/includes/fake_traditional_spelling_index.h"
#include "tests/includes/test_framework.h"
#include "tests/includes/test_utf8_path.h"
#include "engine/english/english_dictionary.h"
#include "engine/local_modes/date_time_query.h"
#include "engine/quanpin/quanpin_query.h"
#include "engine/user_dictionary/learning_dynamics.h"
#include "engine/user_dictionary/user_dictionary_journal.h"
#include "ipc/traditional_variant_expansion_policy.h"

#include <sqlite3.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <windows.h>

namespace
{
class TestDatabase
{
  public:
    explicit TestDatabase(const std::filesystem::path &path)
    {
        REQUIRE_EQ(sqlite3_open(test::Utf8(path).c_str(), &db_), SQLITE_OK);
    }
    ~TestDatabase()
    {
        if (db_ != nullptr)
            sqlite3_close(db_);
    }
    void exec(const char *sql)
    {
        REQUIRE_EQ(sqlite3_exec(db_, sql, nullptr, nullptr, nullptr), SQLITE_OK);
    }
    int scalar_int(const char *sql)
    {
        return static_cast<int>(scalar_int64(sql));
    }

    std::int64_t scalar_int64(const char *sql)
    {
        sqlite3_stmt *stmt = nullptr;
        REQUIRE_EQ(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr), SQLITE_OK);
        REQUIRE_EQ(sqlite3_step(stmt), SQLITE_ROW);
        const std::int64_t value = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        return value;
    }

  private:
    sqlite3 *db_ = nullptr;
};

// TempRoot 下的一个临时目录：构造时清空重建，析构时删掉，断言失败抛异常时也删。
class ScopedTempDirectory
{
  public:
    explicit ScopedTempDirectory(const std::string &name)
        : path_(test::TempRoot() / (name + std::to_string(GetCurrentProcessId())))
    {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ~ScopedTempDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    ScopedTempDirectory(const ScopedTempDirectory &) = delete;
    ScopedTempDirectory &operator=(const ScopedTempDirectory &) = delete;
    const std::filesystem::path &path() const
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};
} // namespace

TEST_CASE(DictionaryLockWaitEndsBeforeTheClientGivesUp)
{
    // The wait exists to absorb a lock held for one commit. Anything longer than the client's own reply window cannot
    // end in a reply the client is still listening for, so the reader is better off failing the page than holding a
    // key until the client resets the composition. windows/src/IPC/Ipc.h owns the numbers; keep this the shorter of
    // the two.
    static_assert(user_dictionary::kJournalBusyTimeoutMs < 300,
                  "must fit inside FANY_IME_COMMIT_REPLY_SESSION_WAIT_MS");

    const auto directory = test::TempRoot() / ("msime-lock-wait-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    // A path other than default_user_db_path(): acquire_database only caches the default journal's connection, so this
    // one is opened per call and closed when the operation returns, which is what lets the directory be deleted below.
    REQUIRE(user_dictionary::ensure_user_database(test::Utf8(user_path)));

    // Warm the per-call open and schema check before taking the lock. Those are disk work, not
    // the lock wait this case bounds, and on a cold cache or under a scanner they alone can
    // reach the client window and time the case out.
    REQUIRE(user_dictionary::set_fixed_position(test::Utf8(user_path), "ni", "ni", "甲", 2));

    {
        // Hold the journal's write lock from a separate connection, the way a learning commit would.
        TestDatabase holder(user_path);
        holder.exec("BEGIN IMMEDIATE");

        const ULONGLONG started = GetTickCount64();
        const bool wrote = user_dictionary::set_fixed_position(test::Utf8(user_path), "ni", "ni", "乙", 1);
        const ULONGLONG waited = GetTickCount64() - started;

        // The write must lose, and must lose after the busy timeout instead of sitting on the
        // lock until the client has already given up. The bound is the wait plus headroom for
        // the (warm) open; it stays inside the client's 300 ms window checked above.
        REQUIRE(!wrote);
        REQUIRE(waited < user_dictionary::kJournalBusyTimeoutMs + 100);
        holder.exec("ROLLBACK");
    }

    std::filesystem::remove_all(directory, ec);
}

// The dictionary writer thread commits learned weights while the key thread
// builds candidates from the same files. In rollback-journal mode a commit's
// exclusive lock made those reads wait out the whole commit -- up to a second
// per key on a busy disk, long enough to time out a following commit key and
// drop the composition. In WAL mode the reads see the last committed snapshot.
TEST_CASE(DictionaryReadsDoNotWaitForALearningWrite)
{
    const auto directory = test::TempRoot() / ("msime-wal-reads-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto main_path = directory / "msime.db";
    {
        TestDatabase db(main_path);
        db.exec("CREATE TABLE tbl_1_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "INSERT INTO tbl_1_n VALUES('ni','n','甲',100);"
                "INSERT INTO tbl_1_n VALUES('ni','n','乙',90);");
    }
    REQUIRE(user_dictionary::enable_write_ahead_log(test::Utf8(main_path)));
    REQUIRE(user_dictionary::set_fixed_position(test::Utf8(user_path), "ni", "ni", "乙", 1));

    {
        TestDatabase journal_writer(user_path);
        TestDatabase main_writer(main_path);
        REQUIRE_EQ(journal_writer.scalar_int("SELECT COUNT(*) FROM pragma_journal_mode WHERE journal_mode='wal'"), 1);
        journal_writer.exec("BEGIN EXCLUSIVE");
        journal_writer.exec("DELETE FROM fixed_candidate_positions");
        main_writer.exec("BEGIN EXCLUSIVE");
        main_writer.exec("UPDATE tbl_1_n SET weight=1");

        const ULONGLONG started = GetTickCount64();
        std::vector<WordItem> candidates = {{"ni", "甲", 100}, {"ni", "乙", 90}};
        user_dictionary::apply_fixed_positions(test::Utf8(user_path), "ni", candidates, false);
        REQUIRE(std::any_of(candidates.begin(), candidates.end(),
                            [](const WordItem &item) { return item.word == "乙" && item.fixed_position == 1; }));
        TestDatabase reader(main_path);
        REQUIRE_EQ(reader.scalar_int("SELECT weight FROM tbl_1_n WHERE value='甲'"), 100);
        REQUIRE(GetTickCount64() - started < 1000);

        journal_writer.exec("ROLLBACK");
        main_writer.exec("ROLLBACK");
    }
    std::filesystem::remove_all(directory, ec);
}

// The session switches both shipped dictionaries to WAL when it is created; english.db used
// to keep the shipped rollback journal even though the dictionary writer commits learned
// English weights into it and the key path reads it.
TEST_CASE(EnableDictionaryWriteAheadLogsCoversEveryShippedDictionary)
{
    const auto directory = test::TempRoot() / ("msime-wal-switch-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory);
    const auto main_path = directory / "msime.db";
    const auto english_path = directory / "english.db";
    {
        TestDatabase main_db(main_path);
        main_db.exec("CREATE TABLE tbl_1_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);");
        TestDatabase english_db(english_path);
        english_db.exec("CREATE TABLE english_words(word TEXT,display TEXT,weight INTEGER);");
        REQUIRE_EQ(main_db.scalar_int("SELECT COUNT(*) FROM pragma_journal_mode WHERE journal_mode='delete'"), 1);
        REQUIRE_EQ(english_db.scalar_int("SELECT COUNT(*) FROM pragma_journal_mode WHERE journal_mode='delete'"), 1);
    }

    user_dictionary::enable_dictionary_write_ahead_logs(test::Utf8(directory));

    for (const auto &path : {main_path, english_path})
    {
        TestDatabase db(path);
        REQUIRE_EQ(db.scalar_int("SELECT COUNT(*) FROM pragma_journal_mode WHERE journal_mode='wal'"), 1);
    }

    // An older data directory can lack a shipped dictionary. The switch must neither fail nor
    // create an empty one, which would then be held open while the installer replaces it.
    std::filesystem::remove(english_path, ec);
    user_dictionary::enable_dictionary_write_ahead_logs(test::Utf8(directory));
    REQUIRE(!std::filesystem::exists(english_path));
    std::filesystem::remove_all(directory, ec);
}

// The key path queries the dictionaries through connections opened once per session. A learning
// commit in flight must not make those reads wait for the write lock (WAL) and must not make them
// fail the page with SQLITE_BUSY; they keep reading the last committed snapshot.
TEST_CASE(DictionaryQueriesReturnTheCommittedSnapshotDuringALearningWrite)
{
    const auto directory = test::TempRoot() / ("msime-wal-query-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory);
    const auto main_path = directory / "msime.db";
    const auto english_path = directory / "english.db";
    {
        TestDatabase main_db(main_path);
        main_db.exec("CREATE TABLE tbl_1_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                     "INSERT INTO tbl_1_n VALUES('ni','n','你',100);"
                     "INSERT INTO tbl_1_n VALUES('ni','n','尼',90);");
        TestDatabase english_db(english_path);
        english_db.exec("CREATE TABLE english_words(word TEXT COLLATE BINARY NOT NULL,display TEXT NOT NULL,"
                        "weight INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(word,display)) WITHOUT ROWID;"
                        "INSERT INTO english_words VALUES('ni','NI',100);"
                        "INSERT INTO english_words VALUES('ni','Ni',90);");
    }
    user_dictionary::enable_dictionary_write_ahead_logs(test::Utf8(directory));

    // Same shape as the session: both query connections are opened, and their statements
    // prepared, before any write happens.
    {
        sqlite3 *reader = nullptr;
        REQUIRE_EQ(sqlite3_open(test::Utf8(main_path).c_str(), &reader), SQLITE_OK);
        std::unordered_map<std::string, sqlite3_stmt *> statement_cache;
        EnglishDictionary english(test::Utf8(english_path), false);
        REQUIRE_EQ(quanpin::query_segments_flat(quanpin::Segments{"ni"}, reader, statement_cache, 8).size(), 2);
        REQUIRE_EQ(english.query_prefix("ni", 8).size(), 2);

        {
            // A learning commit in flight on both dictionaries, as the writer thread does it.
            TestDatabase main_writer(main_path);
            TestDatabase english_writer(english_path);
            main_writer.exec("BEGIN IMMEDIATE");
            main_writer.exec("UPDATE tbl_1_n SET weight=1 WHERE value='你'");
            main_writer.exec("INSERT INTO tbl_1_n VALUES('ni','n','呢',7)");
            english_writer.exec("BEGIN IMMEDIATE");
            english_writer.exec("UPDATE english_words SET weight=weight+1");

            const ULONGLONG started = GetTickCount64();
            const auto words = quanpin::query_segments_flat(quanpin::Segments{"ni"}, reader, statement_cache, 8);
            const auto english_words = english.query_prefix("ni", 8);
            const ULONGLONG elapsed = GetTickCount64() - started;

            // The uncommitted rows stay invisible, and neither page is dropped.
            REQUIRE_EQ(words.size(), 2);
            REQUIRE_EQ(words.front().second, 100);
            REQUIRE_EQ(english_words.size(), 2);
            REQUIRE_EQ(english_words.front().weight, 100);
            REQUIRE(elapsed < quanpin::kDictionaryBusyTimeoutMs);

            main_writer.exec("ROLLBACK");
            english_writer.exec("ROLLBACK");
        }

        for (auto &[sql, statement] : statement_cache)
            sqlite3_finalize(statement);
        statement_cache.clear();
        sqlite3_close(reader);
    }
    std::filesystem::remove_all(directory, ec);
}

TEST_CASE(UserDictionaryReplayIsIdempotentAcrossAllSettingsDictionaries)
{
    const auto directory = test::TempRoot() / ("msime-user-dictionary-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto main_path = directory / "msime.db";
    const auto english_path = directory / "english.db";

    {
        TestDatabase main_db(main_path);
        main_db.exec("CREATE TABLE tbl_2_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                     "CREATE TABLE tbl_others_s(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                     "CREATE TABLE quick_parases(key TEXT,value TEXT,weight INTEGER);"
                     "INSERT INTO tbl_2_n VALUES('ni''hao','nh','旧词',1);");
        TestDatabase english_db(english_path);
        english_db.exec("CREATE TABLE english_words(word TEXT PRIMARY KEY,display TEXT);"
                        "INSERT INTO english_words VALUES('obsolete','obsolete');");
    }

    REQUIRE(user_dictionary::record_upsert(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin, "ni'hao",
                                           "你好", 12000));
    REQUIRE(user_dictionary::record_delete(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin, "ni'hao",
                                           "旧词"));
    REQUIRE(user_dictionary::record_upsert(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin,
                                           "shui'shan'shu'ru'fa'hai'ke'yi", "水杉输入法还可以", 5000));
    REQUIRE(user_dictionary::record_upsert(test::Utf8(user_path), user_dictionary::DictionaryKind::QuickPhrase, "mail",
                                           "example@example.com", 20));
    REQUIRE(user_dictionary::record_upsert(test::Utf8(user_path), user_dictionary::DictionaryKind::English, "codex",
                                           "codex", 0, "Codex"));
    REQUIRE(user_dictionary::record_delete(test::Utf8(user_path), user_dictionary::DictionaryKind::English, "obsolete",
                                           "obsolete"));

    for (int pass = 0; pass < 2; ++pass)
    {
        const auto replay =
            user_dictionary::replay(test::Utf8(user_path), test::Utf8(main_path), test::Utf8(english_path));
        REQUIRE(replay.error.empty());
        REQUIRE_EQ(replay.failed, 0);
        REQUIRE_EQ(replay.applied, 6);
    }

    {
        TestDatabase main_db(main_path);
        REQUIRE_EQ(main_db.scalar_int("SELECT COUNT(*) FROM tbl_2_n WHERE key='ni''hao' AND value='你好' AND "
                                      "jp='nh' AND weight=12000"),
                   1);
        REQUIRE_EQ(main_db.scalar_int("SELECT COUNT(*) FROM tbl_2_n WHERE value='旧词'"), 0);
        REQUIRE_EQ(
            main_db.scalar_int("SELECT COUNT(*) FROM tbl_others_s WHERE key='shui''shan''shu''ru''fa''hai''ke''yi' "
                               "AND value='水杉输入法还可以' AND jp='sssrfhky' AND weight=5000"),
            1);
        REQUIRE_EQ(main_db.scalar_int("SELECT COUNT(*) FROM quick_parases WHERE key='mail' AND weight=20"), 1);
        TestDatabase english_db(english_path);
        REQUIRE_EQ(english_db.scalar_int("SELECT COUNT(*) FROM english_words WHERE word='codex' AND display='Codex'"),
                   1);
        REQUIRE_EQ(english_db.scalar_int("SELECT COUNT(*) FROM english_words WHERE word='obsolete'"), 0);
    }

    std::filesystem::remove_all(directory);
}

// The journal stores the absolute weights a learning write produced against the shipped
// dictionary of that time. After an upgrade that rescaled the dictionary (msime-dictionary
// aligned its pinyin weights with rime-wanxiang: 了/le went from 3.5 million to 52 million),
// replaying 了 at its old learned 6.5 million put 来 and 里 ahead of it on "l". A changed
// dictionary now keeps the new shipped weight for learned rows that sit below it.
TEST_CASE(UserDictionaryReplayKeepsNewShippedWeightsWhenTheDictionaryChanged)
{
    const auto directory = test::TempRoot() / ("msime-replay-rebase-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto main_path = directory / "msime.db";
    const auto english_path = directory / "english.db";
    const auto manifest_path = directory / "dictionary-manifest.json";

    const auto install_dictionary = [&](const char *sha256) {
        std::filesystem::remove(main_path, ec);
        {
            TestDatabase main_db(main_path);
            main_db.exec("CREATE TABLE tbl_1_l(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                         "CREATE TABLE tbl_2_z(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                         "INSERT INTO tbl_1_l VALUES('le','l','了',52009647);"
                         "INSERT INTO tbl_1_l VALUES('le','l','乐',6501204);"
                         "INSERT INTO tbl_1_l VALUES('lai','l','来',9077734);"
                         "INSERT INTO tbl_2_z VALUES('zhi''dao','zd','知道',1807266);"
                         "INSERT INTO tbl_2_z VALUES('zhi''dao','zd','直到',364449);");
        }
        if (sha256 == nullptr)
        {
            std::filesystem::remove(manifest_path, ec);
            return;
        }
        std::ofstream manifest(manifest_path, std::ios::binary | std::ios::trunc);
        manifest << "{\"files\":{\"msime.db\":{\"sha256\":\"" << sha256 << "\"}}}";
    };
    {
        TestDatabase english_db(english_path);
        english_db.exec("CREATE TABLE english_words(word TEXT PRIMARY KEY,display TEXT);");
    }

    // Learned on the previous dictionary: 了 and 乐 by the old staircase, 直到 promoted past 知道.
    // 来 was added by hand and 知道 lowered by hand in settings; both are replayed as written
    // whatever the dictionary says.
    REQUIRE(user_dictionary::record_upsert(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin, "le", "了",
                                           6501704));
    REQUIRE(user_dictionary::record_upsert(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin, "le", "乐",
                                           6501204));
    REQUIRE(user_dictionary::record_upsert(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin, "zhi'dao",
                                           "直到", 5052903));
    REQUIRE(user_dictionary::record_user_insert(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin, "lai",
                                                "来", 10000));
    REQUIRE(user_dictionary::record_weight_edit(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin,
                                                "zhi'dao", "知道", 1));

    // No manifest: the shipped dictionary cannot be identified, so every weight is replayed as written.
    install_dictionary(nullptr);
    auto replay = user_dictionary::replay(test::Utf8(user_path), test::Utf8(main_path), test::Utf8(english_path));
    REQUIRE(replay.error.empty());
    REQUIRE_EQ(replay.rebased, 0);
    {
        TestDatabase main_db(main_path);
        // 旧调频给提升的词让位而压低的权重（了 6501704 < 出厂 52009647）对衰减学习没有意义，保留出厂词频。
        REQUIRE_EQ(main_db.scalar_int64("SELECT weight FROM tbl_1_l WHERE value='了'"), 52009647);
    }

    // A dictionary the journal has not been replayed onto yet.
    install_dictionary("a");
    replay = user_dictionary::replay(test::Utf8(user_path), test::Utf8(main_path), test::Utf8(english_path));
    REQUIRE(replay.error.empty());
    REQUIRE_EQ(replay.failed, 0);
    REQUIRE_EQ(replay.applied, 5);
    // 只有「了」被改记成新出厂权重：「乐」的旧权重正好等于新出厂权重，不需要重写日志。
    REQUIRE_EQ(replay.rebased, 1);
    {
        TestDatabase main_db(main_path);
        REQUIRE_EQ(main_db.scalar_int64("SELECT weight FROM tbl_1_l WHERE value='了'"), 52009647);
        REQUIRE_EQ(main_db.scalar_int64("SELECT weight FROM tbl_1_l WHERE value='乐'"), 6501204);
        // 旧调频把直到提到 5052903（出厂 364449 的 13.9 倍）：换算成初始 dee，权重不丢。
        REQUIRE(std::abs(main_db.scalar_int64("SELECT weight FROM tbl_2_z WHERE value='直到'") - 5052903) <= 2);
        REQUIRE_EQ(main_db.scalar_int64("SELECT weight FROM tbl_2_z WHERE value='知道'"), 1);
        REQUIRE_EQ(main_db.scalar_int64("SELECT weight FROM tbl_1_l WHERE value='来'"), 10000);
        TestDatabase journal(user_path);
        REQUIRE_EQ(journal.scalar_int64("SELECT weight FROM user_dictionary_operations WHERE value='了' AND "
                                        "dictionary='pinyin'"),
                   52009647);
        REQUIRE(std::abs(journal.scalar_int64("SELECT weight FROM user_dictionary_operations WHERE value='直到'") -
                         5052903) <= 2);
        REQUIRE_EQ(journal.scalar_int64("SELECT commits FROM user_dictionary_operations WHERE value='直到'"), 1);
        REQUIRE_EQ(journal.scalar_int64("SELECT base_weight FROM user_dictionary_operations WHERE value='直到'"),
                   364449);
        REQUIRE_EQ(journal.scalar_int("SELECT COUNT(*) FROM dictionary_state WHERE "
                                      "name='replayed_dictionary_sha256' AND value='a'"),
                   1);
    }

    // 旧调频记录（commits = 0）里低于出厂词频的权重不再写回；已迁移的学习条目重装同一份词库后照样换算。
    REQUIRE(user_dictionary::record_upsert(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin, "le", "乐",
                                           5000000));
    install_dictionary("a");
    replay = user_dictionary::replay(test::Utf8(user_path), test::Utf8(main_path), test::Utf8(english_path));
    REQUIRE(replay.error.empty());
    REQUIRE_EQ(replay.rebased, 0);
    {
        TestDatabase main_db(main_path);
        REQUIRE_EQ(main_db.scalar_int64("SELECT weight FROM tbl_1_l WHERE value='了'"), 52009647);
        REQUIRE_EQ(main_db.scalar_int64("SELECT weight FROM tbl_1_l WHERE value='乐'"), 6501204);
        REQUIRE(std::abs(main_db.scalar_int64("SELECT weight FROM tbl_2_z WHERE value='直到'") - 5052903) <= 2);
    }

    // A learning write over a hand-set weight makes the row learned again.
    REQUIRE(user_dictionary::record_upsert(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin, "zhi'dao",
                                           "知道", 2));
    {
        TestDatabase journal(user_path);
        REQUIRE_EQ(journal.scalar_int("SELECT weight_edited FROM user_dictionary_operations WHERE value='知道'"), 0);
    }

    std::filesystem::remove_all(directory, ec);
}

TEST_CASE(UserDictionaryTracksOnlyExplicitUserInsertionsForExport)
{
    const auto directory = test::TempRoot() / ("msime-user-insert-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";

    {
        TestDatabase legacy_db(user_path);
        legacy_db.exec("CREATE TABLE user_dictionary_operations("
                       "dictionary TEXT NOT NULL,key TEXT NOT NULL,value TEXT NOT NULL,"
                       "operation TEXT NOT NULL,weight INTEGER NOT NULL DEFAULT 0,"
                       "display TEXT NOT NULL DEFAULT '',updated_at INTEGER NOT NULL DEFAULT(unixepoch()),"
                       "PRIMARY KEY(dictionary,key,value));"
                       "INSERT INTO user_dictionary_operations VALUES("
                       "'pinyin','yi','一','upsert',999999,'',unixepoch());");
    }

    REQUIRE(user_dictionary::ensure_user_database(test::Utf8(user_path)));
    REQUIRE(
        !user_dictionary::is_user_inserted(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin, "yi", "一"));

    REQUIRE(user_dictionary::record_upsert(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin, "xi'tong",
                                           "系统", 100));
    REQUIRE(!user_dictionary::is_user_inserted(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin,
                                               "xi'tong", "系统"));

    REQUIRE(user_dictionary::record_user_insert(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin,
                                                "yong'hu", "用户", 10000));
    REQUIRE(user_dictionary::is_user_inserted(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin, "yong'hu",
                                              "用户"));

    REQUIRE(user_dictionary::record_upsert(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin, "yong'hu",
                                           "用户", 20000));
    REQUIRE(user_dictionary::is_user_inserted(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin, "yong'hu",
                                              "用户"));

    std::filesystem::remove_all(directory);
}

TEST_CASE(EnterLearnedEnglishWordsAreValidatedPersistedAndIdempotent)
{
    const auto directory = test::TempRoot() / ("msime-enter-english-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto english_path = directory / "english.db";

    {
        TestDatabase english_db(english_path);
    }

    REQUIRE(user_dictionary::learn_entered_english_word(test::Utf8(english_path), test::Utf8(user_path), "Codex"));
    REQUIRE(user_dictionary::learn_entered_english_word(test::Utf8(english_path), test::Utf8(user_path), "Codex"));
    for (const char *word : {"Kotlin", "Ubuntu", "TypeScript", "Emoji", "Metasequoia", "Java", "YouTube", "Rust"})
        REQUIRE(user_dictionary::learn_entered_english_word(test::Utf8(english_path), test::Utf8(user_path), word));
    REQUIRE(!user_dictionary::learn_entered_english_word(test::Utf8(english_path), test::Utf8(user_path), "ni'hao"));
    REQUIRE(!user_dictionary::learn_entered_english_word(test::Utf8(english_path), test::Utf8(user_path), "hello2"));
    REQUIRE(!user_dictionary::learn_entered_english_word(test::Utf8(english_path), test::Utf8(user_path),
                                                         std::string(65, 'a')));

    {
        TestDatabase english_db(english_path);
        REQUIRE_EQ(english_db.scalar_int(
                       "SELECT COUNT(*) FROM english_words WHERE word='codex' AND display='Codex' AND weight=10"),
                   1);
        REQUIRE_EQ(english_db.scalar_int("SELECT COUNT(*) FROM english_words"), 9);
    }
    REQUIRE(user_dictionary::is_user_inserted(test::Utf8(user_path), user_dictionary::DictionaryKind::English, "codex",
                                              "Codex"));

    const auto main_path = directory / "msime.db";
    {
        TestDatabase main_db(main_path);
        main_db.exec("CREATE TABLE tbl_1_a(key TEXT,jp TEXT,value TEXT,weight INTEGER);");
        std::filesystem::remove(english_path);
        TestDatabase factory_english(english_path);
        factory_english.exec("CREATE TABLE english_words ("
                             "word TEXT PRIMARY KEY COLLATE BINARY, display TEXT NOT NULL"
                             ") WITHOUT ROWID;"
                             "INSERT INTO english_words VALUES('hello','hello');");
    }
    const auto replay = user_dictionary::replay(test::Utf8(user_path), test::Utf8(main_path), test::Utf8(english_path));
    REQUIRE(replay.error.empty());
    REQUIRE_EQ(replay.failed, 0);
    {
        TestDatabase english_db(english_path);
        REQUIRE_EQ(english_db.scalar_int(
                       "SELECT COUNT(*) FROM english_words WHERE word='codex' AND display='Codex' AND weight=10"),
                   1);
        REQUIRE_EQ(english_db.scalar_int("SELECT COUNT(*) FROM english_words WHERE word='metasequoia' AND "
                                         "display='Metasequoia' AND weight=10"),
                   1);
        REQUIRE_EQ(english_db.scalar_int("SELECT COUNT(*) FROM english_words WHERE word='hello' AND display='hello'"),
                   1);
    }

    std::filesystem::remove_all(directory);
}

TEST_CASE(UserDictionarySupportsFixedPositionsAndDeferredSafeRanking)
{
    const auto directory = test::TempRoot() / ("msime-ranking-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto main_path = directory / "msime.db";
    {
        TestDatabase db(main_path);
        db.exec("CREATE TABLE tbl_1_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "INSERT INTO tbl_1_n VALUES('ni','n','甲',100);"
                "INSERT INTO tbl_1_n VALUES('ni','n','乙',90);"
                "INSERT INTO tbl_1_n VALUES('ni','n','丙',80);"
                "INSERT INTO tbl_1_n VALUES('ni','n','丁',70);"
                "INSERT INTO tbl_1_n VALUES('ni','n','戊',60);"
                "INSERT INTO tbl_1_n VALUES('ni','n','己',50);");
    }
    std::vector<WordItem> candidates = {{"ni", "甲", 100}, {"ni", "乙", 90}, {"ni", "丙", 80},
                                        {"ni", "丁", 70},  {"ni", "戊", 60}, {"ni", "己", 50}};

    REQUIRE(user_dictionary::set_fixed_position(test::Utf8(user_path), "ni", "ni", "己", 2));
    REQUIRE(!user_dictionary::set_fixed_position(test::Utf8(user_path), "ni", "ni", "己", 6));
    candidates.insert(candidates.begin() + 1, {"ni", "云", 1, CandidateSource::CloudSuggestion});
    candidates.insert(candidates.begin() + 2, {"ni", "AI", 1, CandidateSource::AiSuggestion});
    user_dictionary::apply_fixed_positions(test::Utf8(user_path), "ni", candidates, false);
    REQUIRE_EQ(candidates[1].word, std::string("云"));
    REQUIRE_EQ(candidates[2].word, std::string("AI"));
    REQUIRE_EQ(candidates[3].word, std::string("己"));
    REQUIRE_EQ(candidates[3].fixed_position, 2);

    REQUIRE(user_dictionary::clear_fixed_position(test::Utf8(user_path), "ni", "ni", "己"));

    // With keep_dynamic_candidate_positions the caller already ordered the
    // suggestions, so they must not be hoisted back to slots 1 and 2.
    candidates = {{"ni", "甲", 100}, {"ni", "乙", 90}, {"ni", "丙", 80}};
    candidates.insert(candidates.begin(), {"ni", "AI", 1, CandidateSource::AiSuggestion});
    candidates.insert(candidates.begin() + 3, {"ni", "云", 1, CandidateSource::CloudSuggestion});
    user_dictionary::apply_fixed_positions(test::Utf8(user_path), "ni", candidates, false, {}, true);
    REQUIRE_EQ(candidates[0].word, std::string("AI"));
    REQUIRE_EQ(candidates[3].word, std::string("云"));

    candidates = {{"ni", "甲", 100}, {"ni", "乙", 90}, {"ni", "丙", 80},
                  {"ni", "丁", 70},  {"ni", "戊", 60}, {"ni", "己", 50}};
    // 衰减打分学习：不再数选择次数，每次上屏都记，一次就把权重抬到出厂词频的数倍。
    REQUIRE(user_dictionary::adjust_candidate_ranking(test::Utf8(main_path), test::Utf8(user_path), "ni", candidates,
                                                      "ni", "己", "linear", 2, 2, false));
    int first_weight = 0;
    {
        TestDatabase db(main_path);
        first_weight = db.scalar_int("SELECT weight FROM tbl_1_n WHERE value='己'");
        REQUIRE(first_weight > 100);
        REQUIRE_EQ(db.scalar_int("SELECT weight FROM tbl_1_n WHERE value='甲'"), 100);
    }
    REQUIRE(user_dictionary::adjust_candidate_ranking(test::Utf8(main_path), test::Utf8(user_path), "ni", candidates,
                                                      "ni", "己", "linear", 2, 2, false));
    {
        TestDatabase db(main_path);
        REQUIRE(db.scalar_int("SELECT weight FROM tbl_1_n WHERE value='己'") > first_weight);
    }

    {
        TestDatabase db(main_path);
        db.exec("UPDATE tbl_1_n SET weight=100 WHERE value='甲';"
                "UPDATE tbl_1_n SET weight=99 WHERE value='乙';"
                "UPDATE tbl_1_n SET weight=98 WHERE value='丙';");
    }
    candidates = {{"ni", "甲", 100}, {"ni", "乙", 99}, {"ni", "丙", 98}};
    REQUIRE(user_dictionary::adjust_candidate_ranking(test::Utf8(main_path), test::Utf8(user_path), "ni", candidates,
                                                      "ni", "丙", "linear", 1, 1, false));
    {
        TestDatabase db(main_path);
        REQUIRE(db.scalar_int("SELECT weight FROM tbl_1_n WHERE value='丙'") > 100);
        REQUIRE_EQ(db.scalar_int("SELECT weight FROM tbl_1_n WHERE value='乙'"), 99);
    }
    std::filesystem::remove_all(directory);
}

TEST_CASE(UserDictionaryRebalanceKeepsWeightsBounded)
{
    const auto directory = test::TempRoot() / ("msime-bounded-ranking-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto main_path = directory / "msime.db";
    {
        TestDatabase db(main_path);
        db.exec("CREATE TABLE tbl_1_y(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "INSERT INTO tbl_1_y VALUES('yi','y','甲',1000000000000);"
                "INSERT INTO tbl_1_y VALUES('yi','y','乙',999999000000);"
                "INSERT INTO tbl_1_y VALUES('yi','y','丙',999998000000);");
    }
    std::vector<WordItem> candidates = {
        {"yi", "甲", 1000000000000LL},
        {"yi", "乙", 999999000000LL},
        {"yi", "丙", 999998000000LL},
    };

    REQUIRE(user_dictionary::adjust_candidate_ranking(test::Utf8(main_path), test::Utf8(user_path), "yi", candidates,
                                                      "yi", "丙", "pin", 1, 1, true));
    {
        TestDatabase db(main_path);
        REQUIRE_EQ(db.scalar_int("SELECT COUNT(*) FROM tbl_1_y WHERE weight > 100000000"), 0);
        REQUIRE_EQ(db.scalar_int("SELECT COUNT(*) FROM tbl_1_y WHERE value='丙' AND weight=100000000"), 1);
        REQUIRE_EQ(db.scalar_int("SELECT COUNT(*) FROM tbl_1_y WHERE weight < 1"), 0);
    }
    std::filesystem::remove_all(directory);
}

TEST_CASE(UserDictionaryPromoteDoesNotCrushPrefixSinglesFromSeriesQuery)
{
    const auto directory = test::TempRoot() / ("msime-xianwang-ranking-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto main_path = directory / "msime.db";
    {
        TestDatabase db(main_path);
        db.exec("CREATE TABLE tbl_2_x(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "CREATE TABLE tbl_1_x(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "INSERT INTO tbl_2_x VALUES("
                "'xian''wang','xw','先王',15850),"
                "('xian''wang','xw','先往',4590),"
                "('xian''wang','xw','先汪',1),"
                "('xian''wang','xw','贤王',1),"
                "('xian''wang','xw','先亡',1),"
                "('xian''wang','xw','鲜网',1),"
                "('xian''wang','xw','献王',1),"
                "('xian''wang','xw','宪王',1),"
                "('xian''wang','xw','限网',1),"
                "('xian''wang','xw','现网',1),"
                "('xian''wang','xw','闲望',1),"
                "('xian''wang','xw','线网',1),"
                "('xian''wang','xw','纤网',1),"
                "('xian''wang','xw','宪网',1),"
                "('xian''wang','xw','弦望',1),"
                "('xian''wang','xw','仙王',1);"
                "INSERT INTO tbl_1_x VALUES("
                "'xian','x','先',1662684),"
                "('xian','x','现',1200000),"
                "('xian','x','显',1100000),"
                "('xian','x','线',1000000),"
                "('xian','x','县',1);");
    }

    const std::vector<std::pair<std::string, std::int64_t>> xianwang_words = {
        {"先王", 15850}, {"先往", 4590}, {"先汪", 1}, {"贤王", 1}, {"先亡", 1}, {"鲜网", 1}, {"献王", 1}, {"宪王", 1},
        {"限网", 1},     {"现网", 1},    {"闲望", 1}, {"线网", 1}, {"纤网", 1}, {"宪网", 1}, {"弦望", 1}, {"仙王", 1},
    };
    std::vector<WordItem> candidates;
    for (const auto &[word, weight] : xianwang_words)
        candidates.push_back({"xian'wang", word, weight, CandidateSource::Database, "xian'wang"});
    candidates.push_back({"xian", "先", 1662684, CandidateSource::Database, "xian"});
    candidates.push_back({"xian", "现", 1200000, CandidateSource::Database, "xian"});
    candidates.push_back({"xian", "显", 1100000, CandidateSource::Database, "xian"});
    candidates.push_back({"xian", "线", 1000000, CandidateSource::Database, "xian"});
    candidates.push_back({"xian", "县", 1, CandidateSource::Database, "xian"});

    bool ranking_changed = false;
    REQUIRE(user_dictionary::adjust_candidate_ranking(test::Utf8(main_path), test::Utf8(user_path), "xian'wang",
                                                      candidates, "xian'wang", "现网", "promote", 1, 1, false,
                                                      &ranking_changed));
    REQUIRE(ranking_changed);

    {
        TestDatabase db(main_path);
        REQUIRE(db.scalar_int64("SELECT weight FROM tbl_2_x WHERE value='现网'") > 1);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_1_x WHERE value='先'"), 1662684);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_1_x WHERE value='现'"), 1200000);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_1_x WHERE value='显'"), 1100000);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_1_x WHERE value='线'"), 1000000);
        REQUIRE_EQ(db.scalar_int("SELECT COUNT(*) FROM tbl_1_x WHERE weight < 1"), 0);
        REQUIRE_EQ(db.scalar_int("SELECT COUNT(*) FROM tbl_2_x WHERE weight < 1"), 0);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_2_x WHERE value='先王'"), 15850);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_2_x WHERE value='先往'"), 4590);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_2_x WHERE value='先亡'"), 1);
    }
    {
        TestDatabase user_db(user_path);
        REQUIRE_EQ(user_db.scalar_int("SELECT COUNT(*) FROM user_dictionary_operations WHERE key='xian'"), 0);
        REQUIRE_EQ(user_db.scalar_int("SELECT COUNT(*) FROM user_dictionary_operations WHERE value='先'"), 0);
        REQUIRE_EQ(user_db.scalar_int(
                       "SELECT COUNT(*) FROM user_dictionary_operations WHERE key='xian''wang' AND value='现网' AND "
                       "operation='upsert'"),
                   1);
        REQUIRE(user_db.scalar_int64("SELECT weight FROM user_dictionary_operations WHERE value='现网'") > 1);
    }
    std::filesystem::remove_all(directory);
}

// 回归：调频的权重基准必须取自按权重排好序的比较集，不能按显示顺序取。备选切分词
// （xi'e 的西鄂 w=6）会坐在 xie 显示列表最前面，拿它当基准会把写（原 605147）写成
// 6+500=506，权重反而低于些/血/谢——这就是用户机器上「置顶写之后写还是不在前排」的根因。
// 同音节的多个表仍互相比权，不受影响。
TEST_CASE(UserDictionaryPromoteBasesWeightOnTheHeaviestCandidateNotTheTopmost)
{
    const auto directory = test::TempRoot() / ("msime-cross-table-ranking-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto main_path = directory / "msime.db";
    {
        TestDatabase db(main_path);
        db.exec("CREATE TABLE tbl_1_x(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "CREATE TABLE tbl_2_x(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "INSERT INTO tbl_1_x VALUES("
                "'xie','x','些',3752167),"
                "('xie','x','血',933230),"
                "('xie','x','谢',623220),"
                "('xie','x','写',605147);"
                "INSERT INTO tbl_2_x VALUES('xi''e','xa','西鄂',6),('xi''e','xa','喜恶',1);");
    }
    // 显示列表照旧把备选切分词放在最前（旧排序的产物）。
    std::vector<WordItem> candidates = {
        {"xi'e", "西鄂", 6, CandidateSource::Database, "xi'e"},
        {"xie", "些", 3752167, CandidateSource::Database, "xie"},
        {"xie", "血", 933230, CandidateSource::Database, "xie"},
        {"xie", "谢", 623220, CandidateSource::Database, "xie"},
        {"xie", "写", 605147, CandidateSource::Database, "xie"},
    };

    bool ranking_changed = false;
    REQUIRE(user_dictionary::adjust_candidate_ranking(test::Utf8(main_path), test::Utf8(user_path), "xie", candidates,
                                                      "xie", "写", "pin", 1, 1, true, &ranking_changed));
    REQUIRE(ranking_changed);
    {
        TestDatabase db(main_path);
        // 置顶写：基准必须是同表的些（3752167），不再是跨表的西鄂 6。
        REQUIRE(db.scalar_int64("SELECT weight FROM tbl_1_x WHERE value='写'") > 3752167);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_2_x WHERE value='西鄂'"), 6);
    }

    // 已经是同表首位的词不再被跨表基准反降（旧逻辑会把些写成 6+500=506）。
    ranking_changed = false;
    REQUIRE(user_dictionary::adjust_candidate_ranking(test::Utf8(main_path), test::Utf8(user_path), "xie", candidates,
                                                      "xie", "些", "pin", 1, 1, true, &ranking_changed));
    REQUIRE(!ranking_changed);
    {
        TestDatabase db(main_path);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_1_x WHERE value='些'"), 3752167);
    }
    std::filesystem::remove_all(directory);
}

// 回归：调频必须能把另一种切分的词调到整个列表的首位。打 jian 时列表里混着 ji'an 的
// 吉安（出货词库权重是 1，压在同组的积案 9420 底下），旧实现按音节数圈比较集，吉安
// 只跟积案/几案比，可学权重封顶一万出头，怎么调都追不上 见 的 3460998——用户看到的就是
// 「调多少次都没用」。比较集不再按音节数切开之后，基准取到 见，一次置顶就越过它。
TEST_CASE(UserDictionaryPromoteLiftsAnAlternativeSegmentationAcrossKeys)
{
    const auto directory = test::TempRoot() / ("msime-cross-key-ranking-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto main_path = directory / "msime.db";
    {
        TestDatabase db(main_path);
        db.exec("CREATE TABLE tbl_1_j(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "CREATE TABLE tbl_2_j(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "INSERT INTO tbl_1_j VALUES("
                "'jian','j','见',3460998),"
                "('jian','j','间',3067939),"
                "('jian','j','剑',1151704);"
                "INSERT INTO tbl_2_j VALUES("
                "'ji''an','ja','积案',9420),"
                "('ji''an','ja','几案',5999),"
                "('ji''an','ja','吉安',1);");
    }
    std::vector<WordItem> candidates = {
        {"jian", "见", 3460998, CandidateSource::Database, "jian"},
        {"jian", "间", 3067939, CandidateSource::Database, "jian"},
        {"jian", "剑", 1151704, CandidateSource::Database, "jian"},
        {"jian", "积案", 9420, CandidateSource::Database, "ji'an"},
        {"jian", "几案", 5999, CandidateSource::Database, "ji'an"},
        {"jian", "吉安", 1, CandidateSource::Database, "ji'an"},
    };

    bool ranking_changed = false;
    REQUIRE(user_dictionary::adjust_candidate_ranking(test::Utf8(main_path), test::Utf8(user_path), "jian", candidates,
                                                      "ji'an", "吉安", "pin", 1, 1, true, &ranking_changed));
    REQUIRE(ranking_changed);
    {
        TestDatabase db(main_path);
        // 基准是整个列表最重的 见，不再是 ji'an 这一组的头部 积案。
        REQUIRE(db.scalar_int64("SELECT weight FROM tbl_2_j WHERE value='吉安'") > 3460998);
        // 写入仍只落在 entry_key 的行上：jian 那张表一行都没动（#36 的护栏）。
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_1_j WHERE value='见'"), 3460998);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_1_j WHERE value='间'"), 3067939);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_2_j WHERE value='积案'"), 9420);
    }
    std::filesystem::remove_all(directory);
}

// 回归：不置顶、按 promote 模式连着选几次，也要能把吉安一路顶到首位。每一轮都照着
// 库里的新权重重排候选列表，模拟用户重新打一遍 jian 看到的顺序。
TEST_CASE(UserDictionaryRepeatedCommitsRaiseAnAlternativeSegmentationOnItsOwnKey)
{
    const auto directory = test::TempRoot() / ("msime-cross-key-walk-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto main_path = directory / "msime.db";
    {
        TestDatabase db(main_path);
        db.exec("CREATE TABLE tbl_1_j(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "CREATE TABLE tbl_2_j(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "INSERT INTO tbl_1_j VALUES("
                "'jian','j','见',3460998),"
                "('jian','j','间',3067939),"
                "('jian','j','剑',1151704),"
                "('jian','j','件',935235),"
                "('jian','j','建',598616);"
                "INSERT INTO tbl_2_j VALUES("
                "'ji''an','ja','积案',9420),"
                "('ji''an','ja','几案',5999),"
                "('ji''an','ja','吉安',1);");
    }
    const std::vector<std::pair<std::string, std::string>> rows = {
        {"jian", "见"}, {"jian", "间"},    {"jian", "剑"},    {"jian", "件"},
        {"jian", "建"}, {"ji'an", "积案"}, {"ji'an", "几案"}, {"ji'an", "吉安"},
    };
    const auto current_list = [&]() {
        TestDatabase db(main_path);
        std::vector<WordItem> list;
        for (const auto &row : rows)
        {
            const std::string table = row.first == "jian" ? "tbl_1_j" : "tbl_2_j";
            const auto sql = "SELECT weight FROM " + table + " WHERE value='" + row.second + "'";
            list.push_back({"jian", row.second, db.scalar_int64(sql.c_str()), CandidateSource::Database, row.first});
        }
        std::stable_sort(list.begin(), list.end(),
                         [](const WordItem &lhs, const WordItem &rhs) { return lhs.weight > rhs.weight; });
        return list;
    };
    const auto rank_of = [](const std::vector<WordItem> &list, const std::string &word) {
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i].word == word)
                return i;
        return list.size();
    };

    // 衰减打分：反复上屏同一个另一读音的词，权重逐次上升，写入只落在它自己的键（ji'an）的行上；
    // 权重只有 1 的生僻词不会一下子越过 340 万的常用字（要靠大量上屏才行），也不会拖动别的键的行。
    std::int64_t previous = 1;
    for (int round = 0; round < 20; ++round)
    {
        const auto list = current_list();
        REQUIRE(user_dictionary::adjust_candidate_ranking(test::Utf8(main_path), test::Utf8(user_path), "jian", list,
                                                          "ji'an", "吉安", "promote", 1, 1, false));
        TestDatabase db(main_path);
        const std::int64_t weight = db.scalar_int64("SELECT weight FROM tbl_2_j WHERE value='吉安'");
        REQUIRE(weight > previous);
        previous = weight;
    }
    REQUIRE(previous > 100);
    REQUIRE(rank_of(current_list(), "吉安") > 0);
    {
        TestDatabase db(main_path);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_1_j WHERE value='见'"), 3460998);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_2_j WHERE value='积案'"), 9420);
    }
    std::filesystem::remove_all(directory);
}

TEST_CASE(UserDictionaryEqualWeightPromoteDoesNotWriteNegatives)
{
    const auto directory = test::TempRoot() / ("msime-equal-weight-ranking-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto main_path = directory / "msime.db";
    {
        TestDatabase db(main_path);
        db.exec("CREATE TABLE tbl_1_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "INSERT INTO tbl_1_n VALUES('ni','n','甲',1),"
                "('ni','n','乙',1),"
                "('ni','n','丙',1),"
                "('ni','n','丁',1),"
                "('ni','n','戊',1),"
                "('ni','n','己',1);");
    }
    std::vector<WordItem> candidates = {
        {"ni", "甲", 1}, {"ni", "乙", 1}, {"ni", "丙", 1}, {"ni", "丁", 1}, {"ni", "戊", 1}, {"ni", "己", 1},
    };

    REQUIRE(user_dictionary::adjust_candidate_ranking(test::Utf8(main_path), test::Utf8(user_path), "ni", candidates,
                                                      "ni", "己", "promote", 1, 1, false));
    {
        TestDatabase db(main_path);
        REQUIRE(db.scalar_int64("SELECT weight FROM tbl_1_n WHERE value='己'") > 1);
        REQUIRE_EQ(db.scalar_int("SELECT COUNT(*) FROM tbl_1_n WHERE weight < 1"), 0);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_1_n WHERE value='甲'"), 1);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_1_n WHERE value='乙'"), 1);
    }
    {
        TestDatabase user_db(user_path);
        REQUIRE_EQ(user_db.scalar_int("SELECT COUNT(*) FROM user_dictionary_operations"), 1);
        REQUIRE_EQ(user_db.scalar_int("SELECT COUNT(*) FROM user_dictionary_operations WHERE value='己'"), 1);
    }
    std::filesystem::remove_all(directory);
}

TEST_CASE(UserDictionaryReplaySkipsNonpositivePinyinUpserts)
{
    const auto directory = test::TempRoot() / ("msime-skip-negative-replay-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto main_path = directory / "msime.db";
    const auto english_path = directory / "english.db";
    {
        TestDatabase main_db(main_path);
        main_db.exec("CREATE TABLE tbl_1_x(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                     "INSERT INTO tbl_1_x VALUES('xian','x','先',1662684);");
        TestDatabase english_db(english_path);
        english_db.exec("CREATE TABLE english_words(word TEXT PRIMARY KEY,display TEXT);");
    }

    REQUIRE(user_dictionary::record_upsert(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin, "xian", "先",
                                           -12999));
    REQUIRE(user_dictionary::record_upsert(test::Utf8(user_path), user_dictionary::DictionaryKind::Pinyin, "xian", "现",
                                           5000));

    const auto replay = user_dictionary::replay(test::Utf8(user_path), test::Utf8(main_path), test::Utf8(english_path));
    REQUIRE(replay.error.empty());
    REQUIRE_EQ(replay.failed, 0);
    REQUIRE_EQ(replay.skipped, 1);
    REQUIRE_EQ(replay.applied, 1);
    {
        TestDatabase db(main_path);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_1_x WHERE value='先'"), 1662684);
        REQUIRE_EQ(db.scalar_int("SELECT COUNT(*) FROM tbl_1_x WHERE value='现' AND weight=5000"), 1);
    }

    REQUIRE(user_dictionary::ensure_user_database(test::Utf8(user_path)));
    {
        TestDatabase user_db(user_path);
        REQUIRE_EQ(user_db.scalar_int("SELECT COUNT(*) FROM user_dictionary_operations WHERE value='先'"), 0);
        REQUIRE_EQ(user_db.scalar_int("SELECT COUNT(*) FROM user_dictionary_operations WHERE value='现'"), 1);
    }
    std::filesystem::remove_all(directory);
}

// #869：旧版日文模式调频时，日文候选以罗马音为键进了拼音日志；msime.db 里没有那些行，
// UPDATE 改不到任何东西，但日志照记，升级回放时 apply_pinyin 就把日文词插进了拼音表。
TEST_CASE(UserDictionaryRankingDoesNotJournalRowsMissingFromTheDictionary)
{
    const auto directory = test::TempRoot() / ("msime-ranking-missing-rows-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto main_path = directory / "msime.db";
    {
        TestDatabase db(main_path);
        db.exec("CREATE TABLE tbl_1_p(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "INSERT INTO tbl_1_p VALUES('pan','p','盘',5000);");
    }
    std::vector<WordItem> candidates = {
        {"pan", "パン", 980000, CandidateSource::Database, "pan"},
        {"pan", "パン粉", 970000, CandidateSource::Database, "pan"},
    };
    REQUIRE(user_dictionary::adjust_candidate_ranking(test::Utf8(main_path), test::Utf8(user_path), "pan", candidates,
                                                      "pan", "パン粉", "promote", 1, 1, false));
    {
        TestDatabase db(main_path);
        REQUIRE_EQ(db.scalar_int("SELECT COUNT(*) FROM tbl_1_p"), 1);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_1_p WHERE value='盘'"), 5000);
    }
    {
        TestDatabase user_db(user_path);
        REQUIRE_EQ(user_db.scalar_int("SELECT COUNT(*) FROM user_dictionary_operations"), 0);
    }
    std::filesystem::remove_all(directory);
}

// 固定位置作用在已展开的列表上（混输英文回来后重新应用固定位置）：真实的 apply_fixed_positions 经
// ReorderAsOneItemPerWord 只看到每组一项，固定词的每种写法都留着，插回的位置按组算，不切开别的组。
TEST_CASE(UserDictionaryFixedPositionsOnAnExpandedListKeepEverySpellingGroupWhole)
{
    const ScopedTempDirectory directory("msime-fixed-positions-spellings-");
    const std::string user_path = test::Utf8(directory.path() / "msime_user.db");
    const test::FakeTraditionalSpellingIndex index({{"台", {"臺", "台", "檯", "颱"}}, {"为", {"為", "爲"}}});
    const auto expand = [&](std::vector<WordItem> items) {
        return FanyImeIpc::ExpandTraditionalVariants(std::move(items), index, FanyImeIpc::TraditionalRegion::Taiwan);
    };
    const auto apply_fixed = [&](std::vector<WordItem> &items) {
        user_dictionary::apply_fixed_positions(user_path, "ni", items, false);
    };
    const auto display = [](const std::vector<WordItem> &items) {
        std::vector<std::string> texts;
        for (const auto &item : items)
            texts.push_back(item.output_text);
        return texts;
    };

    REQUIRE(user_dictionary::set_fixed_position(user_path, "ni", "ni", "为", 1));
    const auto to_front = FanyImeIpc::ReorderAsOneItemPerWord(
        expand({{"ni", "你", 9}, {"tai", "台", 8}, {"wei", "为", 7}}), apply_fixed, expand);
    REQUIRE_EQ(display(to_front), (std::vector<std::string>{"為", "爲", "你", "臺", "台", "檯", "颱"}));
    REQUIRE_EQ(to_front[0].fixed_position, 1);
    REQUIRE_EQ(to_front[1].fixed_position, 1);

    REQUIRE(user_dictionary::set_fixed_position(user_path, "ni", "ni", "为", 3));
    const auto third = FanyImeIpc::ReorderAsOneItemPerWord(
        expand({{"ni", "你", 9}, {"wei", "为", 8}, {"tai", "台", 7}}), apply_fixed, expand);
    REQUIRE_EQ(display(third), (std::vector<std::string>{"你", "臺", "台", "檯", "颱", "為", "爲"}));
}

// 已经写进日志的那些残留：升级回放要跳过，打开日志时清掉；用户在设置页手动加的带假名词不能动。
TEST_CASE(UserDictionaryReplaySkipsLearnedKanaPinyinUpserts)
{
    const auto directory = test::TempRoot() / ("msime-skip-kana-replay-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto main_path = directory / "msime.db";
    const auto english_path = directory / "english.db";
    {
        TestDatabase main_db(main_path);
        main_db.exec("CREATE TABLE tbl_1_p(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                     "CREATE TABLE tbl_1_a(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                     "CREATE TABLE tbl_1_o(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                     "CREATE TABLE tbl_1_x(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                     "INSERT INTO tbl_1_x VALUES('xian','x','现',1);");
        TestDatabase english_db(english_path);
        english_db.exec("CREATE TABLE english_words(word TEXT PRIMARY KEY,display TEXT);");
    }

    const auto user = test::Utf8(user_path);
    REQUIRE(user_dictionary::record_upsert(user, user_dictionary::DictionaryKind::Pinyin, "pan", "パン", 980000));
    REQUIRE(user_dictionary::record_upsert(user, user_dictionary::DictionaryKind::Pinyin, "amarini", "余りに", 900000));
    REQUIRE(
        user_dictionary::record_upsert(user, user_dictionary::DictionaryKind::Pinyin, "omoimasu", "思います", 900000));
    REQUIRE(user_dictionary::record_upsert(user, user_dictionary::DictionaryKind::Pinyin, "xian", "现", 5000));
    REQUIRE(user_dictionary::record_user_insert(user, user_dictionary::DictionaryKind::Pinyin, "pen", "ペン", 100));

    const auto replay = user_dictionary::replay(user, test::Utf8(main_path), test::Utf8(english_path));
    REQUIRE(replay.error.empty());
    REQUIRE_EQ(replay.failed, 0);
    REQUIRE_EQ(replay.skipped, 3);
    REQUIRE_EQ(replay.applied, 2);
    {
        TestDatabase db(main_path);
        REQUIRE_EQ(db.scalar_int("SELECT COUNT(*) FROM tbl_1_p WHERE value='パン'"), 0);
        REQUIRE_EQ(db.scalar_int("SELECT COUNT(*) FROM tbl_1_a"), 0);
        REQUIRE_EQ(db.scalar_int("SELECT COUNT(*) FROM tbl_1_o"), 0);
        // 现 的旧调频权重 5000 是出厂 1 的 5000 倍：迁移成封顶热度的学习条目，权重落在 (1, 上限] 内。
        REQUIRE(db.scalar_int64("SELECT weight FROM tbl_1_x WHERE value='现'") > 1);
        REQUIRE_EQ(db.scalar_int("SELECT COUNT(*) FROM tbl_1_p WHERE value='ペン'"), 1);
    }

    REQUIRE(user_dictionary::ensure_user_database(user));
    {
        TestDatabase user_db(user_path);
        REQUIRE_EQ(user_db.scalar_int("SELECT COUNT(*) FROM user_dictionary_operations WHERE user_inserted=0 AND "
                                      "value IN ('パン','余りに','思います')"),
                   0);
        REQUIRE_EQ(user_db.scalar_int("SELECT COUNT(*) FROM user_dictionary_operations WHERE value IN ('现','ペン')"),
                   2);
    }
    std::filesystem::remove_all(directory);
}

TEST_CASE(UserDictionaryLearningLeavesTheOtherRowsOfTheKeyUntouched)
{
    const auto directory = test::TempRoot() / ("msime-local-rebalance-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto main_path = directory / "msime.db";
    {
        TestDatabase db(main_path);
        db.exec("CREATE TABLE tbl_1_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "INSERT INTO tbl_1_n VALUES('ni','n','甲',20000),"
                "('ni','n','乙',19000),"
                "('ni','n','丙',18000),"
                "('ni','n','丁',17000),"
                "('ni','n','戊',16000),"
                "('ni','n','己',15000);");
    }
    std::vector<WordItem> candidates = {
        {"ni", "甲", 20000}, {"ni", "乙", 20000}, {"ni", "丙", 20000},
        {"ni", "丁", 20000}, {"ni", "戊", 20000}, {"ni", "己", 20000},
    };
    REQUIRE(user_dictionary::adjust_candidate_ranking(test::Utf8(main_path), test::Utf8(user_path), "ni", candidates,
                                                      "ni", "己", "promote", 1, 1, false));
    {
        TestDatabase db(main_path);
        REQUIRE_EQ(db.scalar_int("SELECT COUNT(*) FROM tbl_1_n WHERE weight < 1"), 0);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_1_n WHERE value='甲'"), 20000);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_1_n WHERE value='戊'"), 16000);
        REQUIRE(db.scalar_int64("SELECT weight FROM tbl_1_n WHERE value='己'") > 20000);
    }
    std::filesystem::remove_all(directory);
}

// 候选没带 canonical_pinyin 时，键要从 context_key 按字数截出来（先 -> xian，
// 现网 -> xian'wang）。较短的那个键现在也参与比权，但写入必须只落在 entry_key 的行上。
TEST_CASE(UserDictionaryRankingKeepsWritesOnTheEntryKeyWhenKeysAreReconstructed)
{
    const auto directory =
        test::TempRoot() / ("msime-reconstructed-key-ranking-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const auto main_path = directory / "msime.db";
    {
        TestDatabase db(main_path);
        db.exec("CREATE TABLE tbl_2_x(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "CREATE TABLE tbl_1_x(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "INSERT INTO tbl_2_x VALUES('xian''wang','xw','现网',1),('xian''wang','xw','先亡',1);"
                "INSERT INTO tbl_1_x VALUES('xian','x','先',1662684);");
    }
    std::vector<WordItem> candidates = {
        {"xianwang", "先亡", 1},
        {"xianwang", "现网", 1},
        {"xianwang", "先", 1662684},
    };
    REQUIRE(user_dictionary::adjust_candidate_ranking(test::Utf8(main_path), test::Utf8(user_path), "xian'wang",
                                                      candidates, "xian'wang", "现网", "promote", 1, 1, false));
    {
        TestDatabase db(main_path);
        REQUIRE(db.scalar_int64("SELECT weight FROM tbl_2_x WHERE value='现网'") > 1);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_1_x WHERE value='先'"), 1662684);
        REQUIRE_EQ(db.scalar_int64("SELECT weight FROM tbl_2_x WHERE value='先亡'"), 1);
    }
    std::filesystem::remove_all(directory);
}

TEST_CASE(DateTimeFormatOrderIsLearnedPinnedAndFixedByFormatId)
{
    const auto directory = test::TempRoot() / ("msime-date-time-order-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const std::string user_path = test::Utf8(directory / "msime_user.db");
    const metasequoia::local_modes::LocalDateTime now{2026, 8, 9, 0, 14, 30, 0};
    const auto default_ids = metasequoia::local_modes::date_time_format_ids("rq");
    const auto ordered = [&](const char *keyword, bool learned_order = true) {
        auto items = metasequoia::local_modes::query_date_time(keyword, &now);
        user_dictionary::apply_date_time_order(user_path, metasequoia::local_modes::date_time_category(keyword), items,
                                               learned_order);
        return items;
    };
    // REQUIRE_EQ 按引用接住参数，取首位 ID 要按值返回，不能引用临时列表里的元素。
    const auto first = [&](const char *keyword, bool learned_order = true) {
        return ordered(keyword, learned_order)[0].pinyin;
    };
    const auto index_of = [](const std::vector<WordItem> &items, const std::string &format_id) {
        return static_cast<size_t>(
            std::find_if(items.begin(), items.end(), [&](const WordItem &item) { return item.pinyin == format_id; }) -
            items.begin());
    };

    REQUIRE_EQ(first("rq"), std::string("date:ymd_cn"));

    // 已在首位的格式没有可学的；第二位的按 pin 模式一次到顶。
    REQUIRE(user_dictionary::learn_date_time_selection(user_path, "date", default_ids, "date:ymd_cn", "pin", 1, 1));
    REQUIRE_EQ(first("rq"), std::string("date:ymd_cn"));
    REQUIRE(user_dictionary::learn_date_time_selection(user_path, "date", default_ids, "date:ymd_dash", "pin", 1, 1));
    auto items = ordered("rq");
    REQUIRE_EQ(items[0].pinyin, std::string("date:ymd_dash"));
    REQUIRE_EQ(items[0].word, std::string("2026-08-09"));
    REQUIRE_EQ(items[1].pinyin, std::string("date:ymd_cn"));
    // rq / riqi / date 共用一份顺序，时间那组不受影响。
    REQUIRE_EQ(first("riqi"), std::string("date:ymd_dash"));
    REQUIRE_EQ(first("sj"), std::string("time:hm"));

    // 计数到 trigger_count 才挪，linear 每次前移 linear_step 位。
    const size_t lunar = index_of(ordered("rq"), "date:lunar");
    REQUIRE(user_dictionary::learn_date_time_selection(user_path, "date", default_ids, "date:lunar", "linear", 3, 2));
    REQUIRE_EQ(index_of(ordered("rq"), "date:lunar"), lunar);
    REQUIRE(user_dictionary::learn_date_time_selection(user_path, "date", default_ids, "date:lunar", "linear", 3, 2));
    REQUIRE_EQ(index_of(ordered("rq"), "date:lunar"), lunar - 3);

    // 调频关闭时不学，也不用学到的顺序。
    REQUIRE(user_dictionary::learn_date_time_selection(user_path, "date", default_ids, "date:md_cn", "disabled", 1, 1));
    REQUIRE_EQ(first("rq"), std::string("date:ymd_dash"));
    REQUIRE_EQ(first("rq", false), std::string("date:ymd_cn"));

    // 置顶不计数。
    REQUIRE(user_dictionary::pin_date_time_format(user_path, "date", default_ids, "date:md_cn"));
    REQUIRE_EQ(first("rq"), std::string("date:md_cn"));
    // Shift+T 后面写的指定日期沿用同一组格式 ID，学到的顺序跟着走；只写年月时只有年月几种，不受影响。
    const auto christmas = ordered("20241225");
    REQUIRE_EQ(christmas[0].pinyin, std::string("date:md_cn"));
    REQUIRE_EQ(christmas[0].word, std::string("12月25日"));
    REQUIRE_EQ(first("2024/12"), std::string("date:ym_cn"));

    // 固定位置按格式 ID 摆，压过学到的顺序；取消后回到学到的顺序。
    const std::string context = user_dictionary::date_time_fixed_position_context("date");
    REQUIRE(user_dictionary::set_fixed_position(user_path, context, "date:ymd_slash", "date:ymd_slash", 1));
    items = ordered("rq");
    REQUIRE_EQ(items[0].pinyin, std::string("date:ymd_slash"));
    REQUIRE_EQ(items[0].fixed_position, 1);
    REQUIRE_EQ(items[1].pinyin, std::string("date:md_cn"));
    // 固定位置只挪位置，不多不少。格式表里还有唤醒词给不出的年月几种，所以拿未排序的结果比。
    REQUIRE_EQ(items.size(), metasequoia::local_modes::query_date_time("rq", &now).size());
    REQUIRE(user_dictionary::clear_fixed_position(user_path, context, "date:ymd_slash", "date:ymd_slash"));
    REQUIRE_EQ(first("rq"), std::string("date:md_cn"));
    items = ordered("rq");
    REQUIRE_EQ(items[0].fixed_position, 0);
    std::filesystem::remove_all(directory);
}
namespace
{
// 一份只有 tbl_1_n 的小词库：甲 100 > 乙 90 > ... > 己 50。
std::filesystem::path MakeLearningDictionary(const std::filesystem::path &directory)
{
    std::filesystem::create_directories(directory);
    const auto main_path = directory / "msime.db";
    TestDatabase db(main_path);
    db.exec("CREATE TABLE tbl_1_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
            "CREATE TABLE tbl_2_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
            "INSERT INTO tbl_1_n VALUES('ni','n','甲',100),('ni','n','乙',90),('ni','n','丙',80),"
            "('ni','n','丁',70),('ni','n','戊',60),('ni','n','己',50);");
    return main_path;
}

std::int64_t WeightOf(const std::filesystem::path &main_path, const char *sql)
{
    TestDatabase db(main_path);
    return db.scalar_int64(sql);
}

void SetLearnTick(const std::filesystem::path &user_path, std::int64_t tick)
{
    TestDatabase db(user_path);
    db.exec(("INSERT INTO dictionary_state(name,value) VALUES('learn_tick','" + std::to_string(tick) +
             "') ON CONFLICT(name) DO UPDATE SET value=excluded.value")
                .c_str());
}
} // namespace

// 公式照 librime：formula_d 的半衰期约 139 tick，formula_p 在 dee < 20 与 ≥ 20 两段连续。
TEST_CASE(UserDictionaryDecayFormulasFollowLibrime)
{
    namespace dyn = user_dictionary::dynamics;
    REQUIRE(std::abs(dyn::formula_d(1.0, 200.0, 1.0, 0.0) - (1.0 + std::exp(-1.0))) < 1e-12);
    REQUIRE(std::abs(dyn::decayed_dee(1.0, 0, 139) - 0.5) < 0.002);
    REQUIRE(dyn::decayed_dee(3.0, 50, 50) == 3.0);
    // 两段在 dee = 20 处连续
    const double kM = 1 / (1 - std::exp(-0.005));
    REQUIRE(std::abs(dyn::formula_p(0, 0, 1000, 19.999) - dyn::formula_p(0, 0, 1000, 20.001)) < 1e-3);
    REQUIRE(std::abs(dyn::formula_p(0, 0, 0, 20.0 - 1e-9) - 0.5 * (20.0 / kM)) < 1e-6);
    // 一次上屏约 ×11；阈值处倍率恰为 1；越热倍率越大、越冷越小，遗忘前平滑回落。
    REQUIRE(dyn::boost(1.0, 1, 10) > 8.0 && dyn::boost(1.0, 1, 10) < 14.0);
    REQUIRE(std::abs(dyn::boost(dyn::kForgetDee, 1, 10) - 1.0) < 1e-9);
    REQUIRE(dyn::boost(0.5, 1, 10) < dyn::boost(1.0, 1, 10));
    REQUIRE(dyn::boost(5.0, 5, 10) > dyn::boost(1.0, 1, 10));
    REQUIRE(dyn::boost(300.0, 300, 1000) > dyn::boost(30.0, 30, 1000));
    REQUIRE(!dyn::forgotten(dyn::decayed_dee(1.0, 0, 500)));
    REQUIRE(dyn::forgotten(dyn::decayed_dee(1.0, 0, 700)));
    // 换算回去：旧调频的倍率 -> dee -> 倍率
    for (double ratio : {1.5, 13.9, 250.0})
        REQUIRE(std::abs(dyn::boost(dyn::dee_for_boost(ratio, 1, 77), 1, 77) / ratio - 1.0) < 1e-6);
    REQUIRE_EQ(dyn::effective_weight(77000000, 5.0, 5, 10, 100000000), 100000000);
}

TEST_CASE(UserDictionaryLearnedCommitRaisesDecaysAndIsForgotten)
{
    const auto directory = test::TempRoot() / ("msime-decay-learning-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    const auto main_path = MakeLearningDictionary(directory);
    const auto user_path = directory / "msime_user.db";
    const std::string main = test::Utf8(main_path), user = test::Utf8(user_path);

    bool changed = false;
    REQUIRE(user_dictionary::learn_pinyin_commit(main, user, "ni", "己", &changed));
    REQUIRE(changed);
    const std::int64_t first = WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='己'");
    REQUIRE(first > 100); // 一次上屏就越过首位的出厂词频
    {
        TestDatabase journal(user_path);
        REQUIRE_EQ(journal.scalar_int("SELECT commits FROM user_dictionary_operations WHERE value='己'"), 1);
        REQUIRE_EQ(journal.scalar_int("SELECT tick FROM user_dictionary_operations WHERE value='己'"), 1);
        REQUIRE_EQ(journal.scalar_int("SELECT base_weight FROM user_dictionary_operations WHERE value='己'"), 50);
        REQUIRE_EQ(journal.scalar_int("SELECT CAST(dee AS INTEGER) FROM user_dictionary_operations WHERE value='己'"),
                   1);
        REQUIRE_EQ(journal.scalar_int("SELECT value FROM dictionary_state WHERE name='learn_tick'"), 1);
    }
    // 其余行不动
    REQUIRE_EQ(WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='甲'"), 100);

    // 衰减：全局上屏计数走到 139，dee 约减半，权重回落但仍高于出厂词频。
    SetLearnTick(user_path, 139);
    REQUIRE(user_dictionary::sweep_learned_entries(main, user));
    const std::int64_t halved = WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='己'");
    REQUIRE(halved < first);
    REQUIRE(halved > 50);

    // 遗忘：衰减到阈值以下，出厂词回到出厂词频，日志里不留这一行。
    SetLearnTick(user_path, 700);
    REQUIRE(user_dictionary::sweep_learned_entries(main, user));
    REQUIRE_EQ(WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='己'"), 50);
    {
        TestDatabase journal(user_path);
        REQUIRE_EQ(journal.scalar_int("SELECT COUNT(*) FROM user_dictionary_operations WHERE value='己'"), 0);
    }
    REQUIRE_EQ(WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='甲'"), 100);
    std::filesystem::remove_all(directory, ec);
}

// 直接上屏首选也计一次：dee 累积、权重升高，tick 与 commits 前进。
TEST_CASE(UserDictionaryCommittingTheTopCandidateReinforcesIt)
{
    const auto directory = test::TempRoot() / ("msime-decay-first-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    const auto main_path = MakeLearningDictionary(directory);
    const auto user_path = directory / "msime_user.db";
    const std::string main = test::Utf8(main_path), user = test::Utf8(user_path);
    const std::vector<WordItem> list = {{"ni", "甲", 100, CandidateSource::Database, "ni"},
                                        {"ni", "乙", 90, CandidateSource::Database, "ni"}};

    // 走 adjust_candidate_ranking（Server 与引擎的入口）：首选不再被跳过。
    REQUIRE(user_dictionary::adjust_candidate_ranking(main, user, "ni", list, "ni", "甲", "promote", 1, 1, false));
    const std::int64_t once = WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='甲'");
    REQUIRE(once > 100);
    REQUIRE(user_dictionary::adjust_candidate_ranking(main, user, "ni", list, "ni", "甲", "promote", 1, 1, false));
    REQUIRE(WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='甲'") > once);
    {
        TestDatabase journal(user_path);
        REQUIRE_EQ(journal.scalar_int("SELECT commits FROM user_dictionary_operations WHERE value='甲'"), 2);
        REQUIRE_EQ(journal.scalar_int("SELECT tick FROM user_dictionary_operations WHERE value='甲'"), 2);
        REQUIRE(journal.scalar_int("SELECT dee > 1.9 FROM user_dictionary_operations WHERE value='甲'"));
    }
    // 关闭调频时什么也不记。
    REQUIRE(user_dictionary::adjust_candidate_ranking(main, user, "ni", list, "ni", "乙", "disabled", 1, 1, false));
    REQUIRE_EQ(WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='乙'"), 90);
    std::filesystem::remove_all(directory, ec);
}

// 手动加的词、设置页改的权重、置顶写下的权重都不受衰减影响，出厂词没学过也不受影响。
TEST_CASE(UserDictionaryDecayLeavesFactoryManualAndPinnedWordsAlone)
{
    const auto directory = test::TempRoot() / ("msime-decay-owned-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    const auto main_path = MakeLearningDictionary(directory);
    const auto user_path = directory / "msime_user.db";
    const std::string main = test::Utf8(main_path), user = test::Utf8(user_path);
    {
        TestDatabase db(main_path);
        db.exec("INSERT INTO tbl_1_n VALUES('ni','n','手',10000),('ni','n','改',500);");
    }
    REQUIRE(user_dictionary::record_user_insert(user, user_dictionary::DictionaryKind::Pinyin, "ni", "手", 10000));
    REQUIRE(user_dictionary::record_weight_edit(user, user_dictionary::DictionaryKind::Pinyin, "ni", "改", 500));
    // 上屏手动词不改它；置顶 丁
    REQUIRE(user_dictionary::learn_pinyin_commit(main, user, "ni", "手"));
    REQUIRE(user_dictionary::learn_pinyin_commit(main, user, "ni", "改"));
    const std::vector<WordItem> list = {{"ni", "手", 10000, CandidateSource::Database, "ni"},
                                        {"ni", "丁", 70, CandidateSource::Database, "ni"}};
    REQUIRE(user_dictionary::adjust_candidate_ranking(main, user, "ni", list, "ni", "丁", "pin", 1, 1, true));
    const std::int64_t pinned = WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='丁'");
    REQUIRE(pinned > 10000);
    // 置顶之后再上屏它也不被学习（权重不动），遗忘时间再久也在
    REQUIRE(user_dictionary::learn_pinyin_commit(main, user, "ni", "丁"));
    SetLearnTick(user_path, 1000000);
    REQUIRE(user_dictionary::sweep_learned_entries(main, user));
    REQUIRE_EQ(WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='手'"), 10000);
    REQUIRE_EQ(WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='改'"), 500);
    REQUIRE_EQ(WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='丁'"), pinned);
    REQUIRE_EQ(WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='甲'"), 100);
    {
        TestDatabase journal(user_path);
        REQUIRE_EQ(journal.scalar_int("SELECT COUNT(*) FROM user_dictionary_operations"), 3);
        REQUIRE_EQ(journal.scalar_int("SELECT commits FROM user_dictionary_operations WHERE value='手'"), 0);
        REQUIRE_EQ(journal.scalar_int("SELECT commits FROM user_dictionary_operations WHERE value='丁'"), -1);
    }
    std::filesystem::remove_all(directory, ec);
}

// 整句造词记成学习条目：遗忘后从词库里消失；删除后再造，从头累积（删除恢复）。
TEST_CASE(UserDictionaryLearnedPhraseIsForgottenAndRevivesAfterDeletion)
{
    const auto directory = test::TempRoot() / ("msime-decay-phrase-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    const auto main_path = MakeLearningDictionary(directory);
    const auto user_path = directory / "msime_user.db";
    const std::string main = test::Utf8(main_path), user = test::Utf8(user_path);
    const auto learn_phrase = [&]() {
        {
            TestDatabase db(main_path);
            db.exec("INSERT INTO tbl_2_n VALUES('ni''hao','nh','拟好',10000);");
        }
        REQUIRE(user_dictionary::record_learned_phrase(user, "ni'hao", "拟好"));
    };
    learn_phrase();
    {
        TestDatabase journal(user_path);
        REQUIRE_EQ(journal.scalar_int("SELECT user_inserted FROM user_dictionary_operations WHERE value='拟好'"), 1);
        REQUIRE_EQ(journal.scalar_int("SELECT commits FROM user_dictionary_operations WHERE value='拟好'"), 1);
        // 刚落库时权重等于词库里写的 10000
        REQUIRE(std::abs(journal.scalar_int64("SELECT weight FROM user_dictionary_operations WHERE value='拟好'") -
                         10000) <= 10);
    }
    // 再上屏一次它（词库里有这一行了）：commits 2，权重高于 10000。
    REQUIRE(user_dictionary::learn_pinyin_commit(main, user, "ni'hao", "拟好"));
    REQUIRE(WeightOf(main_path, "SELECT weight FROM tbl_2_n WHERE value='拟好'") > 10000);

    // 删除：词库行没了，日志记成 delete，学习状态清零；此时上屏（没有行）什么也不记。
    REQUIRE(user_dictionary::delete_dictionary_candidate(main, user, user_dictionary::DictionaryKind::Pinyin, "ni'hao",
                                                         "拟好"));
    {
        TestDatabase journal(user_path);
        REQUIRE_EQ(journal.scalar_int("SELECT commits FROM user_dictionary_operations WHERE value='拟好'"), 0);
        REQUIRE_EQ(journal.scalar_int("SELECT operation='delete' FROM user_dictionary_operations WHERE "
                                      "value='拟好'"),
                   1);
    }
    REQUIRE(user_dictionary::learn_pinyin_commit(main, user, "ni'hao", "拟好"));
    REQUIRE_EQ(WeightOf(main_path, "SELECT COUNT(*) FROM tbl_2_n"), 0);

    // 再次造出来：恢复成 upsert，从头累积（commits 1）。
    learn_phrase();
    {
        TestDatabase journal(user_path);
        REQUIRE_EQ(journal.scalar_int("SELECT operation='upsert' FROM user_dictionary_operations WHERE "
                                      "value='拟好'"),
                   1);
        REQUIRE_EQ(journal.scalar_int("SELECT commits FROM user_dictionary_operations WHERE value='拟好'"), 1);
    }
    REQUIRE_EQ(WeightOf(main_path, "SELECT COUNT(*) FROM tbl_2_n"), 1);

    // 久不再用：遗忘，词库行与日志行一起消失。
    SetLearnTick(user_path, 5000);
    REQUIRE(user_dictionary::sweep_learned_entries(main, user));
    REQUIRE_EQ(WeightOf(main_path, "SELECT COUNT(*) FROM tbl_2_n"), 0);
    {
        TestDatabase journal(user_path);
        REQUIRE_EQ(journal.scalar_int("SELECT COUNT(*) FROM user_dictionary_operations"), 0);
    }
    std::filesystem::remove_all(directory, ec);
}

// 衰减状态随升级回放写回：衰减后的权重换算到新装的出厂词库上；过期的不写回；旧版整句造词的长条目
// 迁移成学习条目；旧调频的权重迁移成初始 dee；短的旧整句（与设置页手动加的分不开）保持永久。
TEST_CASE(UserDictionaryReplayCarriesTheLearningState)
{
    const auto directory = test::TempRoot() / ("msime-decay-replay-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory);
    const auto user_path = directory / "msime_user.db";
    const std::string user = test::Utf8(user_path);
    const auto english_path = directory / "english.db";
    {
        TestDatabase english_db(english_path);
        english_db.exec("CREATE TABLE english_words(word TEXT PRIMARY KEY,display TEXT);");
    }
    const auto fresh_dictionary = [&]() {
        const auto main_path = directory / "msime.db";
        std::filesystem::remove(main_path, ec);
        auto path = MakeLearningDictionary(directory);
        return path;
    };
    // 学一次己和戊、造一个整句；全局上屏计数随后推到 2000，这些条目回放时都已过期。
    auto main_path = fresh_dictionary();
    const std::string main = test::Utf8(main_path);
    REQUIRE(user_dictionary::learn_pinyin_commit(main, user, "ni", "己"));
    REQUIRE(user_dictionary::learn_pinyin_commit(main, user, "ni", "戊"));
    {
        TestDatabase db(main_path);
        db.exec("INSERT INTO tbl_2_n VALUES('ni''hao','nh','拟好',10000);");
    }
    REQUIRE(user_dictionary::record_learned_phrase(user, "ni'hao", "拟好"));
    // 旧版留下的：八音节整句（user_inserted、10000）、五音节整句、旧调频提升的丁（出厂 70 → 7000）
    REQUIRE(user_dictionary::record_user_insert(user, user_dictionary::DictionaryKind::Pinyin,
                                                "ni'ni'ni'ni'ni'ni'ni'ni", "你你你你你你你你", 10000));
    REQUIRE(user_dictionary::record_user_insert(user, user_dictionary::DictionaryKind::Pinyin, "ni'ni'ni'ni'ni",
                                                "你你你你你", 10000));
    REQUIRE(user_dictionary::record_upsert(user, user_dictionary::DictionaryKind::Pinyin, "ni", "丁", 7000));
    {
        // 模拟旧日志：这些行没有学习状态列的值
        TestDatabase journal(user_path);
        journal.exec("UPDATE user_dictionary_operations SET commits=0,dee=0,tick=0,base_weight=0 WHERE value IN "
                     "('你你你你你你你你','你你你你你','丁')");
        journal.exec("INSERT INTO dictionary_state(name,value) VALUES('learn_tick','2000') "
                     "ON CONFLICT(name) DO UPDATE SET value=excluded.value");
    }

    // 新装一份出厂词库（己的出厂词频换了：50 -> 500）再回放。
    main_path = fresh_dictionary();
    {
        TestDatabase db(main_path);
        db.exec("UPDATE tbl_1_n SET weight=500 WHERE value='己';"
                "CREATE TABLE tbl_others_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);"
                "CREATE TABLE tbl_5_n(key TEXT,jp TEXT,value TEXT,weight INTEGER);");
    }
    const auto replay = user_dictionary::replay(user, test::Utf8(main_path), test::Utf8(english_path));
    REQUIRE(replay.error.empty());
    REQUIRE_EQ(replay.failed, 0);
    // 己：tick 1 的条目在 2000 个 tick 之后已经过期，不写回，保持新出厂词频；戊同理。
    REQUIRE_EQ(WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='己'"), 500);
    REQUIRE_EQ(WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='戊'"), 60);
    REQUIRE(replay.forgotten >= 3);
    // 丁：旧调频 70 -> 7000（100 倍）迁移成学习条目，权重不丢（换算回 7000 上下）。
    REQUIRE(std::abs(WeightOf(main_path, "SELECT weight FROM tbl_1_n WHERE value='丁'") - 7000) <= 2);
    REQUIRE(replay.migrated >= 2);
    {
        TestDatabase journal(user_path);
        REQUIRE_EQ(journal.scalar_int("SELECT commits FROM user_dictionary_operations WHERE value='丁'"), 1);
        REQUIRE_EQ(journal.scalar_int("SELECT base_weight FROM user_dictionary_operations WHERE value='丁'"), 70);
        // 八音节的旧整句迁移成学习条目（commits 1），五音节的原样保留（永久）。
        REQUIRE_EQ(journal.scalar_int("SELECT commits FROM user_dictionary_operations WHERE "
                                      "value='你你你你你你你你'"),
                   1);
        REQUIRE_EQ(journal.scalar_int("SELECT commits FROM user_dictionary_operations WHERE value='你你你你你'"), 0);
    }
    std::filesystem::remove_all(directory, ec);
}

// 用真实用户学习库的只读副本做回放（副本放在工作树外，见 MSIME_LEARN_REPLAY_SRC）：目录里要有
// msime_user.db（用户日志副本）、msime.db（出厂词库）、english.db、dictionary-manifest.json。
// 回放在同级 replay 目录的拷贝上做，不碰源目录；没有设置环境变量时跳过。
TEST_CASE(UserDictionaryReplayOfARealLearningJournalKeepsTheLearning)
{
    const char *source_env = std::getenv("MSIME_LEARN_REPLAY_SRC");
    if (source_env == nullptr || *source_env == '\0')
        return;
    const std::filesystem::path source(source_env);
    const auto work = source.parent_path() / "replay";
    std::error_code ec;
    std::filesystem::remove_all(work, ec);
    std::filesystem::create_directories(work);
    for (const char *name : {"msime_user.db", "msime.db", "english.db", "dictionary-manifest.json"})
        std::filesystem::copy_file(source / name, work / name, std::filesystem::copy_options::overwrite_existing);
    const auto user_path = work / "msime_user.db";
    const auto main_path = work / "msime.db";

    // 回放前的日志与出厂词频。
    struct Legacy
    {
        std::string key, value;
        std::int64_t weight;
        bool user_inserted;
        std::int64_t shipped;
    };
    std::vector<Legacy> rows;
    int total = 0, pinyin_total = 0, kept_short_phrases = 0, long_phrases = 0, tuned_up = 0, demoted = 0;
    {
        sqlite3 *journal = nullptr, *factory = nullptr;
        REQUIRE_EQ(sqlite3_open_v2(test::Utf8(user_path).c_str(), &journal, SQLITE_OPEN_READONLY, nullptr), SQLITE_OK);
        REQUIRE_EQ(sqlite3_open_v2(test::Utf8(main_path).c_str(), &factory, SQLITE_OPEN_READONLY, nullptr), SQLITE_OK);
        sqlite3_stmt *stmt = nullptr;
        REQUIRE_EQ(sqlite3_prepare_v2(journal,
                                      "SELECT dictionary,key,value,weight,user_inserted,weight_edited,operation FROM "
                                      "user_dictionary_operations",
                                      -1, &stmt, nullptr),
                   SQLITE_OK);
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            ++total;
            const std::string kind = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
            const std::string operation = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
            if (kind != "pinyin" || operation != "upsert")
                continue;
            ++pinyin_total;
            Legacy row{reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)),
                       reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2)), sqlite3_column_int64(stmt, 3),
                       sqlite3_column_int(stmt, 4) != 0, 0};
            const bool edited = sqlite3_column_int(stmt, 5) != 0;
            const std::size_t syllables =
                static_cast<std::size_t>(std::count(row.key.begin(), row.key.end(), '\'')) + 1;
            const std::string table = syllables >= 8 ? "tbl_others_" + row.key.substr(0, 1)
                                                     : "tbl_" + std::to_string(syllables) + "_" + row.key.substr(0, 1);
            sqlite3_stmt *shipped = nullptr;
            if (sqlite3_prepare_v2(factory, ("SELECT MAX(weight) FROM " + table + " WHERE key=?1 AND value=?2").c_str(),
                                   -1, &shipped, nullptr) == SQLITE_OK)
            {
                sqlite3_bind_text(shipped, 1, row.key.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(shipped, 2, row.value.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(shipped) == SQLITE_ROW && sqlite3_column_type(shipped, 0) != SQLITE_NULL)
                    row.shipped = sqlite3_column_int64(shipped, 0);
            }
            sqlite3_finalize(shipped);
            if (row.user_inserted && row.weight == 10000 && !edited)
                (syllables > 7 ? long_phrases : kept_short_phrases) += 1;
            else if (!row.user_inserted && !edited && row.shipped > 0)
                (row.weight > row.shipped ? tuned_up : demoted) += 1;
            if (!row.user_inserted && !edited && row.shipped > 0 && row.weight > row.shipped)
                rows.push_back(row);
        }
        sqlite3_finalize(stmt);
        sqlite3_close(journal);
        sqlite3_close(factory);
    }

    const auto replay =
        user_dictionary::replay(test::Utf8(user_path), test::Utf8(main_path), test::Utf8(work / "english.db"));
    REQUIRE(replay.error.empty());
    REQUIRE_EQ(replay.failed, 0);
    std::cout << "[real-journal] rows=" << total << " pinyin_upserts=" << pinyin_total
              << " short_phrases_kept=" << kept_short_phrases << " long_phrases_migrated=" << long_phrases
              << " tuned_up=" << tuned_up << " demoted_dropped=" << demoted << " | replay applied=" << replay.applied
              << " skipped=" << replay.skipped << " learned=" << replay.learned << " migrated=" << replay.migrated
              << " forgotten=" << replay.forgotten << std::endl;
    REQUIRE_EQ(replay.migrated, long_phrases + tuned_up);
    REQUIRE_EQ(replay.forgotten, 0);

    // 旧调频抬高的权重换算后不丢：落在原权重上下（只有被 dee 封顶限制的才会更低）。
    int exact = 0, capped = 0;
    std::vector<std::int64_t> carried;
    const auto read_carried = [&](bool first) {
        sqlite3 *db = nullptr;
        REQUIRE_EQ(sqlite3_open_v2(test::Utf8(main_path).c_str(), &db, SQLITE_OPEN_READONLY, nullptr), SQLITE_OK);
        std::size_t index = 0;
        for (const auto &row : rows)
        {
            const std::size_t syllables =
                static_cast<std::size_t>(std::count(row.key.begin(), row.key.end(), '\'')) + 1;
            const std::string table = syllables >= 8 ? "tbl_others_" + row.key.substr(0, 1)
                                                     : "tbl_" + std::to_string(syllables) + "_" + row.key.substr(0, 1);
            sqlite3_stmt *stmt = nullptr;
            REQUIRE_EQ(sqlite3_prepare_v2(db, ("SELECT weight FROM " + table + " WHERE key=?1 AND value=?2").c_str(),
                                          -1, &stmt, nullptr),
                       SQLITE_OK);
            sqlite3_bind_text(stmt, 1, row.key.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, row.value.c_str(), -1, SQLITE_TRANSIENT);
            REQUIRE_EQ(sqlite3_step(stmt), SQLITE_ROW);
            const std::int64_t weight = sqlite3_column_int64(stmt, 0);
            sqlite3_finalize(stmt);
            REQUIRE(weight > row.shipped);
            if (first)
            {
                carried.push_back(weight);
                if (std::abs(weight - row.weight) <= 2)
                    ++exact;
                else
                {
                    REQUIRE(weight < row.weight); // 只会因封顶而偏低
                    ++capped;
                }
            }
            else
                REQUIRE_EQ(weight, carried[index]); // 重复回放不改变权重
            ++index;
        }
        sqlite3_close(db);
    };
    read_carried(true);
    std::cout << "[real-journal] tuned weights carried: exact=" << exact << " capped=" << capped << std::endl;

    // 再回放一次：不再有要迁移的，结果不变。
    const auto again =
        user_dictionary::replay(test::Utf8(user_path), test::Utf8(main_path), test::Utf8(work / "english.db"));
    REQUIRE(again.error.empty());
    REQUIRE_EQ(again.failed, 0);
    REQUIRE_EQ(again.migrated, 0);
    REQUIRE_EQ(again.learned, replay.learned);
    read_carried(false);
}
