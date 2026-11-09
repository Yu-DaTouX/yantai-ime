#include "engine/contracts/selection_translate.h"
#include "tests/includes/test_framework.h"
#include "translation/selection_translate_state.h"
#include "translation/sentence_split.h"

#include <string>
#include <vector>

using namespace translation;
using namespace translation::selection;
using FanyImeSelectionTranslate::Failure;
using namespace std::chrono_literals;

namespace
{
std::vector<std::wstring> Texts(const SplitText &split)
{
    std::vector<std::wstring> out;
    for (const Piece &piece : split.pieces)
        out.push_back(piece.text);
    return out;
}

std::wstring Rebuild(const SplitText &split)
{
    std::wstring out = split.lead;
    for (const Piece &piece : split.pieces)
        out += piece.text + piece.gap;
    return out;
}

const Time kStart = Time{} + 1000s;

// Drives a machine to the given phase through the normal flow; returns the job id.
uint32_t ToTranslating(Machine &machine, const std::wstring &text, Time now = kStart)
{
    REQUIRE(machine.Click(now, true).action == Action::SendRead);
    const uint32_t job = machine.job();
    REQUIRE(machine.OnSelectionText(job, text, now).action == Action::TranslateNext);
    return job;
}
} // namespace

// ---- 按句切分 ----

TEST_CASE(selection_split_cuts_at_sentence_marks_and_keeps_every_character)
{
    const std::wstring text = L"你好。今天天气不错！你吃饭了吗？好的";
    const SplitText split = SplitSentences(text);
    REQUIRE_EQ(Texts(split), (std::vector<std::wstring>{L"你好。", L"今天天气不错！", L"你吃饭了吗？", L"好的"}));
    REQUIRE(split.TranslatableCount() == 4);
    REQUIRE_EQ(Rebuild(split), text);
}

TEST_CASE(selection_split_keeps_line_breaks_and_surrounding_blanks)
{
    const std::wstring text = L"  第一行\r\n\r\n第二行。 第三句\n";
    const SplitText split = SplitSentences(text);
    REQUIRE_EQ(split.lead, std::wstring(L"  "));
    REQUIRE_EQ(Texts(split), (std::vector<std::wstring>{L"第一行", L"第二行。", L"第三句"}));
    REQUIRE_EQ(split.pieces[0].gap, std::wstring(L"\r\n\r\n"));
    REQUIRE_EQ(split.pieces[1].gap, std::wstring(L" "));
    REQUIRE_EQ(Rebuild(split), text);
}

TEST_CASE(selection_split_leaves_pieces_without_chinese_verbatim)
{
    const SplitText split = SplitSentences(L"版本 3.14 发布了。OK. 123\n");
    REQUIRE_EQ(Texts(split), (std::vector<std::wstring>{L"版本 3.14 发布了。", L"OK.", L"123"}));
    REQUIRE(split.pieces[0].translate);
    REQUIRE(!split.pieces[1].translate);
    REQUIRE(!split.pieces[2].translate);
    // A decimal point is not a sentence end.
    REQUIRE(SplitSentences(L"圆周率是3.14159").pieces.size() == 1);
}

TEST_CASE(selection_split_keeps_closing_quotes_with_their_sentence)
{
    const SplitText split = SplitSentences(L"他说：“走吧！”然后离开了。");
    REQUIRE_EQ(Texts(split), (std::vector<std::wstring>{L"他说：“走吧！”", L"然后离开了。"}));
}

TEST_CASE(selection_split_cuts_long_sentences_at_clause_marks_then_by_length)
{
    std::wstring clauses;
    for (int i = 0; i < 6; ++i)
        clauses += L"这是一个比较长的分句内容，";
    clauses += L"结束。";
    const SplitText byClause = SplitSentences(clauses);
    REQUIRE(byClause.pieces.size() > 1);
    for (const Piece &piece : byClause.pieces)
        REQUIRE(piece.text.size() <= kMaxPieceChars);
    REQUIRE_EQ(Rebuild(byClause), clauses);
    // Only the last fragment carries the source gap.
    REQUIRE(byClause.pieces.front().gap.empty());

    const std::wstring unbroken(kMaxPieceChars * 2 + 5, L'字');
    const SplitText byLength = SplitSentences(unbroken);
    REQUIRE(byLength.pieces.size() == 3);
    for (const Piece &piece : byLength.pieces)
        REQUIRE(piece.text.size() <= kMaxPieceChars);
    REQUIRE_EQ(Rebuild(byLength), unbroken);
}

TEST_CASE(selection_split_never_cuts_a_surrogate_pair)
{
    std::wstring text;
    for (size_t i = 0; i + 1 < kMaxPieceChars; ++i)
        text.push_back(L'字');
    text += L"\xD842\xDFB7"; // U+20BB7 straddles the hard cut
    text += L"字字字";
    for (const Piece &piece : SplitSentences(text).pieces)
    {
        REQUIRE(piece.text.back() < 0xD800 || piece.text.back() > 0xDBFF);
    }
}

TEST_CASE(selection_assemble_joins_translated_sentences_with_spaces_and_keeps_layout)
{
    const SplitText split = SplitSentences(L"你好。再见\n123\n");
    const std::vector<std::wstring> english{L"Hello.\n", L"Goodbye", L"ignored"};
    REQUIRE_EQ(Assemble(split, english), std::wstring(L"Hello. Goodbye\n123\n"));
    REQUIRE(!HasChinese(L"Hello"));
    REQUIRE(HasChinese(L"a水b"));
}

// ---- 状态机：空闲 / 翻译中 / 可还原 / 超时 ----

TEST_CASE(selection_machine_full_translate_and_restore_cycle)
{
    Machine machine;
    REQUIRE(machine.phase() == Phase::Idle);
    REQUIRE(machine.view() == ButtonView::Translate);

    const uint32_t job = ToTranslating(machine, L"你好。再见。");
    REQUIRE(machine.phase() == Phase::Translating);
    REQUIRE(machine.view() == ButtonView::Busy);
    REQUIRE_EQ(machine.total(), size_t(2));
    REQUIRE_EQ(machine.NextSource(), std::wstring(L"你好。"));

    Step step = machine.OnPieceTranslated(job, Machine::EngineStatus::Ready, L"Hello.", kStart + 1s);
    REQUIRE(step.action == Action::TranslateNext);
    REQUIRE_EQ(machine.done(), size_t(1));
    REQUIRE_EQ(machine.NextSource(), std::wstring(L"再见。"));
    step = machine.OnPieceTranslated(job, Machine::EngineStatus::Ready, L"Goodbye.", kStart + 2s);
    REQUIRE(step.action == Action::SendApply);
    REQUIRE_EQ(machine.ApplyText(), std::wstring(L"Hello. Goodbye."));
    REQUIRE(machine.phase() == Phase::Applying);

    step = machine.OnApplied(job, kStart + 3s);
    REQUIRE(step.changed);
    REQUIRE(machine.phase() == Phase::Restorable);
    REQUIRE(machine.view() == ButtonView::Restore);

    REQUIRE(machine.Click(kStart + 4s, true).action == Action::SendRestore);
    REQUIRE(machine.phase() == Phase::Restoring);
    step = machine.OnRestored(job);
    REQUIRE(step.tip == Tip::None);
    REQUIRE(machine.phase() == Phase::Idle);
}

TEST_CASE(selection_machine_ignores_clicks_while_busy)
{
    Machine machine;
    REQUIRE(machine.Click(kStart, true).action == Action::SendRead);
    const uint32_t job = machine.job();
    REQUIRE(machine.Click(kStart + 1s, true).action == Action::None); // Reading
    REQUIRE(machine.job() == job);
    machine.OnSelectionText(job, L"你好", kStart + 1s);
    REQUIRE(machine.Click(kStart + 2s, true).action == Action::None); // Translating
    machine.OnPieceTranslated(job, Machine::EngineStatus::Ready, L"Hello", kStart + 2s);
    REQUIRE(machine.phase() == Phase::Applying);
    REQUIRE(machine.Click(kStart + 3s, true).action == Action::None); // Applying
    REQUIRE(machine.job() == job);
}

TEST_CASE(selection_machine_restore_window_expires_after_ten_seconds)
{
    Machine machine;
    const uint32_t job = ToTranslating(machine, L"你好");
    machine.OnPieceTranslated(job, Machine::EngineStatus::Ready, L"Hello", kStart);
    machine.OnApplied(job, kStart);
    REQUIRE(!machine.Tick(kStart + 9s).changed);
    REQUIRE(machine.phase() == Phase::Restorable);
    REQUIRE(machine.Tick(kStart + kRestoreWindow).changed);
    REQUIRE(machine.phase() == Phase::Idle);

    // A click right after expiry (before any Tick) starts a fresh translation instead of restoring.
    const uint32_t second = ToTranslating(machine, L"你好", kStart + 20s);
    machine.OnPieceTranslated(second, Machine::EngineStatus::Ready, L"Hello", kStart + 20s);
    machine.OnApplied(second, kStart + 20s);
    REQUIRE(machine.Click(kStart + 20s + kRestoreWindow + 1s, true).action == Action::SendRead);
}

TEST_CASE(selection_machine_invalidation_and_route_change_drop_the_restore)
{
    Machine machine;
    uint32_t job = ToTranslating(machine, L"你好");
    machine.OnPieceTranslated(job, Machine::EngineStatus::Ready, L"Hello", kStart);
    machine.OnApplied(job, kStart);
    REQUIRE(!machine.OnInvalidated(job + 1).changed); // another job's report
    REQUIRE(machine.phase() == Phase::Restorable);
    REQUIRE(machine.OnInvalidated(job).changed);
    REQUIRE(machine.phase() == Phase::Idle);

    job = ToTranslating(machine, L"你好", kStart + 1s);
    REQUIRE(machine.OnRouteChanged().changed);
    REQUIRE(machine.phase() == Phase::Idle);
    // A late engine result for the abandoned job changes nothing.
    REQUIRE(!machine.OnPieceTranslated(job, Machine::EngineStatus::Ready, L"Hello", kStart + 2s).changed);
    REQUIRE(machine.phase() == Phase::Idle);
    REQUIRE(!machine.OnRouteChanged().changed);
}

TEST_CASE(selection_machine_times_out_each_waiting_phase)
{
    Machine reading;
    reading.Click(kStart, true);
    REQUIRE(!reading.Tick(kStart + 3s).changed);
    Step step = reading.Tick(kStart + kReadTimeout);
    REQUIRE(step.tip == Tip::Unsupported); // host (or old DLL) never answered
    REQUIRE(reading.phase() == Phase::Idle);

    Machine translating;
    ToTranslating(translating, L"你好");
    REQUIRE(translating.Tick(kStart + kTranslateTimeout).tip == Tip::Timeout);

    Machine applying;
    uint32_t job = ToTranslating(applying, L"你好");
    applying.OnPieceTranslated(job, Machine::EngineStatus::Ready, L"Hello", kStart);
    REQUIRE(applying.Tick(kStart + kHostEditTimeout).tip == Tip::HostRejected);

    Machine restoring;
    job = ToTranslating(restoring, L"你好");
    restoring.OnPieceTranslated(job, Machine::EngineStatus::Ready, L"Hello", kStart);
    restoring.OnApplied(job, kStart);
    restoring.Click(kStart + 1s, true);
    REQUIRE(restoring.Tick(kStart + 1s + kHostEditTimeout).tip == Tip::CannotRestore);
    REQUIRE(restoring.phase() == Phase::Idle);
}

// ---- 无选区 / 未安装 / 失败路径 ----

TEST_CASE(selection_machine_reports_missing_component_without_reading_the_selection)
{
    Machine machine;
    const Step step = machine.Click(kStart, false);
    REQUIRE(step.action == Action::None);
    REQUIRE(step.tip == Tip::NotInstalled);
    REQUIRE(machine.phase() == Phase::Idle);
    REQUIRE_EQ(std::wstring(TipText(Tip::NotInstalled)), std::wstring(L"翻译组件未安装，请到设置里下载"));
}

TEST_CASE(selection_machine_tells_the_user_when_nothing_is_selected)
{
    Machine machine;
    machine.Click(kStart, true);
    Step step = machine.OnHostFailure(machine.job(), Failure::NoSelection);
    REQUIRE(step.tip == Tip::SelectFirst);
    REQUIRE(machine.phase() == Phase::Idle);
    REQUIRE_EQ(std::wstring(TipText(Tip::SelectFirst)), std::wstring(L"先选中要翻译的文字"));

    // A host that returns an empty or blank string is the same as no selection.
    machine.Click(kStart, true);
    REQUIRE(machine.OnSelectionText(machine.job(), L"", kStart).tip == Tip::SelectFirst);
    machine.Click(kStart, true);
    REQUIRE(machine.OnSelectionText(machine.job(), L" \r\n\t", kStart).tip == Tip::SelectFirst);
}

TEST_CASE(selection_machine_maps_host_failures_to_tips)
{
    const std::vector<std::pair<Failure, Tip>> cases{{Failure::Unsupported, Tip::Unsupported},
                                                     {Failure::TooLong, Tip::TooLong},
                                                     {Failure::Composing, Tip::Composing},
                                                     {Failure::HostRejected, Tip::HostRejected},
                                                     {Failure::SelectionChanged, Tip::SelectionChanged}};
    for (const auto &[failure, tip] : cases)
    {
        Machine machine;
        machine.Click(kStart, true);
        REQUIRE(machine.OnHostFailure(machine.job(), failure).tip == tip);
        REQUIRE(machine.phase() == Phase::Idle);
    }
    // A failure for an old job is ignored.
    Machine machine;
    machine.Click(kStart, true);
    REQUIRE(!machine.OnHostFailure(machine.job() + 1, Failure::Unsupported).changed);
    REQUIRE(machine.phase() == Phase::Reading);
}

TEST_CASE(selection_machine_rejects_text_without_chinese_or_too_long)
{
    Machine machine;
    machine.Click(kStart, true);
    REQUIRE(machine.OnSelectionText(machine.job(), L"Hello world 123", kStart).tip == Tip::NotChinese);
    REQUIRE(machine.phase() == Phase::Idle);

    machine.Click(kStart, true);
    const std::wstring tooLong(FanyImeSelectionTranslate::kMaxSelectionChars + 1, L'字');
    REQUIRE(machine.OnSelectionText(machine.job(), tooLong, kStart).tip == Tip::TooLong);
    REQUIRE(machine.phase() == Phase::Idle);
}

TEST_CASE(selection_machine_engine_failures_end_the_job_without_replacing)
{
    const std::vector<std::pair<Machine::EngineStatus, Tip>> cases{{Machine::EngineStatus::Missing, Tip::NotInstalled},
                                                                   {Machine::EngineStatus::Timeout, Tip::Timeout},
                                                                   {Machine::EngineStatus::Error, Tip::Failed}};
    for (const auto &[status, tip] : cases)
    {
        Machine machine;
        const uint32_t job = ToTranslating(machine, L"你好。再见。");
        const Step step = machine.OnPieceTranslated(job, status, L"", kStart + 1s);
        REQUIRE(step.tip == tip);
        REQUIRE(step.action == Action::None);
        REQUIRE(machine.phase() == Phase::Idle);
        REQUIRE(machine.ApplyText().empty());
    }
    // An empty "successful" translation is a failure, never a replacement with nothing.
    Machine machine;
    const uint32_t job = ToTranslating(machine, L"你好");
    REQUIRE(machine.OnPieceTranslated(job, Machine::EngineStatus::Ready, L"  ", kStart).tip == Tip::Failed);
}

TEST_CASE(selection_machine_failed_restore_says_it_cannot_restore)
{
    Machine machine;
    const uint32_t job = ToTranslating(machine, L"你好");
    machine.OnPieceTranslated(job, Machine::EngineStatus::Ready, L"Hello", kStart);
    machine.OnApplied(job, kStart);
    machine.Click(kStart + 1s, true);
    const Step step = machine.OnHostFailure(job, Failure::SelectionChanged);
    REQUIRE(step.tip == Tip::CannotRestore);
    REQUIRE(machine.phase() == Phase::Idle);
}

// ---- 分块传输 ----

TEST_CASE(selection_wire_down_frame_round_trips_text_containing_separators)
{
    using namespace FanyImeSelectionTranslate;
    DownFrame frame{Command::Apply, 7, 2, 5, L"a|b|c 你好"};
    DownFrame decoded;
    REQUIRE(DecodeDown(EncodeDown(frame), decoded));
    REQUIRE(decoded.command == Command::Apply);
    REQUIRE(decoded.job == 7 && decoded.seq == 2 && decoded.total == 5);
    REQUIRE_EQ(decoded.text, frame.text);

    DownFrame ignored;
    REQUIRE(!DecodeDown(L"", ignored));
    REQUIRE(!DecodeDown(L"X|1|0|1|", ignored));
    REQUIRE(!DecodeDown(L"R|0|0|1|", ignored));   // job 0
    REQUIRE(!DecodeDown(L"A|1|3|3|x", ignored));  // seq >= total
    REQUIRE(!DecodeDown(L"A|1|0|65|x", ignored)); // too many chunks
    REQUIRE(!DecodeDown(L"A|1|0|1", ignored));    // no separator after total
    REQUIRE(!DecodeDown(L"A|99999999999|0|1|", ignored));
}

TEST_CASE(selection_wire_chunks_reassemble_and_reject_damaged_streams)
{
    using namespace FanyImeSelectionTranslate;
    std::wstring text;
    for (int i = 0; i < 40; ++i)
        text += L"句子" + std::to_wstring(i) + L"。\U00020BB7";
    const std::vector<std::wstring> chunks = SplitChunks(text, 17);
    for (const std::wstring &chunk : chunks)
    {
        REQUIRE(chunk.size() <= 17);
        REQUIRE(chunk.back() < 0xD800 || chunk.back() > 0xDBFF);
    }
    ChunkAssembler assembler;
    for (size_t i = 0; i < chunks.size(); ++i)
    {
        const auto result = assembler.Add(3, static_cast<uint32_t>(i), static_cast<uint32_t>(chunks.size()), chunks[i],
                                          kMaxSelectionChars);
        REQUIRE(result ==
                (i + 1 == chunks.size() ? ChunkAssembler::Result::Complete : ChunkAssembler::Result::Incomplete));
    }
    REQUIRE_EQ(assembler.Take(), text);

    // A gap, another job, or text over the cap discards everything collected.
    REQUIRE(assembler.Add(4, 0, 3, L"a", 100) == ChunkAssembler::Result::Incomplete);
    REQUIRE(assembler.Add(4, 2, 3, L"c", 100) == ChunkAssembler::Result::Invalid);
    REQUIRE(assembler.Add(4, 0, 3, L"a", 100) == ChunkAssembler::Result::Incomplete);
    REQUIRE(assembler.Add(5, 1, 3, L"b", 100) == ChunkAssembler::Result::Invalid);
    REQUIRE(assembler.Add(6, 0, 2, L"12345", 8) == ChunkAssembler::Result::Incomplete);
    REQUIRE(assembler.Add(6, 1, 2, L"67890", 8) == ChunkAssembler::Result::Invalid);
    REQUIRE(SplitChunks(L"", 10).size() == 1);
}

TEST_CASE(selection_machine_asks_for_a_click_when_no_client_is_found)
{
    Machine machine;
    machine.Click(kStart, true);
    const Step step = machine.OnHostFailure(machine.job(), Failure::NoClient);
    REQUIRE(step.tip == Tip::NeedFocus);
    REQUIRE(machine.phase() == Phase::Idle);
    REQUIRE_EQ(std::wstring(TipText(Tip::NeedFocus)), std::wstring(L"请先点一下要翻译的窗口再试"));
}
