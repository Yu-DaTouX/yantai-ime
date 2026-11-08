#include <cstdint>
#include <cstdio>
#include <string>

using HANDLE = int;
using DWORD = std::uint32_t;
using ULONGLONG = std::uint64_t;
using BOOL = bool;
constexpr HANDLE INVALID_HANDLE_VALUE = -1;
constexpr DWORD GENERIC_READ = 1, GENERIC_WRITE = 2, OPEN_EXISTING = 3;
constexpr DWORD SECURITY_SQOS_PRESENT = 4, SECURITY_IDENTIFICATION = 8;
constexpr DWORD PIPE_READMODE_MESSAGE = 2, PIPE_NOWAIT = 1, ERROR_NO_DATA = 232;
constexpr const wchar_t *FANY_IME_AUX_NAMED_PIPE = L"test-only";

struct PipeDouble
{
    bool available = true;
    bool configureSucceeds = true;
    bool writeSucceeds = true;
    bool quotaAvailable = true;
    bool replyAvailable = true;
    bool replyValid = true;
    DWORD mode = 0;
    int opens = 0;
    int closes = 0;
    int writes = 0;
    int reads = 0;
    int blockingWrites = 0;
    ULONGLONG ticks = 0;
} pipe;

HANDLE CreateFileW(const wchar_t *, DWORD, DWORD, void *, DWORD, DWORD, void *)
{
    ++pipe.opens;
    return pipe.available ? 1 : INVALID_HANDLE_VALUE;
}
BOOL CloseHandle(HANDLE)
{
    ++pipe.closes;
    return true;
}
BOOL SetNamedPipeHandleState(HANDLE, DWORD *mode, void *, void *)
{
    if (!pipe.configureSucceeds)
        return false;
    pipe.mode = *mode;
    return true;
}
BOOL WriteFile(HANDLE, const void *, DWORD bytes, DWORD *written, void *)
{
    ++pipe.writes;
    if ((pipe.mode & PIPE_NOWAIT) == 0)
        ++pipe.blockingWrites;
    *written = pipe.quotaAvailable && pipe.writeSucceeds ? bytes : 0;
    return pipe.writeSucceeds;
}
BOOL ReadFile(HANDLE, void *data, DWORD bytes, DWORD *read, void *)
{
    ++pipe.reads;
    if (!pipe.replyAvailable)
        return false;
    auto text = static_cast<wchar_t *>(data);
    text[0] = L'O';
    text[1] = pipe.replyValid ? L'K' : L'X';
    *read = bytes;
    return true;
}
DWORD GetLastError()
{
    return ERROR_NO_DATA;
}
ULONGLONG GetTickCount64()
{
    return pipe.ticks++;
}
void Sleep(DWORD ms)
{
    pipe.ticks += ms;
}

#include "aux_pipe_source.h"

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                                               \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (false)

int main()
{
    CHECK(SendToAuxNamedpipe(L"IMEActivation", false));
    CHECK(pipe.writes == 1 && pipe.closes == 1 && pipe.reads == 0 && pipe.blockingWrites == 0);
    CHECK(pipe.mode == (PIPE_READMODE_MESSAGE | PIPE_NOWAIT));

    pipe = {};
    CHECK(SendToAuxNamedpipe(L"TerminalDeactivation|1|2", true));
    CHECK(pipe.writes == 1 && pipe.reads == 1 && pipe.closes == 1 && pipe.blockingWrites == 0);

    pipe = {};
    pipe.configureSucceeds = false;
    CHECK(!SendToAuxNamedpipe(L"IMEActivation", false));
    CHECK(pipe.writes == 0 && pipe.closes == 1 && pipe.reads == 0);

    // PIPE_NOWAIT can report success with zero bytes when message quota is full.
    // A lifecycle packet must not be retried: it could otherwise apply twice.
    pipe = {};
    pipe.quotaAvailable = false;
    CHECK(!SendToAuxNamedpipe(L"TerminalDeactivation|1|2", true));
    CHECK(pipe.writes == 1 && pipe.closes == 1 && pipe.reads == 0 && pipe.blockingWrites == 0);

    pipe = {};
    pipe.writeSucceeds = false;
    CHECK(!SendToAuxNamedpipe(L"IMEActivation", false));
    CHECK(pipe.writes == 1 && pipe.closes == 1 && pipe.reads == 0);

    pipe = {};
    pipe.replyAvailable = false;
    CHECK(!SendToAuxNamedpipe(L"TerminalDeactivation|1|2", true));
    CHECK(pipe.ticks >= 150 && pipe.ticks <= 155 && pipe.closes == 1 && pipe.writes == 1);

    pipe = {};
    pipe.replyValid = false;
    CHECK(!SendToAuxNamedpipe(L"TerminalDeactivation|1|2", true));
    CHECK(pipe.closes == 1 && pipe.writes == 1 && pipe.reads == 1);

    pipe = {};
    pipe.available = false;
    CHECK(!SendToAuxNamedpipe(L"IMEActivation", false));
    CHECK(pipe.opens == 5 && pipe.ticks == 100 && pipe.closes == 0 && pipe.writes == 0);
    return 0;
}
