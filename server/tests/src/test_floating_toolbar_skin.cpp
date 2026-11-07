#include "tests/includes/test_framework.h"
#include "skin/candidate_skin_catalog.h"
#include "window/candidate_skin_palette.h"
#include "window/floating_toolbar_skin.h"

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
}
