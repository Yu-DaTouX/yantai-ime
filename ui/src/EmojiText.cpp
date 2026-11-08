#include "msimeui/EmojiText.h"

#include "ControlsInternal.h"

#include <dwrite.h>

#include <cstddef>

namespace msimeui
{
namespace
{
constexpr std::uint32_t kZeroWidthJoiner = 0x200D;
constexpr std::uint32_t kVariationSelector16 = 0xFE0F;
constexpr std::uint32_t kCombiningEnclosingKeycap = 0x20E3;
constexpr std::uint32_t kEmojiModifierFirst = 0x1F3FB;
constexpr std::uint32_t kEmojiModifierLast = 0x1F3FF;
constexpr std::uint32_t kRegionalIndicatorFirst = 0x1F1E6;
constexpr std::uint32_t kRegionalIndicatorLast = 0x1F1FF;
constexpr std::uint32_t kTagCharacterFirst = 0xE0020;
constexpr std::uint32_t kTagCharacterLast = 0xE007F;

// Unicode 15.1 Extended_Pictographic（emoji-data.txt），相邻码位已合并。区域指示符不属于这个属性，
// 它们按国旗序列单独处理。
struct CodePointRange
{
    std::uint32_t first;
    std::uint32_t last;
};

constexpr CodePointRange kExtendedPictographic[] = {
    {0x00A9, 0x00A9},   {0x00AE, 0x00AE},   {0x203C, 0x203C},   {0x2049, 0x2049},   {0x2122, 0x2122},
    {0x2139, 0x2139},   {0x2194, 0x2199},   {0x21A9, 0x21AA},   {0x231A, 0x231B},   {0x2328, 0x2328},
    {0x2388, 0x2388},   {0x23CF, 0x23CF},   {0x23E9, 0x23F3},   {0x23F8, 0x23FA},   {0x24C2, 0x24C2},
    {0x25AA, 0x25AB},   {0x25B6, 0x25B6},   {0x25C0, 0x25C0},   {0x25FB, 0x25FE},   {0x2600, 0x2605},
    {0x2607, 0x2612},   {0x2614, 0x2685},   {0x2690, 0x2705},   {0x2708, 0x2712},   {0x2714, 0x2714},
    {0x2716, 0x2716},   {0x271D, 0x271D},   {0x2721, 0x2721},   {0x2728, 0x2728},   {0x2733, 0x2734},
    {0x2744, 0x2744},   {0x2747, 0x2747},   {0x274C, 0x274C},   {0x274E, 0x274E},   {0x2753, 0x2755},
    {0x2757, 0x2757},   {0x2763, 0x2767},   {0x2795, 0x2797},   {0x27A1, 0x27A1},   {0x27B0, 0x27B0},
    {0x27BF, 0x27BF},   {0x2934, 0x2935},   {0x2B05, 0x2B07},   {0x2B1B, 0x2B1C},   {0x2B50, 0x2B50},
    {0x2B55, 0x2B55},   {0x3030, 0x3030},   {0x303D, 0x303D},   {0x3297, 0x3297},   {0x3299, 0x3299},
    {0x1F000, 0x1F0FF}, {0x1F10D, 0x1F10F}, {0x1F12F, 0x1F12F}, {0x1F16C, 0x1F171}, {0x1F17E, 0x1F17F},
    {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A}, {0x1F1AD, 0x1F1E5}, {0x1F201, 0x1F20F}, {0x1F21A, 0x1F21A},
    {0x1F22F, 0x1F22F}, {0x1F232, 0x1F23A}, {0x1F23C, 0x1F23F}, {0x1F249, 0x1F3FA}, {0x1F400, 0x1F53D},
    {0x1F546, 0x1F64F}, {0x1F680, 0x1F6FF}, {0x1F774, 0x1F77F}, {0x1F7D5, 0x1F7FF}, {0x1F80C, 0x1F80F},
    {0x1F848, 0x1F84F}, {0x1F85A, 0x1F85F}, {0x1F888, 0x1F88F}, {0x1F8AE, 0x1F8FF}, {0x1F90C, 0x1F93A},
    {0x1F93C, 0x1F945}, {0x1F947, 0x1FAFF}, {0x1FC00, 0x1FFFD},
};

struct CodePoint
{
    std::uint32_t value = 0;
    std::uint32_t units = 0;
};

bool NextCodePoint(const std::wstring &text, std::size_t offset, CodePoint &out)
{
    if (offset >= text.size())
    {
        return false;
    }
    const std::uint32_t first = static_cast<std::uint32_t>(text[offset]);
    if (first >= 0xD800 && first <= 0xDBFF && offset + 1 < text.size())
    {
        const std::uint32_t second = static_cast<std::uint32_t>(text[offset + 1]);
        if (second >= 0xDC00 && second <= 0xDFFF)
        {
            out.value = 0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00);
            out.units = 2;
            return true;
        }
    }
    out.value = first;
    out.units = 1;
    return true;
}

bool InRange(const CodePointRange &range, std::uint32_t codepoint)
{
    return codepoint >= range.first && codepoint <= range.last;
}

bool IsExtendedPictographic(std::uint32_t codepoint)
{
    std::size_t low = 0;
    std::size_t high = sizeof(kExtendedPictographic) / sizeof(kExtendedPictographic[0]);
    while (low < high)
    {
        const std::size_t middle = low + (high - low) / 2;
        if (codepoint < kExtendedPictographic[middle].first)
            high = middle;
        else if (codepoint > kExtendedPictographic[middle].last)
            low = middle + 1;
        else
            return true;
    }
    return false;
}

bool IsRegionalIndicator(std::uint32_t codepoint)
{
    return codepoint >= kRegionalIndicatorFirst && codepoint <= kRegionalIndicatorLast;
}

bool IsEmojiModifier(std::uint32_t codepoint)
{
    return codepoint >= kEmojiModifierFirst && codepoint <= kEmojiModifierLast;
}

bool IsTagCharacter(std::uint32_t codepoint)
{
    return codepoint >= kTagCharacterFirst && codepoint <= kTagCharacterLast;
}

bool IsKeycapBase(std::uint32_t codepoint)
{
    return (codepoint >= '0' && codepoint <= '9') || codepoint == '#' || codepoint == '*';
}

// 前面的 emoji 主体后面能直接跟的修饰，逐个吃掉。
bool IsAttachment(std::uint32_t codepoint)
{
    return codepoint == kVariationSelector16 || codepoint == kCombiningEnclosingKeycap || IsEmojiModifier(codepoint) ||
           IsTagCharacter(codepoint);
}

enum Mark : unsigned char
{
    kNoMark = 0,
    kPictographic = 1,
    kRegionalFlag = 2,
};

void MarkRange(std::vector<Mark> &marks, std::size_t start, std::size_t end, Mark mark)
{
    for (std::size_t index = start; index < end && index < marks.size(); ++index)
    {
        marks[index] = mark;
    }
}
} // namespace

std::vector<EmojiRun> FindEmojiRuns(const std::wstring &text)
{
    std::vector<EmojiRun> runs;
    if (text.empty())
    {
        return runs;
    }

    std::vector<Mark> marks(text.size(), kNoMark);
    std::size_t offset = 0;
    while (offset < text.size())
    {
        CodePoint codepoint;
        if (!NextCodePoint(text, offset, codepoint))
        {
            break;
        }
        const std::size_t next = offset + codepoint.units;

        // 国旗：成对的区域指示符。落单的一个只是字母，留在主字体里。
        if (IsRegionalIndicator(codepoint.value))
        {
            CodePoint following;
            if (NextCodePoint(text, next, following) && IsRegionalIndicator(following.value))
            {
                MarkRange(marks, offset, next + following.units, kRegionalFlag);
                offset = next + following.units;
                continue;
            }
            offset = next;
            continue;
        }

        // 键帽：#️⃣ / 1️⃣ / *️⃣。基字符必须和后面的 FE0F? 20E3 一起换字体，否则数字仍按主字体画。
        if (IsKeycapBase(codepoint.value))
        {
            std::size_t end = next;
            CodePoint following;
            if (NextCodePoint(text, end, following) && following.value == kVariationSelector16)
            {
                end += following.units;
            }
            if (NextCodePoint(text, end, following) && following.value == kCombiningEnclosingKeycap)
            {
                end += following.units;
                MarkRange(marks, offset, end, kPictographic);
                offset = end;
                continue;
            }
            offset = next;
            continue;
        }

        if (IsExtendedPictographic(codepoint.value))
        {
            std::size_t end = next;
            for (;;)
            {
                // 吃掉变体选择符、键帽、肤色修饰与 tag 字符。
                CodePoint following;
                while (NextCodePoint(text, end, following) && IsAttachment(following.value))
                {
                    end += following.units;
                }
                // ZWJ 把后面的 emoji 主体接进同一个序列（👨‍👩‍👧、❤️‍🔥）。
                if (!NextCodePoint(text, end, following) || following.value != kZeroWidthJoiner)
                {
                    break;
                }
                const std::size_t joined = end + following.units;
                CodePoint joinedCodepoint;
                if (!NextCodePoint(text, joined, joinedCodepoint) || !IsExtendedPictographic(joinedCodepoint.value))
                {
                    break;
                }
                end = joined + joinedCodepoint.units;
            }
            MarkRange(marks, offset, end, kPictographic);
            offset = end;
            continue;
        }

        // 孤立的修饰符（例如没有主体的 20E3）也交给 emoji 字体，免得在主字体里画成豆腐块。
        if (codepoint.value == kCombiningEnclosingKeycap || IsEmojiModifier(codepoint.value) ||
            IsTagCharacter(codepoint.value))
        {
            MarkRange(marks, offset, next, kPictographic);
        }
        offset = next;
    }

    for (std::size_t index = 0; index < marks.size();)
    {
        if (marks[index] == kNoMark)
        {
            ++index;
            continue;
        }
        const Mark mark = marks[index];
        const std::size_t start = index;
        while (index < marks.size() && marks[index] == mark)
        {
            ++index;
        }
        EmojiRun run;
        run.start = static_cast<std::uint32_t>(start);
        run.length = static_cast<std::uint32_t>(index - start);
        run.kind = mark == kRegionalFlag ? EmojiRunKind::RegionalFlag : EmojiRunKind::Pictographic;
        runs.push_back(run);
    }
    return runs;
}

namespace controls_detail
{
void ApplyEmojiFontRuns(IDWriteTextLayout *layout, const std::wstring &text, const std::wstring &emojiFamily,
                        const std::wstring &flagFamily, IDWriteFontCollection *flagCollection)
{
    if (!layout || text.empty())
    {
        return;
    }
    const bool hasFlagFont = !flagFamily.empty() && flagCollection != nullptr;
    if (emojiFamily.empty() && !hasFlagFont)
    {
        return;
    }
    for (const EmojiRun &run : FindEmojiRuns(text))
    {
        const DWRITE_TEXT_RANGE range{run.start, run.length};
        if (run.kind == EmojiRunKind::RegionalFlag && hasFlagFont)
        {
            layout->SetFontCollection(flagCollection, range);
            layout->SetFontFamilyName(flagFamily.c_str(), range);
        }
        else if (!emojiFamily.empty())
        {
            layout->SetFontFamilyName(emojiFamily.c_str(), range);
        }
    }
}
} // namespace controls_detail
} // namespace msimeui
