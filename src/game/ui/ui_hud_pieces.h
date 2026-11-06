#pragma once

// What the battle HUD's panel sources (ui_hud_*.cpp) share: the click sound, the sling arrow's sprite, the sprites'
// tint and whether the game is paused.

#include "game/ui/ui_mesh.h"
#include "game/app/singletons.h"
#include "game/world/world.h"

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

// A HUD piece's mouse events by kind: `press` takes LeftDown and LeftDouble, `release` LeftUp, `move` MouseMove and
// `wheel` Wheel; any other kind does nothing.
template <class Press, class Release, class Move>
void DispatchMouse(const UiEvent& event, Press press, Release release, Move move) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: press(); break;
    case UiEventKind::LeftUp: release(); break;
    case UiEventKind::MouseMove: move(); break;
    default: break;
    }
}
template <class Press, class Release, class Move, class Wheel>
void DispatchMouse(const UiEvent& event, Press press, Release release, Move move, Wheel wheel) {
    if (event.kind == UiEventKind::Wheel) wheel();
    else DispatchMouse(event, press, release, move);
}

// A click on the sling arrow slides its panel in or out; true when `region` is the arrow.
template <class Slide>
bool ToggleSling(const UiRegion* region, const UiRegion* slingButton, Slide& slide) {
    if (region != slingButton) return false;
    slide.Toggle();
    return true;
}

inline bool GamePaused() { return g_clientWorld.paused != 0; }
