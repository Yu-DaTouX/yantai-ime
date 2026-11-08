set(TEST_EXECUTABLE_NAME "msimeui-tests")

add_executable(${TEST_EXECUTABLE_NAME}
    src/main.cpp
    src/test_animated_image.cpp
    src/test_animation.cpp
    src/test_card_shadow.cpp
    src/test_candidate_text_grid.cpp
    src/test_device_resources.cpp
    src/test_emoji_text.cpp
    src/test_emoji_flag_font.cpp
    src/test_fonts.cpp
    src/test_image.cpp
    src/test_layout.cpp
    src/test_overlay_panel.cpp
    src/test_font_fallback.cpp
    src/test_menu_flyout_item.cpp
    src/test_pager_arrows.cpp
    src/test_scroll_axis.cpp
    src/test_scroll_viewer.cpp
    src/test_text_layout.cpp
    src/test_text_selection.cpp
    src/test_window_focus.cpp
    includes/test_framework.h
)

# The test sources include their headers as "tests/includes/...", mirroring how
# server/ lays its tests out.
target_include_directories(${TEST_EXECUTABLE_NAME} PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(${TEST_EXECUTABLE_NAME} PRIVATE msimeui)

# 区域旗帜字体随包放在 server/assets/fonts；用例按源码树里的原件加载私有字体集。
target_compile_definitions(${TEST_EXECUTABLE_NAME} PRIVATE
    "MSIME_FLAG_FONT_PATH=\"${CMAKE_SOURCE_DIR}/../server/assets/fonts/Twemoji.Mozilla.ttf\"")

if(MSVC)
    target_compile_options(${TEST_EXECUTABLE_NAME} PRIVATE /W4 /permissive- /utf-8)
endif()

add_test(NAME ${TEST_EXECUTABLE_NAME} COMMAND ${TEST_EXECUTABLE_NAME})
