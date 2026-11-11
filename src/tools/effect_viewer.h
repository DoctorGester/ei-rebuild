#pragma once

// The effect viewer (ei-effects; docs/modding.md, "The effect viewer"): a program of its own on the engine
// (src/engine, src/render, src/platform) that plays any of the original particle types (their built-in data files) or
// a mod's data effect on a figure or a free point of its own world (effect_viewer_scene.h), drives its inputs, and edits
// a data effect's file live. Textures, figures and mods are the install's, as the game loads them; nothing is written
// but the file the player saves.

#include <string>
#include <string_view>

namespace platform {
struct Event;
}

namespace tools::effect_viewer {

// The viewer started over the scene (its managers made), `effect` picked when not empty (an original type's name or a
// data effect's); `gameFolder` is the install's.
void Open(std::string_view effect, const std::string& gameFolder);
// The scene ticked (ViewerScene::Advance): the source's tick.
void SceneTicked();
// `event` for the viewer's windows; false when they do not take it (the camera's then).
bool TakeEvent(const platform::Event& event);
// The viewer's windows drawn over the frame (the renderer's overlay).
void DrawFrame();
// The source and its sub-effects let go (before the scene stops).
void Close();
// The folder the next save writes into.
const std::string& SaveFolder();
// One line of a scenario's step "effectviewer" (tests/verify/effects/effects_harness.cpp): an error, empty when done.
std::string Command(std::string_view line);

} // namespace tools::effect_viewer
