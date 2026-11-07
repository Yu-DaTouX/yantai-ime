#pragma once

#include <cstdint>
#include <sqlite3.h>
#include <string_view>

namespace quanpin
{
// Query helpers retain their vector-returning API. This thread-local error
// sequence distinguishes a successful miss from an incomplete query without
// throwing across the input worker. Nested queries cannot clear a parent's
// failure, and other input sessions on other threads cannot contaminate it.
inline thread_local std::uint64_t query_failure_sequence = 0;

inline void note_query_failure()
{
    ++query_failure_sequence;
}

inline void note_query_prepare_failure(sqlite3 *db, int status)
{
    // A missing partition is supported by sparse/older dictionary products.
    const std::string_view message = db ? sqlite3_errmsg(db) : "";
    if (status != SQLITE_ERROR || message.substr(0, 15) != "no such table: ")
        note_query_failure();
}

class QueryCheckpoint
{
  public:
    bool failed() const
    {
        return sequence_ != query_failure_sequence;
    }

  private:
    std::uint64_t sequence_ = query_failure_sequence;
};
} // namespace quanpin
