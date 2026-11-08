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
    // 区域指示符拼成的国旗照常出：候选窗随包装了带国旗图案的 Twemoji Mozilla，只给区域旗帜
    // 序列用私有字体渲染；Windows 自带的 Segoe UI Emoji 没有国旗图案，不再作为过滤的理由。
    const std::size_t count = entries->size();
    LocalQueryResult result;
    result.candidates.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        result.candidates.emplace_back(lower, (*entries)[index], static_cast<std::int64_t>(count - index),
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
