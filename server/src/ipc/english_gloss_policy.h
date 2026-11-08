#pragma once

// 候选词英文上屏用的释义清洗：把 english.db 的一行中译英释义拆成一条条可以单独上屏的英文词义，
// 并把开头的词性标签单独拿出来给候选窗显示（词性不上屏）。纯函数，头文件即可，测试不用链接 Server。
//
// 数据形状：zh_en_glosses.english_gloss 一行一条，词义用 ASCII ';' 连接，每条开头可以带词性标签
// （"n. silver medal" / "proper n. Wanshan District" / "adj. spreading"），标签是固定的小写缩写，
// 见候选词翻译那边同一份 PartOfSpeechLabelLength。custom_translations.txt 覆盖写进来的值也是同一形状。

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ipc/candidate_translation_policy.h"

namespace FanyImeIpc
{
// 英文清单最多列 9 条词义：数字键 1-9 直接选。
inline constexpr size_t kMaxEnglishSenses = 9;

struct EnglishSense
{
    // 上屏与显示的文字：去掉了词性标签和首尾空白，句子型释义去掉了句末句号。
    std::string text;
    // 开头那个词性标签本身（"n."、"adj."、"proper n."），没有标签时为空。只用于显示，不上屏。
    std::string part_of_speech;
};

namespace english_gloss_detail
{
inline std::string_view TrimWhitespace(std::string_view value)
{
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

// 句子型释义（"See you tomorrow."）的句末句号不属于要上屏的英文。只在释义里有空格时去掉一个句号：
// "etc."、"U.S." 这类缩写本身带着点，不能动。专有名词的大小写原样保留，这里不碰任何字母。
inline std::string_view StripSentencePeriod(std::string_view text)
{
    if (text.size() > 1 && text.back() == '.' && text.find(' ') != std::string_view::npos)
        text.remove_suffix(1);
    return text;
}

inline std::string FoldAscii(std::string_view text)
{
    std::string folded(text);
    for (char &ch : folded)
    {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch + ('a' - 'A'));
    }
    return folded;
}
} // namespace english_gloss_detail

// 拆一行中译英释义。规则：
//   - 按 ';'（以及数据里偶尔出现的全角 '；'）拆开，去首尾空白；
//   - 开头是词性标签时把标签拆出来放进 part_of_speech，剩下的文字才是上屏内容；
//   - 句子型的去掉句末句号；
//   - 空项丢弃，同一个英文（不分大小写）只留第一次出现的那条；
//   - 最多 limit 条（默认 9）。
inline std::vector<EnglishSense> SplitEnglishSenses(std::string_view gloss, size_t limit = kMaxEnglishSenses)
{
    static constexpr std::string_view kFullwidthSemicolon = "\xEF\xBC\x9B";
    std::vector<EnglishSense> senses;
    std::vector<std::string> seen;
    if (limit == 0)
        return senses;

    size_t start = 0;
    while (start <= gloss.size())
    {
        const size_t ascii = gloss.find(';', start);
        const size_t fullwidth = gloss.find(kFullwidthSemicolon, start);
        const size_t cut = (std::min)(ascii, fullwidth);
        const std::string_view raw =
            cut == std::string_view::npos ? gloss.substr(start) : gloss.substr(start, cut - start);

        std::string_view sense = english_gloss_detail::TrimWhitespace(raw);
        EnglishSense entry;
        if (const size_t label = PartOfSpeechLabelLength(sense); label != 0)
        {
            entry.part_of_speech.assign(sense.substr(0, label));
            sense = StripPartOfSpeechLabel(sense);
        }
        sense = english_gloss_detail::StripSentencePeriod(english_gloss_detail::TrimWhitespace(sense));
        if (!sense.empty())
        {
            const std::string folded = english_gloss_detail::FoldAscii(sense);
            if (std::find(seen.begin(), seen.end(), folded) == seen.end())
            {
                seen.push_back(folded);
                entry.text.assign(sense);
                senses.push_back(std::move(entry));
            }
        }

        if (cut == std::string_view::npos)
            break;
        start = cut + (cut == fullwidth ? kFullwidthSemicolon.size() : 1);
        if (senses.size() >= limit)
            break;
    }
    return senses;
}

// 英文行显示（也是 Ctrl+Enter 上屏）的那一条：清洗后的第一条词义，没有释义时返回空串。
inline std::string FirstEnglishSense(std::string_view gloss)
{
    const auto senses = SplitEnglishSenses(gloss, 1);
    return senses.empty() ? std::string{} : senses.front().text;
}
} // namespace FanyImeIpc
