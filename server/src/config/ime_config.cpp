#include "window/caret_state_indicator_policy.h"
#include "window/candidate_view_model.h"
#include "ime_config.h"
#include "config/ime_config_internal.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>
#include "utils/common_utils.h"
#include "global/globals.h"
#include "clipboard/clipboard_history.h"
#include "engine/contracts/input_state_shortcuts.h"

using namespace ime_config_detail;

namespace
{
std::string g_session_backend = "legacy";
} // namespace

namespace ime_config_detail
{
SchemeType g_input_scheme = SchemeType::Quanpin;
std::string g_character_set = "simplified";
std::string g_default_ime_mode = "chinese";
std::string g_ime_mode_scope = "app";
bool g_switch_language_shift_enabled = true;
bool g_switch_language_ctrl_enabled = false;
bool g_switch_language_ctrl_alt_space_enabled = true;
bool g_character_set_shortcut_enabled = true;
bool g_commit_pinyin_shift_enter_enabled = true;
InputStateShortcutValues g_input_state_shortcuts = [] {
    InputStateShortcutValues values;
    const auto defaults = FanyImeInputStateShortcuts::Defaults();
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = defaults[i];
    return values;
}();
int g_candidate_page_size = 8;
std::string g_candidate_font = "Noto Sans SC";
std::string g_candidate_english_font = "Segoe UI";
std::string g_candidate_default_font = "Microsoft YaHei";
std::vector<std::string> g_candidate_fallback_fonts = {"Noto Sans SC", "Microsoft YaHei"};
int g_candidate_font_size = 16;
int g_candidate_window_preedit_font_size = 16;
std::atomic_bool g_diagnostic_log_enabled{false};
std::atomic_bool g_tsf_diagnostic_log_enabled{false};
std::string g_candidate_text_color = "auto";
// 造词时 Esc 只清掉还没选的拼音、保留已选的词（小狼毫风格），第二次 Esc 才整体取消。默认关闭。
bool g_escape_keeps_selected_word = false;
// 回车上屏英文时顺带写入英文词库。默认开启，保持原有行为。
bool g_enter_learns_english_word = true;
std::string g_tsf_preedit_style = "raw";
// The legacy single "quanpin.autocorrect" key is deliberately not read anymore:
// all four correction types default to off and users opt in from the settings page.
// The two legacy switches may still be set by users who had them on; the deletion and
// insertion keys are new and get no inheritance from them (upgrade default: off).
bool g_quanpin_autocorrect_transposition = false;
bool g_quanpin_autocorrect_neighbor = false;
bool g_quanpin_autocorrect_deletion = false;
bool g_quanpin_autocorrect_insertion = false;
// Whether correction-sourced candidates show the trailing "*" marker. On by default.
bool g_quanpin_autocorrect_marker = true;
// Bitmask of FuzzyPinyinRule bits. All off by default so upgrades never change behavior.
std::uint32_t g_fuzzy_pinyin_rules = 0;
// Master switch, off by default. It gates GetConfiguredFuzzyPinyinOptions only; the rule
// bitmask above keeps its value so temporary disable/enable preserves the user's choices.
bool g_fuzzy_pinyin_enabled = false;
// First-enable seed marker: once set, toggling the master switch never rewrites the rule
// keys. Internal key — never sent to the settings page; if the template loses it, the
// upgrade merge drops it and every user gets re-seeded on the next master-switch flip.
bool g_fuzzy_seeded = false;
bool g_floating_toolbar_enabled = true;
bool g_caret_state_indicator_enabled = false;
bool g_caret_state_indicator_on_focus = false;
std::string g_caret_state_indicator_position = FanyImeUi::kDefaultCaretStatePosition;
FloatingToolbarItemsConfig g_floating_toolbar_items;
double g_floating_toolbar_scale = 1.0;
int g_floating_toolbar_font_size = kFloatingToolbarFontSizeDefault;
bool g_floating_toolbar_auto_hide = false;
int g_floating_toolbar_auto_hide_delay = kFloatingToolbarAutoHideDelayDefault;
bool g_floating_toolbar_shadow = true;
bool g_english_candidates_enabled = false;
bool g_candidate_translations_enabled = false;
std::atomic_bool g_candidate_translation_show_pos{true};
std::atomic_bool g_candidate_translation_single_sense{false};
int g_english_mixed_input_min_chars = kEnglishMixedInputMinCharsDefault;
int g_english_mixed_input_first_page_count = kEnglishMixedInputFirstPageCountDefault;
// 整句候选来源与去重补位开关：词格、Google 和神经速度档默认开，神经效果档与去重补位默认关。
bool g_assoc_sentence_wordlattice = false;
bool g_assoc_sentence_google = true;
bool g_assoc_sentence_neural_desktop = false;
bool g_assoc_sentence_neural_keyboard = true;
bool g_assoc_context_candidate_rerank = false;
bool g_assoc_prefix_completion = true;
std::size_t g_assoc_prefix_completion_min_chars = metasequoia::kPrefixCompletionDefaultMinimumChars;
bool g_assoc_sentence_show_next_on_duplicate = false;
// octagram 语法模型（八股文 .gram）：总开关默认关；模型 id 默认空（空 = 未选择任何模型）。
// 奖励权重与六参数不进设置页，供配置文件高级调参；默认对齐万象现行（wanxiang.schema.yaml）。
bool g_assoc_sentence_collocation_enabled = false;
std::string g_assoc_sentence_collocation_model;
double g_assoc_sentence_collocation_weight = 1.0;
int g_assoc_sentence_collocation_max_length = 6;
int g_assoc_sentence_collocation_min_length = 2;
double g_assoc_sentence_collocation_penalty = -14.0;
double g_assoc_sentence_collocation_non_penalty = -6.0;
double g_assoc_sentence_collocation_weak_penalty = -100.0;
double g_assoc_sentence_collocation_rear_penalty = -18.0;
// 候选窗整句候选后的来源标签（〔Trigram〕〔神经K〕等），默认不显示。
bool g_assoc_sentence_source_badge = false;
bool g_emoji_mixed_input_enabled = false;
bool g_kaomoji_mixed_input_enabled = false;
bool g_symbol_mixed_input_enabled = false;
bool g_radical_mode_enabled = true;
bool g_quick_phrase_enabled = true;
bool g_quick_phrase_candidates_enabled = false;
bool g_quick_phrase_frequency_enabled = true;
bool g_mixed_candidates_enabled = true;
bool g_date_time_candidates_enabled = false;
bool g_date_time_menu_enabled = true;
bool g_date_time_mode_enabled = true;
bool g_emoji_mode_enabled = false;
bool g_kaomoji_mode_enabled = false;
bool g_symbol_mode_enabled = true;
bool g_y_mode_enabled = true;
bool g_v_mode_enabled = true;
bool g_clipboard_history_enabled = false;
bool g_language_bar_enabled = true;
bool g_language_bar_show_ime_mode = true;
bool g_language_bar_show_double_single_byte = true;
bool g_language_bar_show_punctuation = true;
bool g_paging_minus_equal_enabled = true;
bool g_paging_comma_period_enabled = false;
bool g_paging_brackets_enabled = false;
bool g_paging_tab_enabled = true;
bool g_paging_page_up_down_enabled = true;
// Off by default: the wheel only reaches the candidate host when Windows'
// "scroll inactive windows on hover" is on, so the feature is opt-in.
bool g_paging_mouse_wheel_enabled = false;
bool g_candidate_arrow_navigation_enabled = true;
bool g_candidate_horizontal_arrow_navigation_enabled = false;
bool g_word_to_character_enabled = false;
std::string g_word_to_character_keys = "brackets";
bool g_smart_punctuation_enabled = false;
bool g_smart_punctuation_space_convert_enabled = false;
bool g_smart_punctuation_direct_digit_enabled = false;
bool g_smart_punctuation_direct_letter_enabled = false;
bool g_smart_punctuation_repeat_to_chinese_enabled = false;
bool g_paired_punctuation_enabled = true;
bool g_enumeration_comma_backslash_enabled = true;
bool g_enumeration_comma_slash_enabled = true;
std::string g_punctuation_lock = "follow";
std::string g_candidate_window_layout = "vertical";
bool g_candidate_window_follow_cursor = true;
bool g_candidate_window_fit_content = false;
bool g_candidate_window_single_line = false;
std::string g_candidate_skin = "fluent";
std::string g_candidate_window_preedit_style = "pinyin";
bool g_candidate_fixed_badge = true;
std::string g_candidate_fixed_badge_style = "paperclip";
std::string g_settings_window_linger = "off";
bool g_settings_launch_animation = true;
// 发现新版本时由后台自动下载安装包并校验。出厂开启：下载完还要用户自己确认安装，
// 缺省就打开能让更新真正发生；不想占带宽用户可以关掉。
bool g_auto_download_update = true;
std::string g_theme_mode = "system";
std::string g_theme_settings = "follow";
std::string g_theme_cand = "follow";
std::string g_theme_ftb = "follow";
std::string g_theme_menu = "follow";
} // namespace ime_config_detail

namespace ime_config_detail
{
std::mutex g_network_proxy_mutex;
NetworkProxyConfig g_network_proxy;
FrequencyAdjustmentConfig g_frequency_adjustment;
std::filesystem::path g_config_path;
} // namespace ime_config_detail

namespace
{
std::optional<std::filesystem::file_time_type> g_config_last_write_time;
// 最后一次读到的 config.toml 大小与内容摘要。修改时间只在秒以下精度的粒度上可信，
// 连续两次写入可能落在同一个时间刻度上（尤其写入走的是临时文件 + MoveFileEx 替换），
// 那时 last_write_time 相等但内容已经变了。只在时间戳没变时用它补一次判定。
std::optional<std::uintmax_t> g_config_last_size;
std::optional<uint64_t> g_config_last_content_hash;
} // namespace

namespace ime_config_detail
{
bool IsValidCandidateSkinId(const std::string &skin)
{
    if (skin.empty() || skin.size() > 64 || !std::isalnum(static_cast<unsigned char>(skin.front())))
    {
        return false;
    }
    return std::all_of(skin.begin(), skin.end(), [](unsigned char ch) {
        return std::islower(ch) || std::isdigit(ch) || ch == '.' || ch == '_' || ch == '-';
    });
}

// 固定排位徽标样式白名单。存枚举名而非字面 emoji：TOML/日志/diff 里不出现图形字符，
// 日后新增样式只改这里和展示层的映射。
bool IsValidCandidateFixedBadgeStyle(const std::string &style)
{
    return style == "paperclip" || style == "pushpin" || style == "dot";
}

bool IsValidSettingsWindowLinger(const std::string &linger)
{
    return linger == "off" || linger == "1m" || linger == "5m" || linger == "10m" || linger == "30m" ||
           linger == "60m" || linger == "forever";
}

} // namespace ime_config_detail

namespace
{
template <typename Node> bool TomlFlexibleBool(const Node &node, bool fallback)
{
    if (const auto value = node.template value<bool>())
        return *value;
    if (const auto text = node.template value<std::string>())
    {
        std::string value = *text;
        for (char &ch : value)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (value == "true" || value == "1" || value == "yes" || value == "on")
            return true;
        if (value == "false" || value == "0" || value == "no" || value == "off")
            return false;
    }
    return fallback;
}
} // namespace

namespace ime_config_detail
{
uint64_t HashFileContents(const std::filesystem::path &path)
{
    // FNV-1a：只为判断「内容有没有变」，不做安全用途。ReadFileText 在文件缺失时返回空串。
    const std::string text = ReadFileText(path);
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char byte : text)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

void RememberConfigWriteTime()
{
    std::error_code error;
    const auto write_time = std::filesystem::last_write_time(g_config_path, error);
    if (!error)
    {
        g_config_last_write_time = write_time;
    }
    // 与时间戳同时记下大小和内容摘要，供 ReloadImeConfigIfChanged 在时间戳没变时复核。
    std::error_code size_error;
    const auto size = std::filesystem::file_size(g_config_path, size_error);
    if (!size_error)
    {
        g_config_last_size = size;
        g_config_last_content_hash = ime_config_detail::HashFileContents(g_config_path);
    }
    else
    {
        g_config_last_size.reset();
        g_config_last_content_hash.reset();
    }
}

bool LoadImeConfig()
{
    ConfigFileLock lock;
    if (!lock)
        return false;
    try
    {
        // Read via the wide path and parse the text. toml::parse_file(g_config_path.string()) would run the
        // path through the ANSI code page; on a non-ASCII (e.g. Chinese) config path that corrupts it, and on
        // a code page that cannot represent the characters path::string() throws, crashing the process.
        std::ifstream input(g_config_path, std::ios::binary);
        if (!input)
            return false;
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        auto tbl = toml::parse(text);
        RevealStoredCredentials(tbl);

        const int page_size = tbl["appearance"]["page_size"].value_or(6);
        g_candidate_page_size = page_size >= 3 && page_size <= 9 ? page_size : 6;
        g_candidate_font = tbl["appearance"]["font"].value_or(std::string("Noto Sans SC"));
        if (g_candidate_font.empty())
            g_candidate_font = "Noto Sans SC";
        g_candidate_english_font = tbl["appearance"]["english_font"].value_or(std::string("Segoe UI"));
        if (g_candidate_english_font.empty())
            g_candidate_english_font = "Segoe UI";
        g_candidate_default_font = tbl["appearance"]["default_font"].value_or(std::string("Microsoft YaHei"));
        if (g_candidate_default_font.empty())
            g_candidate_default_font = "Microsoft YaHei";
        // Absence means an old profile; an explicit empty array means system fallback only.
        g_candidate_fallback_fonts = {g_candidate_font, g_candidate_default_font};
        if (const auto *fonts = tbl["appearance"]["fallback_fonts"].as_array())
        {
            g_candidate_fallback_fonts.clear();
            for (const auto &node : *fonts)
            {
                if (auto font = node.value<std::string>();
                    font && !font->empty() && font->size() <= 256 &&
                    !std::any_of(font->begin(), font->end(), [](unsigned char ch) { return ch < 32 || ch == 127; }) &&
                    std::find(g_candidate_fallback_fonts.begin(), g_candidate_fallback_fonts.end(), *font) ==
                        g_candidate_fallback_fonts.end())
                {
                    g_candidate_fallback_fonts.push_back(*font);
                    if (g_candidate_fallback_fonts.size() == 32)
                        break;
                }
            }
        }
        {
            const int font_size = tbl["appearance"]["font_size"].value_or(16);
            g_candidate_font_size =
                font_size >= kCandidateFontSizeMin && font_size <= kCandidateFontSizeMax ? font_size : 16;
        }
        {
            const int font_size =
                tbl["appearance"]["candidate_window_preedit_font_size"].value_or(g_candidate_font_size);
            g_candidate_window_preedit_font_size =
                font_size >= kCandidateFontSizeMin && font_size <= kCandidateFontSizeMax ? font_size
                                                                                         : g_candidate_font_size;
        }
        {
            const std::string color = tbl["appearance"]["cand_text_color"].value_or(std::string("auto"));
            g_candidate_text_color = color.empty() ? "auto" : color;
        }
        g_session_backend = tbl["input"]["session_backend"].value_or(std::string("legacy"));
        g_input_scheme = ParseScheme(tbl["input"]["schema"].value_or(std::string("quanpin")));
        // Removed Japanese preferences fall back to full pinyin.
        if (tbl["input"]["mode"].value_or(std::string("chinese")) == "japanese")
            g_input_scheme = SchemeType::Quanpin;
        const std::string character_set = tbl["input"]["character_set"].value_or(std::string("simplified"));
        g_character_set = character_set == "traditional" ? "traditional" : "simplified";
        {
            const std::string mode = tbl["input"]["default_ime_mode"].value_or(std::string("chinese"));
            g_default_ime_mode = mode == "english" ? "english" : "chinese";
        }
        {
            const std::string scope = tbl["input"]["ime_mode_scope"].value_or(std::string("app"));
            g_ime_mode_scope = scope == "global" ? "global" : "app";
        }
        g_escape_keeps_selected_word = tbl["input"]["escape_keeps_selected_word"].value_or(false);
        g_enter_learns_english_word = tbl["input"]["enter_learns_english_word"].value_or(true);
        g_quanpin_autocorrect_transposition = tbl["quanpin"]["autocorrect_transposition"].value_or(false);
        g_quanpin_autocorrect_neighbor = tbl["quanpin"]["autocorrect_neighbor"].value_or(false);
        g_quanpin_autocorrect_deletion = tbl["quanpin"]["autocorrect_deletion"].value_or(false);
        g_quanpin_autocorrect_insertion = tbl["quanpin"]["autocorrect_insertion"].value_or(false);
        g_quanpin_autocorrect_marker = tbl["quanpin"]["autocorrect_marker"].value_or(true);
        g_fuzzy_pinyin_enabled = tbl["input"]["fuzzy_pinyin"].value_or(false);
        g_fuzzy_seeded = tbl["input"]["fuzzy_seeded"].value_or(false);
        g_fuzzy_pinyin_rules = 0;
        for (const auto &entry : kFuzzyPinyinRuleKeys)
        {
            if (tbl["input"][entry.key].value_or(false))
                g_fuzzy_pinyin_rules |= static_cast<std::uint32_t>(entry.rule);
        }
        g_floating_toolbar_enabled = tbl["general"]["floating_toolbar"].value_or(true);
        g_caret_state_indicator_enabled = tbl["general"]["caret_state_indicator"].value_or(false);
        g_caret_state_indicator_on_focus = tbl["general"]["caret_state_indicator_on_focus"].value_or(false);
        g_caret_state_indicator_position = tbl["general"]["caret_state_indicator_position"].value_or(
            std::string(FanyImeUi::kDefaultCaretStatePosition));
        if (!FanyImeUi::IsValidCaretStatePosition(g_caret_state_indicator_position))
            g_caret_state_indicator_position = FanyImeUi::kDefaultCaretStatePosition;
        // Read the old candidate-only key as a migration fallback. New writes
        // use the unified key.
        g_diagnostic_log_enabled.store(tbl["general"]["diagnostic_log"].value_or(
                                           tbl["general"]["candidate_window_diagnostic_log"].value_or(false)),
                                       std::memory_order_relaxed);
        g_tsf_diagnostic_log_enabled.store(tbl["general"]["tsf_diagnostic_log"].value_or(false),
                                           std::memory_order_relaxed);
        g_floating_toolbar_items.fullwidth = tbl["general"]["floating_toolbar_fullwidth"].value_or(true);
        g_floating_toolbar_items.punctuation = tbl["general"]["floating_toolbar_punctuation"].value_or(true);
        g_floating_toolbar_items.character_set = tbl["general"]["floating_toolbar_character_set"].value_or(true);
        g_floating_toolbar_items.settings = tbl["general"]["floating_toolbar_settings"].value_or(true);
        {
            const double scale = tbl["general"]["floating_toolbar_scale"].value_or(1.0);
            g_floating_toolbar_scale =
                scale >= kFloatingToolbarScaleMin && scale <= kFloatingToolbarScaleMax ? scale : 1.0;
            const int font_size =
                tbl["general"]["floating_toolbar_font_size"].value_or(kFloatingToolbarFontSizeDefault);
            g_floating_toolbar_font_size =
                font_size >= kFloatingToolbarFontSizeMin && font_size <= kFloatingToolbarFontSizeMax
                    ? font_size
                    : kFloatingToolbarFontSizeDefault;
            g_floating_toolbar_auto_hide = tbl["general"]["floating_toolbar_auto_hide"].value_or(false);
            const int auto_hide_delay =
                tbl["general"]["floating_toolbar_auto_hide_delay"].value_or(kFloatingToolbarAutoHideDelayDefault);
            g_floating_toolbar_auto_hide_delay = auto_hide_delay >= kFloatingToolbarAutoHideDelayMin &&
                                                         auto_hide_delay <= kFloatingToolbarAutoHideDelayMax
                                                     ? auto_hide_delay
                                                     : kFloatingToolbarAutoHideDelayDefault;
            g_floating_toolbar_shadow = tbl["general"]["floating_toolbar_shadow"].value_or(true);
        }
        g_english_candidates_enabled = tbl["general"]["cn_en_mixed_input"].value_or(false);
        g_candidate_translations_enabled = tbl["general"]["candidate_translations"].value_or(false);
        g_candidate_translation_show_pos.store(tbl["general"]["candidate_translation_show_pos"].value_or(true),
                                               std::memory_order_relaxed);
        g_candidate_translation_single_sense.store(tbl["general"]["candidate_translation_single_sense"].value_or(false),
                                                   std::memory_order_relaxed);
        {
            const int min_chars =
                tbl["general"]["cn_en_mixed_input_min_chars"].value_or(kEnglishMixedInputMinCharsDefault);
            g_english_mixed_input_min_chars =
                min_chars >= kEnglishMixedInputMinCharsMin && min_chars <= kEnglishMixedInputMinCharsMax
                    ? min_chars
                    : kEnglishMixedInputMinCharsDefault;
        }
        {
            const int count =
                tbl["general"]["cn_en_mixed_input_first_page_count"].value_or(kEnglishMixedInputFirstPageCountDefault);
            g_english_mixed_input_first_page_count =
                count >= kEnglishMixedInputFirstPageCountMin && count <= kEnglishMixedInputFirstPageCountMax
                    ? count
                    : kEnglishMixedInputFirstPageCountDefault;
        }
        g_assoc_sentence_wordlattice = tbl["association"]["sentence_wordlattice"].value_or(false);
        g_assoc_sentence_google = tbl["association"]["sentence_google"].value_or(true);
        g_assoc_sentence_neural_desktop = tbl["association"]["sentence_neural_desktop"].value_or(false);
        g_assoc_sentence_neural_keyboard = tbl["association"]["sentence_neural_keyboard"].value_or(true);
        g_assoc_context_candidate_rerank = tbl["association"]["context_candidate_rerank"].value_or(false);
        g_assoc_prefix_completion = tbl["association"]["prefix_completion"].value_or(true);
        {
            const auto chars = tbl["association"]["prefix_completion_min_chars"].value_or(
                static_cast<std::int64_t>(metasequoia::kPrefixCompletionDefaultMinimumChars));
            g_assoc_prefix_completion_min_chars =
                chars >= static_cast<std::int64_t>(metasequoia::kPrefixCompletionMinimumCharsLowest) &&
                        chars <= static_cast<std::int64_t>(metasequoia::kPrefixCompletionMinimumCharsHighest)
                    ? static_cast<std::size_t>(chars)
                    : metasequoia::kPrefixCompletionDefaultMinimumChars;
        }
        g_assoc_sentence_show_next_on_duplicate = tbl["association"]["sentence_show_next_on_duplicate"].value_or(false);
        g_assoc_sentence_collocation_enabled = tbl["association"]["sentence_collocation_enabled"].value_or(false);
        g_assoc_sentence_collocation_model = tbl["association"]["sentence_collocation_model"].value_or(std::string());
        g_assoc_sentence_collocation_weight = tbl["association"]["sentence_collocation_weight"].value_or(1.0);
        g_assoc_sentence_collocation_max_length = tbl["association"]["sentence_collocation_max_length"].value_or(6);
        g_assoc_sentence_collocation_min_length = tbl["association"]["sentence_collocation_min_length"].value_or(2);
        g_assoc_sentence_collocation_penalty = tbl["association"]["sentence_collocation_penalty"].value_or(-14.0);
        g_assoc_sentence_collocation_non_penalty =
            tbl["association"]["sentence_collocation_non_penalty"].value_or(-6.0);
        g_assoc_sentence_collocation_weak_penalty =
            tbl["association"]["sentence_collocation_weak_penalty"].value_or(-100.0);
        g_assoc_sentence_collocation_rear_penalty =
            tbl["association"]["sentence_collocation_rear_penalty"].value_or(-18.0);
        g_assoc_sentence_source_badge = tbl["association"]["sentence_source_badge"].value_or(false);
        g_emoji_mixed_input_enabled = tbl["general"]["emoji_mixed_input"].value_or(false);
        g_kaomoji_mixed_input_enabled = tbl["general"]["kaomoji_mixed_input"].value_or(false);
        g_symbol_mixed_input_enabled = tbl["general"]["symbol_mixed_input"].value_or(false);
        // 改名前的 unicode_mode 作迁移兜底；升级合并已把它搬到新键，新写入只用新键。
        g_radical_mode_enabled = tbl["utility"]["radical_mode"].value_or(tbl["utility"]["unicode_mode"].value_or(true));
        g_quick_phrase_enabled = tbl["utility"]["quick_phrase"].value_or(true);
        g_quick_phrase_candidates_enabled = tbl["utility"]["quick_phrase_candidates"].value_or(false);
        g_quick_phrase_frequency_enabled = tbl["utility"]["quick_phrase_frequency"].value_or(true);
        g_mixed_candidates_enabled = tbl["utility"]["mixed_candidates"].value_or(true);
        g_date_time_candidates_enabled = tbl["utility"]["date_time_candidates"].value_or(false);
        g_date_time_menu_enabled = tbl["utility"]["date_time_menu"].value_or(true);
        g_date_time_mode_enabled = tbl["utility"]["date_time_mode"].value_or(true);
        g_emoji_mode_enabled = tbl["utility"]["emoji_mode"].value_or(false);
        g_kaomoji_mode_enabled = tbl["utility"]["kaomoji_mode"].value_or(false);
        g_symbol_mode_enabled = tbl["utility"]["symbol_mode"].value_or(true);
        g_y_mode_enabled = tbl["utility"]["y_mode"].value_or(true);
        g_v_mode_enabled = tbl["utility"]["v_mode"].value_or(true);
        {
            const bool previous_clipboard_history = g_clipboard_history_enabled;
            static bool clipboard_history_loaded = false;
            g_clipboard_history_enabled = TomlFlexibleBool(tbl["utility"]["clipboard_history"], false);
            if (!g_clipboard_history_enabled && (previous_clipboard_history || !clipboard_history_loaded))
                ClipboardHistory::Clear();
            clipboard_history_loaded = true;
            ClipboardMonitor::Sync(g_clipboard_history_enabled);
        }
        g_language_bar_enabled = tbl["language_bar"]["enabled"].value_or(true);
        g_language_bar_show_ime_mode = tbl["language_bar"]["show_ime_mode"].value_or(true);
        g_language_bar_show_double_single_byte = tbl["language_bar"]["show_double_single_byte"].value_or(true);
        g_language_bar_show_punctuation = tbl["language_bar"]["show_punctuation"].value_or(true);
        const auto legacy_paging_mode = tbl["general"]["paging_mode"].value<std::string>();
        g_paging_minus_equal_enabled =
            tbl["general"]["paging_minus_equal"].value_or(!legacy_paging_mode || *legacy_paging_mode == "-/=");
        g_paging_comma_period_enabled =
            tbl["general"]["paging_comma_period"].value_or(legacy_paging_mode && *legacy_paging_mode == ",/.");
        g_paging_brackets_enabled = tbl["general"]["paging_brackets"].value_or(false);
        g_paging_tab_enabled =
            tbl["general"]["paging_tab"].value_or(legacy_paging_mode && *legacy_paging_mode == "Shift+Tab/Tab");
        g_paging_page_up_down_enabled = tbl["general"]["paging_page_up_down"].value_or(true);
        g_paging_mouse_wheel_enabled = tbl["general"]["paging_mouse_wheel"].value_or(false);
        g_candidate_arrow_navigation_enabled = tbl["general"]["candidate_arrow_navigation"].value_or(true);
        g_candidate_horizontal_arrow_navigation_enabled =
            tbl["general"]["candidate_horizontal_arrow_navigation"].value_or(false);
        g_word_to_character_enabled = tbl["input"]["word_to_character"].value_or(false);
        g_word_to_character_keys = tbl["input"]["word_to_character_keys"].value_or(std::string("brackets"));
        if (g_word_to_character_keys != "minus_equal")
            g_word_to_character_keys = "brackets";
        if ((g_word_to_character_keys == "brackets" ? g_paging_brackets_enabled : g_paging_minus_equal_enabled) &&
            g_word_to_character_enabled)
        {
            g_word_to_character_enabled = false;
        }
        g_smart_punctuation_enabled = tbl["input"]["smart_punctuation"].value_or(false);
        g_smart_punctuation_space_convert_enabled = tbl["input"]["smart_punctuation_space_convert"].value_or(false);
        g_smart_punctuation_direct_digit_enabled = tbl["input"]["smart_punctuation_direct_digit"].value_or(false);
        g_smart_punctuation_direct_letter_enabled = tbl["input"]["smart_punctuation_direct_letter"].value_or(false);
        g_smart_punctuation_repeat_to_chinese_enabled =
            tbl["input"]["smart_punctuation_repeat_to_chinese"].value_or(false);
        g_paired_punctuation_enabled = tbl["input"]["paired_punctuation"].value_or(true);
        g_enumeration_comma_backslash_enabled = tbl["input"]["enumeration_comma_backslash"].value_or(true);
        g_enumeration_comma_slash_enabled = tbl["input"]["enumeration_comma_slash"].value_or(true);
        // 至少留一个出顿号的键：手改成两个都关时按默认处理。
        if (!g_enumeration_comma_backslash_enabled && !g_enumeration_comma_slash_enabled)
        {
            g_enumeration_comma_backslash_enabled = true;
            g_enumeration_comma_slash_enabled = true;
        }
        {
            const std::string punctuation_lock = tbl["input"]["punctuation_lock"].value_or(std::string("follow"));
            g_punctuation_lock =
                punctuation_lock == "chinese" || punctuation_lock == "english" ? punctuation_lock : "follow";
        }
        {
            // Prefer explicit bool keys; fall back to legacy switch_language array.
            const auto legacy = tbl["keybindings"]["switch_language"].as_array();
            bool legacy_shift = true;
            bool legacy_ctrl_alt_space = true;
            if (legacy)
            {
                legacy_shift = false;
                legacy_ctrl_alt_space = false;
                for (const auto &item : *legacy)
                {
                    const auto value = item.value<std::string>();
                    if (!value)
                        continue;
                    if (*value == "Shift")
                        legacy_shift = true;
                    else if (*value == "Ctrl+Alt+Space" || *value == "Ctrl+Space")
                        legacy_ctrl_alt_space = true;
                }
            }
            g_switch_language_shift_enabled = tbl["keybindings"]["switch_language_shift"].value_or(legacy_shift);
            g_switch_language_ctrl_enabled = tbl["keybindings"]["switch_language_ctrl"].value_or(false);
            g_switch_language_ctrl_alt_space_enabled =
                tbl["keybindings"]["switch_language_ctrl_alt_space"].value_or(legacy_ctrl_alt_space);
            g_character_set_shortcut_enabled = tbl["keybindings"]["toggle_character_set_ctrl_shift_f"].value_or(true);
            g_commit_pinyin_shift_enter_enabled = tbl["keybindings"]["commit_pinyin_shift_enter"].value_or(true);
            for (std::size_t i = 0; i < g_input_state_shortcuts.size(); ++i)
            {
                const auto &definition = FanyImeInputStateShortcuts::Definitions[i];
                const auto value = tbl["keybindings"][definition.key];
                g_input_state_shortcuts[i] = value ? value.value_or(std::string(FanyImeKeyChord::InvalidValue))
                                                   : std::string(definition.defaultBinding);
            }
        }
        {
            const std::string mode = tbl["frequency_adjustment"]["mode"].value_or(std::string("promote"));
            g_frequency_adjustment.mode =
                mode == "disabled" || mode == "pin" || mode == "halve" || mode == "linear" || mode == "promote"
                    ? mode
                    : "promote";
            const int trigger = tbl["frequency_adjustment"]["trigger_count"].value_or(1);
            const int step = tbl["frequency_adjustment"]["linear_step"].value_or(1);
            g_frequency_adjustment.trigger_count = trigger >= 1 && trigger <= 10 ? trigger : 1;
            g_frequency_adjustment.linear_step = step >= 1 && step <= 10 ? step : 1;
        }
        const std::string layout = tbl["appearance"]["candidate_window_layout"].value_or(std::string("vertical"));
        g_candidate_window_layout = layout == "horizontal" ? "horizontal" : "vertical";
        g_candidate_window_follow_cursor = tbl["appearance"]["candidate_window_follow_cursor"].value_or(true);
        g_candidate_window_fit_content = tbl["appearance"]["candidate_window_fit_content"].value_or(false);
        g_candidate_window_single_line = tbl["appearance"]["candidate_window_single_line"].value_or(false);
        const std::string skin = tbl["appearance"]["candidate_skin"].value_or(std::string("fluent"));
        g_candidate_skin = IsValidCandidateSkinId(skin) ? skin : "fluent";
        {
            const std::string preedit_style =
                tbl["appearance"]["candidate_window_preedit_style"].value_or(std::string("pinyin"));
            g_candidate_window_preedit_style = preedit_style == "empty" ? "empty" : "pinyin";
        }
        g_candidate_fixed_badge = tbl["appearance"]["candidate_fixed_badge"].value_or(true);
        {
            // 非法样式一律回退到默认徽标，避免手改配置后出现无法删除的畸形标记
            const std::string badge_style =
                tbl["appearance"]["candidate_fixed_badge_style"].value_or(std::string("paperclip"));
            g_candidate_fixed_badge_style = IsValidCandidateFixedBadgeStyle(badge_style) ? badge_style : "paperclip";
        }
        {
            const std::string linger = tbl["appearance"]["settings_window_linger"].value_or(std::string("off"));
            g_settings_window_linger = IsValidSettingsWindowLinger(linger) ? linger : "off";
        }
        g_settings_launch_animation = tbl["appearance"]["settings_launch_animation"].value_or(true);
        // 配置文件缺这个键时按开启处理，与出厂默认保持一致。
        g_auto_download_update = tbl["appearance"]["auto_download_update"].value_or(true);
        {
            const std::string theme_mode = tbl["appearance"]["theme_mode"].value_or(std::string("system"));
            if (theme_mode == "light" || theme_mode == "system" || theme_mode == "auto")
                g_theme_mode = theme_mode == "auto" ? "system" : theme_mode;
            else
                g_theme_mode = "dark";
        }
        auto normalize_surface = [](const std::string &value) -> std::string {
            if (value == "light" || value == "dark" || value == "follow")
                return value;
            return "follow";
        };
        g_theme_settings = normalize_surface(tbl["appearance"]["theme_settings"].value_or(std::string("follow")));
        g_theme_cand = normalize_surface(tbl["appearance"]["theme_cand"].value_or(std::string("follow")));
        g_theme_ftb = normalize_surface(tbl["appearance"]["theme_ftb"].value_or(std::string("follow")));
        g_theme_menu = normalize_surface(tbl["appearance"]["theme_menu"].value_or(std::string("follow")));
        {
            const std::string tsf_preedit_style = tbl["appearance"]["tsf_preedit_style"].value_or(
                tbl["input"]["tsf_preedit_style"].value_or(std::string("raw")));
            g_tsf_preedit_style = GlobalSettings::normalizeTsfPreeditStyle(tsf_preedit_style);
            RefreshEffectiveTsfPreeditStyle();
        }
        {
            NetworkProxyConfig proxy;
            proxy.mode = tbl["network"]["proxy_mode"].value_or(std::string("system"));
            if (proxy.mode != "system" && proxy.mode != "none" && proxy.mode != "custom")
                proxy.mode = "system";
            proxy.server = NormalizeNetworkProxyServer(tbl["network"]["proxy_server"].value_or(std::string()));
            std::lock_guard<std::mutex> proxy_lock(g_network_proxy_mutex);
            g_network_proxy = std::move(proxy);
        }
        RememberConfigWriteTime();
        return true;
    }
    catch (const toml::parse_error &)
    {
#ifdef FANY_DEBUG
        (void)0;
#endif
        return false;
    }
}

} // namespace ime_config_detail

namespace ime_config_detail
{
SchemeType ParseScheme(const std::string &value)
{
    // 只保留全拼：已删除的 japanese / romaji / wubi / shuangpin 等旧配置一律静默回退
    // 全拼，升级用户不会卡在一个不存在的方案上。
    (void)value;
    return SchemeType::Quanpin;
}
} // namespace ime_config_detail

void InvalidateImeConfigWriteTime()
{
    g_config_last_write_time.reset();
}

void InitImeConfig()
{
    // Build the path from the wide accessor: std::filesystem::path(std::string) decodes with the
    // system ANSI code page, which corrupts a non-ASCII (e.g. Chinese) user profile path on a
    // non-UTF-8 ACP machine and makes every config read/write fail ("设置保存失败").
    g_config_path = std::filesystem::path(CommonUtils::get_ime_config_dir_w()) / L"config.toml";
    std::error_code create_error;
    std::filesystem::create_directories(g_config_path.parent_path(), create_error);
    CommonUtils::ensure_ime_data_writable();
    RecoverLegacyAcpMangledConfig();
    SyncConfigWithInstalledTemplate();
    if (LoadImeConfig())
    {
        ProtectPlaintextCredentials();
#ifdef FANY_DEBUG
        (void)0;
#endif
#ifdef FANY_DEBUG
        (void)0;
#endif
#ifdef FANY_DEBUG
        (void)0;
#endif
#ifdef FANY_DEBUG
        (void)0;
#endif
    }
}

bool ReloadImeConfigIfChanged()
{
    std::error_code error;
    const auto write_time = std::filesystem::last_write_time(g_config_path, error);
    bool config_changed = !error && !(g_config_last_write_time && write_time == *g_config_last_write_time);
    if (!error && !config_changed)
    {
        // 修改时间没变不等于内容没变：写入是「临时文件 + MoveFileEx 替换」，连续两次写入可能落在
        // 同一个时间刻度上。用大小和内容摘要复核一次，避免切换方案这类紧跟在另一次写入之后
        // 的配置变更被当成「没动」而跳过，表现出来就是要重启才生效。
        std::error_code size_error;
        const auto size = std::filesystem::file_size(g_config_path, size_error);
        if (!size_error && g_config_last_size && size != *g_config_last_size)
        {
            // 大小变了就一定变了，不必再算摘要。
            config_changed = true;
        }
        else if (!size_error && g_config_last_content_hash && g_config_last_size && size == *g_config_last_size &&
                 HashFileContents(g_config_path) != *g_config_last_content_hash)
        {
            // 大小没变再看内容；读不到大小或从没记过摘要时不据此判定变化，
            // 否则会退化成每次轮询都全量重载。
            config_changed = true;
        }
    }
    if (!config_changed)
    {
        return false;
    }
    return LoadImeConfig();
}

const std::filesystem::path &GetImeConfigPath()
{
    return g_config_path;
}

const std::string &GetConfiguredSessionBackend()
{
    return g_session_backend;
}
