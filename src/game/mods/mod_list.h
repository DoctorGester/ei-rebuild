#pragma once

// The main menu's list of mods (docs/notes/mods.md, "The menu's list"): the folders and .zip files of the game folder's
// starter\Mods that hold a config.reg (platform::ModRoot), by their [MOD] Title, and the entry for the game without a
// mod.

#include <string>
#include <vector>

namespace modlist {

struct Entry {
    std::string title;     // the shown text (UTF-8)
    std::string folder;    // the mod's folder in full (platform::ModRoot: in a zip for one), empty for the game without a mod
    std::string setting;   // [game] Mod for it (as app::UseMod reads it), empty for none
};

// The entries, "no mod" first, then every mod by its folder's or zip's name. A mod whose config.reg says it is for one kind of
// game only (Single or Multi present and 0 for the other) has "(single player)" or "(multiplayer)" after its title.
// The mod being played (`current`: platform::ModFolder, empty for none) is among them, as a folder outside starter\Mods
// too: its title is the folder's name then.
std::vector<Entry> Scan(const std::string& gameFolder, const std::string& current);

// The entry of `entries` for the mod played now (`current` as Scan's); 0 for none or a folder not in the list.
int IndexOf(const std::vector<Entry>& entries, const std::string& current);

// The settings' [game] Mod of `folder` (in full): relative to the exe's folder when it is below it, else in full.
std::string SettingOf(const std::string& folder);

} // namespace modlist
