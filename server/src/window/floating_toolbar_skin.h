#pragma once

#include <d2d1.h>
#include <string>

// Colors, geometry, and material of the D2D floating toolbar, one set per built-in
// skin and theme. Radii and sizes are in DIPs before the user scale.
struct FloatingToolbarSkin
{
    D2D1_COLOR_F fill;
    D2D1_COLOR_F border;
    D2D1_COLOR_F glyph;
    D2D1_COLOR_F hover;
    D2D1_COLOR_F divider;
    D2D1_COLOR_F handle;
    float radius;
    float iconRadius;
    // Optional status-pill accent colors (alpha == 0 means plain icon button).
    D2D1_COLOR_F statusFill = {0.0f, 0.0f, 0.0f, 0.0f};
    D2D1_COLOR_F statusHover = {0.0f, 0.0f, 0.0f, 0.0f};
    D2D1_COLOR_F statusGlyph = {0.0f, 0.0f, 0.0f, 0.0f};
    float borderWidth = 1.4f;
    float barHeight = 35.0f;
    float iconBox = 26.0f;
    float iconScale = 0.82f;
    float itemGap = 5.0f;
    float padLeft = 8.0f;
    float padRight = 4.0f;
    std::string handleStyle = "bar";
    std::string dividerStyle = "full";
    bool dividerAfterStatus = false;
    std::string backdrop;
    // < 0 means fall back to fill.a / material default.
    float backdropOpacity = -1.0f;
    float backdropLuminosity = -1.0f;
};

namespace CandidateSkinCatalog
{
struct Package;
}

// The toolbar of a base skin's look (CandidateSkinCatalog::ResolveBuiltInLook): its [toolbar]
// colors and radii. External skins pass their base skin's look and then layer their own
// [toolbar] overrides with ApplyFloatingToolbarSkinOverrides.
FloatingToolbarSkin ResolveFloatingToolbarSkin(const CandidateSkinCatalog::Package &baseLook, bool light);

// Same, from the factory look of skinId (no data directory involved).
FloatingToolbarSkin ResolveFloatingToolbarSkin(const std::string &skinId, bool light);

// Applies an external skin's [toolbar] colors and corner radii for the given
// theme. Unset or unparsable values keep what the base skin resolved.
void ApplyFloatingToolbarSkinOverrides(FloatingToolbarSkin &skin, const CandidateSkinCatalog::Package &package,
                                       bool light);
