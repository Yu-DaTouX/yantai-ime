#include "settings/dictionary_manager.h"
#include "settings/dictionary_validation.h"
#include "engine/user_dictionary/user_dictionary_journal.h"
#include "tests/includes/test_framework.h"
#include "tests/includes/test_utf8_path.h"

#include <windows.h>
#include <sqlite3.h>
#include <stdlib.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

TEST_CASE(DictionaryFullPinyinValidationRejectsAbbreviatedSyllables)
{
    quanpin::Segments segments;
    std::string normalized;

    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("nihao", segments, normalized));
    REQUIRE_EQ(normalized, std::string("ni'hao"));
    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("ni hao", segments, normalized));
    REQUIRE_EQ(normalized, std::string("ni'hao"));
    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("xi'an", segments, normalized));
    REQUIRE_EQ(normalized, std::string("xi'an"));
    // ü 别名按词库的写法入库，否则查询归一后永远查不到这一行。
    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("xvan'lue", segments, normalized));
    REQUIRE_EQ(normalized, std::string("xuan'lve"));
    REQUIRE(!SettingsDictionary::Validation::NormalizeFullPinyin("nh", segments, normalized));
    REQUIRE(!SettingsDictionary::Validation::NormalizeFullPinyin("ni'h", segments, normalized));
    REQUIRE(!SettingsDictionary::Validation::NormalizeFullPinyin("ni'", segments, normalized));
}

TEST_CASE(DictionaryFullPinyinPicksSegmentationMatchingSyllableCount)
{
    quanpin::Segments segments;
    std::string normalized;

    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("xian", segments, normalized));
    REQUIRE_EQ(normalized, std::string("xian"));
    REQUIRE_EQ(segments.size(), static_cast<size_t>(1));

    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("xian", segments, normalized, 2));
    REQUIRE_EQ(normalized, std::string("xi'an"));
    REQUIRE_EQ(segments.size(), static_cast<size_t>(2));

    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("a'a'a'a'a'a'a'a", segments, normalized, 8));
    REQUIRE_EQ(normalized, std::string("a'a'a'a'a'a'a'a"));
    REQUIRE_EQ(segments.size(), static_cast<size_t>(8));

    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("abalatiyayunhai", segments, normalized, 7));
    REQUIRE_EQ(normalized, std::string("a'ba'la'ti'ya'yun'hai"));
    REQUIRE_EQ(segments.size(), static_cast<size_t>(7));
}

TEST_CASE(DictionaryFullPinyinPreservesExplicitSyllableBoundaries)
{
    struct Example
    {
        const char *input;
        const char *normalized;
    };
    const Example examples[] = {
        {"gong an", "gong'an"},
        {"gong an ji guan", "gong'an'ji'guan"},
        {"gong gong an quan", "gong'gong'an'quan"},
        {"xi an", "xi'an"},
        {" \tGONG  an \r\n", "gong'an"},
        {"gong ' an ji\tguan", "gong'an'ji'guan"},
        {"gong an'ji guan", "gong'an'ji'guan"},
    };
    for (const auto &example : examples)
    {
        const auto expected = quanpin::split_segments(example.normalized);
        for (size_t syllables : {size_t{0}, expected.size()})
        {
            quanpin::Segments segments;
            std::string normalized;
            REQUIRE(
                SettingsDictionary::Validation::NormalizeFullPinyin(example.input, segments, normalized, syllables));
            REQUIRE_EQ(normalized, std::string(example.normalized));
            REQUIRE_EQ(segments, expected);
        }
    }
}

TEST_CASE(DictionaryFullPinyinSegmentsWithoutCorrectingSpelling)
{
    quanpin::Segments segments;
    std::string normalized;
    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin("gongan", segments, normalized));
    REQUIRE_EQ(normalized, std::string("gong'an"));

    for (const char *input : {"g an", "gong ann", "gong a1", "'gong an", "gong an'", "gong''an", "shangh"})
        REQUIRE(!SettingsDictionary::Validation::NormalizeFullPinyin(input, segments, normalized));
}

TEST_CASE(QuickPhraseValidationMatchesNamedPipeWcharCapacity)
{
    REQUIRE(SettingsDictionary::Validation::QuickPhraseFitsNamedPipe(std::string(199, 'a')));
    REQUIRE(!SettingsDictionary::Validation::QuickPhraseFitsNamedPipe(std::string(200, 'a')));

    const std::string emoji = "\xF0\x9F\x98\x80";
    REQUIRE(SettingsDictionary::Validation::QuickPhraseFitsNamedPipe(std::string(197, 'a') + emoji));
    REQUIRE(!SettingsDictionary::Validation::QuickPhraseFitsNamedPipe(std::string(198, 'a') + emoji));
}

TEST_CASE(CodedDictionaryImportRequiresTabsAndPreservesSpacesInWords)
{
    std::string word;
    std::string code;
    std::string message;
    int weight = -1;

    REQUIRE(SettingsDictionary::Validation::ParseCodedImportLine("包含 空格的词\tbao'han'kong'ge'de'ci\t123", word,
                                                                 code, weight, message));
    REQUIRE_EQ(word, std::string("包含 空格的词"));
    REQUIRE_EQ(code, std::string("bao'han'kong'ge'de'ci"));
    REQUIRE_EQ(weight, 123);

    REQUIRE(!SettingsDictionary::Validation::ParseCodedImportLine("普通词 putongci 10", word, code, weight, message));
    REQUIRE(!SettingsDictionary::Validation::ParseCodedImportLine("普通词\tputongci\t10\textra", word, code, weight,
                                                                  message));
}

TEST_CASE(CodedDictionaryImportAcceptsOptionalWeightAndRimeUserdb)
{
    std::string word;
    std::string code;
    std::string message;
    int weight = -1;

    REQUIRE(SettingsDictionary::Validation::ParseCodedImportLine("普通词\tputongci", word, code, weight, message));
    REQUIRE_EQ(word, std::string("普通词"));
    REQUIRE_EQ(code, std::string("putongci"));
    REQUIRE_EQ(weight, SettingsDictionary::Validation::kDefaultPinyinImportWeight);

    REQUIRE(SettingsDictionary::Validation::ParseCodedImportLine("你好\tni hao\tc=3 d=0.12 t=12345", word, code, weight,
                                                                 message));
    REQUIRE_EQ(word, std::string("你好"));
    REQUIRE_EQ(code, std::string("ni hao"));
    REQUIRE_EQ(weight, SettingsDictionary::Validation::kDefaultPinyinImportWeight);

    REQUIRE(SettingsDictionary::Validation::ParseCodedImportLine("西安\txi an\tc=1", word, code, weight, message));
    REQUIRE_EQ(word, std::string("西安"));
    REQUIRE_EQ(code, std::string("xi an"));
    REQUIRE_EQ(weight, SettingsDictionary::Validation::kDefaultPinyinImportWeight);

    REQUIRE(
        !SettingsDictionary::Validation::ParseCodedImportLine("普通词\tputongci\tabc", word, code, weight, message));
}

TEST_CASE(ImportLineSkipWalksYamlFrontMatterAndComments)
{
    bool in_yaml_header = false;
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("", in_yaml_header));
    REQUIRE(!in_yaml_header);
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("   ", in_yaml_header));
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("# Rime user dictionary", in_yaml_header));
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("#@/db_name\tluna_pinyin", in_yaml_header));
    REQUIRE(!SettingsDictionary::Validation::ShouldSkipImportLine("你好\tni hao\t1", in_yaml_header));

    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("---", in_yaml_header));
    REQUIRE(in_yaml_header);
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("name: luna_pinyin", in_yaml_header));
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("sort: by_weight", in_yaml_header));
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("  ---", in_yaml_header));
    REQUIRE(in_yaml_header);
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("...", in_yaml_header));
    REQUIRE(!in_yaml_header);
    REQUIRE(!SettingsDictionary::Validation::ShouldSkipImportLine("你好\tni hao", in_yaml_header));
    REQUIRE(SettingsDictionary::Validation::ShouldSkipImportLine("  # comment after body", in_yaml_header));
}

TEST_CASE(CodedQuanpinImportAcceptsExportedApostropheTsv)
{
    std::string word;
    std::string code;
    std::string message;
    int weight = -1;
    quanpin::Segments segments;
    std::string normalized;

    REQUIRE(SettingsDictionary::Validation::ParseCodedImportLine("啊啊啊啊啊啊啊啊\ta'a'a'a'a'a'a'a\t41", word, code,
                                                                 weight, message));
    REQUIRE_EQ(word, std::string("啊啊啊啊啊啊啊啊"));
    REQUIRE_EQ(code, std::string("a'a'a'a'a'a'a'a"));
    REQUIRE_EQ(weight, 41);
    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin(code, segments, normalized, 8));
    REQUIRE_EQ(segments.size(), static_cast<size_t>(8));

    REQUIRE(SettingsDictionary::Validation::ParseCodedImportLine("阿巴拉提亚云海\ta'ba'la'ti'ya'yun'hai\t13", word,
                                                                 code, weight, message));
    REQUIRE_EQ(word, std::string("阿巴拉提亚云海"));
    REQUIRE_EQ(code, std::string("a'ba'la'ti'ya'yun'hai"));
    REQUIRE_EQ(weight, 13);
    REQUIRE(SettingsDictionary::Validation::NormalizeFullPinyin(code, segments, normalized, 7));
    REQUIRE_EQ(normalized, std::string("a'ba'la'ti'ya'yun'hai"));
    REQUIRE_EQ(segments.size(), static_cast<size_t>(7));
}

namespace
{
// 指向一个空目录，让每条分支都在写库之前就返回。
//
// 必须同时写两处环境变量，否则会静默污染用户真实词库：
//   - server 的 get_ime_data_path() 走 GetEnvironmentVariableW，读 Win32 环境块；
//   - 引擎的 data_file_path()（default_user_db_path 用它）走 _wdupenv_s，读 CRT 副本。
// 只调 SetEnvironmentVariableW 的话前者生效、后者看不到，引擎侧仍指向真实数据目录，
// record_user_insert 就把测试数据写进了 %LOCALAPPDATA% 的真实词库。
// ime_paths 每次调用都重读、不缓存，所以改动不会漏给别的用例。
class ScopedDataDir
{
  public:
    explicit ScopedDataDir(const std::filesystem::path &path)
    {
        wchar_t buffer[32768];
        const DWORD length = GetEnvironmentVariableW(kName, buffer, 32768);
        had_previous_ = length != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
        previous_.assign(buffer, length);

        const std::wstring value = path.wstring();
        SetEnvironmentVariableW(kName, value.c_str());
        _wputenv_s(kName, value.c_str());
    }
    ~ScopedDataDir()
    {
        SetEnvironmentVariableW(kName, had_previous_ ? previous_.c_str() : nullptr);
        _wputenv_s(kName, had_previous_ ? previous_.c_str() : L"");
    }

    ScopedDataDir(const ScopedDataDir &) = delete;
    ScopedDataDir &operator=(const ScopedDataDir &) = delete;

  private:
    static constexpr const wchar_t *kName = L"METASEQUOIA_IME_DATA_DIR";
    std::wstring previous_;
    bool had_previous_ = false;
};

// 临时数据目录，析构时整棵删掉。PID 后缀不是洁癖：目录开场会 remove_all，固定名在
// 并行 ctest 或多个工作树同时跑时会互相删掉——同样的理由见 test_caret_prefix_input_session.cpp:27。
class ScopedTempDir
{
  public:
    explicit ScopedTempDir(const wchar_t *name)
        : path_(test::TempRoot() / (std::wstring(name) + L"-" + std::to_wstring(GetCurrentProcessId())))
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        std::filesystem::create_directories(path_, error);
    }
    ~ScopedTempDir()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    ScopedTempDir(const ScopedTempDir &) = delete;
    ScopedTempDir &operator=(const ScopedTempDir &) = delete;

    const std::filesystem::path &path() const
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE(QuanpinImportAndQueryKeepExplicitSyllableBoundaries)
{
    const ScopedTempDir dir(L"msime-quanpin-syllable-boundaries");
    const ScopedDataDir scoped(dir.path());
    sqlite3 *raw = nullptr;
    const int opened = sqlite3_open(test::Utf8(dir.path() / L"msime.db").c_str(), &raw);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw, sqlite3_close);
    REQUIRE_EQ(opened, SQLITE_OK);

    struct Entry
    {
        const char *code;
        const char *key;
        const char *word;
    };
    const Entry entries[] = {
        {"gong an", "gong'an", "公安"},
        {"gong an ji guan", "gong'an'ji'guan", "公安机关"},
        {"gong gong an quan", "gong'gong'an'quan", "公共安全"},
        {"xi an", "xi'an", "西安"},
    };
    std::string content;
    for (const auto &entry : entries)
    {
        const std::string table = quanpin::build_table_name(quanpin::split_segments(entry.key));
        const std::string sql = "CREATE TABLE IF NOT EXISTS \"" + table +
                                "\"(key TEXT NOT NULL,jp TEXT NOT NULL,value TEXT NOT NULL,"
                                "weight INTEGER NOT NULL,UNIQUE(key,value))";
        REQUIRE_EQ(sqlite3_exec(db.get(), sql.c_str(), nullptr, nullptr, nullptr), SQLITE_OK);
        content += std::string(entry.word) + '\t' + entry.code + "\t42\n";
    }

    const auto imported =
        SettingsDictionary::HandleRequest({{"dictionary", "quanpin"}, {"action", "import"}, {"content", content}});
    user_dictionary::close_default_user_database();
    REQUIRE(imported.at("ok").as_bool());
    REQUIRE_EQ(std::string(imported.at("message").as_string()), std::string("成功导入 4 条"));

    for (const auto &entry : entries)
    {
        for (const char *code : {entry.code, entry.key})
        {
            const auto response =
                SettingsDictionary::HandleRequest({{"dictionary", "quanpin"}, {"action", "query"}, {"code", code}});
            REQUIRE(response.at("ok").as_bool());
            const auto &rows = response.at("rows").as_array();
            REQUIRE_EQ(rows.size(), size_t{1});
            const auto &row = rows.front().as_object();
            REQUIRE_EQ(std::string(row.at("code").as_string()), std::string(entry.key));
            REQUIRE_EQ(std::string(row.at("word").as_string()), std::string(entry.word));
            REQUIRE_EQ(row.at("weight").as_int64(), 42);
        }
    }
}

namespace
{
int CountRows(sqlite3 *db, const std::string &sql)
{
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        sqlite3_finalize(stmt);
        return -1;
    }
    const int count = sqlite3_step(stmt) == SQLITE_ROW ? sqlite3_column_int(stmt, 0) : -1;
    sqlite3_finalize(stmt);
    return count;
}

std::string Message(const boost::json::object &response)
{
    return std::string(response.at("message").as_string());
}

std::vector<std::string> LatinRows(const char *code)
{
    const auto response =
        SettingsDictionary::HandleRequest({{"dictionary", "latin"}, {"action", "query"}, {"code", code}});
    REQUIRE(response.at("ok").as_bool());
    std::vector<std::string> rows;
    for (const auto &row : response.at("rows").as_array())
        rows.push_back(std::string(row.as_object().at("word").as_string()) + "|" +
                       std::string(row.as_object().at("code").as_string()));
    return rows;
}
} // namespace

// 字母词（T恤、卡拉OK）：拼音页导入时含英文字母的词分流成字母词，不再硬塞进拼音表；字母词页能增删改查、
// 导出；内置的只读；词库是还没有 latin_words 的旧版本时照样能加（设置页补建表）。
TEST_CASE(LatinWordsAreImportedRoutedAndManagedInSettings)
{
    const ScopedTempDir dir(L"msime-latin-words-settings");
    const ScopedDataDir scoped(dir.path());
    sqlite3 *raw = nullptr;
    const int opened = sqlite3_open(test::Utf8(dir.path() / L"msime.db").c_str(), &raw);
    const std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db(raw, sqlite3_close);
    REQUIRE_EQ(opened, SQLITE_OK);
    REQUIRE_EQ(sqlite3_exec(db.get(),
                            "CREATE TABLE tbl_2_t(key TEXT NOT NULL,jp TEXT NOT NULL,value TEXT NOT NULL,"
                            "weight INTEGER NOT NULL,UNIQUE(key,value))",
                            nullptr, nullptr, nullptr),
               SQLITE_OK);

    // 第 4 行是以前硬塞进拼音表的写法，现在要被拒：T 对不上 te。
    const std::string content = "特许\tte xu\t5\n"
                                "T恤\tT xu\t9\n"
                                "卡拉OK\tkǎ'lā'OK\n"
                                "T恤\tte'xu\n";
    const auto imported =
        SettingsDictionary::HandleRequest({{"dictionary", "quanpin"}, {"action", "import"}, {"content", content}});
    user_dictionary::close_default_user_database();
    REQUIRE(imported.at("ok").as_bool());
    REQUIRE(Message(imported).rfind("成功导入 3 条，失败 1 条。第 4 行：", 0) == 0);
    REQUIRE_EQ(CountRows(db.get(), "SELECT count(*) FROM tbl_2_t WHERE value='特许'"), 1);
    REQUIRE_EQ(CountRows(db.get(), "SELECT count(*) FROM tbl_2_t WHERE value='T恤'"), 0);
    REQUIRE_EQ(LatinRows("txu"), std::vector<std::string>{"T恤|t'xu"});
    REQUIRE_EQ(LatinRows("kala"), std::vector<std::string>{"卡拉OK|ka'la'o'k"});

    // 字母词页新增；拼音页新增含字母的词也按字母词加。
    const auto create = [](const char *dictionary, const char *word, const char *code) {
        return SettingsDictionary::HandleRequest(
            {{"dictionary", dictionary}, {"action", "create"}, {"word", word}, {"code", code}, {"weight", 20}});
    };
    REQUIRE(create("latin", "X光", "x guang").at("ok").as_bool());
    REQUIRE_EQ(Message(create("latin", "X光", "X'guang")), std::string("词条已经存在"));
    REQUIRE(create("quanpin", "B超", "b chao").at("ok").as_bool());
    REQUIRE_EQ(LatinRows("bchao"), std::vector<std::string>{"B超|b'chao"});
    REQUIRE_EQ(CountRows(db.get(), "SELECT count(*) FROM sqlite_master WHERE name LIKE 'tbl_2_b'"), 0);
    REQUIRE(!create("latin", "X光3", "x guang 3").at("ok").as_bool());

    // 改名、改权重；内置的（只在 latin_words、不在日志里）不能改删。
    const auto update = SettingsDictionary::HandleRequest({{"dictionary", "latin"},
                                                           {"action", "update"},
                                                           {"oldCode", "x'guang"},
                                                           {"oldWord", "X光"},
                                                           {"word", "X线"},
                                                           {"code", "x xian"},
                                                           {"weight", 30}});
    REQUIRE(update.at("ok").as_bool());
    REQUIRE(LatinRows("xguang").empty());
    REQUIRE_EQ(LatinRows("xxian"), std::vector<std::string>{"X线|x'xian"});
    REQUIRE_EQ(sqlite3_exec(db.get(),
                            "INSERT INTO latin_words(key,value,weight,exact_only) VALUES('a''a''zhi','AA制',7,0)",
                            nullptr, nullptr, nullptr),
               SQLITE_OK);
    const auto remove_builtin = SettingsDictionary::HandleRequest(
        {{"dictionary", "latin"}, {"action", "delete"}, {"oldCode", "a'a'zhi"}, {"oldWord", "AA制"}});
    REQUIRE(!remove_builtin.at("ok").as_bool());
    REQUIRE_EQ(LatinRows("aazhi"), std::vector<std::string>{"AA制|a'a'zhi"});

    const auto exported = SettingsDictionary::HandleRequest({{"dictionary", "latin"}, {"action", "export"}});
    REQUIRE(exported.at("ok").as_bool());
    const std::string text(exported.at("content").as_string());
    REQUIRE(text.find("T恤\tt'xu\t9\n") != std::string::npos);
    REQUIRE(text.find("卡拉OK\tka'la'o'k\t10000\n") != std::string::npos);
    REQUIRE(text.find("B超\tb'chao\t20\n") != std::string::npos);
    REQUIRE(text.find("X线\tx'xian\t30\n") != std::string::npos);
    REQUIRE(text.find("AA制") == std::string::npos);

    const auto remove = SettingsDictionary::HandleRequest(
        {{"dictionary", "latin"}, {"action", "delete"}, {"oldCode", "x'xian"}, {"oldWord", "X线"}});
    REQUIRE(remove.at("ok").as_bool());
    REQUIRE(LatinRows("xxian").empty());
    REQUIRE_EQ(user_dictionary::user_latin_words(user_dictionary::default_user_db_path()).size(), size_t{3});

    // 导出的文件原样导回来：全都已存在。
    const auto again =
        SettingsDictionary::HandleRequest({{"dictionary", "latin"}, {"action", "import"}, {"content", text}});
    REQUIRE_EQ(Message(again), std::string("成功导入 1 条，跳过 3 条（已存在）"));
    user_dictionary::close_default_user_database();
}

TEST_CASE(HansImportIsRoutedByTheTargetDictionary)
{
    const ScopedTempDir dir(L"msime-empty-hans-import");
    const ScopedDataDir scoped(dir.path());

    const auto response_for = [](const char *dictionary) {
        return SettingsDictionary::HandleRequest(
            {{"dictionary", dictionary}, {"action", "importHans"}, {"content", "环境"}});
    };
    const auto message_of = [](const boost::json::object &response) {
        return std::string(response.at("message").as_string());
    };

    // 英文与快捷短语词库收不了纯汉字。改动前这两种请求会一路落到拼音分支，
    // 把汉字写进拼音表；必须明确报错，不能静默成功。
    for (const char *dictionary : {"english", "quick"})
    {
        const auto response = response_for(dictionary);
        REQUIRE(!response.at("ok").as_bool());
        REQUIRE_EQ(message_of(response), std::string("该词库不支持纯汉字导入"));
    }

    // 拼音走注音引擎：空数据目录下开不了库，但报错不能是「该词库不支持纯汉字导入」，
    // 否则说明请求落到了英文/快捷短语那条分支上。
    const auto quanpin = response_for("quanpin");
    REQUIRE(!quanpin.at("ok").as_bool());
    REQUIRE(message_of(quanpin) != "该词库不支持纯汉字导入");
}

namespace
{
class ScopedQuickPhraseDataDir
{
  public:
    ScopedQuickPhraseDataDir() : dir_(L"msime-quick-phrase-test"), scoped_(dir_.path()), db_(nullptr, sqlite3_close)
    {
        sqlite3 *db = nullptr;
        const int result = sqlite3_open(test::Utf8(dir_.path() / L"msime.db").c_str(), &db);
        db_.reset(db);
        REQUIRE_EQ(result, SQLITE_OK);
        exec("CREATE TABLE quick_parases(key TEXT NOT NULL, value TEXT NOT NULL,"
             "weight INTEGER NOT NULL DEFAULT 0, UNIQUE(key,value));"
             "INSERT INTO quick_parases VALUES('abc','测试短语',10),('abc','另一短语',20)");
    }
    ~ScopedQuickPhraseDataDir()
    {
        user_dictionary::close_default_user_database();
    }

    void exec(const char *sql)
    {
        REQUIRE_EQ(sqlite3_exec(db_.get(), sql, nullptr, nullptr, nullptr), SQLITE_OK);
    }

    boost::json::value rows() const
    {
        const auto response = SettingsDictionary::HandleRequest({{"dictionary", "quick"}, {"action", "query"}});
        REQUIRE(response.at("ok").as_bool());
        return response.at("rows");
    }

  private:
    ScopedTempDir dir_;
    ScopedDataDir scoped_;
    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> db_;
};
} // namespace

TEST_CASE(QuickPhraseDuplicateCreateReportsExistingPhrase)
{
    ScopedQuickPhraseDataDir data;
    const auto before = data.rows();
    const auto response = SettingsDictionary::HandleRequest(
        {{"dictionary", "quick"}, {"action", "create"}, {"code", "ABC"}, {"word", "测试短语"}, {"weight", 99}});

    REQUIRE(!response.at("ok").as_bool());
    REQUIRE_EQ(std::string(response.at("message").as_string()), std::string("相同编码和内容的快捷短语已存在"));
    REQUIRE_EQ(data.rows(), before);
    REQUIRE(!std::filesystem::exists(std::filesystem::u8path(user_dictionary::default_user_db_path())));
}

TEST_CASE(QuickPhraseDuplicateUpdatePreservesBothPhrases)
{
    ScopedQuickPhraseDataDir data;
    const auto before = data.rows();
    const auto response = SettingsDictionary::HandleRequest({{"dictionary", "quick"},
                                                             {"action", "update"},
                                                             {"code", "abc"},
                                                             {"word", "测试短语"},
                                                             {"weight", 99},
                                                             {"oldCode", "abc"},
                                                             {"oldWord", "另一短语"}});

    REQUIRE(!response.at("ok").as_bool());
    REQUIRE_EQ(std::string(response.at("message").as_string()), std::string("相同编码和内容的快捷短语已存在"));
    REQUIRE_EQ(data.rows(), before);
    REQUIRE(!std::filesystem::exists(std::filesystem::u8path(user_dictionary::default_user_db_path())));
}

TEST_CASE(QuickPhraseAllowsSharedCodesAndWeightUpdates)
{
    ScopedQuickPhraseDataDir data;
    for (const auto &request : {
             boost::json::object{{"dictionary", "quick"}, {"action", "create"}, {"code", "abc"}, {"word", "新短语"}},
             boost::json::object{{"dictionary", "quick"}, {"action", "create"}, {"code", "def"}, {"word", "测试短语"}},
             boost::json::object{{"dictionary", "quick"},
                                 {"action", "update"},
                                 {"code", "abc"},
                                 {"word", "测试短语"},
                                 {"weight", 99},
                                 {"oldCode", "abc"},
                                 {"oldWord", "测试短语"}},
         })
    {
        REQUIRE(SettingsDictionary::HandleRequest(request).at("ok").as_bool());
    }
    const auto rows = data.rows().as_array();
    REQUIRE_EQ(rows.size(), static_cast<size_t>(4));
    REQUIRE_EQ(std::string(rows.front().as_object().at("word").as_string()), std::string("测试短语"));
    REQUIRE_EQ(rows.front().as_object().at("weight").as_int64(), 99);
}

TEST_CASE(QuickPhraseOtherConstraintsKeepTheirErrorDetails)
{
    ScopedQuickPhraseDataDir data;
    data.exec("CREATE TRIGGER reject_quick_phrase BEFORE INSERT ON quick_parases "
              "BEGIN SELECT RAISE(ABORT, 'test rejection'); END");
    const auto before = data.rows();
    const auto response = SettingsDictionary::HandleRequest(
        {{"dictionary", "quick"}, {"action", "create"}, {"code", "abc"}, {"word", "新短语"}});

    REQUIRE(!response.at("ok").as_bool());
    REQUIRE_EQ(std::string(response.at("message").as_string()), std::string("新增失败：test rejection"));
    REQUIRE_EQ(data.rows(), before);
}
