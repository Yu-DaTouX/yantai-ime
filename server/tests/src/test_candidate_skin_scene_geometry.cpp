#include "tests/includes/test_framework.h"

#include "msimeui/Controls.h"
#include "window/candidate_skin_scene.h"

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
// 候选窗场景的几何：高亮块（候选项矩形）相对卡片内容区的位置。内容区是卡片矩形去掉皮肤内边距后的部分。
struct SceneGeometry
{
    msimeui::RectF content;
    std::vector<msimeui::RectF> items;
};

SceneGeometry LayOutScene(CandidateSkinScene &scene, const std::string &skinId, bool horizontal, bool preeditHidden,
                          const std::vector<std::wstring> &candidates)
{
    CandidateSkinScene::Settings settings;
    settings.skinsRoot = std::filesystem::u8path(MSIME_SKINS_PATH);
    settings.skinId = skinId;
    settings.horizontal = horizontal;
    settings.fontSize = 16.0f;
    settings.preeditFontSize = 16.0f;
    settings.preeditHidden = preeditHidden;
    // 设置页预览不留阴影空白，卡片左上角就是根的原点，算间距最直接。
    settings.framePadding = {0.0f, 0.0f, 0.0f, 0.0f};
    settings.pageSize = static_cast<int>(candidates.size());
    const CandidateSkinScene::Result result = scene.Apply(settings);

    std::vector<msimeui::CandidateList::Item> items;
    items.reserve(candidates.size());
    for (std::size_t index = 0; index < candidates.size(); ++index)
    {
        items.push_back({std::to_wstring(index + 1), candidates[index], L"", L"", {}});
    }
    if (preeditHidden)
        scene.Preedit()->SetHeight(0.0f);
    scene.List()->SetItems(std::move(items));

    const msimeui::SizeF natural = scene.Root()->MeasureInLayout({600.0f, 600.0f});
    scene.Root()->ArrangeInLayout({0.0f, 0.0f, natural.width, natural.height});

    SceneGeometry geometry;
    const msimeui::RectF card = scene.CardVisual()->GetBounds();
    // 卡片的内边距就是这次套皮肤得到的 containerPad（Card 自己的 GetPadding 不暴露它）。
    const float pad = result.tokens.containerPad;
    geometry.content = {card.x + pad, card.y + pad, card.width - pad * 2.0f, card.height - pad * 2.0f};
    for (std::size_t index = 0; index < candidates.size(); ++index)
    {
        geometry.items.push_back(scene.List()->GetItemBounds(index));
    }
    return geometry;
}

void RequireSameGap(float actual, float expected, const char *what)
{
    if (std::fabs(actual - expected) > 0.02f)
    {
        std::string message = "gap mismatch: ";
        message += what;
        throw std::runtime_error(message);
    }
}

// 单候选页：高亮块就是整行，四个方向的留白必须相等（都等于 0 —— 高亮铺满卡片内容区）。
void RequireHighlightFillsContent(const SceneGeometry &geometry)
{
    REQUIRE(geometry.items.size() == 1);
    const msimeui::RectF &item = geometry.items.front();
    const float top = item.y - geometry.content.y;
    const float bottom = geometry.content.y + geometry.content.height - (item.y + item.height);
    const float left = item.x - geometry.content.x;
    const float right = geometry.content.x + geometry.content.width - (item.x + item.width);
    RequireSameGap(bottom, top, "bottom vs top");
    RequireSameGap(left, top, "left vs top");
    RequireSameGap(right, top, "right vs top");
    // 高亮块正好铺满内容区：上下各留出卡片内边距，左右不再多出别的东西。
    REQUIRE(std::fabs(top) < 0.02f);
    REQUIRE(std::fabs(left) < 0.02f);
}
} // namespace

// mac 皮肤（base = fluent，fit_content、深色）横排：隐藏预编辑行与英文行时，高亮块在卡片里上下等距。
TEST_CASE(candidate_skin_scene_highlight_is_centred_in_the_card)
{
    CandidateSkinScene scene;
    const SceneGeometry geometry = LayOutScene(scene, "mac", true, true, {L"\u4e00"});
    RequireHighlightFillsContent(geometry);
}

// 多候选时首项贴内容区左上、末项贴右下；行中间的空隙不参与卡片留白。
TEST_CASE(candidate_skin_scene_row_edges_touch_the_card_content)
{
    CandidateSkinScene scene;
    const SceneGeometry geometry =
        LayOutScene(scene, "mac", true, true, {L"\u4e00", L"\u5fae\u8f6f\u8f93\u5165\u6cd5", L"\u91d1"});
    REQUIRE(geometry.items.size() == 3);
    const msimeui::RectF &first = geometry.items.front();
    const msimeui::RectF &last = geometry.items.back();
    REQUIRE(std::fabs((first.x - geometry.content.x)) < 0.02f);
    REQUIRE(std::fabs((last.x + last.width) - (geometry.content.x + geometry.content.width)) < 0.02f);
    REQUIRE(std::fabs((first.y - geometry.content.y)) < 0.02f);
    REQUIRE(std::fabs((last.y + last.height) - (geometry.content.y + geometry.content.height)) < 0.02f);
}

// fit_content 下高亮块里序号左侧的留白与文字右侧的留白相等（文字不贴高亮块右边）。
TEST_CASE(candidate_skin_scene_fit_content_keeps_row_padding_even)
{
    CandidateSkinScene scene;
    LayOutScene(scene, "mac", true, true, {L"\u4e00"});
    const msimeui::CandidateList::Appearance &appearance = scene.List()->GetAppearance();
    REQUIRE(appearance.fitContent);
    REQUIRE(std::fabs(appearance.contentPadRight - (appearance.contentPadLeft + appearance.textPadLeft)) < 0.02f);
}

// 原装 fluent 皮肤（默认竖排）在隐藏预编辑行时同样要上下等距，修复不能只照顾横排。
TEST_CASE(candidate_skin_scene_fluent_vertical_highlight_is_centred)
{
    CandidateSkinScene scene;
    const SceneGeometry geometry = LayOutScene(scene, "fluent", false, true, {L"\u4e00"});
    RequireHighlightFillsContent(geometry);
}

// 候选条下面的英文行：任何皮肤、深浅、横竖排、缩放下都不能把卡片撑宽，高亮右侧留白保持 contentPadLeft + textPadLeft。
TEST_CASE(candidate_skin_scene_english_line_keeps_selected_padding_and_does_not_widen_the_card)
{
    for (const std::string skin : {"fluent", "mac", "acrylic"})
    {
        for (const bool light : {false, true})
        {
            for (const bool horizontal : {false, true})
            {
                for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f})
                {
                    CandidateSkinScene scene;
                    CandidateSkinScene::Settings settings;
                    settings.skinsRoot = std::filesystem::u8path(MSIME_SKINS_PATH);
                    settings.skinId = skin;
                    settings.light = light;
                    settings.horizontal = horizontal;
                    settings.fontSize = 16 * scale;
                    settings.preeditHidden = true;
                    settings.framePadding = {};
                    scene.Apply(settings);
                    scene.Preedit()->SetHeight(0);
                    scene.List()->SetItems({{L"1", L"你好", L"", L"", {}}});
                    scene.EnglishLine()->SetLineVisible(false);
                    const auto hiddenSize = scene.Root()->MeasureInLayout({1200, 1200});

                    scene.EnglishLine()->SetLineVisible(true);
                    scene.EnglishLine()->SetContent(std::wstring(3000, L'x'), L"n.", L"Ctrl+\u21B5 \u4E0A\u5C4F");
                    const auto size = scene.Root()->MeasureInLayout({1200, 1200});
                    scene.Root()->ArrangeInLayout({0, 0, size.width, size.height});
                    // 3000 个字符的英文只在行内省略，不撑宽卡片，只多出一行的高度。
                    REQUIRE(std::fabs(size.width - hiddenSize.width) < 0.02f);
                    REQUIRE(size.height > hiddenSize.height);
                    const auto &appearance = scene.List()->GetAppearance();
                    if (appearance.fitContent)
                        REQUIRE(std::fabs(appearance.contentPadRight -
                                          (appearance.contentPadLeft + appearance.textPadLeft)) < 0.02f);
                }
            }
        }
    }
}

bool SameRgb(const D2D1_COLOR_F &left, const D2D1_COLOR_F &right)
{
    return std::fabs(left.r - right.r) < 0.005f && std::fabs(left.g - right.g) < 0.005f &&
           std::fabs(left.b - right.b) < 0.005f;
}

// 英文行用皮肤的次要文字色（序号色），不用强调色，也不用内置 fluent 的紫色翻译色；mac 深浅两套的词性小字也不再是紫色。
TEST_CASE(candidate_skin_scene_english_line_uses_the_secondary_text_color_not_purple)
{
    for (const std::string skin : {"fluent", "mac", "acrylic"})
    {
        for (const bool light : {false, true})
        {
            CandidateSkinScene scene;
            CandidateSkinScene::Settings settings;
            settings.skinsRoot = std::filesystem::u8path(MSIME_SKINS_PATH);
            settings.skinId = skin;
            settings.light = light;
            settings.horizontal = true;
            const CandidateSkinScene::Result result = scene.Apply(settings);
            const auto &line = scene.EnglishLine()->GetAppearance();
            REQUIRE(SameRgb(line.wordColor, result.tokens.number));
            REQUIRE(SameRgb(line.posColor, result.tokens.number));
            REQUIRE(SameRgb(line.hintColor, result.tokens.number));
            REQUIRE(!SameRgb(line.wordColor, result.tokens.accent));
            if (skin == "mac")
                REQUIRE(SameRgb(result.tokens.translation, result.tokens.number));
        }
    }
}

// 预编辑行显示时，高亮块与卡片顶边之间由预编辑行占据，末项仍贴着内容底边。
TEST_CASE(candidate_skin_scene_visible_preedit_keeps_the_last_row_flush)
{
    CandidateSkinScene scene;
    const SceneGeometry geometry =
        LayOutScene(scene, "mac", true, false, {L"\u4e00", L"\u5fae\u8f6f\u8f93\u5165\u6cd5"});
    REQUIRE(geometry.items.size() == 2);
    const msimeui::RectF &last = geometry.items.back();
    REQUIRE(std::fabs((last.y + last.height) - (geometry.content.y + geometry.content.height)) < 0.02f);
}
