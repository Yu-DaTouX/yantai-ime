#include "tests/includes/test_framework.h"
#include "translation/translation_worker.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace translation;
using namespace std::chrono_literals;

TEST_CASE(translation_worker_replaces_pending_requests_and_cancels_running_generation)
{
    std::mutex mutex;
    std::condition_variable wake;
    bool first_started = false;
    bool release_first = false;
    std::vector<uint64_t> completed;
    std::atomic<bool> cancellation_seen{false};
    {
        Worker worker(
            [&](const Request &request, const Worker::Cancelled &cancelled) {
                if (request.generation == 1)
                {
                    std::unique_lock lock(mutex);
                    first_started = true;
                    wake.notify_all();
                    wake.wait_for(lock, 2s, [&] { return release_first; });
                    cancellation_seen = cancelled();
                }
                return Result{request.generation, Status::Ready, "result"};
            },
            [&](Result result) {
                std::lock_guard lock(mutex);
                completed.push_back(result.generation);
                wake.notify_all();
            });
        const auto deadline = Clock::now() + 5s;
        worker.Submit({1, "first", deadline});
        bool started;
        {
            std::unique_lock lock(mutex);
            started = wake.wait_for(lock, 2s, [&] { return first_started; });
        }
        worker.Submit({2, "never runs", deadline});
        worker.Submit({3, "newest", deadline});
        {
            std::lock_guard lock(mutex);
            release_first = true;
        }
        wake.notify_all();
        bool done;
        {
            std::unique_lock lock(mutex);
            done = wake.wait_for(lock, 2s, [&] { return !completed.empty(); });
        }
        REQUIRE(started);
        REQUIRE(done);
    }
    REQUIRE(cancellation_seen);
    REQUIRE_EQ(completed.size(), size_t(1));
    REQUIRE_EQ(completed.front(), uint64_t(3));
}

TEST_CASE(translation_worker_expired_job_never_calls_inference)
{
    std::atomic<int> calls{0};
    std::mutex mutex;
    std::condition_variable wake;
    bool done = false;
    Result result;
    bool received;
    {
        Worker worker(
            [&](const Request &, const Worker::Cancelled &) {
                ++calls;
                return Result{};
            },
            [&](Result value) {
                std::lock_guard lock(mutex);
                result = std::move(value);
                done = true;
                wake.notify_all();
            });
        worker.Submit({42, "中文", Clock::now() - 1ms});
        std::unique_lock lock(mutex);
        received = wake.wait_for(lock, 2s, [&] { return done; });
    }
    REQUIRE(received);
    REQUIRE_EQ(calls.load(), 0);
    REQUIRE_EQ(result.generation, uint64_t(42));
    REQUIRE_EQ(result.status, Status::Timeout);
}

TEST_CASE(translation_worker_shutdown_cancels_and_joins_before_callback_owner_dies)
{
    std::atomic<bool> started{false};
    std::atomic<int> completed{0};
    {
        Worker worker(
            [&](const Request &request, const Worker::Cancelled &cancelled) {
                started = true;
                const auto bound = Clock::now() + 2s;
                while (!cancelled() && Clock::now() < bound)
                    std::this_thread::sleep_for(1ms);
                return Result{request.generation, Status::Ready, "discarded"};
            },
            [&](Result) { ++completed; });
        worker.Submit({1, "中文", Clock::now() + 5s});
        const auto bound = Clock::now() + 2s;
        while (!started && Clock::now() < bound)
            std::this_thread::sleep_for(1ms);
    }
    REQUIRE(started);
    REQUIRE_EQ(completed.load(), 0);
}
