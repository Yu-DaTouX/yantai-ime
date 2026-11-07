#include "windows/src/Utils/ThreadStart.h"
#include "tests/includes/test_framework.h"

#include <atomic>
#include <new>
#include <system_error>

namespace
{
template <typename Failure> struct FailingThreadEntry
{
    explicit FailingThreadEntry(std::atomic<bool> &called) : called_(called)
    {
    }
    FailingThreadEntry(const FailingThreadEntry &) = delete;
    FailingThreadEntry(FailingThreadEntry &&other) : called_(other.called_)
    {
        throw Failure();
    }
    void operator()() const
    {
        called_.store(true);
    }
    std::atomic<bool> &called_;
};

struct ThreadResourceFailure : std::system_error
{
    ThreadResourceFailure() : std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again))
    {
    }
};
} // namespace

TEST_CASE(thread_start_success_returns_owned_joinable_thread)
{
    std::atomic<bool> called{false};
    std::thread *thread = FanyThread::TryCreate([](std::atomic<bool> *value) { value->store(true); }, &called);
    REQUIRE(thread != nullptr);
    thread->join();
    delete thread;
    REQUIRE(called.load());
}

TEST_CASE(thread_start_constructor_exception_returns_null_without_running_entry)
{
    std::atomic<bool> called{false};
    // A throwing entry move runs inside std::thread's constructor, before OS
    // startup; inject both failure classes without exhausting the user's host.
    REQUIRE(FanyThread::TryCreate(FailingThreadEntry<ThreadResourceFailure>(called)) == nullptr);
    REQUIRE(FanyThread::TryCreate(FailingThreadEntry<std::bad_alloc>(called)) == nullptr);
    REQUIRE(!called.load());
}
