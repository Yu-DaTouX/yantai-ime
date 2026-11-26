#include "tests/includes/test_framework.h"
#include "skin/candidate_skin_catalog.h"
#include "window/candidate_skin_palette.h"
#include "window/floating_toolbar_skin.h"
#include <cmath>

TEST_CASE(floating_toolbar_skin_defaults_to_fluent_toolbar)
{
    const FloatingToolbarSkin dark = ResolveFloatingToolbarSkin("fluent", false);
    REQUIRE_EQ(FlattenCandidateColor(dark.fill, dark.fill), RGB(26, 26, 26));
    REQUIRE_EQ(FlattenCandidateColor(dark.handle, dark.fill), RGB(107, 105, 214));
    REQUIRE_EQ(dark.radius, 8.0f);

    const FloatingToolbarSkin unknown = ResolveFloatingToolbarSkin("no_such_skin", true);
    REQUIRE_EQ(FlattenCandidateColor(unknown.fill, unknown.fill), RGB(255, 255, 255));
    REQUIRE_EQ(FlattenCandidateColor(unknown.glyph, unknown.fill), RGB(26, 26, 26));
}

// An external skin's [toolbar] tables reach the D2D toolbar.
TEST_CASE(floating_toolbar_skin_applies_external_overrides_for_the_theme)
{
    CandidateSkinCatalog::Package package;
    package.toolbarDark.handle = "#e08aa8";
    package.toolbarLight.background = "#fff7fa";
    package.toolbarLight.icon = "#3a2a30";
    package.toolbarLight.handle = "not a colour";
    package.toolbarCornerRadiusDip = 12.0;

    FloatingToolbarSkin dark = ResolveFloatingToolbarSkin("fluent", false);
    ApplyFloatingToolbarSkinOverrides(dark, package, false);
    REQUIRE_EQ(FlattenCandidateColor(dark.handle, dark.fill), RGB(224, 138, 168));
    // Keys the skin leaves out keep the base toolbar's values.
    REQUIRE_EQ(FlattenCandidateColor(dark.fill, dark.fill), RGB(26, 26, 26));
    REQUIRE_EQ(dark.radius, 12.0f);

    FloatingToolbarSkin light = ResolveFloatingToolbarSkin("fluent", true);
    ApplyFloatingToolbarSkinOverrides(light, package, true);
    REQUIRE_EQ(FlattenCandidateColor(light.fill, light.fill), RGB(255, 247, 250));
    REQUIRE_EQ(FlattenCandidateColor(light.glyph, light.fill), RGB(58, 42, 48));
    REQUIRE_EQ(FlattenCandidateColor(light.handle, light.fill), RGB(107, 105, 214));

    CandidateSkinCatalog::Package plain;
    FloatingToolbarSkin untouched = ResolveFloatingToolbarSkin("fluent", false);
    ApplyFloatingToolbarSkinOverrides(untouched, plain, false);
    REQUIRE_EQ(untouched.radius, 8.0f);
    REQUIRE_EQ(untouched.handleStyle, std::string("bar"));
    REQUIRE_EQ(untouched.dividerStyle, std::string("full"));
    REQUIRE_EQ(untouched.dividerAfterStatus, false);
    REQUIRE(untouched.backdrop.empty());
}

TEST_CASE(floating_toolbar_skin_applies_mac_acrylic_toolbar_design)
{
    CandidateSkinCatalog::Package mac;
    mac.toolbarCornerRadiusDip = 10.0;
    mac.toolbarIconCornerRadiusDip = 6.0;
    mac.toolbarBorderWidthDip = 1.0;
    mac.toolbarBackdrop = "acrylic";
    mac.toolbarBarHeightDip = 36.0;
    mac.toolbarIconBoxDip = 26.0;
    mac.toolbarIconScale = 0.68;
    mac.toolbarItemGapDip = 3.0;
    mac.toolbarPaddingLeftDip = 6.0;
    mac.toolbarPaddingRightDip = 6.0;
    mac.toolbarHandleStyle = "dots";
    mac.toolbarDividerStyle = "short";
    mac.toolbarDividerAfterStatus = true;
    mac.toolbarDark.background = "#28282c";
    mac.toolbarDark.backdropOpacity = 0.70;
    mac.toolbarDark.backdropLuminosity = 0.96;
    mac.toolbarDark.statusBackground = "#0a84ff";
    mac.toolbarDark.statusHover = "#2994ff";
    mac.toolbarDark.statusIcon = "#ffffff";

    FloatingToolbarSkin dark = ResolveFloatingToolbarSkin("fluent", false);
    ApplyFloatingToolbarSkinOverrides(dark, mac, false);
    REQUIRE_EQ(dark.radius, 10.0f);
    REQUIRE_EQ(dark.iconRadius, 6.0f);
    REQUIRE_EQ(dark.borderWidth, 1.0f);
    REQUIRE_EQ(dark.backdrop, std::string("acrylic"));
    REQUIRE_EQ(dark.barHeight, 36.0f);
    REQUIRE_EQ(dark.iconBox, 26.0f);
    REQUIRE_EQ(dark.itemGap, 3.0f);
    REQUIRE_EQ(dark.padLeft, 6.0f);
    REQUIRE_EQ(dark.padRight, 6.0f);
    REQUIRE_EQ(dark.handleStyle, std::string("dots"));
    REQUIRE_EQ(dark.dividerStyle, std::string("short"));
    REQUIRE_EQ(dark.dividerAfterStatus, true);
    REQUIRE_EQ(FlattenCandidateColor(dark.statusFill, dark.fill), RGB(10, 132, 255));
    REQUIRE_EQ(FlattenCandidateColor(dark.statusGlyph, dark.statusFill), RGB(255, 255, 255));
    REQUIRE(std::fabs(dark.backdropOpacity - 0.70f) < 1e-4f);
    REQUIRE(std::fabs(dark.backdropLuminosity - 0.96f) < 1e-4f);
}
