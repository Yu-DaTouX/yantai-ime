// Selection translation ("译" button) on the TSF side. The Server decides when to translate; this file only reads
// the host selection, replaces it, restores it, and reports back over the Main pipe (selection_translate.h).
// Every document access happens inside an edit session on the owner thread.
#include "Private.h"
#include "EditSession.h"
#include "MetasequoiaIME.h"
#include "Ipc.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{
using namespace FanyImeSelectionTranslate;

enum class SelectionRead
{
    Ok,
    None,
    TooLong,
    Unsupported,
};

// Reads the default selection's text. On Ok, *range receives a referenced range the caller must release.
SelectionRead ReadSelection(ITfContext *context, TfEditCookie ec, ITfRange **range, std::wstring &text)
{
    if (range)
        *range = nullptr;
    TF_SELECTION selection = {};
    ULONG fetched = 0;
    const HRESULT selectionHr = context->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
    if (selectionHr == TF_E_NOSELECTION || (SUCCEEDED(selectionHr) && (fetched != 1 || !selection.range)))
        return SelectionRead::None;
    if (FAILED(selectionHr))
        return SelectionRead::Unsupported;

    SelectionRead outcome = SelectionRead::Ok;
    BOOL empty = FALSE;
    if (FAILED(selection.range->IsEmpty(ec, &empty)))
    {
        outcome = SelectionRead::Unsupported;
    }
    else if (empty)
    {
        outcome = SelectionRead::None;
    }
    else
    {
        // One character more than the limit tells "exactly at the limit" from "longer than the limit".
        std::wstring buffer(kMaxSelectionChars + 1, L'\0');
        ULONG received = 0;
        if (FAILED(selection.range->GetText(ec, 0, buffer.data(), static_cast<ULONG>(buffer.size()), &received)))
        {
            outcome = SelectionRead::Unsupported;
        }
        else if (received == 0)
        {
            outcome = SelectionRead::None;
        }
        else if (received > kMaxSelectionChars)
        {
            outcome = SelectionRead::TooLong;
        }
        else
        {
            buffer.resize(received);
            text = std::move(buffer);
        }
    }
    if (outcome == SelectionRead::Ok && range)
        *range = selection.range;
    else
        selection.range->Release();
    return outcome;
}

Failure FailureFor(SelectionRead outcome)
{
    switch (outcome)
    {
    case SelectionRead::None:
        return Failure::NoSelection;
    case SelectionRead::TooLong:
        return Failure::TooLong;
    default:
        return Failure::Unsupported;
    }
}

// Replaces the range's text and leaves the new text selected, so the user sees it and "还原" can find it again.
HRESULT ReplaceAndSelect(ITfContext *context, TfEditCookie ec, ITfRange *range, const std::wstring &text)
{
    const HRESULT hr = range->SetText(ec, 0, text.c_str(), static_cast<LONG>(text.size()));
    if (FAILED(hr))
        return hr;
    TF_SELECTION selection = {};
    selection.range = range;
    selection.style.ase = TF_AE_NONE;
    selection.style.fInterimChar = FALSE;
    context->SetSelection(ec, 1, &selection); // best effort: a host that cannot select just cannot be restored
    return S_OK;
}

class CSelectionTranslateSession : public CEditSessionBase
{
  public:
    CSelectionTranslateSession(CMetasequoiaIME *pTextService, ITfContext *pContext, Command command, uint32_t job,
                               std::wstring translation)
        : CEditSessionBase(pTextService, pContext), _command(command), _job(job), _translation(std::move(translation))
    {
    }

    STDMETHODIMP DoEditSession(TfEditCookie ec) override
    {
        switch (_command)
        {
        case Command::Read:
            return Read(ec);
        case Command::Apply:
            return Apply(ec);
        case Command::Restore:
            return Restore(ec);
        }
        return S_FALSE;
    }

  private:
    HRESULT Read(TfEditCookie ec)
    {
        std::wstring text;
        const SelectionRead outcome = ReadSelection(_pContext, ec, nullptr, text);
        if (outcome != SelectionRead::Ok)
        {
            _pTextService->_ReportSelectionTranslate(ReportKind::Failure, _job, FailureFor(outcome));
            return S_OK;
        }
        _pTextService->_selectionTranslate.readJob = _job;
        _pTextService->_selectionTranslate.readOriginal = text;
        const std::vector<std::wstring> chunks = SplitChunks(text, kUpChunkChars);
        for (size_t i = 0; i < chunks.size(); ++i)
        {
            _pTextService->_ReportSelectionTranslate(ReportKind::ReadChunk, _job, Failure::None,
                                                     static_cast<uint32_t>(i), static_cast<uint32_t>(chunks.size()),
                                                     &chunks[i]);
        }
        return S_OK;
    }

    HRESULT Apply(TfEditCookie ec)
    {
        auto &state = _pTextService->_selectionTranslate;
        std::wstring current;
        ITfRange *range = nullptr;
        // The user may have moved or edited the selection while the translation ran; only replace the same text.
        if (state.readJob != _job || ReadSelection(_pContext, ec, &range, current) != SelectionRead::Ok ||
            current != state.readOriginal)
        {
            if (range)
                range->Release();
            _pTextService->_ReportSelectionTranslate(ReportKind::Failure, _job, Failure::SelectionChanged);
            return S_OK;
        }
        const HRESULT hr = ReplaceAndSelect(_pContext, ec, range, _translation);
        if (FAILED(hr))
        {
            range->Release();
            _pTextService->_ReportSelectionTranslate(ReportKind::Failure, _job, Failure::HostRejected);
            return S_OK;
        }
        range->Release();

        // What the host now holds may differ slightly from what was written (line-ending normalization), and
        // restoring compares against the real text.
        std::wstring stored;
        const bool selected = ReadSelection(_pContext, ec, nullptr, stored) == SelectionRead::Ok;
        _pTextService->_selectionTranslate.restoreJob = selected ? _job : 0;
        _pTextService->_selectionTranslate.original = state.readOriginal;
        _pTextService->_selectionTranslate.translated = selected ? stored : std::wstring();
        _pTextService->_selectionTranslate.restoreContext = _pContext;
        _pTextService->_ReportSelectionTranslate(ReportKind::Applied, _job);
        if (!selected)
            _pTextService->_ReportSelectionTranslate(ReportKind::Invalidated, _job);
        return S_OK;
    }

    HRESULT Restore(TfEditCookie ec)
    {
        auto &state = _pTextService->_selectionTranslate;
        const bool known = state.restoreJob == _job && state.restoreJob != 0 &&
                           CMetasequoiaIME::_IsSameComObject(_pContext, state.restoreContext);
        const std::wstring original = state.original;
        const std::wstring translated = state.translated;
        // Restoring is one-shot whatever happens: a failed attempt must not stay armed.
        _pTextService->_SelectionTranslateForget();

        std::wstring current;
        ITfRange *range = nullptr;
        if (!known || ReadSelection(_pContext, ec, &range, current) != SelectionRead::Ok || current != translated)
        {
            if (range)
                range->Release();
            _pTextService->_ReportSelectionTranslate(ReportKind::Failure, _job, Failure::SelectionChanged);
            return S_OK;
        }
        const HRESULT hr = ReplaceAndSelect(_pContext, ec, range, original);
        range->Release();
        _pTextService->_ReportSelectionTranslate(hr == S_OK ? ReportKind::Restored : ReportKind::Failure, _job,
                                                 hr == S_OK ? Failure::None : Failure::HostRejected);
        return S_OK;
    }

    Command _command;
    uint32_t _job;
    std::wstring _translation;
};

} // namespace

void CMetasequoiaIME::_ReportSelectionTranslate(ReportKind kind, uint32_t job, Failure failure, uint32_t seq,
                                                uint32_t total, const std::wstring *chunk)
{
    const size_t length = chunk ? (std::min)(chunk->size(), kUpChunkChars) : 0;
    SendSelectionTranslateReportViaNamedPipe(static_cast<uint32_t>(kind), job, static_cast<uint32_t>(failure), seq,
                                             total, chunk ? chunk->c_str() : nullptr, length);
}

void CMetasequoiaIME::_SelectionTranslateForget()
{
    _selectionTranslate.restoreJob = 0;
    _selectionTranslate.original.clear();
    _selectionTranslate.translated.clear();
    _selectionTranslate.restoreContext = nullptr;
}

// A Server command, already checked against the focus session. Runs on the owner thread.
void CMetasequoiaIME::_HandleSelectionTranslate(ITfContext *context, const DownFrame &frame)
{
    auto &state = _selectionTranslate;
    switch (frame.command)
    {
    case Command::Read:
        state.assembler.Reset();
        state.readJob = 0;
        state.readOriginal.clear();
        if (_IsComposing())
        {
            _ReportSelectionTranslate(ReportKind::Failure, frame.job, Failure::Composing);
            return;
        }
        break;
    case Command::Apply: {
        if (frame.job != state.readJob)
            return; // a stale command from a job the Server has already moved past
        const auto added = state.assembler.Add(frame.job, frame.seq, frame.total, frame.text, kMaxTranslationChars);
        if (added == ChunkAssembler::Result::Invalid)
        {
            _ReportSelectionTranslate(ReportKind::Failure, frame.job, Failure::HostRejected);
            return;
        }
        if (added == ChunkAssembler::Result::Incomplete)
            return;
        break;
    }
    case Command::Restore:
        break;
    }

    std::wstring translation = frame.command == Command::Apply ? state.assembler.Take() : std::wstring();
    auto *session =
        new (std::nothrow) CSelectionTranslateSession(this, context, frame.command, frame.job, std::move(translation));
    if (!session)
    {
        _ReportSelectionTranslate(ReportKind::Failure, frame.job, Failure::Unsupported);
        return;
    }
    const DWORD flags = (frame.command == Command::Read ? TF_ES_READ : TF_ES_READWRITE) | TF_ES_ASYNCDONTCARE;
    HRESULT sessionHr = E_FAIL;
    const HRESULT requestHr = context->RequestEditSession(_tfClientId, session, flags, &sessionHr);
    session->Release();
    if (FAILED(requestHr))
        _ReportSelectionTranslate(ReportKind::Failure, frame.job, Failure::Unsupported);
}

// OnEndEdit: the selection moved. If it no longer holds the translation, "还原" can no longer be honoured.
void CMetasequoiaIME::_SelectionTranslateOnSelectionChanged(ITfContext *context, TfEditCookie ec)
{
    auto &state = _selectionTranslate;
    if (state.restoreJob == 0)
        return;
    std::wstring current;
    const bool stillTranslation = context && _IsSameComObject(context, state.restoreContext) &&
                                  ReadSelection(context, ec, nullptr, current) == SelectionRead::Ok &&
                                  current == state.translated;
    if (stillTranslation)
        return;
    const uint32_t job = state.restoreJob;
    _SelectionTranslateForget();
    _ReportSelectionTranslate(ReportKind::Invalidated, job);
}
