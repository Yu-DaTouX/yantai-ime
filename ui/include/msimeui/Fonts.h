#pragma once

#include <dwrite.h>
#include <memory>
#include <string>
#include <vector>

namespace msimeui
{
const wchar_t *UiFontFamily();
const wchar_t *UiFontFallbackFamily();
void ApplyUiFontFallback(IDWriteFactory *factory, IDWriteTextFormat *format);

// Resolved icon font/codepoint pair. `family` is null when no installed icon
// font can actually render the requested glyph, in which case the caller must
// fall back to plain text instead of drawing tofu.
struct IconGlyph
{
    const wchar_t *family = nullptr;
    wchar_t codepoint = 0;
};

// Picks the first installed icon font that really contains the glyph:
// "Segoe Fluent Icons" (Windows 11) first, then "Segoe MDL2 Assets"
// (Windows 10). `mdl2Codepoint` overrides the codepoint used when probing
// Segoe MDL2 Assets; pass 0 when both fonts share the same codepoint.
IconGlyph ResolveIconGlyph(wchar_t fluentCodepoint, wchar_t mdl2Codepoint = 0);

// 一段文字在单行里的纵向尺寸，全部以 em 为单位的比例给出，乘上字号就是 DIP。
struct TextVerticalMetrics
{
    // 基线以上的高度（字体 ascent）。
    float ascentEm = 1.0f;
    // 行高（ascent + descent + lineGap）：DWrite 用它在段落里居中一行。
    float lineHeightEm = 1.3f;
    // 墨迹（字形实际笔画）中线在基线上方多少 em。数字的墨迹中线由 cap height 决定，
    // 汉字则接近整字中线；两者不同，所以候选序号要按它对齐，而不是按行盒居中。
    float inkCenterEm = 0.38f;
};

// 按字体族、回退字体与文字的第一个码位解析上述纵向尺寸；结果按同一组参数缓存。
// 主字体缺字时用回退字体列表里第一个能画这个字的字体的行距。
TextVerticalMetrics ResolveTextVerticalMetrics(const std::wstring &fontFamily,
                                               const std::vector<std::wstring> &fallbackFamilies,
                                               const std::wstring &text);

// 从字体文件加载私有字体集（不安装到系统字体）。区域旗帜用的 Twemoji Mozilla 这样随安装包
// 放到数据目录、只给候选窗用。文件不存在、不是字体或系统不支持 COLR 字体集时返回空。
std::shared_ptr<IDWriteFontCollection> LoadPrivateFontCollection(const std::wstring &path);

// 单个字形的精确墨迹外接框（全部以 em 为单位的比例给出，乘上字号就是 DIP）。
// 用于把不同图标字体或中文字体里的单个字形归一到一致的视觉大小并按墨迹中心对齐。
struct GlyphInkMetrics
{
    bool valid = false;
    // 墨迹实际宽、高（em 比例）。
    float widthEm = 0.0f;
    float heightEm = 0.0f;
    // 墨迹水平中心相对于字形原点（基线左端）的距离（em 比例）。
    float inkCenterXEm = 0.5f;
    // 墨迹纵向中心在基线上方的距离（em 比例，正值在基线之上）。
    float inkCenterYAboveBaselineEm = 0.5f;
};

// 读取指定字体族里单个码位的真实轮廓外接框，结果按 (fontFamily, weight, codepoint) 缓存。
// 找不到字体或字形时返回 valid == false。
GlyphInkMetrics ResolveGlyphInkMetrics(const std::wstring &fontFamily, wchar_t codepoint,
                                       DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);

// Ordered, per-format fallback for missing glyphs, followed by the system mappings.
void ApplyFontFallback(IDWriteFactory *factory, IDWriteTextFormat *format, const std::vector<std::wstring> &families);
} // namespace msimeui
