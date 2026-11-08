#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace msimeui
{
// UTF-16 区间，语义同 DWRITE_TEXT_RANGE。
struct TextRange
{
    std::uint32_t start = 0;
    std::uint32_t length = 0;
};

enum class EmojiRunKind
{
    // Extended_Pictographic 主体，连同附着的 FE0F 变体选择符、20E3 键帽、ZWJ 序列、
    // 肤色修饰符与标签字符（如 🏴 + 英格兰的 tag 序列）。
    Pictographic,
    // 两个区域指示符（U+1F1E6–U+1F1FF）拼成的国旗。
    RegionalFlag,
};

struct EmojiRun
{
    std::uint32_t start = 0;
    std::uint32_t length = 0;
    EmojiRunKind kind = EmojiRunKind::Pictographic;
};

// 纯函数：扫一遍 text，返回需要单独指定 emoji 字体的 UTF-16 区间。
//
// 只认 Unicode 的 Extended_Pictographic，外加两种序列：键帽（[0-9#*] + FE0F? + 20E3，整串
// 一起交给 emoji 字体，否则基字符留在主字体里画出来仍是普通数字）与区域旗帜（成对的区域指示符）。
// 普通汉字、数字、标点与字符串里的孤立 FE0F 之外的字符都不会命中，因此主字体与回退字体照旧。
// 相邻且同类别的区间会合并；返回值按 start 递增排列，区间之间至少隔一个码位。
std::vector<EmojiRun> FindEmojiRuns(const std::wstring &text);
} // namespace msimeui
