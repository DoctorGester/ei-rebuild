#pragma once

// Dear ImGui (third_party/imgui, its docking branch) for the effect viewer's exe (ei-effects): one context with docking,
// its input from the platform layer's events and its draw lists handed to the renderer's overlay
// (render::OverlayTriangles, with imgui's textures as overlay textures). The windows are drawn between BeginFrame and
// EndFrame, in frame pixels. See docs/notes/effects.md, "The effect viewer".

#include <string>

namespace platform {
struct Event;
}

namespace tools::imgui_layer {

// The context, its style and its font, docking on; the windows' layout read from and saved to `layoutFile` (imgui's
// ini format; no log is written). Once, before the first event.
void Start(const std::string& layoutFile);
// The layout saved now (imgui saves it a few seconds after each change, too).
void SaveLayout();
// `event` given to imgui; true when imgui wants it (the pointer over a panel, a text field typed into), so the
// scene (its camera) should not see it. A release and a focus change also reach the scene.
bool Feed(const platform::Event& event);
// A new imgui frame at the frame's size; false (no frame) without a frame to draw on.
bool BeginFrame();
// The frame's draw lists to the renderer.
void EndFrame();

} // namespace tools::imgui_layer
