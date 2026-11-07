#include "tests/includes/test_framework.h"
#include "window/candidate_skin_palette.h"

#include <cmath>

TEST_CASE(candidate_skin_palette_resolves_builtin_theme_and_text_override)
{
    const CandidateSkinPalette dark = ResolveCandidateSkinPalette("fluent", false, "auto");
    REQUIRE_EQ(FlattenCandidateColor(dark.surface, dark.surface), RGB(32, 32, 32));
    REQUIRE_EQ(FlattenCandidateColor(dark.text, dark.surface), RGB(233, 232, 232));

    const CandidateSkinPalette light = ResolveCandidateSkinPalette("fluent", true, "#123456");
    REQUIRE_EQ(FlattenCandidateColor(light.surface, light.surface), RGB(255, 255, 255));
    REQUIRE_EQ(FlattenCandidateColor(light.text, light.surface), RGB(18, 52, 86));
}

// 被删掉的内置皮肤 id 留在老配置里时，外观退回 fluent，不再是原来那套配色。
TEST_CASE(candidate_skin_palette_falls_back_to_fluent_for_a_removed_builtin)
{
    const CandidateSkinPalette removed = ResolveCandidateSkinPalette("wechat", false, "auto");
    const CandidateSkinPalette fluent = ResolveCandidateSkinPalette("fluent", false, "auto");
    REQUIRE_EQ(FlattenCandidateColor(removed.surface, removed.surface),
               FlattenCandidateColor(fluent.surface, fluent.surface));
    REQUIRE_EQ(FlattenCandidateColor(removed.text, removed.surface), FlattenCandidateColor(fluent.text, fluent.surface));
}

TEST_CASE(candidate_skin_palette_custom_missing_colors_inherit_package_base)
{
    CandidateSkinCatalog::CandidateColors colors;
    colors.surface = "#102030";
    const CandidateSkinPalette palette = ResolveCandidateSkinPalette("custom", false, "auto", &colors, "fluent");

    REQUIRE_EQ(FlattenCandidateColor(palette.surface, palette.surface), RGB(16, 32, 48));
    REQUIRE(std::abs(palette.border.a - 46.0f / 255.0f) < 0.001f);
    REQUIRE_EQ(FlattenCandidateColor(palette.text, palette.surface), RGB(233, 232, 232));
}

TEST_CASE(candidate_skin_palette_applies_custom_colors_with_builtin_fallbacks)
{
    CandidateSkinCatalog::CandidateColors colors;
    colors.surface = "#102030";
    colors.text = "rgba(255, 255, 255, 0.5)";
    const CandidateSkinPalette palette = ResolveCandidateSkinPalette("custom", false, "auto", &colors);

    REQUIRE_EQ(FlattenCandidateColor(palette.surface, palette.surface), RGB(16, 32, 48));
    REQUIRE_EQ(FlattenCandidateColor(palette.border, palette.surface), RGB(41, 54, 67));
    REQUIRE_EQ(FlattenCandidateColor(palette.text, palette.surface), RGB(136, 144, 152));
}

// Skin authors write CSS hex notation in every form, so D2D must read them all instead of silently falling back.
TEST_CASE(candidate_skin_palette_parses_every_css_hex_notation)
{
    const D2D1_COLOR_F fallback = D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f);
    const D2D1_COLOR_F black = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
    REQUIRE_EQ(FlattenCandidateColor(ParseCandidateCssColor("#f80", fallback), black), RGB(255, 136, 0));
    REQUIRE_EQ(FlattenCandidateColor(ParseCandidateCssColor("#ff8800", fallback), black), RGB(255, 136, 0));
    REQUIRE_EQ(FlattenCandidateColor(ParseCandidateCssColor("#FF8800", fallback), black), RGB(255, 136, 0));

    const D2D1_COLOR_F shortAlpha = ParseCandidateCssColor("#f808", fallback);
    REQUIRE_EQ(FlattenCandidateColor(D2D1::ColorF(shortAlpha.r, shortAlpha.g, shortAlpha.b), black), RGB(255, 136, 0));
    REQUIRE(std::abs(shortAlpha.a - 0x88 / 255.0f) < 0.001f);
    const D2D1_COLOR_F longAlpha = ParseCandidateCssColor("#ff880080", fallback);
    REQUIRE(std::abs(longAlpha.a - 0x80 / 255.0f) < 0.001f);
}

TEST_CASE(candidate_skin_palette_rejects_malformed_hex_colors)
{
    const D2D1_COLOR_F fallback = D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f);
    const D2D1_COLOR_F black = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
    for (const char *text : {"#12zz56", "#ggg", "#12345", "#-12345", "#"})
        REQUIRE_EQ(FlattenCandidateColor(ParseCandidateCssColor(text, fallback), black), RGB(0, 0, 255));
}

TEST_CASE(candidate_skin_palette_flattens_alpha_for_gdi)
{
    REQUIRE_EQ(FlattenCandidateColor(D2D1::ColorF(1.0f, 0.0f, 0.0f, 0.25f), D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f)),
               RGB(64, 0, 191));
}

TEST_CASE(candidate_skin_palette_flattens_translucent_surface_before_border_and_text)
{
    const CandidateSkinPalette palette{D2D1::ColorF(1.0f, 0.0f, 0.0f, 0.5f), D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.25f),
                                       D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.5f)};
    const CandidateSkinPalette flattened =
        FlattenCandidateSkinPaletteForGdi(palette, D2D1::ColorF(0.0f, 0.0f, 1.0f, 1.0f));

    REQUIRE_EQ(FlattenCandidateColor(flattened.surface, flattened.surface), RGB(128, 0, 128));
    REQUIRE_EQ(FlattenCandidateColor(flattened.border, flattened.surface), RGB(96, 0, 96));
    REQUIRE_EQ(FlattenCandidateColor(flattened.text, flattened.surface), RGB(191, 128, 191));
}

TEST_CASE(candidate_skin_palette_canvas_base_is_transparent_with_readable_text)
{
    for (const bool light : {false, true})
    {
        const CandidateSkinPalette palette = ResolveCandidateSkinPalette("canvas", light, "auto");
        REQUIRE_EQ(palette.surface.a, 0.0f);
        REQUIRE_EQ(palette.border.a, 0.0f);
        REQUIRE_EQ(palette.text.a, 1.0f);
        // GDI users flatten the transparent surface onto its own RGB: a readable fluent-like card.
        REQUIRE_EQ(FlattenCandidateColor(palette.surface, palette.surface),
                   light ? RGB(255, 255, 255) : RGB(32, 32, 32));
        REQUIRE_EQ(FlattenCandidateColor(palette.text, palette.surface), light ? RGB(26, 26, 26) : RGB(242, 242, 242));
        REQUIRE_EQ(CandidateSkinBaseItemRadiusDip("canvas"), 4.0f);
    }
    // A package surface still wins over the canvas default.
    CandidateSkinCatalog::CandidateColors colors;
    colors.surface = "#10203080";
    const CandidateSkinPalette custom = ResolveCandidateSkinPalette("art", false, "auto", &colors, "canvas");
    REQUIRE(std::abs(custom.surface.a - 128.0f / 255.0f) < 0.001f);
}