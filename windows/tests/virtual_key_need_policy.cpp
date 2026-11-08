// The full-pinyin key chain, without a TSF host: VirtualKeyNeedPolicy.h is the decision half of
// CCompositionProcessorEngine::IsVirtualKeyNeed, so a deleted branch shows up here instead of as
// "the IME cannot type" in a real application (674e405d removed the whole tail by accident).

#include "Composition/VirtualKeyNeedPolicy.h"

#include <cstdio>

namespace
{
int failures = 0;

virtual_key_need_policy::Context BaseContext()
{
    virtual_key_need_policy::Context context;
    context.candidateMode = CANDIDATE_NONE;
    return context;
}

virtual_key_need_policy::Context LetterContext(unsigned virtualKey, wchar_t character)
{
    virtual_key_need_policy::Context context = BaseContext();
    context.virtualKey = virtualKey;
    context.character = character;
    // _KeystrokeComposition holds A-Z with FUNCTION_INPUT (CompositionProcessorEngine::SetKeystrokeTable).
    context.compositionKeystroke = {true, FUNCTION_INPUT};
    context.compositionInputKeystroke = true;
    return context;
}

void Expect(const char *what, const virtual_key_need_policy::Context &context, BOOL expectedEaten,
            KEYSTROKE_CATEGORY expectedCategory, KEYSTROKE_FUNCTION expectedFunction)
{
    _KEYSTROKE_STATE state{CATEGORY_NONE, FUNCTION_NONE};
    const BOOL eaten = virtual_key_need_policy::Classify(context, &state);
    if (eaten != expectedEaten || state.Category != expectedCategory || state.Function != expectedFunction)
    {
        std::fprintf(stderr, "FAIL %s: eaten=%d expected=%d category=%d expected=%d function=%d expected=%d\n", what,
                     eaten, expectedEaten, state.Category, expectedCategory, state.Function, expectedFunction);
        ++failures;
    }
}
} // namespace

int main()
{
    // Chinese mode, nothing typed yet: every letter is input and TSF must keep the key. This is
    // the regression that made the IME unusable: the letter matched the composition keystroke
    // table, but the code that turned that match into "eat it" had been deleted.
    for (unsigned key = 'A'; key <= 'Z'; ++key)
    {
        const wchar_t character = static_cast<wchar_t>(key - 'A' + 'a');
        Expect("letter with no composition", LetterContext(key, character), TRUE, CATEGORY_COMPOSING, FUNCTION_INPUT);
    }

    // Same first key with a spelling already on screen but no composition object yet.
    {
        virtual_key_need_policy::Context context = LetterContext('N', L'n');
        context.keystrokeBufferLength = 2;
        Expect("letter after typed spelling", context, TRUE, CATEGORY_COMPOSING, FUNCTION_INPUT);
    }

    // Composing, no candidate list yet: the digits select from the candidate index range.
    for (unsigned key = '1'; key <= '9'; ++key)
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = key;
        context.character = static_cast<wchar_t>(key);
        context.composing = true;
        context.keystrokeBufferLength = 3;
        context.virtualKeyInCandidateRange = true;
        Expect("digit while composing", context, TRUE, CATEGORY_CANDIDATE, FUNCTION_SELECT_BY_NUMBER);
    }

    // Candidates on screen: Space commits the highlighted candidate, Enter finalizes the list,
    // Backspace / Esc cancel, paging and arrows are the Server's.
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = VK_SPACE;
        context.character = L' ';
        // IsVirtualKeyNeed clears fComposing in CANDIDATE_ORIGINAL: the candidate list, not the
        // spelling, is on screen.
        context.candidateMode = CANDIDATE_ORIGINAL;
        context.keystrokeBufferLength = 3;
        Expect("space with candidates", context, TRUE, CATEGORY_CANDIDATE, FUNCTION_CONVERT);
    }
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = VK_RETURN;
        context.character = L'\r';
        context.candidateMode = CANDIDATE_ORIGINAL;
        context.keystrokeBufferLength = 3;
        Expect("enter with candidates", context, TRUE, CATEGORY_CANDIDATE, FUNCTION_FINALIZE_CANDIDATELIST);
    }
    for (unsigned key : {static_cast<unsigned>(VK_BACK), static_cast<unsigned>(VK_ESCAPE)})
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = key;
        context.candidateMode = CANDIDATE_ORIGINAL;
        context.keystrokeBufferLength = 3;
        Expect("backspace/esc with candidates", context, TRUE, CATEGORY_CANDIDATE, FUNCTION_CANCEL);
    }
    for (unsigned key : {static_cast<unsigned>(VK_PRIOR), static_cast<unsigned>(VK_NEXT), static_cast<unsigned>(VK_UP),
                         static_cast<unsigned>(VK_DOWN), static_cast<unsigned>(VK_TAB),
                         static_cast<unsigned>(VK_OEM_MINUS), static_cast<unsigned>(VK_OEM_PLUS)})
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = key;
        context.candidateMode = CANDIDATE_ORIGINAL;
        context.keystrokeBufferLength = 3;
        Expect("paging key with candidates", context, TRUE, CATEGORY_CANDIDATE, FUNCTION_SERVER_CANDIDATE_KEY);
    }
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = '2';
        context.character = L'2';
        context.candidateMode = CANDIDATE_ORIGINAL;
        context.keystrokeBufferLength = 3;
        context.virtualKeyInCandidateRange = true;
        Expect("digit with candidates", context, TRUE, CATEGORY_CANDIDATE, FUNCTION_SELECT_BY_NUMBER);
    }

    // Composing without a candidate list: the same keys act locally.
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = VK_SPACE;
        context.character = L' ';
        context.composing = true;
        context.keystrokeBufferLength = 3;
        Expect("space while composing", context, TRUE, CATEGORY_COMPOSING, FUNCTION_CONVERT);
    }
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = VK_RETURN;
        context.character = L'\r';
        context.composing = true;
        context.keystrokeBufferLength = 3;
        Expect("enter while composing", context, TRUE, CATEGORY_COMPOSING, FUNCTION_FINALIZE_CANDIDATELIST);
    }
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = VK_BACK;
        context.composing = true;
        context.keystrokeBufferLength = 3;
        Expect("backspace while composing", context, TRUE, CATEGORY_COMPOSING, FUNCTION_BACKSPACE);
    }
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = VK_ESCAPE;
        context.composing = true;
        context.keystrokeBufferLength = 3;
        Expect("esc while composing", context, TRUE, CATEGORY_COMPOSING, FUNCTION_CANCEL);
    }
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = VK_NEXT;
        context.composing = true;
        context.keystrokeBufferLength = 3;
        Expect("page down while composing", context, TRUE, CATEGORY_COMPOSING, FUNCTION_MOVE_PAGE_DOWN);
    }
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = VK_PRIOR;
        context.composing = true;
        context.keystrokeBufferLength = 3;
        Expect("page up while composing", context, TRUE, CATEGORY_COMPOSING, FUNCTION_MOVE_PAGE_UP);
    }
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = VK_UP;
        context.keystrokeBufferLength = 3;
        context.composing = true;
        Expect("up while composing", context, TRUE, CATEGORY_COMPOSING, FUNCTION_MOVE_UP);
    }
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = VK_DOWN;
        context.keystrokeBufferLength = 3;
        context.composing = true;
        Expect("down while composing", context, TRUE, CATEGORY_COMPOSING, FUNCTION_MOVE_DOWN);
    }
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = VK_LEFT;
        context.composing = true;
        context.keystrokeBufferLength = 3;
        Expect("left while composing", context, TRUE, CATEGORY_COMPOSING, FUNCTION_MOVE_LEFT);
    }
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = VK_RIGHT;
        context.composing = true;
        context.keystrokeBufferLength = 3;
        Expect("right while composing", context, TRUE, CATEGORY_COMPOSING, FUNCTION_MOVE_RIGHT);
    }

    // With the horizontal candidate window the arrows move the highlight instead of the caret.
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = VK_LEFT;
        context.candidateMode = CANDIDATE_ORIGINAL;
        context.keystrokeBufferLength = 3;
        context.usesHorizontalArrowSelection = true;
        Expect("left with candidates, horizontal arrows", context, TRUE, CATEGORY_COMPOSING,
               FUNCTION_MOVE_LEFT_CANDIDATE);
    }

    // A key the classifier does not own is not eaten: TSF hands it back, spelling and all.
    {
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = VK_F5;
        context.composing = false;
        context.keystrokeBufferLength = 0;
        Expect("function key with no composition", context, FALSE, CATEGORY_NONE, FUNCTION_NONE);
    }
    {
        // A non-letter character outside a composition still finalizes anything left in the text
        // store, but the key is not eaten.
        virtual_key_need_policy::Context context = BaseContext();
        context.virtualKey = '5';
        context.character = L'5';
        context.composing = false;
        context.keystrokeBufferLength = 0;
        Expect("stray character with no composition", context, FALSE, CATEGORY_INVOKE_COMPOSITION_EDIT_SESSION,
               FUNCTION_FINALIZE_TEXTSTORE);
    }

    if (failures != 0)
    {
        return 1;
    }
    std::puts("PASS: full-pinyin virtual-key classification stays in the IME's hands");
    return 0;
}
