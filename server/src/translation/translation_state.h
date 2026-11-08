#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace translation
{
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;

enum class Status
{
    Empty,
    Debouncing,
    Translating,
    Ready,
    Missing,
    Timeout,
    Error
};
enum class CommitKind
{
    English,
    Chinese,
    EnglishAndExit
};

struct DraftWord
{
    std::string chinese;
    std::string gloss;
};

struct Request
{
    uint64_t generation = 0;
    std::string chinese;
    Time deadline{};
    std::vector<DraftWord> words; // Background-only english.db fallback input.
};

struct Result
{
    uint64_t generation = 0;
    Status status = Status::Error;
    std::string english;
};

struct Commit
{
    std::string text;
    bool chinese_fallback = false;
};

// Server-owned, single-threaded state machine. No configuration, database writes, logging,
// networking or HWND/TSF dependency. The caller edits raw pinyin before calling Backspace.
class State
{
  public:
    bool active() const
    {
        return active_;
    }
    Status status() const
    {
        return status_;
    }
    uint64_t generation() const
    {
        return generation_;
    }
    const std::vector<DraftWord> &words() const
    {
        return words_;
    }
    const std::string &english() const
    {
        return english_;
    }
    std::string Chinese() const;
    std::string Glosses() const;

    void Enter();
    void Exit();
    void Append(std::string chinese, std::string gloss, Time now);
    bool Backspace(Time now);
    // Returns true if raw pinyin must also be cleared. Empty Escape exits the mode.
    bool Escape(bool has_pinyin);
    void Clear();
    std::optional<Request> TakeDueRequest(Time now);
    bool Apply(Result result, Time now);
    bool Tick(Time now);
    Commit TakeCommit(CommitKind kind);

  private:
    void Changed(Time now);
    bool active_ = false;
    Status status_ = Status::Empty;
    uint64_t generation_ = 0;
    std::vector<DraftWord> words_;
    std::string english_;
    Time due_{};
    Time deadline_{};
};
} // namespace translation
