// 异步候选结果回到任务线程后的合并：神经重排、专用英文、译文，以及混输
// （英文、emoji、颜文字、日期时间）。
#include "ipc/event_listener_internal.h"
#include <Windows.h>
#include <string>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <utility>
#include "ipc.h"
#include "ipc/candidate_selection_policy.h"
#include "ipc/candidate_ui_owner.h"
#include "ipc/traditional_variant_expansion_policy.h"
#include "global/globals.h"
#include "engine/user_dictionary/user_dictionary_journal.h"
#include "english/english_ime.h"
#include "config/ime_config.h"
#include "mixed/mixed_candidates.h"

using namespace event_listener_detail;

namespace FanyNamedPipe
{
bool RequeryCandidatesAfterAsyncMerge(const FanyImeIpc::CandidateUiOwner &owner)
{
    const std::vector<WordItem> before = g_inputSession->get_candidates();
    // 只失效整句那层：逐前缀的词库查询和切分与重排无关，保持热缓存，下一键也不会变冷。
    // 不用 reset_cache——它自己会先冷算一遍候选，这里紧接着又算一遍。
    g_inputSession->reset_sentence_cache();
    g_inputSession->recompute_candidates();
    const std::vector<WordItem> &after = g_inputSession->get_candidates();
    if (after.size() == before.size() &&
        std::equal(before.begin(), before.end(), after.begin(),
                   [](const WordItem &a, const WordItem &b) { return a.word == b.word; }))
        return false;

    PrepareCandidateList(owner.client_id, owner.activation_epoch);
    RequestShowCandidateWindow();
    PushUiLessCandidatesToOwner();
    return true;
}

// 候选列表当初是按词格的静态顺序发出去的，因为打分那会儿还没算完。现在算完了：把引擎的候选缓存
// 丢掉重查一次，这一次 quanpin::make_neural_reranker 能在结果表里查到顺序，整句就落到它该在的位
// 置上。重查本身不碰模型，走的还是词格那条快路。
//
// 后台线程算的可能已经是上一次输入的了（用户没停手），所以这里不认「哪一批」，只看重查出来的词
// 序有没有真的变：没变就一个字节都不动 UI。这既挡掉了过期结果，也挡掉了模型弃权的情况。
void ApplyRescoredOrder()
{
    if (!g_inputSession || !CanMergeAsyncCandidates())
        return;
    // 造词界面和译文页各自占着 items，重排不该去动它们。
    if (GlobalIme::composition.creating_word.active)
        return;
    if (Global::candidate_ui.items.empty())
        return;
    const FanyImeIpc::CandidateUiOwner owner = SnapshotCandidateUiOwner();
    if (!owner || !IsPipeActivationCurrent(owner.client_id, owner.activation_epoch))
        return;
    (void)RequeryCandidatesAfterAsyncMerge(owner);
}

// 专用英文（英文状态、Shift+Y）的整页英文候选。中文里混进来的英文走 ApplyMixedCandidates。
void ApplyEnglishCandidates(std::vector<WordItem> candidates, const std::string &input, uint64_t generation)
{
    const std::string session_input =
        g_inputSession != nullptr ? g_inputSession->get_pinyin_sequence_with_cases() : std::string{};
    const bool y_mode = IsYModeInput(session_input);
    const bool dedicated_mode = g_english_input_mode || y_mode;
    const std::string expected_input = y_mode ? session_input.substr(1) : session_input;
    if (!dedicated_mode || !EnglishIme::IsCurrent(input, generation, dedicated_mode) || g_inputSession == nullptr ||
        expected_input != input || GlobalIme::composition.creating_word.active || IsCandidateSubPageActive())
    {
        return;
    }
    // 等着这次答复时候选窗还没显示，用户只是在看不见的原文占位上按了方向键，不算开始选词：
    // 照常换上英文候选，答复同时放行被扣下的显示。
    if (Global::candidate_ui.selection_started && !g_dedicated_english_answer_pending)
        return;

    auto &items = Global::candidate_ui.items;
    std::string context_input = input;
    std::transform(context_input.begin(), context_input.end(), context_input.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    user_dictionary::apply_fixed_positions(user_dictionary::default_user_db_path(), "english:" + context_input,
                                           candidates, false, {}, false);
    if (y_mode)
    {
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                        [&](const WordItem &item) {
                                            if (item.word.size() != input.size())
                                                return false;
                                            for (size_t i = 0; i < input.size(); ++i)
                                            {
                                                if (std::tolower(static_cast<unsigned char>(item.word[i])) !=
                                                    std::tolower(static_cast<unsigned char>(input[i])))
                                                    return false;
                                            }
                                            return true;
                                        }),
                         candidates.end());
        candidates.insert(candidates.begin(), WordItem("", input, 0, CandidateSource::Generated));
    }
    else if (candidates.empty() && !input.empty())
    {
        // A raw fallback is selectable, but it is not an english.db row
        // and therefore must not participate in dictionary mutations.
        candidates.emplace_back("", input, 0, CandidateSource::Generated);
    }
    items = std::move(candidates);
    Global::candidate_ui.item_total_count = static_cast<int>(items.size());
    Global::candidate_ui.page_index = 0;
    Global::candidate_ui.select_first_on_page();
    Global::candidate_ui.selection_started = false;
    Global::candidate_ui.clear_page();
    g_dedicated_english_answer_pending = false;
    RefreshCandidatePageUi(true);
    PushUiLessCandidatesToOwner();
}

void ApplyCandidateTranslations(std::vector<EnglishIme::TranslationResult> results, uint64_t generation)
{
    if (!EnglishIme::IsTranslationCurrent(generation) || !GetConfiguredCandidateTranslationsEnabled() ||
        IsUiLessMode() || g_candidate_translation_signature.empty() || IsCandidateSubPageActive())
        return;

    // The page reads its glosses from g_candidate_translation_glosses when it is
    // built, so an unchanged map means the page on screen is already current.
    // Most lookups on a new keystroke hit words the previous page already
    // glossed; repainting for them cost a full candidate frame per keystroke.
    bool glosses_changed = false;
    for (auto &result : results)
    {
        std::string gloss = std::move(result.gloss);
        const bool from_dictionary = !gloss.empty();
        const std::string identity = TranslationIdentity({result.key, result.direction});
        if (!gloss.empty())
        {
            auto existing = g_candidate_translation_glosses.find(identity);
            if (existing != g_candidate_translation_glosses.end() && existing->second.text == gloss &&
                existing->second.from_dictionary == from_dictionary)
                continue;
            if (g_candidate_translation_glosses.size() >= kMaxCandidateTranslationGlosses &&
                existing == g_candidate_translation_glosses.end())
                g_candidate_translation_glosses.clear();
            g_candidate_translation_glosses[identity] = {std::move(gloss), from_dictionary};
            glosses_changed = true;
        }
        else if (g_candidate_translation_glosses.erase(identity) > 0)
        {
            glosses_changed = true;
        }
    }
    const FanyImeIpc::CandidateUiOwner owner = SnapshotCandidateUiOwner();
    if (glosses_changed && owner && IsPipeActivationCurrent(owner.client_id, owner.activation_epoch))
        RefreshCandidatePageUi(true);
}

namespace
{
// 去掉列表里同来源的旧候选和与已有候选同文本的新候选，返回剩下的新候选。
std::vector<WordItem> ReplaceSourceAndDeduplicate(std::vector<WordItem> &items, CandidateSource source,
                                                  std::vector<WordItem> candidates)
{
    items.erase(
        std::remove_if(items.begin(), items.end(), [source](const WordItem &item) { return item.source == source; }),
        items.end());
    std::vector<WordItem> unique_candidates;
    for (auto &candidate : candidates)
    {
        const bool duplicate = std::any_of(items.begin(), items.end(),
                                           [&](const WordItem &item) { return item.word == candidate.word; }) ||
                               std::any_of(unique_candidates.begin(), unique_candidates.end(),
                                           [&](const WordItem &item) { return item.word == candidate.word; });
        if (!duplicate)
            unique_candidates.push_back(std::move(candidate));
    }
    return unique_candidates;
}

// Merge query results; NormalizeMixedCandidateOrder determines their final positions.
void AppendCandidates(std::vector<WordItem> &items, std::vector<WordItem> candidates)
{
    items.insert(items.end(), std::make_move_iterator(candidates.begin()), std::make_move_iterator(candidates.end()));
}

// 首个插到 insert_index（最终位置由 NormalizeMixedCandidateOrder 按来源统一摆），其余追加到末尾。
void InsertFirstAndAppendRest(std::vector<WordItem> &items, std::vector<WordItem> candidates, size_t insert_index)
{
    if (candidates.empty())
        return;
    insert_index = (std::min)(insert_index, items.size());
    items.insert(items.begin() + static_cast<std::ptrdiff_t>(insert_index), std::move(candidates.front()));
    for (size_t index = 1; index < candidates.size(); ++index)
        items.push_back(std::move(candidates[index]));
}
} // namespace

// 混输线程（MixedCandidates）查完一次输入后，把英文、emoji、颜文字、符号、日期时间一并合进当前候选，
// 只刷新一次候选窗。各来源的开关在这里再核一遍：请求发出后用户可能刚在设置页关掉了它。
void ApplyMixedCandidates(MixedCandidates::Result result, const std::string &input, uint64_t generation)
{
    if (!GetConfiguredMixedCandidatesEnabled() || !MixedCandidates::IsCurrent(input, generation) ||
        g_inputSession == nullptr || g_english_input_mode || !CanMergeAsyncCandidates() ||
        g_inputSession->current_scheme_type() != SchemeType::Quanpin ||
        g_inputSession->get_pinyin_sequence_with_cases() != input || GlobalIme::composition.creating_word.active)
    {
        return;
    }

    auto &items = Global::candidate_ui.items;
    const size_t size_before = items.size();
    bool changed = false;

    if (GetConfiguredDateTimeCandidatesEnabled())
    {
        // 「📅日期」入口有自己的开关。UILess 宿主自己画候选，日期页是这边刷新候选窗换出来的，
        // 宿主那边看不到，入口点了也没反应，所以那里也不放。
        if (IsUiLessMode() || !GetConfiguredDateTimeMenuEnabled())
            result.date_time.erase(std::remove_if(result.date_time.begin(), result.date_time.end(),
                                                  [](const WordItem &item) {
                                                      return metasequoia::local_modes::is_date_time_menu_item(item);
                                                  }),
                                   result.date_time.end());
        auto date_time = ReplaceSourceAndDeduplicate(items, CandidateSource::DateTime, std::move(result.date_time));
        changed = changed || !date_time.empty();
        InsertFirstAndAppendRest(items, std::move(date_time), 1);
    }

    if (GetConfiguredEnglishCandidatesEnabled())
    {
        auto english =
            ReplaceSourceAndDeduplicate(items, CandidateSource::EnglishDictionary, std::move(result.english));
        if (!english.empty())
        {
            changed = true;
            user_dictionary::apply_fixed_positions(user_dictionary::default_user_db_path(), EnglishRankingContextKey(),
                                                   english, false);
            const size_t insert_index = std::min<size_t>(1, items.size());
            items.insert(items.begin() + static_cast<std::ptrdiff_t>(insert_index), std::move(english.front()));
            // items 已按写法展开过，固定位置要作用在每组一项的列表上。
            ReorderCandidatesAsOneItemPerWord(items, [](std::vector<WordItem> &collapsed) {
                user_dictionary::apply_fixed_positions(user_dictionary::default_user_db_path(),
                                                       CurrentRankingContextKey(), collapsed, false);
            });
            for (size_t index = 1; index < english.size(); ++index)
                items.push_back(std::move(english[index]));
        }
    }

    if (GetConfiguredEmojiMixedInputEnabled())
    {
        auto emoji = ReplaceSourceAndDeduplicate(items, CandidateSource::Emoji, std::move(result.emoji));
        changed = changed || !emoji.empty();
        AppendCandidates(items, std::move(emoji));
    }

    if (GetConfiguredKaomojiMixedInputEnabled())
    {
        auto kaomoji = ReplaceSourceAndDeduplicate(items, CandidateSource::Kaomoji, std::move(result.kaomoji));
        changed = changed || !kaomoji.empty();
        AppendCandidates(items, std::move(kaomoji));
    }

    if (GetConfiguredSymbolMixedInputEnabled())
    {
        auto symbol = ReplaceSourceAndDeduplicate(items, CandidateSource::Symbol, std::move(result.symbol));
        changed = changed || !symbol.empty();
        AppendCandidates(items, std::move(symbol));
    }

    // 全被去重掉、列表也没少东西时，屏幕上的那页就是对的，不必重画。
    if (!changed && items.size() == size_before)
        return;
    FanyImeIpc::NormalizeMixedCandidateOrder(items, CurrentEnglishPlacement(items));

    Global::candidate_ui.item_total_count = static_cast<int>(items.size());
    Global::candidate_ui.page_index = 0;
    Global::candidate_ui.select_first_on_page();
    Global::candidate_ui.clear_page();
    RefreshCandidatePageUi(true);
    PushUiLessCandidatesToOwner();
}
} // namespace FanyNamedPipe
