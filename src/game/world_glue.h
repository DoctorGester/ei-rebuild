#pragma once

// The game glue (00578000-00578E30): methods of two empty objects (007AFEA4 the game's, 007AFEA5
// the local player's) that the interface, the scripts and the console call instead of the worlds.
// None reads `this`, so they are plain functions here. world.h declares the ones rewritten
// earlier (GameTime, LocalPlayerData, ...). Names are provisional.

#include "lib/mfc.h"

// 00578DB0 / 00578DC0: the data folder CString 007AF460 / the base folder CString 007AF458.
const mfc::CString* GameDataPathString();
const mfc::CString* GameBasePathString();

// 0057FB90 (RET 4; the scripts' QuestComplete in a network game). With a live game server: its
// FinishMap for the map named `name` (the held map message dropped, "name_3" queued), 1 when it
// did; else 0.
int __stdcall FinishQuestMap(const char* name);
