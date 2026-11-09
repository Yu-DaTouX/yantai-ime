#pragma once

// Selection translation ("译" button) wire format shared by the Server and the TSF DLL.
//
// Every pipe frame has a small fixed text capacity, but a selected paragraph can be much longer, so the text
// travels in ordered chunks that both ends reassemble with ChunkAssembler:
//   Server -> TSF  worker opcode FanyImeWorkerReplyType::SelectionTranslateCommand, one frame per chunk:
//                  "<R|A|U>|<job>|<seq>|<total>|<chunk>"
//                  R = read the host selection (no text), A = replace it with the assembled translation,
//                  U = restore the original text (no text).
//   TSF -> Server  Main-pipe event FanyImePipeEventType::SelectionTranslateReport, see ReportKind.
// Job ids are non-zero and only compared for equality; either side drops frames of a job it is not waiting for.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace FanyImeSelectionTranslate
{
// A selection longer than this is refused before anything is sent (about 15 sentences).
constexpr std::size_t kMaxSelectionChars = 1500;
// Upper bound for the assembled translation; keeps one job inside the worker pipe's pending-message budget.
constexpr std::size_t kMaxTranslationChars = 6000;
// Chunk sizes leave room for the frame header inside the 200 / 128 wchar capacities.
constexpr std::size_t kDownChunkChars = 150;
constexpr std::size_t kUpChunkChars = 100;
constexpr std::uint32_t kMaxChunks = 64;

enum class Command : wchar_t
{
    Read = L'R',
    Apply = L'A',
    Restore = L'U',
};

struct DownFrame
{
    Command command = Command::Read;
    std::uint32_t job = 0;
    std::uint32_t seq = 0;
    std::uint32_t total = 1;
    std::wstring text;
};

// Main-pipe event fields: keycode = ReportKind, point[0] = job, point[1] = seq << 16 | total,
// wch = Failure (kind Failure / Applied / Restored when not ok), pinyin_string = chunk (ReadChunk only).
enum class ReportKind : std::uint32_t
{
    ReadChunk = 1,
    Failure = 2,
    Applied = 3,
    Restored = 4,
    // The selection or focus moved away from the translation; it can no longer be restored.
    Invalidated = 5,
};

enum class Failure : std::uint32_t
{
    None = 0,
    NoSelection = 1,
    Unsupported = 2,
    TooLong = 3,
    Composing = 4,
    HostRejected = 5,
    SelectionChanged = 6,
};

inline std::wstring EncodeDown(const DownFrame &frame)
{
    std::wstring out;
    out.push_back(static_cast<wchar_t>(frame.command));
    out.push_back(L'|');
    out += std::to_wstring(frame.job);
    out.push_back(L'|');
    out += std::to_wstring(frame.seq);
    out.push_back(L'|');
    out += std::to_wstring(frame.total);
    out.push_back(L'|');
    out += frame.text;
    return out;
}

inline bool ParseNumber(const std::wstring &text, std::size_t &pos, std::uint32_t &value)
{
    std::uint64_t acc = 0;
    std::size_t digits = 0;
    while (pos < text.size() && text[pos] >= L'0' && text[pos] <= L'9')
    {
        acc = acc * 10 + static_cast<std::uint64_t>(text[pos] - L'0');
        if (acc > 0xFFFFFFFFull)
            return false;
        ++pos;
        ++digits;
    }
    if (digits == 0 || pos >= text.size() || text[pos] != L'|')
        return false;
    ++pos;
    value = static_cast<std::uint32_t>(acc);
    return true;
}

inline bool DecodeDown(const std::wstring &payload, DownFrame &frame)
{
    if (payload.size() < 2 || payload[1] != L'|')
        return false;
    const wchar_t command = payload[0];
    if (command != L'R' && command != L'A' && command != L'U')
        return false;
    std::size_t pos = 2;
    DownFrame parsed;
    parsed.command = static_cast<Command>(command);
    if (!ParseNumber(payload, pos, parsed.job) || !ParseNumber(payload, pos, parsed.seq) ||
        !ParseNumber(payload, pos, parsed.total))
        return false;
    if (parsed.job == 0 || parsed.total == 0 || parsed.total > kMaxChunks || parsed.seq >= parsed.total)
        return false;
    parsed.text = payload.substr(pos);
    frame = std::move(parsed);
    return true;
}

// Splits text into pieces of at most maxChars UTF-16 units without separating a surrogate pair.
// Empty text yields one empty piece so the receiver still sees a complete (empty) message.
inline std::vector<std::wstring> SplitChunks(const std::wstring &text, std::size_t maxChars)
{
    std::vector<std::wstring> chunks;
    if (maxChars < 2)
        maxChars = 2;
    std::size_t pos = 0;
    while (pos < text.size())
    {
        std::size_t len = (std::min)(maxChars, text.size() - pos);
        if (pos + len < text.size() && len > 1 && text[pos + len - 1] >= 0xD800 && text[pos + len - 1] <= 0xDBFF)
            --len;
        chunks.push_back(text.substr(pos, len));
        pos += len;
    }
    if (chunks.empty())
        chunks.emplace_back();
    return chunks;
}

// Reassembles one job's chunks. A chunk that is out of order, belongs to another job or disagrees about the total
// discards what was collected so far, so a damaged stream never yields a partial text.
class ChunkAssembler
{
  public:
    enum class Result
    {
        Incomplete,
        Complete,
        Invalid,
    };

    Result Add(std::uint32_t job, std::uint32_t seq, std::uint32_t total, const std::wstring &chunk,
               std::size_t maxChars)
    {
        if (job == 0 || total == 0 || total > kMaxChunks || seq >= total)
            return Fail();
        if (seq == 0)
        {
            job_ = job;
            total_ = total;
            next_ = 0;
            text_.clear();
        }
        else if (job != job_ || total != total_ || seq != next_)
        {
            return Fail();
        }
        if (job_ == 0 || text_.size() + chunk.size() > maxChars)
            return Fail();
        text_ += chunk;
        next_ = seq + 1;
        if (next_ < total_)
            return Result::Incomplete;
        return Result::Complete;
    }

    void Reset()
    {
        job_ = 0;
        total_ = 0;
        next_ = 0;
        text_.clear();
    }

    std::uint32_t job() const
    {
        return job_;
    }
    // Valid only after Add returned Complete; leaves the assembler empty.
    std::wstring Take()
    {
        std::wstring out = std::move(text_);
        Reset();
        return out;
    }

  private:
    Result Fail()
    {
        Reset();
        return Result::Invalid;
    }
    std::uint32_t job_ = 0;
    std::uint32_t total_ = 0;
    std::uint32_t next_ = 0;
    std::wstring text_;
};
} // namespace FanyImeSelectionTranslate
