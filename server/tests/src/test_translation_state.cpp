#include "tests/includes/test_framework.h"
#include "translation/translation_state.h"

using namespace translation;
using namespace std::chrono_literals;

TEST_CASE(translation_draft_is_word_based_and_punctuation_is_a_word)
{
    State state;
    const Time now{};
    state.Append("不进入", "", now);
    REQUIRE(state.words().empty());
    state.Enter();
    state.Append("今天的会议", "today's meeting", now);
    state.Append("改到下午三点", "", now);
    state.Append("。", ".", now);
    REQUIRE_EQ(state.Chinese(), "今天的会议改到下午三点。");
    REQUIRE(state.Backspace(now));
    REQUIRE_EQ(state.Chinese(), "今天的会议改到下午三点");
    REQUIRE(state.Backspace(now));
    REQUIRE_EQ(state.Chinese(), "今天的会议");
    REQUIRE_EQ(state.words().back().chinese, "今天的会议");
}

TEST_CASE(translation_debounce_coalesces_and_stale_results_cannot_leak)
{
    State state;
    Time now{};
    state.Enter();
    state.Append("今天", "today", now);
    REQUIRE(!state.TakeDueRequest(now + 99ms));
    state.Append("开会", "meet", now + 80ms);
    REQUIRE(!state.TakeDueRequest(now + 100ms));
    const auto request = state.TakeDueRequest(now + 180ms);
    REQUIRE(request);
    REQUIRE_EQ(request->chinese, "今天开会");
    REQUIRE(!state.TakeDueRequest(now + 181ms));
    state.Append("。", ".", now + 190ms);
    REQUIRE(!state.Apply({request->generation, Status::Ready, "stale"}, now + 191ms));
    REQUIRE(state.english().empty());
    const auto next = state.TakeDueRequest(now + 290ms);
    REQUIRE(next);
    REQUIRE(state.Apply({next->generation, Status::Ready, "Meeting today."}, now + 300ms));
    REQUIRE_EQ(state.english(), "Meeting today.");
}

TEST_CASE(translation_enter_uses_full_english_and_stays_in_mode)
{
    State state;
    Time now{};
    state.Enter();
    state.Append("你好", "hello", now);
    const auto request = state.TakeDueRequest(now + 100ms);
    const std::string full(2000, 'x');
    REQUIRE(state.Apply({request->generation, Status::Ready, full}, now + 101ms));
    const auto commit = state.TakeCommit(CommitKind::English);
    REQUIRE_EQ(commit.text, full);
    REQUIRE(!commit.chinese_fallback);
    REQUIRE(state.active());
    REQUIRE(state.words().empty());
    REQUIRE(state.english().empty());
    REQUIRE(!state.Apply({request->generation, Status::Ready, "late"}, now + 102ms));
}

TEST_CASE(translation_shift_enter_uses_chinese_and_ctrl_enter_exits)
{
    State state;
    Time now{};
    state.Enter();
    state.Append("你好", "hello", now);
    REQUIRE_EQ(state.TakeCommit(CommitKind::Chinese).text, "你好");
    REQUIRE(state.active());
    state.Append("再见", "", now);
    const auto request = state.TakeDueRequest(now + 100ms);
    REQUIRE(state.Apply({request->generation, Status::Ready, "Bye."}, now + 101ms));
    REQUIRE_EQ(state.TakeCommit(CommitKind::EnglishAndExit).text, "Bye.");
    REQUIRE(!state.active());
}

TEST_CASE(translation_escape_clears_then_exits_and_reentry_rejects_old_result)
{
    State state;
    Time now{};
    state.Enter();
    state.Append("你好", "", now);
    const auto request = state.TakeDueRequest(now + 100ms);
    REQUIRE(state.Escape(true));
    REQUIRE(state.active());
    REQUIRE(state.Chinese().empty());
    REQUIRE(!state.Escape(false));
    REQUIRE(!state.active());
    state.Enter();
    state.Append("再见", "", now);
    REQUIRE(state.TakeDueRequest(now + 100ms));
    REQUIRE(!state.Apply({request->generation, Status::Ready, "Hello."}, now + 101ms));
    state.Exit();
    REQUIRE(state.english().empty());
}

TEST_CASE(translation_missing_model_uses_only_supplied_local_glosses)
{
    State state;
    Time now{};
    state.Enter();
    state.Append("你好", "hello", now);
    state.Append("世界", "world", now);
    const auto request = state.TakeDueRequest(now + 100ms);
    REQUIRE(state.Apply({request->generation, Status::Missing, ""}, now + 101ms));
    REQUIRE_EQ(state.status(), Status::Missing);
    REQUIRE_EQ(state.english(), "hello world");
    REQUIRE_EQ(state.TakeCommit(CommitKind::English).text, "hello world");
}

TEST_CASE(translation_missing_model_accepts_background_glosses_for_only_the_current_request)
{
    State state;
    Time now{};
    state.Enter();
    state.Append("你好", "", now);
    state.Append("世界", "", now);
    const auto request = state.TakeDueRequest(now + 100ms);
    REQUIRE_EQ(request->words.size(), size_t(2));
    REQUIRE_EQ(request->words.front().chinese, "你好");
    REQUIRE(state.Apply({request->generation, Status::Missing, "hello world"}, now + 101ms));
    REQUIRE_EQ(state.TakeCommit(CommitKind::English).text, "hello world");
    state.Append("再见", "", now);
    REQUIRE(state.TakeDueRequest(now + 100ms));
    REQUIRE(!state.Apply({request->generation, Status::Missing, "old fallback"}, now + 101ms));
    REQUIRE(state.english().empty());
}

TEST_CASE(translation_timeout_at_one_second_does_not_commit_partial_or_stale_english)
{
    State state;
    Time now{};
    state.Enter();
    state.Append("中文", "", now);
    const auto request = state.TakeDueRequest(now + 100ms);
    REQUIRE(!state.Tick(now + 1099ms));
    REQUIRE(state.Tick(now + 1100ms));
    REQUIRE_EQ(state.status(), Status::Timeout);
    REQUIRE(!state.Apply({request->generation, Status::Ready, "too late"}, now + 1101ms));
    const auto commit = state.TakeCommit(CommitKind::English);
    REQUIRE_EQ(commit.text, "中文");
    REQUIRE(commit.chinese_fallback);
    REQUIRE(state.active());
}

TEST_CASE(translation_late_completion_publishes_timeout_even_without_a_timer_tick)
{
    State state;
    Time now{};
    state.Enter();
    state.Append("中文", "", now);
    const auto request = state.TakeDueRequest(now + 100ms);
    REQUIRE(state.Apply({request->generation, Status::Ready, "late"}, now + 1100ms));
    REQUIRE_EQ(state.status(), Status::Timeout);
    REQUIRE(state.english().empty());
}

TEST_CASE(translation_enter_while_pending_preserves_chinese_instead_of_dropping_it)
{
    State state;
    Time now{};
    state.Enter();
    state.Append("中文", "", now);
    const auto commit = state.TakeCommit(CommitKind::English);
    REQUIRE_EQ(commit.text, "中文");
    REQUIRE(commit.chinese_fallback);
    REQUIRE(!state.TakeDueRequest(now + 100ms));
}
