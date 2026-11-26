#include "settings/native/native_page_skin_common.h"

#include "msimeui/DeviceResources.h"
#include "msimeui/Fonts.h"
#include "msimeui/Window.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>

using Microsoft::WRL::ComPtr;

namespace native_settings
{
namespace skinpreview
{
namespace
{
bool HitSelf(msimeui::Window *window, const msimeui::Visual &visual, const POINT &point)
{
    return window && visual.HitTest(window->ClientPixelsToDips(point));
}

void DrawCentered(msimeui::DeviceResources &dr, const std::wstring &text, const TextStyle &style, const RectF &box,
                  const D2D1_COLOR_F &color)
{
    TextLayout layout;
    layout.Set(text, style);
    const SizeF size = layout.Measure(100000.0f);
    layout.Draw(dr, {box.x + (box.width - size.width) * 0.5f, box.y + (box.height - size.height) * 0.5f}, color,
                100000.0f);
}

bool PushRoundedClipLayer(msimeui::DeviceResources &dr, const RectF &rect, float radius)
{
    ID2D1RenderTarget *target = dr.GetRenderTarget();
    if (!target)
        return false;
    ComPtr<ID2D1Factory> factory;
    target->GetFactory(factory.GetAddressOf());
    ComPtr<ID2D1RoundedRectangleGeometry> geometry;
    ComPtr<ID2D1Layer> layer;
    if (!factory ||
        FAILED(factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(ToD2D(rect), radius, radius),
                                                       geometry.GetAddressOf())) ||
        FAILED(target->CreateLayer(nullptr, layer.GetAddressOf())))
        return false;
    // A clip-only layer initialized from what is below keeps ClearType text working in it.
    target->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geometry.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                            D2D1::IdentityMatrix(), 1.0f, nullptr,
                                            D2D1_LAYER_OPTIONS_INITIALIZE_FOR_CLEARTYPE),
                      layer.Get());
    return true;
}

struct ToolbarIcon
{
    wchar_t codepoint;
    const wchar_t *fallback;
    int item; // index into Options::items, -1 always shown
};

// ftb/default.html after the preview strips #en, #fullwidth and #puncEn.
const ToolbarIcon kToolbarIcons[] = {
    {0xE982, L"中", -1}, {0xEC46, L"半", 0}, {0xF111, L"。", 1}, {0xE88D, L"简", 2}, {0xE713, L"设", 3},
};

// The skin's button keys for kToolbarIcons, in the same order, with the state each icon shows:
// Chinese input, half width, Chinese punctuation, simplified.
const ToolbarImageButtonRequest kToolbarButtons[] = {
    {"status", "cn"}, {"fullwidth", "half"}, {"punctuation", "cn"}, {"character_set", "simplified"}, {"settings", ""},
};
static_assert(std::size(kToolbarButtons) == std::size(kToolbarIcons));

size_t ToolbarIconIndex(const std::string &key)
{
    for (size_t i = 0; i < std::size(kToolbarButtons); ++i)
    {
        if (kToolbarButtons[i].key == key)
            return i;
    }
    return 0;
}
} // namespace

// ---------------------------------------------------------------------------------
// ToolbarPreview
// ---------------------------------------------------------------------------------
void ToolbarPreview::Set(const ToolbarLook &look, const Options &options)
{
    look_ = look;
    options_ = options;
    slots_.clear();
    if (look_.imageMode && look_.package)
    {
        // The buttons the toolbar would show, placed by the same rules as the toolbar itself.
        std::vector<ToolbarImageButtonRequest> shown;
        for (size_t i = 0; i < std::size(kToolbarIcons); ++i)
        {
            if (kToolbarIcons[i].item < 0 || options_.items[kToolbarIcons[i].item])
                shown.push_back(kToolbarButtons[i]);
        }
        slots_ = LayoutToolbarImageButtons(*look_.package, shown);
    }
    InvalidateMeasure();
    InvalidateVisual();
}

SizeF ToolbarPreview::ImageExtent() const
{
    SizeF extent = look_.imageSize;
    for (const auto &slot : slots_)
    {
        extent.width = std::max(extent.width, static_cast<float>(slot.xDip + slot.widthDip));
        extent.height = std::max(extent.height, static_cast<float>(slot.yDip + slot.heightDip));
    }
    return extent;
}

float ToolbarPreview::BarWidth() const
{
    const float s = ImageScale();
    const float icon = look_.iconBox * s;
    const float gap = look_.itemGap * s;
    int count = 0;
    for (const auto &entry : kToolbarIcons)
        count += entry.item < 0 || options_.items[entry.item] ? 1 : 0;
    const bool hideHandle = look_.handleStyle == "none";
    const bool dotsHandle = look_.handleStyle == "dots";
    const bool hideDivider = look_.dividerStyle == "none";
    const bool shortDivider = look_.dividerStyle == "short";
    const float handleW = hideHandle ? look_.padLeft * s : (look_.padLeft + 10.0f) * s;
    const float dividerW = hideDivider ? 0.0f : ((shortDivider ? 1.0f + 4.0f : 1.2f) * s);
    float width = handleW + count * icon + std::max(count - 1, 0) * gap + look_.padRight * s;
    if (look_.dividerAfterStatus)
    {
        if (!hideHandle)
            width += gap;
        if (!hideDivider && count > 1)
            width += dividerW + gap;
    }
    else
    {
        if (!hideHandle && !hideDivider)
            width += 2.0f * s + dividerW + gap;
        else if (!hideHandle || !hideDivider)
            width += dividerW + gap;
    }
    return width;
}

float ToolbarPreview::ImageScale() const
{
    return options_.scale * options_.iconSize / 24.0f;
}

SizeF ToolbarPreview::Measure(const SizeF &availableSize)
{
    if (look_.imageMode)
    {
        const float s = ImageScale();
        const SizeF extent = ImageExtent();
        return {options_.centered ? availableSize.width : std::min(extent.width * s, availableSize.width),
                extent.height * s};
    }
    const float height = look_.barHeight * ImageScale();
    return {options_.centered ? availableSize.width : std::min(BarWidth(), availableSize.width), height};
}

void ToolbarPreview::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void ToolbarPreview::Render(msimeui::DeviceResources &dr)
{
    ID2D1RenderTarget *target = dr.GetRenderTarget();
    if (!target)
        return;
    if (look_.imageMode)
    {
        RenderImage(dr);
        return;
    }
    const float s = ImageScale();
    const float width = BarWidth();
    const float height = look_.barHeight * s;
    const float x = options_.centered ? std::round(bounds_.x + (bounds_.width - width) * 0.5f) : bounds_.x;
    const RectF bar = {x, bounds_.y, width, height};
    const float radius = look_.radius * s;

    if (options_.shadow)
    {
        for (size_t i = 0; i < look_.shadows.size() && i < 2; ++i)
        {
            const auto &shadow = look_.shadows[i];
            const msimeui::ShadowPass pass = {shadow.blur * 0.5f, shadow.alpha, shadow.x, shadow.y};
            shadows_[i].Render(target, bar, radius, &pass, 1);
        }
    }
    FillRounded(dr, bar, radius, look_.bg);
    StrokeRounded(dr, bar, radius, look_.border, look_.borderWidth);

    const float cy = bar.y + height * 0.5f;
    const float gap = look_.itemGap * s;
    const bool hideHandle = look_.handleStyle == "none";
    const bool dotsHandle = look_.handleStyle == "dots";
    const bool hideDivider = look_.dividerStyle == "none";
    const bool shortDivider = look_.dividerStyle == "short";
    const float dividerStroke = (shortDivider ? 1.0f : 1.2f) * s;
    const float dividerSlot = hideDivider ? 0.0f : ((shortDivider ? 1.0f + 4.0f : 1.2f) * s);

    const auto drawDivider = [&](float dx) {
        if (hideDivider)
            return;
        const float lx = dx + (dividerSlot - dividerStroke) * 0.5f;
        if (shortDivider)
        {
            const float lh = height * 0.44f;
            FillRounded(dr, {lx, cy - lh * 0.5f, dividerStroke, lh}, dividerStroke * 0.5f, look_.divider);
        }
        else
        {
            const float b = CssBorder(look_.borderWidth);
            FillRect(dr, {lx, bar.y + b, dividerStroke, height - b * 2.0f}, look_.divider);
        }
    };

    float cx = bar.x;
    if (!hideHandle)
    {
        const float slotW = (look_.padLeft + 10.0f) * s;
        if (dotsHandle)
        {
            const D2D1_COLOR_F dotColor =
                D2D1::ColorF(look_.icon.r, look_.icon.g, look_.icon.b, look_.light ? 0.28f : 0.30f);
            if (ID2D1SolidColorBrush *brush = dr.GetSolidColorBrush(dotColor))
            {
                const float r = std::max(0.8f, 1.05f * s);
                const float stepX = 3.6f * s;
                const float stepY = 3.8f * s;
                const float leftInset = look_.padLeft * s;
                const float hcx = cx + leftInset + (slotW - leftInset) * 0.5f;
                for (int col = 0; col < 2; ++col)
                {
                    const float px = hcx + (col == 0 ? -0.5f : 0.5f) * stepX;
                    for (int row = -1; row <= 1; ++row)
                        target->FillEllipse(
                            D2D1::Ellipse(D2D1::Point2F(px, cy + static_cast<float>(row) * stepY), r, r), brush);
                }
            }
        }
        else
        {
            const float hx = cx + (slotW - 2.5f * s) * 0.5f;
            FillRounded(dr, {hx, cy - 7.0f * s, 2.5f * s, 14.0f * s}, 2.0f * s, look_.handle);
        }
        cx += slotW;
        if (!look_.dividerAfterStatus)
        {
            if (!hideDivider)
            {
                cx += 2.0f * s;
                drawDivider(cx);
                cx += dividerSlot;
            }
            cx += gap;
        }
        else
        {
            cx += gap;
        }
    }
    else
    {
        cx += look_.padLeft * s;
        if (!look_.dividerAfterStatus && !hideDivider)
        {
            drawDivider(cx);
            cx += dividerSlot + gap;
        }
    }

    const float icon = look_.iconBox * s;
    const float iconRadius = std::min(look_.iconRadius * s, icon * 0.5f);
    int totalShown = 0;
    for (const auto &entry : kToolbarIcons)
        totalShown += entry.item < 0 || options_.items[entry.item] ? 1 : 0;
    int drawn = 0;
    for (const auto &entry : kToolbarIcons)
    {
        if (entry.item >= 0 && !options_.items[entry.item])
            continue;
        if (drawn > 0)
            cx += gap;
        const RectF box = {cx, cy - icon * 0.5f, icon, icon};
        const bool isStatus = entry.item < 0;
        const bool hasPill = isStatus && look_.statusFill.a > 0.001f;
        if (hasPill)
        {
            FillRounded(dr, box, iconRadius, look_.statusFill);
        }
        const D2D1_COLOR_F glyphColor = hasPill ? look_.statusIcon : look_.icon;
        const msimeui::IconGlyph glyph = msimeui::ResolveIconGlyph(entry.codepoint);
        const std::wstring family = glyph.family ? std::wstring(glyph.family) : std::wstring(msimeui::UiFontFamily());
        const std::wstring text = glyph.family ? std::wstring(1, glyph.codepoint) : std::wstring(entry.fallback);
        const float baseSize = icon * look_.iconScale;
        const msimeui::GlyphInkMetrics ink =
            text.size() == 1 ? msimeui::ResolveGlyphInkMetrics(family, text[0]) : msimeui::GlyphInkMetrics{};
        float fontSize = baseSize;
        if (ink.valid)
        {
            const float maxInkEm = std::max(ink.widthEm, ink.heightEm);
            if (maxInkEm > 0.05f)
            {
                const float targetFraction = hasPill ? 0.76f : 0.86f;
                fontSize = baseSize * std::clamp(targetFraction / maxInkEm, 0.70f, 1.25f);
            }
        }
        IDWriteFactory *factory = dr.GetDWriteFactory();
        ID2D1SolidColorBrush *brush = dr.GetSolidColorBrush(glyphColor);
        IDWriteTextFormat *format =
            dr.GetTextFormat(family, fontSize, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER,
                             DWRITE_PARAGRAPH_ALIGNMENT_CENTER, DWRITE_WORD_WRAPPING_NO_WRAP);
        ComPtr<IDWriteTextLayout> layout;
        if (factory && brush && format &&
            SUCCEEDED(factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, box.width,
                                                box.height, layout.GetAddressOf())))
        {
            float offsetX = 0.0f;
            float offsetY = 0.0f;
            if (ink.valid)
            {
                DWRITE_TEXT_METRICS tm{};
                DWRITE_LINE_METRICS lm{};
                UINT32 lineCount = 0;
                if (SUCCEEDED(layout->GetMetrics(&tm)) && SUCCEEDED(layout->GetLineMetrics(&lm, 1, &lineCount)) &&
                    lineCount >= 1)
                {
                    const float inkCenterX = tm.left + ink.inkCenterXEm * fontSize;
                    const float inkCenterY = (tm.top + lm.baseline) - ink.inkCenterYAboveBaselineEm * fontSize;
                    offsetX = box.width * 0.5f - inkCenterX;
                    offsetY = box.height * 0.5f - inkCenterY;
                }
            }
            target->DrawTextLayout(D2D1::Point2F(box.x + offsetX, box.y + offsetY), layout.Get(), brush,
                                   ink.valid ? D2D1_DRAW_TEXT_OPTIONS_NONE : D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        cx += icon;
        ++drawn;
        if (isStatus && look_.dividerAfterStatus && !hideDivider && drawn < totalShown)
        {
            cx += gap;
            drawDivider(cx);
            cx += dividerSlot;
        }
    }
}

void ToolbarPreview::RenderImage(msimeui::DeviceResources &dr)
{
    ID2D1RenderTarget *target = dr.GetRenderTarget();
    ComPtr<ID2D1DeviceContext> context;
    target->QueryInterface(IID_PPV_ARGS(context.GetAddressOf()));
    const auto draw = [&](ID2D1Bitmap *bitmap, const RectF &box) {
        if (context)
            context->DrawBitmap(bitmap, ToD2D(box), 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        else
            target->DrawBitmap(bitmap, ToD2D(box));
    };

    // The background stretched to its declared size and the buttons LayoutToolbarImageButtons placed
    // for the shown ones, as on the real toolbar; the whole is centered by its full extent.
    const float s = ImageScale();
    const float width = ImageExtent().width * s;
    const float x = options_.centered ? std::round(bounds_.x + (bounds_.width - width) * 0.5f) : bounds_.x;
    const float y = bounds_.y;
    const auto path = [&](const std::string &relative) {
        return (look_.directory / std::filesystem::u8path(relative)).lexically_normal().wstring();
    };
    if (ID2D1Bitmap *background = PreviewFrame(dr, look_.image, look_.animated))
        draw(background, {x, y, look_.imageSize.width * s, look_.imageSize.height * s});
    for (const auto &slot : slots_)
    {
        const ToolbarIcon &entry = kToolbarIcons[ToolbarIconIndex(slot.key)];
        const RectF box = {x + static_cast<float>(slot.xDip) * s, y + static_cast<float>(slot.yDip) * s,
                           static_cast<float>(slot.widthDip) * s, static_cast<float>(slot.heightDip) * s};
        if (!slot.background.empty())
        {
            if (ID2D1Bitmap *plate = PreviewFrame(dr, path(slot.background), look_.animated))
                draw(plate, box);
        }
        if (!slot.image.empty())
        {
            // Fitted into the box keeping its aspect, as the toolbar's Uniform stretch does.
            D2D1_SIZE_F size{};
            ID2D1Bitmap *bitmap = PreviewFrame(dr, path(slot.image), look_.animated, &size);
            if (bitmap && size.width > 0.0f && size.height > 0.0f)
            {
                const float fit = std::min(box.width / size.width, box.height / size.height);
                const SizeF fitted = {size.width * fit, size.height * fit};
                draw(bitmap, {box.x + (box.width - fitted.width) * 0.5f, box.y + (box.height - fitted.height) * 0.5f,
                              fitted.width, fitted.height});
            }
            continue;
        }
        const float shorter = std::min(box.width, box.height);
        const msimeui::IconGlyph glyph = msimeui::ResolveIconGlyph(entry.codepoint);
        TextStyle style;
        style.lineHeight = 1.0f;
        style.size = slot.iconSizeDip > 0.0 ? static_cast<float>(slot.iconSizeDip) * s : shorter * 0.82f;
        if (glyph.family)
        {
            style.family = glyph.family;
            DrawCentered(dr, std::wstring(1, glyph.codepoint), style, box, look_.icon);
        }
        else
        {
            DrawCentered(dr, entry.fallback, style, box, look_.icon);
        }
    }
}
// ---------------------------------------------------------------------------------
// CaretStatePreview
// ---------------------------------------------------------------------------------
void CaretStatePreview::Set(const CandidateLook &look, const std::string &position, D2D1_COLOR_F textOverride)
{
    look_ = look;
    position_ = position.empty() ? "top-left" : position;
    textOverride_ = textOverride;
    InvalidateVisual();
}

int CaretStatePreview::Columns(float width) const
{
    // @container (max-width: 569px): two columns.
    return width <= 569.0f ? 2 : 4;
}

SizeF CaretStatePreview::Measure(const SizeF &availableSize)
{
    const int rows = Columns(availableSize.width) == 2 ? 2 : 1;
    return {availableSize.width, rows * 132.0f + (rows - 1) * 14.0f};
}

void CaretStatePreview::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void CaretStatePreview::Render(msimeui::DeviceResources &dr)
{
    const Palette &p = CurrentPalette();
    const int columns = Columns(bounds_.width);
    const float total = columns * 132.0f + (columns - 1) * 14.0f;
    const float left = std::round(bounds_.x + (bounds_.width - total) * 0.5f);
    const bool middle = position_ == "top" || position_ == "bottom";
    const bool right = position_ == "top-right" || position_ == "bottom-right";
    const bool bottom = position_.rfind("bottom", 0) == 0;
    const float caretLeft = middle ? 65.0f : right ? 15.0f : 109.0f;
    const D2D1_COLOR_F text = textOverride_.a > 0.0f ? textOverride_ : look_.text;
    TextStyle badgeStyle;
    badgeStyle.family = L"Microsoft YaHei UI";
    badgeStyle.size = 20.0f;
    const float border = CssBorder();

    for (int i = 0; i < 4; ++i)
    {
        const int row = i / columns;
        const int column = i % columns;
        const RectF tile = {left + column * (132.0f + 14.0f), bounds_.y + row * (132.0f + 14.0f), 132.0f, 132.0f};
        FillRounded(dr, tile, 6.0f, p.surfaceSubtle);
        StrokeRounded(dr, tile, 6.0f, p.border, 1.0f);
        const float ox = tile.x + border;
        const float oy = tile.y + border;
        FillRect(dr, {ox + caretLeft, oy + 59.0f, 2.0f, 24.0f}, p.text);

        const bool punctuation = i == 1;
        const float width = punctuation ? 94.0f : 30.0f;
        float bx = ox + (130.0f - 29.0f - width);
        if (middle)
            bx = ox + 65.0f - width * 0.5f;
        else if (right)
            bx = ox + 23.0f;
        const RectF badge = {bx, oy + (bottom ? 89.0f : 23.0f), width, 30.0f};
        FillRect(dr, badge, look_.bg);
        StrokeRounded(dr, badge, 0.0f, look_.border, 1.0f);
        if (punctuation)
        {
            DrawCentered(dr, L"，。", badgeStyle, {badge.x - 1.0f + border, badge.y, 64.0f, badge.height}, text);
            DrawCentered(dr, L"中", badgeStyle,
                         {badge.x + badge.width - border + 1.0f - 30.0f, badge.y, 30.0f, badge.height}, text);
        }
        else
        {
            const wchar_t *labels[] = {L"中", L"", L"全", L"简"};
            DrawCentered(dr, labels[i], badgeStyle, badge, text);
        }
    }
}

// ---------------------------------------------------------------------------------
// FlushCard
// ---------------------------------------------------------------------------------
FlushCard &FlushCard::Add(VisualPtr child)
{
    AddChild(std::move(child));
    return *this;
}

void FlushCard::SetOpacity(float opacity)
{
    opacity_ = opacity;
    InvalidateVisual();
}

void FlushCard::SetClip(bool clip)
{
    clip_ = clip;
    InvalidateVisual();
}

SizeF FlushCard::Measure(const SizeF &availableSize)
{
    const float border = CssBorder();
    const float inner = std::max(availableSize.width - border * 2.0f, 0.0f);
    sizes_.clear();
    float height = border * 2.0f;
    for (const auto &child : children_)
    {
        const SizeF size = child->MeasureInLayout({inner, availableSize.height});
        sizes_.push_back(size);
        height += size.height;
    }
    return {availableSize.width, height};
}

void FlushCard::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
    const float border = CssBorder();
    float y = finalRect.y + border;
    for (size_t i = 0; i < children_.size(); ++i)
    {
        children_[i]->ArrangeInLayout(
            {finalRect.x + border, y, std::max(finalRect.width - border * 2.0f, 0.0f), sizes_[i].height});
        y += sizes_[i].height;
    }
}

void FlushCard::Render(msimeui::DeviceResources &dr)
{
    const Palette &p = CurrentPalette();
    constexpr float kRadius = 8.0f;
    const bool faded = opacity_ < 1.0f;
    if (faded)
        PushOpacity(dr, opacity_);
    msimeui::ShadowPass passes[2] = {};
    for (int i = 0; i < p.cardShadowCount; ++i)
        passes[i] = {p.cardShadow[i].sigma, p.cardShadow[i].alpha, 0.0f, p.cardShadow[i].offsetY};
    shadow_.Render(dr.GetRenderTarget(), bounds_, kRadius, passes, static_cast<size_t>(p.cardShadowCount));
    FillRounded(dr, bounds_, kRadius, Over(p.cardBg, p.chromeBg));
    const float border = CssBorder();
    const bool clipped = clip_ && PushRoundedClipLayer(dr, Inflate(bounds_, -border), kRadius - border);
    for (const auto &child : children_)
    {
        if (dr.IsVisible(Inflate(child->GetBounds(), 64.0f)))
            child->Render(dr);
    }
    if (clipped)
        dr.GetRenderTarget()->PopLayer();
    StrokeRounded(dr, bounds_, kRadius, p.border, 1.0f);
    if (faded)
        PopOpacity(dr);
}

// ---------------------------------------------------------------------------------
// Band
// ---------------------------------------------------------------------------------
Band::Band(VisualPtr child, Thickness padding) : msimeui::Container(std::move(child)), bandPadding_(padding)
{
}

void Band::SetBackground(std::function<D2D1_COLOR_F()> background)
{
    background_ = std::move(background);
}

void Band::SetTopRule(std::function<D2D1_COLOR_F()> color)
{
    topRule_ = std::move(color);
}

void Band::SetBottomRule(std::function<D2D1_COLOR_F()> color)
{
    bottomRule_ = std::move(color);
}

void Band::SetMinHeight(float height)
{
    bandMinHeight_ = height;
}

void Band::SetClip(bool clip)
{
    clip_ = clip;
}

void Band::SetDimmed(bool dimmed, float opacity)
{
    if (dimmed_ == dimmed)
        return;
    dimmed_ = dimmed;
    dimOpacity_ = opacity;
    InvalidateVisual();
}

SizeF Band::Measure(const SizeF &availableSize)
{
    const float top = topRule_ ? CssBorder() : 0.0f;
    const float bottom = bottomRule_ ? CssBorder() : 0.0f;
    padding_ = {bandPadding_.left, bandPadding_.top + top, bandPadding_.right, bandPadding_.bottom + bottom};
    SizeF size = msimeui::Container::Measure(availableSize);
    size.width = availableSize.width;
    size.height = std::max(size.height, bandMinHeight_);
    return size;
}

void Band::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
    if (child_)
    {
        const RectF content = GetContentRect();
        const SizeF size = child_->MeasureInLayout({content.width, content.height});
        child_->ArrangeInLayout({content.x, content.y, content.width, std::max(size.height, 0.0f)});
    }
}

void Band::Render(msimeui::DeviceResources &dr)
{
    if (background_)
        FillRect(dr, bounds_, background_());
    if (topRule_)
        FillRect(dr, {bounds_.x, bounds_.y, bounds_.width, CssBorder()}, topRule_());
    if (bottomRule_)
        FillRect(dr, {bounds_.x, bounds_.y + bounds_.height - CssBorder(), bounds_.width, CssBorder()}, bottomRule_());
    if (clip_)
        dr.PushClip(bounds_);
    if (dimmed_)
        PushOpacity(dr, dimOpacity_);
    msimeui::Container::Render(dr);
    if (dimmed_)
        PopOpacity(dr);
    if (clip_)
        dr.PopClip();
}

msimeui::Visual *Band::FindVisualAt(const PointF &point)
{
    if (dimmed_)
        return HitTest(point) ? this : nullptr;
    return msimeui::Container::FindVisualAt(point);
}

msimeui::Visual *Band::FindFocusableAt(const PointF &point)
{
    return dimmed_ ? nullptr : msimeui::Container::FindFocusableAt(point);
}

// ---------------------------------------------------------------------------------
// PillButton
// ---------------------------------------------------------------------------------
namespace
{
TextStyle PillStyle()
{
    TextStyle style;
    style.size = kFontCaption;
    style.lineHeight = 1.0f;
    return style;
}
} // namespace

PillButton::PillButton(std::wstring text, float height) : text_(std::move(text)), height_(height)
{
}

void PillButton::SetText(std::wstring text)
{
    if (text == text_)
        return;
    text_ = std::move(text);
    InvalidateMeasure();
    InvalidateVisual();
}

void PillButton::SetOnClick(std::function<void()> handler)
{
    onClick_ = std::move(handler);
}

SizeF PillButton::Measure(const SizeF &availableSize)
{
    return {std::min(MeasureTextWidth(text_, PillStyle()) + 20.0f + CssBorder() * 2.0f, availableSize.width), height_};
}

void PillButton::Arrange(const RectF &finalRect)
{
    bounds_ = finalRect;
}

void PillButton::Render(msimeui::DeviceResources &dr)
{
    const Palette &p = CurrentPalette();
    if (hovered_)
        FillRounded(dr, bounds_, 6.0f, p.buttonSecondaryBg);
    StrokeRounded(dr, bounds_, 6.0f, p.border, 1.0f);
    DrawCentered(dr, text_, PillStyle(), bounds_, hovered_ ? p.text : p.textMuted);
}

bool PillButton::OnMouseDown(const POINT &point, WPARAM)
{
    pressed_ = HitSelf(window_, *this, point);
    return pressed_;
}

bool PillButton::OnMouseUp(const POINT &point, WPARAM)
{
    if (!pressed_)
        return false;
    pressed_ = false;
    if (HitSelf(window_, *this, point) && onClick_)
    {
        // A handler may tear down the visual that owns onClick_ while it runs.
        const auto handler = onClick_;
        handler();
    }
    return true;
}

void PillButton::OnMouseEnter()
{
    hovered_ = true;
    InvalidateVisual();
}

void PillButton::OnMouseLeave()
{
    hovered_ = false;
    pressed_ = false;
    InvalidateVisual();
}

HCURSOR PillButton::GetCursor() const
{
    return LoadCursor(nullptr, IDC_HAND);
}

// ---------------------------------------------------------------------------------
} // namespace skinpreview
} // namespace native_settings
