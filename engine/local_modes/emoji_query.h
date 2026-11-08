#pragma once

#include "local_query_result.h"
#include "../core/scheme_type.h"

#include <filesystem>
#include <string>

namespace metasequoia::local_modes
{
LocalQueryResult query_emoji(const std::string &code, SchemeType scheme, int limit = 10);
LocalQueryResult query_emoji(const std::string &code, SchemeType scheme, const std::filesystem::path &database_path,
                             int limit = 10);

// emoji 混输：只认整串精确等于某个键，不做前缀匹配。
LocalQueryResult query_mixed_emoji(const std::string &code, SchemeType scheme,
                                   const std::filesystem::path &database_path, int limit = 3);
} // namespace metasequoia::local_modes
