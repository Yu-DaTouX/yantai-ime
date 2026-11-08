#include "input_session.h"
#include "../common/helpcode_utils.h"
#include "../quanpin/quanpin_query.h"
#include "../quanpin/quanpin_utils.h"
#include <algorithm>
#include <cctype>

namespace metasequoia
{
namespace
{
std::string remove_delimiters(const std::string &segmented)
{
    std::string normalized;
    normalized.reserve(segmented.size());
    for (const char ch : segmented)
    {
        if (ch != '\'')
        {
            normalized.push_back(ch);
        }
    }
    return normalized;
}

std::size_t raw_length_for_effective_prefix(const std::string &raw_input, std::size_t effective_length)
{
    std::size_t raw_length = 0;
    std::size_t effective_count = 0;
    while (raw_length < raw_input.size() && effective_count < effective_length)
    {
        if (raw_input[raw_length] != '\'')
        {
            ++effective_count;
        }
        ++raw_length;
    }
    return raw_length;
}

void remove_consumed_leading_separators(std::string &raw_input, std::string &raw_input_with_cases)
{
    size_t count = 0;
    while (count < raw_input.size() && raw_input[count] == '\'')
    {
        ++count;
    }
    raw_input.erase(0, count);

    count = 0;
    while (count < raw_input_with_cases.size() && raw_input_with_cases[count] == '\'')
    {
        ++count;
    }
    raw_input_with_cases.erase(0, count);
}

// 整句落库的长度上限。词库里的短语本身就不超过这个音节数（对标
// quanpin::WordLatticeOptions::max_phrase_syllables），再长的整句只是这一次输入
// 的产物，落库除了撑大用户词库没有别的作用。
constexpr size_t kMaxLearnedSentenceSyllables = 7;

std::string normalize_canonical_pinyin_for_word(const std::string &pinyin, const std::string &word)
{
    if (pinyin.empty())
    {
        return {};
    }

    const auto segments = quanpin::split_segments(pinyin);
    if (segments.empty() || segments.size() != HelpcodeUtils::count_han_chars(word))
    {
        return {};
    }
    for (const auto &segment : segments)
    {
        if (segment.empty() || !quanpin::is_complete_pinyin_input(segment))
        {
            return {};
        }
    }
    return quanpin::join_segments(segments);
}

std::string append_canonical_pinyin(const std::string &prefix, const std::string &suffix)
{
    if (prefix.empty())
    {
        return suffix;
    }
    if (suffix.empty())
    {
        return {};
    }
    return prefix + "'" + suffix;
}

bool HasActiveQuanpinHelpcode(const QueryRequest &request)
{
    return request.enable_quanpin_helpcode &&
           quanpin::detect_active_helpcode_length(request.raw_input, request.raw_input_with_cases) > 0;
}

std::string ResolveQuanpinCloudCacheKey(const QueryRequest &request)
{
    return quanpin::strip_active_helpcodes(request.raw_input, request.raw_input_with_cases);
}

// Adapter over the request fields: the switch -> mask rule itself lives in
// quanpin::autocorrect_types_from_switches (quanpin_utils.h), shared with
// engine.cpp and the server session so the preedit rebuild always sees the same
// correction space as the query.
unsigned QuanpinAutocorrectTypes(const QueryRequest &request)
{
    return quanpin::autocorrect_types_from_switches(
        request.enable_quanpin_autocorrect_transposition, request.enable_quanpin_autocorrect_neighbor,
        request.enable_quanpin_autocorrect_deletion, request.enable_quanpin_autocorrect_insertion);
}

std::string QuanpinLettersWithoutDelimiters(const std::string &text)
{
    std::string letters;
    letters.reserve(text.size());
    for (const char ch : text)
    {
        if (ch != '\'')
        {
            letters.push_back(ch);
        }
    }
    return letters;
}

// Folds letters for autocorrect display comparisons: lowercases and strips
// manual delimiters, and maps the u-umlaut style 'v' spelling onto 'u'. This
// equivalence is what classifies length-preserving spelling aliases (jv->ju,
// nue->nve) as "explainable by the cut" so the preedit can be rebuilt with
// separators; removing it would drop separators for alias-typed input. It only
// ever compares two internally derived strings, never gates a user-facing
// mark — the dictionary-side corrected_from contract lives in
// quanpin_dictionary.cpp and treats v/u as distinct letters on purpose.
std::string FoldQuanpinAutocorrectLetters(const std::string &text)
{
    std::string folded;
    folded.reserve(text.size());
    for (const char ch : text)
    {
        if (ch == '\'')
        {
            continue;
        }
        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        folded.push_back(lower == 'v' ? 'u' : lower);
    }
    return folded;
}

// Folded letters of a cut's syllable sequence, for comparing the BFS reading
// against the scheme segmentation reading.
std::string CutSyllableLetters(const quanpin::AutocorrectCut &cut)
{
    std::string letters;
    for (const auto &segment : cut.segments)
    {
        letters += segment.syllable;
    }
    return FoldQuanpinAutocorrectLetters(letters);
}

// Rebuilds the preedit from the cased input letters with a separator after
// each listed letter count (ascending; the final segment end is excluded).
// Letters (case included) are preserved verbatim; manual delimiters are
// replaced by the given boundaries.
std::string RebuildQuanpinDisplayAtBoundaries(const std::string &cased_input, const std::vector<size_t> &boundaries)
{
    std::string display;
    display.reserve(cased_input.size() + boundaries.size());
    size_t letter_index = 0;
    size_t boundary_index = 0;
    for (const char ch : cased_input)
    {
        if (ch == '\'')
        {
            continue;
        }
        display.push_back(ch);
        ++letter_index;
        if (boundary_index < boundaries.size() && letter_index == boundaries[boundary_index])
        {
            display.push_back('\'');
            ++boundary_index;
        }
    }
    return display;
}

// Separators at the raw spans the correction BFS reports.
std::string RebuildQuanpinDisplayFromCut(const std::string &cased_input, const quanpin::AutocorrectCut &cut)
{
    std::vector<size_t> boundaries;
    for (size_t i = 0; i + 1 < cut.segments.size(); ++i)
    {
        boundaries.push_back(cut.segments[i].start + cut.segments[i].raw_text.size());
    }
    return RebuildQuanpinDisplayAtBoundaries(cased_input, boundaries);
}

// Separators for a spelling that passed the jianpin guard, read the way the
// guard reads it: complete syllables plus at most one trailing letter
// ("dongan" -> dong'an, "haoyongg" -> hao'yong'g). Only the typed letters are
// cut, so the result is independent of any alias reading that won the query.
// Returns an empty string when no such reading exists.
std::string RebuildQuanpinDisplayFromLegalSpelling(const std::string &cased_input, const std::string &raw_input)
{
    auto segments = quanpin::cut_one_piece_min_segments(raw_input, true);
    if (segments.empty() && raw_input.size() > 1)
    {
        segments = quanpin::cut_one_piece_min_segments(raw_input.substr(0, raw_input.size() - 1), true);
        if (!segments.empty())
        {
            segments.push_back(raw_input.substr(raw_input.size() - 1));
        }
    }
    if (segments.empty())
    {
        return {};
    }

    std::vector<size_t> boundaries;
    size_t end = 0;
    for (size_t i = 0; i + 1 < segments.size(); ++i)
    {
        end += segments[i].size();
        boundaries.push_back(end);
    }
    return RebuildQuanpinDisplayAtBoundaries(cased_input, boundaries);
}

// The preedit must always show the letters the user actually typed (PRD R5).
// Two layers can rewrite them into canonical pinyin: the scheme alias table
// (sahng -> shang, baked into raw_segmentation) and the dictionary correction
// BFS (shabg -> shang, which only re-separates). Both are rebuilt here from
// the raw letters with separators at the actual cut positions; when the BFS
// cannot explain a rewrite (length-changing aliases such as mihng -> ming) the
// input is shown as typed, with no scheme separators. With the correction
// switches off the scheme alias table no longer rewrites letters, so `base` is
// already the literal cut and this rebuild is a no-op fast path; the rebuild
// matters while correction is on, and AC1 only constrains the candidate list.
std::string BuildQuanpinAutocorrectDisplay(const QueryRequest &request)
{
    const std::string &cased = request.raw_input_with_cases.empty() ? request.raw_input : request.raw_input_with_cases;
    const std::string base = request.raw_segmentation.empty() ? cased : request.raw_segmentation;
    if (request.raw_input.empty() || cased.empty())
    {
        return base;
    }

    const unsigned types = QuanpinAutocorrectTypes(request);
    const std::string folded_input = FoldQuanpinAutocorrectLetters(cased);
    const bool letters_rewritten = FoldQuanpinAutocorrectLetters(QuanpinLettersWithoutDelimiters(base)) != folded_input;

    // Fast path: the scheme kept the typed letters and either no correction
    // type is enabled or the input is already a complete pinyin spelling, so
    // no correction interpretation exists to draw separators from.
    if (!letters_rewritten && (types == 0 || quanpin::is_complete_pinyin_input(request.raw_input)))
    {
        return base;
    }

    // Jianpin guard, mirroring resolve_series_query in the dictionary layer: a
    // leading initial plus complete syllables, or legal syllables plus at most
    // one trailing initial, expresses user intent rather than a typo
    // Corrections like "ywen" -> wen and "zheg" -> zheng would otherwise erase
    // the original syllable boundaries from the preedit
    if (quanpin::looks_like_jianpin_spelling(request.raw_input))
    {
        // The typed spelling is legal, but the scheme alias table may still
        // have re-segmented it into a reading with copied letters (dongan ->
        // dong'gan, haoyonga -> hao'yong'ga). The dictionary ranks those
        // readings against the exact one by frequency, yet the preedit must
        // show the typed letters (PRD R5), so a rewritten base may never
        // reach it verbatim. Separate the typed letters by their own legal
        // reading instead, so one more letter does not drop every separator
        // (hao'yong -> hao'yong'a, not haoyonga).
        if (!letters_rewritten)
        {
            return base;
        }
        const std::string display = RebuildQuanpinDisplayFromLegalSpelling(cased, request.raw_input);
        return display.empty() ? cased : display;
    }

    const auto cut = quanpin::autocorrect_cut_detail(folded_input, types);
    // When the scheme rewrote the letters, the query resolved through the alias
    // layer, so the preedit may only draw separators from the BFS when both
    // layers explain the letters identically ("sahnghao" -> shang'hao).
    // Otherwise the deletion bit would re-separate "sahng" as sa'hng while the
    // candidates actually come from the alias reading shang -- the old code
    // never noticed because the cut happened to be empty without it.
    if (!cut.empty() && (!letters_rewritten || CutSyllableLetters(cut) == FoldQuanpinAutocorrectLetters(
                                                                              QuanpinLettersWithoutDelimiters(base))))
    {
        return RebuildQuanpinDisplayFromCut(cased, cut);
    }
    // The BFS cannot explain the input (e.g. a length-changing alias such as
    // mihng -> ming): fall back to the input exactly as typed when the letters
    // were rewritten, otherwise keep the scheme segmentation untouched. Manual
    // delimiters are part of what was typed, so they stay ("mihng'").
    return letters_rewritten ? cased : base;
}

// 首选是同一串字母的另一种字面切分时（yibananzhao 由 yi'ban'an'zhao 的「一般按照」领衔，
// fangan 由 fang'an 的「方案」领衔），预编辑跟着首选分，而不是停在方案层的最少音节切分上，
// 否则用户看着 yi'ba'nan'zhao 选「一般按照」。只认读音字母与敲的字母逐个相同、盖住整串、
// 不带纠错标记的首选（辅助码、手动分隔符都不满足），而且音节数与方案层切分相同：那正是
// 只差一处 n/ng 边界的等长歧义；音节数不同的读法（nue 的字面整词「怒鹅」nu'e）不在此列，
// 预编辑照旧。否则返回空串。
std::string BuildQuanpinDisplayFromLiteralHead(const QueryRequest &request, const WordItem &head)
{
    const std::string &cased = request.raw_input_with_cases.empty() ? request.raw_input : request.raw_input_with_cases;
    if (!head.corrected_from.empty() || request.raw_input.find('\'') != std::string::npos ||
        cased.size() != request.raw_input.size())
    {
        return {};
    }
    const auto syllables = quanpin::split_segments(head.pinyin);
    const std::string &scheme_segmentation =
        request.normalized_segmentation.empty() ? request.segmentation : request.normalized_segmentation;
    if (syllables.size() < 2 || syllables.size() != quanpin::split_segments(scheme_segmentation).size())
    {
        return {};
    }
    std::vector<size_t> boundaries;
    size_t end = 0;
    for (const auto &syllable : syllables)
    {
        if (request.raw_input.compare(end, syllable.size(), syllable) != 0)
        {
            return {};
        }
        end += syllable.size();
        boundaries.push_back(end);
    }
    if (end != request.raw_input.size())
    {
        return {};
    }
    boundaries.pop_back();
    return RebuildQuanpinDisplayAtBoundaries(cased, boundaries);
}

// 合法输入上的纠错领衔时（ziazheliya 出「在这里呀」、jiuzheeyang 出「就这样」），预编辑
// 按领衔读法分隔成 zia'zhe'li'ya、jiu'zhee'yang，而不是字面切分 zi'a'zhe'li'ya——后者
// 和首选对不上。边界取纠错切分记录的原始区间（多字、漏字纠错和原始字母不等长）；
// 罕见音节替换（lia -> lai）不在 k-best 里，它是换位、字母数不变，按读法音节长度分。
// 切不成合法音节的输入同理：上下文把贵档读法提到首位时（shiideya 出「是的呀」），
// 预编辑要跟着它分成 shii'de'ya，而不是 k-best 首条的 shi'ide'ya。
// 只在首选带纠错标记、读法恰好盖住整串时生效（辅助码、手动分隔符都不满足），否则
// 返回空串。
std::string BuildQuanpinDisplayFromLeadingCorrection(const QueryRequest &request, const WordItem &head)
{
    const std::string &cased = request.raw_input_with_cases.empty() ? request.raw_input : request.raw_input_with_cases;
    if (head.corrected_from.empty() || request.raw_input.find('\'') != std::string::npos ||
        cased.size() != request.raw_input.size())
    {
        return {};
    }
    const auto syllables = quanpin::split_segments(head.pinyin);
    if (syllables.empty())
    {
        return {};
    }
    std::vector<size_t> boundaries;
    size_t end = 0;
    const auto cut =
        quanpin::correction_cut_for_reading(request.raw_input, syllables, QuanpinAutocorrectTypes(request));
    if (cut.has_value() && cut->segments.size() == syllables.size())
    {
        for (const auto &segment : cut->segments)
        {
            end = segment.start + segment.raw_text.size();
            boundaries.push_back(end);
        }
    }
    else
    {
        for (const auto &syllable : syllables)
        {
            end += syllable.size();
            boundaries.push_back(end);
        }
    }
    if (end != request.raw_input.size())
    {
        return {};
    }
    boundaries.pop_back();
    return RebuildQuanpinDisplayAtBoundaries(cased, boundaries);
}
} // namespace

void InputSession::handle_engine_key(ImeKeyCode vk, ImeModifierMask modifiers_down, ImeCharacter wch)
{
    engine_.handle_key(vk, modifiers_down, wch);
    online_requests_.invalidate();
    update_mixed_candidates();
}

void InputSession::recompute_candidates()
{
    if (has_pending_pinyin_sequence_ || has_pending_pinyin_sequence_with_cases_)
    {
        apply_pending_sequence();
        return;
    }
    engine_.handle_key(0, 0, 0);
    update_mixed_candidates();
}

SchemeType InputSession::current_scheme_type() const
{
    return engine_.current_scheme_type();
}

void InputSession::reset_state()
{
    clear_pending_sequence();
    reset_composition();
}

void InputSession::reset_cache()
{
    engine_.reset_cache();
    if (canonical_phrase_engine_)
        canonical_phrase_engine_->reset_cache();
    // 前缀候选是按文本缓存的，不跟着引擎缓存失效：只清缓存键，让下一次
    // refresh_prefix_candidates 按新权重/选项重查；保留当前列表，避免 caret
    // 激活期间出现空候选窗。
    prefix_query_input_.clear();
}

void InputSession::reset_sentence_cache()
{
    engine_.reset_sentence_cache();
    if (canonical_phrase_engine_)
        canonical_phrase_engine_->reset_sentence_cache();
    // 前缀候选同样可能带整句，理由同 reset_cache。
    prefix_query_input_.clear();
}

const std::vector<WordItem> &InputSession::get_candidates() const
{
    return candidates();
}

bool InputSession::can_expand_initial_candidates() const
{
    return engine_.can_expand_initial_candidates();
}

bool InputSession::expand_initial_candidates()
{
    return engine_.expand_initial_candidates();
}

std::optional<WordItem> InputSession::find_candidate(const std::string &key, const std::string &value)
{
    return engine_.find_candidate(scheme(), key, value);
}

const QueryRequest &InputSession::request() const
{
    return engine_.get_request();
}

const std::string &InputSession::get_pinyin_sequence() const
{
    return request().raw_input;
}

const std::string &InputSession::get_pinyin_sequence_with_cases() const
{
    return request().raw_input_with_cases.empty() ? request().raw_input : request().raw_input_with_cases;
}

const std::string &InputSession::get_pure_pinyin_sequence() const
{
    return request().normalized_input;
}

const std::string &InputSession::get_pinyin_segmentation() const
{
    return request().normalized_segmentation.empty() ? request().segmentation : request().normalized_segmentation;
}

std::string InputSession::get_pinyin_segmentation_with_cases() const
{
    return build_pinyin_segmentation_with_cases();
}

std::string InputSession::build_pinyin_segmentation_with_cases() const
{
    const std::string &typed = get_pinyin_sequence_with_cases();
    // 容错仲裁选中的读音（句中简拼展开、槽内漏字变体）与敲的字母不逐音节相同，
    // 下面几条按首选重建显示的路径都会比对失败、退回字面切分；那时预编辑会和
    // 首选候选对不上（layout-and-unit-boundaries：切分即编辑单元）。仲裁已经把
    // 分段显示按原串大小写算好，直接用。
    const std::string &fault_display = engine_.fault_segmentation_with_cases();
    if (!fault_display.empty())
    {
        return fault_display;
    }
    if (!candidates().empty())
    {
        const auto &head = candidates().front();
        if (quanpin::literal_mixed_pinyin_match(request().raw_input, head.pinyin, head.canonical_pinyin))
        {
            std::vector<size_t> boundaries;
            size_t consumed = 0;
            for (const auto &segment : quanpin::split_segments(head.pinyin))
            {
                consumed += segment.size();
                boundaries.push_back(consumed);
            }
            if (consumed == request().raw_input.size())
                boundaries.pop_back();
            return RebuildQuanpinDisplayAtBoundaries(typed, boundaries);
        }
    }
    if (!candidates().empty())
    {
        std::string display = BuildQuanpinDisplayFromLiteralHead(request(), candidates().front());
        if (display.empty())
        {
            display = BuildQuanpinDisplayFromLeadingCorrection(request(), candidates().front());
        }
        if (!display.empty())
        {
            return display;
        }
    }
    return BuildQuanpinAutocorrectDisplay(request());
}

std::string InputSession::get_quanpin() const
{
    return request().normalized_input;
}

bool InputSession::is_all_complete_pure_pinyin() const
{
    const auto &segmentation =
        request().normalized_segmentation.empty() ? request().segmentation : request().normalized_segmentation;
    return !segmentation.empty() && quanpin::is_complete_pinyin_input(segmentation);
}

bool InputSession::reads_as_pinyin() const
{
    if (has_active_helpcode())
        return true;
    std::string raw = remove_delimiters(request().raw_input);
    std::transform(raw.begin(), raw.end(), raw.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (raw.empty())
        return false;
    return !quanpin::cut_one_piece_min_segments(raw, false).empty();
}

bool InputSession::has_active_helpcode() const
{
    return HasActiveQuanpinHelpcode(request());
}

void InputSession::set_pinyin_sequence(const std::string &pinyin_sequence)
{
    pending_pinyin_sequence_ = pinyin_sequence;
    has_pending_pinyin_sequence_ = true;
}

void InputSession::set_pinyin_sequence_with_cases(const std::string &pinyin_sequence)
{
    pending_pinyin_sequence_with_cases_ = pinyin_sequence;
    has_pending_pinyin_sequence_with_cases_ = true;
}

int InputSession::store_user_phrase(std::string pinyin, std::string word)
{
    return engine_.create_word(std::move(pinyin), std::move(word));
}

int InputSession::store_user_phrase_from_canonical_pinyin(std::string pinyin, std::string word)
{
    if (!canonical_phrase_engine_)
        canonical_phrase_engine_ = std::make_unique<QuanpinEngine>(paths_);
    return canonical_phrase_engine_->create_word_from_canonical_pinyin(std::move(pinyin), std::move(word));
}

// 整句候选（词格 CandidateSource::Generated、Google 解码器 CandidateSource::Fallback、
// 神经整句 NeuralDesktop / NeuralKeyboard）
// 是猜出来的，词库里没有它那一行，所以调频对它无效：update_weight_by_pinyin_and_word
// 改的是 SQLite 里已存在的行，找不到行就什么也不做，用户于是发现自己选多少次都提不上来。
// 选中即落库才是对的处理：存成用户词组之后，下次同样的输入它以 UserDatabase 候选出现，
// 按 quanpin::generated_sentence_insert_position 的约定排在两条整句之前，之后再选还能
// 走正常调频。
std::optional<std::string> InputSession::learn_sentence_candidate(const WordItem &selected)
{
    // 本地模式（U/K/E/M/J/Y、日期时间）和英文模式也用 Generated 装自己的候选，
    // 那些不是拼音整句，不能往拼音用户词库里塞。
    if (local_input_mode_ != LocalInputMode::None || dedicated_english_mode_)
    {
        return std::nullopt;
    }

    // 读音必须完整且音节数与字数对得上，否则落库的是个读音残缺的词条。
    const std::string canonical = normalize_canonical_pinyin_for_word(selected.canonical_pinyin, selected.word);
    if (canonical.empty() || quanpin::split_segments(canonical).size() > kMaxLearnedSentenceSyllables)
    {
        return std::nullopt;
    }

    if (store_user_phrase_from_canonical_pinyin(canonical, selected.word) != 0)
    {
        return "Unable to persist the selected sentence.";
    }
    return std::nullopt;
}

int InputSession::remove_candidate(std::string pinyin, std::string word, SchemeType scheme)
{
    if (remove_delimiters(request().raw_input).size() == 1)
    {
        return -1;
    }
    return engine_.delete_by_pinyin_and_word(scheme, std::move(pinyin), std::move(word));
}

int InputSession::cache_dynamic_candidate(const std::string &pinyin, const std::string &word, CandidateSource source)
{
    const int cache_result = engine_.cache_dynamic_candidate(pinyin, word, source);
    (void)engine_.cache_dynamic_candidate_for_current_request(word, source);
    return cache_result;
}

InputSession::SelectionTransition InputSession::advance_composition_after_selection(
    const std::string &selected_pinyin, const std::string &selected_word, const std::string &selected_canonical_pinyin)
{
    SelectionTransition transition;
    transition.selected_canonical_pinyin = selected_canonical_pinyin;
    transition.full_pure_pinyin = request().normalized_input;
    const std::string current_segmentation =
        request().normalized_segmentation.empty() ? request().segmentation : request().normalized_segmentation;
    const std::string current_segmentation_with_cases = get_pinyin_segmentation_with_cases();
    const std::string selected_pure_pinyin = remove_delimiters(selected_pinyin);
    const std::string raw_input_without_helpcodes =
        quanpin::strip_active_helpcodes(request().raw_input, request().raw_input_with_cases);
    const std::string raw_input_with_cases_without_helpcodes =
        quanpin::strip_active_helpcodes_with_cases(request().raw_input, request().raw_input_with_cases);

    // 纠错读音按它在原始输入里盖住的字母消耗：多字、漏字纠错和原始字母不等长，按读音
    // 长度消耗会剩下或多吞字母（buuhui 选「不会」剩个 i）。
    size_t consumed_letters = selected_pure_pinyin.size();
    const unsigned autocorrect_types = QuanpinAutocorrectTypes(request());
    const bool literal_mixed_prefix =
        current_scheme_type() == SchemeType::Quanpin && !candidates().empty() &&
        quanpin::literal_mixed_pinyin_match(raw_input_without_helpcodes, candidates().front().pinyin,
                                            candidates().front().canonical_pinyin) &&
        raw_input_without_helpcodes.compare(0, consumed_letters, selected_pure_pinyin) == 0;
    if (autocorrect_types != 0 && !literal_mixed_prefix && raw_input_without_helpcodes.find('\'') == std::string::npos)
    {
        if (const auto aligned = quanpin::corrected_reading_raw_length(
                raw_input_without_helpcodes, quanpin::split_segments(selected_pinyin), autocorrect_types))
        {
            consumed_letters = *aligned;
        }
        else if (const auto fault_aligned = quanpin::fault_reading_raw_length(raw_input_without_helpcodes,
                                                                              quanpin::split_segments(selected_pinyin)))
        {
            // 长句容错仲裁的读音（句中简拼展开、槽内漏字变体）不在 k-best 切分里，
            // corrected_reading_raw_length 对不上；按槽位模型对回原始字母。
            consumed_letters = *fault_aligned;
        }
    }

    size_t consumed_raw_length =
        raw_length_for_effective_prefix(raw_input_with_cases_without_helpcodes, consumed_letters);
    transition.consumed_raw_input_with_cases = raw_input_with_cases_without_helpcodes.substr(0, consumed_raw_length);

    transition.continues_composition = !selected_pure_pinyin.empty() &&
                                       consumed_letters < transition.full_pure_pinyin.size() &&
                                       consumed_raw_length < raw_input_without_helpcodes.size();

    if (transition.continues_composition)
    {
        std::string rest_raw_input = raw_input_without_helpcodes.substr(consumed_raw_length);
        std::string rest_raw_input_with_cases = raw_input_with_cases_without_helpcodes.substr(consumed_raw_length);
        remove_consumed_leading_separators(rest_raw_input, rest_raw_input_with_cases);
        engine_.replace_active_raw_input(rest_raw_input, rest_raw_input_with_cases);
        online_requests_.invalidate();
        update_mixed_candidates();
        transition.current_segmentation = get_pinyin_segmentation();
        transition.current_segmentation_with_cases = get_pinyin_segmentation_with_cases();
        return transition;
    }

    transition.current_segmentation = current_segmentation;
    transition.current_segmentation_with_cases = current_segmentation_with_cases;
    return transition;
}

InputSession::CloudQueryState InputSession::get_cloud_query_state() const
{
    CloudQueryState state;

    state.committed_pinyin = request().normalized_input;
    state.cache_key = ResolveQuanpinCloudCacheKey(request());

    if (has_active_helpcode())
    {
        return state;
    }

    state.should_query = !request().normalized_input.empty();
    // 云端要按用户看到的分词来查：手动把 qi'e'huan 分成三段，就该带着音节边界发给云接口，
    // 否则云端自行贪心断句会当成 qie'huan（切换）。normalized_segmentation 保留了用户的手动
    // 撇号（cut_pinyin_with_corrections 先按撇号切分），既定住了分词，也让 to_google_spelling
    // 能逐音节把 nve / lve 换成云端认的 nue / lue 写法。分词缺失时退回裸串。
    const std::string &segmentation =
        request().normalized_segmentation.empty() ? request().normalized_input : request().normalized_segmentation;
    state.query_text = quanpin::to_google_spelling(segmentation);
    return state;
}

InputSession::CreatingWordProgress InputSession::update_creating_word_progress(
    const std::string &current_pinyin, const std::string &current_word, const std::string &selected_word,
    const SelectionTransition &selection_transition) const
{
    CreatingWordProgress progress;

    const std::string selected_canonical =
        normalize_canonical_pinyin_for_word(selection_transition.selected_canonical_pinyin, selected_word);
    const bool prior_parts_are_storeable = current_word.empty() || !current_pinyin.empty();
    if (prior_parts_are_storeable && !selected_canonical.empty())
    {
        progress.pinyin = append_canonical_pinyin(current_pinyin, selected_canonical);
    }
    progress.word = current_word + selected_word;
    progress.preedit = progress.word + selection_transition.current_segmentation_with_cases;
    progress.completed = !selection_transition.continues_composition;
    progress.can_store =
        progress.completed && !normalize_canonical_pinyin_for_word(progress.pinyin, progress.word).empty();
    return progress;
}

void InputSession::clear_pending_sequence()
{
    pending_pinyin_sequence_.clear();
    pending_pinyin_sequence_with_cases_.clear();
    has_pending_pinyin_sequence_ = false;
    has_pending_pinyin_sequence_with_cases_ = false;
}

void InputSession::apply_pending_sequence()
{
    caret_.reset();
    const std::string raw_input = has_pending_pinyin_sequence_ ? pending_pinyin_sequence_ : request().raw_input;
    const std::string raw_input_with_cases =
        has_pending_pinyin_sequence_with_cases_ ? pending_pinyin_sequence_with_cases_ : raw_input;

    switch (current_scheme_type())
    {
    case SchemeType::Quanpin:
        engine_.replace_quanpin_raw_input(raw_input, raw_input_with_cases);
        break;
    }
    clear_pending_sequence();
    online_requests_.invalidate();
    update_mixed_candidates();
}
} // namespace metasequoia
