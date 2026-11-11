#pragma once

// Ours: the mod a game plays as our peers tell each other at login (docs/notes/network.md, "Mods"): its title and its
// content hash (mod_hash.h), and the texts of a join refused for it.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace modidentity {

struct Identity {
    std::string title;          // [MOD] Title (UTF-8), else the mod's name; empty: no mod
    std::uint64_t hash = 0;     // modhash::ContentHash, 0 without a mod

    bool operator==(const Identity&) const = default;
};

// The mod played now (platform::ModFolder): its title and hash made at the first call for that folder (logged) and
// kept, since a mod changes only in the main menu outside a network session. The hash is Prepare's when it began one
// for the folder (waited for if it is not done).
const Identity& Played();
// The main menu's Multiplayer button: the played mod's content hash begun on a thread of its own, so that hosting or
// joining finds it made (a folder mod's counted files are read whole: Lost in Astral's 160 MB took seconds from a
// cold disk). Nothing for no mod or a hash made or begun already.
void Prepare();

// Whether a peer of mod `theirs` plays with this game: the same title and hash. A peer that sent no mod mark (none:
// the original game) counts as playing none.
bool Agrees(const std::optional<Identity>& theirs);

// The refusal box of a join this game's mod stopped, the host's mod `host` (none: the host sent no mark, the original
// game): its title and text. Each is the game's text of its key when the game's texts have one (lmp_mod_mismatch,
// lmp_mod_mismatch_msg, lmp_mod_version_msg, lmp_mod_original_msg), else ours in Russian where the game's texts are
// (GameTextsInRussian) or in English.
struct Box {
    std::string title;
    std::string text;
};
Box RefusalBox(const std::optional<Identity>& host);

// The server's chat line for a join from `address` it refused for the client's mod `theirs` (none: an original
// client's), as RefusalBox finds its text (lmp_mod_refused).
std::string RefusedLine(std::string_view address, const std::optional<Identity>& theirs);

// The connection screen's lines for a listed host of mod `host` (none: the original game), as RefusalBox finds its
// texts: the host's mod (lmp_mod_listed) and, when this game cannot join it for the mods, why (lmp_mod_cannot_join,
// lmp_mod_cannot_join_version; else empty).
struct Listing {
    std::string mod;
    std::string cannotJoin;
};
Listing Listed(const std::optional<Identity>& host);

// The mod as the log names it: "\"<title>\" <hash>", "no mod" or "no mark (the original game)".
std::string Described(const std::optional<Identity>& mod);

} // namespace modidentity
