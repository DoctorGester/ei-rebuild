// The global map's input side (right-button drag, keys), its close and message-result slots
// (CGlobalMap, ui_camp.h). See docs/notes/ui.md, section 5.
#include "ui_camp.h"

#include "ui_cursor.h"
#include "ui_screens.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "originals_ui.h"

namespace {

// The escape menu the map opens on Esc (0x168 bytes, built inline as the battle screen's key
// handler does).
constexpr std::size_t kEscMenuSize = 0x168;
void** EscMenuVtable() { return reinterpret_cast<void**>(0x007412A4); }
void** EscMenuInputVtable() { return reinterpret_cast<void**>(0x00741270); }
const char* QuickSaveName() { return reinterpret_cast<const char*>(0x007933AC); }   // "saveq"

constexpr int kActionQuickSave = '5';
constexpr int kActionQuickLoad = '6';
constexpr int kScanCodeMask = 0x1FF;

// The cursor stops being held and goes back to where the drag began.
void ReleaseDragCursor() {
    int x = MouseScreenX();
    int y = MouseScreenY();
    MouseDragY() = 0;
    MouseDragX() = 0;
    CursorSetHeldOriginal(GameCursor(), nullptr, 0);
    gameapi::SetCursorPos(x, y);
}

} // namespace

void CGlobalMap::OnDeactivate() {
    dragging_ = false;
    selectPending_ = 1;
    ReleaseDragCursor();
}

void CGlobalMap::OnRightDown(int, int x, int y) {
    dragStart_ = POINT{x, y};
    dragging_ = true;
    turning_ = 0;
    interface_->CallScalePointFromScreen(&dragStart_);
    dragAngle_ = angle_;
    MouseDragY() = 0;
    MouseDragX() = 0;
    CursorSetHeldOriginal(GameCursor(), nullptr, 1);
}

void CGlobalMap::OnRightUp(int, int, int) {
    dragging_ = false;
    selectPending_ = 1;
    ReleaseDragCursor();
}

void CGlobalMap::OnKeyDown(int key, int repeat, int flags) {
    InputOf(this)->BaseKeyDown(key, repeat, flags);
    if (key == VK_ESCAPE) {
        void* memory = gamecrt::operator_new(kEscMenuSize);
        CUiWindow* menu = nullptr;
        if (memory) {
            ConstructContainerAt(memory, nullptr, interface_);
            menu = static_cast<CUiWindow*>(memory);
            menu->vtable_ = EscMenuVtable();
            menu->inputVtable_ = EscMenuInputVtable();
        }
        InterfacePushWindowOriginal(interface_, nullptr, menu, 1, 1);
        inputHandled_ = true;
    }
    if (inputHandled_) return;
    int action = KeyActions()[flags & kScanCodeMask];
    if (action == kActionQuickSave)
        AutoSaveOriginal(QuickSaveName(), 0);
    else if (action == kActionQuickLoad)
        LoadSavedGameOriginal(QuickSaveName());
}

void CGlobalMap::OnMessageResult(int) {
    int x = MouseScreenX();
    int y = MouseScreenY();
    turning_ = 0;
    dragging_ = false;
    selectPending_ = 1;
    InputMouseMove(0, x, y);
}

void CGlobalMap::Close() {
    if (weather_) weather_->DeletingDestruct(1);
    CUiSurface* backdrop = backdrop_;
    weather_ = nullptr;
    if (backdrop) {
        backdrop->ReleaseContents();
        gamecrt::operator_delete(backdrop);
    }
    backdrop_ = nullptr;
    InterfaceStopSoundOriginal(interface_, nullptr, windSound_);
}
EI_REPLACE_METHOD(0x0062EDD0, CGlobalMap::Close);
EI_REPLACE_METHOD(0x0062EE30, CGlobalMap::OnMessageResult);

namespace {
void __fastcall GlobalMapDeactivate(void* input) { WindowFromInput<CGlobalMap>(input)->OnDeactivate(); }
void __fastcall GlobalMapRightDown(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CGlobalMap>(input)->OnRightDown(buttons, x, y);
}
void __fastcall GlobalMapRightUp(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CGlobalMap>(input)->OnRightUp(buttons, x, y);
}
void __fastcall GlobalMapKeyDown(void* input, void*, int key, int repeat, int flags) {
    WindowFromInput<CGlobalMap>(input)->OnKeyDown(key, repeat, flags);
}
} // namespace
EI_REPLACE(0x0062FF10, GlobalMapDeactivate);
EI_REPLACE(0x0062FF60, GlobalMapRightDown);
EI_REPLACE(0x0062FFC0, GlobalMapRightUp);
EI_REPLACE(0x00630580, GlobalMapKeyDown);
