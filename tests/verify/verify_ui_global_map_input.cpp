// Checks for the global map's input side, close and message-result slots
// (src/game/ui_global_map_input.cpp).
//
// The right-button drag handlers only change a few fields, the mouse-drag motion and the cursor's
// held flag (and put the cursor back with SetCursorPos, harmless twice): side by side (original,
// record, restore, ours, compare). The key handler opens the escape menu or saves, Close deletes
// the weather and backdrop, OnMessageResult sends a mouse move: ours only. Scenario: ui_camp (the
// map's right-button drag, Esc opening the escape menu over it (OnKeyDown, OnDeactivate) and Esc
// closing it again (OnMessageResult)); Close is not known to be reached (no scenario leaves the map).
#include "verify.h"

#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_cursor.h"

#include <cstring>

namespace {
void* s_unused;
void* s_deactivate;
void* s_rightDown;
void* s_rightUp;

struct DragState {
    std::uint32_t dragging;
    std::uint32_t turning;
    std::uint32_t selectPending;
    POINT start;
    std::uint32_t angleBits;
    int motionX;
    int motionY;
    std::uint32_t held;

    static DragState Take(const CGlobalMap* map) {
        DragState s{};
        s.dragging = map->dragging_ ? 1u : 0u;
        s.turning = static_cast<std::uint32_t>(map->turning_);
        s.selectPending = static_cast<std::uint32_t>(map->selectPending_);
        s.start = map->dragStart_;
        std::memcpy(&s.angleBits, &map->dragAngle_, 4);
        s.motionX = MouseDragX();
        s.motionY = MouseDragY();
        s.held = reinterpret_cast<const std::uint8_t*>(GameCursor())[0x90];
        return s;
    }
    void Put(CGlobalMap* map) const {
        map->dragging_ = dragging != 0;
        map->turning_ = static_cast<int>(turning);
        map->selectPending_ = static_cast<int>(selectPending);
        map->dragStart_ = start;
        std::memcpy(&map->dragAngle_, &angleBits, 4);
        MouseDragX() = motionX;
        MouseDragY() = motionY;
        reinterpret_cast<std::uint8_t*>(GameCursor())[0x90] = static_cast<std::uint8_t>(held);
    }
};

template <typename Original, typename Ours>
void SideBySide(const char* name, CGlobalMap* map, Original original, Ours ours) {
    DragState start = DragState::Take(map);
    original();
    DragState expected = DragState::Take(map);
    start.Put(map);
    ours();
    ei::verify::Check(name, expected, DragState::Take(map), "dragging %u, start (%d,%d)", expected.dragging,
                      expected.start.x, expected.start.y);
}

using MouseFn = void(__fastcall*)(void*, void*, int, int, int);
} // namespace

static void __fastcall DeactivateVerify(void* input) {
    auto* map = WindowFromInput<CGlobalMap>(input);
    SideBySide(
        "CGlobalMap::OnDeactivate", map, [&] { reinterpret_cast<void(__fastcall*)(void*)>(s_deactivate)(input); },
        [&] { map->OnDeactivate(); });
}
// 0062FF10: XOR EAX,EAX; PUSH ESI; MOV byte ptr [ECX+0x1D0],AL (2 + 1 + 6 bytes).
EI_VERIFY(0x0062FF10, DeactivateVerify, 9, s_deactivate);

static void __fastcall RightDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* map = WindowFromInput<CGlobalMap>(input);
    SideBySide(
        "CGlobalMap::OnRightDown", map,
        [&] { reinterpret_cast<MouseFn>(s_rightDown)(input, nullptr, buttons, x, y); },
        [&] { map->OnRightDown(buttons, x, y); });
}
// 0062FF60: MOV EDX,[ESP+0xC]; PUSH ESI (4 + 1 bytes).
EI_VERIFY(0x0062FF60, RightDownVerify, 5, s_rightDown);

static void __fastcall RightUpVerify(void* input, void*, int buttons, int x, int y) {
    auto* map = WindowFromInput<CGlobalMap>(input);
    SideBySide(
        "CGlobalMap::OnRightUp", map, [&] { reinterpret_cast<MouseFn>(s_rightUp)(input, nullptr, buttons, x, y); },
        [&] { map->OnRightUp(buttons, x, y); });
}
// 0062FFC0: XOR EAX,EAX; PUSH ESI; MOV byte ptr [ECX+0x1D0],AL (2 + 1 + 6 bytes).
EI_VERIFY(0x0062FFC0, RightUpVerify, 9, s_rightUp);

static void __fastcall KeyDownVerify(void* input, void*, int key, int repeat, int flags) {
    auto* map = WindowFromInput<CGlobalMap>(input);
    CInterface3D* ui = map->interface_;
    map->OnKeyDown(key, repeat, flags);
    if (key == VK_ESCAPE) {
        CUiWindow* top = ui->GetActiveWindow();
        bool ok = map->inputHandled_ && top != nullptr && top->vtable_ == reinterpret_cast<void**>(0x007412A4);
        ei::verify::Check("CGlobalMap::OnKeyDown", true, ok, "Esc: the escape menu on top");
    } else {
        ei::verify::Check("CGlobalMap::OnKeyDown", true, true, "");
    }
}
EI_VERIFY(0x00630580, KeyDownVerify, 0, s_unused);

static void __fastcall MessageResultVerify(CGlobalMap* self, void*, int result) {
    self->OnMessageResult(result);
    // (The mouse move it sends may refresh the selection itself, so selectPending_ is not checked.)
    bool ok = self->turning_ == 0 && !self->dragging_;
    ei::verify::Check("CGlobalMap::OnMessageResult", true, ok, "result %X", result);
}
EI_VERIFY(0x0062EE30, MessageResultVerify, 0, s_unused);

static void __fastcall CloseVerify(CGlobalMap* self, void*) {
    self->Close();
    ei::verify::Check("CGlobalMap::Close", true, self->weather_ == nullptr && self->backdrop_ == nullptr, "");
}
EI_VERIFY(0x0062EDD0, CloseVerify, 0, s_unused);
