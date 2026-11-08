#include "input_session.h"
#include "data_path.h"
#include "../common/string_utils.h"
#include "../contracts/assets/assets.h"
#include "../user_dictionary/user_dictionary_journal.h"
#include "../quanpin/quanpin_utils.h"
#include <algorithm>
#include <cctype>

namespace metasequoia
{
void InputSession::enable_fixed_positions()
{
    fixed_positions_enabled_ = true;
    update_mixed_candidates();
}

std::string InputSession::position_context(bool english) const
{
    if (english)
    {
        std::string input = dedicated_english_mode_ ? dedicated_english_preedit_
                                                    : (local_input_mode_ == LocalInputMode::TemporaryEnglish
                                                           ? local_preedit_.substr(1)
                                                           : engine_.get_request().raw_input_with_cases);
        std::transform(input.begin(), input.end(), input.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return "english:" + input;
    }
    std::string context = get_quanpin();
    if (context.empty())
        context = get_pinyin_segmentation();
    if (engine_.get_request().raw_input.size() == 1)
        return context;
    auto plain = context;
    plain.erase(std::remove(plain.begin(), plain.end(), '\''), plain.end());
    // 全拼的上下文 key 跟查询走同一个纠错开关：关掉纠错时 laing 的候选按字面 lai'ng 出，
    // key 若仍被别名层切成 liang，两组固定位置会互相串。
    const bool correction_aliases = quanpin::correction_aliases_enabled(quanpin_autocorrect_types_);
    const auto cuts = quanpin::cut_pinyin_by_mode(plain, "correction", correction_aliases);
    return cuts.empty() ? context : quanpin::join_segments(cuts.front());
}

void InputSession::apply_candidate_positions(std::vector<WordItem> &items)
{
    if (!fixed_positions_enabled_ || items.empty())
        return;
    const auto journal = path_to_utf8(paths_.user(assets::user_journal));
    const bool regular = local_input_mode_ == LocalInputMode::None && !dedicated_english_mode_;
    if (regular)
    {
        user_dictionary::apply_fixed_positions(journal, position_context(false), items,
                                               engine_.get_request().raw_input.size() == 1,
                                               [this](const std::string &key, const std::string &word) {
                                                   return engine_.find_candidate(scheme(), key, word);
                                               });
    }
    if (std::any_of(items.begin(), items.end(),
                    [](const auto &item) { return item.source == CandidateSource::EnglishDictionary; }))
        user_dictionary::apply_fixed_positions(journal, position_context(true), items, false, {}, true);
}

KeyResult InputSession::set_candidate_position(std::size_t index, int position)
{
    if (position < 0 || position > 5 || index >= candidates().size())
        return {};
    const auto selected = candidates()[index];
    const bool english = selected.source == CandidateSource::EnglishDictionary;
    if (!english &&
        ((selected.source != CandidateSource::Database && selected.source != CandidateSource::UserDatabase)))
        return {};
    const auto context = position_context(english);
    const auto key =
        english ? selected.pinyin : (selected.canonical_pinyin.empty() ? selected.pinyin : selected.canonical_pinyin);
    if (context.empty() || key.empty())
        return {};
    const auto journal = path_to_utf8(paths_.user(assets::user_journal));
    const bool ok = position == 0 ? user_dictionary::clear_fixed_position(journal, context, key, selected.word)
                                  : user_dictionary::set_fixed_position(journal, context, key, selected.word, position);
    if (!ok)
        return {true, std::nullopt, "Unable to persist candidate position."};
    reset_cache();
    if (dedicated_english_mode_)
        update_dedicated_english_candidates();
    else if (local_input_mode_ != LocalInputMode::None)
        return {true, std::nullopt, update_local_candidates()};
    else
        recompute_candidates();
    return {true, std::nullopt, std::nullopt};
}

KeyResult InputSession::remove_candidate(std::size_t index)
{
    if (index >= candidates().size())
        return {};
    const auto selected = candidates()[index];
    const bool english = selected.source == CandidateSource::EnglishDictionary;
    if (!english &&
        ((selected.source != CandidateSource::Database && selected.source != CandidateSource::UserDatabase) ||
         CommonUtils::count_utf8_chars(selected.word) <= 1))
        return {};

    const auto kind = english ? user_dictionary::DictionaryKind::English : user_dictionary::DictionaryKind::Pinyin;
    // A displayed pinyin candidate already carries its exact dictionary key. Re-segmenting it
    // can delete a different pronunciation of the same word.
    const auto &key = english ? selected.pinyin : selected.canonical_pinyin;
    if (key.empty())
        return {};
    if (!user_dictionary::delete_dictionary_candidate(
            path_to_utf8(paths_.dictionary(english ? assets::english_dictionary : assets::main_dictionary)),
            path_to_utf8(paths_.user(assets::user_journal)), kind, key, selected.word))
        return {true, std::nullopt, "Unable to persist candidate removal."};

    reset_cache();
    if (dedicated_english_mode_)
        update_dedicated_english_candidates();
    else if (local_input_mode_ != LocalInputMode::None)
        return {true, std::nullopt, update_local_candidates()};
    else
        recompute_candidates();
    return {true, std::nullopt, std::nullopt};
}
} // namespace metasequoia
