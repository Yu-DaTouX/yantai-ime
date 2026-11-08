#pragma once

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

// [network] 段：模型下载、更新检查等出站请求共用的代理设置。
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
    bool translate = true;
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
// 候选词英文行：候选条下面固定一行，显示当前高亮候选的第一条英文词义。默认开。
bool GetConfiguredEnglishLineEnabled();
bool SetConfiguredEnglishLineEnabled(bool enabled);
// [keybindings] commit_english / english_senses 的原样配置（"Ctrl+Enter" / "Ctrl+Shift+Enter"）。
// 空串表示关闭；解析由调用方用 FanyImeKeyChord::Parse 完成。
const std::string &GetConfiguredCommitEnglishBinding();
const std::string &GetConfiguredEnglishSensesBinding();
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
// 词格高权重多音节整词的边加成（log10，0 = 关闭）及其词库权重门槛。
double GetConfiguredAssocSentencePhraseBonus();
long long GetConfiguredAssocSentencePhraseBonusMinWeight();
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
// Resolve effective "dark" / "light" for a surface override ("follow" | "dark" | "light").
std::string ResolveConfiguredTheme(const std::string &surface_theme);
bool IsSystemAppsLightTheme();
// Snapshot by value: download threads read it while the settings host rewrites it.
NetworkProxyConfig GetConfiguredNetworkProxy();
bool SetConfiguredNetworkString(const std::string &key, const std::string &value);
// Accepts "host:port" or "http://host:port[/]"; returns the normalized "host:port", or an empty string when the
// value is not a usable HTTP proxy (other schemes such as socks5:// are rejected: WinHTTP cannot speak them).
std::string NormalizeNetworkProxyServer(const std::string &value);
const FrequencyAdjustmentConfig &GetConfiguredFrequencyAdjustment();
bool SetConfiguredFrequencyAdjustmentString(const std::string &key, const std::string &value);
bool SetConfiguredFrequencyAdjustmentInt(const std::string &key, int value);
