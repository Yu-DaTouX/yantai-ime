#include "settings/native/native_page_dictionary_panel.h"

// partials/tools-settings.html + modules/tools-settings.ts + styles/modules/tools-settings.css
namespace native_settings
{
namespace
{
// `.section-header` with `align-items: flex-start` and the toggle nudged down 2px
// (clipboard / U / E / M sections and the quick-phrase heading).
class TopAlignedRow : public msimeui::Panel
{
  public:
    TopAlignedRow(VisualPtr left, VisualPtr control, float controlTop)
        : left_(std::move(left)), control_(std::move(control)), controlTop_(controlTop)
    {
        AddChild(left_);
        AddChild(control_);
    }

    SizeF Measure(const SizeF &availableSize) override
    {
        constexpr float kGap = 10.0f;
        controlSize_ = control_->MeasureInLayout(availableSize);
        leftSize_ = left_->MeasureInLayout(
            {std::max(0.0f, availableSize.width - controlSize_.width - kGap), availableSize.height});
        return {availableSize.width, std::max(leftSize_.height, controlSize_.height + controlTop_)};
    }

    void Arrange(const RectF &finalRect) override
    {
        constexpr float kGap = 10.0f;
        bounds_ = finalRect;
        control_->ArrangeInLayout({finalRect.x + finalRect.width - controlSize_.width, finalRect.y + controlTop_,
                                   controlSize_.width, controlSize_.height});
        left_->ArrangeInLayout(
            {finalRect.x, finalRect.y, std::max(0.0f, finalRect.width - controlSize_.width - kGap), leftSize_.height});
    }

    void Render(msimeui::DeviceResources &dr) override
    {
        for (const auto &child : children_)
            child->Render(dr);
    }

  private:
    VisualPtr left_;
    VisualPtr control_;
    float controlTop_ = 0.0f;
    SizeF leftSize_ = {};
    SizeF controlSize_ = {};
};

// Title + `.quick-phrase-description` (the shared description, max-width 52em).
VisualPtr QuickTitleBlock(const std::wstring &title, const std::wstring &description)
{
    auto column = std::make_shared<Column>(0.0f);
    column->Add(MakeTitle(title, description));
    auto desc = MakeDescription(description);
    desc->SetMaxWidth(52.0f * kFontCaption);
    column->Add(desc);
    return column;
}

VisualPtr DetailsRow(PageContext &context, const std::wstring &title, const std::wstring &description,
                     const std::shared_ptr<ToggleSwitch> &toggle)
{
    (void)context;
    auto row = MakeRow(title, description, toggle, true);
    // .toggle-section-details .section-header { padding: 2px 0 }
    row->SetMargin({0.0f, 2.0f, 0.0f, 2.0f});
    return row;
}

// A toggle-section: expander + master switch, details that open below it. The sub
// switches keep showing their state but are greyed out while the master is off.
struct ToggleSection
{
    std::shared_ptr<Column> body;
    std::shared_ptr<Column> details;
};

ToggleSection MakeToggleSection(PageContext &context, const std::wstring &title, const std::string &masterPath,
                                std::function<void(bool disabled)> applyDisabled)
{
    ToggleSection section;
    section.body = std::make_shared<Column>(0.0f);
    auto expander = MakeExpander(title);
    auto master = BindToggle(context, masterPath);
    section.body->Add(std::make_shared<SettingRow>(expander, master));
    section.details = std::make_shared<Column>(0.0f);
    // margin: 4px 0 0 18px. On the inner column: a margin on the Hideable would still
    // take room while the details are display: none.
    section.details->SetMargin({18.0f, 4.0f, 0.0f, 0.0f});
    auto details = std::make_shared<Hideable>(section.details, false);
    section.body->Add(details);
    BindExpander(expander, details);
    SettingsModel *model = &context.model;
    context.OnConfig([model, masterPath, applyDisabled]() {
        if (applyDisabled)
            applyDisabled(!model->GetBool(masterPath));
    });
    return section;
}

std::shared_ptr<Label> DetailsDescription(const std::wstring &text)
{
    auto label = MakeDescription(text);
    // .toggle-section-details .input-setting-description { margin-top: 0 }
    label->SetMargin({0.0f, 0.0f, 0.0f, 0.0f});
    return label;
}
} // namespace

VisualPtr BuildToolsPage(PageContext &context)
{
    PageBuilder page(L"实用功能");

    // 剪贴板管理
    page.AddSection(std::make_shared<TopAlignedRow>(QuickTitleBlock(L"剪贴板管理", L"记录复制过的文本；关闭后立即清空"),
                                                    BindToggle(context, "utility.clipboard_history"), 2.0f));

    // 候选混输
    {
        auto zhEn = BindToggle(context, "general.cn_en_mixed_input");
        auto emoji = BindToggle(context, "general.emoji_mixed_input");
        auto kaomoji = BindToggle(context, "general.kaomoji_mixed_input");
        auto symbol = BindToggle(context, "general.symbol_mixed_input");
        auto dateTime = BindToggle(context, "utility.date_time_candidates");
        auto dateMenu = BindToggle(context, "utility.date_time_menu");
        std::vector<std::shared_ptr<ToggleSwitch>> subs = {zhEn, emoji, kaomoji, symbol, dateTime, dateMenu};
        ToggleSection section =
            MakeToggleSection(context, L"候选混输", "utility.mixed_candidates", [subs](bool disabled) {
                for (const auto &toggle : subs)
                    toggle->SetDisabled(disabled);
            });
        auto &details = *section.details;
        details.Add(DetailsDescription(L"全拼下把这些内容直接混进候选"));
        details.Add(DetailsRow(context, L"中英混输", L"补充英文单词候选", zhEn));
        details.Add(DetailsRow(context, L"emoji 混输", L"输入关键词时加入 emoji", emoji));
        details.Add(DetailsRow(context, L"颜文字混输", L"输入关键词时加入颜文字", kaomoji));
        details.Add(DetailsRow(context, L"符号混输", L"输入符号编码时加入符号", symbol));
        details.Add(DetailsRow(context, L"日期时间混入候选", L"输入 rq / sj / xq 时直接给出", dateTime));
        details.Add(DetailsRow(context, L"日期格式入口", L"在第三位放「📅日期」入口", dateMenu));
        page.AddSection(section.body);
    }

    // 语言栏：三个按钮的子开关已并入总开关，配置键仍读取。
    page.AddSection(
        MakeRow(L"语言栏", L"在任务栏语言栏显示输入法按钮，重启生效", BindToggle(context, "language_bar.enabled")));

    // 快捷短语(K 模式): .dict-card.quick-phrase-card
    auto panel = std::make_shared<DictionaryPanel>(context, DictionaryPanel::Kind::Quick);
    {
        auto body = std::make_shared<Column>(0.0f);
        auto heading =
            std::make_shared<TopAlignedRow>(QuickTitleBlock(L"快捷短语(K 模式)", L"中文模式下按 Shift+K，输入编码调用"),
                                            BindToggle(context, "utility.quick_phrase"), 2.0f);
        // .quick-phrase-heading { padding: 20px 24px 4px }
        heading->SetMargin({24.0f, 20.0f, 24.0f, 4.0f});
        body->Add(heading);
        panel->AddTo(*body);
        page.AddRaw(std::make_shared<PageCard>(body, PageCard::Style::DictCard, Thickness{}), 18.0f);
    }
    // .quick-phrase-export-section: padding 4px 24px, row min-height 42px without a divider.
    page.AddRaw(
        std::make_shared<PageCard>(MakeExportRow(
                                       L"导出快捷短语用户词库", [panel]() { panel->Export("quick"); }, 42.0f, false),
                                   PageCard::Style::Section, Thickness{24.0f, 4.0f, 24.0f, 4.0f}),
        18.0f);

    // 快捷输入模式：T / U / E / M / S / Y / V 固定开启，只留说明，不再各给一个开关（配置键仍读取）。
    {
        auto column = std::make_shared<Column>(0.0f);
        column->Add(MakeTitleBlock(L"快捷输入模式", L"中文模式下按 Shift+字母进入，V 直接输入"));
        const std::pair<const wchar_t *, const wchar_t *> modes[] = {
            {L"日期与时间(T)", L"rq / sj / xq 输入日期、时间、星期"},
            {L"部件拆字(U)", L"输入各部件拼音，如 ririri → 晶"},
            {L"Emoji(E)", L"输入关键词，如 xiaolian"},
            {L"颜文字(M)", L"输入关键词，如 haixiu"},
            {L"符号(S)", L"输入分组码或符号名，如 szq"},
            {L"临时英文(Y)", L"之后的字母按英文处理"},
            {L"数字与计算(V)", L"v 接数字或算式，如 v123、v1+2"},
        };
        for (const auto &[title, description] : modes)
        {
            column->Add(std::make_shared<Divider>());
            column->Add(MakeRow(title, description, nullptr, true));
        }
        page.AddSection(column);
    }

    struct Holder : msimeui::Container
    {
        Holder(VisualPtr child, std::shared_ptr<DictionaryPanel> panel)
            : msimeui::Container(std::move(child)), panel_(std::move(panel))
        {
        }
        std::shared_ptr<DictionaryPanel> panel_;
    };
    return std::make_shared<Holder>(page.Build(), panel);
}
} // namespace native_settings
