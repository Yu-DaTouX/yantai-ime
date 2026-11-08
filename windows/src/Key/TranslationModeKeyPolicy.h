#pragma once

#include <cstdint>

namespace translation_mode_key_policy
{
enum class Route
{
    Existing,
    Toggle,
    CommitEnglish,
    CommitChinese,
    CommitEnglishAndExit,
    Backspace,
    Escape
};

struct Context
{
    bool active = false;
    bool composing = false;
    bool fullPinyin = true;
    bool serverAvailable = true;
    bool windowsKey = false;
    bool toggleBindingMatches = false;
};

// Use this same decision in TestKeyDown and the actual dispatch. In particular, never register
// Ctrl+T as an unconditional TSF preserved key: idle browsers must retain New Tab.
constexpr Route Classify(uint32_t key, uint32_t modifiers, const Context &context)
{
    if (!context.serverAvailable || context.windowsKey)
        return Route::Existing;
    if (context.toggleBindingMatches && (context.active || (context.composing && context.fullPinyin)))
        return Route::Toggle;
    if (!context.active)
        return Route::Existing;
    modifiers &= 7u; // Preserve existing metadata/UI-less bits without treating them as modifiers.
    if (key == 0x0D)
    {
        if (modifiers == 0)
            return Route::CommitEnglish;
        if (modifiers == 1)
            return Route::CommitChinese;
        if (modifiers == 2)
            return Route::CommitEnglishAndExit;
    }
    if (modifiers == 0 && key == 0x08)
        return Route::Backspace;
    if (modifiers == 0 && key == 0x1B)
        return Route::Escape;
    return Route::Existing;
}
} // namespace translation_mode_key_policy
