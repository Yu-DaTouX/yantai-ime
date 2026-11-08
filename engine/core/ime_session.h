#pragma once

#include "composition_state.h"
#include "input_session_types.h"
#include "scheme_type.h"
#include "sentence_association_options.h"
#include "../providers/provider_registry.h"
#include "../schemes/input_scheme.h"
#include <memory>

class ImeSession
{
  public:
    explicit ImeSession(SchemeType scheme_type = SchemeType::Quanpin,
                        metasequoia::RuntimePaths paths = metasequoia::RuntimePaths::legacy());

    void handle_key(ImeKeyCode vk, ImeModifierMask modifiers_down = 0, ImeCharacter wch = 0);
    void switch_scheme(SchemeType scheme_type);
    void set_quanpin_autocorrect_types(unsigned autocorrect_types);
    void set_fuzzy_pinyin_options(metasequoia::FuzzyPinyinOptions options)
    {
        fuzzy_pinyin_ = options;
    }
    void set_prefix_completion_options(metasequoia::PrefixCompletionOptions options)
    {
        prefix_completion_ = options;
    }
    void set_sentence_association(const SentenceAssociationOptions &options)
    {
        sentence_association_ = options;
    }
    // 本会话最近上屏的文本，随查询下发给神经重排当前文。
    void set_rescoring_context(std::string context)
    {
        rescoring_context_ = std::move(context);
    }
    void replace_quanpin_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases);
    // Writes back to whichever scheme is composing. Committing a pinyin answer out of a longer
    // mixed composition has to shorten the live composition, and the pinyin-shaped caller would
    // otherwise address a scheme that is not the active one and be ignored.
    void replace_active_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases);
    // Runs one standalone candidate query for the given raw input without touching the live
    // composition: the active scheme's raw/key strokes and state_'s request/candidates stay put.
    // Query options (autocorrect, fuzzy) match refresh_candidates exactly. The caret
    // driven prefix decoding in InputSession uses this to decode a prefix while the composition
    // still owns the full raw string.
    std::vector<WordItem> query_raw_candidates(const std::string &raw_input, const std::string &raw_input_with_cases);
    void reset();
    void reset_cache();
    // 只清整句排序所在的缓存层，不重算候选：调用方随后自己 refresh，免得像 reset_cache
    // 那样先冷算一遍、调用方再算一遍。
    void reset_sentence_cache();
    int create_word(std::string pinyin, std::string word);
    int update_weight_by_pinyin_and_word(SchemeType scheme, std::string pinyin, std::string word);
    int delete_by_pinyin_and_word(SchemeType scheme, std::string pinyin, std::string word);
    int cache_dynamic_candidate(const std::string &pinyin, const std::string &word, CandidateSource source);
    int cache_dynamic_candidate_for_current_request(const std::string &word, CandidateSource source);
    int apply_dynamic_candidate(const std::string &word, CandidateSource source);
    std::optional<WordItem> find_candidate(SchemeType scheme, const std::string &key, const std::string &value);

    SchemeType current_scheme_type() const;
    const std::string &get_preedit() const;
    const QueryRequest &get_request() const;
    const std::vector<WordItem> &get_candidates() const;
    // 只判断还能不能展开首字母候选，不查库也不改候选；候选窗据此决定满页末页的下一页箭头。
    bool can_expand_initial_candidates() const;
    bool expand_initial_candidates();
    // 容错仲裁选中读音后的分段显示（大小写原样）；空表示当前串没有容错布局。
    const std::string &fault_segmentation_with_cases() const
    {
        return provider_registry_.fault_segmentation_with_cases();
    }

  private:
    // Shared option injection for refresh_candidates() and query_raw_candidates(); the two must
    // not drift or a prefix query would answer with different candidates than the live pipeline.
    void apply_request_options(QueryRequest &request) const;
    void refresh_candidates();
    // 把会话的纠错掩码下发给全拼方案（非全拼方案什么也不做）。要在 build_request 之前调，
    // 掩码决定切分要不要跑手误别名层。
    void configure_quanpin_scheme(IInputScheme *scheme) const;
    // 换方案或改纠错开关时把掩码绑到当前方案上。
    void bind_quanpin_scheme();
    std::unique_ptr<IInputScheme> create_scheme(SchemeType scheme_type) const;

  private:
    ProviderRegistry provider_registry_;
    std::unique_ptr<IInputScheme> scheme_;
    CompositionState state_;
    unsigned quanpin_autocorrect_types_ = 0;
    metasequoia::FuzzyPinyinOptions fuzzy_pinyin_;
    metasequoia::PrefixCompletionOptions prefix_completion_;
    SentenceAssociationOptions sentence_association_;
    std::string rescoring_context_;
    // Resolved when the scheme changes rather than on every keystroke.
};
