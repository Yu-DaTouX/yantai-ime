// 候选键的归属顺序：英文行（commit_english / english_senses）、译文上屏（Ctrl+Enter / Ctrl+数字）
// 与普通选词都落在 Ctrl+Enter 附近，谁先判谁就截走这颗键。51d7ee1d 之前 IsTranslationCommitKey
// 排在英文行前面，译文开关关闭时 Ctrl+Enter 回了 NavigationIgnored，英文行上屏永远到不了。

#include "ipc/candidate_key_route_policy.h"
#include "tests/includes/test_framework.h"

TEST_CASE(ctrl_enter_prefers_the_english_line_when_translations_are_off)
{
    using FanyImeIpc::CandidateKeyRoute;
    using FanyImeIpc::RouteCandidateKey;

    // 译文开关关闭：commit_english 的默认键位就是 Ctrl+Enter，这颗键必须归英文行。
    REQUIRE(RouteCandidateKey(0x0D, FanyImeIpc::kModifierControl, false, true, false) ==
            CandidateKeyRoute::EnglishLine);
    // 没配 commit_english 键位时没有英文行可上屏；IsTranslationCommitKey 不看开关，这颗键仍进
    // 译文分支回 NavigationIgnored（TSF 已经吃掉它，必须有一条回复），不能落到普通选词。
    REQUIRE(RouteCandidateKey(0x0D, FanyImeIpc::kModifierControl, false, false, false) ==
            CandidateKeyRoute::TranslationCommit);
}

TEST_CASE(ctrl_enter_goes_to_the_translation_when_enabled)
{
    using FanyImeIpc::CandidateKeyRoute;
    using FanyImeIpc::RouteCandidateKey;

    // 译文开关打开：同一个键位改归译文上屏，英文行让位（TSF 侧 IsEnglishCommitShortcut 同样返回 false）。
    REQUIRE(RouteCandidateKey(0x0D, FanyImeIpc::kModifierControl, true, true, false) ==
            CandidateKeyRoute::TranslationCommit);
    // 没配 commit_english 也一样：译文上屏不看那颗键位的配置。
    REQUIRE(RouteCandidateKey(0x0D, FanyImeIpc::kModifierControl, true, false, false) ==
            CandidateKeyRoute::TranslationCommit);
}

TEST_CASE(ctrl_shift_enter_expands_the_english_senses)
{
    using FanyImeIpc::CandidateKeyRoute;
    using FanyImeIpc::RouteCandidateKey;

    const uint32_t ctrl_shift = FanyImeIpc::kModifierControl | FanyImeIpc::kModifierShift;
    // english_senses 默认键位 Ctrl+Shift+Enter：两个开关下都归展开词义。commit_english 是另一颗键，
    // 它匹配不上这里（真实配置里 MatchesConfiguredBinding 连修饰键一起比）。
    REQUIRE(RouteCandidateKey(0x0D, ctrl_shift, false, false, true) == CandidateKeyRoute::EnglishSenses);
    REQUIRE(RouteCandidateKey(0x0D, ctrl_shift, true, false, true) == CandidateKeyRoute::EnglishSenses);
    // IsTranslationCommitKey 不认 Shift，反过来也不会截走它。
    REQUIRE(!FanyImeIpc::IsTranslationCommitKey(0x0D, ctrl_shift));
}

TEST_CASE(ctrl_digit_commits_the_numbered_translation)
{
    using FanyImeIpc::CandidateKeyRoute;
    using FanyImeIpc::RouteCandidateKey;

    REQUIRE(RouteCandidateKey('3', FanyImeIpc::kModifierControl, true, false, false) ==
            CandidateKeyRoute::TranslationDigit);
    // 译文关闭时 TSF 不再吃 Ctrl+数字，正常情况下这颗键到不了这里；真到了仍走译文分支回
    // NavigationIgnored，不能落回普通选词把第 3 个候选上屏（IsTranslationDigitKey 同样不看开关）。
    REQUIRE(RouteCandidateKey('3', FanyImeIpc::kModifierControl, false, false, false) ==
            CandidateKeyRoute::TranslationDigit);
}

TEST_CASE(ordinary_selection_keys_keep_their_branch)
{
    using FanyImeIpc::CandidateKeyRoute;
    using FanyImeIpc::RouteCandidateKey;

    // 普通选词：数字、空格、回车都不带 Ctrl/Shift/Alt，任何开关组合下都不归上面四个分支。
    for (uint32_t keycode : {static_cast<uint32_t>('1'), static_cast<uint32_t>('9'), static_cast<uint32_t>(0x20),
                             static_cast<uint32_t>(0x0D)})
    {
        REQUIRE(RouteCandidateKey(keycode, 0, true, false, false) == CandidateKeyRoute::Ordinary);
        REQUIRE(RouteCandidateKey(keycode, 0, false, false, false) == CandidateKeyRoute::Ordinary);
    }
}
