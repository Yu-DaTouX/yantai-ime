#include "pinyin_candidate_provider.h"

PinyinCandidateProvider::PinyinCandidateProvider(metasequoia::RuntimePaths paths) : quanpin_engine_(paths)
{
}

std::vector<WordItem> PinyinCandidateProvider::query(const QueryRequest &request)
{
    if (!request.valid)
    {
        return {};
    }

    std::vector<WordItem> candidates = quanpin_engine_.query(request);

    // 候选带上产出它的方案：删除与调频按候选自己的方案路由。
    for (WordItem &item : candidates)
    {
        item.scheme = request.scheme;
    }
    return candidates;
}

bool PinyinCandidateProvider::can_expand_initial_candidates(const QueryRequest &request,
                                                            const std::vector<WordItem> &candidates) const
{
    return quanpin_engine_.can_expand_initial_candidates(request, candidates);
}

bool PinyinCandidateProvider::expand_initial_candidates(const QueryRequest &request, std::vector<WordItem> &candidates)
{
    const bool expanded = quanpin_engine_.expand_initial_candidates(request, candidates);
    // 展开出来的候选同样要带上方案，选中/删除它们时才能回到同一个引擎。
    if (expanded)
    {
        for (WordItem &item : candidates)
        {
            item.scheme = request.scheme;
        }
    }
    return expanded;
}

void PinyinCandidateProvider::reset_cache()
{
    quanpin_engine_.reset_cache();
}

void PinyinCandidateProvider::reset_sentence_cache()
{
    quanpin_engine_.reset_sentence_cache();
}

int PinyinCandidateProvider::create_word(SchemeType /*scheme*/, std::string pinyin, std::string word)
{
    return quanpin_engine_.create_word(std::move(pinyin), std::move(word));
}

int PinyinCandidateProvider::update_weight_by_pinyin_and_word(SchemeType /*scheme*/, std::string pinyin,
                                                              std::string word)
{
    return quanpin_engine_.update_weight_by_pinyin_and_word(std::move(pinyin), std::move(word));
}

int PinyinCandidateProvider::delete_by_pinyin_and_word(SchemeType /*scheme*/, std::string pinyin, std::string word)
{
    return quanpin_engine_.delete_by_pinyin_and_word(std::move(pinyin), std::move(word));
}

int PinyinCandidateProvider::cache_dynamic_candidate(SchemeType /*scheme*/, const std::string &pinyin,
                                                     const std::string &word, CandidateSource source)
{
    return quanpin_engine_.insert_word_to_series_cache(pinyin, word, source);
}

std::optional<WordItem> PinyinCandidateProvider::find_candidate(SchemeType /*scheme*/, const std::string &key,
                                                                const std::string &value)
{
    std::optional<WordItem> found = quanpin_engine_.find_candidate(key, value);
    if (found)
    {
        found->scheme = SchemeType::Quanpin;
    }
    return found;
}

int PinyinCandidateProvider::cache_dynamic_candidate_for_request(const QueryRequest &request, const std::string &word,
                                                                 CandidateSource source)
{
    return quanpin_engine_.insert_word_to_series_cache(request, word, source);
}
