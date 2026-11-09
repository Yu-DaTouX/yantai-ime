#include "ipc/selection_client_policy.h"
#include "tests/includes/test_framework.h"

using FanyImeIpc::ChooseSelectionClient;

namespace
{
constexpr uint64_t Client(uint32_t pid, uint32_t tid)
{
    return (static_cast<uint64_t>(pid) << 32) | tid;
}
} // namespace

TEST_CASE(selection_client_prefers_a_connected_active_client)
{
    const auto choice = ChooseSelectionClient(Client(10, 1), 5, {Client(10, 1), Client(20, 2)}, 20, {2});
    REQUIRE_EQ(choice.client_id, Client(10, 1));
    REQUIRE_EQ(choice.epoch, uint64_t(5));
    REQUIRE(!choice.via_foreground);
}

TEST_CASE(selection_client_falls_back_to_the_foreground_process_after_a_restart)
{
    // Server restarted: nothing is active (client 0, a stale epoch), the notepad client is connected.
    const auto choice = ChooseSelectionClient(0, 5, {Client(10, 1), Client(20, 2)}, 20, {2});
    REQUIRE_EQ(choice.client_id, Client(20, 2));
    REQUIRE_EQ(choice.epoch, uint64_t(0));
    REQUIRE(choice.via_foreground);
}

TEST_CASE(selection_client_ignores_a_retired_active_client)
{
    // The active client has disconnected; the foreground process's client is used instead.
    const auto choice = ChooseSelectionClient(Client(10, 1), 7, {Client(20, 2)}, 20, {2});
    REQUIRE_EQ(choice.client_id, Client(20, 2));
    REQUIRE(choice.via_foreground);
}

TEST_CASE(selection_client_picks_the_focused_thread_among_several_in_one_process)
{
    const std::vector<uint64_t> connected{Client(20, 3), Client(20, 4)};
    REQUIRE_EQ(ChooseSelectionClient(0, 0, connected, 20, {9, 4}).client_id, Client(20, 4));
    // No focused match and ambiguous: refuse to guess.
    REQUIRE_EQ(ChooseSelectionClient(0, 0, connected, 20, {9}).client_id, uint64_t(0));
}

TEST_CASE(selection_client_is_none_without_a_matching_client)
{
    REQUIRE_EQ(ChooseSelectionClient(0, 0, {Client(10, 1)}, 20, {2}).client_id, uint64_t(0));
    REQUIRE_EQ(ChooseSelectionClient(0, 0, {Client(10, 1)}, 0, {}).client_id, uint64_t(0));
    REQUIRE_EQ(ChooseSelectionClient(0, 0, {}, 20, {2}).client_id, uint64_t(0));
}
