#pragma once

// The maps folder and its files (world_map.cpp).

#include <string>
#include <string_view>

// The maps folder: the game's data folder + "maps\".
std::string MapsFolder();
// The full path of the map file `name`: `name` as it is
// when its second character is ':' (a drive), else under the maps folder.
std::string MapPath(std::string_view name);
// Whether the maps folder has a file (or folder) `name`.
bool MapFileExists(const char* name);
