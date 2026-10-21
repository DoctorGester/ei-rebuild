// The material type of a face group name (0044A2A0, Ghidra: ParseMaterial; called by
// LoadFigureRegistry): the name split at its dots must have three parts; the second (lower-cased)
// names the texture type, the third the material. Unknown names are fatal errors. Names
// provisional; see docs/notes/objects.md ("Rewritten").
#include "../lib/crt.h"
#include "game_strings.h"
#include "../lib/mfc.h"
#include "ei/hook.h"

#include <cstdint>
#include <new>
#include "originals_library.h"
#include "originals_engine.h"

namespace scene {
namespace {


bool Is(const mfc::CString& s, const char* text) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(s)),
                           reinterpret_cast<const unsigned char*>(text)) == 0;
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
    mfc::CString rest(name);
    mfc::CStringArray parts;
    StringArrayConstruct(&parts);
    rest += '.';
    for (int dot = rest.Find('.'); dot != -1; dot = rest.Find('.')) {
        {
            mfc::CString part = rest.Left(dot);
            StringArraySetAtGrowStringOriginal(&parts, nullptr, parts.size, &part);
        }
        mfc::CString tail = rest.Right(rest.GetLength() - dot - 1);
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
            game::FatalError("ParseMaterialName.\n Unknown texture type \"%s\" in material \"%s\"\n", static_cast<const char*>(parts.data[1]), name);   // unknown texture type
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
            game::FatalError("ParseMaterialName.\n Unknown material type \"%s\" in material \"%s\"\n", static_cast<const char*>(parts.data[2]), name);   // unknown material
            *material = 0x15;
        }
    }
    StringArrayDestruct(&parts);
    return 1;
}
EI_REPLACE(0x0044A2A0, ParseMaterial);

} // namespace scene
