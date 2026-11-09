#pragma once

#include "engine/contracts/selection_translate.h"
#include "translation/sentence_split.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace translation
{
// Pure state machine for the toolbar "译" / "还原" button. It owns no threads, pipes or windows: the caller feeds it
// events and performs the returned Action, which keeps every transition testable with a fake clock.
//
//   Idle --click--> Reading --text--> Translating --done--> Applying --ok--> Restorable --click--> Restoring --ok-->
//   Idle
// Every phase falls back to Idle on failure, cancellation or timeout, and Restorable also on expiry or invalidation.
namespace selection
{
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;

enum class Phase
{
    Idle,
    Reading,
    Translating,
    Applying,
    Restorable,
    Restoring,
};

// What the button shows.
enum class ButtonView
{
    Translate, // "译"
    Busy,      // "译中" / "2/5"
    Restore,   // "还原"
};

enum class Tip
{
    None,
    SelectFirst,
    Unsupported,
    NeedFocus,
    NotInstalled,
    NotChinese,
    TooLong,
    Composing,
    Failed,
    Timeout,
    SelectionChanged,
    HostRejected,
    CannotRestore,
};

enum class Action
{
    None,
    SendRead,      // ask the TSF client for the selection text
    TranslateNext, // run NextSource() through the engine
    SendApply,     // send ApplyText() to the TSF client
    SendRestore,   // ask the TSF client to put the original text back
};

struct Step
{
    Action action = Action::None;
    Tip tip = Tip::None;
    bool changed = false; // phase, progress or label changed: redraw the button
};

inline constexpr auto kReadTimeout = std::chrono::seconds(4);
inline constexpr auto kTranslateTimeout = std::chrono::seconds(90);
inline constexpr auto kHostEditTimeout = std::chrono::seconds(4);
// How long "还原" stays available after a replacement.
inline constexpr auto kRestoreWindow = std::chrono::seconds(10);

const wchar_t *TipText(Tip tip);

class Machine
{
  public:
    Phase phase() const
    {
        return phase_;
    }
    uint32_t job() const
    {
        return job_;
    }
    ButtonView view() const;
    // Done / total engine pieces while Translating (both 0 otherwise); the label shows them when total > 1.
    size_t done() const
    {
        return done_;
    }
    size_t total() const
    {
        return total_;
    }

    Step Click(Time now, bool componentInstalled);
    Step OnSelectionText(uint32_t job, const std::wstring &text, Time now);
    Step OnHostFailure(uint32_t job, FanyImeSelectionTranslate::Failure failure);
    // status: engine outcome of the piece returned by NextSource(); english is UTF-16.
    enum class EngineStatus
    {
        Ready,
        Missing,
        Timeout,
        Error,
    };
    Step OnPieceTranslated(uint32_t job, EngineStatus status, const std::wstring &english, Time now);
    Step OnApplied(uint32_t job, Time now);
    Step OnRestored(uint32_t job);
    // The TSF client reports the selection moved away from the translation.
    Step OnInvalidated(uint32_t job);
    // Focus / active client changed: whatever is pending belongs to a window the user left.
    Step OnRouteChanged();
    // Advances timers. Call at least every 500 ms while phase() != Idle.
    Step Tick(Time now);

    // Source of the piece to translate next (valid after Action::TranslateNext).
    std::wstring NextSource() const;
    // Assembled English text (valid after Action::SendApply).
    const std::wstring &ApplyText() const
    {
        return applyText_;
    }

  private:
    Step ToIdle(Tip tip);
    Step ToIdleIf(Phase expected, uint32_t job, Tip tip);

    Phase phase_ = Phase::Idle;
    uint32_t job_ = 0;
    Time deadline_{};
    SplitText split_;
    std::vector<size_t> pending_; // indexes into split_.pieces still to translate, front first
    std::vector<std::wstring> translated_;
    std::wstring applyText_;
    size_t done_ = 0;
    size_t total_ = 0;
};
} // namespace selection
} // namespace translation
