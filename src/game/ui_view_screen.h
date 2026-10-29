#pragma once

// Interface screen 7 (CreateScreen's id 7): the world view with nothing drawn over it, a debug
// screen. A click on a unit adds it to a list of picked units; a click on the ground walks them
// there, with Ctrl a click on a unit attacks it; the right button forgets them. See
// docs/notes/ui.md, section 4.

#include "ui.h"

#include <cstddef>
#include <cstdint>

// The picked units' input side (one of screen 7's extra inputs). The other virtuals are the base
// input side's.
struct UnitPickInput final : UiInputSide {
    using UiInputSide::UiInputSide;

    // Without Ctrl: a unit under the point is added to the
    // list (its id) and controlled; else each listed unit still in the client world
    // walks to the ground point under it. With Ctrl and a unit under the point: each listed unit
    // attacks it (how 6). The event is not marked used.
    void LeftDown(int buttons, int x, int y) override;
    // The list emptied (storage freed), the world's selection cleared.
    void RightDown(int buttons, int x, int y) override;
    // Ctrl (VK_CONTROL) shows cursor 1; Space toggles the server
    // flag.
    void KeyDown(int key, int repeat, int flags) override;
    // Ctrl released: cursor 0.
    void KeyUp(int key, int repeat, int flags) override;

    std::vector<std::uint32_t> picked_;            // ids of the picked units
};

// Screen 7 (0x168 bytes: a container; CreateScreen constructs it inline). Its virtuals but draw, create and
// close are the container's.
class CViewScreen : public CUiContainer {
public:
    explicit CViewScreen(CInterface3D* ui) : CUiContainer(ui) {}
    // The screen's Create. Three extra inputs: an inert one, UnitPickInput, the battle camera's keys; the camera
    // no longer free, its focus at (30, 30) and its rotation turned by 225 degrees about x (premultiplied).
    void CreateScreen() override;
    // The client glue's per-frame step.
    void Draw(CSceneRenderer*) override;
    // The camera free again.
    void CloseScreen() override;

};
