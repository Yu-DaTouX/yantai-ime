// Virtual-key classification: IsVirtualKeyNeed and IsVirtualKeyNeedForFreshComposition, with
// the keystroke, candidate and number-range tests they use.

#include "Private.h"
#include "MetasequoiaIME.h"
#include "CompositionProcessorEngine.h"
#include "VirtualKeyNeedPolicy.h"
#include "ModifierState.h"
#include "TfInputProcessorProfile.h"
#include "Globals.h"
#include "FanyDefines.h"
#include "Compartment.h"
#include "LanguageBar.h"
#include "RegKey.h"
#include "define.h"
#include <msctf.h>
#include <string>
#include <fmt/xchar.h>
#include "Ipc.h"
#include "FanyUtils.h"
#include "FanyLog.h"
#include "EditSession.h"
#include "TfTextLayoutSink.h"
#include "../../../engine/contracts/date_time_input.h"
#include <new>

//////////////////////////////////////////////////////////////////////
//
//    CCompositionProcessorEngine
//
//////////////////////////////////////////////////////////////////////

BOOL CCompositionProcessorEngine::IsVirtualKeyNeedForFreshComposition(UINT uCode, _In_reads_(1) WCHAR *pwch,
                                                                      _Out_opt_ _KEYSTROKE_STATE *pKeyState)
{
    if (pKeyState)
    {
        pKeyState->Category = CATEGORY_NONE;
        pKeyState->Function = FUNCTION_NONE;
    }

    // Classify against an actually empty composition. This path is used while
    // the old focus session is still being cancelled, so none of its candidate
    // mode, wildcard flags, or virtual-key buffer may affect the first key in
    // the replacement session.
    if (virtual_key_need_policy::IsManualPinyinSeparator(pwch ? *pwch : 0, FALSE, CANDIDATE_NONE, 0))
    {
        if (pKeyState)
        {
            pKeyState->Category = CATEGORY_COMPOSING;
            pKeyState->Function = FUNCTION_INPUT;
        }
        return TRUE;
    }
    if (IsVirtualKeyKeystrokeComposition(uCode, pKeyState, FUNCTION_INPUT))
    {
        return TRUE;
    }
    if (pwch && IsWildcard() && IsWildcardChar(*pwch) && !IsDisableWildcardAtFirst())
    {
        if (pKeyState)
        {
            pKeyState->Category = CATEGORY_COMPOSING;
            pKeyState->Function = FUNCTION_INPUT;
        }
        return TRUE;
    }
    return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyNeed
//
// Test virtual key code need to the Composition Processor Engine.
// param
//     [in] uCode - Specify virtual key code.
//     [in/out] pwch       - char code
//     [in] fComposing     - Specified composing.
//     [in] fCandidateMode - Specified candidate mode.
//     [out] pKeyState     - Returns function regarding virtual key.
// returns
//     If engine need this virtual key code, returns true. Otherwise returns false.
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyNeed( //
    UINT uCode,                                     //
    _In_reads_(1) WCHAR *pwch,                      //
    BOOL fComposing,                                //
    CANDIDATE_MODE candidateMode,                   //
    BOOL hasCandidateWithWildcard,                  //
    _Out_opt_ _KEYSTROKE_STATE *pKeyState           //
)
{
    // The decision itself lives in VirtualKeyNeedPolicy.h, free of COM/TSF, so windows/tests can
    // drive it. Everything below only gathers what that policy needs from this engine.
    if (candidateMode == CANDIDATE_ORIGINAL)
    {
        fComposing = FALSE;
    }

    const WCHAR character = pwch ? *pwch : 0;
    const WCHAR *buffer = _keystrokeBuffer.Get();
    const DWORD_PTR bufferLength = _keystrokeBuffer.GetLength();

    virtual_key_need_policy::Context context;
    context.virtualKey = uCode;
    context.character = character;
    context.composing = fComposing != FALSE;
    context.candidateMode = candidateMode;
    context.hasCandidateWithWildcard = hasCandidateWithWildcard != FALSE;
    context.keystrokeBufferLength = bufferLength;
    context.wildcardEnabled = IsWildcard() != FALSE;
    context.disableWildcardAtFirst = IsDisableWildcardAtFirst() != FALSE;
    context.wildcardInKeystrokeBuffer = _hasWildcardIncludedInKeystrokeBuffer != FALSE;
    context.usesHorizontalArrowSelection = UsesHorizontalArrowSelection();
    context.shiftDown = IsShiftDownForCurrentMessage();
    context.pureShiftKeyUp = Global::PureShiftKeyUp != FALSE;
    context.dateTimeInputKey = IsDateTimeInputKey(uCode, character, buffer, bufferLength, _caretPosition) != FALSE;
    context.vModeInputKey = IsVModeInputKey(character, buffer, bufferLength, _caretPosition) != FALSE;
    context.vModeShiftDigitSelectionKey = IsVModeShiftDigitSelectionKey(uCode, buffer, bufferLength) != FALSE;
    context.characterCommitsHighlightedCandidate = Global::CommitWithHighlightedCandPunc.count(character) > 0;
    context.virtualKeyInCandidateRange = _candidateListIndexRange.IsRange(uCode) != FALSE;

    // A scratch state keeps the table lookups side-effect free for the policy: they only ever
    // report whether the key matched and with which function.
    _KEYSTROKE_STATE compositionState{CATEGORY_NONE, FUNCTION_NONE};
    if (IsVirtualKeyKeystrokeComposition(uCode, &compositionState, FUNCTION_NONE))
    {
        context.compositionKeystroke.matched = true;
        context.compositionKeystroke.function = compositionState.Function;
    }
    context.compositionInputKeystroke =
        IsVirtualKeyKeystrokeComposition(uCode, &compositionState, FUNCTION_INPUT) != FALSE;

    _KEYSTROKE_STATE candidateState{CATEGORY_NONE, FUNCTION_NONE};
    BOOL candidateRetCode = FALSE;
    if (IsVirtualKeyKeystrokeCandidate(uCode, &candidateState, candidateMode, &candidateRetCode, &_KeystrokeCandidate))
    {
        context.candidateKeystroke.matched = true;
        context.candidateKeystroke.function = candidateState.Function;
    }
    if (IsVirtualKeyKeystrokeCandidate(uCode, &candidateState, candidateMode, &candidateRetCode,
                                       &_KeystrokeCandidateWildcard))
    {
        context.candidateWildcardKeystroke.matched = true;
        context.candidateWildcardKeystroke.function = candidateState.Function;
    }

    return virtual_key_need_policy::Classify(context, pKeyState);
}

// Server 按同一条形状规则改输入串（engine/contracts/date_time_input.h）。看不到 T 模式开没开，只认开头的 T，
// Server 也一样不看，两边才不会分叉。小键盘数字也算：Server 收到时已经把它归一成主键盘数字。
bool CCompositionProcessorEngine::IsDateTimeInputKey(UINT uCode, WCHAR wch, const WCHAR *buffer, DWORD_PTR length,
                                                     DWORD_PTR caret)
{
    const bool digit_key = (uCode >= L'0' && uCode <= L'9') || (uCode >= VK_NUMPAD0 && uCode <= VK_NUMPAD9);
    const bool digit = digit_key && wch >= L'0' && wch <= L'9';
    const bool slash = uCode == VK_OEM_2 && wch == L'/';
    const bool colon = uCode == VK_OEM_1 && wch == L':';
    return (digit || slash || colon) && buffer != nullptr &&
           FanyImeDateTimeInput::AcceptsAt(buffer, static_cast<std::size_t>(length),
                                           static_cast<std::size_t>(min(caret, length)), wch);
}

// 只看字符不看键位：主键盘和小键盘的数字、运算符一样收，Server 也只看字符。前缀认哪个由 Server 的
// VModeChanged 决定（全拼 V、v 都认）。
bool CCompositionProcessorEngine::IsVModeShiftDigitSelectionKey(UINT uCode, const WCHAR *buffer, DWORD_PTR length)
{
    const bool shift_only =
        IsShiftDownForCurrentMessage() && !IsControlDownForCurrentMessage() && !IsAltDownForCurrentMessage();
    return shift_only && uCode >= L'1' && uCode <= L'9' &&
           FanyImeVModeInput::IsComposition(buffer, static_cast<std::size_t>(length),
                                            Global::VModeTrigger.load(std::memory_order_relaxed));
}

bool CCompositionProcessorEngine::IsVModeInputKey(WCHAR wch, const WCHAR *buffer, DWORD_PTR length, DWORD_PTR caret)
{
    return buffer != nullptr && FanyImeVModeInput::AcceptsAt(buffer, static_cast<std::size_t>(length),
                                                             static_cast<std::size_t>(min(caret, length)), wch,
                                                             Global::VModeTrigger.load(std::memory_order_relaxed));
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyKeystrokeComposition( //
    UINT uCode,                                                     //
    _Out_opt_ _KEYSTROKE_STATE *pKeyState,                          //
    KEYSTROKE_FUNCTION function                                     //
)
{
    if (pKeyState == nullptr)
    {
        return FALSE;
    }

    pKeyState->Category = CATEGORY_NONE;
    pKeyState->Function = FUNCTION_NONE;

    // 26 basic English characters
    for (UINT i = 0; i < _KeystrokeComposition.Count(); i++)
    {
        _KEYSTROKE *pKeystroke = nullptr;

        pKeystroke = _KeystrokeComposition.GetAt(i);

        if ((pKeystroke->VirtualKey == uCode) &&
            (Global::ModifiersValue == 36 || Global::ModifiersValue == 260 || Global::ModifiersValue == 292 ||
             Global::CheckModifiers(Global::ModifiersValue, pKeystroke->Modifiers)))
        {
            if (function == FUNCTION_NONE)
            {
                pKeyState->Category = CATEGORY_COMPOSING;
                pKeyState->Function = pKeystroke->Function;
                return TRUE;
            }
            else if (function == pKeystroke->Function)
            {
                pKeyState->Category = CATEGORY_COMPOSING;
                pKeyState->Function = pKeystroke->Function;
                return TRUE;
            }
        }
    }

    return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsVirtualKeyKeystrokeCandidate
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsVirtualKeyKeystrokeCandidate(
    UINT uCode, _In_ _KEYSTROKE_STATE *pKeyState, CANDIDATE_MODE /*candidateMode*/, _Out_ BOOL *pfRetCode,
    _In_ CMetasequoiaImeArray<_KEYSTROKE> *pKeystrokeMetric)
{
    if (pfRetCode == nullptr)
    {
        return FALSE;
    }
    *pfRetCode = FALSE;

    for (UINT i = 0; i < pKeystrokeMetric->Count(); i++)
    {
        _KEYSTROKE *pKeystroke = nullptr;

        pKeystroke = pKeystrokeMetric->GetAt(i);

        if ((pKeystroke->VirtualKey == uCode) && Global::CheckModifiers(Global::ModifiersValue, pKeystroke->Modifiers))
        {
            *pfRetCode = TRUE;
            if (pKeyState)
            {
                pKeyState->Category = CATEGORY_CANDIDATE;

                pKeyState->Function = pKeystroke->Function;
            }
            return TRUE;
        }
    }

    return FALSE;
}

//+---------------------------------------------------------------------------
//
// CCompositionProcessorEngine::IsKeyKeystrokeRange
//
//----------------------------------------------------------------------------

BOOL CCompositionProcessorEngine::IsKeystrokeRange(UINT uCode, _Out_ _KEYSTROKE_STATE *pKeyState,
                                                   CANDIDATE_MODE candidateMode)
{
    if (pKeyState == nullptr)
    {
        return FALSE;
    }

    pKeyState->Category = CATEGORY_NONE;
    pKeyState->Function = FUNCTION_NONE;

    if (_candidateListIndexRange.IsRange(uCode))
    {
        if (candidateMode != CANDIDATE_NONE)
        {
            pKeyState->Category = CATEGORY_CANDIDATE;
            pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
            return TRUE;
        }
        else if (GetVirtualKeyLength() > 0)
        {
            pKeyState->Category = CATEGORY_CANDIDATE;
            pKeyState->Function = FUNCTION_SELECT_BY_NUMBER;
            return TRUE;
        }
    }
    return FALSE;
}
