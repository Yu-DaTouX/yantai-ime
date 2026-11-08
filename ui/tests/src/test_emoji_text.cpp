#include "tests/includes/test_framework.h"

#include "msimeui/EmojiText.h"

#include <string>
#include <vector>

using namespace msimeui;

namespace
{
struct ExpectedRun
{
    std::uint32_t start;
    std::uint32_t length;
    EmojiRunKind kind;
};

void RequireRuns(const std::wstring &text, const std::vector<ExpectedRun> &expected)
{
    const std::vector<EmojiRun> runs = FindEmojiRuns(text);
    REQUIRE(runs.size() == expected.size());
    for (size_t index = 0; index < runs.size(); ++index)
    {
        REQUIRE(runs[index].start == expected[index].start);
        REQUIRE(runs[index].length == expected[index].length);
        REQUIRE(runs[index].kind == expected[index].kind);
    }
}
} // namespace

// 拼音 yi 的候选 1️⃣：基字符 '1' 必须和 FE0F、20E3 一起换 emoji 字体，否则主字体把整串画成怪字形。
TEST_CASE(emoji_runs_include_the_digit_of_a_keycap_sequence)
{
    RequireRuns(L"1\uFE0F\u20E3", {{0, 3, EmojiRunKind::Pictographic}});
    RequireRuns(L"#\uFE0F\u20E3", {{0, 3, EmojiRunKind::Pictographic}});
    RequireRuns(L"*\u20E3", {{0, 2, EmojiRunKind::Pictographic}});
    // 没有键帽的组合只是普通数字 / 符号，不能把它们的字体换掉。
    RequireRuns(L"1", {});
    RequireRuns(L"#", {});
    RequireRuns(L"\uFE0F", {});
}

TEST_CASE(emoji_runs_cover_variation_selectors_and_skin_tone_modifiers)
{
    // ❤ 本身是 Extended_Pictographic，FE0F 要一起进区间。
    RequireRuns(L"\u2764\uFE0F", {{0, 2, EmojiRunKind::Pictographic}});
    // 👍🏻：主体 + 肤色修饰符。
    RequireRuns(L"\U0001F44D\U0001F3FB", {{0, 4, EmojiRunKind::Pictographic}});
}

TEST_CASE(emoji_runs_join_zwj_sequences_into_one_range)
{
    // 👨‍👩‍👧：三个主体用两个 ZWJ 串起来，8 个 UTF-16 码元。
    RequireRuns(L"\U0001F468\u200D\U0001F469\u200D\U0001F467", {{0, 8, EmojiRunKind::Pictographic}});
    // 👨‍❤️‍👨：中间的主体带 FE0F，ZWJ 仍要接下去。
    RequireRuns(L"\U0001F468\u200D\u2764\uFE0F\u200D\U0001F468", {{0, 8, EmojiRunKind::Pictographic}});
}

TEST_CASE(emoji_runs_separate_regional_flags_from_pictographs)
{
    // 🇨🇳 / 🇺🇸：两个区域指示符拼成国旗，单独一类，交给宿主加载的国旗字体。
    RequireRuns(L"\U0001F1E8\U0001F1F3", {{0, 4, EmojiRunKind::RegionalFlag}});
    // 相邻的两面旗连在一起也没问题，同一类区间会合并。
    RequireRuns(L"\U0001F1E8\U0001F1F3\U0001F1FA\U0001F1F8", {{0, 8, EmojiRunKind::RegionalFlag}});
    // 落单的区域指示符不是国旗，留在主字体里。
    RequireRuns(L"\U0001F1E8", {});
}

TEST_CASE(emoji_runs_leave_ordinary_text_alone)
{
    RequireRuns(L"汉字", {});
    RequireRuns(L"汉字42", {});
    RequireRuns(L"yi", {});
    RequireRuns(L"！？", {});
    // 区间按 UTF-16 偏移给出，混在汉字里的键帽只覆盖它自己那三个码元。
    RequireRuns(L"候1\uFE0F\u20E3选", {{1, 3, EmojiRunKind::Pictographic}});
    RequireRuns(L"选\U0001F44D\U0001F3FB汉", {{1, 4, EmojiRunKind::Pictographic}});
}

TEST_CASE(emoji_runs_handle_tag_sequences_after_a_pictograph)
{
    // 🏴 + tag 序列（英格兰旗）是一个序列，tag 字符属于这个区间。
    const std::wstring england = L"\U0001F3F4\U000E0067\U000E0062\U000E0065\U000E006E\U000E0067\U000E007F";
    RequireRuns(england, {{0, static_cast<std::uint32_t>(england.size()), EmojiRunKind::Pictographic}});
}
