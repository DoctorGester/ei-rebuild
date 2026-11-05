#pragma once

// IronMod's fixfslpatei.plg carried as code, on while the playing mod's config.reg names it under [DEV] Plugin.
// The plugin is a generic patcher of the Russian 1.06 executable: it writes the bytes its folder's patches.txt lists;
// IronMod's lines make a game without cheats or quick saves. docs/notes/mods.md, "The mods' plugins".

enum class ScriptFunction : int;

namespace modplugins {

// The key bindings file: config\kbdinput.ini with IronMod (Rebirth's copy of the bindings without the quick save
// and quick load keys), else config\keyboard.ini.
const char* KeyBindingsFile();

// What the console prints when the cheat word is typed: "NoCheating" with IronMod, else "Activated!". The word still
// unlocks the cheat commands and the script lines.
const char* CheatWordReply();

// IronMod: the quick save and the quick load do nothing (Rebirth's slot "save:" cannot be made, and there the
// failed save left the game for the main menu).
bool QuickSavesOff();

// While a ConsoleScript lives, the script parser is reading text the console runs (an "@", "#" or "$" line, execs).
class ConsoleScript {
public:
    ConsoleScript();
    ~ConsoleScript();
    ConsoleScript(const ConsoleScript&) = delete;
    ConsoleScript& operator=(const ConsoleScript&) = delete;
};

// IronMod hides eighteen script commands from what the console runs (GiveMoney, GiveItem, KillUnit, AddMob, GodMode,
// SetDiplomacy and their kind): the parser does not know them there. The zones' own scripts keep them.
bool HidesKeyword(ScriptFunction id);

} // namespace modplugins
