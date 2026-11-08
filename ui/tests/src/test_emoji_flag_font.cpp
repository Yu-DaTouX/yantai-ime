#include "tests/includes/test_framework.h"

#include "msimeui/Controls.h"
#include "msimeui/Fonts.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

using namespace msimeui;

TEST_CASE(private_font_collection_loads_the_bundled_flag_font)
{
    const std::wstring path = std::filesystem::u8path(MSIME_FLAG_FONT_PATH).wstring();
    const std::shared_ptr<IDWriteFontCollection> collection = LoadPrivateFontCollection(path);
    REQUIRE(collection != nullptr);
    UINT32 index = 0;
    BOOL exists = FALSE;
    REQUIRE(SUCCEEDED(collection->FindFamilyName(L"Twemoji Mozilla", &index, &exists)));
    REQUIRE(exists);
    // 缺失路径要安静地退回空字体集，由调用方决定怎么画。
    REQUIRE(LoadPrivateFontCollection(path + L".missing") == nullptr);
}

TEST_CASE(flag_candidates_are_measured_with_the_private_flag_font)
{
    const std::shared_ptr<IDWriteFontCollection> collection =
        LoadPrivateFontCollection(std::filesystem::u8path(MSIME_FLAG_FONT_PATH).wstring());
    REQUIRE(collection != nullptr);

    const auto widthOf = [&](const std::wstring &text, bool withFlagFont) {
        CandidateList::Appearance appearance;
        appearance.fontFamily = L"Segoe UI";
        appearance.fallbackFontFamilies = {L"Microsoft YaHei"};
        appearance.fontSize = 36.0f;
        appearance.labelFontSize = 26.0f;
        appearance.itemHeight = 48.0f;
        appearance.contentPadLeft = 0.0f;
        appearance.contentPadRight = 0.0f;
        appearance.textPadLeft = 5.0f;
        appearance.labelGap = 5.0f;
        appearance.fitContent = true;
        if (withFlagFont)
        {
            appearance.flagFontFamily = L"Twemoji Mozilla";
            appearance.flagFontCollection = collection;
        }
        CandidateList list(appearance.itemHeight);
        list.SetAppearance(appearance);
        list.SetOrientation(CandidateList::Orientation::Horizontal);
        list.SetItems({{L"1", text, L"", L"", {}}});
        return list.Measure({2000.0f, 10000.0f}).width;
    };

    // 一面旗在 Twemoji Mozilla 里是一个 1 em 的连字字形，Segoe UI Emoji 没有国旗图案、
    // 只能画成两个字母，量出来的宽度因此不同；这证明区域旗帜确实换到了私有字体集。
    const std::wstring flag = L"\U0001F1E8\U0001F1F3";
    const float withFlagFont = widthOf(flag, true);
    const float withoutFlagFont = widthOf(flag, false);
    REQUIRE(withFlagFont < withoutFlagFont);

    // 普通汉字不受国旗字体影响。
    const std::wstring hanzi = L"\u6c49\u5b57";
    REQUIRE(std::fabs(widthOf(hanzi, true) - widthOf(hanzi, false)) < 0.01f);
}
