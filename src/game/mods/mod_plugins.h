#pragma once

// The game patches a mod's config.reg names under [DEV] Plugin, which Rebirth's starter loads into the game as DLLs:
// the ones a shipped mod uses, carried as code and on while the mod plays. docs/notes/mods.md, "The mods' plugins".

namespace modplugins {

// Lost in Astral's fixshops.plg: a new game builds the four trader classes of a network game (1, 3, 4 and 5, each
// selling items and spells) instead of the campaign's five, and loading a game builds them again (the saved ones and
// their stock dropped, so each loaded game restocks on the next camp visit). The mod's bases open classes 1, 3, 4 and
// 5 only; with the five, class 1 sells no spells and class 5 nothing.
bool FourShopClasses();

} // namespace modplugins
