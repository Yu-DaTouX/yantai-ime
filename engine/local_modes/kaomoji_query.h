#pragma once

#include "local_query_result.h"
#include "../core/scheme_type.h"

#include <filesystem>
#include <string>

namespace metasequoia::local_modes
{
LocalQueryResult query_kaomoji(const std::string &code, SchemeType scheme, int limit = 10);
LocalQueryResult query_kaomoji(const std::string &code, SchemeType scheme, const std::filesystem::path &database_path,
                               int limit = 10);
} // namespace metasequoia::local_modes
