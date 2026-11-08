#include "settings/native/native_settings_pages.h"

#include "msimeui/DeviceResources.h"
#include "msimeui/Fonts.h"
#include "msimeui/Window.h"

#include <dwrite_1.h>

#include <algorithm>
#include <iterator>

// partials/helpcode.html + modules/helpcode.ts + styles/modules/helpcode.css
namespace native_settings
{
namespace
{
using Microsoft::WRL::ComPtr;

// A paragraph mixing the page font with <code> runs (monospace), for the custom
// helpcode description. `breakAll` mirrors `word-break: break-all`.
class RichText : public msimeui::Visual
{
  public:
    struct Run
    {
        std::wstring text;
        bool code = false;
    };

    RichText(std::vector<Run> runs, bool breakAll = false) : runs_(std::move(runs)), breakAll_(breakAll)
    {
    }

    void SetRuns(std::vector<Run> runs)
    {
        runs_ = std::move(runs);
        layout_.Reset();
        InvalidateMeasure();
        InvalidateVisual();
    }

    SizeF Measure(const SizeF &availableSize) override
    {
        IDWriteTextLayout *layout = Get(availableSize.width);
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
        IDWriteTextLayout *layout = Get(bounds_.width);
        ID2D1RenderTarget *target = dr.GetRenderTarget();
        ID2D1SolidColorBrush *brush = target ? dr.GetSolidColorBrush(CurrentPalette().textMuted) : nullptr;
        if (layout && brush)
            target->DrawTextLayout(D2D1::Point2F(bounds_.x, bounds_.y), layout, brush,
                                   D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }

  private:
    IDWriteTextLayout *Get(float width)
    {
        width = std::max(width, 1.0f);
        if (layout_ && width_ == width)
            return layout_.Get();
        layout_.Reset();
        width_ = width;
        IDWriteFactory *factory = DWriteFactory();
        if (!factory)
            return nullptr;
        if (!format_)
        {
            if (FAILED(factory->CreateTextFormat(UiFontFamily().c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, kFontCaption,
                                                 L"zh-cn", format_.GetAddressOf())))
                return nullptr;
            msimeui::ApplyUiFontFallback(factory, format_.Get());
        }
        std::wstring text;
        std::vector<DWRITE_TEXT_RANGE> codes;
        for (const Run &run : runs_)
        {
            if (run.code)
                codes.push_back({static_cast<UINT32>(text.size()), static_cast<UINT32>(run.text.size())});
            text += run.text;
        }
        if (FAILED(factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format_.Get(), width,
                                             100000.0f, layout_.GetAddressOf())))
            return nullptr;
        layout_->SetWordWrapping(breakAll_ ? DWRITE_WORD_WRAPPING_CHARACTER : DWRITE_WORD_WRAPPING_WRAP);
        // line-height: 1.6, the extra space split above and below each line.
        UINT32 count = 0;
        layout_->GetLineMetrics(nullptr, 0, &count);
        std::vector<DWRITE_LINE_METRICS> lines(std::max<UINT32>(count, 1));
        layout_->GetLineMetrics(lines.data(), static_cast<UINT32>(lines.size()), &count);
        const DWRITE_LINE_METRICS &line = lines[0];
        if (count > 0 && line.height > 0.0f)
        {
            const float spacing = kFontCaption * kLineHeightCaption;
            layout_->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, spacing,
                                    line.baseline + (spacing - line.height) * 0.5f);
        }
        for (const DWRITE_TEXT_RANGE &range : codes)
            layout_->SetFontFamilyName(L"Consolas", range);
        return layout_.Get();
    }

    std::vector<Run> runs_;
    bool breakAll_ = false;
    float width_ = -1.0f;
    ComPtr<IDWriteTextFormat> format_;
    ComPtr<IDWriteTextLayout> layout_;
};

// .helpcode-custom-button: 30px, transparent, muted 12px text, 6px radius.
class CustomButton : public msimeui::Visual
{
  public:
    CustomButton(std::wstring text, std::function<void()> onClick)
        : text_(std::move(text)), onClick_(std::move(onClick))
    {
        // margin-top: 2px
        SetMargin({0.0f, 2.0f, 0.0f, 0.0f});
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

    bool OnMouseDown(const POINT &, WPARAM) override
    {
        pressed_ = true;
        return true;
    }

    bool OnMouseUp(const POINT &point, WPARAM) override
    {
        const bool clicked = pressed_ && window_ && HitTest(window_->ClientPixelsToDips(point));
        pressed_ = false;
        if (clicked && onClick_)
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
    std::function<void()> onClick_;
    bool hovered_ = false;
    bool pressed_ = false;
};

std::vector<DropdownOption> BuiltinSchemes()
{
    return Options({{"lantian", L"蓝天小雨点"},
                    {"ziranma", L"自然码"},
                    {"shouyou2_0", L"首右2.0"},
                    {"shouyouplus", L"首右plus"},
                    {"xiaohe", L"小鹤"},
                    {"jiajia", L"拼音加加"}});
}

// Built-in schemes followed by the custom ones the host scanned from helpcodes/custom.
std::vector<DropdownOption> SchemeOptions(const SettingsModel &model)
{
    std::vector<DropdownOption> options = BuiltinSchemes();
    if (const Json *schemas = model.Find("helpcode.custom_schemas"); schemas && schemas->is_array())
    {
        for (const Json &schema : *schemas)
        {
            if (!schema.is_object() || !schema.contains("id") || !schema["id"].is_string() ||
                !schema.contains("name") || !schema["name"].is_string())
                continue;
            options.push_back({Widen(schema["name"].get<std::string>()), schema["id"].get<std::string>(), false, {}});
        }
    }
    return options;
}

// Scheme dropdown whose options follow the snapshot (setupDropdownMenu + config-sync).
std::shared_ptr<Dropdown> SchemeDropdown(PageContext &context, std::string path)
{
    Dropdown::Options settings;
    settings.minWidth = 110.0f;
    settings.paddingLeft = 9.0f;
    auto dropdown = std::make_shared<Dropdown>(BuiltinSchemes(), settings);
    SettingsModel *model = &context.model;
    dropdown->SetOnChanged([model, path](size_t, const DropdownOption &option) { model->Update(path, option.value); });
    std::weak_ptr<Dropdown> weak = dropdown;
    context.OnConfig([weak, model, path]() {
        auto control = weak.lock();
        if (!control)
            return;
        std::vector<DropdownOption> options = SchemeOptions(*model);
        const auto &current = control->GetOptions();
        const bool same =
            current.size() == options.size() && std::equal(current.begin(), current.end(), options.begin(),
                                                           [](const DropdownOption &a, const DropdownOption &b) {
                                                               return a.value == b.value && a.label == b.label;
                                                           });
        if (!same)
            control->SetOptions(std::move(options));
        control->SetValue(model->GetString(path));
    });
    return dropdown;
}

// `.section-header-*-helpcode { margin-top: 12px }`
VisualPtr Spaced(VisualPtr row)
{
    row->SetMargin({0.0f, 12.0f, 0.0f, 0.0f});
    return row;
}

// Single choice drawn as one 30px box split into segments, the chosen one filled with the accent.
// A struck option cannot stay on together with what the other choice of the page has now: its
// label is crossed out with a backslash, and choosing it still goes through (the page then turns
// the other side off).
class SegmentedChoice : public msimeui::Visual
{
  public:
    using SelectHandler = std::function<void(size_t index)>;

    explicit SegmentedChoice(std::vector<std::wstring> labels)
        : labels_(std::move(labels)), struck_(labels_.size(), false)
    {
    }

    void SetOnSelect(SelectHandler handler)
    {
        onSelect_ = std::move(handler);
    }

    void SetSelected(size_t index)
    {
        if (selected_ == index)
            return;
        selected_ = index;
        InvalidateVisual();
    }

    void SetStruck(size_t index, bool struck)
    {
        if (index >= struck_.size() || struck_[index] == struck)
            return;
        struck_[index] = struck;
        InvalidateVisual();
    }

    SizeF Measure(const SizeF &) override
    {
        float width = 0.0f;
        for (const std::wstring &label : labels_)
            width += SegmentWidth(label);
        return {width, 30.0f};
    }

    void Arrange(const RectF &finalRect) override
    {
        bounds_ = finalRect;
    }

    void Render(msimeui::DeviceResources &dr) override
    {
        const Palette &p = CurrentPalette();
        FillRounded(dr, bounds_, kRadius, p.controlBg);
        for (size_t i = 0; i < labels_.size(); ++i)
        {
            const RectF segment = SegmentRect(i);
            if (i == selected_)
                FillSegment(dr, i, segment, p.accent);
            else if (i == hovered_)
                FillSegment(dr, i, segment, p.buttonSecondaryHover);
            if (i > 0)
                FillRect(dr, {segment.x, bounds_.y, CssBorder(), bounds_.height}, p.border);
            const D2D1_COLOR_F color = i == selected_ ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)
                                       : struck_[i]   ? p.textMuted
                                                      : p.text;
            DrawTextInRect(dr, labels_[i], Style(), segment, color, true);
            if (struck_[i])
            {
                // A backslash through the label: falling to the right, 12 degrees off level.
                const float half = MeasureTextWidth(labels_[i], Style()) * 0.5f + 2.0f;
                const float fall = half * 0.2126f;
                const float cx = segment.x + segment.width * 0.5f;
                const float cy = segment.y + segment.height * 0.5f;
                DrawLine(dr, {cx - half, cy - fall}, {cx + half, cy + fall}, color, 1.5f);
            }
        }
        StrokeRounded(dr, bounds_, kRadius, p.border, CssBorder());
    }

    bool OnMouseDown(const POINT &point, WPARAM) override
    {
        pressed_ = HitSegment(point);
        return pressed_ != kNone;
    }

    bool OnMouseUp(const POINT &point, WPARAM) override
    {
        const size_t hit = HitSegment(point);
        const bool clicked = hit != kNone && hit == pressed_;
        pressed_ = kNone;
        if (clicked && hit != selected_ && onSelect_)
        {
            // The handler rewrites the config, which syncs the selection back here.
            const auto handler = onSelect_;
            handler(hit);
        }
        return true;
    }

    bool OnMouseMove(const POINT &point, WPARAM) override
    {
        const size_t hit = HitSegment(point);
        if (hit != hovered_)
        {
            hovered_ = hit;
            InvalidateVisual();
        }
        return true;
    }

    void OnMouseLeave() override
    {
        if (hovered_ == kNone)
            return;
        hovered_ = kNone;
        InvalidateVisual();
    }

    HCURSOR GetCursor() const override
    {
        return LoadCursor(nullptr, IDC_HAND);
    }

  private:
    static constexpr size_t kNone = static_cast<size_t>(-1);
    static constexpr float kRadius = 6.0f;

    static TextStyle Style()
    {
        return ControlStyle();
    }

    static float SegmentWidth(const std::wstring &label)
    {
        return MeasureTextWidth(label, Style()) + 28.0f;
    }

    RectF SegmentRect(size_t index) const
    {
        float x = bounds_.x;
        for (size_t i = 0; i < index; ++i)
            x += SegmentWidth(labels_[i]);
        return {x, bounds_.y, SegmentWidth(labels_[index]), bounds_.height};
    }

    // Rounded only on the outer corners of the box.
    void FillSegment(msimeui::DeviceResources &dr, size_t index, const RectF &segment, const D2D1_COLOR_F &color)
    {
        FillRounded(dr, segment, kRadius, color);
        const float half = segment.width * 0.5f;
        if (index > 0)
            FillRect(dr, {segment.x, segment.y, half, segment.height}, color);
        if (index + 1 < labels_.size())
            FillRect(dr, {segment.x + half, segment.y, segment.width - half, segment.height}, color);
    }

    size_t HitSegment(const POINT &point) const
    {
        if (!window_)
            return kNone;
        const PointF dip = window_->ClientPixelsToDips(point);
        for (size_t i = 0; i < labels_.size(); ++i)
        {
            if (Contains(SegmentRect(i), dip))
                return i;
        }
        return kNone;
    }

    std::vector<std::wstring> labels_;
    std::vector<bool> struck_;
    size_t selected_ = kNone;
    size_t hovered_ = kNone;
    size_t pressed_ = kNone;
    SelectHandler onSelect_;
};

struct RefreshState
{
    bool pending = false;
    UINT_PTR timer = 0;
    HostServices *host = nullptr;
};

RefreshState &Refresh()
{
    static RefreshState state;
    return state;
}

void CALLBACK RefreshTimeout(HWND, UINT, UINT_PTR id, DWORD)
{
    KillTimer(nullptr, id);
    RefreshState &state = Refresh();
    if (!state.pending || state.timer != id)
        return;
    state.pending = false;
    state.timer = 0;
    if (state.host)
        state.host->ShowToast(L"刷新失败，请稍后重试", false);
}
} // namespace

VisualPtr BuildHelpcodePage(PageContext &context)
{
    PageBuilder page(L"辅助码");
    SettingsModel *model = &context.model;

    // 全拼辅助码
    {
        auto column = std::make_shared<Column>(0.0f);
        column->Add(MakeRow(L"全拼辅助码", L"", BindToggle(context, "helpcode.quanpin_helpcode")));
        column->Add(
            Spaced(MakeRow(L"全拼辅助码方案", L"", SchemeDropdown(context, "helpcode.quanpin_helpcode_schema"))));
        column->Add(Spaced(MakeRow(L"在候选窗口中显示全拼辅助码", L"",
                                   BindToggle(context, "helpcode.show_qp_helpcode_in_candidate_window"))));
        page.AddSection(column);
    }

    // 自定义辅助码
    {
        auto left = std::make_shared<Column>(0.0f);
        left->Add(MakeTitle(L"自定义辅助码"));
        auto description = std::make_shared<RichText>(std::vector<RichText::Run>{
            {L"把 UTF-8 编码的 "},
            {L".txt", true},
            {L" 辅助码文件放进下面的文件夹，点击“刷新方案”后即可在上方方案中选择。 每行写作 "},
            {L"字=辅助码", true},
            {L"；设置中默认按文件名显示，可在文件开头用 "},
            {L"# name: 中文名", true},
            {L" 和 "},
            {L"# name_en: English name", true},
            {L" 注释指定方案名称。修改正在使用的方案文件后，需要切换一次方案或重启输入法才会生效。"},
        });
        description->SetMargin({0.0f, 4.0f, 0.0f, 0.0f});
        left->Add(description);
        auto directory = std::make_shared<RichText>(std::vector<RichText::Run>{}, true);
        // .helpcode-custom-directory { margin-top: 8px } on top of the description's 4px
        directory->SetMargin({0.0f, 8.0f, 0.0f, 0.0f});
        auto directoryBox = std::make_shared<Hideable>(directory, false);
        left->Add(directoryBox);
        std::weak_ptr<RichText> directoryWeak = directory;
        std::weak_ptr<Hideable> directoryBoxWeak = directoryBox;
        context.OnConfig([model, directoryWeak, directoryBoxWeak]() {
            const Json *value = model->Find("helpcode.custom_directory");
            auto label = directoryWeak.lock();
            auto box = directoryBoxWeak.lock();
            if (!label || !box || !value || !value->is_string())
                return;
            label->SetRuns({{L"文件夹：" + Widen(value->get<std::string>())}});
            box->SetVisible(true);
        });

        HostServices *host = &context.host;
        auto actions = std::make_shared<Row>(8.0f);
        actions->SetAlign(Row::Align::Start);
        actions->Add(std::make_shared<CustomButton>(L"打开目录", [model]() { model->Post("openHelpcodeDirectory"); }));
        actions->Add(std::make_shared<CustomButton>(L"刷新方案", [model, host]() {
            // The host rescans the folder and answers with a snapshot; say so once it is
            // there, or report a failure after 5 seconds. A second click replaces the first.
            RefreshState &state = Refresh();
            if (state.timer)
                KillTimer(nullptr, state.timer);
            state.host = host;
            state.pending = true;
            state.timer = SetTimer(nullptr, 0, 5000, &RefreshTimeout);
            model->Post("configRequest");
        }));
        context.OnConfig([model, host]() {
            RefreshState &state = Refresh();
            if (!state.pending)
                return;
            state.pending = false;
            if (state.timer)
                KillTimer(nullptr, state.timer);
            state.timer = 0;
            const Json *schemas = model->Find("helpcode.custom_schemas");
            const size_t count = schemas && schemas->is_array() ? schemas->size() : 0;
            host->ShowToast(count > 0 ? L"刷新成功，找到 " + std::to_wstring(count) + L" 个自定义方案"
                                      : std::wstring(L"刷新成功，文件夹里还没有自定义方案"),
                            true);
        });

        // .section-header:has(> .helpcode-custom-actions) { align-items: flex-start; gap: 16px }
        auto header = std::make_shared<Row>(16.0f);
        header->SetAlign(Row::Align::Start);
        header->Add(left, 1.0f);
        header->Add(actions);
        page.AddSection(header);
    }
    return page.Build();
}
} // namespace native_settings
