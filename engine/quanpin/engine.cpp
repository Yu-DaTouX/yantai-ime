#include "engine.h"
#include "quanpin_query.h"
#include "quanpin_utils.h"

namespace
{ // The request carries one bool per user-visible correction switch; the dictionary layer
// gates the whole feature with a single mask. The mapping itself lives in
// quanpin::autocorrect_types_from_switches (quanpin_utils.h) so this, the preedit
// rebuild (input_session_composition.cpp) and the server session adapter cannot drift.
unsigned autocorrect_types_from_request(const QueryRequest &request)
{
    return quanpin::autocorrect_types_from_switches(
        request.enable_quanpin_autocorrect_transposition, request.enable_quanpin_autocorrect_neighbor,
        request.enable_quanpin_autocorrect_deletion, request.enable_quanpin_autocorrect_insertion);
}
} // namespace

QuanpinEngine::QuanpinEngine(metasequoia::RuntimePaths paths) : dictionary_({}, paths)
{
}

QuanpinEngine::~QuanpinEngine() = default;

std::optional<WordItem> QuanpinEngine::find_candidate(const std::string &key, const std::string &value)
{
    return dictionary_.find_candidate(key, value);
}

std::vector<WordItem> QuanpinEngine::query(const QueryRequest &request)
{
    if (!request.valid)
    {
        return {};
    }

    // 整句联想开关随请求下发到词典层，四条整句来源据此各自门控（见 query_series）。
    dictionary_.set_sentence_association(request.sentence_association);
    // 前文同理随请求下发；换了前文，按旧前文重排出来的缓存顺序作废。
    dictionary_.set_rescoring_context(request.rescoring_context);

    dictionary_.set_prefix_completion(request.prefix_completion);
    return dictionary_.query(request.raw_input, request.segmentation, autocorrect_types_from_request(request),
                             request.fuzzy_pinyin);
}

namespace
{
// 能展开首字母候选时返回那个首字母，否则返回空串。
std::string expandable_initial_code(const QueryRequest &request)
{
    if (request.scheme != SchemeType::Quanpin)
    {
        return {};
    }

    const auto segments = quanpin::split_segments(request.segmentation);
    if (segments.empty() || segments.front().size() != 1)
    {
        return {};
    }
    return segments.front();
}
} // namespace

bool QuanpinEngine::can_expand_initial_candidates(const QueryRequest &request,
                                                  const std::vector<WordItem> &candidates) const
{
    const std::string code = expandable_initial_code(request);
    return !code.empty() && QuanpinDictionary::can_expand_initial_candidates(code, candidates);
}

bool QuanpinEngine::expand_initial_candidates(const QueryRequest &request, std::vector<WordItem> &candidates)
{
    const std::string code = expandable_initial_code(request);
    if (code.empty())
    {
        return false;
    }
    return dictionary_.expand_initial_candidates(code, candidates);
}

int QuanpinEngine::handleVkCode(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch)
{
    return dictionary_.handleVkCode(vk, modifiers_down, wch);
}

int QuanpinEngine::create_word(std::string pinyin, std::string word)
{
    return dictionary_.create_word(std::move(pinyin), std::move(word));
}

int QuanpinEngine::create_word_from_canonical_pinyin(std::string pinyin, std::string word)
{
    return dictionary_.create_word_from_canonical_pinyin(std::move(pinyin), std::move(word));
}

int QuanpinEngine::update_weight_by_word(std::string word)
{
    return dictionary_.update_weight_by_word(std::move(word));
}

int QuanpinEngine::update_weight_by_pinyin_and_word(std::string pinyin, std::string word)
{
    return dictionary_.update_weight_by_pinyin_and_word(std::move(pinyin), std::move(word));
}

int QuanpinEngine::delete_by_pinyin_and_word(std::string pinyin, std::string word)
{
    return dictionary_.delete_by_pinyin_and_word(std::move(pinyin), std::move(word));
}

int QuanpinEngine::insert_word_to_series_cache(const std::string &pinyin, const std::string &word,
                                               CandidateSource source)
{
    return dictionary_.insert_word_to_series_cache(pinyin, word, source);
}

int QuanpinEngine::insert_word_to_series_cache(const QueryRequest &request, const std::string &word,
                                               CandidateSource source)
{
    return dictionary_.insert_word_to_series_cache(request.raw_input, request.segmentation,
                                                   autocorrect_types_from_request(request), word, source);
}

std::string QuanpinEngine::search_sentence_from_ime_engine(const std::string &user_pinyin)
{
    return dictionary_.search_sentence_from_ime_engine(user_pinyin);
}

void QuanpinEngine::reset_state()
{
    dictionary_.reset_state();
}

void QuanpinEngine::reset_cache()
{
    dictionary_.reset_cache();
}

void QuanpinEngine::reset_sentence_cache()
{
    dictionary_.reset_sentence_cache();
}
