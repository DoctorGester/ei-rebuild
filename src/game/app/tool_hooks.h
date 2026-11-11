#pragma once

// Where a tool built into the exe beside the game (src/tools: the effect viewer, ei-rebuild --effects) joins the main
// loop (app/main_loop.cpp). Empty unless the tool was started: the game then runs as without it.

namespace platform {
struct Event;
}

namespace app {

struct ToolHooks {
    // An event of the main loop before the interface gets it: true when the tool takes it (its panel under the mouse,
    // its text field typed into), and the game does not see it.
    bool (*takeEvent)(const platform::Event& event) = nullptr;
    // After each client world tick, before the camera follows it.
    void (*clientTicked)() = nullptr;
    // In a shown frame's step after the interface is drawn, before the harness's capture and the present: the tool's
    // panel (render::OverlayTriangles).
    void (*drawOverlay)() = nullptr;
    // Before the worlds are emptied (a switch of mods, the exit): the tool lets go of what it made in them.
    void (*worldsEnding)() = nullptr;
};

inline ToolHooks g_toolHooks;

} // namespace app
