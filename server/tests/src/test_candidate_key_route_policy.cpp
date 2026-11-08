// 候选键的归属顺序：英文行（commit_english）与展开词义（english_senses）。
// TSF 侧同一判断在 windows/src/Key/KeyRoutingPolicy.h。

#include "ipc/candidate_key_route_policy.h"
#include "tests/includes/test_framework.h"

TEST_CASE(commit_english_binding_routes_to_the_english_line)
{
    using FanyImeIpc::CandidateKeyRoute;
    using FanyImeIpc::RouteCandidateKey;

    REQUIRE(RouteCandidateKey(true, false) == CandidateKeyRoute::EnglishLine);
    REQUIRE(RouteCandidateKey(true, true) == CandidateKeyRoute::EnglishLine);
}

TEST_CASE(english_senses_binding_routes_to_the_senses_page)
{
    using FanyImeIpc::CandidateKeyRoute;
    using FanyImeIpc::RouteCandidateKey;

    REQUIRE(RouteCandidateKey(false, true) == CandidateKeyRoute::EnglishSenses);
}

TEST_CASE(unbound_keys_stay_on_the_ordinary_branch)
{
    using FanyImeIpc::CandidateKeyRoute;
    using FanyImeIpc::RouteCandidateKey;

    // 候选词翻译已删除：Ctrl+Enter / Ctrl+数字没有配置的英文键位时不再有任何专属分支，照常交给宿主或普通选词。
    REQUIRE(RouteCandidateKey(false, false) == CandidateKeyRoute::Ordinary);
}
