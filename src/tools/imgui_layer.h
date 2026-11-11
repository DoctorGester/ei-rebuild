#pragma once

// Dear ImGui over the game (third_party/imgui) for the tools built into the exe: one context, its input from the
// platform layer's events and its draw lists handed to the renderer's overlay (render::OverlayTriangles, with
// imgui's textures as overlay textures). A tool's panel is drawn between BeginFrame and EndFrame, in frame pixels.
// See docs/notes/effects.md, "The effect viewer".

namespace platform {
struct Event;
}

namespace tools::imgui_layer {

// The context, its style and its font (no imgui.ini or log is written). Once, before the first event.
void Start();
// `event` given to imgui; true when imgui wants it (the pointer over a panel, a text field typed into), so the game
// should not see it. A release and a focus change also reach the game.
bool Feed(const platform::Event& event);
// A new imgui frame at the frame's size; false (no frame) without a frame to draw on.
bool BeginFrame();
// The frame's draw lists to the renderer.
void EndFrame();

} // namespace tools::imgui_layer
