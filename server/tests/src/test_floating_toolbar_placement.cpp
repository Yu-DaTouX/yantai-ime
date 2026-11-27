#include "tests/includes/test_framework.h"
#include "window/floating_toolbar_placement.h"

#include <cmath>

TEST_CASE(floating_toolbar_default_bottom_taskbar_each_scale)
{
    for (const int dpi : {96, 120, 144, 192})
    {
        const FLOAT scale = static_cast<FLOAT>(dpi) / 96.0f;
        const auto px = [dpi](int dip) { return MulDiv(dip, dpi, 96); };
        const MonitorCoordinates work{0, 0, px(1920), px(1032)};
        for (const bool shadow : {false, true})
        {
            const RECT insets{shadow ? px(16) : px(2), shadow ? px(14) : px(2), shadow ? px(32) : px(2),
                              shadow ? px(34) : px(2)};
            const int hostWidth = px(239) + insets.left + insets.right;
            const int hostHeight = px(35) + insets.top + insets.bottom;
            const POINT pos = FanyImeUi::FloatingToolbarDefaultPosition(hostWidth, hostHeight, insets, work, scale);
            const int insetPx = static_cast<int>(std::lround(10.0 * static_cast<double>(scale)));
            REQUIRE_EQ(pos.x + hostWidth - insets.right, work.right - insetPx);
            REQUIRE_EQ(pos.y + hostHeight - insets.bottom, work.bottom - insetPx);
        }
    }
}

TEST_CASE(floating_toolbar_default_right_taskbar_avoids_overlap)
{
    for (const int dpi : {96, 120, 144, 192})
    {
        const FLOAT scale = static_cast<FLOAT>(dpi) / 96.0f;
        const auto px = [dpi](int dip) { return MulDiv(dip, dpi, 96); };
        // Vertical taskbar docked on the right edge (e.g. 75 DIPs wide).
        const MonitorCoordinates work{0, 0, px(1845), px(1080)};
        const RECT insets{px(16), px(14), px(32), px(34)};
        const int hostWidth = px(239) + insets.left + insets.right;
        const int hostHeight = px(35) + insets.top + insets.bottom;
        const POINT pos = FanyImeUi::FloatingToolbarDefaultPosition(hostWidth, hostHeight, insets, work, scale);
        const int insetPx = static_cast<int>(std::lround(10.0 * static_cast<double>(scale)));
        // Visible bar sits to the left of the right-side taskbar, and near the bottom.
        REQUIRE_EQ(pos.x + hostWidth - insets.right, work.right - insetPx);
        REQUIRE_EQ(pos.y + hostHeight - insets.bottom, work.bottom - insetPx);
        REQUIRE(pos.x + hostWidth - insets.right < work.right);
    }
}

TEST_CASE(floating_toolbar_default_left_and_top_taskbars)
{
    const RECT insets{16, 14, 32, 34};
    const int hostWidth = 287;
    const int hostHeight = 83;
    const MonitorCoordinates leftWork{80, 0, 1920, 1080};
    const POINT leftPos = FanyImeUi::FloatingToolbarDefaultPosition(hostWidth, hostHeight, insets, leftWork, 1.0f);
    REQUIRE_EQ(leftPos.x + hostWidth - insets.right, 1910);
    REQUIRE_EQ(leftPos.y + hostHeight - insets.bottom, 1070);

    const MonitorCoordinates topWork{0, 48, 1920, 1080};
    const POINT topPos = FanyImeUi::FloatingToolbarDefaultPosition(hostWidth, hostHeight, insets, topWork, 1.0f);
    REQUIRE_EQ(topPos.x + hostWidth - insets.right, 1910);
    REQUIRE_EQ(topPos.y + hostHeight - insets.bottom, 1070);
}

TEST_CASE(floating_toolbar_default_clamps_to_narrow_work_area_and_negative_coords)
{
    const RECT insets{16, 14, 32, 34};
    const MonitorCoordinates negWork{-1920, -1080, -80, -40};
    const POINT negPos = FanyImeUi::FloatingToolbarDefaultPosition(287, 83, insets, negWork, 1.0f);
    REQUIRE_EQ(negPos.x + 287 - insets.right, -90);
    REQUIRE_EQ(negPos.y + 83 - insets.bottom, -50);

    const MonitorCoordinates narrowWork{0, 0, 200, 100};
    const POINT clamped = FanyImeUi::FloatingToolbarDefaultPosition(287, 83, insets, narrowWork, 1.0f);
    REQUIRE_EQ(clamped.x + insets.left, narrowWork.left);
    REQUIRE_EQ(clamped.y + 83 - insets.bottom, 90);
}
