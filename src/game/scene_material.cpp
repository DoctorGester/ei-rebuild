// The material type of a face group name (0044A2A0, Ghidra: ParseMaterial; called by
// LoadFigureRegistry): the name split at its dots must have three parts; the second (lower-cased)
// names the texture type, the third the material. Unknown names are fatal errors. Names
// provisional; see docs/notes/objects.md ("Rewritten").
#include "../lib/crt.h"
#include "game_strings.h"
#include "../lib/mfc.h"
#include "ei/hook.h"
#include "lib/str.h"

#include <cstdint>
#include <new>
#include <string>

namespace scene {
namespace {


bool Is(const mfc::CString& s, const char* text) {
    return gamecrt::mbscmp(s.GetString(),
                           text) == 0;
}

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

// ECX: the name, EDX: the texture type out, `material` out. Returns 1. A name without exactly three
// parts gives (1, 0x15); an unknown texture type 1, an unknown material 0x15 (after the error).
int __fastcall ParseMaterial(const char* name, int* texture, int* material) {
    std::string rest = name;
    mfc::CStringArray parts;
    parts.Construct();
    rest += '.';
    for (int dot = str::Find(rest, '.'); dot != -1; dot = str::Find(rest, '.')) {
        {
            std::string part = str::Left(rest, dot);
            parts.SetAtGrow(parts.size, part);
        }
        std::string tail = str::Right(rest, static_cast<int>(rest.size()) - dot - 1);
        rest = tail;
    }
    if (parts.size != 3) {
        *texture = 1;
        *material = 0x15;
    } else {
        parts.data[1].MakeLower();
        bool found = false;
        for (const Entry& e : kTextures) {
            if (Is(parts.data[1], e.text)) {
                *texture = e.value;
                found = true;
                break;
            }
        }
        if (!found) {
            game::FatalError("ParseMaterialName.\n Unknown texture type \"%s\" in material \"%s\"\n", parts.data[1].GetString(), name);   // unknown texture type
            *texture = 1;
        }
        parts.data[2].MakeLower();
        found = false;
        for (const Entry& e : kMaterials) {
            if (Is(parts.data[2], e.text)) {
                *material = e.value;
                found = true;
                break;
            }
        }
        if (!found) {
            game::FatalError("ParseMaterialName.\n Unknown material type \"%s\" in material \"%s\"\n", parts.data[2].GetString(), name);   // unknown material
            *material = 0x15;
        }
    }
    parts.Destruct();
    return 1;
}
EI_REWRITTEN(0x0044A2A0, ParseMaterial);

} // namespace scene
