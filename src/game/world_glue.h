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

class CScript;            // script.h
struct NamedFloatTable;   // object_tables.h

// A script's last error as callers take it (00456360 copies it out of the script): the text, the
// code and the line.
struct ScriptErrorInfo {
    mfc::CString text;
    int code;
    int line;
};
static_assert(sizeof(ScriptErrorInfo) == 0xC);

// 005785D0 (RET 0xC; the console's script command 005DD800). Loads `text` into the command script
// (007C29CC; CScript::Load with the WorldScript started), runs it when it loaded and `run` is set
// (ScriptTick), and copies the script's last error into `error` when given. Returns Load's result.
int __stdcall RunScriptText(mfc::CString* text, char run, ScriptErrorInfo* error);

// 00578C40 (RET 8; the quest and briefing screens). Player zero's variables whose names start with
// `prefix` copied into `out` (NamedFloatTable::CopyPrefixed): the local player's as the client sees
// them in a network game, else the server's player 0; `out` is emptied when there is no such player.
void __stdcall PlayerZeroVariables(const char* prefix, NamedFloatTable* out);

// 0055EA10 (the application's start-up 00456440). The start folder CString 007AF454 set to the
// current directory with a backslash appended.
void __cdecl StoreStartFolder();
