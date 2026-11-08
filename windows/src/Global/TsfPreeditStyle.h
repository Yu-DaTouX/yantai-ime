#pragma once

#include <atomic>
#include <cwchar>
#include <string>
#include <string_view>

namespace GlobalSettings
{
//
// 支持的 TSF 预编辑格式
//  - raw: 原始按键序列
//  - pinyin: 分词后的拼音序列
//  - empty: 行内不显示预编辑
//  - cand: 当前高亮的候选词序列（预留）
//
namespace TsfPreeditStyle
{
constexpr std::string_view Raw = "raw";
constexpr std::string_view Pinyin = "pinyin";
constexpr std::string_view Empty = "empty";
constexpr std::string_view Cand = "cand";
} // namespace TsfPreeditStyle

inline bool isKnownTsfPreeditStyle(std::string_view style)
{
    return style == TsfPreeditStyle::Raw || style == TsfPreeditStyle::Pinyin || style == TsfPreeditStyle::Empty;
}

inline std::string normalizeTsfPreeditStyle(std::string_view style)
{
    if (style == TsfPreeditStyle::Pinyin || style == TsfPreeditStyle::Empty)
    {
        return std::string(style);
    }
    return std::string(TsfPreeditStyle::Raw);
}

enum class TsfPreeditStyleValue : unsigned char
{
    Raw,
    Pinyin,
    Empty
};

// Worker readers in several TSF apartments can publish settings concurrently
// with host-thread key processing. Publish a scalar, never a mutable string.
inline std::atomic<TsfPreeditStyleValue> tsfPreeditStyle{TsfPreeditStyleValue::Raw};

inline std::string_view getTsfPreeditStyle()
{
    switch (tsfPreeditStyle.load(std::memory_order_relaxed))
    {
    case TsfPreeditStyleValue::Pinyin:
        return TsfPreeditStyle::Pinyin;
    case TsfPreeditStyleValue::Empty:
        return TsfPreeditStyle::Empty;
    default:
        return TsfPreeditStyle::Raw;
    }
}

inline void setTsfPreeditStyle(std::string_view newStyle)
{
    const auto value = newStyle == TsfPreeditStyle::Pinyin  ? TsfPreeditStyleValue::Pinyin
                       : newStyle == TsfPreeditStyle::Empty ? TsfPreeditStyleValue::Empty
                                                            : TsfPreeditStyleValue::Raw;
    tsfPreeditStyle.store(value, std::memory_order_relaxed);
}

inline void setTsfPreeditStyleFromWide(const wchar_t *style)
{
    if (!style)
    {
        setTsfPreeditStyle(TsfPreeditStyle::Raw);
        return;
    }
    if (wcscmp(style, L"pinyin") == 0)
    {
        setTsfPreeditStyle(TsfPreeditStyle::Pinyin);
    }
    else if (wcscmp(style, L"empty") == 0)
    {
        setTsfPreeditStyle(TsfPreeditStyle::Empty);
    }
    else
    {
        setTsfPreeditStyle(TsfPreeditStyle::Raw);
    }
}

inline bool isKnownTsfPreeditStyleWide(const wchar_t *style)
{
    return style && (wcscmp(style, L"raw") == 0 || wcscmp(style, L"pinyin") == 0 || wcscmp(style, L"empty") == 0);
}
} // namespace GlobalSettings
