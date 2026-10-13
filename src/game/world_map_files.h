#pragma once

// The maps folder and its files (world_map_files.cpp, A-W14). All four are patched by Rebirth (the
// mod's own maps folder), so these install in the vanilla launch only and rewritten callers keep
// calling the original addresses. ECX holds the world data or the game flow at the call sites and
// is not read.

#include "lib/mfc.h"

class GameBuffer;   // net_coder.h

// 0055E990 (RET 4). The maps folder: the game's data folder (the CString at 007AF460) + "maps\",
// constructed into `out`. Returns `out`.
mfc::CString* __stdcall MapsFolder(mfc::CString* out);
// 00595BA0 (RET 8). The full path of the map file `name`, constructed into `out`: `name` as it is
// when its second character is ':' (a drive), else under the maps folder. Returns `out`.
mfc::CString* __stdcall MapPath(mfc::CString* out, const char* name);
// 005812F0 (RET 4; AL). Whether the maps folder has a file `name` (CFileFind::FindFile).
bool __stdcall MapFileExists(const char* name);
// 005813A0 (RET 8; the client's file chunk handler 0041ECC0). Writes what `buffer` holds (from its
// start to the highest position written, which the cursor may raise first) to the file `name` of the
// maps folder, created or cut to length. A file that does not open is written to all the same (the
// CFile's own error handling).
void __stdcall WriteMapFile(const char* name, GameBuffer* buffer);
