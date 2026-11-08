#pragma once

// Which layer owns a key before the local classifier runs: the Server's candidate list, the
// composition's segment editing, or the application. Kept free of COM/TSF and of
// GetAsyncKeyState (the caller passes the modifier state it captured), so windows/tests can drive
// the same decisions a TSF host would.
//
// The order inside ShouldClaimForServerCandidate is the one that matters: the English-line chords
// ([keybindings] commit_english / english_senses) and the translation chords (Ctrl+Enter,
// Ctrl+1..9) all sit on Ctrl+Enter, and the translation switch decides which one a chord means.
// The Server runs the same order (server/src/ipc/candidate_key_route_policy.h); the 51d7ee1d bug
// was the two sides disagreeing, which made Ctrl+Enter reach neither branch.

#include "PinyinCommitShortcutPolicy.h"

namespace key_route_policy
{

// Modifier bits as CaptureIpcModifiers() captures them.
inline constexpr unsigned kShift = 0b001;
inline constexpr unsigned kControl = 0b010;
inline constexpr unsigned kAlt = 0b100;
inline constexpr unsigned kModifierMask = kShift | kControl | kAlt;

// VK_RETURN comes from PinyinCommitShortcutPolicy.h (kVirtualKeyReturn).
// Ctrl+Enter, without Shift/Alt/Windows, while candidate translations are on: commit the
// translation shown to the right of the highlighted candidate. The Server decides what that is,
// so the key only has to reach it as a candidate key. With translations off there is nothing to
// commit and eating the chord would take Ctrl+Enter away from a chat box's "send".
inline bool IsTranslationCommitChord(bool translationsEnabled, unsigned virtualKey, unsigned modifiers,
                                     bool windowsKeyDown)
{
    return translationsEnabled && !windowsKeyDown && virtualKey == kVirtualKeyReturn &&
           (modifiers & kModifierMask) == kControl;
}

// Ctrl+1..9 commits the translation of the candidate at that position. Numpad digits count too
// (the Server normalizes them); Shift+digit is deliberately not a chord.
inline bool IsTranslationDigitChord(bool translationsEnabled, unsigned virtualKey, unsigned modifiers,
                                    bool windowsKeyDown)
{
    const bool digit = (virtualKey >= L'1' && virtualKey <= L'9') || (virtualKey >= 0x61 && virtualKey <= 0x69);
    return translationsEnabled && !windowsKeyDown && digit && (modifiers & kModifierMask) == kControl;
}

// [keybindings] commit_english: commit the candidate's English line. It only yields Ctrl+Enter
// to the translation chords when those are on, so one chord never means two things.
inline bool IsEnglishCommitChord(bool translationsEnabled, bool matchesConfiguredBinding, bool windowsKeyDown)
{
    return !translationsEnabled && !windowsKeyDown && matchesConfiguredBinding;
}

// [keybindings] english_senses: expand the highlighted candidate's senses. It does not collide
// with the translation chords (Ctrl+Shift+Enter vs Ctrl+Enter), so it applies either way.
inline bool IsEnglishSensesChord(bool matchesConfiguredBinding, bool windowsKeyDown)
{
    return !windowsKeyDown && matchesConfiguredBinding;
}

struct ServerCandidateShortcut
{
    bool freshCompositionState = false;
    bool candidatesActive = false;
    bool translationCommitChord = false;
    bool translationDigitChord = false;
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
        shortcut.candidatesActive && (shortcut.translationCommitChord || shortcut.translationDigitChord ||
                                      shortcut.englishCommitChord || shortcut.englishSensesChord);
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
