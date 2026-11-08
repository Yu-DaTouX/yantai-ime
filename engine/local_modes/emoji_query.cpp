#include "../contracts/assets/assets.h"
#include "emoji_query.h"
#include "local_database.h"

#include "../core/data_path.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <vector>

namespace metasequoia::local_modes
{
namespace
{
bool valid_code(const std::string &code)
{
    return !code.empty() && std::all_of(code.begin(), code.end(), [](unsigned char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '\'';
    });
}

std::string lower_ascii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return text;
}

LocalQueryResult query_failure(const char *diagnostic)
{
    return {{}, std::string(diagnostic)};
}

// 国旗 emoji 是两个区域指示符（U+1F1E6–U+1F1FF）拼成的：Windows 的 Segoe UI Emoji 把单个区域指示符
// 画成带框的字母，候选窗里 🇨🇳 这样的国旗因此显示成「CN」两个方框字母。只收由区域指示符组成的串，
// 🏳️ 这类由其它码位画出来的旗照常保留。
bool is_regional_indicator_flag(const std::string &text)
{
    if (text.size() < 8 || text.size() % 4 != 0)
    {
        return false;
    }
    for (std::size_t index = 0; index < text.size(); index += 4)
    {
        const auto first = static_cast<unsigned char>(text[index]);
        const auto second = static_cast<unsigned char>(text[index + 1]);
        const auto third = static_cast<unsigned char>(text[index + 2]);
        const auto fourth = static_cast<unsigned char>(text[index + 3]);
        if (first != 0xF0 || second != 0x9F || third != 0x87 || fourth < 0xA6 || fourth > 0xBF)
        {
            return false;
        }
    }
    return true;
}

LocalQueryResult run_emoji_query(const std::string &code, SchemeType /*scheme*/,
                                 const std::filesystem::path &database_path, int limit, bool exact_only)
{
    if (!valid_code(code) || limit <= 0)
    {
        return {};
    }
    if (database_path.empty())
    {
        return query_failure("Emoji database is unavailable.");
    }

    const std::string lower = lower_ascii(code);
    std::vector<RankedLookup> lookups{{lower, true}};

    const std::shared_ptr<sqlite3> database = open_local_database(database_path);
    if (!database)
    {
        return query_failure("Emoji database is unavailable.");
    }

    // 精确命中的键排在更长的键之前。只按目录顺序（sort_order）排会把短码挤下去——
    // ku 会先出 kuxiao（苦笑）→😅，而 哭→😢 的目录位置更靠后；精确行排在 LIMIT 之前，不会被前缀行截掉。
    constexpr const char *kPrefixSql = "SELECT emoji,sort_order,key IS ?1 FROM emoji_pinyin WHERE key>=?1 AND key<?2 "
                                       "ORDER BY key IS NOT ?1,sort_order LIMIT ?3";
    constexpr const char *kExactSql = "SELECT emoji,sort_order,1 FROM emoji_pinyin WHERE key=?1 AND key<?2 "
                                      "ORDER BY sort_order LIMIT ?3";
    const auto entries = query_ranked_entries(database.get(), exact_only ? kExactSql : kPrefixSql, lookups, limit);
    if (!entries)
    {
        return query_failure("Emoji database could not be queried.");
    }
    std::vector<std::string> visible;
    visible.reserve(entries->size());
    for (const auto &entry : *entries)
    {
        if (!is_regional_indicator_flag(entry))
        {
            visible.push_back(entry);
        }
    }
    const std::size_t count = visible.size();
    LocalQueryResult result;
    result.candidates.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        result.candidates.emplace_back(lower, visible[index], static_cast<std::int64_t>(count - index),
                                       CandidateSource::Emoji);
    }
    return result;
}
} // namespace

LocalQueryResult query_emoji(const std::string &code, SchemeType scheme, int limit)
{
    return query_emoji(code, scheme, data_file_path(metasequoia::assets::other_dictionary), limit);
}

LocalQueryResult query_emoji(const std::string &code, SchemeType scheme, const std::filesystem::path &database_path,
                             int limit)
{
    return run_emoji_query(code, scheme, database_path, limit, false);
}

LocalQueryResult query_mixed_emoji(const std::string &code, SchemeType scheme,
                                   const std::filesystem::path &database_path, int limit)
{
    return run_emoji_query(code, scheme, database_path, limit, true);
}
} // namespace metasequoia::local_modes
