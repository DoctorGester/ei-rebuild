#pragma once

// Interface screen 7 (CreateScreen's id 7): the world view with nothing drawn over it, a debug
// screen. A click on a unit adds it to a list of picked units; a click on the ground walks them
// there, with Ctrl a click on a unit attacks it; the right button forgets them. See
// docs/notes/ui.md, section 4.

#include "ui.h"

#include <cstddef>
#include <cstdint>

// The picked units' input side (vtable 00740278, 0x20 bytes; one of screen 7's extra inputs).
// The other slots are the base input's (vtable 007401D0).
struct UnitPickInput : UiInputSide {
    // 005EB8C0 (RET 4). Constructor: the interface, an empty id list. Returns this.
    UnitPickInput* Construct(CInterface3D* ui);
    // 005EB910. Destructor body: the list freed, the base input's vtable.
    void Destruct();
    // 005EB8F0 (slot 0, RET 4). The destructor, then operator delete when bit 0 of `flags` is
    // set. Returns this.
    UnitPickInput* DeleteThis(int flags);
    // 005EB940 (slot 2, RET 0xC). Without Ctrl: a unit under the point (00565D00) is added to the
    // list (its id) and controlled (00566030); else each listed unit still in the client world
    // walks to the ground point under it. With Ctrl and a unit under the point: each listed unit
    // attacks it (how 6). The event is not marked used.
    void OnLeftDown(int buttons, int x, int y);
    // 005EBB40 (slot 5, RET 0xC). The list emptied (storage freed), the world's selection cleared.
    void OnRightDown(int buttons, int x, int y);
    // 005EBB80 (slot 9, RET 0xC). Ctrl (VK_CONTROL) shows cursor 1; Space toggles the server
    // flag (00578D00 / 00578D10).
    void OnKeyDown(int key, int repeat, int flags);
    // 005EBBC0 (slot 11, RET 0xC). Ctrl released: cursor 0.
    void OnKeyUp(int key, int repeat, int flags);

    static void** Vtable() { return reinterpret_cast<void**>(std::uintptr_t(0x00740278)); }
    static void** BaseVtable() { return reinterpret_cast<void**>(std::uintptr_t(0x007401D0)); }

    UiDwordArray picked_;            // +0x0C ids of the picked units (CDWordArray, SetSize 0059C660)
};
static_assert(offsetof(UnitPickInput, picked_) == 0x0C);
static_assert(sizeof(UnitPickInput) == 0x20);

// Screen 7 (vtables 00740840 / 0074080C, 0x168 bytes: a container; CreateScreen constructs it
// inline). Its slots but 20, 25 and 26 are the container's.
class CViewScreen : public CUiContainer {
public:
    // 0062A830 (slot 25, the screen's Create). Three extra inputs: an inert one (vtable 00741154),
    // UnitPickInput, the battle camera's keys (005EACB0); the camera no longer free, its focus at
    // (30, 30) and its rotation turned by 225 degrees about x (premultiplied).
    void Build();
    // 0062AA50 (slot 20, RET 4). The client glue's per-frame step (00578910); returns 1.
    BOOL DrawView(int pass);
    // 0062AA40 (slot 26). The camera free again.
    void OnClose();

    std::uint8_t unknown_164[4];
};
static_assert(sizeof(CViewScreen) == 0x168);
