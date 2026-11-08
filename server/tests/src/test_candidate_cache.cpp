#include "engine/common/cache.h"
#include "tests/includes/test_framework.h"

#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
struct CountedValue
{
    static inline int copies = 0;
    static inline bool throw_on_copy = false;
    int value = 0;

    explicit CountedValue(int input = 0) : value(input)
    {
    }
    CountedValue(const CountedValue &other) : value(other.value)
    {
        ++copies;
        if (throw_on_copy)
            throw std::runtime_error("cache copy failed");
    }
    CountedValue(CountedValue &&) noexcept = default;
    CountedValue &operator=(const CountedValue &) = default;
    CountedValue &operator=(CountedValue &&) noexcept = default;
};
} // namespace

TEST_CASE(candidate_cache_rvalue_insert_and_update_do_not_copy_candidates)
{
    CircularBuffer<std::string, std::vector<CountedValue>> cache(2);
    std::vector<CountedValue> candidates(512, CountedValue{1});
    CountedValue::copies = 0;
    cache.insert("a", std::move(candidates));
    REQUIRE_EQ(CountedValue::copies, 0);
    REQUIRE_EQ(cache.find("a")->size(), size_t{512});
    std::vector<CountedValue> replacement(256, CountedValue{2});
    CountedValue::copies = 0;
    cache.insert("a", std::move(replacement));
    REQUIRE_EQ(CountedValue::copies, 0);
    REQUIRE_EQ(cache.find("a")->front().value, 2);
    REQUIRE_EQ(cache.find("a")->size(), size_t{256});
}

TEST_CASE(candidate_cache_failed_insert_keeps_lru_consistent)
{
    CircularBuffer<std::string, CountedValue> cache(2);
    cache.insert("a", CountedValue{1});
    cache.insert("b", CountedValue{2});
    const CountedValue candidate{3};
    CountedValue::throw_on_copy = true;
    bool threw = false;
    try
    {
        cache.insert("c", candidate);
    }
    catch (const std::runtime_error &)
    {
        threw = true;
    }
    CountedValue::throw_on_copy = false;
    REQUIRE(threw);
    REQUIRE_EQ(cache.size(), size_t{2});
    REQUIRE(cache.find("c") == nullptr);
    // The failed replacement must not have evicted a, or left a phantom c.
    REQUIRE_EQ(cache.find("a")->value, 1);
    REQUIRE_EQ(cache.find("b")->value, 2);
    cache.insert("c", candidate);
    cache.insert("d", CountedValue{4});
    REQUIRE(cache.find("a") == nullptr);
    REQUIRE(cache.find("b") == nullptr);
    REQUIRE_EQ(cache.find("c")->value, 3);
    REQUIRE_EQ(cache.find("d")->value, 4);
}

TEST_CASE(candidate_cache_zero_capacity_disables_storage)
{
    CircularBuffer<std::string, int> cache(0);
    cache.insert("a", 1);
    cache.insert("b", 2);
    REQUIRE(cache.empty());
    REQUIRE(cache.find("a") == nullptr);
    REQUIRE(!cache.get("b").has_value());
}

TEST_CASE(candidate_cache_update_refreshes_recency)
{
    CircularBuffer<std::string, int> cache(2);
    cache.insert("a", 1);
    cache.insert("b", 2);
    cache.insert("a", 3);
    cache.insert("c", 4);
    REQUIRE(cache.find("b") == nullptr);
    REQUIRE_EQ(*cache.find("a"), 3);
    REQUIRE_EQ(*cache.find("c"), 4);
    REQUIRE_EQ(cache.size(), size_t{2});
}

TEST_CASE(candidate_cache_lookup_refreshes_recency_and_remove_releases_slot)
{
    CircularBuffer<std::string, int> cache(2);
    cache.insert("a", 1);
    cache.insert("b", 2);
    REQUIRE_EQ(*cache.find("a"), 1);
    cache.insert("c", 3);
    REQUIRE(cache.find("b") == nullptr);
    REQUIRE(cache.remove("a"));
    REQUIRE(!cache.remove("a"));
    cache.insert("d", 4);
    REQUIRE_EQ(*cache.find("c"), 3);
    cache.clear();
    REQUIRE(cache.empty());
    cache.insert("e", 5);
    REQUIRE_EQ(*cache.find("e"), 5);
}
