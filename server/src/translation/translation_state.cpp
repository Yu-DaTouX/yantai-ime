#include "translation/translation_state.h"

#include <utility>

namespace translation
{
std::string State::Chinese() const
{
    std::string text;
    for (const auto &word : words_)
        text += word.chinese;
    return text;
}

std::string State::Glosses() const
{
    std::string text;
    for (const auto &word : words_)
    {
        if (!text.empty())
            text += " ";
        text += word.gloss.empty() ? word.chinese : word.gloss;
    }
    return text;
}

void State::Enter()
{
    if (!active_)
    {
        Clear();
        active_ = true;
    }
}

void State::Exit()
{
    Clear();
    active_ = false;
}

void State::Clear()
{
    ++generation_; // Invalidates in-flight results even across exit/re-entry or focus reset.
    words_.clear();
    english_.clear();
    status_ = Status::Empty;
    due_ = {};
    deadline_ = {};
}

void State::Changed(Time now)
{
    ++generation_;
    english_.clear(); // Never display/commit the previous draft's translation.
    status_ = words_.empty() ? Status::Empty : Status::Debouncing;
    due_ = now + std::chrono::milliseconds(100);
    deadline_ = {};
}

void State::Append(std::string chinese, std::string gloss, Time now)
{
    if (!active_ || chinese.empty())
        return;
    words_.push_back({std::move(chinese), std::move(gloss)});
    Changed(now);
}

bool State::Backspace(Time now)
{
    if (!active_ || words_.empty())
        return false;
    words_.pop_back();
    Changed(now);
    return true;
}

bool State::Escape(bool has_pinyin)
{
    if (!active_)
        return false;
    if (has_pinyin || !words_.empty())
        Clear();
    else
        Exit();
    return has_pinyin;
}

std::optional<Request> State::TakeDueRequest(Time now)
{
    if (!active_ || status_ != Status::Debouncing || now < due_)
        return std::nullopt;
    status_ = Status::Translating;
    deadline_ = now + std::chrono::seconds(1);
    return Request{generation_, Chinese(), deadline_, words_};
}

bool State::Tick(Time now)
{
    if (status_ != Status::Translating || now < deadline_)
        return false;
    status_ = Status::Timeout;
    english_.clear();
    return true;
}

bool State::Apply(Result result, Time now)
{
    if (!active_ || status_ != Status::Translating || result.generation != generation_)
        return false;
    if (Tick(now))
        return true; // The timeout itself changed the view; do not publish the late hypothesis.
    switch (result.status)
    {
    case Status::Ready:
        status_ = result.english.empty() ? Status::Error : Status::Ready;
        english_ = std::move(result.english);
        break;
    case Status::Missing:
        status_ = Status::Missing;
        english_ = result.english.empty() ? Glosses() : std::move(result.english);
        break;
    case Status::Timeout:
    case Status::Error:
        status_ = result.status;
        english_.clear();
        break;
    default:
        return false;
    }
    return true;
}

Commit State::TakeCommit(CommitKind kind)
{
    if (!active_)
        return {};
    // Caller must first accept any unselected pinyin candidate into the draft.
    const bool use_english =
        kind != CommitKind::Chinese && (status_ == Status::Ready || status_ == Status::Missing) && !english_.empty();
    Commit commit{use_english ? english_ : Chinese(), kind != CommitKind::Chinese && !use_english && !words_.empty()};
    Clear();
    if (kind == CommitKind::EnglishAndExit)
        active_ = false;
    return commit;
}
} // namespace translation
