#include "ipc/english_gloss_policy.h"
#include "tests/includes/test_framework.h"

TEST_CASE(english_senses_split_and_strip_labels)
{
    using FanyImeIpc::SplitEnglishSenses;
    const auto develop = SplitEnglishSenses("v. develop; n. development");
    REQUIRE_EQ(develop.size(), static_cast<size_t>(2));
    REQUIRE_EQ(develop[0].text, std::string("develop"));
    REQUIRE_EQ(develop[0].part_of_speech, std::string("v."));
    REQUIRE_EQ(develop[1].text, std::string("development"));
    REQUIRE_EQ(develop[1].part_of_speech, std::string("n."));

    // 没有标签的词义原样保留，词性为空。
    const auto plain = SplitEnglishSenses("string; trail");
    REQUIRE_EQ(plain.size(), static_cast<size_t>(2));
    REQUIRE_EQ(plain[0].text, std::string("string"));
    REQUIRE_EQ(plain[0].part_of_speech, std::string{});

    // 词性标签只在上屏文字里去掉，显示用的小标签完整到 "proper n."。
    const auto place = SplitEnglishSenses("proper n. Wanshan District");
    REQUIRE_EQ(place.size(), static_cast<size_t>(1));
    REQUIRE_EQ(place[0].text, std::string("Wanshan District"));
    REQUIRE_EQ(place[0].part_of_speech, std::string("proper n."));

    // 全角分号也拆；空项丢弃。
    const auto fullwidth = SplitEnglishSenses("good； ; well");
    REQUIRE_EQ(fullwidth.size(), static_cast<size_t>(2));
    REQUIRE_EQ(fullwidth[0].text, std::string("good"));
    REQUIRE_EQ(fullwidth[1].text, std::string("well"));
    REQUIRE(SplitEnglishSenses("").empty());
    REQUIRE(SplitEnglishSenses("  ;  ").empty());
}

TEST_CASE(english_senses_strip_sentence_period_and_keep_case)
{
    using FanyImeIpc::SplitEnglishSenses;
    // 句子型释义去掉句末句号。
    const auto sentence = SplitEnglishSenses("See you tomorrow.");
    REQUIRE_EQ(sentence.size(), static_cast<size_t>(1));
    REQUIRE_EQ(sentence[0].text, std::string("See you tomorrow"));
    // 缩写词自带的点不是句末句号。
    const auto abbreviation = SplitEnglishSenses("etc.; U.S.");
    REQUIRE_EQ(abbreviation[0].text, std::string("etc."));
    REQUIRE_EQ(abbreviation[1].text, std::string("U.S."));
    // 专有名词保留大小写。
    const auto proper = SplitEnglishSenses("People's Republic of China");
    REQUIRE_EQ(proper[0].text, std::string("People's Republic of China"));
}

TEST_CASE(english_senses_deduplicate_and_cap)
{
    using FanyImeIpc::SplitEnglishSenses;
    const auto duplicated = SplitEnglishSenses("n. Light; adj. light; v. ignite");
    REQUIRE_EQ(duplicated.size(), static_cast<size_t>(2));
    REQUIRE_EQ(duplicated[0].text, std::string("Light"));
    REQUIRE_EQ(duplicated[0].part_of_speech, std::string("n."));
    REQUIRE_EQ(duplicated[1].text, std::string("ignite"));

    std::string many;
    for (int i = 1; i <= 12; ++i)
    {
        if (!many.empty())
            many += "; ";
        many += "sense" + std::to_string(i);
    }
    const auto capped = SplitEnglishSenses(many);
    REQUIRE_EQ(capped.size(), static_cast<size_t>(9));
    REQUIRE_EQ(capped.back().text, std::string("sense9"));

    const auto limited = SplitEnglishSenses("a; b; c", 2);
    REQUIRE_EQ(limited.size(), static_cast<size_t>(2));
    REQUIRE(SplitEnglishSenses("a; b", 0).empty());
}

TEST_CASE(first_english_sense_is_the_committed_line)
{
    using FanyImeIpc::FirstEnglishSense;
    REQUIRE_EQ(FirstEnglishSense("v. develop; n. development"), std::string("develop"));
    REQUIRE_EQ(FirstEnglishSense("  "), std::string{});
    REQUIRE_EQ(FirstEnglishSense("See you tomorrow."), std::string("See you tomorrow"));
}
