#include "Global/TsfPreeditStyle.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <type_traits>

using namespace GlobalSettings;
static_assert(std::is_same_v<decltype(getTsfPreeditStyle()), std::string_view>);

int main()
{
    if (getTsfPreeditStyle() != TsfPreeditStyle::Raw)
        return 1;
    for (const auto style : {TsfPreeditStyle::Raw, TsfPreeditStyle::Pinyin, TsfPreeditStyle::Empty})
    {
        setTsfPreeditStyle(style);
        const auto &snapshot = getTsfPreeditStyle();
        if (snapshot != style)
            return 2;
        setTsfPreeditStyle(style == TsfPreeditStyle::Empty ? TsfPreeditStyle::Raw : TsfPreeditStyle::Empty);
        // A reader keeps a complete immutable snapshot even after another
        // thread publishes a different style. No reference to mutable storage.
        if (snapshot != style)
            return 3;
    }
    setTsfPreeditStyle("unknown");
    if (getTsfPreeditStyle() != TsfPreeditStyle::Raw)
        return 4;
    for (const auto style : {L"raw", L"pinyin", L"empty", L"unknown", static_cast<const wchar_t *>(nullptr)})
    {
        setTsfPreeditStyleFromWide(style);
        const auto expected = style && wcscmp(style, L"pinyin") == 0  ? TsfPreeditStyle::Pinyin
                              : style && wcscmp(style, L"empty") == 0 ? TsfPreeditStyle::Empty
                                                                      : TsfPreeditStyle::Raw;
        if (getTsfPreeditStyle() != expected)
            return 5;
    }

    std::atomic<bool> start{false};
    std::atomic<int> failures{0};
    const auto writer = [&](std::string_view first, std::string_view second) {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        for (int index = 0; index < 100000; ++index)
            setTsfPreeditStyle(index % 2 ? first : second);
    };
    const auto reader = [&] {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        for (int index = 0; index < 100000; ++index)
        {
            const auto snapshot = getTsfPreeditStyle();
            if (!isKnownTsfPreeditStyle(snapshot))
                ++failures;
            std::this_thread::yield();
            if (!isKnownTsfPreeditStyle(snapshot))
                ++failures;
        }
    };
    std::thread first(writer, TsfPreeditStyle::Raw, TsfPreeditStyle::Pinyin);
    std::thread second(writer, TsfPreeditStyle::Empty, TsfPreeditStyle::Pinyin);
    std::thread third(reader);
    std::thread fourth(reader);
    start.store(true, std::memory_order_release);
    first.join();
    second.join();
    third.join();
    fourth.join();
    if (failures.load() != 0)
    {
        std::fprintf(stderr, "torn preedit-style snapshot\n");
        return 6;
    }
    return 0;
}
