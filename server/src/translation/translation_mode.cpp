#include "translation/translation_mode.h"
#include "translation/translation_worker.h"
#include "settings/translation_model.h"
#include "ipc/event_listener_internal.h"
#include "global/globals.h"
#include "defines/globals.h"
#include "defines/defines.h"
#include "utils/common_utils.h"
#include "config/ime_config.h"
#include "engine/contracts/key_chord.h"
#include "engine/english/english_dictionary.h"
#include <atomic>
#include <mutex>
#include <cwctype>

using namespace event_listener_detail;
namespace translation_mode
{
namespace
{
translation::State state;
std::atomic<bool> active{false};
std::shared_ptr<const View> view = std::make_shared<View>();
std::unique_ptr<translation::Worker> worker;
std::mutex results_mutex;
std::optional<translation::Result> result;
uint64_t owner = 0, epoch = 0;
bool preserving_commit = false;
struct PendingCommit
{
    uint64_t request;
    translation::CommitKind kind;
};
std::optional<PendingCommit> pending_commit;
std::unique_ptr<EnglishDictionary> dictionary;

void Notify()
{
    active.store(state.active());
    if (owner && IsPipeActivationCurrent(owner, epoch))
        SendToTsfWorkerThreadClientViaNamedpipe(owner, epoch, FanyImeWorkerReplyType::TranslationModeChanged,
                                                state.active() ? L"1" : L"0");
    if (::global_hwnd_ftb)
        PostMessage(::global_hwnd_ftb, UPDATE_FTB_TRANSLATION_MODE, 0, 0);
}
void Publish()
{
    auto next = std::make_shared<View>();
    next->active = state.active();
    next->draft = string_to_wstring(state.Chinese());
    if (!state.words().empty())
        next->latest = string_to_wstring(state.words().back().chinese);
    next->english = string_to_wstring(state.english());
    switch (state.status())
    {
    case translation::Status::Debouncing:
    case translation::Status::Translating:
        next->english = L"翻译中…";
        break;
    case translation::Status::Timeout:
        next->english = L"翻译超时 · Enter 上屏中文";
        break;
    case translation::Status::Error:
        next->english = L"翻译失败 · Enter 上屏中文";
        break;
    case translation::Status::Missing:
        next->hint = L"逐词释义 · 请到设置下载整句翻译模型";
        break;
    default:
        break;
    }
    if (next->english.empty())
        next->english = translation_model::ResolveDirectory().empty()
                            ? L"需要下载翻译模型（约 81 MB）· 点击英文行打开设置"
                            : L"英文会显示在这里";
    if (next->hint.empty())
        next->hint = L"Enter 上屏 · Shift+Enter 中文 · Ctrl+Enter 上屏并退出";
    std::atomic_store(&view, std::shared_ptr<const View>(next));
    if (state.active())
        RefreshCandidatePageUi(true);
}
void SyncRaw()
{
    GlobalIme::composition.clear();
    GlobalIme::composition.raw_input_with_cases = g_inputSession->get_pinyin_sequence_with_cases();
    GlobalIme::composition.caret_position = GlobalIme::composition.raw_input_with_cases.size();
    if (!GlobalIme::composition.raw_input_with_cases.empty())
        FanyNamedPipe::PrepareCandidateList(owner, epoch);
    else
    {
        Global::candidate_ui.set_items({});
        Global::CandidateString.clear();
    }
}
void Append(std::string word)
{
    if (!dictionary)
        dictionary = std::make_unique<EnglishDictionary>(
            metasequoia::path_to_utf8(metasequoia::data_directory() / "english.db"), false);
    const auto gloss = dictionary->query_english_gloss(word);
    if (worker)
        worker->Cancel();
    state.Append(std::move(word), gloss, translation::Clock::now());
}
bool AcceptHighlighted()
{
    if (g_inputSession->get_pinyin_sequence().empty())
        return false;
    EnsureCandidatePageReady();
    WordItem item;
    if (!FanyNamedPipe::ResolveCandidateItem(Global::candidate_ui.selected_index_in_page + 1, item))
        return false;
    return Select(item, owner, epoch);
}
std::wstring CompositionPayload()
{
    const auto raw = g_inputSession->get_pinyin_sequence_with_cases();
    const auto caret = std::to_wstring(GlobalIme::composition.caret_position);
    auto payload = BuildCreateWordPipePayload(raw, "") + L"\t" + caret;
    if (payload.size() >= FanyImePipeLimits::CandidateTextCapacity)
        payload = string_to_wstring(raw) + L"\t\t\t" + caret;
    return payload; // Omit redundant display text when necessary; TSF renders its local spelling.
}

void CompleteCommit(uint64_t request, translation::CommitKind kind)
{
    auto commit = state.TakeCommit(kind);
    pending_commit.reset();
    if (worker)
        worker->Cancel();
    Notify();
    g_inputSession->reset_state();
    SyncRaw();
    Global::MsgTypeToTsf = FanyImeReplyType::TranslationCommitEnd;
    Global::candidate_ui.selected_text = string_to_wstring(commit.text);
    preserving_commit = true;
    FanyNamedPipe::SendCurrentDataToClient(owner, epoch, request);
    preserving_commit = false;
    if (commit.chinese_fallback)
        PostCaretStateBadge({L"译：已上屏中文"}, Global::Point[0], Global::Point[1]);
    if (state.active())
    {
        PublishCandidateUiOwner(owner, epoch);
        Publish();
    }
    else
        Reset();
}
void AcceptRemainder()
{
    for (size_t n = 0; n < 256 && !g_inputSession->get_pinyin_sequence().empty(); ++n)
    {
        const auto before = g_inputSession->get_pinyin_sequence_with_cases();
        if (!AcceptHighlighted() || g_inputSession->get_pinyin_sequence_with_cases() == before)
        {
            Append(before); // No candidate: preserve the user's raw spelling, never silently drop it.
            g_inputSession->reset_state();
            SyncRaw();
            break;
        }
    }
}
} // namespace

std::shared_ptr<const View> Snapshot()
{
    return std::atomic_load(&view);
}
bool Active()
{
    return active.load();
}
bool PreservingCommit()
{
    return preserving_commit;
}

void Reset()
{
    if (!state.active() && !Snapshot()->active)
        return;
    if (pending_commit && IsPipeActivationCurrent(owner, epoch))
        SendToTsfClientViaNamedpipe(owner, epoch, FanyImeReplyType::NavigationIgnored, pending_commit->request, L"");
    state.Exit();
    pending_commit.reset();
    if (worker)
        worker->Cancel();
    Notify();
    std::atomic_store(&view, std::shared_ptr<const View>(std::make_shared<View>()));
    if (::global_hwnd_caret_state)
        PostMessage(::global_hwnd_caret_state, WM_HIDE_CARET_STATE, 0, 0);
    owner = epoch = 0;
}
void Shutdown()
{
    Reset();
    worker.reset();
    dictionary.reset();
}

void Toggle(uint64_t client, uint64_t activation, bool toolbar)
{
    if (state.active())
    {
        Reset();
        g_inputSession->reset_state();
        FanyNamedPipe::ClearState();
        if (toolbar)
            SendToTsfWorkerThreadClientViaNamedpipe(client, activation,
                                                    FanyImeWorkerReplyType::TranslationCompositionChanged, L"\t\t\t0");
        return;
    }
    if (!ClientNegotiatedTranslationMode(client) || g_authoritative_cn_mode == 0)
        return;
    if (!g_inputSession || g_inputSession->current_scheme_type() != SchemeType::Quanpin || g_english_input_mode)
        return;
    owner = client;
    epoch = activation;
    state.Enter();
    if (!worker)
        worker = std::make_unique<translation::Worker>(translation::LocalInference(translation_model::Directory()),
                                                       [](translation::Result value) {
                                                           std::lock_guard lock(results_mutex);
                                                           result = std::move(value);
                                                       });
    Notify();
    const bool had_raw = !g_inputSession->get_pinyin_sequence().empty();
    if (toolbar && !had_raw)
    {
        Global::Point[0] = 0;
        Global::Point[1] = Global::INVALID_Y;
    }
    if (GlobalIme::composition.creating_word.active && !GlobalIme::composition.creating_word.prefix_text.empty())
    {
        Append(GlobalIme::composition.creating_word.prefix_text);
        GlobalIme::composition.clear_creating_word();
    }
    AcceptRemainder();
    if (toolbar && had_raw)
        SendToTsfWorkerThreadClientViaNamedpipe(owner, epoch, FanyImeWorkerReplyType::TranslationCompositionChanged,
                                                CompositionPayload());
    PublishCandidateUiOwner(owner, epoch);
    Publish();
    PostCaretStateBadge({L"译"}, Global::Point[0], Global::Point[1]);
}

bool Select(const WordItem &item, uint64_t client, uint64_t activation)
{
    if (!state.active())
        return false;
    if (client != owner || activation != epoch || !IsPipeActivationCurrent(owner, epoch))
    {
        Reset();
        return false;
    }
    Append(CandidateOutputText(item));
    const auto transition =
        g_inputSession->advance_composition_after_selection(item.pinyin, item.word, item.canonical_pinyin);
    if (!transition.continues_composition)
        g_inputSession->reset_state();
    SyncRaw();
    PublishCandidateUiOwner(owner, epoch);
    Publish();
    Global::MsgTypeToTsf = Global::DataFromServerMsgType::CompositionRestored;
    Global::candidate_ui.selected_text = CompositionPayload();
    return true;
}

void ReplyComposition(uint64_t client, uint64_t activation, uint64_t request)
{
    Global::MsgTypeToTsf = Global::DataFromServerMsgType::CompositionRestored;
    Global::candidate_ui.selected_text = CompositionPayload();
    FanyNamedPipe::SendCurrentDataToClient(client, activation, request);
}

bool HandleKey(uint64_t client, uint64_t activation, uint64_t request)
{
    const auto key = Global::Keycode;
    const auto modifiers = Global::ModifiersDown & 7;
    FanyImeKeyChord::Binding binding;
    const bool toggle = FanyImeKeyChord::ParseTranslationMode(GetConfiguredTranslateModeBinding(), binding) &&
                        binding.key && binding.Matches(key, modifiers);
    if (toggle && (state.active() || !g_inputSession->get_pinyin_sequence().empty()))
    {
        Toggle(client, activation);
        ReplyComposition(client, activation, request);
        return true;
    }
    if (!state.active())
        return false;
    if (client != owner || activation != epoch || !IsPipeActivationCurrent(owner, epoch))
    {
        Reset();
        return false;
    }
    if (key == VK_RETURN && (modifiers == 0 || modifiers == 1 || modifiers == 2))
    {
        AcceptRemainder();
        const auto kind = modifiers == 1   ? translation::CommitKind::Chinese
                          : modifiers == 2 ? translation::CommitKind::EnglishAndExit
                                           : translation::CommitKind::English;
        // TSF already owns this key and its cooperative commit poll allows 1500 ms.
        // Finish the current draft in the background (100 ms debounce + <= 1 s inference),
        // rather than committing stale English or prematurely falling back to Chinese.
        if (kind != translation::CommitKind::Chinese &&
            (state.status() == translation::Status::Debouncing || state.status() == translation::Status::Translating))
            pending_commit = PendingCommit{request, kind};
        else
            CompleteCommit(request, kind);
        return true;
    }
    if (key == VK_ESCAPE && modifiers == 0)
    {
        state.Escape(!g_inputSession->get_pinyin_sequence().empty());
        if (worker)
            worker->Cancel();
        g_inputSession->reset_state();
        SyncRaw();
        Notify();
        ReplyComposition(client, activation, request);
        if (state.active())
            Publish();
        else
            FanyNamedPipe::ClearState();
        return true;
    }
    if (key == VK_BACK && modifiers == 0)
    {
        if (!g_inputSession->get_pinyin_sequence().empty())
            FanyNamedPipe::EditTranslationBackspace();
        else
        {
            state.Backspace(translation::Clock::now());
            if (worker)
                worker->Cancel();
        }
        ReplyComposition(client, activation, request);
        Publish();
        return true;
    }
    // Selection is intercepted in ProcessSelectionKey before any learning code.
    if (modifiers == 0 && (key == VK_SPACE || (key >= '0' && key <= '9')))
    {
        if (!g_inputSession->get_pinyin_sequence().empty())
        {
            if (key != '0')
                FanyNamedPipe::ProcessSelectionKey(key, client, activation);
        }
        else if (key != VK_SPACE)
            Append(std::string(1, static_cast<char>(key)));
        ReplyComposition(client, activation, request);
        Publish();
        return true;
    }
    const WCHAR c = Global::Wch;
    if (!(modifiers & 6) && c >= L' ' && (c != L'\'' || g_inputSession->get_pinyin_sequence().empty()) &&
        !std::iswalnum(c) && c != L' ' && key != VK_TAB)
    {
        AcceptRemainder();
        std::wstring punctuation(1, c);
        switch (c)
        {
        case L',':
            punctuation = L"，";
            break;
        case L'.':
            punctuation = L"。";
            break;
        case L'?':
            punctuation = L"？";
            break;
        case L'!':
            punctuation = L"！";
            break;
        case L':':
            punctuation = L"：";
            break;
        case L';':
            punctuation = L"；";
            break;
        case L'(':
            punctuation = L"（";
            break;
        case L')':
            punctuation = L"）";
            break;
        default:
            break;
        }
        Append(wstring_to_string(punctuation));
        ReplyComposition(client, activation, request);
        Publish();
        return true;
    }
    return false;
}

void Poll()
{
    if (!state.active())
        return;
    if (!IsPipeActivationCurrent(owner, epoch))
    {
        Reset();
        return;
    }
    bool changed = state.Tick(translation::Clock::now());
    {
        std::lock_guard lock(results_mutex);
        if (result)
        {
            changed = state.Apply(std::move(*result), translation::Clock::now()) || changed;
            result.reset();
        }
    }
    if (auto request = state.TakeDueRequest(translation::Clock::now()))
    {
        worker->Submit(std::move(*request));
        changed = true;
    }
    if (changed)
        Publish();
    if (pending_commit && state.status() != translation::Status::Debouncing &&
        state.status() != translation::Status::Translating)
        CompleteCommit(pending_commit->request, pending_commit->kind);
}
} // namespace translation_mode
