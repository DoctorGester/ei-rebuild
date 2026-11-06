#pragma once

// Interface screen 7 (CreateScreen's id 7): the world view with nothing drawn over it, a debug
// screen. A click on a unit adds it to a list of picked units; a click on the ground walks them
// there, with Ctrl a click on a unit attacks it; the right button forgets them. See
// docs/notes/ui.md, section 4.

#include "game/ui/ui.h"

#include <cstddef>
#include <cstdint>

// The picked units' input (one of screen 7's extra inputs).
struct UnitPickInput final : UiInput {
    using UiInput::UiInput;

    void OnEvent(UiEvent& event) override;
    // Without Ctrl: a unit under the point is added to the
    // list (its id) and controlled; else each listed unit still in the client world
    // walks to the ground point under it. With Ctrl and a unit under the point: each listed unit
    // attacks it (how 6). The event is not marked used.
    void LeftDown(UiEvent& event);
    // The list emptied (storage freed), the world's selection cleared.
    void RightDown(UiEvent& event);
    // Ctrl (platform::Key::Control) shows cursor 1; Space toggles the server
    // flag.
    void KeyDown(UiEvent& event);
    // Ctrl released: cursor 0.
    void KeyUp(UiEvent& event);

    std::vector<std::uint32_t> picked;             // ids of the picked units
};

// Screen 7 (0x168 bytes: a container; CreateScreen constructs it inline). Its virtuals but draw, create and
// close are the container's.
class ViewScreen : public UiContainer {
public:
    // The screen's Create. Two extra inputs: UnitPickInput, the battle camera's keys; the camera
    // no longer free, its focus at (30, 30) and its rotation turned by 225 degrees about x (premultiplied).
    void CreateScreen() override;
    // The client glue's per-frame step.
    void DrawSprites(SceneRenderer*) override;
    // The camera free again.
    void CloseScreen() override;

};
