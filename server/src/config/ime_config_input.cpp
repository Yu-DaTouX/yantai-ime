// 输入方案与输入行为配置：方案、中日文模式、简繁、中英切换键、上屏样式、辅助码、全拼纠错、模糊音和调频。
#include "config/ime_config_internal.h"
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>
#include "global/globals.h"
#include "defines/defines.h"
#include "engine/common/helpcode_utils.h"
#include "engine/contracts/input_state_shortcuts.h"
#include "engine/core/data_path.h"

using namespace ime_config_detail;

SchemeType GetConfiguredInputScheme()
{
    return g_input_scheme;
}

std::string GetConfiguredInputSchemeName()
{
    return "quanpin";
}

bool SetConfiguredInputScheme(const std::string &scheme)
{
    if (scheme != "quanpin")
    {
        return false;
    }
    if (!WriteConfiguredValue("input", "schema", EscapeTomlBasicString(scheme)))
    {
        return false;
    }
    g_input_scheme = ParseScheme(scheme);
    RefreshEffectiveTsfPreeditStyle();
    NotifyImeServerInputSchemeChanged();
    return true;
}

const std::string &GetConfiguredCharacterSet()
{
    return g_character_set;
}

bool SetConfiguredCharacterSet(const std::string &character_set)
{
    if (character_set != "simplified" && character_set != "traditional")
    {
        return false;
    }
    if (!WriteConfiguredValue("input", "character_set", EscapeTomlBasicString(character_set)))
    {
        return false;
    }
    g_character_set = character_set;
    // Marshal toolbar refreshes to their owner thread. Settings
    // in a separate process reaches the same refresh through ConfigChanged.
    NotifyImeServer(WM_REFRESH_CHARACTER_SET, L"ConfigChanged");
    return true;
}

bool GetConfiguredCharacterSetShortcutEnabled()
{
    return g_character_set_shortcut_enabled;
}

bool SetConfiguredCharacterSetShortcutEnabled(bool enabled)
{
    if (!WriteConfiguredValue("keybindings", "toggle_character_set_ctrl_shift_f", enabled ? "true" : "false"))
        return false;
    g_character_set_shortcut_enabled = enabled;
    return true;
}

bool GetConfiguredPinyinCommitShortcutEnabled()
{
    return g_commit_pinyin_shift_enter_enabled;
}

bool SetConfiguredPinyinCommitShortcutEnabled(bool enabled)
{
    if (!WriteConfiguredValue("keybindings", "commit_pinyin_shift_enter", enabled ? "true" : "false"))
        return false;
    g_commit_pinyin_shift_enter_enabled = enabled;
    return true;
}

const std::string &GetConfiguredDefaultImeMode()
{
    return g_default_ime_mode;
}

bool SetConfiguredDefaultImeMode(const std::string &mode)
{
    if (mode != "chinese" && mode != "english")
    {
        return false;
    }
    if (!WriteConfiguredValue("input", "default_ime_mode", EscapeTomlBasicString(mode)))
    {
        return false;
    }
    g_default_ime_mode = mode;
    return true;
}

const std::string &GetConfiguredImeModeScope()
{
    return g_ime_mode_scope;
}

bool SetConfiguredImeModeScope(const std::string &scope)
{
    if (scope != "app" && scope != "global")
    {
        return false;
    }
    if (!WriteConfiguredValue("input", "ime_mode_scope", EscapeTomlBasicString(scope)))
    {
        return false;
    }
    g_ime_mode_scope = scope;
    return true;
}

bool IsConfiguredImeModeScopeGlobal()
{
    return g_ime_mode_scope == "global";
}

bool GetConfiguredSwitchLanguageShiftEnabled()
{
    return g_switch_language_shift_enabled;
}

bool SetConfiguredSwitchLanguageShiftEnabled(bool enabled)
{
    if (!WriteConfiguredValue("keybindings", "switch_language_shift", enabled ? "true" : "false"))
    {
        return false;
    }
    g_switch_language_shift_enabled = enabled;
    return true;
}

bool GetConfiguredSwitchLanguageCtrlEnabled()
{
    return g_switch_language_ctrl_enabled;
}

bool SetConfiguredSwitchLanguageCtrlEnabled(bool enabled)
{
    if (!WriteConfiguredValue("keybindings", "switch_language_ctrl", enabled ? "true" : "false"))
    {
        return false;
    }
    g_switch_language_ctrl_enabled = enabled;
    return true;
}

bool GetConfiguredSwitchLanguageCtrlAltSpaceEnabled()
{
    return g_switch_language_ctrl_alt_space_enabled;
}

bool SetConfiguredSwitchLanguageCtrlAltSpaceEnabled(bool enabled)
{
    if (!WriteConfiguredValue("keybindings", "switch_language_ctrl_alt_space", enabled ? "true" : "false"))
    {
        return false;
    }
    g_switch_language_ctrl_alt_space_enabled = enabled;
    return true;
}

const InputStateShortcutValues &GetConfiguredInputStateShortcuts()
{
    return g_input_state_shortcuts;
}

bool SetConfiguredInputStateShortcut(std::size_t index, const std::string &binding)
{
    using namespace FanyImeInputStateShortcuts;
    if (index >= Definitions.size())
        return false;
    if (!Validate(index, binding, InputStateShortcutViews(g_input_state_shortcuts)).empty() ||
        !WriteConfiguredValue("keybindings", std::string(Definitions[index].key), EscapeTomlBasicString(binding)))
        return false;
    g_input_state_shortcuts[index] = binding;
    return true;
}

bool GetConfiguredEscapeKeepsSelectedWord()
{
    return g_escape_keeps_selected_word;
}

bool SetConfiguredEscapeKeepsSelectedWord(bool enabled)
{
    if (!WriteConfiguredValue("input", "escape_keeps_selected_word", enabled ? "true" : "false"))
    {
        return false;
    }
    g_escape_keeps_selected_word = enabled;
    return true;
}

bool GetConfiguredEnterLearnsEnglishWord()
{
    return g_enter_learns_english_word;
}

bool SetConfiguredEnterLearnsEnglishWord(bool enabled)
{
    if (!WriteConfiguredValue("input", "enter_learns_english_word", enabled ? "true" : "false"))
    {
        return false;
    }
    g_enter_learns_english_word = enabled;
    return true;
}

const std::string &GetConfiguredTsfPreeditStyle()
{
    return g_tsf_preedit_style;
}

bool SetConfiguredTsfPreeditStyle(const std::string &style)
{
    if (!GlobalSettings::isKnownTsfPreeditStyle(style))
    {
        return false;
    }
    if (!WriteConfiguredValue("appearance", "tsf_preedit_style", EscapeTomlBasicString(style)))
    {
        return false;
    }
    g_tsf_preedit_style = style;
    RefreshEffectiveTsfPreeditStyle();
    return true;
}

namespace ime_config_detail
{
void RefreshEffectiveTsfPreeditStyle()
{
    GlobalSettings::setTsfPreeditStyle(g_tsf_preedit_style);
}
} // namespace ime_config_detail

bool GetConfiguredQuanpinHelpcodeEnabled()
{
    return g_quanpin_helpcode_enabled;
}

bool SetConfiguredQuanpinHelpcodeEnabled(bool enabled)
{
    if (!WriteConfiguredValue("helpcode", "quanpin_helpcode", enabled ? "true" : "false"))
    {
        return false;
    }
    g_quanpin_helpcode_enabled = enabled;
    return true;
}

bool GetConfiguredQuanpinAutocorrectTransposition()
{
    return g_quanpin_autocorrect_transposition;
}

bool SetConfiguredQuanpinAutocorrectTransposition(bool enabled)
{
    if (!WriteConfiguredValue("quanpin", "autocorrect_transposition", enabled ? "true" : "false"))
    {
        return false;
    }
    g_quanpin_autocorrect_transposition = enabled;
    return true;
}

bool GetConfiguredQuanpinAutocorrectNeighbor()
{
    return g_quanpin_autocorrect_neighbor;
}

bool SetConfiguredQuanpinAutocorrectNeighbor(bool enabled)
{
    if (!WriteConfiguredValue("quanpin", "autocorrect_neighbor", enabled ? "true" : "false"))
    {
        return false;
    }
    g_quanpin_autocorrect_neighbor = enabled;
    return true;
}

bool GetConfiguredQuanpinAutocorrectDeletion()
{
    return g_quanpin_autocorrect_deletion;
}

bool SetConfiguredQuanpinAutocorrectDeletion(bool enabled)
{
    if (!WriteConfiguredValue("quanpin", "autocorrect_deletion", enabled ? "true" : "false"))
    {
        return false;
    }
    g_quanpin_autocorrect_deletion = enabled;
    return true;
}

bool GetConfiguredQuanpinAutocorrectInsertion()
{
    return g_quanpin_autocorrect_insertion;
}

bool SetConfiguredQuanpinAutocorrectInsertion(bool enabled)
{
    if (!WriteConfiguredValue("quanpin", "autocorrect_insertion", enabled ? "true" : "false"))
    {
        return false;
    }
    g_quanpin_autocorrect_insertion = enabled;
    return true;
}

bool GetConfiguredQuanpinAutocorrectMarker()
{
    return g_quanpin_autocorrect_marker;
}

bool SetConfiguredQuanpinAutocorrectMarker(bool enabled)
{
    if (!WriteConfiguredValue("quanpin", "autocorrect_marker", enabled ? "true" : "false"))
    {
        return false;
    }
    g_quanpin_autocorrect_marker = enabled;
    return true;
}

bool GetConfiguredFuzzyPinyinEnabled()
{
    return g_fuzzy_pinyin_enabled;
}

bool SetConfiguredFuzzyPinyinEnabled(bool enabled)
{
    // 首次启用播种：出厂态第一次开总开关，11 条规则全部置 true 并持久化；之后总开关的
    // 任何翻动只写总开关一个键，用户修剪过的选择在临时停用/恢复间原样保留。不能拿
    // 「规则位全零」当首次信号——用户故意全部取消勾选后位图同样是零，无标记会把每次
    // 开启都误判成首次启用，反复改写用户的空选择。
    if (enabled && !g_fuzzy_seeded)
    {
        std::vector<ConfigValueUpdate> updates;
        updates.reserve(std::size(kFuzzyPinyinRuleKeys) + 2);
        updates.push_back({"input", "fuzzy_pinyin", "true"});
        updates.push_back({"input", "fuzzy_seeded", "true"});
        for (const auto &entry : kFuzzyPinyinRuleKeys)
            updates.push_back({"input", entry.key, "true"});
        // 一次批量写：WriteConfiguredValues 走单文件临时改名，无部分失败态。
        if (!WriteConfiguredValues(updates))
            return false;
        g_fuzzy_pinyin_enabled = true;
        g_fuzzy_seeded = true;
        for (const auto &entry : kFuzzyPinyinRuleKeys)
            g_fuzzy_pinyin_rules |= static_cast<std::uint32_t>(entry.rule);
        return true;
    }
    if (!WriteConfiguredValue("input", "fuzzy_pinyin", enabled ? "true" : "false"))
        return false;
    g_fuzzy_pinyin_enabled = enabled;
    return true;
}

// The master switch is gated here and nowhere else: sessions see all-zero rules while it is
// off, and the cached rule bits survive the toggle so re-enabling restores the prior choice.
metasequoia::FuzzyPinyinOptions GetConfiguredFuzzyPinyinOptions()
{
    metasequoia::FuzzyPinyinOptions options;
    if (g_fuzzy_pinyin_enabled)
        options.rules = g_fuzzy_pinyin_rules;
    return options;
}

metasequoia::FuzzyPinyinOptions GetConfiguredFuzzyPinyinRuleStates()
{
    metasequoia::FuzzyPinyinOptions options;
    options.rules = g_fuzzy_pinyin_rules;
    return options;
}

bool SetConfiguredFuzzyPinyinRule(const std::string &key, bool enabled)
{
    const FuzzyPinyinRuleKey *entry = nullptr;
    for (const auto &candidate : kFuzzyPinyinRuleKeys)
    {
        if (key == candidate.key)
        {
            entry = &candidate;
            break;
        }
    }
    if (!entry)
        return false;
    const auto bit = static_cast<std::uint32_t>(entry->rule);
    if (!WriteConfiguredValue("input", entry->key, enabled ? "true" : "false"))
        return false;
    if (enabled)
        g_fuzzy_pinyin_rules |= bit;
    else
        g_fuzzy_pinyin_rules &= ~bit;
    return true;
}

const std::string &GetConfiguredQuanpinHelpcodeSchema()
{
    return g_quanpin_helpcode_schema;
}

bool SetConfiguredQuanpinHelpcodeSchema(const std::string &schema)
{
    if (!IsHelpcodeSchemaAvailable(schema))
        return false;
    if (!WriteConfiguredValue("helpcode", "quanpin_helpcode_schema", EscapeTomlBasicString(schema)))
        return false;
    g_quanpin_helpcode_schema = schema;
    return true;
}

std::vector<CustomHelpcodeSchemaInfo> GetCustomHelpcodeSchemas()
{
    std::vector<CustomHelpcodeSchemaInfo> result;
    for (const auto &schema : HelpcodeUtils::list_custom_helpcode_schemas(metasequoia::data_directory()))
    {
        const std::string &name = schema.name.empty() ? schema.name_en : schema.name;
        const std::string &name_en = schema.name_en.empty() ? schema.name : schema.name_en;
        result.push_back(
            {schema.schema, name.empty() ? schema.file_stem : name, name_en.empty() ? schema.file_stem : name_en});
    }
    return result;
}

std::string GetCustomHelpcodeDirectory()
{
    return metasequoia::path_to_utf8(HelpcodeUtils::custom_helpcode_directory(metasequoia::data_directory()));
}

bool GetConfiguredShowQuanpinHelpcodeInCandidateWindow()
{
    return g_show_quanpin_helpcode_in_candidate_window;
}

bool SetConfiguredShowQuanpinHelpcodeInCandidateWindow(bool enabled)
{
    if (!WriteConfiguredValue("helpcode", "show_qp_helpcode_in_candidate_window", enabled ? "true" : "false"))
    {
        return false;
    }
    g_show_quanpin_helpcode_in_candidate_window = enabled;
    return true;
}

const FrequencyAdjustmentConfig &GetConfiguredFrequencyAdjustment()
{
    return g_frequency_adjustment;
}

bool SetConfiguredFrequencyAdjustmentString(const std::string &key, const std::string &value)
{
    if (key != "mode" ||
        (value != "disabled" && value != "pin" && value != "halve" && value != "linear" && value != "promote") ||
        !WriteConfiguredValue("frequency_adjustment", key, EscapeTomlBasicString(value)))
        return false;
    g_frequency_adjustment.mode = value;
    return true;
}

bool SetConfiguredFrequencyAdjustmentInt(const std::string &key, int value)
{
    if ((key != "trigger_count" && key != "linear_step") || value < 1 || value > 10 ||
        !WriteConfiguredValue("frequency_adjustment", key, std::to_string(value)))
        return false;
    if (key == "trigger_count")
        g_frequency_adjustment.trigger_count = value;
    else
        g_frequency_adjustment.linear_step = value;
    return true;
}
