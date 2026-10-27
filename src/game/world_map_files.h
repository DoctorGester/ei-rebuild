#pragma once

// The maps folder and its files (world_map_files.cpp). All four are patched by Rebirth (the mod's own maps
// folder), so these install in the vanilla launch only and callers keep calling the fixed addresses.

#include "ei/hook.h"
#include "lib/mfc.h"

class GameBuffer;   // net_coder.h

// The maps folder: the game's data folder (a CString) + "maps\", constructed into `out`. Returns `out`.
mfc::CString* __stdcall MapsFolder(mfc::CString* out);
// The full path of the map file `name`, constructed into `out`: `name` as it is
// when its second character is ':' (a drive), else under the maps folder. Returns `out`.
mfc::CString* __stdcall MapPath(mfc::CString* out, const char* name);
// Whether the maps folder has a file `name` (CFileFind::FindFile).
bool __stdcall MapFileExists(const char* name);
// Called by the client's file chunk handler. Writes what `buffer` holds (from its start to the highest
// position written, which the cursor may raise first) to the file `name` of the maps folder, created or cut
// to length. A file that does not open is written to all the same (the CFile's own error handling).
void __stdcall WriteMapFile(const char* name, GameBuffer* buffer);
