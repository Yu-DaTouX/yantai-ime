#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utf8.h>

namespace FanyImeIpc
{
// Candidate bytes come from dictionaries, not the keyboard. A malformed row
// must not unwind out of the input worker or be passed to a learning write.
// Recovery owns the page/cache invalidation; it must not reset the composition
// or reconnect the transport for a data error.
template <typename Recover>
std::optional<std::size_t> CandidateTextLength(const std::string &text, Recover &&recover)
{
    try
    {
        return static_cast<std::size_t>(utf8::distance(text.begin(), text.end()));
    }
    catch (const utf8::exception &)
    {
        recover();
        return std::nullopt;
    }
}
} // namespace FanyImeIpc
