#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace FanyImeTranslationMode
{
// Negotiated TranslationModeState payload: active \t revision [\t composition].
// The revision is Server-owned and monotonic within one focus session. Main and
// Worker pipes can be consumed in either order; an older snapshot cannot toggle
// the mode back. Focus/transport reset discards the entire local mirror.
struct Snapshot
{
    bool active = false;
    uint64_t revision = 0;
    std::wstring_view composition;
};
inline bool Parse(std::wstring_view payload, Snapshot &snapshot)
{
    if (payload.size() < 3 || (payload[0] != L'0' && payload[0] != L'1') || payload[1] != L'\t')
        return false;
    const auto end = payload.find(L'\t', 2);
    const auto number = payload.substr(2, end == std::wstring_view::npos ? end : end - 2);
    if (number.empty())
        return false;
    uint64_t revision = 0;
    for (const wchar_t c : number)
    {
        if (c < L'0' || c > L'9' || revision > ((std::numeric_limits<uint64_t>::max)() - (c - L'0')) / 10)
            return false;
        revision = revision * 10 + (c - L'0');
    }
    if (!revision)
        return false;
    snapshot = {payload[0] == L'1', revision,
                end == std::wstring_view::npos ? std::wstring_view{} : payload.substr(end + 1)};
    return true;
}
inline std::wstring Format(bool active, uint64_t revision)
{
    return std::wstring(active ? L"1\t" : L"0\t") + std::to_wstring(revision);
}
struct Mirror
{
    bool active = false;
    uint64_t revision = 0;
    bool Apply(const Snapshot &snapshot)
    {
        if (snapshot.revision < revision || (snapshot.revision == revision && snapshot.active != active))
            return false;
        active = snapshot.active;
        revision = snapshot.revision;
        return true;
    }
};
} // namespace FanyImeTranslationMode
