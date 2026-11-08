#include "ime_session.h"
#include "../schemes/quanpin_scheme.h"
#include "../quanpin/quanpin_utils.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

ImeSession::ImeSession(SchemeType scheme_type, metasequoia::RuntimePaths paths)
    : provider_registry_(std::move(paths)), scheme_(create_scheme(scheme_type))
{
    bind_quanpin_scheme();
}

void ImeSession::configure_quanpin_scheme(IInputScheme *scheme) const
{
    // build_request 在 apply_request_options 之前跑，纠错掩码必须先落到方案上，
    // 否则切分会用方案自己的默认值（关）而不是会话的开关状态。
    if (auto *quanpin_scheme = dynamic_cast<QuanpinScheme *>(scheme))
    {
        quanpin_scheme->set_autocorrect_types(quanpin_autocorrect_types_);
    }
}

void ImeSession::bind_quanpin_scheme()
{
    configure_quanpin_scheme(scheme_.get());
}

void ImeSession::handle_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch)
{
    scheme_->handle_key(vk, modifiers_down, wch);
    // 查询并更新候选词列表
    refresh_candidates();
}

void ImeSession::switch_scheme(SchemeType scheme_type)
{
    scheme_ = create_scheme(scheme_type);
    bind_quanpin_scheme();
    state_ = CompositionState{};
}

void ImeSession::set_quanpin_helpcode_enabled(bool enabled)
{
    enable_quanpin_helpcode_ = enabled;
}

void ImeSession::set_quanpin_autocorrect_types(unsigned autocorrect_types)
{
    quanpin_autocorrect_types_ = autocorrect_types;
    bind_quanpin_scheme();
}

void ImeSession::replace_quanpin_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases)
{
    if (scheme_->type() != SchemeType::Quanpin)
    {
        return;
    }

    auto *quanpin_scheme = dynamic_cast<QuanpinScheme *>(scheme_.get());
    if (!quanpin_scheme)
    {
        return;
    }

    quanpin_scheme->set_raw_input(raw_input, raw_input_with_cases);
    refresh_candidates();
}

void ImeSession::reset()
{
    scheme_->reset();
    state_ = CompositionState{};
}

void ImeSession::reset_cache()
{
    provider_registry_.reset_cache(current_scheme_type());
    // 混输组合同时查过拼音 provider，清缓存必须两边都清，否则选中调频后的重排会读到旧序。
    refresh_candidates();
}

void ImeSession::reset_sentence_cache()
{
    provider_registry_.reset_sentence_cache();
}

int ImeSession::create_word(std::string pinyin, std::string word)
{
    return provider_registry_.create_word(current_scheme_type(), std::move(pinyin), std::move(word));
}

int ImeSession::update_weight_by_pinyin_and_word(SchemeType scheme, std::string pinyin, std::string word)
{
    return provider_registry_.update_weight_by_pinyin_and_word(scheme, std::move(pinyin), std::move(word));
}

int ImeSession::delete_by_pinyin_and_word(SchemeType scheme, std::string pinyin, std::string word)
{
    return provider_registry_.delete_by_pinyin_and_word(scheme, std::move(pinyin), std::move(word));
}

int ImeSession::cache_dynamic_candidate(const std::string &pinyin, const std::string &word, CandidateSource source)
{
    return provider_registry_.cache_dynamic_candidate(current_scheme_type(), pinyin, word, source);
}

std::vector<WordItem> ImeSession::query_raw_candidates(const std::string &raw_input,
                                                       const std::string &raw_input_with_cases)
{
    // A throwaway scheme instance: set_raw_input on the live scheme would clobber its key
    // strokes, and a prefix query must leave the composition exactly as it found it.
    const std::unique_ptr<IInputScheme> query_scheme = create_scheme(scheme_->type());
    configure_quanpin_scheme(query_scheme.get());
    query_scheme->set_raw_input(raw_input, raw_input_with_cases);
    QueryRequest request = query_scheme->build_request();
    apply_request_options(request);
    if (!request.valid)
    {
        return {};
    }
    return provider_registry_.resolve(request.scheme).query(request);
}

void ImeSession::replace_active_raw_input(const std::string &raw_input, const std::string &raw_input_with_cases)
{
    replace_quanpin_raw_input(raw_input, raw_input_with_cases);
}

int ImeSession::cache_dynamic_candidate_for_current_request(const std::string &word, CandidateSource source)
{
    return provider_registry_.cache_dynamic_candidate_for_request(state_.request, word, source);
}

int ImeSession::apply_dynamic_candidate(const std::string &word, CandidateSource source)
{
    const int result = cache_dynamic_candidate_for_current_request(word, source);
    if (result == 0)
    {
        refresh_candidates();
    }
    return result;
}

SchemeType ImeSession::current_scheme_type() const
{
    return scheme_->type();
}

const std::string &ImeSession::get_preedit() const
{
    return state_.preedit;
}

const QueryRequest &ImeSession::get_request() const
{
    return state_.request;
}

const std::vector<WordItem> &ImeSession::get_candidates() const
{
    return state_.candidates;
}

std::optional<WordItem> ImeSession::find_candidate(SchemeType scheme, const std::string &key, const std::string &value)
{
    return provider_registry_.find_candidate(scheme, key, value);
}

bool ImeSession::can_expand_initial_candidates() const
{
    return provider_registry_.can_expand_initial_candidates(state_.request, state_.candidates);
}

bool ImeSession::expand_initial_candidates()
{
    return provider_registry_.expand_initial_candidates(state_.request, state_.candidates);
}

void ImeSession::apply_request_options(QueryRequest &request) const
{
    request.enable_quanpin_helpcode = enable_quanpin_helpcode_;
    request.enable_quanpin_autocorrect_transposition =
        (quanpin_autocorrect_types_ & quanpin::kAutocorrectTransposition) != 0;
    request.enable_quanpin_autocorrect_neighbor = (quanpin_autocorrect_types_ & quanpin::kAutocorrectNeighbor) != 0;
    request.enable_quanpin_autocorrect_deletion = (quanpin_autocorrect_types_ & quanpin::kAutocorrectDeletion) != 0;
    request.enable_quanpin_autocorrect_insertion = (quanpin_autocorrect_types_ & quanpin::kAutocorrectInsertion) != 0;
    request.fuzzy_pinyin = fuzzy_pinyin_;
    request.prefix_completion = prefix_completion_;
    request.sentence_association = sentence_association_;
    request.rescoring_context = rescoring_context_;
}

void ImeSession::refresh_candidates()
{
    state_.preedit = scheme_->get_preedit();
    state_.request = scheme_->build_request();
    apply_request_options(state_.request);

    if (!state_.request.valid)
    {
        state_.candidates.clear();
        return;
    }
    state_.candidates = provider_registry_.resolve(state_.request.scheme).query(state_.request);
}

std::unique_ptr<IInputScheme> ImeSession::create_scheme(SchemeType scheme_type) const
{
    switch (scheme_type)
    {
    case SchemeType::Quanpin:
        return std::make_unique<QuanpinScheme>();
    default:
        throw std::runtime_error("Unknown scheme type.");
    }
}
