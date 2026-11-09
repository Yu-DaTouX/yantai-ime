#pragma once

#include <cstdint>
#include <vector>

namespace FanyImeIpc
{
// Which TSF client should receive a selection-translate command. client_id is pid << 32 | tid of the client's TSF
// thread. After a Server restart (or a route suspension) no client is active until the focus changes, so the
// active client alone is not enough: fall back to the connected client that lives in the foreground process.
struct SelectionClientChoice
{
    uint64_t client_id = 0;
    // The active epoch when the active client was chosen; 0 when chosen by foreground match (no epoch to pin to).
    uint64_t epoch = 0;
    bool via_foreground = false;
};

inline uint32_t ClientProcessId(uint64_t client_id)
{
    return static_cast<uint32_t>(client_id >> 32);
}

inline uint32_t ClientThreadId(uint64_t client_id)
{
    return static_cast<uint32_t>(client_id & 0xFFFFFFFFull);
}

// preferred_threads: the foreground window's thread and the thread owning keyboard focus.
inline SelectionClientChoice ChooseSelectionClient(uint64_t active_id, uint64_t active_epoch,
                                                   const std::vector<uint64_t> &connected, uint32_t foreground_pid,
                                                   const std::vector<uint32_t> &preferred_threads)
{
    if (active_id != 0)
    {
        for (const uint64_t id : connected)
        {
            if (id == active_id)
                return {active_id, active_epoch, false};
        }
    }
    if (foreground_pid == 0)
        return {};
    std::vector<uint64_t> sameProcess;
    for (const uint64_t id : connected)
    {
        if (id != 0 && ClientProcessId(id) == foreground_pid)
            sameProcess.push_back(id);
    }
    for (const uint32_t thread : preferred_threads)
    {
        for (const uint64_t id : sameProcess)
        {
            if (thread != 0 && ClientThreadId(id) == thread)
                return {id, 0, true};
        }
    }
    // Several TSF threads in one process and none is the focused one: refuse to guess.
    if (sameProcess.size() == 1)
        return {sameProcess.front(), 0, true};
    return {};
}
} // namespace FanyImeIpc
