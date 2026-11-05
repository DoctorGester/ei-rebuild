#pragma once

// What the battle HUD's panel sources (ui_hud_*.cpp) share: the click sound, the sling arrow's sprite, the sprites'
// tint and whether the game is paused.

#include "ui_mesh.h"
#include "game_singletons.h"
#include "world.h"

inline constexpr const char* kSoundClick = "buttons\\battle\\click.wav";
inline constexpr const char* kArrowSprite = "in25arrow";

// A sprite's three tint floats set to `value`.
inline void SetTint(UiMesh* object, float value) {
    float* tint = object->tint;
    tint[0] = value;
    tint[1] = value;
    tint[2] = value;
}
inline constexpr float kTintOn = 1.0f;
inline constexpr float kTintOff = 0.5f;
inline constexpr float kTintDim = 0.7f;   // the movement panel's other modes

inline bool GamePaused() { return g_clientWorld.paused != 0; }
