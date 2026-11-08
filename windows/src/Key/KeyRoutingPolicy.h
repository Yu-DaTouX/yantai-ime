#pragma once

// Which layer owns a key before the local classifier runs: the Server's candidate list, the
// composition's segment editing, or the application. Kept free of COM/TSF and of
// GetAsyncKeyState (the caller passes the modifier state it captured), so windows/tests can drive
// the same decisions a TSF host would.
//
// The English-line chords ([keybindings] commit_english / english_senses) leave TSF as server
// candidate keys; the Server makes the matching decision in
// server/src/ipc/candidate_key_route_policy.h. The two sides have to agree, or the chord reaches
// neither branch.

#include "PinyinCommitShortcutPolicy.h"

namespace key_route_policy
{

// Modifier bits as CaptureIpcModifiers() captures them.
inline constexpr unsigned kShift = 0b001;
inline constexpr unsigned kControl = 0b010;
inline constexpr unsigned kAlt = 0b100;
inline constexpr unsigned kModifierMask = kShift | kControl | kAlt;

// VK_RETURN comes from PinyinCommitShortcutPolicy.h (kVirtualKeyReturn).
// [keybindings] commit_english: commit the candidate's English line.
inline bool IsEnglishCommitChord(bool matchesConfiguredBinding, bool windowsKeyDown)
{
    return !windowsKeyDown && matchesConfiguredBinding;
}

// [keybindings] english_senses: expand the highlighted candidate's senses.
inline bool IsEnglishSensesChord(bool matchesConfiguredBinding, bool windowsKeyDown)
{
    return !windowsKeyDown && matchesConfiguredBinding;
}

struct ServerCandidateShortcut
{
    bool freshCompositionState = false;
    bool candidatesActive = false;
    bool englishCommitChord = false;
    bool englishSensesChord = false;
    // Shift+Enter commits the Server's converted spelling; it needs a spelling to convert.
    bool pinyinCommitChord = false;
    bool pinyinCommitEligible = false;
};

// The chords above all leave TSF as FUNCTION_SERVER_CANDIDATE_KEY; the Server's reply is what
// commits text. Anything else must fall through to the local classifier.
inline bool ShouldClaimForServerCandidate(const ServerCandidateShortcut &shortcut)
{
    if (shortcut.freshCompositionState)
    {
        return false;
    }
    const bool candidateChord =
        shortcut.candidatesActive && (shortcut.englishCommitChord || shortcut.englishSensesChord);
    return candidateChord || (shortcut.pinyinCommitEligible && shortcut.pinyinCommitChord);
}

// Whether Shift+Enter should be treated as the pinyin commit chord at all.
inline bool IsPinyinCommitChord(bool commitShiftEnterEnabled, unsigned virtualKey, unsigned modifiers,
                                bool windowsKeyDown)
{
    return ShouldCommitPinyinOnShiftEnter(commitShiftEnterEnabled, virtualKey, modifiers, windowsKeyDown);
}

// What to do with a key that carries Ctrl/Alt or a Windows key. The local classifier is only
// allowed to see unmodified keys; Ctrl+Backspace / Ctrl+Left / Ctrl+Right inside a composition are
// the one IME-owned exception, because one input unit's length is only known to the Server.
enum class ModifiedKeyDisposition
{
    ClassifyLocally,
    SegmentEdit,
    Application,
};

inline ModifiedKeyDisposition RouteModifiedKey(unsigned modifiers, bool windowsKeyDown, bool segmentEditApplies)
{
    if ((modifiers & (kControl | kAlt)) == 0 && !windowsKeyDown)
    {
        return ModifiedKeyDisposition::ClassifyLocally;
    }
    if (segmentEditApplies)
    {
        return ModifiedKeyDisposition::SegmentEdit;
    }
    return ModifiedKeyDisposition::Application;
}

} // namespace key_route_policy
