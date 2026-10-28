#pragma once

// The maps folder and its files (world_map_files.cpp). All four are patched by Rebirth (the mod's own maps
// folder), so these install in the vanilla launch only and callers keep calling the fixed addresses.

#include "ei/hook.h"
#include <string>
#include <string_view>


class GameBuffer;

// The maps folder: the game's data folder + "maps\", into `out`. Returns `out`.
std::string* MapsFolder(std::string* out);
// The full path of the map file `name`, into `out`: `name` as it is
// when its second character is ':' (a drive), else under the maps folder. Returns `out`.
std::string* MapPath(std::string* out, std::string_view name);
// Whether the maps folder has a file `name` (lib::FileFind::FindFile).
bool MapFileExists(const char* name);
// Called by the client's file chunk handler. Writes what `buffer` holds (from its start to the highest
// position written, which the cursor may raise first) to the file `name` of the maps folder, created or cut
// to length. A file that does not open is written to all the same (the file's own error handling).
void WriteMapFile(const char* name, GameBuffer* buffer);
