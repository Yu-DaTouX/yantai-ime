#include "../date_time_input.h"
#include "../enumeration_comma.h"
#include "../ipc_negotiation.h"
#include "../uiless_page.h"
#include "../v_mode_input.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>

#define CHECK(expression)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << #expression << " at " << __LINE__ << '\n';                                                    \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (false)

int main()
{
    // Wire bytes, not just matching C++ declarations: existing v1 DLLs write
    // these offsets on both x86 and x64. Changes must not move UTF-16 data.
    std::array<unsigned char, 304> bytes{};
    bytes[0] = 10; // ClientHello
    bytes[8] = 7;  // client id
    FanyImeNamedpipeData legacy{};
    std::memcpy(&legacy, bytes.data(), bytes.size());
    CHECK(legacy.client_id == 7);
    CHECK(FanyImePipeEventType::InputStateAction == 17);
    CHECK(FanyImeProtocol::InputStateShortcuts == (1u << 7));
    CHECK(FanyImeProtocol::ReservedCapability8 == (1u << 8));
    CHECK(FanyImeWorkerReplyType::Reserved36 == 36);
    CHECK(FanyImeWorkerReplyType::UpdateInstallerReady == 40);
    CHECK(FanyImeWorkerReplyType::MaxKnown == FanyImeWorkerReplyType::SelectionTranslateCommand);
    CHECK(FanyImePipeEventType::HideCaretState == 16);
    CHECK(FanyImePipeEventType::HideCaretState != FanyImePipeEventType::HideCandidateWnd);
    CHECK(!FanyImePipeEventType::IsRouteDeactivation(FanyImePipeEventType::HideCaretState));
    CHECK(FanyImeProtocol::CaretStateIndicator == (1u << 5));
    CHECK((FanyImeProtocol::CaretStateIndicator & FanyImeProtocol::RequiredCapabilities) == 0);
    // CapsLockEdge keeps the VK_CAPITAL value the first Servers matched on.
    CHECK(FanyImeCaretStateTrigger::UserToggle == 0);
    CHECK(FanyImeCaretStateTrigger::CapsLockEdge == 0x14);
    CHECK(FanyImeCaretStateTrigger::FocusEntered != FanyImeCaretStateTrigger::UserToggle);
    CHECK(FanyImeCaretStateTrigger::FocusEntered != FanyImeCaretStateTrigger::CapsLockEdge);
    CHECK(FanyImeProtocol::Negotiate(legacy).legacy);
    CHECK(FanyImeProtocol::Negotiate(legacy).accepted);

    const auto capsOffSnapshot = FanyImePipeFlags::EncodeImeSwitchCapsLockSnapshot(false);
    const auto capsOnSnapshot = FanyImePipeFlags::EncodeImeSwitchCapsLockSnapshot(true);
    CHECK((FanyImePipeFlags::ImeSwitchCapsSnapshotPresent & FanyImePipeFlags::UiLess) == 0);
    CHECK((FanyImePipeFlags::ImeSwitchCapsSnapshotEnabled & FanyImePipeFlags::UiLess) == 0);
    CHECK((FanyImePipeFlags::ImeSwitchCapsSnapshotPresent & 0xffu) == 0);
    CHECK((FanyImePipeFlags::ImeSwitchCapsSnapshotEnabled & 0xffu) == 0);
    CHECK(FanyImePipeFlags::HasImeSwitchCapsLockSnapshot(capsOffSnapshot));
    CHECK(!FanyImePipeFlags::ImeSwitchCapsLockSnapshotEnabled(capsOffSnapshot));
    CHECK(FanyImePipeFlags::HasImeSwitchCapsLockSnapshot(capsOnSnapshot));
    CHECK(FanyImePipeFlags::ImeSwitchCapsLockSnapshotEnabled(capsOnSnapshot));
    CHECK(!FanyImePipeFlags::HasImeSwitchCapsLockSnapshot(0)); // old DLL packet
    CHECK(!FanyImePipeFlags::ImeSwitchCapsLockSnapshotEnabled(FanyImePipeFlags::ImeSwitchCapsSnapshotEnabled));
    CHECK(FanyImePipeFlags::DecodeImeSwitchCapsLockSnapshot(capsOffSnapshot).has_value());
    CHECK(!*FanyImePipeFlags::DecodeImeSwitchCapsLockSnapshot(capsOffSnapshot));
    CHECK(*FanyImePipeFlags::DecodeImeSwitchCapsLockSnapshot(capsOnSnapshot));
    CHECK(!FanyImePipeFlags::DecodeImeSwitchCapsLockSnapshot(0).has_value());
    CHECK(
        !FanyImePipeFlags::DecodeImeSwitchCapsLockSnapshot(FanyImePipeFlags::ImeSwitchCapsSnapshotEnabled).has_value());

    auto hello = FanyImeProtocol::Hello(7, 19);
    auto result = FanyImeProtocol::Negotiate(hello);
    CHECK(result.accepted && !result.legacy);
    auto reply = FanyImeProtocol::Reply(hello, result);
    CHECK(FanyImeProtocol::AcceptReply(reply, 19));
    CHECK(!FanyImeProtocol::AcceptReply(reply, 18)); // stale reconnect ACK
    CHECK(!FanyImeProtocol::AcceptReply(reply, 0));

    const auto shortcutCapabilities = FanyImeProtocol::Capabilities | FanyImeProtocol::CharacterSetShortcut;
    const auto shortcutHello = FanyImeProtocol::Hello(7, 20, shortcutCapabilities);
    const auto oldServer = FanyImeProtocol::Negotiate(shortcutHello);
    CHECK(oldServer.accepted);
    CHECK((oldServer.capabilities & FanyImeProtocol::CharacterSetShortcut) == 0);
    const auto newServer = FanyImeProtocol::Negotiate(shortcutHello, shortcutCapabilities);
    CHECK(newServer.accepted);
    CHECK((newServer.capabilities & FanyImeProtocol::CharacterSetShortcut) != 0);
    const auto shortcutReply = FanyImeProtocol::Reply(shortcutHello, newServer);
    CHECK(FanyImeProtocol::AcceptReply(shortcutReply, 20));
    CHECK(FanyImeProtocol::ReplyCapabilities(shortcutReply) == shortcutCapabilities);
    CHECK((FanyImeProtocol::Negotiate(hello, shortcutCapabilities).capabilities &
           FanyImeProtocol::CharacterSetShortcut) == 0); // old client/new server
    CHECK(FanyImeProtocol::IsCharacterSetShortcut('F', 3));
    CHECK(FanyImeProtocol::IsCharacterSetShortcut('F', 0x80000003u));
    for (unsigned modifiers = 0; modifiers < 8; ++modifiers)
        CHECK(FanyImeProtocol::IsCharacterSetShortcut('F', modifiers) == (modifiers == 3));
    CHECK(!FanyImeProtocol::IsCharacterSetShortcut('E', 3));

    // Backspace retraction is negotiated separately: an old Server must keep
    // its current behavior, and an old DLL must never receive CompositionRestored.
    const auto restoreCapabilities = FanyImeProtocol::Capabilities | FanyImeProtocol::CompositionRestore;
    const auto restoreHello = FanyImeProtocol::Hello(7, 21, restoreCapabilities);
    const auto oldServerForRestore = FanyImeProtocol::Negotiate(restoreHello);
    CHECK(oldServerForRestore.accepted);
    CHECK((oldServerForRestore.capabilities & FanyImeProtocol::CompositionRestore) == 0);
    const auto newServerForRestore = FanyImeProtocol::Negotiate(restoreHello, restoreCapabilities);
    CHECK(newServerForRestore.accepted);
    CHECK((newServerForRestore.capabilities & FanyImeProtocol::CompositionRestore) != 0);
    CHECK(FanyImeProtocol::AcceptReply(FanyImeProtocol::Reply(restoreHello, newServerForRestore), 21));
    CHECK(FanyImeProtocol::ReplyCapabilities(FanyImeProtocol::Reply(restoreHello, newServerForRestore)) ==
          restoreCapabilities);
    CHECK((FanyImeProtocol::Negotiate(hello, restoreCapabilities).capabilities & FanyImeProtocol::CompositionRestore) ==
          0); // old client/new server
    CHECK(FanyImeReplyType::CompositionRestored == 14);
    CHECK(FanyImeReplyType::Reserved15 == 15);
    CHECK(FanyImeReplyType::Reserved16 == 16);
    CHECK(FanyImeReplyType::Reserved17 == 17);
    CHECK(FanyImeReplyType::Reserved18 == 18);
    CHECK(FanyImeReplyType::MaxKnown == FanyImeReplyType::Reserved15);
    CHECK(FanyImeProtocol::ReservedCapability9 == (1u << 9));
    CHECK(FanyImeProtocol::ReservedCapability10 == (1u << 10));
    CHECK(FanyImeWorkerReplyType::Reserved43 == 43);
    CHECK(FanyImeWorkerReplyType::Reserved44 == 44);
    CHECK(FanyImeWorkerReplyType::SelectionTranslateCommand == 45);
    CHECK(FanyImePipeEventType::SelectionTranslateReport == 18);
    CHECK(FanyImeReplyType::TransportUnavailable > FanyImeReplyType::MaxKnown);

    hello.wch += 1;
    result = FanyImeProtocol::Negotiate(hello);
    CHECK(!result.accepted);
    CHECK(!FanyImeProtocol::AcceptReply(FanyImeProtocol::Reply(hello, result), 19));
    hello = FanyImeProtocol::Hello(7, 19);
    hello.point[1] |= 1u << 20; // client requires an unknown capability
    CHECK(!FanyImeProtocol::Negotiate(hello).accepted);
    hello = FanyImeProtocol::Hello(7, 19);
    hello.modifiers_down |= 1u << 20; // unknown optional capabilities are ignored
    CHECK(FanyImeProtocol::Negotiate(hello).accepted);
    hello.modifiers_down &= ~FanyImeProtocol::FocusEpochs;
    CHECK(!FanyImeProtocol::Negotiate(hello).accepted);
    hello = FanyImeProtocol::Hello(7, 19);
    hello.keycode ^= 1;
    CHECK(!FanyImeProtocol::Negotiate(hello).accepted);
    hello = FanyImeProtocol::Hello(7, 0);
    CHECK(!FanyImeProtocol::Negotiate(hello).accepted);

    FanyImeNamedpipeDataToTsf old_server{};
    old_server.msg_type = FanyImeReplyType::PipeReady;
    old_server.request_id = 19;
    CHECK(!FanyImeProtocol::AcceptReply(old_server, 19));
    // Corrupt/missing negotiated capabilities cannot authorize the new DLL.
    reply.candidate_string[2] = 0;
    CHECK(!FanyImeProtocol::AcceptReply(reply, 19));

    CHECK(FanyImeWorkerReplyType::SwitchToEn == FanyImeWorkerReplyType::SwitchToEnglish);
    CHECK(FanyImeWorkerReplyType::CommitCandidate == FanyImeWorkerReplyType::CommitCurCandidate);
    CHECK(FanyImeWorkerReplyType::CommitCandidateAndContinue == 27);
    CHECK(FanyImeWorkerReplyType::Reserved28 == 28);
    CHECK(FanyImeWorkerReplyType::Reserved29 == 29);
    CHECK(FanyImeWorkerReplyType::Reserved30 == 30);
    CHECK(FanyImeWorkerReplyType::Reserved31 == 31);
    CHECK(FanyImeWorkerReplyType::VModeChanged == 32);
    CHECK(FanyImeWorkerReplyType::Reserved33 == 33);
    // 34 was AiContextUploadChanged, retired with the AI assistant; never reused.
    CHECK(FanyImeWorkerReplyType::UiLessCandidatesChanged == 35);
    CHECK(FanyImeWorkerReplyType::Reserved36 == 36);
    CHECK(FanyImeWorkerReplyType::Reserved37 == 37);
    CHECK(FanyImeWorkerReplyType::EnumerationCommaKeysChanged == 38);
    CHECK(FanyImeWorkerReplyType::HorizontalArrowSelectionChanged == 39);
    CHECK(FanyImeWorkerReplyType::UpdateInstallerReady == 40);
    CHECK(FanyImeWorkerReplyType::Reserved41 == 41);
    CHECK(FanyImeWorkerReplyType::MaxKnown == FanyImeWorkerReplyType::SelectionTranslateCommand);
    // EnumerationCommaKeysChanged 的载荷：\ 和 / 各一位，两个都没勾按只有 \ 发。
    CHECK(FanyImeEnumerationComma::PayloadFor(true, false) == L'1' &&
          FanyImeEnumerationComma::PayloadFor(false, true) == L'2' &&
          FanyImeEnumerationComma::PayloadFor(true, true) == L'3' &&
          FanyImeEnumerationComma::PayloadFor(false, false) == L'1');
    CHECK(FanyImeEnumerationComma::KeysFromPayload(L'3') ==
              (FanyImeEnumerationComma::kBackslash | FanyImeEnumerationComma::kSlash) &&
          !FanyImeEnumerationComma::IsValidPayload(L'0') && !FanyImeEnumerationComma::IsValidPayload(L'4') &&
          FanyImeEnumerationComma::KeysFromPayload(L'x') == FanyImeEnumerationComma::kBackslash);
    {
        const auto full = FanyImeUiLessPage::Parse(FanyImeUiLessPage::Format(L"ni hao", L"你好,拟好", 1, 42));
        CHECK(full.preedit == L"ni hao" && full.page == L"你好,拟好" && full.selection == 1 && full.generation == 42);
        // A Server predating the generation field.
        CHECK(full.hasPage && !FanyImeUiLessPage::Parse(L"ni hao").hasPage);
        const auto legacy = FanyImeUiLessPage::Parse(L"\t你好,拟好\t1");
        CHECK(legacy.page == L"你好,拟好" && legacy.selection == 1 && legacy.generation == 0);
        // A DLL predating the field reads selection with _wtoi, which must stop at the third tab.
        const std::wstring formatted = FanyImeUiLessPage::Format(L"", L"你好", 3, 7);
        CHECK(std::wcstol(formatted.c_str() + formatted.find(L'\t', 1) + 1, nullptr, 10) == 3);
        CHECK(FanyImeUiLessPage::IsCurrent(0, 9) && FanyImeUiLessPage::IsCurrent(9, 9));
        CHECK(FanyImeUiLessPage::IsCurrent(10, 9) && !FanyImeUiLessPage::IsCurrent(8, 9));
    }
    // The retired sensitive-field bit (was FanyImePipeFlags::SensitiveField) stays clear of
    // Shift/Ctrl/Alt, every pipe flag and the retired language-cycle bits.
    constexpr std::uint32_t kRetiredSensitiveField = 0x02000000u;
    CHECK((kRetiredSensitiveField & 7u) == 0);
    CHECK((kRetiredSensitiveField & (FanyImePipeFlags::UiLess | FanyImePipeFlags::ImeSwitchCapsSnapshotPresent |
                                     FanyImePipeFlags::ImeSwitchCapsSnapshotEnabled | 0x1C000000u)) == 0);
    // Same for the horizontal arrow selection flag, which also stays clear of the sensitive-field bit.
    CHECK((FanyImePipeFlags::HorizontalArrowSelection & 7u) == 0);
    CHECK((FanyImePipeFlags::HorizontalArrowSelection &
           (FanyImePipeFlags::UiLess | FanyImePipeFlags::ImeSwitchCapsSnapshotPresent |
            FanyImePipeFlags::ImeSwitchCapsSnapshotEnabled | 0x1C000000u | kRetiredSensitiveField)) == 0);
    // V 模式的形状规则，TSF（WCHAR）与 Server（char）共用。
    {
        using FanyImeVModeInput::Trigger;
        const auto v_at = [](const std::wstring &text, std::size_t caret, wchar_t ch, Trigger trigger) {
            return FanyImeVModeInput::AcceptsAt(text.data(), text.size(), caret, ch, trigger);
        };
        const auto v_end = [&](const std::wstring &text, wchar_t ch, Trigger trigger) {
            return v_at(text, text.size(), ch, trigger);
        };
        CHECK(v_end(L"V", L'1', Trigger::UppercaseOnly) && v_end(L"V", L'(', Trigger::UppercaseOnly));
        CHECK(!v_end(L"V", L'+', Trigger::UppercaseOnly) && !v_end(L"V", L'.', Trigger::AnyCase));
        CHECK(v_end(L"V1", L'+', Trigger::UppercaseOnly) && v_end(L"V1+2", L'*', Trigger::AnyCase));
        CHECK(v_end(L"V12", L'.', Trigger::AnyCase) && v_end(L"V(1", L')', Trigger::AnyCase));
        CHECK(!v_end(L"v", L'1', Trigger::UppercaseOnly) && v_end(L"v", L'1', Trigger::AnyCase));
        CHECK(!v_end(L"V", L'1', Trigger::Off) && !v_end(L"vi", L'1', Trigger::AnyCase));
        CHECK(!v_end(L"V1", L'a', Trigger::AnyCase) && !v_end(L"T1", L'1', Trigger::AnyCase));
        // 光标在中间：不能把运算符插到 V 和第一个数字之间。
        CHECK(!v_at(L"V12", 1, L'+', Trigger::AnyCase) && v_at(L"V12", 1, L'3', Trigger::AnyCase) &&
              v_at(L"V12", 2, L'+', Trigger::AnyCase) && !v_at(L"V12", 0, L'3', Trigger::AnyCase));
        CHECK(!FanyImeVModeInput::IsComposition(L"V", 1, Trigger::UppercaseOnly) &&
              !FanyImeVModeInput::IsComposition(L"v", 1, Trigger::AnyCase) &&
              FanyImeVModeInput::IsComposition(L"v1", 2, Trigger::AnyCase) &&
              !FanyImeVModeInput::IsComposition(L"v1", 2, Trigger::UppercaseOnly) &&
              FanyImeVModeInput::IsComposition("V1a", 3, Trigger::UppercaseOnly) &&
              !FanyImeVModeInput::IsComposition("vip", 3, Trigger::AnyCase));
        CHECK(FanyImeVModeInput::TriggerFromPayload(L'2') == Trigger::AnyCase &&
              FanyImeVModeInput::TriggerFromPayload(L'1') == Trigger::UppercaseOnly &&
              FanyImeVModeInput::TriggerFromPayload(L'x') == Trigger::Off &&
              FanyImeVModeInput::PayloadFromTrigger(Trigger::UppercaseOnly) == L'1');
    }
    // Shift+T 指定日期时间的形状规则，TSF（WCHAR）、Server 与引擎（char）共用。
    const auto date_time_at = [](const std::wstring &text, std::size_t caret, wchar_t ch) {
        return FanyImeDateTimeInput::AcceptsAt(text.data(), text.size(), caret, ch);
    };
    const auto date_time_end = [&](const std::wstring &text, wchar_t ch) {
        return date_time_at(text, text.size(), ch);
    };
    CHECK(date_time_end(L"T", L'2') && date_time_end(L"T2024122", L'5') && date_time_end(L"T20241225", L'1'));
    CHECK(date_time_end(L"T2024122514", L':') && date_time_end(L"T2024122514:30", L':'));
    CHECK(!date_time_end(L"T2024122514:30:00", L'1') && !date_time_end(L"T20241225", L':'));
    CHECK(date_time_end(L"T2024", L'/') && date_time_end(L"T2024/1", L'2') && date_time_end(L"T2024/12", L'/'));
    CHECK(!date_time_end(L"T2024/12/25", L'1') && !date_time_end(L"T2024/12/25", L'/'));
    CHECK(date_time_end(L"T12", L'/') && date_time_end(L"T9", L':') && date_time_end(L"T9:0", L'5'));
    CHECK(!date_time_end(L"T123", L'/') && !date_time_end(L"T123", L':') && !date_time_end(L"T12/25", L':'));
    CHECK(!date_time_end(L"T", L'/') && !date_time_end(L"T", L':') && !date_time_end(L"Trq", L'1'));
    CHECK(!date_time_end(L"U12", L'3') && !date_time_end(L"T2024", L'a') && !date_time_end(L"T", L'!'));
    // 光标在中间：插入后的整串仍要能接成某种形状；光标不能停在开头的 T 前面。
    CHECK(!date_time_at(L"T2024/12", 1, L'1') && date_time_at(L"T202412", 5, L'1') && !date_time_at(L"T2", 0, L'1'));
    CHECK(FanyImeDateTimeInput::MatchComplete("20241225", 8).shape == FanyImeDateTimeInput::Shape::Date);
    CHECK(FanyImeDateTimeInput::MatchComplete("2024122514", 10).shape == FanyImeDateTimeInput::Shape::DateTime);
    CHECK(FanyImeDateTimeInput::MatchComplete("2024122514:30:05", 16).group_count == 3);
    CHECK(FanyImeDateTimeInput::MatchComplete("2024/1/5", 8).shape == FanyImeDateTimeInput::Shape::YearMonthDay);
    CHECK(FanyImeDateTimeInput::MatchComplete("2024/12", 7).shape == FanyImeDateTimeInput::Shape::YearMonth);
    CHECK(FanyImeDateTimeInput::MatchComplete("12/25", 5).shape == FanyImeDateTimeInput::Shape::MonthDay);
    CHECK(FanyImeDateTimeInput::MatchComplete("9:05", 4).shape == FanyImeDateTimeInput::Shape::Time);
    CHECK(FanyImeDateTimeInput::MatchComplete("1430", 4).shape == FanyImeDateTimeInput::Shape::None);
    CHECK(FanyImeDateTimeInput::MatchComplete("12/", 3).shape == FanyImeDateTimeInput::Shape::None);
    CHECK(FanyImeDateTimeInput::MatchComplete("9:5", 3).shape == FanyImeDateTimeInput::Shape::None);
    static_assert(FanyImeDateTimeInput::AcceptsAt("T2024", 5, 5, '/'), "the date shape must be usable at compile time");
    CHECK((FanyImeProtocol::Capabilities & FanyImeProtocol::FramedVoice) == 0);

    // Statistics frames: fixed 28-byte header plus 24-byte events; the Go stats
    // tool mirrors these numbers in internal/frames, so both sides must change
    // together.
    CHECK(FANY_IME_STATS_MAGIC == 0x54415453);
    CHECK(FANY_IME_STATS_VERSION == 1);
    CHECK(sizeof(FanyImeStatsBatchHeader) == 28);
    CHECK(sizeof(FanyImeStatsEvent) == 24);
    CHECK(offsetof(FanyImeStatsEvent, timestamp_utc_ft) == 0);
    CHECK(offsetof(FanyImeStatsEvent, cjk) == 8);
    CHECK(sizeof(FanyImeStatsBatchHeader) + 256 * sizeof(FanyImeStatsEvent) <= FANY_IME_STATS_MAX_FRAME_BYTES);
    std::cout << "Windows IPC layout, negotiation, upgrade and stats contracts passed\n";
}
