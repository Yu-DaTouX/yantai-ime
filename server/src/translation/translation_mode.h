#pragma once
#include <cstdint>
#include <memory>
#include <string>
struct WordItem;
namespace translation_mode
{
struct View
{
    bool active = false;
    std::wstring draft, latest, english, hint;
};
std::shared_ptr<const View> Snapshot();
bool Active();
bool PreservingCommit();
void Reset();
void Poll();
void Shutdown();
void Toggle(uint64_t client, uint64_t epoch, bool toolbar = false);
bool HandleKey(uint64_t client, uint64_t epoch, uint64_t request);
bool Select(const WordItem &item, uint64_t client, uint64_t epoch);
void ReplyComposition(uint64_t client, uint64_t epoch, uint64_t request);
} // namespace translation_mode
