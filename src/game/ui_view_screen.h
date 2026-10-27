#pragma once

// Interface screen 7 (CreateScreen's id 7): the world view with nothing drawn over it, a debug
// screen. A click on a unit adds it to a list of picked units; a click on the ground walks them
// there, with Ctrl a click on a unit attacks it; the right button forgets them. See
// docs/notes/ui.md, section 4.

#include "ui.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

// The picked units' input side (one of screen 7's extra inputs). The other virtuals are the base
// input side's.
struct UnitPickInput final : UiInputSide {
    // The interface, an empty id list.
    explicit UnitPickInput(CInterface3D* ui);
    // The deleting destructor. The list freed.
    ~UnitPickInput() override;
    int LeftDown(int b, int x, int y) override { return OnLeftDown(b, x, y), 0; }
    int RightDown(int b, int x, int y) override { return OnRightDown(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    int KeyUp(int k, int r, int f) override { return OnKeyUp(k, r, f), 0; }

    // Without Ctrl: a unit under the point is added to the
    // list (its id) and controlled; else each listed unit still in the client world
    // walks to the ground point under it. With Ctrl and a unit under the point: each listed unit
    // attacks it (how 6). The event is not marked used.
    void OnLeftDown(int buttons, int x, int y);
    // The list emptied (storage freed), the world's selection cleared.
    void OnRightDown(int buttons, int x, int y);
    // Ctrl (VK_CONTROL) shows cursor 1; Space toggles the server
    // flag.
    void OnKeyDown(int key, int repeat, int flags);
    // Ctrl released: cursor 0.
    void OnKeyUp(int key, int repeat, int flags);

    UiDwordArray picked_;            // ids of the picked units (CDWordArray)
};

// Screen 7 (0x168 bytes: a container; CreateScreen constructs it inline). Its virtuals but draw, create and
// close are the container's.
class CViewScreen : public CUiContainer {
public:
    // Built inline by CInterface3D::CreateScreen (screen 7): only the container.
    explicit CViewScreen(CInterface3D* ui) : CUiContainer(ui) {}
    // The screen's Create. Three extra inputs: an inert one, UnitPickInput, the battle camera's keys; the camera
    // no longer free, its focus at (30, 30) and its rotation turned by 225 degrees about x (premultiplied).
    void CreateScreen() override;
    // The client glue's per-frame step; returns 1.
    BOOL DrawView(int pass);
    // The camera free again.
    void OnClose();
    // The virtuals of its own (its input side is the container's).
    BOOL Draw(int pass) override { return DrawView(pass); }
    void CloseScreen() override { OnClose(); }

    std::uint8_t unused_164[4];
};
static_assert(sizeof(CViewScreen) == 0x168);
