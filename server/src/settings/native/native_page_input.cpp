#include "settings/native/native_settings_pages.h"

#include "msimeui/DeviceResources.h"
#include "msimeui/Fonts.h"
#include "msimeui/Window.h"

#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <regex>

// partials/input.html + modules/input.ts (+ the input parts of config-sync.ts, shared.ts,
// credential-test.ts) and styles/modules/input.css.
namespace native_settings
{
namespace
{
using Microsoft::WRL::ComPtr;

constexpr float kUnbounded = 100000.0f;

bool HitSelf(msimeui::Window *window, const msimeui::Visual &visual, const POINT &point)
{
    return window && visual.HitTest(window->ClientPixelsToDips(point));
}

// ---------------------------------------------------------------------------------
// One-shot / repeating timers on the window thread (TIMERPROC, so the window's own
// WM_TIMER handling is not involved).
// ---------------------------------------------------------------------------------
class PageTimers
{
  public:
    static UINT_PTR Start(HWND hwnd, UINT ms, bool repeat, std::function<void()> callback)
    {
        const UINT_PTR id = next_++;
        entries_[id] = {hwnd, repeat, std::move(callback)};
        SetTimer(hwnd, id, ms, &PageTimers::Proc);
        return id;
    }

    static void Stop(UINT_PTR &id)
    {
        if (id == 0)
            return;
        auto it = entries_.find(id);
        if (it != entries_.end())
        {
            KillTimer(it->second.hwnd, id);
            entries_.erase(it);
        }
        id = 0;
    }

  private:
    struct Entry
    {
        HWND hwnd;
        bool repeat;
        std::function<void()> callback;
    };

    static void CALLBACK Proc(HWND hwnd, UINT, UINT_PTR id, DWORD)
    {
        auto it = entries_.find(id);
        if (it == entries_.end())
        {
            KillTimer(hwnd, id);
            return;
        }
        std::function<void()> callback = it->second.callback;
        if (!it->second.repeat)
        {
            KillTimer(hwnd, id);
            entries_.erase(it);
        }
        callback();
    }

    static inline UINT_PTR next_ = 0x7100;
    static inline std::map<UINT_PTR, Entry> entries_;
};

// ---------------------------------------------------------------------------------
// Inline text with differently styled runs: `<code>` in descriptions, the model name
// with its meta span and the 浏览器下载 link.
// ---------------------------------------------------------------------------------
struct Span
{
    std::wstring text;
    float size = kFontCaption;
    std::function<D2D1_COLOR_F()> color;
    std::wstring family;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    std::function<void()> onClick;
    float marginLeft = 0.0f;
};

class RichText : public msimeui::Visual
{
  public:
    RichText(float baseSize, float lineHeight, DWRITE_WORD_WRAPPING wrapping)
        : baseSize_(baseSize), lineHeight_(lineHeight), wrapping_(wrapping)
    {
    }

    void SetSpans(std::vector<Span> spans)
    {
        spans_ = std::move(spans);
        layout_.Reset();
        InvalidateMeasure();
        InvalidateVisual();
    }

    SizeF Measure(const SizeF &availableSize) override
    {
        IDWriteTextLayout *layout = Layout(availableSize.width);
        if (!layout)
            return {};
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        return {std::min(std::ceil(metrics.widthIncludingTrailingWhitespace), availableSize.width), metrics.height};
    }

    void Arrange(const RectF &finalRect) override
    {
        bounds_ = finalRect;
    }

    void Render(msimeui::DeviceResources &dr) override
    {
        ID2D1RenderTarget *target = dr.GetRenderTarget();
        IDWriteTextLayout *layout = Layout(bounds_.width);
        if (!target || !layout)
            return;
        UINT32 start = 0;
        for (size_t i = 0; i < spans_.size(); ++i)
        {
            const UINT32 length = static_cast<UINT32>(Text(i).size());
            const D2D1_COLOR_F color = spans_[i].color ? spans_[i].color() : CurrentPalette().text;
            if (ID2D1SolidColorBrush *brush = dr.GetSolidColorBrush(color))
                layout->SetDrawingEffect(brush, {start, length});
            layout->SetUnderline(static_cast<int>(i) == hovered_ && spans_[i].onClick, {start, length});
            start += length;
        }
        if (ID2D1SolidColorBrush *brush = dr.GetSolidColorBrush(CurrentPalette().text))
            target->DrawTextLayout(D2D1::Point2F(bounds_.x, bounds_.y), layout, brush,
                                   D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }

    bool OnMouseMove(const POINT &point, WPARAM) override
    {
        const int span = SpanAt(point);
        if (span != hovered_)
        {
            hovered_ = span;
            InvalidateVisual();
        }
        return false;
    }

    void OnMouseLeave() override
    {
        if (hovered_ >= 0)
        {
            hovered_ = -1;
            InvalidateVisual();
        }
    }

    bool OnMouseDown(const POINT &point, WPARAM) override
    {
        pressed_ = SpanAt(point);
        return pressed_ >= 0 && spans_[pressed_].onClick;
    }

    bool OnMouseUp(const POINT &point, WPARAM) override
    {
        const int span = SpanAt(point);
        const bool clicked = span >= 0 && span == pressed_ && spans_[span].onClick;
        pressed_ = -1;
        if (clicked)
            spans_[span].onClick();
        return clicked;
    }

    HCURSOR GetCursor() const override
    {
        return LoadCursor(nullptr, hovered_ >= 0 && spans_[hovered_].onClick ? IDC_HAND : IDC_ARROW);
    }

  private:
    // A margin-left becomes leading spaces' worth of advance via a narrow no-break space run.
    std::wstring Text(size_t i) const
    {
        return spans_[i].text;
    }

    int SpanAt(const POINT &point)
    {
        if (!window_ || !layout_)
            return -1;
        const PointF dip = window_->ClientPixelsToDips(point);
        BOOL trailing = FALSE;
        BOOL inside = FALSE;
        DWRITE_HIT_TEST_METRICS metrics{};
        layout_->HitTestPoint(dip.x - bounds_.x, dip.y - bounds_.y, &trailing, &inside, &metrics);
        if (!inside)
            return -1;
        UINT32 start = 0;
        for (size_t i = 0; i < spans_.size(); ++i)
        {
            const UINT32 length = static_cast<UINT32>(Text(i).size());
            if (metrics.textPosition >= start && metrics.textPosition < start + length)
                return static_cast<int>(i);
            start += length;
        }
        return -1;
    }

    IDWriteTextLayout *Layout(float maxWidth)
    {
        const float width = wrapping_ == DWRITE_WORD_WRAPPING_NO_WRAP ? kUnbounded : std::max(maxWidth, 1.0f);
        if (layout_ && width_ == width)
            return layout_.Get();
        layout_.Reset();
        width_ = width;
        IDWriteFactory *factory = DWriteFactory();
        if (!factory)
            return nullptr;
        std::wstring text;
        for (size_t i = 0; i < spans_.size(); ++i)
            text += Text(i);
        ComPtr<IDWriteTextFormat> format;
        if (FAILED(factory->CreateTextFormat(UiFontFamily().c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, baseSize_, L"zh-cn",
                                             format.GetAddressOf())))
            return nullptr;
        msimeui::ApplyUiFontFallback(factory, format.Get());
        if (FAILED(factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), width,
                                             kUnbounded, layout_.GetAddressOf())))
            return nullptr;
        layout_->SetWordWrapping(wrapping_);
        UINT32 start = 0;
        for (size_t i = 0; i < spans_.size(); ++i)
        {
            const DWRITE_TEXT_RANGE range = {start, static_cast<UINT32>(Text(i).size())};
            layout_->SetFontSize(spans_[i].size, range);
            layout_->SetFontWeight(spans_[i].weight, range);
            if (!spans_[i].family.empty())
                layout_->SetFontFamilyName(spans_[i].family.c_str(), range);
            start += range.length;
        }
        if (lineHeight_ > 0.0f)
        {
            DWRITE_LINE_METRICS line{};
            UINT32 count = 0;
            layout_->GetLineMetrics(&line, 1, &count);
            if (count > 0)
            {
                const float spacing = baseSize_ * lineHeight_;
                layout_->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, spacing,
                                        line.baseline + (spacing - line.height) * 0.5f);
            }
        }
        return layout_.Get();
    }

    float baseSize_ = kFontCaption;
    float lineHeight_ = 0.0f;
    DWRITE_WORD_WRAPPING wrapping_ = DWRITE_WORD_WRAPPING_WRAP;
    std::vector<Span> spans_;
    float width_ = -1.0f;
    ComPtr<IDWriteTextLayout> layout_;
    int hovered_ = -1;
    int pressed_ = -1;
};

std::function<D2D1_COLOR_F()> Muted()
{
    return [] { return CurrentPalette().textMuted; };
}

// `<code>` inside a 12px description: the default monospace font for zh-CN pages
// (NSimSun) at the inherited 12px.
Span Code(std::wstring text)
{
    Span span{std::move(text), kFontCaption, Muted(), L"NSimSun"};
    return span;
}

Span Plain(std::wstring text)
{
    return Span{std::move(text), kFontCaption, Muted()};
}

// ---------------------------------------------------------------------------------
// Small layout helpers
// ---------------------------------------------------------------------------------

// `.section-item-row`: padding 12px 0 with a 1px divider on top.
class ItemRow : public msimeui::Container
{
  public:
    ItemRow(VisualPtr child, bool last) : msimeui::Container(std::move(child)), last_(last)
    {
    }

    SizeF Measure(const SizeF &availableSize) override
    {
        padding_ = {0.0f, 12.0f + CssBorder(), 0.0f, last_ ? 0.0f : 12.0f};
        return msimeui::Container::Measure(availableSize);
    }

    void Render(msimeui::DeviceResources &dr) override
    {
        FillRect(dr, {bounds_.x, bounds_.y, bounds_.width, CssBorder()}, CurrentPalette().divider);
        msimeui::Container::Render(dr);
    }

  private:
    bool last_ = false;
};

// border-top separated block (`.word-to-character-keys-row`: padding-top 16px).
class TopRuled : public msimeui::Container
{
  public:
    TopRuled(VisualPtr child, float paddingTop) : msimeui::Container(std::move(child)), paddingTop_(paddingTop)
    {
    }

    SizeF Measure(const SizeF &availableSize) override
    {
        padding_ = {0.0f, paddingTop_ + CssBorder(), 0.0f, 0.0f};
        return msimeui::Container::Measure(availableSize);
    }

    void Render(msimeui::DeviceResources &dr) override
    {
        FillRect(dr, {bounds_.x, bounds_.y, bounds_.width, CssBorder()}, CurrentPalette().divider);
        msimeui::Container::Render(dr);
    }

  private:
    float paddingTop_ = 0.0f;
};

// Plain padded box.
VisualPtr Padded(VisualPtr child, Thickness padding)
{
    auto box = std::make_shared<msimeui::Container>(std::move(child));
    box->SetPadding(padding);
    return box;
}

// `opacity` without blocking input.
class OpacityBox : public msimeui::Container
{
  public:
    explicit OpacityBox(VisualPtr child) : msimeui::Container(std::move(child))
    {
    }

    void SetDimmed(bool dimmed)
    {
        if (dimmed_ == dimmed)
            return;
        dimmed_ = dimmed;
        InvalidateVisual();
    }

    void Render(msimeui::DeviceResources &dr) override
    {
        if (!dimmed_)
        {
            msimeui::Container::Render(dr);
            return;
        }
        PushOpacity(dr, 0.45f);
        msimeui::Container::Render(dr);
        PopOpacity(dr);
    }

  private:
    bool dimmed_ = false;
};

// `.input-mode-options`: grid 1fr auto 1fr, gap 12px (the divider in the auto track is empty).
class TwoColumns : public msimeui::Panel
{
  public:
    TwoColumns(VisualPtr left, VisualPtr right)
    {
        AddChild(std::move(left));
        AddChild(std::move(right));
    }

    SizeF Measure(const SizeF &availableSize) override
    {
        const float column = std::max((availableSize.width - 24.0f) * 0.5f, 0.0f);
        float height = 0.0f;
        for (const auto &child : children_)
            height = std::max(height, child->MeasureInLayout({column, availableSize.height}).height);
        height_ = height;
        return {availableSize.width, height};
    }

    void Arrange(const RectF &finalRect) override
    {
        bounds_ = finalRect;
        const float column = std::max((finalRect.width - 24.0f) * 0.5f, 0.0f);
        for (size_t i = 0; i < children_.size(); ++i)
        {
            const SizeF size = children_[i]->MeasureInLayout({column, finalRect.height});
            children_[i]->ArrangeInLayout({finalRect.x + static_cast<float>(i) * (column + 24.0f),
                                           finalRect.y + (finalRect.height - size.height) * 0.5f, size.width,
                                           size.height});
        }
    }

    void Render(msimeui::DeviceResources &dr) override
    {
        for (const auto &child : children_)
            child->Render(dr);
    }

  private:
    float height_ = 0.0f;
};

// Title (+ description) where the title is a sub item (`.section-item-row .section-title`).
VisualPtr SubBlock(std::wstring title, std::wstring description, bool descriptionMargin = true)
{
    auto column = std::make_shared<Column>(0.0f);
    column->Add(MakeSubTitle(std::move(title), description));
    if (!description.empty())
    {
        auto desc = MakeDescription(std::move(description));
        if (!descriptionMargin)
            desc->SetMargin({0.0f, 0.0f, 0.0f, 0.0f});
        column->Add(desc);
    }
    return column;
}

std::shared_ptr<Label> Description(std::wstring text, float marginTop = 4.0f)
{
    auto label = MakeDescription(std::move(text));
    label->SetMargin({0.0f, marginTop, 0.0f, 0.0f});
    return label;
}

// `.ghost-button`: transparent, muted 12px text, 30px tall, 6px radius.
class GhostButton : public msimeui::Visual
{
  public:
    explicit GhostButton(std::wstring text) : text_(std::move(text))
    {
        SetMargin({0.0f, 2.0f, 0.0f, 0.0f});
    }

    void SetOnClick(std::function<void()> handler)
    {
        onClick_ = std::move(handler);
    }

    SizeF Measure(const SizeF &) override
    {
        return {MeasureTextWidth(text_, Style()) + 20.0f + CssBorder() * 2.0f, 30.0f};
    }

    void Arrange(const RectF &finalRect) override
    {
        bounds_ = finalRect;
    }

    void Render(msimeui::DeviceResources &dr) override
    {
        const Palette &p = CurrentPalette();
        if (hovered_)
            FillRounded(dr, bounds_, 6.0f, p.buttonSecondaryBg);
        StrokeRounded(dr, bounds_, 6.0f, p.border, 1.0f);
        DrawTextInRect(dr, text_, Style(), bounds_, hovered_ ? p.text : p.textMuted, true);
    }

    bool OnMouseDown(const POINT &point, WPARAM) override
    {
        pressed_ = HitSelf(window_, *this, point);
        return pressed_;
    }

    bool OnMouseUp(const POINT &point, WPARAM) override
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

    void OnMouseEnter() override
    {
        hovered_ = true;
        InvalidateVisual();
    }

    void OnMouseLeave() override
    {
        hovered_ = false;
        pressed_ = false;
        InvalidateVisual();
    }

    HCURSOR GetCursor() const override
    {
        return LoadCursor(nullptr, IDC_HAND);
    }

  private:
    static TextStyle Style()
    {
        TextStyle style;
        style.size = kFontCaption;
        return style;
    }

    std::wstring text_;
    bool hovered_ = false;
    bool pressed_ = false;
    std::function<void()> onClick_;
};

class BareRadio : public msimeui::Visual
{
  public:
    void SetOnSelected(std::function<void()> handler)
    {
        onSelected_ = std::move(handler);
    }

    void SetState(bool checked, bool disabled)
    {
        if (checked_ == checked && disabled_ == disabled)
            return;
        checked_ = checked;
        disabled_ = disabled;
        InvalidateVisual();
    }

    SizeF Measure(const SizeF &) override
    {
        return {5.0f + 13.0f + 3.0f, 3.0f + 13.0f};
    }

    void Arrange(const RectF &finalRect) override
    {
        bounds_ = finalRect;
    }

    void Render(msimeui::DeviceResources &dr) override
    {
        const bool light = CurrentPalette().light;
        // Chrome's form-control palette: accent #0075ff (light) / #99c8ff (dark).
        const D2D1_COLOR_F accent = light ? Hex(0x0075FF) : Hex(0x99C8FF);
        const D2D1_COLOR_F ring = light ? Hex(0x767676) : Hex(0x858585);
        const D2D1_COLOR_F fill = light ? Hex(0xFFFFFF) : Hex(0x3B3B3B);
        if (disabled_)
            PushOpacity(dr, 0.5f);
        const RectF box = {bounds_.x + 5.0f, bounds_.y + (bounds_.height - 16.0f) * 0.5f + 3.0f, 13.0f, 13.0f};
        FillRounded(dr, box, 6.5f, checked_ ? Over(WithAlpha(fill, 1.0f), CurrentPalette().chromeBg) : fill);
        StrokeRounded(dr, box, 6.5f, checked_ ? accent : ring, checked_ ? 1.5f : 1.0f);
        if (checked_)
            FillRounded(dr, {box.x + 3.5f, box.y + 3.5f, 6.0f, 6.0f}, 3.0f, accent);
        if (disabled_)
            PopOpacity(dr);
    }
    bool OnMouseDown(const POINT &point, WPARAM) override
    {
        pressed_ = !disabled_ && HitSelf(window_, *this, point);
        return pressed_;
    }

    bool OnMouseUp(const POINT &point, WPARAM) override
    {
        if (!pressed_)
            return false;
        pressed_ = false;
        if (HitSelf(window_, *this, point) && !checked_ && onSelected_)
        {
            const auto handler = onSelected_;
            handler();
        }
        return true;
    }

  private:
    bool checked_ = false;
    bool disabled_ = false;
    bool pressed_ = false;
    std::function<void()> onSelected_;
};

// ---------------------------------------------------------------------------------
// Model rows (shared.ts: seedCollocationRows / applyCollocationModelStatus /
// applyNeuralModelStatus)
// ---------------------------------------------------------------------------------
bool IsBusy(const std::string &state)
{
    return state == "downloading" || state == "importing";
}

bool UpdatePending(const Json *status)
{
    return status && status->is_object() && status->value("updatePending", false);
}

bool UpdateAvailable(const Json *status)
{
    return status && status->is_object() && status->value("updateAvailable", false);
}

std::wstring StatusText(const Json *status)
{
    if (UpdatePending(status))
        return L"新版本已下载，点「重启输入法」生效";
    const std::string state = status && status->is_object() ? status->value("state", std::string("absent")) : "absent";
    if (state == "ready")
        return UpdateAvailable(status) ? L"有新版本可用" : L"模型已就绪";
    if (state == "downloading")
    {
        const int progress =
            status->contains("progress") && (*status)["progress"].is_number() ? (*status)["progress"].get<int>() : 0;
        return L"下载中 " + std::to_wstring(progress) + L"%";
    }
    if (state == "importing")
        return L"导入并校验中…";
    if (state == "error")
    {
        std::string error = status->value("error", std::string());
        return L"下载失败：" + (error.empty() ? std::wstring(L"未知原因") : Widen(error));
    }
    return L"未下载";
}

std::string StateOf(const Json *status)
{
    return status && status->is_object() ? status->value("state", std::string("absent")) : "absent";
}

struct ModelRow
{
    std::string id;
    std::string url;
    std::shared_ptr<RichText> name;
    std::shared_ptr<Label> status;
    std::shared_ptr<BareRadio> radio;
    std::shared_ptr<Button> download;
    std::function<void()> downloadAction;
    // 已下载的模型把同一个按钮换成「检查更新」：只有用户点下去才发 HEAD 探测请求。
    std::function<void()> checkAction;
    std::function<void()> restartAction;
    std::shared_ptr<Hideable> importBox;
    std::shared_ptr<Button> import;
    std::shared_ptr<Hideable> removeBox;
    VisualPtr view;
};

std::vector<Span> NameSpans(const std::wstring &name, const std::wstring &meta, bool showLink,
                            std::function<void()> onLink)
{
    std::vector<Span> spans;
    spans.push_back({name, 13.0f, [] { return CurrentPalette().text; }});
    if (!meta.empty())
        spans.push_back({L" " + meta, kFontCaption, Muted()});
    if (showLink)
        spans.push_back({L" 浏览器下载",
                         kFontCaption,
                         [] { return CurrentPalette().accentStrong; },
                         {},
                         DWRITE_FONT_WEIGHT_NORMAL,
                         std::move(onLink)});
    return spans;
}

ModelRow MakeModelRow(PageContext &context, const std::string &id, const std::wstring &name, const std::wstring &meta,
                      const std::string &url, bool withRadio, const char *downloadType, const char *importType,
                      const char *deleteType, const char *checkType = nullptr)
{
    ModelRow row;
    row.id = id;
    row.url = url;
    SettingsModel *model = &context.model;
    row.name = std::make_shared<RichText>(13.0f, 0.0f, DWRITE_WORD_WRAPPING_WRAP);
    const std::string link = url;
    row.name->SetSpans(NameSpans(name, meta, !link.empty(), [model, link]() { model->Post("openExternalUrl", link); }));
    auto info = std::make_shared<Column>(0.0f);
    info->Add(row.name);
    row.status = Description(L"未下载");
    info->Add(row.status);

    auto actions = std::make_shared<Row>(8.0f);
    const bool hasId = !id.empty();
    const Json data = hasId ? Json{{"modelId", id}} : Json(nullptr);
    if (withRadio)
    {
        row.radio = std::make_shared<BareRadio>();
        row.radio->SetOnSelected([model, id]() { model->Update("association.sentence_collocation_model", id); });
        actions->Add(row.radio);
    }
    row.download = std::make_shared<Button>(L"下载", ButtonKind::Secondary);
    row.downloadAction = [model, downloadType, data]() { model->Post(downloadType, data); };
    if (checkType != nullptr && hasId)
        row.checkAction = [model, checkType, data]() { model->Post(checkType, data); };
    // 待重启时同一个按钮换成「重启」，直接走设置页已有的 restartServer 动作。
    row.restartAction = [model]() { model->Post("restartServer"); };
    row.download->SetOnClick(row.downloadAction);
    actions->Add(row.download);
    row.import = std::make_shared<Button>(L"导入", ButtonKind::Secondary);
    row.import->SetOnClick([model, importType, data]() { model->Post(importType, data); });
    row.importBox = std::make_shared<Hideable>(row.import, true);
    actions->Add(row.importBox);
    auto remove = std::make_shared<Button>(L"删除", ButtonKind::Secondary);
    remove->SetOnClick([model, deleteType, data]() { model->Post(deleteType, data); });
    row.removeBox = std::make_shared<Hideable>(remove, false);
    actions->Add(row.removeBox);

    auto line = std::make_shared<Row>(12.0f);
    line->Add(info, 1.0f);
    line->Add(actions);
    row.view = line;
    return row;
}

// ---------------------------------------------------------------------------------
// Page state
// ---------------------------------------------------------------------------------
struct InputPage
{
    PageContext *context = nullptr;

    // Input mode
    std::vector<std::shared_ptr<Hideable>> chineseSections;
    std::shared_ptr<RadioGroup> schemeGroup = std::make_shared<RadioGroup>();

    // Word to character
    std::shared_ptr<RadioGroup> wordKeys = std::make_shared<RadioGroup>();
    std::shared_ptr<RadioOption> wordKeysBrackets;
    std::shared_ptr<RadioOption> wordKeysMinusEqual;
    std::shared_ptr<RadioGroup> punctuationLock = std::make_shared<RadioGroup>();

    // Fuzzy / smart punctuation
    std::vector<std::shared_ptr<Dimmable>> fuzzyRules;
    std::shared_ptr<Hideable> fuzzyHint;
    std::vector<std::shared_ptr<ToggleSwitch>> smartOptions;
    std::shared_ptr<Hideable> smartHint;

    // Translation
    std::shared_ptr<ToggleSwitch> translationsToggle;
    std::shared_ptr<OpacityBox> translationOptions;

    // Models
    ModelRow lattice;
    std::shared_ptr<ToggleSwitch> latticeToggle;
    UINT_PTR latticePoll = 0;
    bool latticeLinkShown = false;
    std::string latticeUrl;
    ModelRow neural;
    std::shared_ptr<ToggleSwitch> neuralToggle;
    UINT_PTR neuralPoll = 0;
    bool neuralLinkShown = false;
    std::string neuralUrl;
    std::shared_ptr<Column> collocationList;
    std::vector<ModelRow> collocationRows;
    std::shared_ptr<ToggleSwitch> collocationToggle;
    UINT_PTR collocationPoll = 0;
};

// Section card that can be hidden (`hidden` attribute) without leaving its margin behind.
std::shared_ptr<Hideable> AddHideableSection(PageBuilder &page, VisualPtr content, bool visible = true)
{
    auto section = std::make_shared<Section>(std::move(content));
    section->SetMargin({0.0f, 0.0f, 0.0f, 18.0f});
    auto box = std::make_shared<Hideable>(section, visible);
    page.AddRaw(box, 0.0f);
    return box;
}

// `.content-page-option`: column, margin-top 8px, items separated by .input-option-divider.
std::shared_ptr<Column> OptionList(const std::vector<VisualPtr> &items, float marginTop = 8.0f)
{
    auto column = std::make_shared<Column>(0.0f);
    column->SetMargin({0.0f, marginTop, 0.0f, 0.0f});
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0)
            column->Add(std::make_shared<Divider>(Divider::Kind::Option));
        column->Add(items[i]);
    }
    return column;
}

std::shared_ptr<RadioOption> PaddedRadio(std::shared_ptr<RadioOption> radio)
{
    radio->SetInnerPadding({4.0f, 8.0f, 4.0f, 8.0f});
    return radio;
}

std::shared_ptr<Label> DisabledHint()
{
    TextStyle style;
    style.size = kFontCaption;
    auto label = std::make_shared<Label>(L"开启总开关后生效", style, TextRole::Muted);
    label->SetMargin({0.0f, 8.0f, 0.0f, 0.0f});
    return label;
}

// `.toggle-section-details .section-header { padding: 2px 0 }`
VisualPtr DetailsRow(std::wstring title, std::wstring description, VisualPtr control)
{
    return Padded(
        std::make_shared<SettingRow>(SubBlock(std::move(title), std::move(description), false), std::move(control)),
        {0.0f, 2.0f, 0.0f, 2.0f});
}

// `.frequency-option-row { padding: 10px 0 }`
VisualPtr FrequencyRow(VisualPtr left, VisualPtr control)
{
    return Padded(std::make_shared<SettingRow>(std::move(left), std::move(control)), {0.0f, 10.0f, 0.0f, 10.0f});
}

// `.translation-language-row { padding: 14px 0; gap: 16px }`
VisualPtr LanguageRow(std::wstring title, std::wstring description, VisualPtr control)
{
    auto left = SubBlock(std::move(title), std::move(description));
    left->SetMargin({0.0f, 0.0f, 6.0f, 0.0f});
    return Padded(std::make_shared<SettingRow>(left, std::move(control)), {0.0f, 14.0f, 0.0f, 14.0f});
}

// The expandable header (`.toggle-section-header`) with its master switch, and the details.
struct ToggleSection
{
    std::shared_ptr<Expander> expander;
    std::shared_ptr<Hideable> details;
};

ToggleSection MakeToggleSection(std::wstring title, VisualPtr detailsContent)
{
    ToggleSection section;
    section.expander = MakeExpander(std::move(title));
    detailsContent->SetMargin({18.0f, 4.0f, 0.0f, 0.0f});
    section.details = std::make_shared<Hideable>(detailsContent, false);
    BindExpander(section.expander, section.details);
    return section;
}
} // namespace

namespace
{
std::shared_ptr<Column> Col(std::initializer_list<VisualPtr> items, float gap = 0.0f)
{
    auto column = std::make_shared<Column>(gap);
    for (const auto &item : items)
        column->Add(item);
    return column;
}

std::shared_ptr<ToggleSwitch> Toggle(PageContext &context, const char *path)
{
    return BindToggle(context, path);
}

VisualPtr WithMarginTop(VisualPtr visual, float top)
{
    visual->SetMargin({0.0f, top, 0.0f, 0.0f});
    return visual;
}

VisualPtr ItemSetting(std::wstring title, std::wstring description, VisualPtr control, bool first, bool last)
{
    auto row = std::make_shared<ItemRow>(
        std::make_shared<SettingRow>(SubBlock(std::move(title), std::move(description)), std::move(control)), last);
    if (first)
        row->SetMargin({0.0f, 12.0f, 0.0f, 0.0f});
    return row;
}
} // namespace

VisualPtr BuildInputPage(PageContext &context)
{
    auto state = std::make_shared<InputPage>();
    state->context = &context;
    InputPage *s = state.get();
    SettingsModel *model = &context.model;
    HostServices *host = &context.host;
    PageBuilder page(L"输入");

    // ---- 输入模式 ----

    // ---- 输入方案 ----
    {
        auto quanpin = s->schemeGroup->Add(L"全拼", "quanpin");
        s->schemeGroup->SetValue("quanpin");
        s->schemeGroup->SetOnChanged([model](const std::string &value) { model->Update("input.schema", value); });
        // .input-option-content: margin-top 12px, gap 8px
        auto options = Col({quanpin}, 8.0f);
        options->SetMargin({0.0f, 12.0f, 0.0f, 0.0f});
        s->chineseSections.push_back(AddHideableSection(page, Col({MakeTitle(L"输入方案"), options})));
    }

    // ---- 输入纠错 ----
    page.AddSection(
        Col({MakeTitleBlock(
                 L"输入纠错",
                 L"全拼输错时按误键类型给出纠正候选，默认关闭；开启后预编辑始终显示原始输入，纠正结果仅出现在候选中"),
             ItemSetting(L"乱序纠错", L"相邻字母顺序打错时纠正，如 sahng → shang（上）；默认关闭",
                         Toggle(context, "quanpin.autocorrect_transposition"), true, false),
             ItemSetting(L"邻键纠错", L"误触相邻按键时纠正，如 shabg → shang（上）；默认关闭",
                         Toggle(context, "quanpin.autocorrect_neighbor"), false, false),
             ItemSetting(L"漏字纠错", L"漏敲一个字母时纠正，如 shng → shang（上）；默认关闭",
                         Toggle(context, "quanpin.autocorrect_deletion"), false, false),
             ItemSetting(L"多字纠错", L"多敲一个相邻按键时纠正，如 sjhang → shang（上）；默认关闭",
                         Toggle(context, "quanpin.autocorrect_insertion"), false, false),
             ItemSetting(L"显示纠错标记", L"纠错得到的候选末尾显示 * 号，便于和正常候选区分；不影响上屏文字",
                         Toggle(context, "quanpin.autocorrect_marker"), false, true)}));

    // ---- 模糊音 ----
    {
        auto master = BindToggle(context, "input.fuzzy_pinyin");
        const std::vector<std::pair<const wchar_t *, const char *>> initialRules = {
            {L"z ⇄ zh", "fuzzy_z_zh"}, {L"c ⇄ ch", "fuzzy_c_ch"}, {L"s ⇄ sh", "fuzzy_s_sh"},
            {L"n ⇄ l", "fuzzy_n_l"},   {L"f ⇄ h", "fuzzy_f_h"},   {L"r ⇄ l", "fuzzy_r_l"}};
        const std::vector<std::pair<const wchar_t *, const char *>> finalRules = {{L"an ⇄ ang", "fuzzy_an_ang"},
                                                                                  {L"en ⇄ eng", "fuzzy_en_eng"},
                                                                                  {L"in ⇄ ing", "fuzzy_in_ing"},
                                                                                  {L"ian ⇄ iang", "fuzzy_ian_iang"},
                                                                                  {L"uan ⇄ uang", "fuzzy_uan_uang"}};
        std::vector<VisualPtr> initials;
        for (const auto &[label, key] : initialRules)
            initials.push_back(BindCheck(context, label, std::string("input.") + key));
        std::vector<VisualPtr> finals;
        for (const auto &[label, key] : finalRules)
            finals.push_back(BindCheck(context, label, std::string("input.") + key));
        auto initialList = OptionList(initials);
        auto finalList = OptionList(finals);
        s->fuzzyRules = {std::make_shared<Dimmable>(initialList), std::make_shared<Dimmable>(finalList)};
        s->fuzzyHint = std::make_shared<Hideable>(DisabledHint(), false);
        auto details = Col({Description(L"开启后两个读音互相匹配，如输入 zhang 也可命中 zang 开头的词", 0.0f),
                            Padded(MakeSubTitle(L"声母"), {0.0f, 2.0f, 0.0f, 2.0f}), s->fuzzyRules[0],
                            Padded(MakeSubTitle(L"韵母"), {0.0f, 2.0f, 0.0f, 2.0f}), s->fuzzyRules[1], s->fuzzyHint});
        auto section = MakeToggleSection(L"模糊音", details);
        page.AddSection(Col({std::make_shared<SettingRow>(section.expander, master), section.details}));
    }

    // ---- 候选词翻译 ----
    {
        auto master = std::make_shared<ToggleSwitch>(false);
        auto options =
            Col({std::make_shared<Divider>(Divider::Kind::Option),
                 LanguageRow(L"显示词性", L"显示译文中的 n.、v. 等词性标记，关闭后仅显示释义",
                             BindToggle(context, "general.candidate_translation_show_pos")),
                 std::make_shared<Divider>(Divider::Kind::Option),
                 LanguageRow(L"只显示单条释义", L"每个候选只显示第一条译文释义；Ctrl+Enter 仍可选择全部释义上屏",
                             BindToggle(context, "general.candidate_translation_single_sense"))});
        s->translationOptions = std::make_shared<OpacityBox>(options);
        s->translationOptions->SetMargin({0.0f, 8.0f, 0.0f, 0.0f});
        master->SetOnChanged([s, model](bool on) {
            model->Update("general.candidate_translations", on);
            s->translationOptions->SetDimmed(!on);
        });
        page.AddSection(
            Col({MakeRow(L"候选词翻译", L"在候选窗口中显示本机中英词库的互译，每项最多两个简短释义", master),
                 s->translationOptions}));
        s->translationsToggle = master;
    }

    // ---- 候选词英文行 ----
    page.AddSection(MakeRow(L"候选词英文行",
                            L"在候选条下面固定一行，显示当前高亮候选的第一个英文词义；Ctrl+Enter 直接上屏，"
                            L"Ctrl+Shift+Enter 查看全部词义",
                            Toggle(context, "general.english_line")));

    // ---- 以词定字 ----
    {
        s->wordKeysBrackets = PaddedRadio(s->wordKeys->Add(L"[ / ]", "brackets"));
        s->wordKeysMinusEqual = PaddedRadio(s->wordKeys->Add(L"- / =", "minus_equal"));
        s->wordKeys->SetValue("brackets");
        s->wordKeysMinusEqual->SetDisabled(true);
        s->wordKeys->SetOnChanged(
            [model](const std::string &value) { model->Update("input.word_to_character_keys", value); });
        auto keys = std::make_shared<TopRuled>(
            Col({MakeSubTitle(L"以词定字快捷键"), OptionList({s->wordKeysBrackets, s->wordKeysMinusEqual})}), 16.0f);
        keys->SetMargin({0.0f, 18.0f, 0.0f, 0.0f});
        page.AddSection(Col({MakeRow(L"以词定字", L"开启后，按所选键组的左键上屏高亮候选的首个汉字，右键上屏末个汉字",
                                     Toggle(context, "input.word_to_character")),
                             keys}));
    }

    page.AddSection(MakeRow(L"Esc 保留已选词",
                            L"造词时按 Esc 只清除还没选的拼音、保留已选的词，再按一次 Esc 整体取消（小狼毫风格）",
                            Toggle(context, "input.escape_keeps_selected_word")));
    page.AddSection(MakeRow(L"回车上屏英文时入库",
                            L"按回车上屏输入的英文时，把这个单词写入英文词库，之后可作为候选出现",
                            Toggle(context, "input.enter_learns_english_word")));

    // ---- 固定标点 ----
    {
        auto follow = PaddedRadio(s->punctuationLock->Add(L"跟随中英文状态", "follow"));
        auto chinese = PaddedRadio(s->punctuationLock->Add(L"始终使用中文标点", "chinese"));
        auto english = PaddedRadio(s->punctuationLock->Add(L"始终使用英文标点", "english"));
        s->punctuationLock->SetValue("follow");
        s->punctuationLock->SetOnChanged(
            [model](const std::string &value) { model->Update("input.punctuation_lock", value); });
        page.AddSection(Col({MakeTitle(L"固定标点"), Description(L"切换中英文时的标点形态，三者互斥"),
                             OptionList({follow, chinese, english})}));
    }

    // ---- 顿号自定义 ----
    {
        std::vector<VisualPtr> keys;
        for (auto &check : AtLeastOneChecks(context, {{L"\\ 反斜杠", "input.enumeration_comma_backslash"},
                                                      {L"/ 斜杠", "input.enumeration_comma_slash"}}))
            keys.push_back(check);
        page.AddSection(Col({MakeTitle(L"顿号自定义"),
                             Description(L"中文标点状态下用哪些键输出顿号「、」，至少保留一个；未勾选的键按原样输出"),
                             OptionList(keys)}));
    }

    // ---- 智能标点 ----
    {
        auto master = BindToggle(context, "input.smart_punctuation");
        auto option = [s, &context](const char *path) {
            auto toggle = BindToggle(context, path);
            s->smartOptions.push_back(toggle);
            return toggle;
        };
        s->smartHint = std::make_shared<Hideable>(DisabledHint(), false);
        auto details = Col({Description(L"中文标点模式下的标点自动转换", 0.0f),
                            DetailsRow(L"中文标点后按空格转换",
                                       L"。，！？；：、 以及单独出现的 “ ” 【 】 《 》 （ ） 转为对应英文标点",
                                       option("input.smart_punctuation_space_convert")),
                            DetailsRow(L"同键转回中文", L"转换或直出的英文标点，2 秒内再按同一标点键转回中文",
                                       option("input.smart_punctuation_repeat_to_chinese")),
                            DetailsRow(L"数字后直出", L"数字后输入 , . : 直接输出英文标点（如 3.14）",
                                       option("input.smart_punctuation_direct_digit")),
                            DetailsRow(L"字母后直出", L"字母后输入 , . : 直接输出英文标点",
                                       option("input.smart_punctuation_direct_letter")),
                            s->smartHint});
        auto section = MakeToggleSection(L"智能标点", details);
        page.AddSection(Col({std::make_shared<SettingRow>(section.expander, master), section.details}));
    }

    page.AddSection(MakeRow(L"成对标点自动补全", L"输入左侧符号时自动补全右侧符号，并将光标置于中间",
                            Toggle(context, "input.paired_punctuation")));
    page.AddSection(MakeRow(
        L"默认中英文", L"",
        BindDropdown(context, "input.default_ime_mode", Options({{"chinese", L"中文"}, {"english", L"英文"}}))));
    page.AddSection(MakeRow(
        L"中英文状态", L"",
        BindDropdown(context, "input.ime_mode_scope", Options({{"app", L"按应用记忆"}, {"global", L"全局统一"}}))));
    page.AddSection(MakeRow(
        L"简繁输入", L"",
        BindDropdown(context, "input.character_set", Options({{"simplified", L"简体"}, {"traditional", L"繁体"}}))));

    // ---- 整句候选 ----
    {
        // 按需下载的模型：紧贴在各自开关下面，标题带折叠箭头，默认折叠。标题比上面开关的标题小一号，
        // 读起来是那个开关的附属项。
        auto downloadSection = [](std::wstring title, std::wstring description, const ModelRow &row) {
            TextStyle titleStyle = SubItemTitleStyle();
            titleStyle.size = 13.0f;
            auto expander = MakeExpander(std::move(title), titleStyle);
            auto list = Col({row.view}, 10.0f);
            list->SetMargin({0.0f, 6.0f, 0.0f, 0.0f});
            auto body = Col({MakeDescription(std::move(description)), list});
            body->SetMargin({18.0f, 4.0f, 10.0f, 0.0f});
            auto details = std::make_shared<Hideable>(body, false);
            BindExpander(expander, details);
            return Padded(Col({expander, details}), {0.0f, 0.0f, 0.0f, 10.0f});
        };

        s->lattice = MakeModelRow(context, "", L"词格语言模型 sc.lm", L"约 35 MB · LGPL-2.1", "", false,
                                  "latticeModelDownload", "latticeModelImport", "latticeModelDelete");
        auto latticeSection = downloadSection(
            L"Trigram 模型下载",
            L"三元语言模型不随安装包分发，下载后才能开启上面的 Trigram 整句开关，可随时删除；神经网络整句也会用它"
            L"给备选句打分。应用内下载较慢时，可点「浏览器下载」用浏览器下好，再点「导入」选中该 sc.lm 文件；"
            L"也可以在「帮助」页设置网络代理",
            s->lattice);
        s->latticeToggle = BindToggle(context, "association.sentence_wordlattice");

        s->neural = MakeModelRow(context, "", L"chinese-ime-lm 效果优先模型", L"约 25 MB · Apache-2.0", "", false,
                                 "neuralModelDownload", "neuralModelImport", "neuralModelDelete");
        auto neuralSection =
            downloadSection(L"神经网络模型（效果优先）下载",
                            L"效果优先模型不随安装包分发，下载后才能开启上面的整句开关，可随时删除。"
                            L"应用内下载较慢时，可点「浏览器下载」用浏览器下好，再点「导入」选中该 .safetensors 文件；"
                            L"也可以在「帮助」页设置网络代理",
                            s->neural);
        s->neuralToggle = BindToggle(context, "association.sentence_neural_desktop");

        s->collocationList = std::make_shared<Column>(10.0f);
        s->collocationList->SetMargin({0.0f, 6.0f, 0.0f, 0.0f});
        auto collocationBlock =
            Col({MakeSubTitle(L"octagram 语法模型（八股文，需先下载）"),
                 MakeDescription(
                     L"内置目录里的模型包各自下载、状态独立，激活其中一个用于整句加成；默认未选择任何模型"
                     L"（整句加成保持关闭），下载后单选激活，可随时切换或删除。应用内下载较慢时，可点「浏览器下载」"
                     L"用浏览器下好，再点「导入」选中该 .gram 文件；也可以在「帮助」页设置网络代理"),
                 s->collocationList});
        collocationBlock->SetMargin({0.0f, 0.0f, 10.0f, 0.0f});
        s->collocationToggle = BindToggle(context, "association.sentence_collocation_enabled");

        auto content = Col({
            FrequencyRow(SubBlock(L"Trigram 模型整句", L"本地生成，下载模型后可用：使用三元语言模型生成整句候选"),
                         s->latticeToggle),
            latticeSection,
            FrequencyRow(SubBlock(L"Unigram 模型整句", L"本地生成，使用一元语言模型生成整句候选"),
                         Toggle(context, "association.sentence_google")),
            FrequencyRow(
                SubBlock(
                    L"神经网络模型整句（效果优先）",
                    L"本地生成，下载模型后可用：结合已上屏的前文，从内部生成的最多 12 个备选句中选择，处理时间较长"),
                s->neuralToggle),
            neuralSection,
            FrequencyRow(SubBlock(L"神经网络模型整句（速度优先）",
                                  L"本地生成，使用轻量模型从最多 12 个备选句中选择；两种模式可同时独立给出结果"),
                         Toggle(context, "association.sentence_neural_keyboard")),
            FrequencyRow(
                SubBlock(
                    L"上文词库重排（实验）",
                    L"使用轻量神经模型，根据本次输入的上文调整单字和词组顺序，例如“冬天会下大”后输入 xue 优先选“雪”"),
                Toggle(context, "association.context_candidate_rerank")),
            FrequencyRow(SubBlock(L"长词前缀补全",
                                  L"本地处理，首选已有 4 个字以上时，把词库里以它开头的最可能的长词放在第二位，例如"
                                  L"“中华人民”补出“中华人民共和国”；下载万象或 Trigram 模型后排得更准"),
                         Toggle(context, "association.prefix_completion")),
            FrequencyRow(SubBlock(L"去重后补充次选",
                                  L"本地处理，某个模型的首选与已有候选重复时，显示该模型排名最高的下一条不同结果"),
                         Toggle(context, "association.sentence_show_next_on_duplicate")),
            FrequencyRow(SubBlock(L"显示整句来源标签",
                                  L"本地处理，在整句候选后显示〔Trigram〕〔Unigram〕〔神经D〕〔神经K〕〔万象〕"
                                  L"〔八股〕〔墨奇〕等来源标签"),
                         Toggle(context, "association.sentence_source_badge")),
            Padded(collocationBlock, {0.0f, 10.0f, 0.0f, 10.0f}),
            FrequencyRow(
                SubBlock(L"octagram 语法整句",
                         L"本地生成，下载模型后可用：一并接入该模型的全部能力——解码时按字级搭配分选出更准的首选，"
                         L"并把搭配紧密的整句提到前面，以所选模型自己的来源行显示（万象〔万象〕、八股文〔"
                         L"八股〕、白霜〔墨奇〕）"),
                s->collocationToggle),
        });
        content->SetMargin({0.0f, 8.0f, 0.0f, 0.0f});
        page.AddSection(
            Col({MakeTitleBlock(L"整句候选", L"输入多个拼音音节时，额外提供本机模型给出的整句候选"), content}));
    }

    // ---- 拼音方案调频 ----
    {
        Dropdown::Options frequency;
        frequency.minWidth = 110.0f;
        frequency.menuMinWidth = 110.0f;
        auto content = Col({FrequencyRow(MakeSubTitle(L"调频方式"), BindDropdown(context, "frequency_adjustment.mode",
                                                                                 Options({{"disabled", L"关闭"},
                                                                                          {"pin", L"一次置顶"},
                                                                                          {"halve", L"折半调频"},
                                                                                          {"linear", L"线性调频"},
                                                                                          {"promote", L"一次置前"}}),
                                                                                 frequency)),
                            std::make_shared<Divider>(Divider::Kind::Option),
                            FrequencyRow(MakeSubTitle(L"触发频次(第几次上屏触发)"),
                                         BindDropdown(context, "frequency_adjustment.trigger_count",
                                                      NumberOptions(1, 6), frequency, ValueKind::Integer)),
                            std::make_shared<Divider>(Divider::Kind::Option),
                            FrequencyRow(MakeSubTitle(L"线性调频步长"),
                                         BindDropdown(context, "frequency_adjustment.linear_step", NumberOptions(1, 6),
                                                      frequency, ValueKind::Integer))});
        content->SetMargin({0.0f, 8.0f, 0.0f, 0.0f});
        page.AddSection(Col({MakeTitle(L"拼音方案调频"), content}));
    }

    // ---- Snapshot -> page (config-sync.ts) ----
    context.OnConfig([s, state, model, host, &context]() {
        (void)state;
        s->schemeGroup->SetValue("quanpin");

        // 以词定字: a key pair that pages candidates cannot also pick characters.
        if (const Json *paging = model->Find("general.paging_brackets"); paging && paging->is_boolean())
            s->wordKeysBrackets->SetDisabled(paging->get<bool>());
        if (const Json *paging = model->Find("general.paging_minus_equal"); paging && paging->is_boolean())
            s->wordKeysMinusEqual->SetDisabled(paging->get<bool>());
        if (const Json *keys = model->Find("input.word_to_character_keys"); keys && keys->is_string())
            s->wordKeys->SetValue(keys->get<std::string>());
        const std::string lock = model->GetString("input.punctuation_lock");
        if (lock == "follow" || lock == "chinese" || lock == "english")
            s->punctuationLock->SetValue(lock);

        if (const Json *smart = model->Find("input.smart_punctuation"); smart && smart->is_boolean())
        {
            for (auto &toggle : s->smartOptions)
                toggle->SetDisabled(!smart->get<bool>());
            s->smartHint->SetVisible(!smart->get<bool>());
        }
        if (const Json *fuzzy = model->Find("input.fuzzy_pinyin"); fuzzy && fuzzy->is_boolean())
        {
            for (auto &rules : s->fuzzyRules)
                rules->SetDisabled(!fuzzy->get<bool>());
            s->fuzzyHint->SetVisible(!fuzzy->get<bool>());
        }

        if (const Json *translations = model->Find("general.candidate_translations");
            translations && translations->is_boolean())
        {
            s->translationsToggle->SetChecked(translations->get<bool>());
            s->translationOptions->SetDimmed(!translations->get<bool>());
        }
        // Lattice language model row. Server 映射着 sc.lm 时删除只留标记（deletePending），
        // 下载按钮换成「重启输入法」，重启后 Server 在映射前把文件删掉。
        {
            const Json *status = model->Find("association.sentence_wordlattice_model_status");
            const std::string stateName = StateOf(status);
            const bool busy = IsBusy(stateName);
            const bool deletePending = status && status->is_object() && status->value("deletePending", false);
            s->lattice.status->SetText(deletePending ? L"已删除，重启输入法后释放文件" : StatusText(status));
            s->lattice.download->SetText(deletePending ? L"重启输入法" : L"下载");
            s->lattice.download->SetOnClick(deletePending ? s->lattice.restartAction : s->lattice.downloadAction);
            s->lattice.download->SetDisabled(!deletePending && (stateName == "ready" || busy));
            s->lattice.importBox->SetVisible(stateName != "ready" && !deletePending);
            s->lattice.import->SetDisabled(busy);
            s->lattice.removeBox->SetVisible(stateName == "ready");
            const std::string url = model->GetString("association.sentence_wordlattice_model_url");
            const bool showLink = !url.empty() && stateName != "ready" && !deletePending;
            if (showLink != s->latticeLinkShown || url != s->latticeUrl)
            {
                s->latticeLinkShown = showLink;
                s->latticeUrl = url;
                s->lattice.name->SetSpans(NameSpans(L"词格语言模型 sc.lm", L"约 35 MB · LGPL-2.1", showLink,
                                                    [model, url]() { model->Post("openExternalUrl", url); }));
            }
            // 模型没就绪时开关置灰并显示为关：Server 那边同样只在模型在时才出 Trigram 整句。
            // 绑定已按配置值设过一次，这里在它之后跑，只需在未就绪时覆盖成关。
            const bool ready = stateName == "ready";
            s->latticeToggle->SetDisabled(!ready);
            if (!ready)
                s->latticeToggle->SetChecked(false);
            // 待重启期间也轮询：点「重启」后 Server 删掉文件，下一份快照就回到「未下载」。
            if ((busy || deletePending) && s->latticePoll == 0)
                s->latticePoll = PageTimers::Start(host->Hwnd(), 2000, true,
                                                   [model]() { model->Post("latticeModelStatusRequest"); });
            else if (!busy && !deletePending)
                PageTimers::Stop(s->latticePoll);
        }

        // Neural model row (applyNeuralModelStatus).
        {
            const Json *status = model->Find("association.sentence_neural_desktop_model_status");
            const std::string stateName = StateOf(status);
            const bool busy = IsBusy(stateName);
            s->neural.status->SetText(StatusText(status));
            s->neural.download->SetDisabled(stateName == "ready" || busy);
            s->neural.importBox->SetVisible(stateName != "ready");
            s->neural.import->SetDisabled(busy);
            s->neural.removeBox->SetVisible(stateName == "ready");
            const std::string url = model->GetString("association.sentence_neural_desktop_model_url");
            const bool showLink = !url.empty() && stateName != "ready";
            if (showLink != s->neuralLinkShown || url != s->neuralUrl)
            {
                s->neuralLinkShown = showLink;
                s->neuralUrl = url;
                s->neural.name->SetSpans(NameSpans(L"chinese-ime-lm 效果优先模型", L"约 25 MB · Apache-2.0", showLink,
                                                   [model, url]() { model->Post("openExternalUrl", url); }));
            }
            // 同 Trigram：未下载时置灰并显示为关，引擎在模型缺席时本来也不重排。
            s->neuralToggle->SetDisabled(stateName != "ready");
            if (stateName != "ready")
                s->neuralToggle->SetChecked(false);
            if (busy && s->neuralPoll == 0)
                s->neuralPoll =
                    PageTimers::Start(host->Hwnd(), 2000, true, [model]() { model->Post("neuralModelStatusRequest"); });
            else if (!busy)
                PageTimers::Stop(s->neuralPoll);
        }

        // Collocation rows (seedCollocationRows + applyCollocationModelStatus).
        {
            const Json *catalog = model->Find("association.sentence_collocation_catalog");
            if (s->collocationRows.empty() && catalog && catalog->is_array() && !catalog->empty())
            {
                for (const Json &entry : *catalog)
                {
                    if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string())
                        continue;
                    const std::string id = entry["id"].get<std::string>();
                    std::wstring meta;
                    for (const char *key : {"sizeHint", "license"})
                    {
                        const std::string part = entry.value(key, std::string());
                        if (part.empty())
                            continue;
                        if (!meta.empty())
                            meta += L" · ";
                        meta += Widen(part);
                    }
                    ModelRow row =
                        MakeModelRow(context, id, Widen(entry.value("displayName", id)), meta,
                                     entry.value("url", std::string()), true, "collocationModelDownload",
                                     "collocationModelImport", "collocationModelDelete", "collocationModelCheckUpdate");
                    if (msimeui::Window *window = context.host.Window())
                        row.view->Attach(window);
                    s->collocationList->Add(row.view);
                    s->collocationRows.push_back(std::move(row));
                }
            }
            const Json *statuses = model->Find("association.sentence_collocation_model_status");
            const std::string active = model->GetString("association.sentence_collocation_model");
            bool anyBusy = false;
            bool anyPending = false;
            if (statuses && statuses->is_object())
            {
                for (const auto &item : statuses->items())
                {
                    anyBusy = anyBusy || IsBusy(StateOf(&item.value()));
                    anyPending = anyPending || UpdatePending(&item.value());
                }
            }
            for (ModelRow &row : s->collocationRows)
            {
                const Json *status =
                    statuses && statuses->is_object() && statuses->contains(row.id) ? &(*statuses)[row.id] : nullptr;
                const std::string stateName = StateOf(status);
                const bool pending = UpdatePending(status);
                const bool update_available = UpdateAvailable(status);
                row.status->SetText(StatusText(status));
                // 每次快照都重设按钮文本和动作，避免上一次的「更新 / 重启」残留到别的状态。
                // 待重启时按钮直接变成可点的「重启」，点下去走设置页已有的 restartServer。
                // 已就绪且没有已知更新时按钮是「检查更新」：点了才向远端发探测请求，不点不联网。
                const bool can_check = stateName == "ready" && !update_available && row.checkAction;
                row.download->SetText(pending            ? L"重启输入法"
                                      : update_available ? L"更新"
                                      : can_check        ? L"检查更新"
                                                         : L"下载");
                row.download->SetOnClick(pending     ? row.restartAction
                                         : can_check ? row.checkAction
                                                     : row.downloadAction);
                row.download->SetDisabled(pending ? false : ((stateName == "ready" && !can_check) || anyBusy));
                row.importBox->SetVisible(stateName != "ready");
                row.import->SetDisabled(anyBusy);
                row.removeBox->SetVisible(stateName == "ready");
                row.radio->SetState(active == row.id, stateName != "ready");
            }
            const Json *activeStatus =
                statuses && statuses->is_object() && statuses->contains(active) ? &(*statuses)[active] : nullptr;
            s->collocationToggle->SetDisabled(StateOf(activeStatus) != "ready");
            // 待重启期间也轮询：点「重启」后 Server 落位完，下一份快照就能把按钮恢复成「下载」。
            if ((anyBusy || anyPending) && s->collocationPoll == 0)
                s->collocationPoll = PageTimers::Start(host->Hwnd(), 2000, true,
                                                       [model]() { model->Post("collocationModelStatusRequest"); });
            else if (!anyBusy && !anyPending)
                PageTimers::Stop(s->collocationPoll);
        }
    });

    return page.Build();
}
} // namespace native_settings
