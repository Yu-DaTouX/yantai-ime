#pragma once

#include <cstdint>

#include "input_key_policy.h"

namespace FanyImeIpc
{
// Which candidate branch owns a key before the ordinary selection / composition path.
//
// The TSF side makes the same decision in windows/src/Key/KeyRoutingPolicy.h; the two have to agree or
// the chord reaches neither branch.
enum class CandidateKeyRoute
{
    // Not a candidate chord: digits page-select, Enter commits, and so on.
    Ordinary,
    // [keybindings] commit_english: commit the candidate's English line.
    EnglishLine,
    // [keybindings] english_senses: expand the highlighted candidate's senses.
    EnglishSenses,
};

constexpr CandidateKeyRoute RouteCandidateKey(bool commit_english_matches, bool english_senses_matches)
{
    if (commit_english_matches)
        return CandidateKeyRoute::EnglishLine;
    if (english_senses_matches)
        return CandidateKeyRoute::EnglishSenses;
    return CandidateKeyRoute::Ordinary;
}
} // namespace FanyImeIpc
