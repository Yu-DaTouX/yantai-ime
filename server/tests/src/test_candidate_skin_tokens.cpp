#include "tests/includes/test_framework.h"
#include "tests/includes/test_utf8_path.h"

#include "skin/candidate_skin_catalog.h"
#include "window/candidate_skin_palette.h"
#include "window/candidate_skin_tokens.h"
#include "window/floating_toolbar_skin.h"

#include <windows.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

// 内置皮肤的外观从写死在候选窗代码里改成了 skins/default/<id>/skin.toml。这里留着改之前那段 C++
// （CandidatePresenter::ApplySkin 的分支，原样搬来），逐个皮肤、主题、排列与字号比对清单解析出的结果，
// 保证迁移前后画出来的一模一样。以后改出厂清单的外观时，同步改这里的参照值。
namespace
{
D2D1_COLOR_F ColorFromRgb(UINT rgb, float alpha = 1.0f)
{
    return CandidateColorFromRgb(rgb, alpha);
}

D2D1_COLOR_F ParseCssColor(const std::string &text, D2D1_COLOR_F fallback)
{
    return ParseCandidateCssColor(text, fallback);
}

constexpr float kTranslationPosAlpha = 0x80 / 255.0f;
constexpr float kTranslationSeparatorAlpha = 0x73 / 255.0f;

void SetTranslationColors(CandSkinTokens &tokens, UINT rgb, UINT posRgb)
{
    tokens.translation = ColorFromRgb(rgb);
    tokens.translationPos = ColorFromRgb(posRgb, kTranslationPosAlpha);
    tokens.translationSeparator = ColorFromRgb(rgb, kTranslationSeparatorAlpha);
}

void SetSelectedTranslationColors(CandSkinTokens &tokens, UINT rgb, float alpha, UINT posRgb, float posAlpha)
{
    tokens.rowTranslationSelected = ColorFromRgb(rgb, alpha);
    tokens.rowTranslationPosSelected = ColorFromRgb(posRgb, posAlpha);
    tokens.rowTranslationSeparatorSelected = ColorFromRgb(rgb, alpha * kTranslationSeparatorAlpha);
}

CandSkinTokens LegacyTokens(const std::string &baseSkin, bool candLight, bool horizontalLayout, float fontSize)
{
    CandSkinTokens tokens;
    if (candLight)
    {
        tokens.number = D2D1::ColorF(26.0f / 255.0f, 26.0f / 255.0f, 26.0f / 255.0f, 0.55f);
        tokens.selected = ColorFromRgb(0xE8E8E8);
        tokens.hover = ColorFromRgb(0xECECEC);
        tokens.menuFill = ColorFromRgb(0xFFFFFF);
        tokens.menuBorder = D2D1::ColorF(0, 0.12f);
        tokens.menuText = ColorFromRgb(0x1A1A1A);
        tokens.menuHover = ColorFromRgb(0xECECEC);
    }
    if (baseSkin == "fluent")
    {
        if (horizontalLayout)
            tokens.outerItemRadius = tokens.radius;
        SetTranslationColors(tokens, candLight ? 0x6940A5 : 0x9A6DD7, candLight ? 0xAD1A72 : 0xE255A1);
    }
    else if (CandidateSkinCatalog::IsCanvasBase(baseSkin))
    {
        tokens.borderWidth = 0.0f;
        tokens.radius = 0.0f;
        tokens.containerPad = 5.0f;
        tokens.itemRadius = 4.0f;
        tokens.number = candLight ? D2D1::ColorF(26.0f / 255.0f, 26.0f / 255.0f, 26.0f / 255.0f, 0.6f)
                                  : ColorFromRgb(0xF2F2F2, 0.62f);
        tokens.selected = candLight ? D2D1::ColorF(0, 0.10f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.16f);
        tokens.hover = candLight ? D2D1::ColorF(0, 0.06f) : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.09f);
        SetTranslationColors(tokens, candLight ? 0x6940A5 : 0x9A6DD7, candLight ? 0xAD1A72 : 0xE255A1);
    }
    return tokens;
}

// 旧代码在 D2D1::ColorF 里直接写 alpha，清单里写成 rgba() 或八位十六进制，换算有最后一位的浮点误差。
bool SameColor(const D2D1_COLOR_F &a, const D2D1_COLOR_F &b)
{
    constexpr float kTolerance = 0.5f / 255.0f;
    return std::fabs(a.r - b.r) <= kTolerance && std::fabs(a.g - b.g) <= kTolerance &&
           std::fabs(a.b - b.b) <= kTolerance && std::fabs(a.a - b.a) <= kTolerance;
}

std::string Describe(const D2D1_COLOR_F &c)
{
    return std::to_string(c.r * 255.0f) + "," + std::to_string(c.g * 255.0f) + "," + std::to_string(c.b * 255.0f) +
           "," + std::to_string(c.a);
}

void RequireSameTokens(const CandSkinTokens &expected, const CandSkinTokens &actual, const std::string &where)
{
#define TOKEN_COLOR(field)                                                                                             \
    if (!SameColor(expected.field, actual.field))                                                                      \
        throw std::runtime_error(where + " " #field ": expected " + Describe(expected.field) + " got " +               \
                                 Describe(actual.field));
#define TOKEN_VALUE(field)                                                                                             \
    if (std::fabs(static_cast<float>(expected.field) - static_cast<float>(actual.field)) > 0.001f)                     \
        throw std::runtime_error(where + " " #field ": expected " + std::to_string(expected.field) + " got " +         \
                                 std::to_string(actual.field));
    TOKEN_COLOR(number)
    TOKEN_COLOR(selected)
    TOKEN_COLOR(hover)
    TOKEN_COLOR(accent)
    TOKEN_COLOR(translation)
    TOKEN_VALUE(radius)
    TOKEN_VALUE(borderWidth)
    TOKEN_VALUE(containerPad)
    TOKEN_VALUE(showSelectedBar)
    TOKEN_VALUE(itemRadius)
    TOKEN_VALUE(outerItemRadius)
    TOKEN_VALUE(itemGap)
    TOKEN_VALUE(itemExtraHeight)
    TOKEN_VALUE(itemPadLeft)
    TOKEN_VALUE(itemPadRight)
    TOKEN_VALUE(justifyRows)
    TOKEN_VALUE(labelScale)
    TOKEN_VALUE(labelGap)
    TOKEN_VALUE(minItemWidth)
    TOKEN_VALUE(selectedBarWidth)
    TOKEN_VALUE(selectedBarHeightEm)
    TOKEN_VALUE(selectedBarInside)
    TOKEN_VALUE(trianglePager)
    TOKEN_VALUE(trimHiddenPreeditGap)
    TOKEN_COLOR(pagerDivider)
    TOKEN_VALUE(preeditMargin.left)
    TOKEN_VALUE(preeditMargin.top)
    TOKEN_VALUE(preeditMargin.right)
    TOKEN_VALUE(preeditMargin.bottom)
    TOKEN_COLOR(rowTextSelected)
    TOKEN_COLOR(rowLabelSelected)
    TOKEN_COLOR(menuFill)
    TOKEN_COLOR(menuBorder)
    TOKEN_COLOR(menuText)
    TOKEN_COLOR(menuHover)
    TOKEN_COLOR(preeditCaret)
    TOKEN_COLOR(selectedBar)
    TOKEN_COLOR(rowTranslationSelected)
    TOKEN_COLOR(translationPos)
    TOKEN_COLOR(translationSeparator)
    TOKEN_COLOR(rowTranslationPosSelected)
    TOKEN_COLOR(rowTranslationSeparatorSelected)
    TOKEN_COLOR(preeditBackground)
    TOKEN_COLOR(preeditDivider)
#undef TOKEN_COLOR
#undef TOKEN_VALUE
}

const char *const kBases[] = {"fluent", "canvas"};
} // namespace

TEST_CASE(builtin_skin_manifests_reproduce_the_former_hardcoded_tokens)
{
    for (const char *base : kBases)
    {
        REQUIRE(CandidateSkinCatalog::HasFactoryLook(base));
        const CandidateSkinCatalog::Package &look = CandidateSkinCatalog::FactoryLook(base);
        for (const bool light : {false, true})
        {
            for (const bool horizontal : {false, true})
            {
                for (const float fontSize : {12.0f, 16.0f, 18.0f, 21.0f, 27.0f, 40.0f})
                {
                    const std::string where = std::string(base) + (light ? " light" : " dark") +
                                              (horizontal ? " horizontal " : " vertical ") +
                                              std::to_string(static_cast<int>(fontSize));
                    RequireSameTokens(LegacyTokens(base, light, horizontal, fontSize),
                                      ResolveCandidateSkinTokens(look, nullptr, light, horizontal, fontSize), where);
                }
            }
        }
    }
}

TEST_CASE(builtin_skin_manifests_keep_canvas_without_a_default_shadow)
{
    REQUIRE_EQ(ResolveCandidateSkinShadow(CandidateSkinCatalog::FactoryLook("canvas"), nullptr), std::string("none"));
    REQUIRE(ResolveCandidateSkinShadow(CandidateSkinCatalog::FactoryLook("fluent"), nullptr).empty());
    CandidateSkinCatalog::Package package;
    package.shadow = "soft";
    REQUIRE_EQ(ResolveCandidateSkinShadow(CandidateSkinCatalog::FactoryLook("canvas"), &package), std::string("soft"));
}

// 外部皮肤的圆角覆盖：贴角的高亮跟着新圆角走，圆角为 0 时贴角规则仍保持开启。
TEST_CASE(candidate_skin_tokens_corner_radius_override_keeps_following_the_card)
{
    CandidateSkinCatalog::Package package;
    package.cornerRadiusDip = 12.0;
    const CandSkinTokens rounded =
        ResolveCandidateSkinTokens(CandidateSkinCatalog::FactoryLook("fluent"), &package, false, true, 16.0f);
    REQUIRE_EQ(rounded.radius, 12.0f);
    REQUIRE_EQ(rounded.outerItemRadius, 12.0f);
    package.cornerRadiusDip = 0.0;
    const CandSkinTokens square =
        ResolveCandidateSkinTokens(CandidateSkinCatalog::FactoryLook("fluent"), &package, false, true, 16.0f);
    REQUIRE(square.outerItemRadius > 0.0f);
    const CandSkinTokens vertical =
        ResolveCandidateSkinTokens(CandidateSkinCatalog::FactoryLook("fluent"), &package, false, false, 16.0f);
    REQUIRE_EQ(vertical.outerItemRadius, 0.0f);
}

namespace
{
std::filesystem::path MakeLookRoot()
{
    const auto root = test::TempRoot() / (L"msime-skin-look-" + std::to_wstring(GetCurrentProcessId())) / L"skins";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / L"default" / L"fluent", ec);
    return root;
}
} // namespace

// 数据目录里的内置清单写了的键覆盖出厂外观，没写的沿用出厂值；写坏了就整份退回出厂外观。
TEST_CASE(builtin_look_overlays_the_data_directory_manifest_on_the_factory_one)
{
    const auto root = MakeLookRoot();
    const auto manifest = root / L"default" / L"fluent" / L"skin.toml";
    {
        std::ofstream out(manifest, std::ios::binary | std::ios::trunc);
        out << "schema_version = 1\nid = \"fluent\"\n[candidate_window]\ncorner_radius_dip = 11\n"
               "[candidate.dark]\naccent = \"#123456\"\n";
    }
    CandidateSkinCatalog::Package look = CandidateSkinCatalog::ResolveBuiltInLook(root, "fluent");
    REQUIRE(look.cornerRadiusDip && *look.cornerRadiusDip == 11.0);
    REQUIRE_EQ(look.dark.accent, std::string("#123456"));
    REQUIRE_EQ(look.light.accent, CandidateSkinCatalog::FactoryLook("fluent").light.accent);
    REQUIRE_EQ(look.dark.selected, CandidateSkinCatalog::FactoryLook("fluent").dark.selected);
    {
        std::ofstream out(manifest, std::ios::binary | std::ios::trunc);
        out << "schema_version = 1\nid = \"fluent\"\n[candidate_window]\ncorner_radius_dip = 99\n";
    }
    look = CandidateSkinCatalog::ResolveBuiltInLook(root, "fluent");
    REQUIRE(look.cornerRadiusDip && *look.cornerRadiusDip == 6.0);
    // 没有出厂清单的 id 用 fluent 的外观。
    REQUIRE_EQ(CandidateSkinCatalog::ResolveBuiltInLook(root, "no_such_skin").dark.accent,
               CandidateSkinCatalog::FactoryLook("fluent").dark.accent);
    std::error_code ec;
    std::filesystem::remove_all(root.parent_path(), ec);
}

// em 长度按字号取整，数值长度原样；写成别的样子的拒绝加载。
TEST_CASE(candidate_skin_lengths_accept_dip_and_em)
{
    REQUIRE_EQ(CandidateSkinCatalog::ResolveLength({0.3, true}, 16.0f), 5.0f);
    REQUIRE_EQ(CandidateSkinCatalog::ResolveLength({0.3, true}, 20.0f), 6.0f);
    REQUIRE_EQ(CandidateSkinCatalog::ResolveLength({7.5, false}, 20.0f), 7.5f);
    const auto root = MakeLookRoot();
    const auto writeSkin = [&](const std::string &windowKeys) {
        std::error_code ec;
        std::filesystem::create_directories(root / L"lengths", ec);
        std::ofstream out(root / L"lengths" / L"skin.toml", std::ios::binary | std::ios::trunc);
        out << "schema_version = 1\nid = \"lengths\"\nname = \"L\"\nversion = \"1\"\nbase = \"fluent\"\n"
               "[supports]\nlayouts = [\"horizontal\"]\nthemes = [\"dark\"]\n[candidate_window]\n"
            << windowKeys;
    };
    writeSkin("padding = \"0.5em\"\nitem_gap = 3\n[candidate_window.horizontal]\nitem_min_width = \"2em\"\n");
    auto package = CandidateSkinCatalog::Load(root, "lengths");
    REQUIRE(package.has_value());
    REQUIRE(package->geometry.padding && package->geometry.padding->em && package->geometry.padding->value == 0.5);
    REQUIRE(package->geometry.itemGap && !package->geometry.itemGap->em);
    REQUIRE(package->horizontalGeometry.itemMinWidth && package->horizontalGeometry.itemMinWidth->em);
    const CandSkinTokens tokens =
        ResolveCandidateSkinTokens(CandidateSkinCatalog::FactoryLook("fluent"), &*package, false, true, 20.0f);
    REQUIRE_EQ(tokens.containerPad, 10.0f);
    REQUIRE_EQ(tokens.itemGap, 3.0f);
    REQUIRE_EQ(tokens.minItemWidth, 40.0f);
    for (const char *bad : {"padding = \"5px\"\n", "padding = \"em\"\n", "padding = -1\n", "padding = \"9em\"\n",
                            "page_arrow_style = \"round\"\n", "preedit_margin_dip = [1, 2, 3]\n"})
    {
        writeSkin(bad);
        REQUIRE(!CandidateSkinCatalog::Load(root, "lengths").has_value());
    }
    std::error_code ec;
    std::filesystem::remove_all(root.parent_path(), ec);
}
