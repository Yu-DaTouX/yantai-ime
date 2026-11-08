#include "translation/translation_worker.h"
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
int wmain(int argc, wchar_t **argv)
{
    if (argc != 2)
        return 2;
    std::mutex mutex;
    std::condition_variable wake;
    bool done = false;
    translation::Result result;
    translation::Worker worker(translation::LocalInference(std::filesystem::path(argv[1])),
                               [&](translation::Result value) {
                                   std::lock_guard lock(mutex);
                                   result = std::move(value);
                                   done = true;
                                   wake.notify_one();
                               });
    worker.Submit({77, "今天的会议改到下午三点。", translation::Clock::now() + std::chrono::seconds(1)});
    std::unique_lock lock(mutex);
    if (!wake.wait_for(lock, std::chrono::seconds(2), [&] { return done; }))
        return 1;
    std::printf("generation=%llu status=%d english=%s\n", static_cast<unsigned long long>(result.generation),
                static_cast<int>(result.status), result.english.c_str());
    if (result.generation != 77 || result.status != translation::Status::Ready ||
        result.english != "Today's meeting has been rescheduled to 3 p.m.")
        return 1;
    std::puts("PASS: real background worker, component receipt, local INT8 inference and shutdown");
    return 0;
}
