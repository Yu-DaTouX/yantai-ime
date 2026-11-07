#include "engine/common/cache.h"
#include "tests/includes/test_framework.h"

#include <string>

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
