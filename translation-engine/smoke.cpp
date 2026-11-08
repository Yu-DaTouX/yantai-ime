#include "translation/local_engine.h"

#include <Windows.h>
#include <psapi.h>
#include <chrono>
#include <cstdio>
#include <filesystem>

#pragma comment(lib, "psapi.lib")

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2)
        return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    translation::LocalEngine engine;
    const auto start = std::chrono::steady_clock::now();
    if (!engine.Load(std::filesystem::path(argv[1])))
    {
        std::puts("engine unavailable");
        return 1;
    }
    const auto loaded = std::chrono::steady_clock::now();
    std::string english;
    const auto status = engine.Translate("今天的会议改到下午三点。", 1000, nullptr, nullptr, english);
    const auto done = std::chrono::steady_clock::now();
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
    std::printf("status=%d load_ms=%lld translate_ms=%lld peak_working_set=%zu\n", status,
                std::chrono::duration_cast<std::chrono::milliseconds>(loaded - start).count(),
                std::chrono::duration_cast<std::chrono::milliseconds>(done - loaded).count(),
                static_cast<size_t>(counters.PeakWorkingSetSize));
    std::printf("%s\n", english.c_str());
    std::fflush(stdout);
    if (status != YT_TRANSLATE_OK || english.empty())
        return 1;
    auto cancel = [](void *) -> int32_t { return 1; };
    if (engine.Translate("你好", 1000, cancel, nullptr, english) != YT_TRANSLATE_CANCELLED || !english.empty())
        return 1;
    engine.Unload();
    // The runtime remains pinned (oneDNN/OpenMP TLS cannot safely outlive FreeLibrary), but
    // the model must be destructible/reloadable and a missing component must still be harmless.
    if (!engine.Load(std::filesystem::path(argv[1])))
        return 1;
    engine.Unload();
    if (engine.Load(std::filesystem::path(argv[1]) / "does-not-exist"))
        return 1;
    std::puts("PASS: local int8 translation, cancellation, reload and missing DLL");
    return 0;
}
