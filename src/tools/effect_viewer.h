#pragma once

// The effect viewer (ei-rebuild --effects, straight in a new game: app::g_toolScene; docs/modding.md, "The effect
// viewer"): a panel over the running game that
// plays any of the original particle types (from their code or their data file) or a mod's data effect on a unit or a
// free point of the world, drives its inputs, and edits a data effect's file live. Textures and figures are the
// install's, as the game loads them; nothing is written but the file the player saves.

#include <string>
#include <string_view>

namespace tools::effect_viewer {

// The viewer joins the main loop (app/tool_hooks.h) and shows from the next frame, `effect` picked when not empty (an
// original type's name or a data effect's).
void Open(std::string_view effect);
// One line of the harness's step "effectviewer" (tests/verify/harness.cpp): an error, empty when done.
std::string Command(std::string_view line);

} // namespace tools::effect_viewer
