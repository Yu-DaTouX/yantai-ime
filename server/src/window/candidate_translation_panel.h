#pragma once
#include "msimeui/Controls.h"
#include "msimeui/DeviceResources.h"
#include <wrl/client.h>
#include <algorithm>
#include <functional>

// Separate rows: never alter CandidateList padding/emoji layout or selected-item geometry.
class CandidateTranslationPanel : public msimeui::Visual
{
  public:
    explicit CandidateTranslationPanel(bool draft) : draftRow_(draft)
    {
        // Cancel this hidden child's own body-row spacing, exactly like CandidateEnglishLine.
        SetMargin({0.0f, -2.0f, 0.0f, 0.0f});
    }
    void SetContent(bool visible, std::wstring text, std::wstring latest, std::wstring hint)
    {
        if (visible_ == visible && text_ == text && latest_ == latest && hint_ == hint)
            return;
        SetMargin({0.0f, visible ? 0.0f : -2.0f, 0.0f, 0.0f});
        visible_ = visible;
        text_ = std::move(text);
        latest_ = std::move(latest);
        hint_ = std::move(hint);
        InvalidateMeasure();
        InvalidateVisual();
    }
    void SetAppearance(std::wstring font, float size, D2D1_COLOR_F color, D2D1_COLOR_F muted)
    {
        font_ = std::move(font);
        size_ = size;
        color_ = color;
        muted_ = muted;
        InvalidateMeasure();
        InvalidateVisual();
    }
    void SetOnClick(std::function<void()> action)
    {
        click_ = std::move(action);
    }
    msimeui::SizeF Measure(const msimeui::SizeF &available) override
    {
        return visible_ ? msimeui::SizeF{(std::min)(360.0f, available.width),
                                         draftRow_ ? size_ * 1.6f + 8 : size_ * 2.9f + 25}
                        : msimeui::SizeF{};
    }
    void Arrange(const msimeui::RectF &rect) override
    {
        bounds_ = rect;
    }
    bool HitTest(const msimeui::PointF &point) const override
    {
        return visible_ && point.x >= bounds_.x && point.y >= bounds_.y && point.x < bounds_.x + bounds_.width &&
               point.y < bounds_.y + bounds_.height;
    }
    bool OnMouseDown(const POINT &, WPARAM) override
    {
        return visible_ && !draftRow_;
    }
    bool OnMouseUp(const POINT &, WPARAM) override
    {
        if (visible_ && !draftRow_ && click_)
            click_();
        return true;
    }
    void Render(msimeui::DeviceResources &dr) override
    {
        if (!visible_)
            return;
        auto *target = dr.GetRenderTarget();
        auto *factory = dr.GetDWriteFactory();
        if (!target || !factory)
            return;
        auto layout = [&](const std::wstring &text, float size, float width, float height, bool wrap) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
            Microsoft::WRL::ComPtr<IDWriteTextLayout> value;
            factory->CreateTextFormat(font_.empty() ? L"Microsoft YaHei UI" : font_.c_str(), nullptr,
                                      DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                      size, L"zh-CN", &format);
            if (!format)
                return value;
            format->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
            factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(),
                                      (std::max)(1.0f, width), height, &value);
            if (value)
            {
                DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
                Microsoft::WRL::ComPtr<IDWriteInlineObject> ellipsis;
                factory->CreateEllipsisTrimmingSign(format.Get(), &ellipsis);
                value->SetTrimming(&trim, ellipsis.Get());
                value->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, size * 1.45f, size * 1.1f);
            }
            return value;
        };
        auto draw = [&](IDWriteTextLayout *value, float x, float y, D2D1_COLOR_F color) {
            if (value)
                if (auto *brush = dr.GetSolidColorBrush(color))
                    target->DrawTextLayout(D2D1::Point2F(x, y), value, brush,
                                           D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        };
        const float width = (std::max)(1.0f, bounds_.width - 16);
        if (auto *brush = dr.GetSolidColorBrush(D2D1::ColorF(muted_.r, muted_.g, muted_.b, 0.25f)))
            target->DrawLine({bounds_.x + 8, draftRow_ ? bounds_.y + bounds_.height : bounds_.y},
                             {bounds_.x + bounds_.width - 8, draftRow_ ? bounds_.y + bounds_.height : bounds_.y},
                             brush);
        if (draftRow_)
        {
            const float tagWidth = size_ + 10;
            const auto tag = D2D1::RoundedRect(
                D2D1::RectF(bounds_.x + 5, bounds_.y + 4, bounds_.x + tagWidth + 5, bounds_.y + size_ * 1.45f + 4), 4,
                4);
            if (auto *brush = dr.GetSolidColorBrush(D2D1::ColorF(0xFF9F0A)))
                target->FillRoundedRectangle(tag, brush);
            auto tagText = layout(L"译", size_ * 0.9f, tagWidth, size_ * 1.5f, false);
            draw(tagText.Get(), bounds_.x + 9, bounds_.y + 4, D2D1::ColorF(0x202020));
            std::wstring shown = text_.empty() ? L"打拼音选词，中文先攒在这里" : text_;
            const float room = (std::max)(1.0f, width - tagWidth - 8);
            const auto fits = [&](size_t start) {
                const auto candidate = (start ? L"…" : L"") + shown.substr(start);
                auto measured = layout(candidate, size_, 1000000, size_ * 2, false);
                DWRITE_TEXT_METRICS metrics{};
                return !measured || FAILED(measured->GetMetrics(&metrics)) || metrics.width <= room;
            };
            size_t start = 0, end = shown.size();
            while (start < end)
            {
                const size_t middle = start + (end - start) / 2;
                if (fits(middle))
                    end = middle;
                else
                    start = middle + 1;
            }
            if (start && start < shown.size() && shown[start] >= 0xDC00 && shown[start] <= 0xDFFF)
                ++start;
            if (start)
                shown = L"…" + shown.substr(start);
            auto value = layout(shown, size_, room, size_ * 1.6f, false);
            if (value && !latest_.empty())
                value->SetUnderline(
                    TRUE, {static_cast<UINT32>(shown.size() > latest_.size() ? shown.size() - latest_.size() : 0),
                           static_cast<UINT32>((std::min)(shown.size(), latest_.size()))});
            draw(value.Get(), bounds_.x + tagWidth + 13, bounds_.y + 4, color_);
        }
        else
        {
            auto value = layout(text_, size_, width, size_ * 2.9f, true);
            draw(value.Get(), bounds_.x + 8, bounds_.y + 5, text_ == L"翻译中…" ? muted_ : color_);
            auto hint = layout(hint_, size_ * 0.77f, width, size_ * 1.2f, false);
            draw(hint.Get(), bounds_.x + 8, bounds_.y + size_ * 2.9f + 7, muted_);
        }
    }

  private:
    bool draftRow_, visible_ = false;
    std::wstring text_, latest_, hint_, font_;
    float size_ = 14;
    D2D1_COLOR_F color_ = D2D1::ColorF(0xE5E5EA), muted_ = D2D1::ColorF(0x8E8E93);
    std::function<void()> click_;
};
