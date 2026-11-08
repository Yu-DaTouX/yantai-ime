#pragma once

#include "ai/ai_provider_store.h"
#include "engine/core/fuzzy_pinyin_options.h"
#include "engine/core/prefix_completion_options.h"
#include "engine/core/scheme_type.h"
#include "engine/contracts/input_state_shortcuts.h"
#include <toml++/toml.h>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct VoiceInputConfig
{
    bool enabled = false;
    bool hotkey_ralt = true;
    bool hotkey_ctrl_f9 = true;
    bool hotkey_ctrl_win = false;
    bool hotkey_rctrl_ralt = false;
    bool hotkey_hold_space_lock = true;
    // doubao | openai | siliconflow | groq
    std::string asr_provider = "doubao";
    // Doubao console generation: api_key (new console, single API Key) | legacy (App ID + Access Token).
    // Empty means "infer from asr_app_key", which keeps configs written before this setting working.
    std::string doubao_auth_mode = "api_key";
    std::string asr_app_key;
    std::string asr_token;
    // Per-provider tokens so switching ASR/polish providers restores the matching key.
    std::map<std::string, std::string> asr_tokens;
    std::string asr_endpoint = "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel_async";
    std::string asr_resource_id = "volc.seedasr.sauc.duration";
    bool doubao_enable_itn = true;
    bool doubao_enable_punc = true;
    bool doubao_enable_ddc = false;
    std::string doubao_boosting_table_id;
    // OpenAI-compatible transcriptions model. Unused for doubao streaming.
    std::string asr_model;
    // siliconflow | openai | deepseek | groq
    std::string polish_provider = "siliconflow";
    std::string polish_token;
    std::map<std::string, std::string> polish_tokens;
    std::string polish_endpoint = "https://api.siliconflow.cn/v1/chat/completions";
    std::string polish_model;
    // DeepSeek reasoning effort: disabled | low | high | max. Other providers ignore it.
    std::string polish_thinking = "disabled";
    // cleanup | faithful | zh2en | casual | custom_1 | custom_2 | custom_3
    std::string polish_prompt_id = "cleanup";
    // Legacy single custom prompt. Retained for config compatibility.
    std::string polish_prompt;
    std::string polish_prompt_custom_1;
    std::string polish_prompt_custom_2;
    std::string polish_prompt_custom_3;
    std::string language = "zh-cn";
    bool start_sound = true;
    bool end_sound = true;
    // Temporarily mute other apps' playback while the microphone is recording.
    bool mute_system_audio = false;
    bool polish_text = false;
    // When true, Doubao streaming partials are shown as TSF inline preedit.
    bool stream_inline_preedit = false;
    // tsf | sendinput | ctrl_v
    std::string commit_mode = "tsf";
};

struct AiAssistantConfig
{
    bool enabled = false;
    // Active provider id. The legacy set is deepseek | openai | siliconflow | groq | custom; ids
    // beyond that come from the provider registry in the data directory.
    std::string provider = "deepseek";
    // 生成式模式下最后选过的服务商。provider 切到 jev 时它保持不变，切回生成式时用它恢复。
    std::string generate_provider = "deepseek";
    std::string token;
    std::map<std::string, std::string> tokens;
    std::string endpoint = "https://api.deepseek.com/chat/completions";
    std::string model = "deepseek-v4-flash";
    // The provider registry (ai_providers.json in the data dir): any number of user-defined
    // OpenAI-compatible services, HanaAgent-style. The legacy maps below keep the config.toml
    // slots alive for rollback; requests resolve through `active`.
    std::vector<AiAssistant::AiProvider> providers;
    AiAssistant::AiProvider active;
    // 这两份默认值的唯一权威在 ai_provider_store.cpp 的 BuiltinDefaults()，设置页也从那里取。
    // 结构体默认成员初始化器没法调用函数，只能在这里再写一遍；tests/src/test_ai_provider_store.cpp
    // 的 ai_provider_store_defaults_match_the_config_struct 用例钉住两份一致。
    std::map<std::string, std::string> endpoints{
        {"deepseek", "https://api.deepseek.com/chat/completions"},
        {"openai", "https://api.openai.com/v1/chat/completions"},
        {"siliconflow", "https://api.siliconflow.cn/v1/chat/completions"},
        {"groq", "https://api.groq.com/openai/v1/chat/completions"},
        {"minimax", "https://api.minimax.cn/v1/chat/completions"},
        {"moonshot", "https://api.moonshot.cn/v1/chat/completions"},
        {"zhipu", "https://open.bigmodel.cn/api/paas/v4/chat/completions"},
        {"dashscope", "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"},
        {"volcengine", "https://ark.cn-beijing.volces.com/api/v3/chat/completions"},
        {"openrouter", "https://openrouter.ai/api/v1/chat/completions"},
        {"jev", "https://api.typesafe.ai/v1/systemone"},
        {"ollama", "http://localhost:11434/v1/chat/completions"},
        {"custom", ""}};
    // Groq 已弃用 llama-3.3-70b-versatile，默认模型改用 qwen/qwen3.8-27b。
    // custom 槽位不预设接口地址和模型，全部由用户填写并单独保存。ollama 不预设模型：本机装了哪些模型因人而异。
    std::map<std::string, std::string> models{{"deepseek", "deepseek-v4-flash"},
                                              {"openai", "gpt-4o-mini"},
                                              {"siliconflow", "Qwen/Qwen3-8B"},
                                              {"groq", "qwen/qwen3.8-27b"},
                                              {"minimax", "MiniMax-M3.1-Flash-Preview"},
                                              {"moonshot", "kimi-k2-0905-preview"},
                                              {"zhipu", "glm-4-flash"},
                                              {"dashscope", "qwen-plus"},
                                              {"volcengine", "doubao-seed-1.6-flash"},
                                              {"openrouter", "openrouter/auto"},
                                              {"jev", "jev-1.13.0"},
                                              {"ollama", ""},
                                              {"custom", ""}};
    int candidate_limit = 3;
    // DeepSeek reasoning effort: disabled | low | high | max. Other providers ignore it.
    std::string thinking = "disabled";
    // response_format 策略：auto | force | off，见 ChatCompletions::JsonMode。AI 联想与候选词
    // 翻译共用这一项，两边对同一个端点的请求形状保持一致。
    std::string json_mode = "auto";
    // custom_1 | custom_2 | custom_3
    std::string prompt_id = "custom_1";
    std::string prompt_custom_1;
    std::string prompt_custom_2;
    std::string prompt_custom_3;
    // Resolved active prompt. `prompt` is also retained as the legacy slot-one key.
    std::string prompt =
        R"PROMPT(你是一个中文全拼输入法联想引擎。输入为已经切分好的拼音数组、前文上下文和候选数量。

优先生成与拼音严格对应的中文候选：若有 N 段拼音，首选必须尽量为 N 个汉字，每段拼音对应一个汉字，不得随意增删或改变读音。结合上下文、常用程度、语义完整性和固定搭配排序。

若去掉分词后能明显组成更合理的英文单词、缩写、产品名或技术术语，如 `deep + seek → DeepSeek`、`git + hub → GitHub`，可优先返回英文；不要生造英文或做牵强匹配。

只输出合法 JSON，不要解释或输出 Markdown：

{
"candidates": [
{
"text": "候选内容",
"type": "chinese或english",
"confidence": 0.98
}
]
}

候选按推荐程度降序排列，数量不超过指定上限；没有合理结果时返回空数组。)PROMPT";
};

struct TencentTmtConfig
{
    bool enabled = false;
    std::string secret_id;
    std::string secret_key;
    std::string region = "ap-guangzhou";
    // Language shown beside Chinese candidates. Tencent TMT language codes:
    // en / fr / ja / es / ru / de / ko / th / vi / it.
    std::string target_language = "en";
};

struct AliyunMtConfig
{
    bool enabled = false;
    // 键名沿用 secret_id / secret_key，才会被 IsCredentialKey 加密保存。
    std::string secret_id;
    std::string secret_key;
};

struct CustomTranslationConfig
{
    bool enabled = false;
    // Full URL of a DeepLX-compatible POST endpoint.
    std::string endpoint;
    // Optional bearer token used by private or protected deployments.
    std::string api_key;
};

struct NiuTransConfig
{
    bool enabled = false;
    // 控制台->API应用 中的应用唯一标识与 apikey。
    std::string app_id;
    std::string apikey;
};

// 候选词翻译读取的整份配置快照。provider 来自 [translation].provider，旧配置没有这个键时由
// [tencent_tmt]/[aliyun_mt]/[custom_translation]/[niutrans] 的 enabled 布尔在加载时推导；其余字段是各提供方
// 配置的副本，ai 复用 AI 辅助当前服务商。配置加载和相关设置写入后整份替换，翻译 worker 与
// 候选线程只读快照，不直接读会被重载改写的各段全局字符串。
struct TranslationConfig
{
    // tencent | aliyun | niutrans | custom | ai
    std::string provider = "tencent";
    TencentTmtConfig tencent;
    AliyunMtConfig aliyun;
    CustomTranslationConfig custom;
    NiuTransConfig niutrans;
    struct Ai
    {
        std::string provider;
        std::string endpoint;
        std::string token;
        std::string model;
        // 从 [ai_assistant].json_mode 带过来，翻译请求与 AI 联想用同一条策略。
        std::string json_mode = "auto";
    } ai;
};

// [network] 段：云输入、模型下载、AI、翻译、语音等所有出站请求共用的代理设置。
struct NetworkProxyConfig
{
    std::string mode = "system"; // system（跟随系统）| none（直连）| custom（自定义 HTTP 代理）
    std::string server;          // custom 模式的 HTTP 代理，已规范化为 host:port
};

struct FrequencyAdjustmentConfig
{
    std::string mode = "promote"; // disabled | pin | halve | linear | promote
    int trigger_count = 1;
    int linear_step = 1;
};

void InitImeConfig();
void InvalidateImeConfigWriteTime();
void NotifyImeServerConfigChanged();
void NotifyImeServerCandidateSkinRefresh();
void NotifyImeServerInputSchemeChanged();
// 请求输入法后台进程重启（由 Watchdog 拉起）。返回是否成功送达。
bool NotifyImeServerRestart();
// 请求输入法后台进程以 stop exit code 退出（Watchdog 一起退出）。导入备份前用。
bool NotifyImeServerStopForBackup();
// 升级用：以新版模板为骨架重建配置。用户改过的值（与 baseline 中的旧默认值不同）保留，
// 其余跟随新默认值；模板里没有的旧键被丢弃。baseline 为空时一律保留用户值。
std::string MergeConfigIntoTemplate(const std::string &template_text, const std::string &user_text,
                                    const std::string &baseline_text);
bool ReloadImeConfigIfChanged();
const std::filesystem::path &GetImeConfigPath();
const std::string &GetConfiguredSessionBackend();
int GetConfiguredCandidatePageSize();
bool SetConfiguredCandidatePageSize(int page_size);
const std::string &GetConfiguredCandidateFont();
bool SetConfiguredCandidateFont(const std::string &font);
const std::string &GetConfiguredCandidateEnglishFont();
bool SetConfiguredCandidateEnglishFont(const std::string &font);
const std::string &GetConfiguredCandidateDefaultFont();
const std::vector<std::string> &GetConfiguredCandidateFallbackFonts();
bool SetConfiguredCandidateFallbackFonts(const std::vector<std::string> &fonts);
std::vector<std::string> GetConfiguredCandidateFallbackFontFamilies();
bool SetConfiguredCandidateDefaultFont(const std::string &font);
int GetConfiguredCandidateFontSize();
bool SetConfiguredCandidateFontSize(int font_size);
int GetConfiguredCandidateWindowPreeditFontSize();
bool SetConfiguredCandidateWindowPreeditFontSize(int font_size);
bool GetConfiguredDiagnosticLogEnabled();
bool SetConfiguredDiagnosticLogEnabled(bool enabled);
bool GetConfiguredTsfDiagnosticLogEnabled();
bool SetConfiguredTsfDiagnosticLogEnabled(bool enabled);
// 统计保留策略：forever|30d|90d|180d|365d，默认 forever。缺键或非法值一律回落
// forever——坏配置绝不能导致自动清理误删数据；setter 校验枚举才落盘。
const std::string &GetConfiguredCandidateTextColor();
bool SetConfiguredCandidateTextColor(const std::string &color);
// Installed font family names for settings dropdowns (cached after first call).
const std::vector<std::string> &GetSystemFontFamilies();
// Resolve a GDI/legacy face name (for example "Family W03") to the
// typographic family name Chromium expects in CSS. Returns the input on failure.
std::string ResolveSystemFontFamilyForCss(const std::string &font);
SchemeType GetConfiguredInputScheme();
std::string GetConfiguredInputSchemeName();
bool SetConfiguredInputScheme(const std::string &scheme);
const std::string &GetConfiguredCharacterSet();
bool SetConfiguredCharacterSet(const std::string &character_set);
bool GetConfiguredCharacterSetShortcutEnabled();
bool SetConfiguredCharacterSetShortcutEnabled(bool enabled);
// Shift+Enter commits the converted spelling. Turning it off leaves the chord to the
// plain Enter path, which commits the raw input.
bool GetConfiguredPinyinCommitShortcutEnabled();
bool SetConfiguredPinyinCommitShortcutEnabled(bool enabled);
// "chinese" | "english" — default CN/EN when activating this IME.
const std::string &GetConfiguredDefaultImeMode();
bool SetConfiguredDefaultImeMode(const std::string &mode);
// "app" | "global" — per-app memory vs unified CN/EN across apps.
const std::string &GetConfiguredImeModeScope();
bool SetConfiguredImeModeScope(const std::string &scope);
bool IsConfiguredImeModeScopeGlobal();
bool GetConfiguredSwitchLanguageShiftEnabled();
bool SetConfiguredSwitchLanguageShiftEnabled(bool enabled);
bool GetConfiguredSwitchLanguageCtrlEnabled();
bool SetConfiguredSwitchLanguageCtrlEnabled(bool enabled);
bool GetConfiguredSwitchLanguageCtrlAltSpaceEnabled();
bool SetConfiguredSwitchLanguageCtrlAltSpaceEnabled(bool enabled);
using InputStateShortcutValues = std::array<std::string, FanyImeInputStateShortcuts::Definitions.size()>;
// Views over values for the contract's Validate/Resolve; valid while values lives.
inline FanyImeInputStateShortcuts::Values InputStateShortcutViews(const InputStateShortcutValues &values)
{
    FanyImeInputStateShortcuts::Values views;
    for (std::size_t i = 0; i < views.size(); ++i)
        views[i] = values[i];
    return views;
}
const InputStateShortcutValues &GetConfiguredInputStateShortcuts();
bool SetConfiguredInputStateShortcut(std::size_t index, const std::string &binding);
// 造词时 Esc 只清未选拼音、保留已选词（小狼毫风格）；关闭时 Esc 直接整体取消。
bool GetConfiguredEscapeKeepsSelectedWord();
bool SetConfiguredEscapeKeepsSelectedWord(bool enabled);
bool GetConfiguredEnterLearnsEnglishWord();
bool SetConfiguredEnterLearnsEnglishWord(bool enabled);
const std::string &GetConfiguredTsfPreeditStyle();
bool SetConfiguredTsfPreeditStyle(const std::string &style);
bool GetConfiguredQuanpinHelpcodeEnabled();
bool SetConfiguredQuanpinHelpcodeEnabled(bool enabled);
bool GetConfiguredQuanpinAutocorrectTransposition();
bool SetConfiguredQuanpinAutocorrectTransposition(bool enabled);
bool GetConfiguredQuanpinAutocorrectNeighbor();
bool SetConfiguredQuanpinAutocorrectNeighbor(bool enabled);
bool GetConfiguredQuanpinAutocorrectDeletion();
bool SetConfiguredQuanpinAutocorrectDeletion(bool enabled);
bool GetConfiguredQuanpinAutocorrectInsertion();
bool SetConfiguredQuanpinAutocorrectInsertion(bool enabled);
bool GetConfiguredQuanpinAutocorrectMarker();
bool SetConfiguredQuanpinAutocorrectMarker(bool enabled);
// Fuzzy pinyin rules are flat [input] booleans ("fuzzy_z_zh", ...) so the settings page can
// bind them per key and the upgrade merge keeps user choices per key. Callers get the
// synthesized engine options and never touch the bitmask themselves.
// The master switch ("fuzzy_pinyin") gates GetConfiguredFuzzyPinyinOptions only: sessions see
// all-zero rules while it is off, but the stored rule choices stay intact so flipping it back
// on restores them.
// First enable seeds: SetConfiguredFuzzyPinyinEnabled(true) while the internal marker key
// "fuzzy_seeded" is still false batch-writes all 11 rules true plus the marker in one atomic
// file write. Later toggles only touch the master switch, so a user's pruned selection
// survives disable/enable cycles; the marker also prevents re-seeding an intentionally empty
// selection. The marker is never sent to the settings page.
bool GetConfiguredFuzzyPinyinEnabled();
bool SetConfiguredFuzzyPinyinEnabled(bool enabled);
metasequoia::FuzzyPinyinOptions GetConfiguredFuzzyPinyinOptions();
// Rule bits without the master gate, for the settings snapshot: checkboxes must show the
// stored choices even while the master switch is off.
metasequoia::FuzzyPinyinOptions GetConfiguredFuzzyPinyinRuleStates();
// key is the config key without section, e.g. "fuzzy_n_l"; unknown keys return false.
bool SetConfiguredFuzzyPinyinRule(const std::string &key, bool enabled);
const std::string &GetConfiguredQuanpinHelpcodeSchema();
bool SetConfiguredQuanpinHelpcodeSchema(const std::string &schema);
// User helpcode tables under <data>/helpcodes/custom, rescanned on every call so a file dropped
// there shows up the next time the settings page asks. Names fall back to the file stem.
struct CustomHelpcodeSchemaInfo
{
    std::string schema;
    std::string name;
    std::string name_en;
};
std::vector<CustomHelpcodeSchemaInfo> GetCustomHelpcodeSchemas();
// UTF-8 path of the folder GetCustomHelpcodeSchemas scans.
std::string GetCustomHelpcodeDirectory();
bool GetConfiguredShowQuanpinHelpcodeInCandidateWindow();
bool SetConfiguredShowQuanpinHelpcodeInCandidateWindow(bool enabled);
bool GetConfiguredFloatingToolbarEnabled();
bool SetConfiguredFloatingToolbarEnabled(bool enabled);
bool GetConfiguredCaretStateIndicatorEnabled();
bool SetConfiguredCaretStateIndicatorEnabled(bool enabled);
// Also announce the current mode when focus moves into another text field,
// in the same window or another one. Only meaningful while the indicator
// itself is enabled.
bool GetConfiguredCaretStateIndicatorOnFocus();
bool SetConfiguredCaretStateIndicatorOnFocus(bool enabled);
const std::string &GetConfiguredCaretStateIndicatorPosition();
bool SetConfiguredCaretStateIndicatorPosition(const std::string &position);
struct FloatingToolbarItemsConfig
{
    bool fullwidth = true;
    bool punctuation = true;
    bool character_set = true;
    bool settings = true;
};
const FloatingToolbarItemsConfig &GetConfiguredFloatingToolbarItems();
bool SetConfiguredFloatingToolbarItemEnabled(const std::string &item, bool enabled);
// User scale independent of system DPI (0.75 / 1.0 / 1.25 / 1.5).
double GetConfiguredFloatingToolbarScale();
bool SetConfiguredFloatingToolbarScale(double scale);
// Icon box size in CSS px before user scale (16–28, default 24).
int GetConfiguredFloatingToolbarFontSize();
bool SetConfiguredFloatingToolbarFontSize(int font_size);
// Fade the toolbar out after it has been shown this many seconds (1–60,
// default 5) without hover; a CN/EN-style state change shows it again.
bool GetConfiguredFloatingToolbarAutoHide();
bool SetConfiguredFloatingToolbarAutoHide(bool enabled);
int GetConfiguredFloatingToolbarAutoHideDelay();
bool SetConfiguredFloatingToolbarAutoHideDelay(int seconds);
// Draw the toolbar's drop shadow (default on). Either way the bar itself can
// sit flush with a screen edge; the shadow margin may hang off the screen.
bool GetConfiguredFloatingToolbarShadow();
bool SetConfiguredFloatingToolbarShadow(bool enabled);
bool GetConfiguredEnglishCandidatesEnabled();
bool SetConfiguredEnglishCandidatesEnabled(bool enabled);
bool GetConfiguredCandidateTranslationsEnabled();
bool SetConfiguredCandidateTranslationsEnabled(bool enabled);
bool GetConfiguredCandidateTranslationShowPos();
bool SetConfiguredCandidateTranslationShowPos(bool show);
bool GetConfiguredCandidateTranslationSingleSense();
bool SetConfiguredCandidateTranslationSingleSense(bool single);
int GetConfiguredEnglishMixedInputMinChars();
bool SetConfiguredEnglishMixedInputMinChars(int min_chars);
int GetConfiguredEnglishMixedInputFirstPageCount();
bool SetConfiguredEnglishMixedInputFirstPageCount(int count);
bool GetConfiguredEmojiMixedInputEnabled();
bool SetConfiguredEmojiMixedInputEnabled(bool enabled);
bool GetConfiguredKaomojiMixedInputEnabled();
bool SetConfiguredKaomojiMixedInputEnabled(bool enabled);
// 全拼里输入恰好等于某个符号编码（alpha、szq、xl）时，把符号混进候选。
bool GetConfiguredSymbolMixedInputEnabled();
bool SetConfiguredSymbolMixedInputEnabled(bool enabled);
bool GetConfiguredCloudCandidatesEnabled();
bool SetConfiguredCloudCandidatesEnabled(bool enabled);
// 整句候选来源与去重补位开关（[association] 段），默认全关。
bool GetConfiguredAssocSentenceWordLattice();
bool SetConfiguredAssocSentenceWordLattice(bool enabled);
bool GetConfiguredAssocSentenceGoogle();
bool SetConfiguredAssocSentenceGoogle(bool enabled);
bool GetConfiguredAssocSentenceNeuralDesktop();
bool SetConfiguredAssocSentenceNeuralDesktop(bool enabled);
bool GetConfiguredAssocSentenceNeuralKeyboard();
bool SetConfiguredAssocSentenceNeuralKeyboard(bool enabled);
bool GetConfiguredAssocContextCandidateRerank();
bool SetConfiguredAssocContextCandidateRerank(bool enabled);
// 长词前缀补全（#1343）：开关（prefix_completion，默认开）与首选的最少字数
// （prefix_completion_min_chars，3~8，默认 4，只在配置文件里调）合成会话选项。
bool GetConfiguredAssocPrefixCompletion();
bool SetConfiguredAssocPrefixCompletion(bool enabled);
metasequoia::PrefixCompletionOptions GetConfiguredPrefixCompletionOptions();
bool GetConfiguredAssocSentenceShowNextOnDuplicate();
bool SetConfiguredAssocSentenceShowNextOnDuplicate(bool enabled);
bool GetConfiguredAssocSentenceSourceBadge();
bool SetConfiguredAssocSentenceSourceBadge(bool enabled);
// octagram 语法模型（八股文，[association] 段）：两个机制开关进设置页，模型 id 默认留空
// = 未选择任何模型（设置页单选激活后写入），权重与六参数是配置文件里的高级调参项，不进设置页。
bool GetConfiguredAssocSentenceCollocationEnabled();
bool SetConfiguredAssocSentenceCollocationEnabled(bool enabled);
std::string GetConfiguredAssocSentenceCollocationModel();
bool SetConfiguredAssocSentenceCollocationModel(const std::string &model_id);
double GetConfiguredAssocSentenceCollocationWeight();
int GetConfiguredAssocSentenceCollocationMaxLength();
int GetConfiguredAssocSentenceCollocationMinLength();
double GetConfiguredAssocSentenceCollocationPenalty();
double GetConfiguredAssocSentenceCollocationNonPenalty();
double GetConfiguredAssocSentenceCollocationWeakPenalty();
double GetConfiguredAssocSentenceCollocationRearPenalty();
// Shift+U 部件拆字反查（U 模式）。
bool GetConfiguredRadicalModeEnabled();
bool SetConfiguredRadicalModeEnabled(bool enabled);
// Shift+K 快捷短语模式。
bool GetConfiguredQuickPhraseEnabled();
bool SetConfiguredQuickPhraseEnabled(bool enabled);
// 全拼里把编码精确匹配的快捷短语混进候选，与 K 模式各自开关。
bool GetConfiguredQuickPhraseCandidatesEnabled();
bool SetConfiguredQuickPhraseCandidatesEnabled(bool enabled);
// 快捷短语参与调频：关掉后快捷短语组固定在首位，选择也不再学习。
bool GetConfiguredQuickPhraseFrequencyEnabled();
bool SetConfiguredQuickPhraseFrequencyEnabled(bool enabled);
// 候选混输总开关：关掉后英文、emoji、颜文字、快捷短语、日期时间都不再混进候选，各自的开关值保留。
bool GetConfiguredMixedCandidatesEnabled();
bool SetConfiguredMixedCandidatesEnabled(bool enabled);
// 全拼里输入 rq / sj / xq 等唤醒词时，把当前日期、时间、星期混进候选。
bool GetConfiguredDateTimeCandidatesEnabled();
bool SetConfiguredDateTimeCandidatesEnabled(bool enabled);
// 日期时间混入候选时，第三位的「📅日期」入口：选中后候选框换成全部日期格式。依附 date_time_candidates。
bool GetConfiguredDateTimeMenuEnabled();
bool SetConfiguredDateTimeMenuEnabled(bool enabled);
bool GetConfiguredDateTimeModeEnabled();
bool SetConfiguredDateTimeModeEnabled(bool enabled);
bool GetConfiguredEmojiModeEnabled();
bool SetConfiguredEmojiModeEnabled(bool enabled);
bool GetConfiguredKaomojiModeEnabled();
bool SetConfiguredKaomojiModeEnabled(bool enabled);
// Shift+S 符号快捷输入模式。
bool GetConfiguredSymbolModeEnabled();
bool SetConfiguredSymbolModeEnabled(bool enabled);
bool GetConfiguredYModeEnabled();
bool SetConfiguredYModeEnabled(bool enabled);
// V 模式：数字转中文、算式计算。全拼大小写 V 都可以，规则见
// engine/contracts/v_mode_input.h。
bool GetConfiguredVModeEnabled();
bool SetConfiguredVModeEnabled(bool enabled);
// VModeChanged 的载荷："0" 关着，"2" 全拼（V、v 都认）。
std::wstring FormatVModeWorkerPayload();
bool GetConfiguredClipboardHistoryEnabled();
bool SetConfiguredClipboardHistoryEnabled(bool enabled);
bool GetConfiguredLanguageBarEnabled();
bool SetConfiguredLanguageBarEnabled(bool enabled);
bool GetConfiguredLanguageBarShowImeMode();
bool SetConfiguredLanguageBarShowImeMode(bool enabled);
bool GetConfiguredLanguageBarShowDoubleSingleByte();
bool SetConfiguredLanguageBarShowDoubleSingleByte(bool enabled);
bool GetConfiguredLanguageBarShowPunctuation();
bool SetConfiguredLanguageBarShowPunctuation(bool enabled);
bool GetConfiguredPagingMinusEqualEnabled();
bool SetConfiguredPagingMinusEqualEnabled(bool enabled);
bool GetConfiguredPagingCommaPeriodEnabled();
bool SetConfiguredPagingCommaPeriodEnabled(bool enabled);
bool GetConfiguredPagingBracketsEnabled();
bool SetConfiguredPagingBracketsEnabled(bool enabled);
// Worker payload for PagingCommaPeriodChanged: "0|raw" / "1|pinyin" / "0|empty".
// Legacy TSF only reads data[0] as the paging flag and ignores the rest.
std::wstring FormatPagingCommaPeriodWorkerPayload();
bool GetConfiguredPagingTabEnabled();
bool SetConfiguredPagingTabEnabled(bool enabled);
bool GetConfiguredPagingPageUpDownEnabled();
bool SetConfiguredPagingPageUpDownEnabled(bool enabled);
// Mouse wheel over the candidate window. Default off: the wheel only reaches a
// never-focused NOACTIVATE host when Windows' "scroll inactive windows when I
// hover over them" is enabled, so this cannot be promised unconditionally.
bool GetConfiguredPagingMouseWheelEnabled();
bool SetConfiguredPagingMouseWheelEnabled(bool enabled);
bool GetConfiguredCandidateArrowNavigationEnabled();
bool SetConfiguredCandidateArrowNavigationEnabled(bool enabled);
// 横排候选时左右方向键移动高亮（general.candidate_horizontal_arrow_navigation），默认关；
// 只在候选窗布局为 horizontal 时生效，生效与否经 HorizontalArrowSelectionChanged 发给 TSF。
bool GetConfiguredCandidateHorizontalArrowNavigationEnabled();
bool SetConfiguredCandidateHorizontalArrowNavigationEnabled(bool enabled);
// Worker payload for HorizontalArrowSelectionChanged: "1" when the setting is on and the layout is horizontal.
std::wstring FormatHorizontalArrowSelectionWorkerPayload();
bool GetConfiguredWordToCharacterEnabled();
bool SetConfiguredWordToCharacterEnabled(bool enabled);
std::string GetConfiguredWordToCharacterKeys();
bool SetConfiguredWordToCharacterKeys(const std::string &keys);
bool GetConfiguredSmartPunctuationEnabled();
bool SetConfiguredSmartPunctuationEnabled(bool enabled);
bool GetConfiguredSmartPunctuationSpaceConvertEnabled();
bool SetConfiguredSmartPunctuationSpaceConvertEnabled(bool enabled);
bool GetConfiguredSmartPunctuationDirectDigitEnabled();
bool SetConfiguredSmartPunctuationDirectDigitEnabled(bool enabled);
bool GetConfiguredSmartPunctuationDirectLetterEnabled();
bool SetConfiguredSmartPunctuationDirectLetterEnabled(bool enabled);
bool GetConfiguredSmartPunctuationRepeatToChineseEnabled();
bool SetConfiguredSmartPunctuationRepeatToChineseEnabled(bool enabled);
bool GetConfiguredPairedPunctuationEnabled();
bool SetConfiguredPairedPunctuationEnabled(bool enabled);
// 顿号自定义：中文标点下 \（input.enumeration_comma_backslash）和 /（input.enumeration_comma_slash）是否出「、」，
// 新装默认都开（升级用户的 / 由模板合并补成关），至少留一个，关掉最后一个的写入不收。
bool GetConfiguredEnumerationCommaBackslashEnabled();
bool SetConfiguredEnumerationCommaBackslashEnabled(bool enabled);
bool GetConfiguredEnumerationCommaSlashEnabled();
bool SetConfiguredEnumerationCommaSlashEnabled(bool enabled);
// Worker payload for EnumerationCommaKeysChanged (FanyImeEnumerationComma::PayloadFor).
std::wstring FormatEnumerationCommaWorkerPayload();
// "follow" | "chinese" | "english" — punctuation stays put when switching CN/EN.
const std::string &GetConfiguredPunctuationLock();
bool SetConfiguredPunctuationLock(const std::string &lock);
// Worker payload for PunctuationLockChanged: "0" follow / "1" Chinese / "2" English.
std::wstring FormatPunctuationLockWorkerPayload();
const std::string &GetConfiguredCandidateWindowLayout();
bool SetConfiguredCandidateWindowLayout(const std::string &layout);
bool GetConfiguredCandidateWindowFollowCursor();
bool SetConfiguredCandidateWindowFollowCursor(bool enabled);
// 候选框严格按文字宽度收紧；与皮肤 skin.toml 的 fit_content 取或，任一打开即生效。
bool GetConfiguredCandidateWindowFitContent();
bool SetConfiguredCandidateWindowFitContent(bool enabled);
// 横排强制单行：候选框最宽放开到当前屏幕宽度（默认半屏），超出才换行；竖排不受影响。
bool GetConfiguredCandidateWindowSingleLine();
bool SetConfiguredCandidateWindowSingleLine(bool enabled);
// 固定排位候选词徽标：总开关 + 样式枚举（"paperclip" | "pushpin" | "dot"）。
bool GetConfiguredCandidateFixedBadge();
bool SetConfiguredCandidateFixedBadge(bool enabled);
const std::string &GetConfiguredCandidateFixedBadgeStyle();
bool SetConfiguredCandidateFixedBadgeStyle(const std::string &style);
// AI 联想候选词的来源徽标样式（"robot" | "whale"），出厂 "robot"（🤖）。
const std::string &GetConfiguredCandidateAiBadgeStyle();
bool SetConfiguredCandidateAiBadgeStyle(const std::string &style);
// 设置窗口关闭后隐藏驻留多久再退出进程：
// "off" | "1m" | "5m" | "10m" | "30m" | "60m" | "forever"，默认 "10m"。
const std::string &GetConfiguredSettingsWindowLinger();
bool SetConfiguredSettingsWindowLinger(const std::string &linger);
// 设置窗口的齿轮启动画面，出厂开启。
bool GetConfiguredSettingsLaunchAnimation();
bool SetConfiguredSettingsLaunchAnimation(bool enabled);
// 发现新版本时由后台自动下载安装包并校验 SHA256，出厂关闭。
bool GetConfiguredAutoDownloadUpdate();
bool SetConfiguredAutoDownloadUpdate(bool enabled);
// "fluent" (built in) | "acrylic" (bundled package) | any external skin id
// — candidate-window and floating-toolbar skin.
const std::string &GetConfiguredCandidateSkin();
bool SetConfiguredCandidateSkin(const std::string &skin);
const std::string &GetConfiguredCandidateWindowPreeditStyle();
bool SetConfiguredCandidateWindowPreeditStyle(const std::string &style);
const std::string &GetConfiguredThemeMode();
bool SetConfiguredThemeMode(const std::string &mode);
const std::string &GetConfiguredThemeSettings();
bool SetConfiguredThemeSettings(const std::string &theme);
const std::string &GetConfiguredThemeCand();
bool SetConfiguredThemeCand(const std::string &theme);
const std::string &GetConfiguredThemeFtb();
bool SetConfiguredThemeFtb(const std::string &theme);
const std::string &GetConfiguredThemeMenu();
bool SetConfiguredThemeMenu(const std::string &theme);
const std::string &GetConfiguredThemeVoice();
bool SetConfiguredThemeVoice(const std::string &theme);
// Resolve effective "dark" / "light" for a surface override ("follow" | "dark" | "light").
std::string ResolveConfiguredTheme(const std::string &surface_theme);
bool IsSystemAppsLightTheme();
// Returns a snapshot by value: the backing config is rewritten by the IPC worker thread while the voice control thread
// and the low-level keyboard hook read it, so callers must never hold a reference into it.
VoiceInputConfig GetConfiguredVoiceInput();
bool SetConfiguredVoiceInputString(const std::string &key, const std::string &value);
bool SetConfiguredVoiceInputBool(const std::string &key, bool value);
const AiAssistantConfig &GetConfiguredAiAssistant();
// Replaces the whole provider registry (settings page "save providers"). Persists the store,
// keeps the active provider valid (falls back to the first entry when it disappeared) and
// re-resolves the active endpoint/model/token.
bool SetAiAssistantProviders(const std::vector<AiAssistant::AiProvider> &entries);
const TencentTmtConfig &GetConfiguredTencentTmt();
bool SetConfiguredTencentTmtString(const std::string &key, const std::string &value);
const CustomTranslationConfig &GetConfiguredCustomTranslation();
bool SetConfiguredCustomTranslationBool(const std::string &key, bool value);
bool SetConfiguredCustomTranslationString(const std::string &key, const std::string &value);
const AliyunMtConfig &GetConfiguredAliyunMt();
bool SetConfiguredAliyunMtBool(const std::string &key, bool value);
bool SetConfiguredAliyunMtString(const std::string &key, const std::string &value);
const NiuTransConfig &GetConfiguredNiuTrans();
bool SetConfiguredNiuTransBool(const std::string &key, bool value);
bool SetConfiguredNiuTransString(const std::string &key, const std::string &value);
bool IsValidTranslationProvider(const std::string &provider);
std::shared_ptr<const TranslationConfig> GetConfiguredTranslation();
bool SetConfiguredTranslationString(const std::string &key, const std::string &value);
// Snapshot by value: download threads and the cloud worker read it while the settings host rewrites it.
NetworkProxyConfig GetConfiguredNetworkProxy();
bool SetConfiguredNetworkString(const std::string &key, const std::string &value);
// Accepts "host:port" or "http://host:port[/]"; returns the normalized "host:port", or an empty string when the
// value is not a usable HTTP proxy (other schemes such as socks5:// are rejected: WinHTTP cannot speak them).
std::string NormalizeNetworkProxyServer(const std::string &value);
const FrequencyAdjustmentConfig &GetConfiguredFrequencyAdjustment();
bool SetConfiguredFrequencyAdjustmentString(const std::string &key, const std::string &value);
bool SetConfiguredFrequencyAdjustmentInt(const std::string &key, int value);
bool SetConfiguredAiAssistantString(const std::string &key, const std::string &value);
// 按服务商写 endpoint / model：落槽位键 endpoint_<id> / model_<id>，单值键只在目标就是活动服务商时改写。
// 设置页在多提供商之间切换时用它，免得"写单值键"被当成写给后端此刻的活动服务商。
bool SetConfiguredAiAssistantProviderValue(std::string_view provider, std::string_view field, const std::string &value);
bool SetConfiguredAiAssistantBool(const std::string &key, bool value);
bool SetConfiguredAiAssistantInt(const std::string &key, int value);
