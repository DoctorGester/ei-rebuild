// Checks for the item panels' left button (005EE310, ui_item_panel_input.cpp) and the item model
// (005EBFB0, ui_item_model.cpp).
//
// 005EE310: side by side on the call it makes. The panel's slot 25 (the item or topic under the
// point; a lookup) gives the expected call; the owner gets a stand-in copy of its vtable whose
// slots 42 and 44 record the call and forward it, ours runs once, the owner's vtable is put back
// and the recorded call must be the expected one. Scenario ui_camp_deals (clicks on the item
// constructor's trade grid) and any click on a camp or hero screen panel.
// 005EBFB0: ours only (it makes objects): a counted object comes back. Every item drawn in the
// camp's strips (ui_camp, ui_describe).
#include "verify.h"

#include "../../src/game/engine_object.h"
#include "../../src/game/ui.h"
#include "../../src/game/ui_item_info.h"
#include "../../src/game/ui_screens.h"

#include "slot_recorder.h"

#include <cstdint>

namespace {
void* s_unused;
constexpr int kTakeItemSlot = 42;
constexpr int kTopicSlot = 44;
} // namespace

static void __fastcall ItemPanelLeftDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CUiItemPanel>(input);
    int topic = x;
    void* item = self->VirtualItemAt(x, y, &topic);
    ei::verify::SlotCall expected;
    if (item) {
        expected = {kTakeItemSlot, {reinterpret_cast<std::uintptr_t>(item), static_cast<std::uintptr_t>(self->mode_)}, 1};
    } else if (topic) {
        expected = {kTopicSlot, {static_cast<std::uintptr_t>(topic), static_cast<std::uintptr_t>(self->mode_)}, 1};
    }
    ei::verify::SlotRecorder recorder(self->owner_);
    recorder.Record<kTakeItemSlot, 2>();
    recorder.Record<kTopicSlot, 2>();
    self->OnLeftDown(buttons, x, y);
    const ei::verify::SlotCall& actual = ei::verify::SlotRecorder::Call();
    bool same = actual.calls == expected.calls &&
                (expected.calls == 0 || (actual.slot == expected.slot && actual.args[0] == expected.args[0] &&
                                         actual.args[1] == expected.args[1]));
    ei::verify::Check("CUiItemPanel::OnLeftDown", true, same, "(%d, %d): slot %d/%d args %x/%x %x/%x calls %d/%d", x, y,
                      expected.slot, actual.slot, expected.args[0], actual.args[0], expected.args[1], actual.args[1],
                      expected.calls, actual.calls);
}
EI_VERIFY(0x005EE310, ItemPanelLeftDownVerify, 0, s_unused);

static CEngineObject* __fastcall ItemModelVerify(const char* name) {
    CEngineObject* model = ItemModel(name);
    ei::verify::Check("ItemModel", true, model != nullptr && model->refCount_ >= 1, "%s: %p refs %d", name ? name : "(null)",
                      model, model ? model->refCount_ : 0);
    return model;
}
EI_VERIFY(0x005EBFB0, ItemModelVerify, 0, s_unused);
