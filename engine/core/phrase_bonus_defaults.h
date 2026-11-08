#pragma once

#include <cstdint>

// 词格里「高权重多音节整词」边加成的默认值，词格（quanpin/word_lattice.h）与整句选项
// （core/sentence_association_options.h）共用，免得两处各写一份漂开。含义见
// WordLatticeOptions::phrase_bonus。默认 0（关闭）：按 320 句集回归，打开后至少有一套整句配置
// 首选下降，数字见 engine/tests/data/quanpin_sentence_eval_300.results.md。
inline constexpr double kDefaultPhraseBonus = 0.0;
inline constexpr std::int64_t kDefaultPhraseBonusMinWeight = 1000;
