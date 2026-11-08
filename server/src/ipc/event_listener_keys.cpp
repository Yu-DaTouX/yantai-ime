// 按键处理：按键分类、组合编辑（光标/退格/插入）、译文副候选页，以及 HandleImeKey 主流程。
#include "ipc/event_listener_internal.h"
#include <Windows.h>
#include <string>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>
#include "ipc.h"
#include "ipc/candidate_text_policy.h"
#include "ipc/candidate_translation_policy.h"
#include "ipc/input_key_policy.h"
#include "engine/contracts/date_time_input.h"
#include "engine/contracts/ipc_negotiation.h"
#include "defines/defines.h"
#include "defines/globals.h"
#include "utils/common_utils.h"
#include "global/globals.h"
#include "window/caret_state_indicator_policy.h"
#include "utils/ime_utils.h"
#include "config/ime_config.h"
#include "conversion/chinese_converter.h"
#include "session/session_factory.h"
#include "mixed/date_time_candidates.h"
#include "log/candidate_diag_log.h"

using namespace event_listener_detail;

namespace
{
bool IsShiftLetterSpecialModeTriggered()
{
    return g_quick_phrase_triggered || g_radical_mode_triggered || g_date_time_mode_triggered ||
           g_emoji_mode_triggered || g_kaomoji_mode_triggered || g_symbol_mode_triggered || g_y_mode_triggered;
}

// 字母键写进 raw 的那个字母：键产生了字母就照收（保留大小写），否则取键位本身的小写字母。
// 插入与顶字重建共用这一条，preedit 才与用户敲键一致。
char CompositionLetter(UINT keycode, WCHAR wch)
{
    return (wch >= L'A' && wch <= L'Z') || (wch >= L'a' && wch <= L'z') ? static_cast<char>(wch)
                                                                        : static_cast<char>(keycode + ('a' - 'A'));
}

bool IsCommitWithHighlightedCandidatePunctuationInCandidateMode(UINT keycode, WCHAR wch)
{
    if (keycode == VK_TAB)
    {
        return false;
    }
    if ((keycode == VK_OEM_MINUS || keycode == VK_OEM_PLUS))
    {
        return false;
    }
    const bool has_active_composition = g_inputSession != nullptr && !g_inputSession->get_pinyin_sequence().empty();
    if ((keycode == VK_OEM_COMMA || keycode == VK_OEM_PERIOD) && GetConfiguredPagingCommaPeriodEnabled() &&
        has_active_composition)
    {
        return false;
    }
    if ((keycode == VK_OEM_4 || keycode == VK_OEM_6) && GetConfiguredPagingBracketsEnabled() && has_active_composition)
    {
        return false;
    }

    static const std::unordered_set<WCHAR> kCommitWithHighlightedCandidatePunctuation = {
        L'`',  //
        L'!',  //
        L'@',  //
        L'#',  //
        L'$',  //
        L'%',  //
        L'^',  //
        L'&',  //
        L'*',  //
        L'-',  // Numpad arithmetic keys are not candidate paging keys.
        L'+',  //
        L'_',  //
        L'=',  //
        L'(',  //
        L')',  //
        L'[',  //
        L']',  //
        L'\\', //
        L'/',  //
        L';',  //
        L':',  //
        L'\'', //
        L'"',  //
        L',',  //
        L'<',  //
        L'.',  //
        L'>',  //
        L'?'   //
    };
    return kCommitWithHighlightedCandidatePunctuation.find(wch) != kCommitWithHighlightedCandidatePunctuation.end();
}

bool IsManualPinyinSeparatorKey(UINT keycode, WCHAR wch)
{
    return keycode == VK_OEM_7 && wch == L'\'' && g_inputSession != nullptr &&
           !g_inputSession->get_pinyin_sequence().empty();
}

// Shift+T 指定日期时间的数字、/ 和 :：插入后 T 后面那一串还能接成某种日期时间形状时它是编码键，否则数字
// 仍是选词键、/ 和 : 仍是标点；Shift+数字的字符不是数字，始终选词。TSF 看不到配置，只按开头的 T 和同一条
// 形状规则（FanyImeDateTimeInput::AcceptsAt）预判吃键，所以这里也不看 T 模式开没开，两边的输入串才不会
// 分叉。小键盘数字到这里时已经归一成主键盘数字（NormalizeNumpadDigitKey），TSF 同样收它。光标按
// 同一方式算。
bool IsDateTimeInputKey(UINT keycode, WCHAR wch, const std::string &raw_input)
{
    const bool digit = keycode >= '0' && keycode <= '9' && wch >= L'0' && wch <= L'9';
    const bool slash = keycode == VK_OEM_2 && wch == L'/';
    const bool colon = keycode == VK_OEM_1 && wch == L':';
    if ((!digit && !slash && !colon) || g_inputSession == nullptr)
    {
        return false;
    }
    const auto &composition = GlobalIme::composition;
    const size_t caret = composition.raw_input_with_cases != raw_input && composition.caret_position == 0
                             ? raw_input.size()
                             : (std::min)(composition.caret_position, raw_input.size());
    return FanyImeDateTimeInput::AcceptsAt(raw_input.data(), raw_input.size(), caret, static_cast<char>(wch));
}

// V 模式的数字和 . + - * / ( )：插入后仍是一段 V 模式输入时是编码键，不再选词、翻页或当标点。只看字符不看
// 键位，主键盘和小键盘一样；Shift+1 这类出 ! 的仍是选词键。TSF 按同一条规则
// （FanyImeVModeInput::AcceptsAt）和同一个 Trigger（VModeChanged）预判吃键。光标按同一方式算。
bool IsVModeInputKey(WCHAR wch, const std::string &raw_input)
{
    if (wch > 0x7f || g_inputSession == nullptr)
    {
        return false;
    }
    const auto &composition = GlobalIme::composition;
    const size_t caret = composition.raw_input_with_cases != raw_input && composition.caret_position == 0
                             ? raw_input.size()
                             : (std::min)(composition.caret_position, raw_input.size());
    return FanyImeVModeInput::AcceptsAt(raw_input.data(), raw_input.size(), caret, static_cast<char>(wch),
                                        CurrentVModeTrigger());
}

bool IsSelectionKey(UINT keycode)
{
    if (keycode == VK_SPACE)
        return true;
    if (keycode >= '0' && keycode <= '9')
    {
        const std::string raw = g_inputSession ? g_inputSession->get_pinyin_sequence_with_cases() : std::string{};
        // T 模式指定日期时间：接得上的数字是编码键；V 模式里的数字也是。
        return !IsDateTimeInputKey(keycode, Global::Wch, raw) && !IsVModeInputKey(Global::Wch, raw);
    }
    return false;
}

bool IsPagingKey(UINT keycode)
{
    return keycode == VK_OEM_MINUS || keycode == VK_OEM_PLUS || keycode == VK_TAB || keycode == VK_PRIOR ||
           keycode == VK_NEXT || keycode == VK_LEFT || keycode == VK_RIGHT || keycode == VK_UP || keycode == VK_DOWN ||
           ((keycode == VK_OEM_COMMA || keycode == VK_OEM_PERIOD) && GetConfiguredPagingCommaPeriodEnabled()) ||
           ((keycode == VK_OEM_4 || keycode == VK_OEM_6) && GetConfiguredPagingBracketsEnabled());
}

bool IsCandidateNavigationKey(UINT keycode)
{
    return keycode == VK_OEM_MINUS || keycode == VK_OEM_PLUS || keycode == VK_OEM_COMMA || keycode == VK_OEM_PERIOD ||
           keycode == VK_OEM_4 || keycode == VK_OEM_6 || keycode == VK_TAB || keycode == VK_PRIOR ||
           keycode == VK_NEXT || keycode == VK_UP || keycode == VK_DOWN;
}

// [keybindings] 里的字符串键位是否就是这颗键（空串、写错都不匹配）。
bool MatchesConfiguredBinding(const std::string &binding, UINT keycode, UINT modifiers_down)
{
    FanyImeKeyChord::Binding parsed;
    return FanyImeKeyChord::Parse(binding, parsed) &&
           parsed.Matches(keycode, modifiers_down & FanyImeIpc::kKeyModifierMask);
}

bool ApplyCompositionEditKey(UINT keycode, WCHAR wch, UINT modifiers_down, bool client_supports_restore,
                             bool &composition_restored)
{
    composition_restored = false;
    std::string raw = g_inputSession->get_pinyin_sequence_with_cases();
    auto &composition = GlobalIme::composition;
    composition.caret_position =
        FanyImeIpc::ResolveCompositionCaret(composition.caret_position, composition.raw_input_with_cases, raw);
    composition.caret_position = (std::min)(composition.caret_position, raw.size());

    // R2/R10：光标前缀重算总门控（与 Ctrl+Backspace / Ctrl+方向同一谓词族）。未协商、
    // UILess、专用英文、特殊模式组合一律维持整串转换，光标只是显示层插入点。
    const bool caret_resegmentation = FanyImeIpc::ShouldResegmentCompositionByCaret(
        client_supports_restore, IsUiLessMode(), g_english_input_mode, IsSpecialModeCompositionActive(raw));
    // 箭头与 Ctrl+方向路径不改 raw、没有 pending 序列，喂完光标重解一次即可。串尾
    // 也必须显式喂：引擎 caret_ 只在 set_pinyin_sequence 触发的 apply_pending_sequence
    // 里复位，箭头路径绕过它——串尾不喂 nullopt（与 set_caret(size) 在量化边界上等
    // 价）会残留上一次前缀激活的 caret_，候选停在旧前缀上、空格结算走错前缀路径（R7）。
    const auto resegment_by_caret = [&]() {
        if (!caret_resegmentation)
        {
            return;
        }
        g_inputSession->set_caret(composition.caret_position < raw.size()
                                      ? std::optional<std::size_t>(composition.caret_position)
                                      : std::nullopt);
        g_inputSession->recompute_candidates();
    };

    // Ctrl+Left / Ctrl+Right jump the caret by one segmentation unit instead of
    // one character, consuming the same engine boundaries Ctrl+Backspace
    // deletes. The Server owns the unit model, so it moves the authoritative
    // caret and answers with CompositionRestored; TSF only applies that caret.
    // Everything unnegotiated, UILess or unit-less keeps the single-character
    // move below, so both sides agree on when the jump happens.
    const bool segment_caret = FanyImeIpc::IsSegmentCaretKey(keycode, modifiers_down);
    const bool segment_caret_supported = segment_caret && client_supports_restore && !IsUiLessMode() &&
                                         !g_english_input_mode && !IsSpecialModeCompositionActive(raw);
    if (keycode == VK_LEFT || keycode == VK_RIGHT)
    {
        if (segment_caret_supported)
        {
            const std::vector<std::size_t> boundaries = g_inputSession->segment_raw_boundaries();
            if (!boundaries.empty())
            {
                composition.caret_position =
                    keycode == VK_LEFT ? FanyImeIpc::PreviousSegmentBoundary(boundaries, composition.caret_position)
                                       : FanyImeIpc::NextSegmentBoundary(boundaries, composition.caret_position);
                composition_restored = true;
                resegment_by_caret();
                return true;
            }
        }
        if (keycode == VK_LEFT)
        {
            if (composition.caret_position > 0)
            {
                --composition.caret_position;
            }
        }
        else if (composition.caret_position < raw.size())
        {
            ++composition.caret_position;
        }
        resegment_by_caret();
        return true;
    }

    // A spelling emptied by a segment Backspace below keeps the creating-word
    // state alive with the word alone (PRD R3), so the empty-raw cleanup has to
    // know this key produced that state on purpose.
    bool keep_creating_word_after_empty_raw = false;

    if (keycode == VK_BACK)
    {
        // Ctrl+Backspace deletes one segmentation unit (one character's pinyin)
        // instead of one character. The boundaries are the engine's, so TSF
        // cannot mirror the deletion: it rebuilds from the CompositionRestored
        // reply, and everything unnegotiated or unit-less falls back to the
        // ordinary single-character behavior right below.
        const bool segment_backspace = FanyImeIpc::IsSegmentBackspaceKey(keycode, modifiers_down);
        const bool segment_supported = segment_backspace && client_supports_restore && !IsUiLessMode() &&
                                       !g_english_input_mode && !IsSpecialModeCompositionActive(raw);
        if (segment_supported && FanyImeIpc::ShouldDropCreatingWordSegment(
                                     composition.creating_word.active, IsUiLessMode(), client_supports_restore,
                                     composition.caret_position, composition.selection_history.size()))
        {
            // R3: nothing is left before the caret, so the key removes the last
            // selected segment itself. Its spelling is discarded -- unlike the
            // retraction below the user asked to delete the segment, not to
            // edit its pinyin again -- and the raw stays empty.
            composition_restored = composition.drop_last_selection();
            keep_creating_word_after_empty_raw = composition_restored && composition.creating_word.active;
        }
        else if (segment_supported)
        {
            const std::vector<std::size_t> boundaries = g_inputSession->segment_raw_boundaries();
            const std::size_t start = FanyImeIpc::PreviousSegmentBoundary(boundaries, composition.caret_position);
            if (start < composition.caret_position)
            {
                raw.erase(start, composition.caret_position - start);
                composition.caret_position = start;
                FanyImeIpc::DropDanglingSegmentDelimiter(raw, start);
                composition_restored = true;
                // Emptying the raw does not end the word: the accumulated
                // segments stay on screen and the next Ctrl+Backspace drops one
                // of them (R3).
                keep_creating_word_after_empty_raw =
                    raw.empty() && FanyImeIpc::ShouldKeepCreatingWordAfterRawEmptied(
                                       composition.creating_word.active, IsUiLessMode(), client_supports_restore,
                                       composition.selection_history.size());
            }
        }

        if (!composition_restored &&
            FanyImeIpc::ShouldRetreatCreatingWordSelection(
                composition.creating_word.active, IsUiLessMode(), client_supports_restore, raw.size(),
                composition.selection_history.size(), composition.last_selection_raw_edited()))
        {
            // This Backspace must not also delete the character: the retraction
            // removes the segment and restores its raw spelling instead. The
            // restore is state only -- the engine sequence, its candidates and
            // the restored-caret prefix are applied exactly once by the tail
            // below, so nothing here may rebuild them; a helper that did cost
            // a second full candidate query on every retraction.
            composition_restored = composition.restore_last_selection();
            if (composition_restored)
            {
                // The retraction already replaced the raw, the word and the
                // caret; the local copy must follow it so the tail below
                // re-applies the restored sequence with its caret prefix
                // recompute instead of clobbering it with the stale raw.
                raw = composition.raw_input_with_cases;
            }
        }
        else if (!composition_restored && composition.caret_position > 0)
        {
            raw.erase(composition.caret_position - 1, 1);
            --composition.caret_position;
            // Deleting the last raw character must not take the accumulated word
            // down with it: the composition stays alive showing the selected
            // segments alone -- the same R3 state a segment Backspace produces --
            // and the reply below tells the client to keep composing instead of
            // cancelling. The next Backspace then retracts the newest selection
            // from that empty raw (the empty-raw override of the edit lock)
            // rather than discarding everything the user picked.
            keep_creating_word_after_empty_raw =
                raw.empty() && FanyImeIpc::ShouldKeepCreatingWordAfterRawEmptied(
                                   composition.creating_word.active, IsUiLessMode(), client_supports_restore,
                                   composition.selection_history.size());
        }
    }
    else if (keycode == VK_DELETE)
    {
        if (composition.caret_position < raw.size())
        {
            raw.erase(composition.caret_position, 1);
        }
    }
    else
    {
        char input = 0;
        if (keycode >= 'A' && keycode <= 'Z')
        {
            input = CompositionLetter(keycode, wch);
        }
        else if (keycode == VK_OEM_7 && wch == L'\'')
        {
            input = '\'';
        }
        else if (keycode == VK_OEM_MINUS && (wch == L'-' || wch == L'_'))
        {
            // Page-boundary minus and its Shift variant become literal input.
            input = static_cast<char>(wch);
        }
        else if (keycode == VK_OEM_PLUS && wch == L'+')
        {
            // Shift+= becomes a literal plus.
            input = static_cast<char>(wch);
        }
        else if (IsDateTimeInputKey(keycode, wch, raw) || IsVModeInputKey(wch, raw))
        {
            input = static_cast<char>(wch);
        }
        else
        {
            return false;
        }
        // U 模式里引号、分号、斜杠、大写字母这些键不能在这里吞掉：TSF 已经把它们当输入键记进了
        // 自己的按键缓冲，吞掉又不发 CompositionRestored 重同步，两侧的串和光标就会错位，之后的
        // 退格删的也不是同一个字符。照常插入，拆字查询对含非 a-z 的码返回空候选即可。
        if (input == '\'' && ((composition.caret_position > 0 && raw[composition.caret_position - 1] == '\'') ||
                              (composition.caret_position < raw.size() && raw[composition.caret_position] == '\'')))
        {
            return true;
        }
        // 0：上面已经整串改写过，没有要插入的字符。
        if (input != 0)
        {
            raw.insert(raw.begin() + static_cast<std::ptrdiff_t>(composition.caret_position), input);
            ++composition.caret_position;
            // Typing locks the newest selection (Rime's selected_before_editing):
            // Backspace must keep deleting these fresh characters instead of
            // retracting the selection out from under them. Caret moves never reach
            // here and deliberately do not lock.
            composition.note_raw_inserted();
        }
    }

    if (raw.empty() && !keep_creating_word_after_empty_raw)
    {
        // Without a kept state, TSF cancels the whole composition as soon as the
        // last remaining character is gone, so the accumulated word and the
        // snapshots a later Backspace could retract from must not survive here:
        // they would let a fresh pinyin composition retract a segment of the
        // previous one. Both Backspaces that legitimately empty the raw keep them
        // on purpose instead: the segment one through R3, the plain one because
        // its reply tells the client to keep composing with the word alone.
        composition.clear_creating_word();
        composition.selection_history.clear();
    }

    g_inputSession->set_pinyin_sequence(raw);
    g_inputSession->set_pinyin_sequence_with_cases(raw);
    if (caret_resegmentation && composition.caret_position < raw.size())
    {
        // apply_pending_sequence() 会复位引擎光标：先让新 raw 生效，再喂光标做前缀
        // 重解（R2/R6）。caret 在串尾时不进这里，上一次 recompute 就是现状整串解码
        // （R7 零回归）。
        g_inputSession->recompute_candidates();
        g_inputSession->set_caret(composition.caret_position);
        g_inputSession->recompute_candidates();
    }
    else
    {
        g_inputSession->recompute_candidates();
    }
    composition.raw_input_with_cases = raw;
    return true;
}

// A literal character that would otherwise be swallowed as a candidate-navigation
// key becomes part of the raw spelling. The engine cannot split a spelling that
// carries it, so the candidate list collapses to the single fallback entry
// showing the typed string, and Space commits exactly that string.
//
// Only hosts that negotiated composition restore can apply the frame. Callers
// decide which keys qualify; this helper only checks that the append itself is
// meaningful.
bool AppendLiteralToComposition(WCHAR literal, uint64_t client_id, uint64_t activation_epoch, uint64_t request_id)
{
    const std::string raw = g_inputSession->get_pinyin_sequence_with_cases();
    if (raw.empty() || GlobalIme::composition.caret_position < raw.size() || g_english_input_mode || IsUiLessMode() ||
        IsSpecialModeCompositionActive(raw) || IsCandidateSubPageActive() ||
        !ClientNegotiatedCompositionRestore(client_id))
    {
        return false;
    }

    // Reuse the ordinary edit-key path so the engine sequence and the caret
    // rule match a normal typed letter.
    const UINT keycode = literal == L'+' ? VK_OEM_PLUS : VK_OEM_MINUS;
    bool composition_restored = false;
    if (!ApplyCompositionEditKey(keycode, literal, Global::ModifiersDown, true, composition_restored))
    {
        return false;
    }
    if (composition_restored)
    {
        return false;
    }

    // PrepareCandidateList emits the same single fallback for an unsplittable
    // spelling, so later rebuilds keep the entry instead of dropping it right
    // before Space.
    FanyNamedPipe::PrepareCandidateList(client_id, activation_epoch);

    Global::MsgTypeToTsf = Global::DataFromServerMsgType::CompositionRestored;
    Global::candidate_ui.selected_text = BuildCreateWordPipePayload(g_inputSession->get_pinyin_sequence_with_cases(),
                                                                    GlobalIme::composition.creating_word.word);
    FanyNamedPipe::SendCurrentDataToClient(client_id, activation_epoch, request_id);
    return true;
}

// 按键延迟必须用高精度单调时钟。GetTickCount64 每个系统 tick 才前进一次（约 15.6 ms），
// 分辨不出 8 ms 阈值以下的差异。steady_clock 在 Windows 上走 QPC。
using KeyLatencyClock = std::chrono::steady_clock;
constexpr double kSlowStageThresholdMs = 8.0;

double ElapsedMilliseconds(KeyLatencyClock::time_point start, KeyLatencyClock::time_point end = KeyLatencyClock::now())
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// HandleImeKey 的分段计时打点：构造记起点，Stop() 或析构时耗时 >=8ms 才打一条
// stage=handle-<sub> 分段日志（阈值、字段集与 ScopedServerKeyLatency 一致）。部分调用点
// 落在 if 条件里，RAII 作用域包不住条件求值，那些点用 Stop() 显式收口；Stop() 只生效
// 一次，之后析构不再打点。红线：行内仅 stage 名/request_id/client/epoch/elapsed_ms，
// 不得出现按键、候选词、预编辑或提交文本。
struct ScopedKeyStage
{
    ScopedKeyStage(uint64_t client_id, uint64_t activation_epoch, uint64_t request_id, const wchar_t *stage)
        : client_id_(client_id), activation_epoch_(activation_epoch), request_id_(request_id), stage_(stage)
    {
    }

    ~ScopedKeyStage()
    {
        Stop();
    }

    ScopedKeyStage(const ScopedKeyStage &) = delete;
    ScopedKeyStage &operator=(const ScopedKeyStage &) = delete;

    void Stop()
    {
        if (!stopped_)
        {
            stopped_ = true;
            const double elapsed_ms = ElapsedMilliseconds(started_at_);
            if (elapsed_ms >= kSlowStageThresholdMs)
            {
                DIAG_LOGF(L"[key-latency] side=server stage={} request={} client={} epoch={} elapsed_ms={:.3f}", stage_,
                          request_id_, client_id_, activation_epoch_, elapsed_ms);
            }
        }
    }

    uint64_t client_id_;
    uint64_t activation_epoch_;
    uint64_t request_id_;
    const wchar_t *stage_;
    KeyLatencyClock::time_point started_at_ = KeyLatencyClock::now();
    bool stopped_ = false;
};
} // namespace

namespace FanyNamedPipe
{
struct ScopedServerKeyLatency
{
    uint64_t client_id;
    uint64_t activation_epoch;
    uint64_t request_id;
    KeyLatencyClock::time_point started_at = KeyLatencyClock::now();

    ~ScopedServerKeyLatency()
    {
        const double elapsed_ms = ElapsedMilliseconds(started_at);
        if (elapsed_ms >= kSlowStageThresholdMs)
        {
            DIAG_LOGF(L"[key-latency] side=server stage=handle request={} client={} epoch={} elapsed_ms={:.3f}",
                      request_id, client_id, activation_epoch, elapsed_ms);
        }
    }
};

// 把候选框换回 Ctrl+Enter 或选中「📅日期」之前的那一屏。子页期间输入串一个字都没动，session
// 里的候选还是原来那批，所以这里直接把存下来的 items / 页码 / 高亮位放回去。子页期间切过字形时，
// 放回的列表还是按旧字形展开的写法，要按当前字形换写法，显示与上屏才跟得上切换。随后这颗按键
// 继续走它本来的流程，就像子页从来没出现过一样。
void ExitCandidateSubPage()
{
    if (!IsCandidateSubPageActive())
    {
        return;
    }
    const bool was_english_senses_page = g_english_senses_page_active;
    g_translation_candidates_active = false;
    g_date_time_page_active = false;
    g_date_time_page_keyword.clear();
    g_english_senses_page_active = false;
    g_english_senses_page_word.clear();
    g_english_senses_page_senses.clear();
    auto &ui = Global::candidate_ui;
    ui.set_items(std::move(g_translation_saved_items));
    g_translation_saved_items.clear();
    ui.page_index = g_translation_saved_page_index;
    ui.selected_index_in_page = g_translation_saved_selected_index;
    ui.selection_started = g_translation_saved_selection_started;
    g_translation_saved_page_index = 0;
    g_translation_saved_selected_index = 0;
    g_translation_saved_selection_started = false;
    RefreshCandidatePageUi(false);
    if (was_english_senses_page)
    {
        NotifyEnglishSensesPage(false);
    }
    if (CandidateListCharacterSet() != GetConfiguredCharacterSet())
        RespellCandidatesForCharacterSetChange();
}

// 混输里的「📅日期」入口：把候选框换成这组的全部格式（顺序与 Shift+T 模式同一份），空格/数字键
// 照常选一条上屏，其它键先换回原来那一屏（见 HandleImeKey）。
bool EnterDateTimeCandidatePage(const std::string &keyword)
{
    if (IsCandidateSubPageActive())
    {
        return false;
    }
    auto items = OrderedDateTimeCandidates(keyword);
    if (items.empty())
    {
        return false;
    }
    auto &ui = Global::candidate_ui;
    g_translation_saved_items = ui.items;
    g_translation_saved_page_index = ui.page_index;
    g_translation_saved_selected_index = ui.selected_index_in_page;
    g_translation_saved_selection_started = ui.selection_started;
    ui.set_items(std::move(items));
    g_date_time_page_active = true;
    g_date_time_page_keyword = keyword;
    RefreshCandidatePageUi(true);
    return true;
}

void RebuildDateTimeCandidatePage()
{
    if (!g_date_time_page_active)
    {
        return;
    }
    auto &ui = Global::candidate_ui;
    const int page_index = ui.page_index;
    const int selected_index = ui.selected_index_in_page;
    ui.set_items(OrderedDateTimeCandidates(g_date_time_page_keyword));
    if (page_index > 0 && page_index * ui.page_size < static_cast<int>(ui.items.size()))
    {
        ui.page_index = page_index;
    }
    ui.selected_index_in_page = selected_index;
    RefreshCandidatePageUi(true);
}

// Ctrl+Enter：上屏高亮候选右边的那条译文（副候选）。Ctrl+数字：上屏指定序号候选的那条译文
// （forced_index_in_page，0 起）。只有一条译义就直接上屏；有多条时把候选框整个换成这几条
// 译义，空格/数字键照常选一条上屏（见 ProcessSelectionKey 的译文分支）。
void HandleTranslationCommitKey(uint64_t client_id, uint64_t activation_epoch, uint64_t request_id,
                                int forced_index_in_page)
{
    // 拿不到译文时回 NavigationIgnored：这颗键已经被 TSF 吃掉了，必须给一条回复，
    // 而且这条回复既不上屏也不给 wch 补标点。
    Global::MsgTypeToTsf = Global::DataFromServerMsgType::NavigationIgnored;
    if (IsCandidateSubPageActive() || IsUiLessMode() || !GetConfiguredCandidateTranslationsEnabled() ||
        Global::candidate_ui.items.empty())
    {
        SendCurrentDataToClient(client_id, activation_epoch, request_id);
        return;
    }

    // 和数字/空格选词一样，先等画面追上已发布的那一页，否则取到的是用户没看见的那条译文。
    WaitForCandidateRenderSync(Global::Keycode);
    EnsureCandidatePageReady();

    auto &ui = Global::candidate_ui;
    const int highlighted_position = (std::max)(0, ui.selected_index_in_page);
    const int page_position = forced_index_in_page >= 0 ? forced_index_in_page : highlighted_position;
    const size_t index = static_cast<size_t>(page_position);
    if (index >= ui.page_glosses.size())
    {
        SendCurrentDataToClient(client_id, activation_epoch, request_id);
        return;
    }
    const auto senses = FanyImeIpc::SplitTranslationGloss(wstring_to_string(ui.page_glosses[index]));
    if (senses.empty())
    {
        SendCurrentDataToClient(client_id, activation_epoch, request_id);
        return;
    }

    if (senses.size() == 1)
    {
        Global::MsgTypeToTsf = Global::DataFromServerMsgType::CommitExactText;
        ui.selected_text = string_to_wstring(CandidateTextForOutput(senses.front()));
        // Normal/CommitExactText 的回复由 SendCurrentDataToClient 负责收尾（ClearState）。
        SendCurrentDataToClient(client_id, activation_epoch, request_id);
        return;
    }

    g_translation_saved_items = ui.items;
    g_translation_saved_page_index = ui.page_index;
    g_translation_saved_selected_index = ui.selected_index_in_page;
    g_translation_saved_selection_started = ui.selection_started;
    std::vector<WordItem> translation_items;
    translation_items.reserve(senses.size());
    for (const auto &sense : senses)
    {
        translation_items.emplace_back(std::string{}, sense, 0, CandidateSource::Fallback);
    }
    ui.set_items(std::move(translation_items));
    g_translation_candidates_active = true;
    RefreshCandidatePageUi(true);
    SendCurrentDataToClient(client_id, activation_epoch, request_id);
}

// 英文词义清单页上的按键：数字/空格/回车选一条上屏，方向键移动高亮，Esc/Backspace 只退回原中文候选。
// 返回 true 表示这颗键已经在清单里处理完；返回 false 时清单已经关掉，按键按普通候选键继续处理。
bool HandleEnglishSensesPageKey(uint32_t keycode, uint64_t client_id, uint64_t activation_epoch, uint64_t request_id)
{
    if (!g_english_senses_page_active)
    {
        return false;
    }

    auto &ui = Global::candidate_ui;
    const uint32_t modifiers = Global::ModifiersDown & FanyImeIpc::kKeyModifierMask;
    const int count = static_cast<int>(g_english_senses_page_senses.size());

    const auto reply_and_repaint = [&](UINT reply_type) {
        Global::MsgTypeToTsf = reply_type;
        SendCurrentDataToClient(client_id, activation_epoch, request_id);
        RefreshCandidatePageUi(true);
        RequestShowCandidateWindow();
    };

    // 没有释义：清单只有一行「没有英文释义」，任意键都退回原中文候选。
    if (count == 0)
    {
        ExitCandidateSubPage();
        reply_and_repaint(Global::DataFromServerMsgType::NavigationIgnored);
        return true;
    }

    // Esc / Backspace：只退回原中文候选，候选与高亮不变。清单页打开时 TSF 把这两个键当服务端候选键
    // 发过来（见 EnglishSensesPageChanged），所以这里不会取消组合、也不会删字符。
    if ((keycode == VK_ESCAPE || keycode == VK_BACK) && modifiers == 0)
    {
        ExitCandidateSubPage();
        reply_and_repaint(Global::DataFromServerMsgType::NavigationIgnored);
        return true;
    }

    // 数字 1-9 直接选；空格/回车选高亮那一条。
    if (keycode == VK_SPACE || keycode == VK_RETURN || (keycode >= '1' && keycode <= '9'))
    {
        const int index =
            (keycode == VK_SPACE || keycode == VK_RETURN) ? ui.selected_index_in_page : static_cast<int>(keycode - '1');
        if (index < 0 || index >= count)
        {
            Global::MsgTypeToTsf = Global::DataFromServerMsgType::OutofRange;
            SendCurrentDataToClient(client_id, activation_epoch, request_id);
            return true;
        }
        const std::string english = g_english_senses_page_senses[static_cast<size_t>(index)].text;
        // 先退回原中文候选：上屏按那一屏的高亮候选推进组合（部分上屏时要接着组剩下的拼音）。
        ExitCandidateSubPage();
        if (!CommitEnglishGlossText(english, client_id, activation_epoch))
        {
            Global::MsgTypeToTsf = Global::DataFromServerMsgType::NavigationIgnored;
        }
        SendCurrentDataToClient(client_id, activation_epoch, request_id);
        return true;
    }

    // 方向键和翻页键在清单里移动高亮。
    if (IsCandidateNavigationKey(keycode))
    {
        const bool move_next = keycode == VK_DOWN || keycode == VK_RIGHT || keycode == VK_NEXT || keycode == VK_TAB ||
                               keycode == VK_OEM_PLUS;
        const int next = std::clamp(ui.selected_index_in_page + (move_next ? 1 : -1), 0, count - 1);
        const bool moved = next != ui.selected_index_in_page;
        ui.selected_index_in_page = next;
        ui.selection_started = true;
        reply_and_repaint(moved ? (move_next ? Global::DataFromServerMsgType::MoveSelectionNext
                                             : Global::DataFromServerMsgType::MoveSelectionPrevious)
                                : Global::DataFromServerMsgType::NavigationIgnored);
        return true;
    }

    // 其它键先退回原中文候选，再按普通候选键处理（和译文页、日期页同一套）。
    ExitCandidateSubPage();
    return false;
}

// Bring the candidate page in step with the CompositionRestored frame just sent.
// TSF applies that payload without touching its candidate presenter, so the page
// on screen has to follow here -- and that covers every frame the reply branch
// sends, not just the retraction: the unit Backspace deletion shortens the raw
// and the Ctrl+arrow unit jump moves the caret prefix, and both would otherwise
// leave the pre-key page (and its selection) on screen.
//   - the raw is gone: nothing to show. The selected segments may still be on
//     screen (a segment Backspace emptied the raw but kept the word), where TSF
//     deliberately leaves its presenter alone because ending it would send
//     HideCandidateWnd and reset this very composition; and a Backspace that
//     ended the word must never fall through to a rebuild that would publish the
//     empty-input fallback candidate;
//   - the caret prefix is empty: hide too (R4, the same rule as the ShowCandidate
//     task and the caret-arrow path);
//   - otherwise rebuild from the engine -- the old page holds the pre-frame
//     items -- and re-highlight the restored pick.
void PublishRestoredCompositionCandidates(uint64_t client_id, uint64_t activation_epoch)
{
    // One-shot: consume it even when this outcome hides instead of rebuilding, so
    // a position recorded on a page that no longer exists cannot leak into a
    // later frame.
    const GlobalIme::RestoredSelectionHighlight restored_highlight =
        GlobalIme::composition.take_restored_selection_highlight();
    if (GlobalIme::composition.raw_input_with_cases.empty())
    {
        HideCandidateWindowAndDropItems();
        return;
    }
    if (FanyImeIpc::IsCaretPrefixEmpty(g_inputSession->prefix_end(),
                                       g_inputSession->get_pinyin_sequence_with_cases().size()))
    {
        HideCandidateWindowAndDropItems();
        return;
    }

    PrepareCandidateList(client_id, activation_epoch);
    if (restored_highlight.absolute_index >= 0)
    {
        // A retraction re-highlights the item the user had picked on the rebuilt
        // page: no frequency update ran during the creating word, so a page
        // rebuilt for the same prefix still holds the same items in the same
        // order. A page for another prefix (the suffix was edited between the
        // pick and the retraction) does not, and the recorded position would land
        // on an unrelated candidate -- apply it only after the prefixes match.
        const std::string rebuilt_page_prefix = FanyImeIpc::NormalizeCandidatePagePrefix(
            g_inputSession->get_pinyin_sequence_with_cases(), g_inputSession->prefix_end());
        auto &ui = Global::candidate_ui;
        if (rebuilt_page_prefix == restored_highlight.page_prefix && ui.item_total_count > 0 && ui.page_size > 0)
        {
            const int position = std::min(restored_highlight.absolute_index, ui.item_total_count - 1);
            ui.page_index = position / ui.page_size;
            ui.selected_index_in_page = position % ui.page_size;
            ui.begin_selection();
            RefreshCandidatePageUi(false);
        }
    }
    RequestShowCandidateWindow();
}

// Esc inside the creating-word shape (see HasEscapeCreatingWordShape). TSF is
// holding for a CompositionRestored frame, so it is sent in both outcomes:
//   - keep (ShouldEscapeKeepSelectedWord): drop only the unselected spelling
//     and leave the selected word alone on screen -- the R3 state a segment
//     Backspace already produces, except that the candidate window stays up
//     showing just the word as its preedit. The selection history stays, so a
//     later Backspace can still retract the word;
//   - otherwise reset everything like any other Esc; the empty payload makes
//     TSF cancel its composition.
void HandleCreatingWordEscape(uint64_t client_id, uint64_t activation_epoch, uint64_t request_id,
                              const std::string &input_before_key)
{
    if (FanyImeIpc::ShouldEscapeKeepSelectedWord(GetConfiguredEscapeKeepsSelectedWord(), input_before_key.size()))
    {
        g_inputSession->set_pinyin_sequence("");
        g_inputSession->set_pinyin_sequence_with_cases("");
        g_inputSession->recompute_candidates();
        UpdateEnglishInput("");
        UpdateMixedInput({});
        g_dedicated_english_answer_pending = false;
        ClearSpecialModeTriggers();
        auto &composition = GlobalIme::composition;
        composition.raw_input_with_cases.clear();
        composition.segmented_pinyin.clear();
        composition.caret_position = 0;
        composition.restored_selection_highlight = {};
        Global::MsgTypeToTsf = Global::DataFromServerMsgType::CompositionRestored;
        Global::candidate_ui.selected_text =
            BuildCreateWordPipePayload(std::string{}, composition.creating_word.prefix_text) + L"\t0";
        SendCurrentDataToClient(client_id, activation_epoch, request_id);
        if (GetConfiguredCandidateWindowPreeditStyle() == "empty")
        {
            // The window draws no preedit, so an empty page would be a blank card.
            HideCandidateWindowAndDropItems();
            return;
        }
        // Keep the window up with the word alone as its preedit (Weasel style): an
        // empty page, so the stale candidates of the dropped spelling go away.
        Global::CandidateString.clear();
        Global::candidate_ui.set_items({});
        RefreshCandidatePageUi(true);
        return;
    }

    PostMessage(::global_hwnd, WM_HIDE_MAIN_WINDOW, 0, 0);
    ClearState();
    Global::MsgTypeToTsf = Global::DataFromServerMsgType::CompositionRestored;
    Global::candidate_ui.selected_text = L"\t\t\t0";
    SendCurrentDataToClient(client_id, activation_epoch, request_id);
}

// The toolbar toggles the ordinary Chinese/English compartment.
void HandleToolbarLanguageToggle(bool from_ime_enabled)
{
    SendToTsfWorkerThreadViaNamedpipe(from_ime_enabled ? Global::DataFromServerMsgTypeToTsfWorkerThread::SwitchToEn
                                                       : Global::DataFromServerMsgTypeToTsfWorkerThread::SwitchToCn,
                                      L"");
}

/**
 * @brief
 *
 * 调频、造词也都在这里处理。
 *
 */
void HandleImeKey(uint64_t client_id, uint64_t activation_epoch, uint64_t request_id)
{
    const ScopedServerKeyLatency latency{client_id, activation_epoch, request_id};
    /* 先清理一下状态 */
    Global::MsgTypeToTsf = Global::DataFromServerMsgType::Normal;
    ScopedKeyStage read_packet{client_id, activation_epoch, request_id, L"handle-read-packet"};
    ::ReadDataFromNamedPipe(0b000111);
    read_packet.Stop();

    // TSF classifies VK_NUMPAD0..9 as candidate digit keys. Keep the IPC
    // contract symmetric before any selection/composition predicates run.
    Global::Keycode = FanyImeIpc::NormalizeNumpadDigitKey(Global::Keycode);

    Global::ModifiersDown &= ~0x02000000u; // Retired SensitiveField bit an old DLL may still set.
    NoteRescoringContextKeySource(client_id, activation_epoch);
    // 横排候选的左右键：TSF 只在这项生效、且协商过 CompositionRestore 时打这个标记，并且一定会读这次的回复。
    // 是否生效以标记为准而不是再查配置：配置刚改、TSF 还没收到新状态时，两边仍按同一个约定走。
    const bool horizontal_arrow_request = (Global::ModifiersDown & FanyImePipeFlags::HorizontalArrowSelection) != 0;
    Global::ModifiersDown &= ~FanyImePipeFlags::HorizontalArrowSelection;

    // MainPipeClientThread only enqueues this from a negotiated client with a known action.
    if (namedpipeData.event_type == FanyImePipeEventType::InputStateAction)
    {
        SetEnglishInputMode(!g_english_input_mode);
        ClearState();
        return;
    }

    if (FanyImeProtocol::IsCharacterSetShortcut(Global::Keycode, Global::ModifiersDown))
    {
        if (g_authoritative_cn_mode != 0 && GetConfiguredCharacterSetShortcutEnabled())
        {
            const std::string previous = GetConfiguredCharacterSet();
            const std::string next = previous == "traditional" ? "simplified" : "traditional";
            // The packet's point[] is this key's badge anchor, not the
            // candidate anchor: read it locally and leave Global::Point alone.
            // Legacy clients leave the struct-default point there instead.
            const bool changed = SetConfiguredCharacterSet(next) && GetConfiguredCharacterSet() != previous;
            if (changed && GetConfiguredCharacterSet() == "traditional")
                ChineseConverter::PreloadInBackground();
            if (changed && ClientNegotiatedCaretStateIndicator(client_id))
            {
                PostCaretStateBadge(FanyImeUi::SingleStateBadge(FanyImeUi::CaretStateKind::CharacterSet,
                                                                GetConfiguredCharacterSet() == "traditional"),
                                    namedpipeData.point[0], namedpipeData.point[1]);
            }
            if (changed)
                RespellCandidatesForCharacterSetChange();
        }
        return;
    }

    if (FanyImeIpc::IsEnglishModeToggleKey(Global::Keycode, Global::ModifiersDown) &&
        !ClientNegotiatedInputStateShortcuts(client_id))
    {
        SetEnglishInputMode(!g_english_input_mode);
        ClearState();
        return;
    }

    // 英文行的两个键位要排在译文上屏之前：IsTranslationCommitKey 不看译文开关，
    // 排在后面时 Ctrl+Enter 永远先被它截走（回 NavigationIgnored），英文行上屏不会触发。
    // 候选词英文上屏：[keybindings] commit_english 上屏候选条下面的英文行，english_senses 展开全部词义。
    // 老的「候选词翻译」在用 Ctrl+Enter 上屏译文，两者同时开时把 Ctrl+Enter 留给它（TSF 侧同样不抢）。
    if (!GetConfiguredCandidateTranslationsEnabled() &&
        MatchesConfiguredBinding(GetConfiguredCommitEnglishBinding(), Global::Keycode, Global::ModifiersDown))
    {
        if (g_english_senses_page_active)
        {
            // 清单里再按一次就是选高亮那一条。
            HandleEnglishSensesPageKey(VK_SPACE, client_id, activation_epoch, request_id);
            return;
        }
        if (!CommitEnglishLine(client_id, activation_epoch))
        {
            // 没有释义：这颗键已经被 TSF 吃掉，必须回一条什么都不做的回复。
            Global::MsgTypeToTsf = Global::DataFromServerMsgType::NavigationIgnored;
        }
        SendCurrentDataToClient(client_id, activation_epoch, request_id);
        return;
    }
    // 展开全部词义不和「候选词翻译」抢键：那条功能只用 Ctrl+Enter 与 Ctrl+数字。
    if (MatchesConfiguredBinding(GetConfiguredEnglishSensesBinding(), Global::Keycode, Global::ModifiersDown))
    {
        if (!g_english_senses_page_active)
        {
            EnterEnglishSensesPage(client_id, activation_epoch);
        }
        Global::MsgTypeToTsf = Global::DataFromServerMsgType::NavigationIgnored;
        SendCurrentDataToClient(client_id, activation_epoch, request_id);
        return;
    }
    if (FanyImeIpc::IsTranslationCommitKey(Global::Keycode, Global::ModifiersDown))
    {
        HandleTranslationCommitKey(client_id, activation_epoch, request_id, -1);
        return;
    }
    // Ctrl+数字：上屏指定序号候选的译文（Ctrl+Enter 上屏当前高亮那条），两条 chord 落到同一个
    // 处理 branch。译文总开关（general.candidate_translations）关掉时 TSF 直接不吃它们，这里仍进
    // 同一个 branch 回 NavigationIgnored——不要改成先判总开关再放行，那样会落到下面的普通选词分支，
    // Ctrl+1 直接上屏第一个候选。
    if (FanyImeIpc::IsTranslationDigitKey(Global::Keycode, Global::ModifiersDown))
    {
        const uint32_t digit_position = FanyImeIpc::TranslationDigitPosition(Global::Keycode, Global::ModifiersDown);
        HandleTranslationCommitKey(client_id, activation_epoch, request_id, static_cast<int>(digit_position) - 1);
        return;
    }
    // 英文词义清单页只认自己的那几个键，其余按键先退回原中文候选再照常处理。
    if (HandleEnglishSensesPageKey(Global::Keycode, client_id, activation_epoch, request_id))
    {
        return;
    }
    // 横排候选左右键决定移高亮还是移光标（规则见 ShouldArrowMoveCandidateSelection）。
    const bool horizontal_arrow_key =
        horizontal_arrow_request && (Global::Keycode == VK_LEFT || Global::Keycode == VK_RIGHT) &&
        (Global::ModifiersDown & 0b00000111u) == 0 && !IsUiLessMode() && ClientNegotiatedCompositionRestore(client_id);
    bool arrow_moves_selection = false;
    if (horizontal_arrow_key)
    {
        const auto &composition = GlobalIme::composition;
        const auto &ui = Global::candidate_ui;
        arrow_moves_selection = FanyImeIpc::ShouldArrowMoveCandidateSelection(
            Global::Keycode, composition.caret_position >= composition.raw_input_with_cases.size(),
            ui.current_page_start() + ui.selected_index_in_page, ui.item_total_count);
    }

    // 译文页、日期页只认选词和翻页/移动高亮。其它任何键都先把候选框换回原来那一屏，然后照常处理，
    // 所以退格、字母、回车、标点在子页上的表现和没打开过它时完全一致。
    if (IsCandidateSubPageActive())
    {
        // Shift 放行是给 Shift+Tab 上一页留的；Ctrl/Alt 组合一律退出。
        const bool stays_on_sub_page =
            (Global::ModifiersDown & 0b00000110u) == 0 &&
            (IsSelectionKey(Global::Keycode) || IsCandidateNavigationKey(Global::Keycode) || arrow_moves_selection);
        if (!stays_on_sub_page)
        {
            ExitCandidateSubPage();
        }
    }

    const std::string input_before_key =
        g_inputSession ? g_inputSession->get_pinyin_sequence_with_cases() : std::string{};
    const bool shift_only = (Global::ModifiersDown & 0b00000111u) == 0b00000001u;
    const std::optional<SchemeType> scheme =
        g_inputSession ? std::optional<SchemeType>(g_inputSession->current_scheme_type()) : std::nullopt;
    const bool chinese_scheme = scheme == SchemeType::Quanpin;
    const bool date_time_scheme = chinese_scheme;
    const bool pinyin_commit =
        FanyImeIpc::IsPinyinCommitKey(Global::Keycode, Global::ModifiersDown) && !input_before_key.empty();
    if (Global::Keycode == VK_RETURN && !input_before_key.empty() && GetConfiguredEnterLearnsEnglishWord())
    {
        std::string english_word;
        // V 模式没有触发标记，按输入串认：回车上屏的 V123 不是要学的英文词。
        const bool shift_letter_special_mode =
            IsShiftLetterSpecialModeTriggered() || IsVModeCompositionActive(input_before_key);
        if (FanyImeIpc::ShouldLearnEnteredEnglishWord(g_english_input_mode, shift_letter_special_mode, chinese_scheme,
                                                      g_inputSession->is_all_complete_pure_pinyin()))
            english_word = input_before_key;
        EnqueueLearnEnteredEnglishWordTask(english_word);
    }
    if (pinyin_commit)
    {
        const std::string raw = input_before_key;
        const std::string &text = raw;
        Global::candidate_ui.selected_text = string_to_wstring(GlobalIme::composition.creating_word.prefix_text + text);
        Global::MsgTypeToTsf = Global::DataFromServerMsgType::CommitExactText;
        SendCurrentDataToClient(client_id, activation_epoch, request_id);
        return;
    }
    if (chinese_scheme && !g_english_input_mode && GetConfiguredQuickPhraseEnabled() && input_before_key.empty() &&
        Global::Keycode == 'K' && Global::Wch == L'K' && shift_only)
        g_quick_phrase_triggered = true;
    if (chinese_scheme && !g_english_input_mode && GetConfiguredRadicalModeEnabled() && input_before_key.empty() &&
        Global::Keycode == 'U' && Global::Wch == L'U' && shift_only)
        g_radical_mode_triggered = true;
    if (date_time_scheme && !g_english_input_mode && GetConfiguredDateTimeModeEnabled() && input_before_key.empty() &&
        Global::Keycode == 'T' && Global::Wch == L'T' && shift_only)
        g_date_time_mode_triggered = true;
    if (chinese_scheme && !g_english_input_mode && GetConfiguredEmojiModeEnabled() && input_before_key.empty() &&
        Global::Keycode == 'E' && Global::Wch == L'E' && shift_only)
        g_emoji_mode_triggered = true;
    if (chinese_scheme && !g_english_input_mode && GetConfiguredKaomojiModeEnabled() && input_before_key.empty() &&
        Global::Keycode == 'M' && Global::Wch == L'M' && shift_only)
        g_kaomoji_mode_triggered = true;
    if (chinese_scheme && !g_english_input_mode && GetConfiguredSymbolModeEnabled() && input_before_key.empty() &&
        Global::Keycode == 'S' && Global::Wch == L'S' && shift_only)
        g_symbol_mode_triggered = true;
    if (chinese_scheme && !g_english_input_mode && GetConfiguredYModeEnabled() && input_before_key.empty() &&
        Global::Keycode == 'Y' && Global::Wch == L'Y' && shift_only)
        g_y_mode_triggered = true;

    if (FanyImeIpc::HasEscapeCreatingWordShape(Global::Keycode, GlobalIme::composition.creating_word.active,
                                               IsUiLessMode(), ClientNegotiatedCompositionRestore(client_id)))
    {
        HandleCreatingWordEscape(client_id, activation_epoch, request_id, input_before_key);
        return;
    }

    if (FanyImeIpc::IsBackendIndependentCompositionResetKey(Global::Keycode))
    {
        // TSF completes/cancels the composition locally. Keep every backend in
        // lockstep, invalidate async candidates, and do not manufacture a
        // reply for this locally consumed key.
        PostMessage(::global_hwnd, WM_HIDE_MAIN_WINDOW, 0, 0);
        ClearState();
        return;
    }

    const bool is_paging_key = IsPagingKey(Global::Keycode);
    const bool is_manual_pinyin_separator = IsManualPinyinSeparatorKey(Global::Keycode, Global::Wch);
    const bool is_date_time_input_key = IsDateTimeInputKey(Global::Keycode, Global::Wch, input_before_key);
    // V 模式的数字和 . + - * / ( )：只进输入串，不选词、不翻页、不当标点。
    const bool is_v_mode_input_key = IsVModeInputKey(Global::Wch, input_before_key);
    // V 模式里数字键用来输入，Shift+数字选词。Shift+1 出的是 !，不排除就会被当成「上屏高亮候选
    // + 标点」；Shift+8/9/0 出 * ( ) 是编码键，上一行已经收下。
    const bool is_v_mode_shift_digit_selection = !is_v_mode_input_key && shift_only && Global::Keycode >= '1' &&
                                                 Global::Keycode <= '9' && IsVModeCompositionActive(input_before_key);
    const int word_character_direction =
        FanyImeIpc::WordToCharacterDirection(Global::Keycode, Global::Wch, Global::ModifiersDown,
                                             GetConfiguredWordToCharacterEnabled() && !is_v_mode_input_key,
                                             GetConfiguredWordToCharacterKeys() == "minus_equal");
    const bool is_commit_with_highlighted_candidate_punctuation =
        word_character_direction != 0 ||
        (!is_manual_pinyin_separator && !is_date_time_input_key && !is_v_mode_input_key &&
         !is_v_mode_shift_digit_selection &&
         IsCommitWithHighlightedCandidatePunctuationInCandidateMode(Global::Keycode, Global::Wch));
    const bool is_selection_key = IsSelectionKey(Global::Keycode);
    // 移高亮的左右键不碰组合，交给下面的候选导航分支。
    const bool is_composition_edit_key =
        ((Global::Keycode == VK_LEFT || Global::Keycode == VK_RIGHT) && !arrow_moves_selection) ||
        Global::Keycode == VK_BACK || Global::Keycode == VK_DELETE ||
        (Global::Keycode >= 'A' && Global::Keycode <= 'Z') || is_manual_pinyin_separator || is_date_time_input_key ||
        is_v_mode_input_key;
    const bool should_forward_key_to_session = !is_commit_with_highlighted_candidate_punctuation && !is_selection_key &&
                                               !is_paging_key && !is_composition_edit_key;

    // Punctuation needs a synchronous highlighted-candidate response on the TSF pipe.
    // Reply before candidate recomputation work so the TSF-side timeout sentinel
    // keeps its original meaning instead of masking latency here.
    if (is_commit_with_highlighted_candidate_punctuation)
    {
        Global::MsgTypeToTsf = Global::DataFromServerMsgType::Normal;
        const bool has_active_composition = g_inputSession != nullptr && !g_inputSession->get_pinyin_sequence().empty();
        if (has_active_composition)
        {
            ScopedKeyStage ensure_punct{client_id, activation_epoch, request_id, L"handle-candidates"};
            EnsureCandidatePageReady();
            ensure_punct.Stop();
            auto &ui = Global::candidate_ui;
            ui.selected_text = FanyImeIpc::HighlightedCandidateText(ui.page_words, ui.selected_index_in_page);

            WordItem highlighted_item;
            const bool highlighted_resolved = ResolveCandidateItem(ui.selected_index_in_page + 1, highlighted_item);
            // 首词命中率：标点收尾带上屏候选时也是一次中文候选提交（用户决策 1：只有
            // 「纯标点收尾不带候选」才排除）。真正上屏的那条候选要么是高亮项，要么（高亮
            // 停在「📅日期」入口上时）是本页第一条；is_first 按实际提交的那条算。
            int committed_index_in_page = -2;
            WordItem committed_item;
            if (highlighted_resolved && metasequoia::local_modes::is_date_time_menu_item(highlighted_item))
            {
                // 「📅日期」入口不是要上屏的字：高亮停在它上面时，标点带出本页首个候选。
                ui.selected_text = FanyImeIpc::HighlightedCandidateText(ui.page_words, 0);
                // 计数门要看真正上屏的那条（本页第一条），不是高亮的日期入口（DateTime 会被排除）。
                if (ResolveCandidateItem(1, committed_item))
                {
                    committed_index_in_page = 0;
                }
            }
            else if (highlighted_resolved)
            {
                committed_index_in_page = ui.selected_index_in_page;
                committed_item = highlighted_item;
                if (word_character_direction != 0)
                {
                    const auto edge = word_character_direction < 0 ? FanyImeIpc::HanCharacterEdge::First
                                                                   : FanyImeIpc::HanCharacterEdge::Last;
                    const auto character = FanyImeIpc::ExtractHanCharacter(CandidateOutputText(highlighted_item), edge);
                    if (character)
                    {
                        Global::MsgTypeToTsf = Global::DataFromServerMsgType::CommitExactText;
                        ui.selected_text = string_to_wstring(*character);
                    }
                }
            }
            SendCurrentDataToClient(client_id, activation_epoch, request_id);
        }
        else
        {
            ClearState();
        }
        return;
    }

    /* 先处理一下通用的按键，包括所有可能的按键，如普通的拼音字符按键、空格、Tab
     * 等等，然后再在下面处理其中的特殊的按键 */
    bool composition_restored = false;
    // The client arms its Backspace reply hold from its own creating-word mirror,
    // which mirrors the state before this key. Capture that shape here so the
    // reply below can still answer a Backspace that ends the word (the post-key
    // shape is gone by then, and the hold would otherwise burn its full timeout).
    bool retreat_backspace_shape_before_key = false;
    // 前缀重算让所有编辑键（含字母/Delete）都需要协商结果；段操作（Ctrl+Backspace /
    // Ctrl+方向）的键位与修饰键条件仍由各自的 chord 判定把守，这里放宽键位限制不影
    // 响它们。
    const bool client_supports_restore = is_composition_edit_key && ClientNegotiatedCompositionRestore(client_id);
    if (is_composition_edit_key)
    {
        retreat_backspace_shape_before_key =
            Global::Keycode == VK_BACK &&
            FanyImeIpc::HasRetreatBackspaceShape(GlobalIme::composition.creating_word.active, IsUiLessMode(),
                                                 client_supports_restore);
        ScopedKeyStage engine_edit{client_id, activation_epoch, request_id, L"handle-engine"};
        ApplyCompositionEditKey(Global::Keycode, Global::Wch, Global::ModifiersDown, client_supports_restore,
                                composition_restored);
        // 横排候选左右键移的是光标：TSF 不再自己移，等 CompositionRestored 里的光标（原地不动也要回这一帧）。
        if (horizontal_arrow_key)
        {
            composition_restored = true;
        }
    }
    else if (should_forward_key_to_session)
    {
        ScopedKeyStage engine_forward{client_id, activation_epoch, request_id, L"handle-engine"};
        g_inputSession->handle_key(Global::Keycode, Global::ModifiersDown, Global::Wch);
    }
    GlobalIme::composition.segmented_pinyin = g_inputSession->get_pinyin_segmentation_with_cases();
    GlobalIme::composition.raw_input_with_cases = g_inputSession->get_pinyin_sequence_with_cases();
    if (g_english_input_mode)
    {
        // English candidates are queried by the raw spelling. Do not expose
        // the Chinese pinyin session's syllable boundaries in the preedit.
        GlobalIme::composition.segmented_pinyin = GlobalIme::composition.raw_input_with_cases;
    }
    if (g_inputSession->get_pinyin_sequence_with_cases().empty())
    {
        ClearSpecialModeTriggers();
    }
    if (!g_english_input_mode && IsRadicalCompositionActive(GlobalIme::composition.raw_input_with_cases))
    {
        // Keep preedit identical to the typed U+letters sequence.
        GlobalIme::composition.segmented_pinyin = GlobalIme::composition.raw_input_with_cases;
    }
    if (!g_english_input_mode && IsDateTimeCompositionActive(GlobalIme::composition.raw_input_with_cases))
    {
        GlobalIme::composition.segmented_pinyin = GlobalIme::composition.raw_input_with_cases;
    }
    if (!g_english_input_mode && IsQuickPhraseCompositionActive(GlobalIme::composition.raw_input_with_cases))
    {
        // Keep preedit identical to the typed K-prefixed code.
        GlobalIme::composition.segmented_pinyin = GlobalIme::composition.raw_input_with_cases;
    }
    if (!g_english_input_mode && IsEmojiCompositionActive(GlobalIme::composition.raw_input_with_cases))
    {
        // Keep preedit identical to the typed E-prefixed code.
        GlobalIme::composition.segmented_pinyin = GlobalIme::composition.raw_input_with_cases;
    }
    if (!g_english_input_mode && IsKaomojiCompositionActive(GlobalIme::composition.raw_input_with_cases))
    {
        // Keep preedit identical to the typed M-prefixed code.
        GlobalIme::composition.segmented_pinyin = GlobalIme::composition.raw_input_with_cases;
    }
    if (!g_english_input_mode && IsSymbolCompositionActive(GlobalIme::composition.raw_input_with_cases))
    {
        // Keep preedit identical to the typed S-prefixed code.
        GlobalIme::composition.segmented_pinyin = GlobalIme::composition.raw_input_with_cases;
    }
    if (!g_english_input_mode && IsYModeCompositionActive(GlobalIme::composition.raw_input_with_cases))
    {
        // Keep preedit identical to the typed Y-prefixed English.
        GlobalIme::composition.segmented_pinyin = GlobalIme::composition.raw_input_with_cases;
    }
    if (!g_english_input_mode && IsVModeCompositionActive(GlobalIme::composition.raw_input_with_cases))
    {
        // Keep preedit identical to the typed V-prefixed number or expression.
        GlobalIme::composition.segmented_pinyin = GlobalIme::composition.raw_input_with_cases;
    }

    // The shape the key leaves behind: a Backspace that keeps the creating word
    // alive still owes the client's hold a frame even when nothing was restored
    // (a plain deletion behind the edit lock, or a no-op).
    const bool retreat_backspace_shape_after_key =
        Global::Keycode == VK_BACK && FanyImeIpc::HasRetreatBackspaceShape(GlobalIme::composition.creating_word.active,
                                                                           IsUiLessMode(), client_supports_restore);

    //
    // 普通的拼音字符，发送 preedit 到 TSF 端
    //
    if (FanyImeIpc::ShouldSendCompositionReply(Global::Keycode >= 'A' && Global::Keycode <= 'Z',
                                               is_manual_pinyin_separator,
                                               is_date_time_input_key || is_v_mode_input_key))
    {
        if (IsUiLessMode())
        {
            ScopedKeyStage prepare_reply_uiless{client_id, activation_epoch, request_id, L"handle-candidates"};
            PrepareCandidateList(client_id, activation_epoch);
            prepare_reply_uiless.Stop();
            SendUiLessCompositionToClient(client_id, activation_epoch, request_id);
        }
        else
        {
            if (GlobalSettings::getTsfPreeditStyle() == GlobalSettings::TsfPreeditStyle::Pinyin)
            {
                std::wstring preedit = GetTsfPreedit();
                Global::MsgTypeToTsf = Global::DataFromServerMsgType::Preedit;
                Global::candidate_ui.selected_text = preedit;
                SendCurrentDataToClient(client_id, activation_epoch, request_id);
            }
        }
    }
    else if (Global::Keycode == VK_BACK || Global::Keycode == VK_DELETE || composition_restored)
    {
        if (IsUiLessMode())
        {
            if (g_inputSession->get_pinyin_sequence().empty())
            {
                ClearState();
                Global::MsgTypeToTsf = Global::DataFromServerMsgType::UiLessComposition;
                Global::candidate_ui.selected_text = L"\t";
                SendCurrentDataToClient(client_id, activation_epoch, request_id);
            }
            else
            {
                ScopedKeyStage prepare_backspace_uiless{client_id, activation_epoch, request_id, L"handle-candidates"};
                PrepareCandidateList(client_id, activation_epoch);
                prepare_backspace_uiless.Stop();
                SendUiLessCompositionToClient(client_id, activation_epoch, request_id);
            }
        }
        else if (FanyImeIpc::ShouldAnswerRetreatBackspace(composition_restored, retreat_backspace_shape_before_key,
                                                          retreat_backspace_shape_after_key))
        {
            // Unlike an ordinary deletion, the retraction and the unit caret
            // jump are not mirrored by TSF on its own: for a deletion TSF
            // rebuilds its keystroke buffer from this payload, and for a jump
            // it applies the caret field. It must therefore be sent in both
            // preedit styles, and the trailing caret field pins the
            // authoritative caret.
            //
            // Every Backspace the client may be holding for gets this frame --
            // retreat, plain character deletion behind the edit lock, or a
            // no-op behind an empty history: the DLL arms its hold from the
            // creating-word mirror it saw before the key (word_for_creating_word),
            // and without this frame the default raw preedit style would send no
            // reply at all, burning the hold's full 50 ms timeout. The pre-key
            // shape keeps that promise for the Backspace that deletes the last
            // raw character and ends the word: the post-key shape is gone by
            // then, while the client is still holding. The payload then
            // describes the state the key left behind -- restored, unchanged, or
            // one character shorter when the caret deletion in
            // ApplyCompositionEditKey ran -- which is what the hold applies in
            // every outcome. The candidate page follows it below.
            Global::MsgTypeToTsf = Global::DataFromServerMsgType::CompositionRestored;
            Global::candidate_ui.selected_text =
                BuildCreateWordPipePayload(GlobalIme::composition.raw_input_with_cases,
                                           GlobalIme::composition.creating_word.prefix_text) +
                L'\t' + std::to_wstring(GlobalIme::composition.caret_position);
            SendCurrentDataToClient(client_id, activation_epoch, request_id);
            ScopedKeyStage publish_restored{client_id, activation_epoch, request_id, L"handle-candidates"};
            PublishRestoredCompositionCandidates(client_id, activation_epoch);
        }
        else if (GlobalSettings::getTsfPreeditStyle() == GlobalSettings::TsfPreeditStyle::Pinyin)
        {
            if (!g_inputSession->get_pinyin_sequence().empty())
            {
                std::wstring preedit = GetTsfPreedit();
                Global::MsgTypeToTsf = Global::DataFromServerMsgType::Preedit;
                Global::candidate_ui.selected_text = preedit;
                SendCurrentDataToClient(client_id, activation_epoch, request_id);
            }
        }
    }
    else if (IsUiLessMode() && is_composition_edit_key && Global::Keycode != VK_LEFT && Global::Keycode != VK_RIGHT &&
             Global::Keycode != VK_BACK)
    {
        ScopedKeyStage prepare_edit_uiless{client_id, activation_epoch, request_id, L"handle-candidates"};
        PrepareCandidateList(client_id, activation_epoch);
        prepare_edit_uiless.Stop();
        SendUiLessCompositionToClient(client_id, activation_epoch, request_id);
    }

    //
    // 在以下情况下，TSF 端会请求候选字符串
    //  - 空格，会上屏第一个候选项
    //  - 数字，会上屏相应序号对应的候选项
    //
    // 空格和数字键可能会触发造词，如果数字键上屏的汉字字符串所对应的拼音比实际的拼音要短的话，
    // 那么，就可能会触发造词事件，那么，就要适时改变候选框的状态
    //
    /* VK_SPACE, Digits (T-mode digits that extend a date/time are input) */
    if (Global::Keycode == VK_SPACE ||
        (!is_date_time_input_key && !is_v_mode_input_key && Global::Keycode > '0' && Global::Keycode <= '9'))
    {
        ScopedKeyStage select_space_digit{client_id, activation_epoch, request_id, L"handle-selection"};
        ProcessSelectionKey(Global::Keycode, client_id, activation_epoch);
        select_space_digit.Stop();
        SendCurrentDataToClient(client_id, activation_epoch, request_id);
    }
    else if ((Global::Keycode == VK_LEFT || Global::Keycode == VK_RIGHT) && !arrow_moves_selection)
    {
        if (IsUiLessMode())
        {
            ScopedKeyStage prepare_arrow_uiless{client_id, activation_epoch, request_id, L"handle-candidates"};
            PrepareCandidateList(client_id, activation_epoch);
            prepare_arrow_uiless.Stop();
            SendUiLessCompositionToClient(client_id, activation_epoch, request_id);
        }
        else
        {
            // R2/R4：前缀重算生效时光标移动会改变候选内容——前缀为空则收起候选窗，
            // 其余（含移回串尾）一律从引擎重读重建页面。移回串尾时引擎已按整串重算，
            // 但页面 items 还是旧前缀候选，只刷新页面会让那批旧候选参与结算（真机回归：
            // ni'hao'ya 右移回串尾后空格只上屏「你好」+「ya」）。整串解码（未启用或未协商）
            // 维持只刷新页面的现状（R7/AC8 零差异）。
            const std::string caret_raw = g_inputSession->get_pinyin_sequence_with_cases();
            const std::size_t prefix_end = g_inputSession->prefix_end();
            const bool caret_resegmentation = FanyImeIpc::ShouldResegmentCompositionByCaret(
                client_supports_restore, IsUiLessMode(), g_english_input_mode,
                IsSpecialModeCompositionActive(caret_raw));
            switch (FanyImeIpc::ResolveCaretArrowCandidatePublish(caret_resegmentation, prefix_end, caret_raw.size()))
            {
            case FanyImeIpc::CaretArrowCandidatePublish::Hide:
                HideCandidateWindowAndDropItems();
                break;
            case FanyImeIpc::CaretArrowCandidatePublish::RebuildFromEngine:
                // 窗口可能因之前的前缀为空状态被收起（单音节后缀从 caret=0 右移两次），
                // 必须显式请求显示；PrepareCandidateList 末尾自带 RefreshCandidatePageUi(false)。
                {
                    ScopedKeyStage prepare_arrow_rebuild{client_id, activation_epoch, request_id, L"handle-candidates"};
                    PrepareCandidateList(client_id, activation_epoch);
                }
                RequestShowCandidateWindow();
                break;
            case FanyImeIpc::CaretArrowCandidatePublish::RefreshPageOnly: {
                ScopedKeyStage refresh_arrow_page{client_id, activation_epoch, request_id, L"handle-candidates"};
                RefreshCandidatePageUi(true);
            }
            break;
            }
        }
    }
    else if ((IsCandidateNavigationKey(Global::Keycode) || arrow_moves_selection) && !is_v_mode_input_key)
    {
        auto &ui = Global::candidate_ui;
        UINT result = Global::DataFromServerMsgType::NavigationIgnored;
        bool refresh = false;

        const auto move_page = [&](int offset, UINT response_type) {
            result = response_type;
            // Keyboard paging keeps the in-page selection where it is; the wheel
            // path in WorkerThread is the one that restarts it at the top.
            ScopedKeyStage move_page_nav{client_id, activation_epoch, request_id, L"handle-candidates"};
            if (MoveCandidatePage(offset) != PageMoveResult::Unchanged)
            {
                refresh = true;
            }
        };
        const auto move_selection = [&](int offset, UINT response_type) {
            result = response_type;
            // 要在下面的扩展之前标记：扩展得按「已开始选词」只追加，不能重排已显示的候选。
            ui.begin_selection();
            if (offset > 0 && (ui.is_selection_at_last_candidate() ||
                               (ui.is_selection_at_current_page_end() && ui.is_next_page_partial_last_page())))
            {
                ScopedKeyStage expand_nav{client_id, activation_epoch, request_id, L"handle-candidates"};
                ExpandCandidatesKeepingPagePosition();
            }
            if (ui.move_selection(offset))
            {
                refresh = true;
            }
        };

        const bool shift_down = (Global::ModifiersDown & 0b00000001u) != 0;
        // Shift+- / Shift+= append the ASCII symbol to the raw spelling. The key
        // stays claimed by the IME, so the reply has to rebuild the composition;
        // MoveCandidatePage would otherwise swallow it as a page turn. Hosts that
        // cannot apply the frame fall through to the ordinary paging path.
        if (GetConfiguredPagingMinusEqualEnabled() &&
            FanyImeIpc::IsShiftedPagingSymbolKey(Global::Keycode, Global::Wch, Global::ModifiersDown) &&
            AppendLiteralToComposition(Global::Wch, client_id, activation_epoch, request_id))
        {
            return;
        }
        if (Global::Keycode == VK_OEM_MINUS && GetConfiguredPagingMinusEqualEnabled())
        {
            move_page(-1, Global::DataFromServerMsgType::MovePagePrevious);
        }
        else if (Global::Keycode == VK_OEM_PLUS && GetConfiguredPagingMinusEqualEnabled())
        {
            move_page(1, Global::DataFromServerMsgType::MovePageNext);
        }
        else if (Global::Keycode == VK_OEM_COMMA && GetConfiguredPagingCommaPeriodEnabled())
        {
            move_page(-1, Global::DataFromServerMsgType::MovePagePrevious);
        }
        else if (Global::Keycode == VK_OEM_PERIOD && GetConfiguredPagingCommaPeriodEnabled())
        {
            move_page(1, Global::DataFromServerMsgType::MovePageNext);
        }
        else if (Global::Keycode == VK_OEM_4 && GetConfiguredPagingBracketsEnabled())
        {
            move_page(-1, Global::DataFromServerMsgType::MovePagePrevious);
        }
        else if (Global::Keycode == VK_OEM_6 && GetConfiguredPagingBracketsEnabled())
        {
            move_page(1, Global::DataFromServerMsgType::MovePageNext);
        }
        else if (Global::Keycode == VK_TAB && GetConfiguredPagingTabEnabled())
        {
            move_page(shift_down ? -1 : 1, shift_down ? Global::DataFromServerMsgType::MovePagePrevious
                                                      : Global::DataFromServerMsgType::MovePageNext);
        }
        else if (Global::Keycode == VK_PRIOR && GetConfiguredPagingPageUpDownEnabled())
        {
            move_page(-1, Global::DataFromServerMsgType::MovePagePrevious);
        }
        else if (Global::Keycode == VK_NEXT && GetConfiguredPagingPageUpDownEnabled())
        {
            move_page(1, Global::DataFromServerMsgType::MovePageNext);
        }
        else if (arrow_moves_selection)
        {
            if (Global::Keycode == VK_LEFT)
            {
                move_selection(-1, Global::DataFromServerMsgType::MoveSelectionPrevious);
            }
            else
            {
                move_selection(1, Global::DataFromServerMsgType::MoveSelectionNext);
            }
        }
        else if (GetConfiguredCandidateArrowNavigationEnabled() &&
                 (Global::Keycode == VK_UP || Global::Keycode == VK_DOWN))
        {
            if (Global::Keycode == VK_UP)
            {
                move_selection(-1, Global::DataFromServerMsgType::MoveSelectionPrevious);
            }
            else
            {
                move_selection(1, Global::DataFromServerMsgType::MoveSelectionNext);
            }
        }

        if (IsUiLessMode())
        {
            if (refresh)
            {
                ScopedKeyStage refresh_nav_uiless{client_id, activation_epoch, request_id, L"handle-candidates"};
                RefreshCandidatePageUi(false);
            }
            else
            {
                ScopedKeyStage ensure_nav_uiless{client_id, activation_epoch, request_id, L"handle-candidates"};
                EnsureCandidatePageReady();
            }
            // Prefer selection index in page for host-drawn lists.
            if (!ui.page_words.empty())
            {
                ui.selected_index_in_page =
                    std::clamp(ui.selected_index_in_page, 0, static_cast<int>(ui.page_words.size()) - 1);
            }
            SendUiLessCompositionToClient(client_id, activation_epoch, request_id);
        }
        else
        {
            Global::MsgTypeToTsf = result;
            SendCurrentDataToClient(client_id, activation_epoch, request_id);
            if (refresh)
            {
                ScopedKeyStage refresh_nav{client_id, activation_epoch, request_id, L"handle-candidates"};
                RefreshCandidatePageUi(true);
            }
        }
    }
}
} // namespace FanyNamedPipe
