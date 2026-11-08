#include "Key/TranslationModeKeyPolicy.h"
#include "../../engine/contracts/key_chord.h"

#include <cstdio>
#include <initializer_list>

using namespace translation_mode_key_policy;

int main()
{
    int failures = 0;
    auto check = [&](const char *name, Route actual, Route expected) {
        if (actual != expected)
        {
            std::fprintf(stderr, "FAIL %s\n", name);
            ++failures;
        }
    };
    FanyImeKeyChord::Binding binding;
    for (const auto value : {"Ctrl+T", "Ctrl+KeyT"})
    {
        if (!FanyImeKeyChord::ParseTranslationMode(value, binding) || !binding.Matches('T', 2))
            ++failures;
    }
    if (!FanyImeKeyChord::ParseTranslationMode("Ctrl+Shift+G", binding) || !binding.Matches('G', 3))
        ++failures;
    for (const auto value : {"Alt+T", "Ctrl+t", "!invalid", "Ctrl+TT"})
        if (FanyImeKeyChord::ParseTranslationMode(value, binding) || binding.key)
            ++failures;
    if (!FanyImeKeyChord::ParseTranslationMode("", binding) || binding.key)
        ++failures;
    if (FanyImeKeyChord::Parse("Ctrl+T", binding))
        ++failures; // Do not widen other keybindings' accepted syntax.
    Context context;
    FanyImeKeyChord::ParseTranslationMode("Ctrl+T", binding);
    context.toggleBindingMatches = binding.Matches('T', 2);
    check("idle Ctrl+T belongs to browser", Classify('T', 2, context), Route::Existing);
    context.composing = true;
    check("composing Ctrl+T enters", Classify('T', 2, context), Route::Toggle);
    context.fullPinyin = false;
    check("not full pinyin", Classify('T', 2, context), Route::Existing);
    context.active = true;
    context.composing = false;
    check("active Ctrl+T exits without pinyin", Classify('T', 2, context), Route::Toggle);
    check("rebound chord uses binding matcher", Classify('G', 3, context), Route::Toggle);
    context.toggleBindingMatches = false;
    check("Enter", Classify(0x0D, 0, context), Route::CommitEnglish);
    check("Shift Enter", Classify(0x0D, 1, context), Route::CommitChinese);
    check("Ctrl Enter", Classify(0x0D, 2, context), Route::CommitEnglishAndExit);
    check("Alt Enter stays existing", Classify(0x0D, 4, context), Route::Existing);
    check("Ctrl Shift Enter stays existing", Classify(0x0D, 3, context), Route::Existing);
    check("Backspace without pinyin reaches Server", Classify(0x08, 0, context), Route::Backspace);
    check("Escape without pinyin reaches Server", Classify(0x1B, 0, context), Route::Escape);
    check("metadata is not a modifier", Classify(0x0D, 0x02000000, context), Route::CommitEnglish);
    context.windowsKey = true;
    check("Win Ctrl Enter belongs to shell", Classify(0x0D, 2, context), Route::Existing);
    context.windowsKey = false;
    context.serverAvailable = false;
    check("stale active flag offline cannot claim Enter", Classify(0x0D, 0, context), Route::Existing);
    context.serverAvailable = true;
    context.active = false;
    check("mode punctuation", Classify(0xBE, 0, Context{true}), Route::DraftKey);
    check("mode digit", Classify('2', 0, Context{true}), Route::DraftKey);
    check("mode space", Classify(0x20, 0, Context{true}), Route::DraftKey);
    check("letter remains composition input", Classify('A', 0, Context{true}), Route::Existing);
    check("normal Enter unchanged", Classify(0x0D, 0, context), Route::Existing);
    check("normal Backspace unchanged", Classify(0x08, 0, context), Route::Existing);
    check("normal Escape unchanged", Classify(0x1B, 0, context), Route::Existing);
    if (failures)
        return 1;
    std::puts("PASS: translation-mode key ownership");
    return 0;
}
