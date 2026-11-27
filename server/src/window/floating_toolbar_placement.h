#pragma once

#include "defines/base_structures.h"

#include <algorithm>
#include <cmath>
#include <windows.h>

namespace FanyImeUi
{
inline constexpr double kFloatingToolbarDefaultCornerInsetDip = 10.0;

// Computes the default bottom-right host top-left (physical px) inside the
// primary monitor's work area (`rcWork`), so a taskbar or appbar docked on any
// edge (including a right-side vertical taskbar) never clips or covers the
// visible toolbar. `shadowInsetsPx` is the transparent shadow/slack margin
// around the visible bar.
inline POINT FloatingToolbarDefaultPosition(int hostWidthPx, int hostHeightPx, const RECT &shadowInsetsPx,
                                            const MonitorCoordinates &workArea, FLOAT scale)
{
    const double safeScale = scale > 0.0f ? static_cast<double>(scale) : 1.0;
    const int cornerInsetPx = static_cast<int>(std::lround(kFloatingToolbarDefaultCornerInsetDip * safeScale));
    int x = workArea.right - hostWidthPx + shadowInsetsPx.right - cornerInsetPx;
    int y = workArea.bottom - hostHeightPx + shadowInsetsPx.bottom - cornerInsetPx;
    const int minX = workArea.left - shadowInsetsPx.left;
    const int minY = workArea.top - shadowInsetsPx.top;
    x = (std::max)(x, minX);
    y = (std::max)(y, minY);
    return {x, y};
}
} // namespace FanyImeUi
