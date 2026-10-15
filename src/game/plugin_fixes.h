#pragma once

// EI_Plugin.asi's Fixes feature in the player faces (docs/notes/rebirth.md, W4b). With Patch::EiPluginFixes
// the sex of a face is decided by one character of the figure's model name, and a player whose unit
// prototype is unknown gets the first prototype that is not a creature of the game's own races.
#include "unit_create.h"

#include "ei/rebirth.h"

#include <cctype>
#include <cstddef>
#include <initializer_list>

namespace pluginfixes {

inline bool Enabled() { return ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginFixes); }

// The plugin's replacement of the "unhuma" / "unhufe" comparisons (cmp byte [name+4], 0x66): a
// model whose fifth character is 'f' is a woman's, every other one a man's.
inline bool ModelIsFemale(const char* model) { return model[4] == 'f'; }

namespace detail {
// The plugin's 100030B0: does `name` contain `needle`, compared without case (letters only)?
inline bool Contains(const char* name, const char* needle) {
    auto fold = [](char c) { return c >= 0x61 ? static_cast<char>(c - 0x20) : c; };
    if (!*name) return false;
    for (; *name; ++name) {
        std::size_t i = 0;
        while (needle[i] && name[i] && fold(name[i]) == fold(needle[i])) ++i;
        if (!needle[i]) return true;
    }
    return false;
}
} // namespace detail

// The plugin's 10003110, called where a player's unit prototype was not found (006093B7, 00640C97):
// the head of the first bucket of the prototype table's name index (in bucket order, chains not
// walked) whose name has none of "NPC", "Human", "Orc", "Undead", "Male", "Female" in it. Null when
// there is none (the plugin would run on past the buckets).
inline char* FallbackPrototype() {
    auto& index = unitdb::PrototypeTable().byName;
    for (std::size_t b = 0; b < index.BucketCount(); ++b) {
        auto* node = index.Bucket(b);
        if (!node || !node->key.data()) continue;
        const char* name = node->key.data();
        bool listed = false;
        for (const char* race : {"NPC", "Human", "Orc", "Undead", "Male", "Female"})
            listed = listed || detail::Contains(name, race);
        if (!listed) return node->value;
    }
    return nullptr;
}

} // namespace pluginfixes
