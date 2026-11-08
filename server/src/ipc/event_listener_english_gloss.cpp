// 候选词英文上屏：候选条下面的英文行（Ctrl+Enter 上屏）与英文词义清单页（Ctrl+Shift+Enter 展开）。
// 释义只来自本机 english.db 的中译英表（及 custom_translations.txt 覆盖），清洗规则见
// ipc/english_gloss_policy.h。上屏的英文不调频、不写用户词库；只覆盖一部分拼音时只把这一段换成英文，
// 剩下的拼音继续组字，和普通的部分上屏一致。
#include "ipc/event_listener_internal.h"
#include <Windows.h>
#include <string>
#include <algorithm>
#include <cstdint>
#include <utility>
#include "ipc.h"
#include "ipc/input_key_policy.h"
#include "defines/globals.h"
#include "global/globals.h"
#include "config/ime_config.h"
#include "utils/common_utils.h"

using namespace event_listener_detail;

namespace
{
std::string BindingKeyLabel(uint32_t keycode)
{
    if (keycode == VK_RETURN)
        return "\xE2\x86\xB5"; // ↵
    if (keycode == VK_SPACE)
        return "Space";
    for (const auto &named : FanyImeKeyChord::NamedKeys)
    {
        if (named.key == keycode)
            return std::string(named.label);
    }
    if (keycode >= 'A' && keycode <= 'Z')
        return std::string(1, static_cast<char>(keycode));
    if (keycode >= '0' && keycode <= '9')
        return std::string(1, static_cast<char>(keycode));
    return {};
}

// 英文行右侧那条淡色提示：跟着 commit_english 的配置走，键位改了提示也跟着改。
std::wstring MakeEnglishLineHint()
{
    FanyImeKeyChord::Binding parsed;
    if (!FanyImeKeyChord::Parse(GetConfiguredCommitEnglishBinding(), parsed) || parsed.key == 0)
        return {};
    std::string label;
    if ((parsed.modifiers & FanyImeKeyChord::Control) != 0)
        label += "Ctrl+";
    if ((parsed.modifiers & FanyImeKeyChord::Shift) != 0)
        label += "Shift+";
    if ((parsed.modifiers & FanyImeKeyChord::Alt) != 0)
        label += "Alt+";
    label += BindingKeyLabel(parsed.key);
    return string_to_wstring(label) + L" 上屏";
}

// 造词中间态的管道载荷：前缀（已上屏的文字）+ 剩余拼音 + 光标。与 ProcessSelectionKey 的
// BuildCreateWordPipePayloadWithCaret 同一形状。
std::wstring BuildEnglishCreatingWordPayload(uint64_t client_id)
{
    auto &composition = GlobalIme::composition;
    std::wstring payload = BuildCreateWordPipePayload(g_inputSession->get_pinyin_sequence_with_cases(),
                                                      composition.creating_word.prefix_text);
    if (FanyImeIpc::ShouldCreateWordFrameCarryCaret(ClientNegotiatedCompositionRestore(client_id),
                                                    composition.caret_position,
                                                    g_inputSession->get_pinyin_sequence_with_cases().size()))
    {
        payload += L'\t' + std::to_wstring(composition.caret_position);
    }
    return payload;
}
} // namespace

namespace event_listener_detail
{
std::wstring EnglishLineHint()
{
    return MakeEnglishLineHint();
}

bool EnglishGlossFeatureEnabled()
{
    if (GetConfiguredEnglishLineEnabled())
        return true;
    FanyImeKeyChord::Binding parsed;
    return (FanyImeKeyChord::Parse(GetConfiguredCommitEnglishBinding(), parsed) && parsed.key != 0) ||
           (FanyImeKeyChord::Parse(GetConfiguredEnglishSensesBinding(), parsed) && parsed.key != 0);
}

std::string HighlightedEnglishGloss()
{
    const auto &ui = Global::candidate_ui;
    if (ui.page_glosses.empty())
        return {};
    const int index = std::clamp(ui.selected_index_in_page, 0, static_cast<int>(ui.page_glosses.size()) - 1);
    return wstring_to_string(ui.page_glosses[static_cast<size_t>(index)]);
}

void NotifyEnglishSensesPage(bool active)
{
    const FanyImeIpc::CandidateUiOwner owner = SnapshotCandidateUiOwner();
    if (!owner || !IsPipeActivationCurrent(owner.client_id, owner.activation_epoch))
        return;
    SendToTsfWorkerThreadClientViaNamedpipe(owner.client_id, owner.activation_epoch,
                                            Global::DataFromServerMsgTypeToTsfWorkerThread::EnglishSensesPageChanged,
                                            active ? L"1" : L"0");
}

bool CommitEnglishGlossText(const std::string &english, uint64_t client_id, uint64_t activation_epoch)
{
    if (english.empty() || g_inputSession == nullptr)
        return false;

    auto &ui = Global::candidate_ui;
    auto &composition = GlobalIme::composition;
    WordItem item;
    if (!FanyNamedPipe::ResolveCandidateItem(ui.selected_index_in_page + 1, item))
        return false;

    const std::string raw_before = g_inputSession->get_pinyin_sequence_with_cases();
    const std::string selection_page_prefix =
        FanyImeIpc::NormalizeCandidatePagePrefix(raw_before, g_inputSession->prefix_end());
    const bool caret_prefix_selection = g_inputSession->prefix_end() < raw_before.size();
    // 组合按中文候选的读音推进（advance 只看读音与消耗的字母数），上屏文字换成英文。
    auto transition =
        g_inputSession->advance_composition_after_selection(item.pinyin, item.word, item.canonical_pinyin);
    if (caret_prefix_selection)
    {
        g_inputSession->set_caret(std::nullopt);
        g_inputSession->recompute_candidates();
        GlobalIme::composition.caret_position = 0;
    }

    if (!transition.continues_composition)
    {
        // 整段上屏：回 Normal，TSF 写入 selected_text 并结束组合（SendCurrentDataToClient 收尾时清状态）。
        // 前面已经选过的段照常算进上屏文字（和造词收尾一致）；这些段也不补写词库，英文本来就不入词库。
        std::string commit_text = composition.creating_word.prefix_text + english;
        if (commit_text.empty())
            commit_text = english;
        composition.clear_creating_word();
        Global::MsgTypeToTsf = Global::DataFromServerMsgType::Normal;
        ui.selected_text = string_to_wstring(commit_text);
        return true;
    }

    // 只覆盖一部分拼音：英文顶掉这一段，剩下的拼音继续组字。造词状态里的 word 只是显示与撤回用的
    // 已选文字；它的读音留空，引擎的 can_store 会因此拒绝落库，上屏的英文也不参与调频。
    composition.push_selection_snapshot(transition.consumed_raw_input_with_cases, -1, selection_page_prefix);
    composition.creating_word.active = true;
    composition.creating_word.includes_english = true;
    composition.creating_word.word += english;
    composition.creating_word.output_text += english;
    composition.creating_word.all_first_spellings = false;
    composition.creating_word.prefix_text = composition.creating_word.output_text;
    composition.segmented_pinyin = transition.current_segmentation_with_cases;
    composition.raw_input_with_cases = g_inputSession->get_pinyin_sequence_with_cases();
    Global::MsgTypeToTsf = Global::DataFromServerMsgType::NeedToCreateWord;
    FanyNamedPipe::PrepareCandidateList(client_id, activation_epoch);
    ui.selected_text = BuildEnglishCreatingWordPayload(client_id);
    return true;
}

bool CommitEnglishLine(uint64_t client_id, uint64_t activation_epoch)
{
    if (IsUiLessMode() || IsCandidateSubPageActive() || Global::candidate_ui.items.empty())
        return false;
    EnsureCandidatePageReady();
    const std::string english = FanyImeIpc::FirstEnglishSense(HighlightedEnglishGloss());
    if (english.empty())
        return false;
    return CommitEnglishGlossText(english, client_id, activation_epoch);
}

bool EnterEnglishSensesPage(uint64_t client_id, uint64_t activation_epoch)
{
    if (IsUiLessMode() || IsCandidateSubPageActive())
        return false;
    auto &ui = Global::candidate_ui;
    EnsureCandidatePageReady();
    WordItem item;
    if (ui.items.empty() || !FanyNamedPipe::ResolveCandidateItem(ui.selected_index_in_page + 1, item))
        return false;

    g_english_senses_page_senses = FanyImeIpc::SplitEnglishSenses(HighlightedEnglishGloss());
    g_english_senses_page_word = CandidateOutputText(item);
    g_translation_saved_items = ui.items;
    g_translation_saved_page_index = ui.page_index;
    g_translation_saved_selected_index = ui.selected_index_in_page;
    g_translation_saved_selection_started = ui.selection_started;
    g_english_senses_page_active = true;
    ui.selected_index_in_page = 0;
    RefreshCandidatePageUi(true);
    NotifyEnglishSensesPage(true);
    return true;
}
} // namespace event_listener_detail
