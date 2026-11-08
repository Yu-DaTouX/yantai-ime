#include "translation/translation_worker.h"

#include <utility>

namespace translation
{
Worker::Worker(Infer infer, Completion completion)
    : infer_(std::move(infer)), completion_(std::move(completion)), thread_(&Worker::Run, this)
{
}

Worker::~Worker()
{
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        ++ticket_;
        pending_.reset();
    }
    wake_.notify_one();
    if (thread_.joinable())
        thread_.join();
}

void Worker::Submit(Request request)
{
    {
        std::lock_guard lock(mutex_);
        pending_ = Job{std::move(request), ++ticket_};
    }
    wake_.notify_one();
}

void Worker::Cancel()
{
    std::lock_guard lock(mutex_);
    ++ticket_;
    pending_.reset();
}

void Worker::Run()
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&] { return stop_ || pending_.has_value(); });
            if (stop_)
                return;
            job = std::move(*pending_);
            pending_.reset();
        }
        const Cancelled cancelled = [this, ticket = job.ticket] { return ticket_.load() != ticket; };
        Result result{job.request.generation, Status::Error, {}};
        try
        {
            if (Clock::now() >= job.request.deadline)
                result.status = Status::Timeout;
            else
                result = infer_(job.request, cancelled);
        }
        catch (...)
        {
            // Never log source text/exception contents. A failed component is a normal error.
        }
        if (cancelled())
            continue;
        result.generation = job.request.generation;
        if (Clock::now() >= job.request.deadline)
        {
            result.status = Status::Timeout;
            result.english.clear();
        }
        try
        {
            completion_(std::move(result));
        }
        catch (...)
        {
            // A stopped owner must not terminate the process from a worker callback.
        }
    }
}
} // namespace translation
