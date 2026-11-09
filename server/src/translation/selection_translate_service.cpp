#include "translation/selection_translate_service.h"

#include "defines/defines.h"
#include "defines/globals.h"
#include "ipc/ipc.h"
#include "settings/translation_model.h"
#include "translation/translation_worker.h"
#include "utils/common_utils.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace translation::selection_service
{
namespace
{
using namespace FanyImeSelectionTranslate;
using selection::Action;
using selection::Machine;
using selection::Phase;
using selection::Step;
using selection::Tip;

std::mutex g_mutex;
std::condition_variable g_wake;
Machine g_machine;
uint64_t g_client = 0;
uint64_t g_epoch = 0;
ChunkAssembler g_read;
// Deliberately never destroyed, like the gloss model: unloading would have to join a possibly running inference.
Worker *g_worker = nullptr;
bool g_tickerStarted = false;

void CompleteFromWorker(Result result);
void Handle(Step step);

void PostUi(Tip tip)
{
    const HWND window = ::global_hwnd_ftb;
    if (window && IsWindow(window))
        PostMessage(window, WM_SELECTION_TRANSLATE_UI, static_cast<WPARAM>(tip), 0);
}

bool SendCommand(const DownFrame &frame)
{
    return SendToTsfWorkerThreadClientViaNamedpipe(
        g_client, g_epoch, Global::DataFromServerMsgTypeToTsfWorkerThread::SelectionTranslateCommand,
        EncodeDown(frame));
}

Worker &EnsureWorker()
{
    if (!g_worker)
        g_worker = new Worker(LocalInference(translation_model::Directory()), CompleteFromWorker);
    return *g_worker;
}

// Runs the step's action. Called with g_mutex held; may produce a follow-up step (for example when a send fails).
void Handle(Step step)
{
    while (true)
    {
        if (step.changed || step.tip != Tip::None)
            PostUi(step.tip);
        Step next;
        switch (step.action)
        {
        case Action::SendRead: {
            const PipeClientActivation active = GetActivePipeClient();
            g_client = active.client_id;
            g_epoch = active.epoch;
            g_read.Reset();
            if (g_client == 0 || !SendCommand({Command::Read, g_machine.job(), 0, 1, {}}))
                next = g_machine.OnHostFailure(g_machine.job(), Failure::Unsupported);
            break;
        }
        case Action::TranslateNext: {
            const std::wstring source = g_machine.NextSource();
            const auto budget = g_machine.done() == 0 ? std::chrono::seconds(10) : std::chrono::seconds(5);
            EnsureWorker().Submit({g_machine.job(), wstring_to_string(source), Clock::now() + budget});
            break;
        }
        case Action::SendApply: {
            const std::vector<std::wstring> chunks = SplitChunks(g_machine.ApplyText(), kDownChunkChars);
            bool sent = chunks.size() <= kMaxChunks;
            for (size_t i = 0; sent && i < chunks.size(); ++i)
            {
                sent = SendCommand({Command::Apply, g_machine.job(), static_cast<uint32_t>(i),
                                    static_cast<uint32_t>(chunks.size()), chunks[i]});
            }
            if (!sent)
                next = g_machine.OnHostFailure(g_machine.job(), Failure::HostRejected);
            break;
        }
        case Action::SendRestore:
            if (!SendCommand({Command::Restore, g_machine.job(), 0, 1, {}}))
                next = g_machine.OnHostFailure(g_machine.job(), Failure::HostRejected);
            break;
        case Action::None:
            break;
        }
        // A job that ended (or was abandoned) must not keep the engine busy with its remaining pieces.
        if (g_worker && g_machine.phase() == Phase::Idle)
            g_worker->Cancel();
        if (next.action == Action::None && !next.changed && next.tip == Tip::None)
            return;
        step = next;
    }
}

void CompleteFromWorker(Result result)
{
    selection::Machine::EngineStatus status = selection::Machine::EngineStatus::Error;
    switch (result.status)
    {
    case translation::Status::Ready:
        status = selection::Machine::EngineStatus::Ready;
        break;
    case translation::Status::Missing:
        status = selection::Machine::EngineStatus::Missing;
        break;
    case translation::Status::Timeout:
        status = selection::Machine::EngineStatus::Timeout;
        break;
    default:
        break;
    }
    std::lock_guard lock(g_mutex);
    Handle(g_machine.OnPieceTranslated(static_cast<uint32_t>(result.generation), status,
                                       string_to_wstring(result.english), Clock::now()));
}

void Ticker()
{
    std::unique_lock lock(g_mutex);
    for (;;)
    {
        if (g_machine.phase() == Phase::Idle)
            g_wake.wait(lock, [] { return g_machine.phase() != Phase::Idle; });
        else
            g_wake.wait_for(lock, std::chrono::milliseconds(250));
        Handle(g_machine.Tick(Clock::now()));
    }
}

void EnsureTicker()
{
    if (g_tickerStarted)
        return;
    g_tickerStarted = true;
    std::thread(Ticker).detach();
}
} // namespace

std::wstring ButtonLabel(const ButtonState &state)
{
    switch (state.view)
    {
    case selection::ButtonView::Restore:
        return L"还原";
    case selection::ButtonView::Busy:
        return state.total > 1 ? std::to_wstring(state.done) + L"/" + std::to_wstring(state.total) : L"译中";
    case selection::ButtonView::Translate:
        break;
    }
    return L"译";
}

ButtonState CurrentButton()
{
    std::lock_guard lock(g_mutex);
    return {g_machine.view(), g_machine.done(), g_machine.total()};
}

void OnToolbarClick()
{
    std::lock_guard lock(g_mutex);
    EnsureTicker();
    const bool installed = !translation_model::ResolveDirectory().empty();
    Handle(g_machine.Click(Clock::now(), installed));
    g_wake.notify_all();
}

void OnTsfReport(uint64_t client_id, uint64_t activation_epoch, const FanyImeNamedpipeData &report)
{
    const uint32_t job = static_cast<uint32_t>(report.point[0]);
    std::lock_guard lock(g_mutex);
    if (g_machine.phase() == Phase::Idle || client_id != g_client || activation_epoch != g_epoch ||
        job != g_machine.job())
        return;
    const Time now = Clock::now();
    switch (static_cast<ReportKind>(report.keycode))
    {
    case ReportKind::ReadChunk: {
        const uint32_t packed = static_cast<uint32_t>(report.point[1]);
        const auto outcome = g_read.Add(job, packed >> 16, packed & 0xFFFFu,
                                        std::wstring(report.pinyin_string, static_cast<size_t>(report.pinyin_length)),
                                        kMaxSelectionChars);
        if (outcome == ChunkAssembler::Result::Complete)
            Handle(g_machine.OnSelectionText(job, g_read.Take(), now));
        else if (outcome == ChunkAssembler::Result::Invalid)
            Handle(g_machine.OnHostFailure(job, Failure::Unsupported));
        break;
    }
    case ReportKind::Failure:
        Handle(g_machine.OnHostFailure(job, static_cast<Failure>(report.wch)));
        break;
    case ReportKind::Applied:
        Handle(g_machine.OnApplied(job, now));
        break;
    case ReportKind::Restored:
        Handle(g_machine.OnRestored(job));
        break;
    case ReportKind::Invalidated:
        Handle(g_machine.OnInvalidated(job));
        break;
    }
}

void OnRouteChanged()
{
    std::lock_guard lock(g_mutex);
    Handle(g_machine.OnRouteChanged());
}
} // namespace translation::selection_service
