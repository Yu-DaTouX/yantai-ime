#pragma once

#include "engine/contracts/windows_ipc.h"
#include "translation/selection_translate_state.h"

#include <cstdint>
#include <string>

// Glue between the toolbar "译" button, the TSF client that owns the selection and the local translation engine.
// All state lives in selection::Machine; this layer only performs its actions. Thread safe: the toolbar click arrives
// on the UI thread, TSF reports on the Main-pipe reader, engine results on the translation worker.
namespace translation::selection_service
{
struct ButtonState
{
    selection::ButtonView view = selection::ButtonView::Translate;
    size_t done = 0;
    size_t total = 0;
};

// Label drawn on the toolbar button: "译", "还原", or "译中" / "2/5" while translating.
std::wstring ButtonLabel(const ButtonState &state);

ButtonState CurrentButton();
void OnToolbarClick();
void OnTsfReport(uint64_t client_id, uint64_t activation_epoch, const FanyImeNamedpipeData &report);
// The active TSF client or its focus changed; a pending job or restore belongs to a window the user left.
void OnRouteChanged();
} // namespace translation::selection_service
