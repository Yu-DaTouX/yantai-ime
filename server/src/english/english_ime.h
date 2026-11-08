#pragma once

#include "engine/core/word_item.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace EnglishIme
{
// 候选词的中译英释义查询（候选条下面的英文行用）。key 是候选的简体字面。
struct TranslationQuery
{
    std::string key;
};

struct TranslationResult
{
    std::string key;
    std::string gloss;
    // 随包覆盖表标了「-」：这个词不显示英文行（和「没查到」不同）。
    bool hidden = false;
};

struct GlossAnswer
{
    std::string text;
    bool hidden = false;
};
// 在后台线程里决定一个词的释义：user() 读用户 custom_translations.txt，dictionary() 懒读 english.db。
// 没设置时先用户文件再 english.db。
using GlossLookup = std::function<std::string()>;
using GlossResolver =
    std::function<GlossAnswer(const std::string &key, const GlossLookup &user, const GlossLookup &dictionary)>;

using ApplyCallback =
    std::function<void(std::vector<WordItem> candidates, const std::string &input, uint64_t generation)>;
using TranslationCallback = std::function<void(std::vector<TranslationResult> results, uint64_t generation)>;

void Start(const std::string &db_path, ApplyCallback apply_callback, TranslationCallback translation_callback = {});
void Stop();
// While running, every non-empty dedicated-mode input is answered through the
// apply callback, even when the dictionary has no match.
bool IsRunning();
void OnInputChanged(const std::string &input, bool dedicated_mode = false, size_t mixed_min_prefix = 2);
void Clear();
bool IsCurrent(const std::string &input, uint64_t generation, bool dedicated_mode = false);
// Resolves the Chinese->English glosses of the given candidates on the background thread.
void RequestTranslations(std::vector<TranslationQuery> queries);
// 在 Start 之前设置。
void SetGlossResolver(GlossResolver resolver);
// 用最近一次的查询再算一遍（后台补查的释义到了）。
void RefreshTranslations();
void ClearTranslations();
bool IsTranslationCurrent(uint64_t generation);
} // namespace EnglishIme
