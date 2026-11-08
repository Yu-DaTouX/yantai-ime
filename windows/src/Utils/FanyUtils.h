#pragma once
#include <Windows.h>
#include <string>
#include "../../../engine/contracts/input_state_shortcuts.h"

namespace FanyUtils
{
void SendKeys(std::wstring pinyin);
std::wstring string_to_wstring(const std::string &str);
std::string wstring_to_string(const std::wstring &wstr);
std::string to_lower_copy(const std::string &str);
std::wstring GetCurrentProcessName();
std::string::size_type count_utf8_chars(const std::string &str);
// Read input.default_ime_mode from the shared config.toml in the IME data directory
// (%LOCALAPPDATA%\metasequoiaime by default; the installer may place it on another volume).
// Returns TRUE for Chinese (default), FALSE for English.
BOOL ReadConfiguredDefaultImeModeChinese();
// Read input.punctuation_lock from shared config.toml.
// 0 = follow IME, 1 = always Chinese punctuation, 2 = always English punctuation.
int ReadConfiguredPunctuationLock();
// Refresh the in-process lock cache from config.toml.
void RefreshPunctuationLockFromConfig();

struct SwitchLanguageHotkeys
{
    bool shift = true;
    bool ctrl = false;
    bool ctrl_alt_space = true;
    bool character_set_ctrl_shift_f = true;
    bool commit_pinyin_shift_enter = true;
    // 候选词英文上屏：[keybindings] commit_english / english_senses，默认 Ctrl+Enter /
    // Ctrl+Shift+Enter。未配置、空串或写错时 key 为 0，表示这两条不在 TSF 侧吃键。
    FanyImeKeyChord::Binding commit_english{0x0d, FanyImeKeyChord::Control};
    FanyImeKeyChord::Binding english_senses{0x0d, FanyImeKeyChord::Control | FanyImeKeyChord::Shift};
    FanyImeKeyChord::Binding translate_mode{'T', FanyImeKeyChord::Control};
    FanyImeInputStateShortcuts::Snapshot input_state;
};
// Read keybindings.switch_language_* from shared config.toml.
SwitchLanguageHotkeys ReadConfiguredSwitchLanguageHotkeys();
// Whether input.vim_mode_apps in shared config.toml lists this host process; cached, disabled by default.
bool ReadConfiguredVimMode();

// Which taskbar language-bar buttons to show. Read from [language_bar] in the
// shared config.toml; a missing key or file defaults to visible, so an absent
// config keeps every button on. `enabled` is the master switch: when false, no
// language-bar button is registered regardless of the per-button flags.
struct LanguageBarVisibility
{
    bool enabled = true;
    bool ime_mode = true;
    bool double_single_byte = true;
    bool punctuation = true;
};
// Read language_bar.enabled / show_ime_mode / show_double_single_byte / show_punctuation.
LanguageBarVisibility ReadConfiguredLanguageBarVisibility();

// Whether the candidate glosses are on (general.candidate_translations). The two translation
// chords, Ctrl+Enter and Ctrl+1..9, hang off it and are otherwise always on: with the glosses
// turned off there is nothing to commit, so the chords must not be taken from the host either.
bool ReadConfiguredCandidateTranslationsEnabled();
} // namespace FanyUtils
