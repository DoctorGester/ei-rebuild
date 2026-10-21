#pragma once

// The game's three global objects and its tick counter, named once (they were per-file copies):
// the game flow (007AFE20), the interface manager (007C7D50) and the world tick (007AFFC4).

#include "game_data.h"

struct GameFlow;      // world.h
class CInterface3D;   // ui.h

// 007AFE20: the game flow object (its state machine, sessions and requests).
inline GameFlow* TheGameFlow() { return ei::GamePointer<GameFlow>(0x007AFE20); }

// 007C7D50: the interface manager (windows, the quest log, status text, the console window).
inline CInterface3D* TheInterface() { return ei::GamePointer<CInterface3D>(0x007C7D50); }

// 007AFFC4: the world tick, advanced once per simulation step.
inline int CurrentWorldTick() { return ei::GameData<const int>(0x007AFFC4); }
