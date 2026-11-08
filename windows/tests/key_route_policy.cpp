// Who owns a chord before the local classifier: the Server's candidate list or the application.
// Covers the English-line chords, which the Server's branch has to agree with
// (server/src/ipc/candidate_key_route_policy.h).

#include "Key/KeyRoutingPolicy.h"

#include <cstdio>

namespace
{
using namespace key_route_policy;

int failures = 0;

void Check(const char *what, bool actual, bool expected)
{
    if (actual != expected)
    {
        std::fprintf(stderr, "FAIL %s: expected=%d actual=%d\n", what, expected, actual);
        ++failures;
    }
}

void CheckDisposition(const char *what, ModifiedKeyDisposition actual, ModifiedKeyDisposition expected)
{
    if (actual != expected)
    {
        std::fprintf(stderr, "FAIL %s: expected=%d actual=%d\n", what, static_cast<int>(expected),
                     static_cast<int>(actual));
        ++failures;
    }
}
} // namespace

int main()
{
    // The English-line chords follow their configured binding unless a Windows key is held.
    Check("commit_english bound", IsEnglishCommitChord(true, false), true);
    Check("commit_english not bound", IsEnglishCommitChord(false, false), false);
    Check("commit_english with win held", IsEnglishCommitChord(true, true), false);
    Check("english_senses bound", IsEnglishSensesChord(true, false), true);
    Check("english_senses not bound", IsEnglishSensesChord(false, false), false);
    Check("english_senses with win held", IsEnglishSensesChord(true, true), false);

    // A chord reaches the Server only while the candidate list is up; a fresh composition state
    // belongs to the replacement session and never claims the old one's chords.
    {
        ServerCandidateShortcut shortcut;
        shortcut.candidatesActive = true;
        shortcut.englishCommitChord = true;
        Check("commit_english with candidates goes to the server", ShouldClaimForServerCandidate(shortcut), true);
        shortcut.englishCommitChord = false;
        shortcut.englishSensesChord = true;
        Check("english_senses with candidates goes to the server", ShouldClaimForServerCandidate(shortcut), true);
        shortcut.englishSensesChord = false;
        Check("no chord is not claimed", ShouldClaimForServerCandidate(shortcut), false);
    }
    {
        ServerCandidateShortcut shortcut;
        shortcut.englishCommitChord = true;
        Check("commit_english without candidates stays local", ShouldClaimForServerCandidate(shortcut), false);
        shortcut.freshCompositionState = true;
        shortcut.candidatesActive = true;
        shortcut.englishSensesChord = true;
        Check("fresh composition state ignores candidate chords", ShouldClaimForServerCandidate(shortcut), false);
    }
    {
        ServerCandidateShortcut shortcut;
        shortcut.pinyinCommitChord = true;
        Check("shift+enter without a spelling stays local", ShouldClaimForServerCandidate(shortcut), false);
        shortcut.pinyinCommitEligible = true;
        Check("shift+enter with a spelling goes to the server", ShouldClaimForServerCandidate(shortcut), true);
        shortcut.freshCompositionState = true;
        Check("shift+enter in a fresh composition stays local", ShouldClaimForServerCandidate(shortcut), false);
    }
    Check("pinyin commit chord on", IsPinyinCommitChord(true, kVirtualKeyReturn, kShift, false), true);
    Check("pinyin commit chord off", IsPinyinCommitChord(false, kVirtualKeyReturn, kShift, false), false);
    Check("pinyin commit chord is not plain enter", IsPinyinCommitChord(true, kVirtualKeyReturn, 0, false), false);

    // Ctrl and Alt combinations belong to the application; the only exception is the composition's
    // segment editing, and only while a composition exists. Shift is not a gated chord.
    CheckDisposition("ctrl+a", RouteModifiedKey(kControl, false, false), ModifiedKeyDisposition::Application);
    CheckDisposition("alt+f", RouteModifiedKey(kAlt, false, false), ModifiedKeyDisposition::Application);
    CheckDisposition("ctrl+alt+x", RouteModifiedKey(kControl | kAlt, false, false),
                     ModifiedKeyDisposition::Application);
    CheckDisposition("win+space", RouteModifiedKey(0, true, false), ModifiedKeyDisposition::Application);
    CheckDisposition("ctrl+enter", RouteModifiedKey(kControl, false, false), ModifiedKeyDisposition::Application);
    CheckDisposition("plain letter", RouteModifiedKey(0, false, false), ModifiedKeyDisposition::ClassifyLocally);
    CheckDisposition("shift+letter", RouteModifiedKey(kShift, false, false), ModifiedKeyDisposition::ClassifyLocally);
    CheckDisposition("ctrl+backspace in a composition", RouteModifiedKey(kControl, false, true),
                     ModifiedKeyDisposition::SegmentEdit);
    CheckDisposition("ctrl+backspace outside a composition", RouteModifiedKey(kControl, false, false),
                     ModifiedKeyDisposition::Application);

    if (failures != 0)
    {
        return 1;
    }
    std::puts("PASS: candidate chords and Ctrl/Alt combinations go to the right layer");
    return 0;
}
