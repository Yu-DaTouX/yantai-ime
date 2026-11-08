#pragma once

#include "ime_session.h"
#include "input_session_types.h"
#include "candidate_queries.h"
#include "punctuation_policy.h"
#include "online_request_guard.h"
#include "../local_modes/date_time_query.h"
#include "../english/english_dictionary.h"
#include "word_item.h"
#include "../quanpin/engine.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace metasequoia
{
// Platform-neutral composition session shared by the native frontends. It owns an ImeSession and
// applies the key-handling and commit policy that each frontend would otherwise reimplement, so a
// frontend only has to translate platform key events into these calls.
class InputSession
{
  public:
    // Frontends pass their persisted options at session creation so every platform uses the same
    // engine configuration and commit policy.
    explicit InputSession(SchemeType scheme_type = SchemeType::Quanpin, unsigned quanpin_autocorrect_types = 0,
                          bool chinese_punctuation_enabled = true, bool candidate_learning_enabled = true,
                          RuntimePaths paths = RuntimePaths::legacy());

    // Feeds one lowercase ASCII letter or an in-composition apostrophe. Other input is rejected as
    // unhandled so the frontend can pass it through to the client application.
    KeyResult handle_character(char character, bool shift_only = false);
    // Applies a command. Every command is unhandled while no composition is active, which keeps
    // Backspace and Escape working normally in the client application.
    KeyResult handle_command(Command command);
    // Maps the visible 1-9 candidate keys and Chinese punctuation independently of platform UI.
    KeyResult handle_candidate_key(char character);
    KeyResult handle_punctuation(char character);
    // Commit the selected prefix and retain any unconsumed pinyin. Hosts insert
    // KeyResult::commit and then render the remaining preedit from this session.
    KeyResult select_candidate(std::size_t index);
    // Flush all remaining input for punctuation, scheme changes and host passthrough.
    KeyResult finish_composition(std::size_t first_index = 0);
    KeyResult select_candidate(const std::string &candidate);
    KeyResult select_candidate_edge(std::size_t index, CandidateEdge edge);
    KeyResult pin_candidate(std::size_t index);
    KeyResult remove_candidate(std::size_t index);
    KeyResult set_candidate_position(std::size_t index, int position);
    void enable_fixed_positions();
    bool set_frequency_adjustment(FrequencyAdjustmentOptions options);
    const FrequencyAdjustmentOptions &frequency_adjustment() const;
    void set_local_mode_options(LocalModeOptions options);
    const LocalModeOptions &local_mode_options() const;
    bool set_english_input_options(EnglishInputOptions options);
    const EnglishInputOptions &english_input_options() const;
    void set_mixed_expressive_options(MixedExpressiveOptions options);
    const MixedExpressiveOptions &mixed_expressive_options() const;
    void set_dedicated_english_mode(bool enabled);
    bool dedicated_english_mode() const;
    LocalInputMode local_input_mode() const;
    void set_local_date_time_provider(std::function<local_modes::LocalDateTime()> provider);
    std::optional<OnlineQuery> online_query() const;
    bool apply_online_candidate(const OnlineQuery &query, std::string candidate, CandidateSource source);

    SchemeType scheme_type() const;
    unsigned quanpin_autocorrect_types() const;
    bool chinese_punctuation_enabled() const;
    bool candidate_learning_enabled() const;
    // Switching schemes discards the current composition. A frontend that promises to preserve
    // typed text must commit it before calling this method.
    void switch_scheme(SchemeType scheme_type);
    SchemeType scheme() const;

    bool has_composition() const;
    const std::string &preedit() const;
    std::string editing_text() const;
    std::size_t caret_position() const;
    const std::string &raw_segmentation() const;
    const std::string &normalized_segmentation() const;
    const std::vector<WordItem> &candidates() const;
    // Advanced composition operations for hosts with their own asynchronous text insertion.
    // They share the same engine/configuration as the portable character/command API.
    struct SelectionTransition
    {
        bool continues_composition = false;
        std::string full_pure_pinyin;
        std::string current_segmentation;
        std::string current_segmentation_with_cases;
        std::string selected_canonical_pinyin;
        // Raw spelling the selection consumed from the active scheme's input,
        // in the exact form the user typed it. A host that lets the user retract
        // a selected segment must replay this spelling, not the pre-selection
        // raw: the pre-selection raw also contains suffix characters the user
        // may have deleted since the selection. Sources that consume no input
        // (cloud, associative, whole-word commits) leave it empty.
        std::string consumed_raw_input_with_cases;
    };

    struct CloudQueryState
    {
        bool should_query = false;
        std::string query_text;
        std::string cache_key;
        std::string committed_pinyin;
    };

    struct CreatingWordProgress
    {
        std::string pinyin;
        std::string word;
        std::string preedit;
        bool completed = false;
        bool can_store = false;
    };

    void handle_engine_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch);
    void recompute_candidates();
    // Moves the caret without editing the raw string. nullopt = end of string (full-string
    // decoding, the default). The value is clamped to [0, editing_text().size()]. Setting it
    // only updates state; candidates re-decode by the new boundary on the next
    // recompute_candidates() or key handling.
    void set_caret(std::optional<std::size_t> caret);
    // Raw length consumed by the current decode: the caret moved onto the last complete
    // syllable-unit boundary at or before it (floor). A caret inside the first unit rounds up to
    // the end of that unit instead, so prefix_end() == 0 means caret == 0. The caret being
    // unset, or a scheme without the unit model (segment_raw_boundaries() empty), decodes the
    // whole string and this equals the raw length.
    std::size_t prefix_end() const;
    // raw[prefix_end, size) with its original casing: the pending input this decode did not
    // consume. Empty unless the caret prefix is strictly shorter than the raw string.
    std::string pending_suffix() const;
    SchemeType current_scheme_type() const;

    void reset_state();
    void reset_cache();
    // 神经重排结果回来后用：只失效整句排序，词库和切分缓存保持热，且不在这里重算，
    // 由调用方接着 recompute_candidates()。
    void reset_sentence_cache();

    const std::vector<WordItem> &get_candidates() const;
    bool can_expand_initial_candidates() const;
    bool expand_initial_candidates();
    std::optional<WordItem> find_candidate(const std::string &key, const std::string &value);

    const std::string &get_pinyin_sequence() const;
    const std::string &get_pinyin_sequence_with_cases() const;
    const std::string &get_pure_pinyin_sequence() const;
    const std::string &get_pinyin_segmentation() const;
    std::string get_pinyin_segmentation_with_cases() const;
    // Offsets in get_pinyin_sequence_with_cases() where one input unit starts,
    // always including 0 (when non-empty) and raw.size(). A unit is one syllable:
    // the `ma` of ni'hao'ma. Local modes and
    // the dedicated English scheme have no unit model and return an empty vector, and hosts then fall back
    // to single-character editing. An active autocorrect display is
    // mapped back to the raw spans the engine actually cut, so a deleted unit
    // can never leave half a segment or a stray separator behind. Consumed by
    // segment deletion (Ctrl+Backspace), so any later caret-movement feature
    // must use this same boundary set instead of re-deriving one.
    std::vector<std::size_t> segment_raw_boundaries() const;
    std::string get_quanpin() const;
    bool is_all_complete_pure_pinyin() const;
    // 整串输入能不能读成拼音：全拼按「完整音节或音节前缀」（含简拼声母，bus = bu + s）切完整串，
    // 不要求音节完整，也不看词库有没有词。混输里用它决定英文前缀补全词的默认位置：能读成拼音的
    // 输入，用户多半在打中文，补全词默认退到首页末位。
    bool reads_as_pinyin() const;

    void set_pinyin_sequence(const std::string &pinyin_sequence);
    void set_pinyin_sequence_with_cases(const std::string &pinyin_sequence);

    int store_user_phrase(std::string pinyin, std::string word);
    int store_user_phrase_from_canonical_pinyin(std::string pinyin, std::string word);
    int remove_candidate(std::string pinyin, std::string word, SchemeType scheme);
    int cache_dynamic_candidate(const std::string &pinyin, const std::string &word, CandidateSource source);
    SelectionTransition advance_composition_after_selection(const std::string &selected_pinyin,
                                                            const std::string &selected_word,
                                                            const std::string &selected_canonical_pinyin);
    CloudQueryState get_cloud_query_state() const;
    CreatingWordProgress update_creating_word_progress(const std::string &current_pinyin,
                                                       const std::string &current_word,
                                                       const std::string &selected_word,
                                                       const SelectionTransition &selection_transition) const;

    void set_quanpin_autocorrect_types(unsigned autocorrect_types);
    void set_fuzzy_pinyin_options(metasequoia::FuzzyPinyinOptions options)
    {
        engine_.set_fuzzy_pinyin_options(options);
    }
    // 长词前缀补全（#1343）。下一次查询起生效；词典按前缀缓存的补全与开关无关，不必清。
    void set_prefix_completion_options(metasequoia::PrefixCompletionOptions options)
    {
        engine_.set_prefix_completion_options(options);
    }
    void set_sentence_association(const SentenceAssociationOptions &options)
    {
        engine_.set_sentence_association(options);
    }
    void set_rescoring_context(std::string context)
    {
        engine_.set_rescoring_context(std::move(context));
    }
    void set_chinese_punctuation_enabled(bool enabled)
    {
        chinese_punctuation_enabled_ = enabled;
    }
    void set_candidate_learning_enabled(bool enabled)
    {
        candidate_learning_enabled_ = enabled;
    }

  private:
    const QueryRequest &request() const;
    // 拼音类方案的预编辑切分。容错仲裁给出了分段显示时（QuanpinDictionary::fault_preedit_with_cases_
    // 非空，经 QuanpinEngine::fault_segmentation_with_cases 读出），直接返回它：读音与敲的字母不再
    // 逐音节相同，重建式比对永远过不了，只会退回字面切分。
    std::string build_pinyin_segmentation_with_cases() const;
    void clear_pending_sequence();
    void apply_pending_sequence();

    KeyResult commit(std::size_t index);
    KeyResult handle_local_character(char character);
    KeyResult insert_at_caret(char character);
    KeyResult edit_at_caret(Command command);
    KeyResult replace_editing_text(std::string text, std::size_t caret);
    std::optional<std::size_t> caret_;
    // Last complete unit boundary at or before the caret; only consumes segment_raw_boundaries()
    // (segmentation contract #187). A caret inside the first unit rounds up to that unit's end,
    // so only caret == 0 yields 0. Caret unset or no unit model yields the full raw length.
    std::size_t quantized_prefix_end() const;
    // Decodes the quantized caret prefix into prefix_candidates_ when it is strictly shorter
    // than the raw string, caching by prefix so unchanged keystrokes skip the extra query.
    void refresh_prefix_candidates();
    std::vector<WordItem> prefix_candidates_;
    // Lowercased prefix the cache was built from; also feeds mixed-candidate association so
    // English/emoji suggestions follow the string being converted.
    std::string prefix_query_input_;
    // True while candidates()/mixed assembly must read prefix_candidates_ instead of engine_.
    bool prefix_candidates_active_ = false;
    std::optional<std::string> update_local_candidates();
    void update_mixed_candidates();
    void apply_candidate_positions(std::vector<WordItem> &items);
    std::string position_context(bool english) const;
    bool fixed_positions_enabled_ = false;
    void update_dedicated_english_candidates();
    void reset_composition();
    void discard_abandoned_phrase_progress();
    std::optional<std::string> learn_candidate(std::size_t index);
    // 当前组合可以混入快捷短语时返回它的编码：全拼、非本地模式、未造词、无光标前缀，
    // 且原始输入全是小写字母。
    std::optional<std::string> quick_phrase_code() const;
    // candidates() 返回混排列表（固定位置、英文/表情/颜文字混排任一开启）的条件。
    bool mixed_candidates_active() const;
    // 选中快捷短语时把组前移，越过组选普通候选时把组后移。
    std::optional<std::string> learn_quick_phrase_order(std::size_t index);
    // 词格 / Google 解码器猜出来的整句在词库里没有对应行，选中后落成一条用户词组。
    std::optional<std::string> learn_sentence_candidate(const WordItem &selected);
    std::optional<std::string> adjust_candidate_frequency(std::size_t index, FrequencyAdjustmentOptions options,
                                                          bool force_top);
    // 拼音候选的调频，排位参照 ranked 而不是当前列表。
    std::optional<std::string> adjust_pinyin_candidate_frequency(const WordItem &selected,
                                                                 const std::vector<WordItem> &ranked,
                                                                 FrequencyAdjustmentOptions options, bool force_top);

    CreatingWordProgress immediate_phrase_progress_;
    std::unique_ptr<QuanpinEngine> canonical_phrase_engine_;
    // 容错仲裁的分段显示串在 QuanpinDictionary::fault_preedit_with_cases_（经
    // QuanpinEngine::fault_segmentation_with_cases 读出），会话侧不另存一份。
    std::string pending_pinyin_sequence_;
    std::string pending_pinyin_sequence_with_cases_;
    bool has_pending_pinyin_sequence_ = false;
    bool has_pending_pinyin_sequence_with_cases_ = false;

    RuntimePaths paths_;
    CandidateQueries candidate_queries_;
    ImeSession engine_;
    // 位掩码（quanpin::kAutocorrect* 位），不是 bool：bool 会把邻键位截断丢失。
    unsigned quanpin_autocorrect_types_ = 0;
    bool chinese_punctuation_enabled_ = true;
    bool candidate_learning_enabled_ = true;
    PunctuationPolicy punctuation_;
    FrequencyAdjustmentOptions frequency_adjustment_;
    bool frequency_adjustment_configured_ = false;
    LocalModeOptions local_mode_options_;
    EnglishInputOptions english_input_options_;
    MixedExpressiveOptions mixed_expressive_options_;
    bool dedicated_english_mode_ = false;
    std::string dedicated_english_preedit_;
    std::vector<WordItem> dedicated_english_candidates_;
    std::vector<WordItem> mixed_candidates_;
    LocalInputMode local_input_mode_ = LocalInputMode::None;
    std::string local_preedit_;
    std::vector<WordItem> local_candidates_;
    std::function<local_modes::LocalDateTime()> local_date_time_provider_;
    OnlineRequestGuard online_requests_;
};
} // namespace metasequoia
