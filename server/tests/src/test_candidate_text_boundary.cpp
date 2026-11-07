#include "ipc/candidate_text_boundary.h"
#include "tests/includes/test_framework.h"

#include <string>
#include <vector>

TEST_CASE(candidate_text_boundary_counts_valid_utf8_without_recovery)
{
    int recoveries = 0;
    const auto recover = [&] { ++recoveries; };
    REQUIRE_EQ(*FanyImeIpc::CandidateTextLength("", recover), size_t{0});
    REQUIRE_EQ(*FanyImeIpc::CandidateTextLength("a", recover), size_t{1});
    REQUIRE_EQ(*FanyImeIpc::CandidateTextLength("你好", recover), size_t{2});
    REQUIRE_EQ(*FanyImeIpc::CandidateTextLength("\xF0\x9F\x98\x80", recover), size_t{1});
    REQUIRE_EQ(recoveries, 0);
}

TEST_CASE(candidate_text_boundary_discards_bad_page_and_accepts_next_candidate)
{
    // Bad lead byte, truncated sequence, overlong encoding and surrogate.
    for (const std::string &bad : {std::string("\xFF"), std::string("\xE4\xBD"), std::string("\xC0\xAF"),
                                   std::string("\xED\xA0\x80")})
    {
        std::vector<std::string> page{bad};
        std::vector<std::string> cache{bad};
        const std::string composition = "nihao";
        int recoveries = 0;
        const auto length = FanyImeIpc::CandidateTextLength(bad, [&] {
            ++recoveries;
            page.clear();
            cache.clear();
        });
        REQUIRE(!length.has_value());
        REQUIRE(page.empty());
        REQUIRE(cache.empty());
        REQUIRE_EQ(recoveries, 1);
        REQUIRE_EQ(composition, std::string("nihao"));
        REQUIRE_EQ(*FanyImeIpc::CandidateTextLength("你好", [&] { ++recoveries; }), size_t{2});
        REQUIRE_EQ(recoveries, 1);
    }
}
