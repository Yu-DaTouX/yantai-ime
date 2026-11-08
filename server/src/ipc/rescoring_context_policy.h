#pragma once

#include <cstddef>
#include <string>

namespace FanyImeIpc
{
// 本地神经整句重排的上屏历史；不用于网络请求。
inline constexpr std::size_t kRescoringContextMaxBytes = 1024;

inline std::string TrimRescoringContextFront(std::string context)
{
    if (context.size() <= kRescoringContextMaxBytes)
        return context;
    std::size_t cut = context.size() - kRescoringContextMaxBytes;
    while (cut < context.size() && (static_cast<unsigned char>(context[cut]) & 0xC0) == 0x80)
        ++cut;
    context.erase(0, cut);
    return context;
}
} // namespace FanyImeIpc
