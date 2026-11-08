// Real translation-mode controller + real engine/dictionary, with only IPC/window boundaries stubbed.
#include "tests/includes/test_framework.h"
#include "translation/translation_mode.h"
#include "ipc/event_listener_internal.h"
#include "session/engine_input_session.h"
#include "global/globals.h"
#include "utils/common_utils.h"
#include "engine/contracts/key_chord.h"
#include "engine/contracts/translation_mode.h"
#include <chrono>
#include <atomic>
#include <thread>

namespace
{
bool current = true;
bool supports = true;
std::wstring committed;
UINT replyType = 0;
uint64_t replyRequest = 0;
std::wstring replyPayload;
std::vector<std::pair<UINT, std::wstring>> workerPayloads;
struct Session
{
    std::shared_ptr<IInputSession> previous = g_inputSession;
    Session()
    {
        current = true;
        supports = true;
        event_listener_detail::g_english_input_mode = false;
        workerPayloads.clear();
        g_inputSession = std::make_shared<EngineInputSession>();
    }
    ~Session()
    {
        translation_mode::Shutdown();
        g_inputSession = previous;
        event_listener_detail::g_english_input_mode = false;
        current = true;
    }
    void Spell(const std::string &text)
    {
        g_inputSession->set_pinyin_sequence(text);
        g_inputSession->set_pinyin_sequence_with_cases(text);
        g_inputSession->recompute_candidates();
        FanyNamedPipe::PrepareCandidateList(7, 9);
    }
    void Key(UINT key, UINT modifiers = 0)
    {
        Global::Keycode = key;
        Global::ModifiersDown = modifiers;
        Global::Wch = static_cast<WCHAR>(key);
        REQUIRE(translation_mode::HandleKey(7, 9, 3));
    }
};
} // namespace
namespace event_listener_detail
{
bool g_english_input_mode = false;
int g_authoritative_cn_mode = 1;
void PublishCandidateUiOwner(uint64_t, uint64_t)
{
}
void PostCaretStateBadge(FanyImeUi::CaretStateBadge, int, int)
{
}
void RefreshCandidatePageUi(bool)
{
}
void EnsureCandidatePageReady()
{
}
std::string CandidateOutputText(const WordItem &item)
{
    return item.word;
}
std::wstring BuildCreateWordPipePayload(const std::string &raw, const std::string &word)
{
    return string_to_wstring(raw) + L"\t" + string_to_wstring(word) + L"\t" + string_to_wstring(raw);
}
} // namespace event_listener_detail
bool ClientNegotiatedTranslationMode(uint64_t)
{
    return supports;
}
bool IsPipeActivationCurrent(uint64_t client, uint64_t epoch)
{
    return current && client == 7 && epoch == 9;
}
bool SendToTsfClientViaNamedpipe(uint64_t, uint64_t, UINT, uint64_t, const std::wstring &)
{
    return true;
}
bool SendToTsfWorkerThreadClientViaNamedpipe(uint64_t, uint64_t, UINT type, const std::wstring &payload)
{
    workerPayloads.emplace_back(type, payload);
    return true;
}
namespace FanyNamedPipe
{
void PrepareCandidateList(uint64_t, uint64_t)
{
    Global::candidate_ui.set_items(g_inputSession->get_candidates());
    Global::candidate_ui.page_words.clear();
    for (const auto &item : g_inputSession->get_candidates())
        Global::candidate_ui.page_words.push_back(string_to_wstring(item.word));
    Global::candidate_ui.selected_index_in_page = 0;
}
bool ResolveCandidateItem(int n, WordItem &item)
{
    const auto &items = g_inputSession->get_candidates();
    if (n < 1 || static_cast<size_t>(n) > items.size())
        return false;
    item = items[n - 1];
    return true;
}
void ProcessSelectionKey(UINT, uint64_t client, uint64_t epoch, int)
{
    WordItem item;
    if (ResolveCandidateItem(1, item))
        translation_mode::Select(item, client, epoch);
}
void EditTranslationBackspace()
{
    auto raw = g_inputSession->get_pinyin_sequence_with_cases();
    if (!raw.empty())
        raw.pop_back();
    g_inputSession->set_pinyin_sequence(raw);
    g_inputSession->set_pinyin_sequence_with_cases(raw);
    g_inputSession->recompute_candidates();
}
void ClearState()
{
    if (!translation_mode::PreservingCommit())
        translation_mode::Reset();
    g_inputSession->reset_state();
}
bool SendCurrentDataToClient(uint64_t, uint64_t, uint64_t request)
{
    replyRequest = request;
    replyType = Global::MsgTypeToTsf;
    replyPayload = Global::candidate_ui.selected_text;
    if (replyType == FanyImeReplyType::TranslationCommitEnd)
    {
        committed = Global::candidate_ui.selected_text;
        ClearState();
    }
    return true;
}
} // namespace FanyNamedPipe
TEST_CASE(translation_controller_composing_default_ctrl_t_replies_without_waiting_for_inference)
{
    Session session;
    session.Spell("jintiandehuiyi");
    REQUIRE(!g_inputSession->get_candidates().empty());
    const auto expected = string_to_wstring(g_inputSession->get_candidates().front().word);
    replyType = FanyImeReplyType::TransportUnavailable;
    const auto start = std::chrono::steady_clock::now();
    session.Key('T', FanyImeKeyChord::Control); // Use actual key entry, not Toggle().
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed < std::chrono::milliseconds(250)); // Well below TSF's 1500 ms timeout.
    REQUIRE_EQ(replyType, FanyImeReplyType::TranslationComposition);
    REQUIRE_EQ(replyRequest, uint64_t(3));
    FanyImeTranslationMode::Snapshot acknowledgment;
    REQUIRE(FanyImeTranslationMode::Parse(replyPayload, acknowledgment));
    REQUIRE(acknowledgment.active);
    REQUIRE(!acknowledgment.composition.empty());
    REQUIRE(translation_mode::Active());
    REQUIRE_EQ(translation_mode::Snapshot()->draft, expected);
    REQUIRE_EQ(translation_mode::Snapshot()->english, L"翻译中…");
    REQUIRE(g_inputSession->get_pinyin_sequence().empty());
    session.Key('T', FanyImeKeyChord::Control);
    REQUIRE(!translation_mode::Active());
    Global::Keycode = 'T';
    Global::ModifiersDown = FanyImeKeyChord::Control;
    REQUIRE(!translation_mode::HandleKey(7, 9, 4)); // Idle chord belongs to Chrome.
}

TEST_CASE(translation_controller_refused_toggle_acknowledges_actual_mode_and_preserves_pinyin)
{
    Session session;
    session.Spell("nihao");
    event_listener_detail::g_english_input_mode = true;
    session.Key('T', FanyImeKeyChord::Control);
    FanyImeTranslationMode::Snapshot acknowledgment;
    REQUIRE_EQ(replyType, FanyImeReplyType::TranslationComposition);
    REQUIRE(FanyImeTranslationMode::Parse(replyPayload, acknowledgment));
    REQUIRE(!acknowledgment.active);
    REQUIRE(!translation_mode::Active());
    REQUIRE_EQ(g_inputSession->get_pinyin_sequence(), "nihao");
}

TEST_CASE(translation_controller_idle_toolbar_state_and_composition_are_revisioned)
{
    Session session;
    translation_mode::Toggle(7, 9, true);
    REQUIRE(translation_mode::Active());
    REQUIRE(!workerPayloads.empty());
    FanyImeTranslationMode::Snapshot entered;
    REQUIRE(FanyImeTranslationMode::Parse(workerPayloads.back().second, entered));
    REQUIRE(entered.active);
    const auto revision = entered.revision;
    translation_mode::Toggle(7, 9, true);
    REQUIRE(!translation_mode::Active());
    REQUIRE_EQ(workerPayloads.back().first, FanyImeWorkerReplyType::TranslationCompositionChanged);
    FanyImeTranslationMode::Snapshot exited;
    REQUIRE(FanyImeTranslationMode::Parse(workerPayloads.back().second, exited));
    REQUIRE(!exited.active);
    REQUIRE(exited.revision > revision);
    REQUIRE(!exited.composition.empty());
    current = false;
    translation_mode::Toggle(7, 9, true);
    REQUIRE(!translation_mode::Active()); // An old toolbar task cannot resurrect a focus session.
}

TEST_CASE(translation_controller_toggle_and_toolbar_never_wait_for_slow_cold_inference)
{
    // The fake cold engine is deliberately blocked, not merely a fast missing-model path.
    // Session is destroyed before the latch, so even an assertion failure can safely join.
    std::atomic<bool> started{false};
    Session session;
    translation_mode_test_inference = [&](const translation::Request &request,
                                          const translation::Worker::Cancelled &cancelled) {
        started.store(true);
        while (!cancelled())
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return translation::Result{request.generation, translation::Status::Missing, {}};
    };
    session.Spell("jintiandehuiyi");
    session.Key('T', FanyImeKeyChord::Control);
    std::this_thread::sleep_for(std::chrono::milliseconds(110));
    translation_mode::Poll();
    for (int n = 0; n < 100 && !started.load(); ++n)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    REQUIRE(started.load());
    const auto start = std::chrono::steady_clock::now();
    session.Key('T', FanyImeKeyChord::Control);
    REQUIRE(!translation_mode::Active());
    translation_mode::Toggle(7, 9, true); // Idle floating-toolbar entry uses the same owner.
    REQUIRE(translation_mode::Active());
    translation_mode::Toggle(7, 9, true);
    REQUIRE(!translation_mode::Active());
    session.Spell("jintiandehuiyi");
    session.Key('T', FanyImeKeyChord::Control);
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(250));
    REQUIRE(translation_mode::Active());
}

TEST_CASE(translation_controller_keeps_selected_chinese_out_of_composition_and_uses_local_fallback)
{
    Session session;
    session.Spell("nihao");
    REQUIRE(!g_inputSession->get_candidates().empty());
    const auto expected = string_to_wstring(g_inputSession->get_candidates().front().word);
    translation_mode::Toggle(7, 9);
    const auto draft = translation_mode::Snapshot()->draft;
    REQUIRE_EQ(draft, expected); // The completed selection must not append raw pinyin a second time.
    REQUIRE(translation_mode::Active());
    REQUIRE(!draft.empty());
    REQUIRE(g_inputSession->get_pinyin_sequence().empty());
    REQUIRE(Global::candidate_ui.selected_text.find(draft) == std::wstring::npos);
    for (int n = 0; n < 100; ++n)
    {
        translation_mode::Poll();
        if (translation_mode::Snapshot()->hint.find(L"逐词释义") != std::wstring::npos)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto fallback = translation_mode::Snapshot();
    REQUIRE(fallback->hint.find(L"逐词释义") != std::wstring::npos);
    const auto english = fallback->english;
    session.Key(VK_RETURN);
    REQUIRE_EQ(replyType, FanyImeReplyType::TranslationCommitEnd);
    REQUIRE_EQ(committed, english);
    REQUIRE(translation_mode::Active());
    REQUIRE(translation_mode::Snapshot()->draft.empty());
    session.Key(VK_ESCAPE);
    REQUIRE(!translation_mode::Active());
}
TEST_CASE(translation_controller_does_not_enter_for_an_older_tsf_client)
{
    Session session;
    supports = false;
    session.Spell("nihao");
    translation_mode::Toggle(7, 9, true);
    REQUIRE(!translation_mode::Active());
    REQUIRE_EQ(g_inputSession->get_pinyin_sequence(), "nihao");
}

TEST_CASE(translation_controller_enter_awaits_newest_draft_in_background)
{
    Session session;
    session.Spell("nihao");
    translation_mode::Toggle(7, 9);
    committed.clear();
    session.Key(VK_RETURN);
    REQUIRE(committed.empty()); // No premature Chinese/stale translation on Enter during debounce.
    for (int n = 0; n < 200 && committed.empty(); ++n)
    {
        translation_mode::Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(!committed.empty());
    REQUIRE(translation_mode::Active());
    REQUIRE(translation_mode::Snapshot()->draft.empty());
}

TEST_CASE(translation_controller_backspace_escape_punctuation_and_toggle_discard)
{
    Session session;
    translation_mode::Toggle(7, 9, true);
    session.Spell("nihao");
    session.Key(VK_SPACE);
    const auto first = translation_mode::Snapshot()->draft;
    REQUIRE(!first.empty());
    session.Spell("zhongwen");
    session.Key(VK_BACK);
    REQUIRE_EQ(g_inputSession->get_pinyin_sequence_with_cases(), "zhongwe");
    REQUIRE_EQ(translation_mode::Snapshot()->draft, first);
    session.Key(VK_ESCAPE);
    REQUIRE(translation_mode::Active());
    REQUIRE(translation_mode::Snapshot()->draft.empty());
    session.Key('0');
    REQUIRE_EQ(translation_mode::Snapshot()->draft, L"0");
    session.Key(VK_BACK);
    REQUIRE(translation_mode::Snapshot()->draft.empty());
    Global::Keycode = VK_OEM_PERIOD;
    Global::ModifiersDown = 0;
    Global::Wch = L'.';
    REQUIRE(translation_mode::HandleKey(7, 9, 3));
    REQUIRE_EQ(translation_mode::Snapshot()->draft, L"。");
    session.Spell("nihao");
    translation_mode::Toggle(7, 9);
    REQUIRE(!translation_mode::Active());
    REQUIRE(!translation_mode::Snapshot()->active);
    REQUIRE(g_inputSession->get_pinyin_sequence().empty());
}

TEST_CASE(translation_controller_shift_enter_ctrl_enter_and_focus_invalidation)
{
    Session session;
    session.Spell("zhongwen");
    translation_mode::Toggle(7, 9);
    const auto draft = translation_mode::Snapshot()->draft;
    REQUIRE(!draft.empty());
    session.Key(VK_RETURN, 1);
    REQUIRE_EQ(committed, draft);
    REQUIRE(translation_mode::Active());
    session.Key(VK_RETURN, 2);
    REQUIRE(!translation_mode::Active());
    REQUIRE(!translation_mode::Snapshot()->active);
    translation_mode::Toggle(7, 9, true);
    REQUIRE(translation_mode::Active());
    current = false;
    translation_mode::Poll();
    REQUIRE(!translation_mode::Active());
    REQUIRE(translation_mode::Snapshot()->draft.empty());
}
