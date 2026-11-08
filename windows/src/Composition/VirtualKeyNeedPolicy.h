#pragma once

// The classification half of CCompositionProcessorEngine::IsVirtualKeyNeed. Kept free of COM,
// TSF and of the engine's members: the caller passes the results of the engine's keystroke
// tables, its wildcard/caret state and the two contracts (engine/contracts/date_time_input.h,
// v_mode_input.h), and gets back the same answer the engine always returned.
//
// This is the code 674e405d deleted by accident while removing double pinyin: the tail after the
// double-pinyin branches decides whether a letter, a digit, Space, Enter, Backspace, Esc, a paging
// key or an arrow belongs to the IME. TSF hands every key it does not claim back to the host, so
// losing them silently turns this into a keyboard that cannot type at all. Behaviourizing the
// decision here lets windows/tests catch that class of deletion without a TSF host.

#include "MetasequoiaIMEBaseStructure.h"

namespace virtual_key_need_policy
{

// One engine keystroke table lookup. `function` is the function the matching table entry carries.
struct CompositionKeystroke
{
    bool matched = false;
    KEYSTROKE_FUNCTION function = FUNCTION_NONE;
};

struct Context
{
    unsigned virtualKey = 0;
    wchar_t character = L'\0';

    // fComposing after the CANDIDATE_ORIGINAL adjustment (that mode classifies as not composing).
    bool composing = false;
    CANDIDATE_MODE candidateMode = CANDIDATE_NONE;
    bool hasCandidateWithWildcard = false;

    // Engine state the classifier reads.
    DWORD_PTR keystrokeBufferLength = 0;
    bool wildcardEnabled = false;
    bool disableWildcardAtFirst = false;
    bool wildcardInKeystrokeBuffer = false;
    bool usesHorizontalArrowSelection = false;
    bool shiftDown = false;
    // Global::PureShiftKeyUp: a lone shift release is swallowed while composing.
    bool pureShiftKeyUp = false;

    // Results of the engine's own predicates, evaluated before the call.
    bool dateTimeInputKey = false;
    bool vModeInputKey = false;
    bool vModeShiftDigitSelectionKey = false;
    // The character is listed in Global::CommitWithHighlightedCandPunc; the paging exclusions
    // stay in the policy below.
    bool characterCommitsHighlightedCandidate = false;
    // IsVirtualKeyKeystrokeComposition(..., FUNCTION_NONE) on _KeystrokeComposition.
    CompositionKeystroke compositionKeystroke;
    // IsVirtualKeyKeystrokeComposition(..., FUNCTION_INPUT).
    bool compositionInputKeystroke = false;
    // IsVirtualKeyKeystrokeCandidate(...) on _KeystrokeCandidate / _KeystrokeCandidateWildcard.
    CompositionKeystroke candidateKeystroke;
    CompositionKeystroke candidateWildcardKeystroke;
    // _candidateListIndexRange.IsRange(uCode).
    bool virtualKeyInCandidateRange = false;
};

inline void SetState(_Out_opt_ _KEYSTROKE_STATE *pKeyState, KEYSTROKE_CATEGORY category, KEYSTROKE_FUNCTION function)
{
    if (pKeyState)
    {
        pKeyState->Category = category;
        pKeyState->Function = function;
    }
}

// '?' matches one character, '*' any run (Global::IsWildcardChar).
inline bool IsWildcardCharacter(wchar_t character)
{
    return character == L'?' || character == L'*';
}

// An apostrophe between syllables splits a pinyin spelling. Only meaningful while a spelling is
// already on screen: with nothing typed the key stays the host's.
inline bool IsManualPinyinSeparator(wchar_t character, bool composing, CANDIDATE_MODE candidateMode,
                                    DWORD_PTR keystrokeLength)
{
    if (character != L'\'')
    {
        return false;
    }
    if (keystrokeLength == 0)
    {
        return false;
    }
    return composing || candidateMode != CANDIDATE_NONE;
}

// A configured punctuation character committed together with the highlighted candidate. Candidate
// paging keys keep their navigation meaning even when the same character is configured.
inline bool CommitsHighlightedCandidatePunctuation(unsigned virtualKey, bool characterCommits,
                                                   CANDIDATE_MODE candidateMode)
{
    if (candidateMode == CANDIDATE_NONE)
    {
        return false;
    }

    switch (virtualKey)
    {
    case VK_PRIOR:
    case VK_NEXT:
    case VK_HOME:
    case VK_END:
    case VK_TAB:
        return false;
    case VK_OEM_MINUS:
    case VK_OEM_PLUS:
        return false;
    default:
        break;
    }

    return characterCommits;
}

// Mirrors CCompositionProcessorEngine::IsKeystrokeRange: a digit inside the candidate index range
// selects only while candidates or a spelling exist.
inline bool IsKeystrokeRange(const Context &context)
{
    return context.virtualKeyInCandidateRange &&
           (context.candidateMode != CANDIDATE_NONE || context.keystrokeBufferLength > 0);
}

inline BOOL Classify(const Context &context, _Out_opt_ _KEYSTROKE_STATE *pKeyState)
{
    if (pKeyState)
    {
        pKeyState->Category = CATEGORY_NONE;
        pKeyState->Function = FUNCTION_NONE;
    }

    if (IsManualPinyinSeparator(context.character, context.composing, context.candidateMode,
                                context.keystrokeBufferLength))
    {
        SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_INPUT);
        return TRUE;
    }

    // T-mode: digits, '/' and ':' that still extend a date/time are input. The rest keep their usual
    // meaning below (digits select, '/' and ':' are punctuation); Shift+digit never matches here.
    // V-mode: digits and . + - * / ( ) after the V prefix are input, so they neither select, page nor commit
    // punctuation. Shift+8/9/0 give * ( ) and are input too; other Shift+digits still select.
    if (context.dateTimeInputKey || context.vModeInputKey)
    {
        SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_INPUT);
        return TRUE;
    }
    // V-mode: bare digits are input, so Shift+1..9 selects. It must be decided here, before the
    // punctuation rule below would commit the highlighted candidate together with '!' '@' ... .
    if (context.vModeShiftDigitSelectionKey)
    {
        SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_SELECT_BY_NUMBER);
        return TRUE;
    }

    // The Server owns the configurable comma/period behavior. Always route these keys through it while
    // candidates are active; its response decides whether the key navigates or commits the highlighted
    // candidate with punctuation.
    const bool isCommaPeriodPagingKey = context.virtualKey == VK_OEM_COMMA || context.virtualKey == VK_OEM_PERIOD;
    const bool isBracketPagingKey = context.virtualKey == VK_OEM_4 || context.virtualKey == VK_OEM_6;
    const bool isMinusEqualPagingKey = context.virtualKey == VK_OEM_MINUS || context.virtualKey == VK_OEM_PLUS;
    if (context.candidateMode != CANDIDATE_NONE &&
        (isMinusEqualPagingKey || isCommaPeriodPagingKey || isBracketPagingKey || context.virtualKey == VK_TAB ||
         context.virtualKey == VK_PRIOR || context.virtualKey == VK_NEXT || context.virtualKey == VK_UP ||
         context.virtualKey == VK_DOWN))
    {
        SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_SERVER_CANDIDATE_KEY);
        return TRUE;
    }

    if (context.candidateMode != CANDIDATE_NONE && (context.virtualKey == VK_LEFT || context.virtualKey == VK_RIGHT))
    {
        // 横排候选左右键选词开着时，移高亮还是移光标由 Server 定，TSF 只应用它的回复。
        const bool moveLeft = context.virtualKey == VK_LEFT;
        SetState(pKeyState, CATEGORY_COMPOSING,
                 context.usesHorizontalArrowSelection
                     ? (moveLeft ? FUNCTION_MOVE_LEFT_CANDIDATE : FUNCTION_MOVE_RIGHT_CANDIDATE)
                     : (moveLeft ? FUNCTION_MOVE_LEFT : FUNCTION_MOVE_RIGHT));
        return TRUE;
    }

    if (CommitsHighlightedCandidatePunctuation(context.virtualKey, context.characterCommitsHighlightedCandidate,
                                               context.candidateMode))
    {
        SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_PUNCTUATION);
        return TRUE;
    }

    if (context.composing || context.candidateMode == CANDIDATE_INCREMENTAL || context.candidateMode == CANDIDATE_NONE)
    {
        if (context.compositionKeystroke.matched) // 26 basic English chars
        {
            SetState(pKeyState, CATEGORY_COMPOSING, context.compositionKeystroke.function);
            return TRUE;
        }
        else if ((context.wildcardEnabled && IsWildcardCharacter(context.character) &&
                  !context.disableWildcardAtFirst) ||
                 (context.wildcardEnabled && IsWildcardCharacter(context.character) && context.disableWildcardAtFirst &&
                  context.keystrokeBufferLength))
        {
            SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_INPUT);
            return TRUE;
        }
        else if (context.wildcardInKeystrokeBuffer && context.virtualKey == VK_SPACE)
        {
            SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_CONVERT_WILDCARD);
            return TRUE;
        }
        if (context.pureShiftKeyUp)
        {
            return TRUE;
        }
    }

    if (context.candidateMode == CANDIDATE_ORIGINAL)
    {
        if (context.candidateKeystroke.matched)
        {
            SetState(pKeyState, CATEGORY_CANDIDATE, context.candidateKeystroke.function);
            return TRUE;
        }

        if (context.hasCandidateWithWildcard && context.candidateWildcardKeystroke.matched)
        {
            SetState(pKeyState, CATEGORY_CANDIDATE, context.candidateWildcardKeystroke.function);
            return TRUE;
        }

        // Candidate list could not handle key. We can try to restart the composition.
        if (context.compositionInputKeystroke)
        {
            SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_FINALIZE_CANDIDATELIST_AND_INPUT);
            return TRUE;
        }
    }

    // CANDIDATE_INCREMENTAL should process Keystroke.Candidate virtual keys.
    else if (context.candidateMode == CANDIDATE_INCREMENTAL)
    {
        if (context.candidateKeystroke.matched)
        {
            SetState(pKeyState, CATEGORY_CANDIDATE, context.candidateKeystroke.function);
            return TRUE;
        }
    }

    if (!context.composing && context.candidateMode != CANDIDATE_ORIGINAL)
    {
        if (context.compositionInputKeystroke)
        {
            SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_INPUT);
            return TRUE;
        }
    }

    // System pre-defined keystroke
    if (context.composing)
    {
        if (context.candidateMode != CANDIDATE_INCREMENTAL)
        {
            switch (context.virtualKey)
            {
            case VK_LEFT:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_MOVE_LEFT);
                return TRUE;
            case VK_RIGHT:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_MOVE_RIGHT);
                return TRUE;
            case VK_RETURN:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_FINALIZE_CANDIDATELIST);
                return TRUE;
            case VK_ESCAPE:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_CANCEL);
                return TRUE;
            case VK_BACK:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_BACKSPACE);
                return TRUE;
            case VK_DELETE:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_DELETE);
                return TRUE;

            case VK_UP:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_MOVE_UP);
                return TRUE;
            case VK_DOWN:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_MOVE_DOWN);
                return TRUE;
            case VK_PRIOR:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_MOVE_PAGE_UP);
                return TRUE;
            case VK_OEM_MINUS:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_MOVE_PAGE_UP);
                return TRUE;
            case VK_NEXT:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_MOVE_PAGE_DOWN);
                return TRUE;
            case VK_OEM_PLUS:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_MOVE_PAGE_DOWN);
                return TRUE;
            case VK_TAB:
                SetState(pKeyState, CATEGORY_COMPOSING,
                         context.shiftDown ? FUNCTION_MOVE_PAGE_UP : FUNCTION_MOVE_PAGE_DOWN);
                return TRUE;

            case VK_HOME:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_MOVE_PAGE_TOP);
                return TRUE;
            case VK_END:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_MOVE_PAGE_BOTTOM);
                return TRUE;

            case VK_SPACE:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_CONVERT);
                return TRUE;
            }
        }
        else if (context.candidateMode == CANDIDATE_INCREMENTAL)
        {
            switch (context.virtualKey)
            {
                // VK_LEFT, VK_RIGHT - set *pIsEaten = FALSE for application could move caret left or right.
                // and for CUAS, invoke _HandleCompositionCancel() edit session due to ignore CUAS default key handler
                // for send out terminate composition
            case VK_LEFT:
            case VK_RIGHT:
                SetState(pKeyState, CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION, FUNCTION_CANCEL);
                return FALSE;

            case VK_RETURN:
                // Do something when user press return key
                SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_FINALIZE_CANDIDATELISTForVKReturn);
                return TRUE;
            case VK_ESCAPE:
                SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_CANCEL);
                return TRUE;

                // VK_BACK - remove one char from reading string.
            case VK_BACK:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_BACKSPACE);
                return TRUE;
            case VK_DELETE:
                SetState(pKeyState, CATEGORY_COMPOSING, FUNCTION_DELETE);
                return TRUE;

            case VK_UP:
                SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_UP);
                return TRUE;
            case VK_DOWN:
                SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_DOWN);
                return TRUE;
            case VK_PRIOR:
                SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_PAGE_UP);
                return TRUE;
            case VK_OEM_MINUS:
                SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_PAGE_UP);
                return TRUE;
            case VK_NEXT:
                SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_PAGE_DOWN);
                return TRUE;
            case VK_OEM_PLUS:
                SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_PAGE_DOWN);
                return TRUE;
            case VK_TAB:
                SetState(pKeyState, CATEGORY_COMPOSING,
                         context.shiftDown ? FUNCTION_MOVE_PAGE_UP : FUNCTION_MOVE_PAGE_DOWN);
                return TRUE;
            case VK_HOME:
                SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_PAGE_TOP);
                return TRUE;
            case VK_END:
                SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_PAGE_BOTTOM);
                return TRUE;

            case VK_SPACE:
                SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_CONVERT);
                return TRUE;
            }
        }
    }

    if (context.candidateMode == CANDIDATE_ORIGINAL)
    {
        switch (context.virtualKey)
        {
        case VK_UP:
            SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_UP);
            return TRUE;
        case VK_DOWN:
            SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_DOWN);
            return TRUE;
        case VK_PRIOR:
            SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_PAGE_UP);
            return TRUE;
        case VK_OEM_MINUS:
            SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_PAGE_UP);
            return TRUE;
        case VK_NEXT:
            SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_PAGE_DOWN);
            return TRUE;
        case VK_OEM_PLUS:
            SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_PAGE_DOWN);
            return TRUE;
        case VK_TAB:
            SetState(pKeyState, CATEGORY_COMPOSING,
                     context.shiftDown ? FUNCTION_MOVE_PAGE_UP : FUNCTION_MOVE_PAGE_DOWN);
            return TRUE;
        case VK_HOME:
            SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_PAGE_TOP);
            return TRUE;
        case VK_END:
            SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_MOVE_PAGE_BOTTOM);
            return TRUE;
        case VK_RETURN:
            SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_FINALIZE_CANDIDATELIST);
            return TRUE;
        case VK_SPACE:
            SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_CONVERT);
            return TRUE;
        case VK_BACK:
        case VK_DELETE:
            SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_CANCEL);
            return TRUE;

        case VK_ESCAPE:
            SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_CANCEL);
            return TRUE;
        }
    }

    //
    // Check whether the keystroke is number(for selecting candidate) and is in the range
    //
    if (IsKeystrokeRange(context))
    {
        SetState(pKeyState, CATEGORY_CANDIDATE, FUNCTION_SELECT_BY_NUMBER);
        return TRUE;
    }
    else if (pKeyState && pKeyState->Category != CATEGORY_NONE)
    {
        return FALSE;
    }

    if (context.character && !context.compositionKeystroke.matched)
    {
        SetState(pKeyState, CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION, FUNCTION_FINALIZE_TEXTSTORE);
        return FALSE;
    }

    return FALSE;
}

} // namespace virtual_key_need_policy
