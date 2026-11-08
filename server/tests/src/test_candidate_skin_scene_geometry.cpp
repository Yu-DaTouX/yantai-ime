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

// fit_content 下行的左右内边距相等，卡片两侧留白才对得上（此前右侧多加了一份 textPadLeft）。
TEST_CASE(candidate_skin_scene_fit_content_keeps_row_padding_even)
{
    CandidateSkinScene scene;
    LayOutScene(scene, "mac", true, true, {L"\u4e00"});
    const msimeui::CandidateList::Appearance &appearance = scene.List()->GetAppearance();
    REQUIRE(appearance.fitContent);
    REQUIRE(std::fabs(appearance.contentPadRight - appearance.contentPadLeft) < 0.02f);
}

// 原装 fluent 皮肤（默认竖排）在隐藏预编辑行时同样要上下等距，修复不能只照顾横排。
TEST_CASE(candidate_skin_scene_fluent_vertical_highlight_is_centred)
{
    CandidateSkinScene scene;
    const SceneGeometry geometry = LayOutScene(scene, "fluent", false, true, {L"\u4e00"});
    RequireHighlightFillsContent(geometry);
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
