// The material type of a face group name (called by LoadFigureRegistry): the name split at its dots must
// have three parts; the second (lower-cased) names the texture type, the third the material. Unknown names
// are fatal errors. Names provisional; see docs/notes/objects.md ("Rewritten").
#include "../lib/crt.h"
#include "game_strings.h"
#include "../lib/mfc.h"
#include "lib/str.h"

#include <cstdint>
#include <new>
#include <string>
#include <vector>

namespace scene {
namespace {


bool Is(const std::string& s, const char* text) { return gamecrt::mbscmp(s.c_str(), text) == 0; }

struct Entry {
    const char* text;
    int value;
};
// The texture types (second part) and materials (third part), in the order they are tried.
constexpr Entry kTextures[] = {{"um", 1},  {"uw", 2},  {"tm", 3},  {"hm", 4},
                               {"hs", 5},  {"sm", 7},  {"spm", 6},  {"sps", 0xC},
                               {"om", 8},  {"wtm", 9},  {"wts", 10}, {"at1", 0xD},
                               {"at2", 0xE}, {"at3", 0xF}, {"at4", 0x10}, {"nn", 0}};
constexpr Entry kMaterials[] = {{"le", 0x11}, {"wo", 0x12}, {"me", 0x13},
                                {"sk", 0x15}, {"cl", 0x14}, {"st", 0x16},
                                {"mg", 0x17}, {"wt", 0x18}, {"cr", 0x19}};

} // namespace

// Writes the texture type and `material`. Returns 1. A name without exactly three parts gives (1, 0x15); an
// unknown texture type 1, an unknown material 0x15 (after the error).
int __fastcall ParseMaterial(const char* name, int* texture, int* material) {
    std::string rest = name;
    std::vector<std::string> parts;
    rest += '.';
    for (int dot = str::Find(rest, '.'); dot != -1; dot = str::Find(rest, '.')) {
        {
            parts.push_back(str::Left(rest, dot));
        }
        std::string tail = str::Right(rest, static_cast<int>(rest.size()) - dot - 1);
        rest = tail;
    }
    if (parts.size() != 3) {
        *texture = 1;
        *material = 0x15;
    } else {
        str::MakeLower(parts[1]);
        bool found = false;
        for (const Entry& e : kTextures) {
            if (Is(parts[1], e.text)) {
                *texture = e.value;
                found = true;
                break;
            }
        }
        if (!found) {
            game::FatalError("ParseMaterialName.\n Unknown texture type \"%s\" in material \"%s\"\n", parts[1].c_str(), name);   // unknown texture type
            *texture = 1;
        }
        str::MakeLower(parts[2]);
        found = false;
        for (const Entry& e : kMaterials) {
            if (Is(parts[2], e.text)) {
                *material = e.value;
                found = true;
                break;
            }
        }
        if (!found) {
            game::FatalError("ParseMaterialName.\n Unknown material type \"%s\" in material \"%s\"\n", parts[2].c_str(), name);   // unknown material
            *material = 0x15;
        }
    }
    return 1;
}

} // namespace scene
