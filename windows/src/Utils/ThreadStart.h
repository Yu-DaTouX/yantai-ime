#pragma once

#include <thread>
#include <utility>

namespace FanyThread
{
// nothrow new only handles allocation failure, not std::thread's constructor
// (which can throw system_error when the OS cannot start another thread).
// Callers keep their existing nullable-pointer cleanup/activation-failure path.
template <typename Function, typename... Args> std::thread *TryCreate(Function &&function, Args &&...args) noexcept
{
    try
    {
        return new std::thread(std::forward<Function>(function), std::forward<Args>(args)...);
    }
    catch (...)
    {
        return nullptr;
    }
}
} // namespace FanyThread
