#pragma once

#include <cstdint>

#include "candidate_translation_policy.h"
#include "input_key_policy.h"

namespace FanyImeIpc
{
// Which candidate branch owns a key before the ordinary selection / composition path. The order is
// the whole point: IsTranslationCommitKey does not look at the translation switch, so as long as it
// runs first it swallows Ctrl+Enter whenever candidate translations are off, and the English-line
// branch (commit_english, default Ctrl+Enter) can never fire. Everything lands on the same chord, so
// the switch is what decides which meaning it has.
//
// The TSF side runs the same order in windows/src/Key/KeyRoutingPolicy.h; the two have to agree or
// Ctrl+Enter reaches neither branch.
enum class CandidateKeyRoute
{
    // Not a candidate chord: digits page-select, Enter commits, and so on.
    Ordinary,
    // [keybindings] commit_english: commit the candidate's English line.
    EnglishLine,
    // [keybindings] english_senses: expand the highlighted candidate's senses.
    EnglishSenses,
    // Ctrl+Enter: commit the highlighted candidate's translation.
    TranslationCommit,
    // Ctrl+1..9: commit the translation of the candidate at that position.
    TranslationDigit,
};

// `translations_enabled` is general.candidate_translations. The English line yields Ctrl+Enter to
// the translation chords while they are on (one chord must not mean two things); english_senses
// sits on a different chord, so it applies in both cases.
constexpr CandidateKeyRoute RouteCandidateKey(uint32_t keycode, uint32_t modifiers_down, bool translations_enabled,
                                              bool commit_english_matches, bool english_senses_matches)
{
    if (!translations_enabled && commit_english_matches)
        return CandidateKeyRoute::EnglishLine;
    if (english_senses_matches)
        return CandidateKeyRoute::EnglishSenses;
    if (IsTranslationCommitKey(keycode, modifiers_down))
        return CandidateKeyRoute::TranslationCommit;
    if (IsTranslationDigitKey(keycode, modifiers_down))
        return CandidateKeyRoute::TranslationDigit;
    return CandidateKeyRoute::Ordinary;
}
} // namespace FanyImeIpc
