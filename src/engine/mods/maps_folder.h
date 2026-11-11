#pragma once

// The maps folder and its files (mods/mod_folders.cpp).

#include <string>
#include <string_view>

namespace platform {
struct FolderEntry;
}

// The maps folder: the game's data folder + "maps\".
std::string MapsFolder();
// The full path of the map file `name`: `name` as it is
// when its second character is ':' (a drive), else under the maps folder.
std::string MapPath(std::string_view name);
// Whether Rebirth's Maps rule takes the map file `name` from the mod's Maps\ (an archive also as its folder), not the
// game's (modfolders::SelectMaps at MapPath's site). False without a mod.
bool MapFromMod(std::string_view name);
// Whether the maps folder has a file (or folder) `name`; an archive (a .mpr, a .mq) also as its folder
// (data/resource.h, ResourceExists).
bool MapFileExists(const char* name);
// Whether the maps folder's entry `found` is a zone's quest file: a .mq archive, or the folder that stands for one
// (docs/notes/mods.md, "Loose files"), which holds its <name>\quest.reg.
bool IsQuestFile(const platform::FolderEntry& found);
