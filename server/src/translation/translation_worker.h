#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace translation
{
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;

enum class Status
{
    Empty,
    Debouncing,
    Translating,
    Ready,
    Missing,
    Timeout,
    Error
};

struct Request
{
    uint64_t generation = 0;
    std::string chinese;
    Time deadline{};
};

struct Result
{
    uint64_t generation = 0;
    Status status = Status::Error;
    std::string english;
};

// One running job plus one replaceable pending job. Submit/Cancel are cheap; no DLL/model
// loading or joining occurs on the key path. Completion runs on this worker: marshal it to the
// Server owner thread before touching any Server state.
class Worker
{
  public:
    using Cancelled = std::function<bool()>;
    using Infer = std::function<Result(const Request &, const Cancelled &)>;
    using Completion = std::function<void(Result)>;
    Worker(Infer infer, Completion completion);
    ~Worker();
    Worker(const Worker &) = delete;
    Worker &operator=(const Worker &) = delete;
    void Submit(Request request);
    void Cancel();

  private:
    struct Job
    {
        Request request;
        uint64_t ticket;
    };
    void Run();
    Infer infer_;
    Completion completion_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Job> pending_;
    std::atomic<uint64_t> ticket_{0};
    bool stop_ = false;
    std::thread thread_;
};

// Captures the optional engine, which is lazy-loaded by Worker, never by the TSF/UI/key thread.
// Pass <DataDir>/models/translate-zh-en. Missing files/dependencies are an ordinary Missing result.
Worker::Infer LocalInference(std::filesystem::path directory);
} // namespace translation
