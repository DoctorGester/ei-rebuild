// The material type of a face group name (0044A2A0, Ghidra: ParseMaterial; called by
// LoadFigureRegistry): the name split at its dots must have three parts; the second (lower-cased)
// names the texture type, the third the material. Unknown names are fatal errors. Names
// provisional; see docs/notes/objects.md ("Sixth round").
#include "../lib/crt.h"
#include "../lib/mfc.h"
#include "ei/hook.h"

#include <cstdint>
#include <new>

namespace scene {
namespace {

// MFC members not in lib/mfc.h (library): CString::operator+=(char), Find(char), Left / Right
// (returned by value: the result is constructed in `out`), CStringArray constructor, destructor,
// SetAtGrow.
__declspec(naked) void __fastcall AppendChar(mfc::CString*, void*, char) EI_ORIGINAL(0x00700D73)
__declspec(naked) int __fastcall FindChar(const mfc::CString*, void*, char) EI_ORIGINAL(0x00700E91)
__declspec(naked) mfc::CString* __fastcall Left(const mfc::CString*, void*, mfc::CString* /*out*/, int) EI_ORIGINAL(0x006FDAE9)
__declspec(naked) mfc::CString* __fastcall Right(const mfc::CString*, void*, mfc::CString* /*out*/, int) EI_ORIGINAL(0x006FDA6D)
__declspec(naked) void __fastcall StringArrayConstruct(void*) EI_ORIGINAL(0x006FE925)
__declspec(naked) void __fastcall StringArrayDestruct(void*) EI_ORIGINAL(0x006FE958)
__declspec(naked) void __fastcall StringArraySetAtGrow(void*, void*, int, const mfc::CString*) EI_ORIGINAL(0x006FEBFA)
__declspec(naked) int __cdecl FatalError(const char*, ...) EI_ORIGINAL(0x00480080)

struct StringArray {   // CStringArray
    std::uintptr_t vtable;
    mfc::CString* data;
    int size;
    int max;
    int grow;
};

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
bool Is(const mfc::CString& s, std::uintptr_t text) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(s)),
                           reinterpret_cast<const unsigned char*>(Text(text))) == 0;
}

struct Entry {
    std::uintptr_t text;
    int value;
};
// The texture types (second part) and materials (third part), in the order they are tried.
constexpr Entry kTextures[] = {{0x00784530, 1},  {0x0078452C, 2},  {0x00784528, 3},  {0x00784524, 4},
                               {0x00784520, 5},  {0x0078451C, 7},  {0x00784518, 6},  {0x00784514, 0xC},
                               {0x00784510, 8},  {0x0078450C, 9},  {0x00784508, 10}, {0x00784504, 0xD},
                               {0x00784500, 0xE}, {0x007844FC, 0xF}, {0x007844F8, 0x10}, {0x007844F4, 0}};
constexpr Entry kMaterials[] = {{0x007844B0, 0x11}, {0x007844AC, 0x12}, {0x007844A8, 0x13},
                                {0x007844A4, 0x15}, {0x007844A0, 0x14}, {0x0078449C, 0x16},
                                {0x00784498, 0x17}, {0x00784494, 0x18}, {0x00784490, 0x19}};

} // namespace

// ECX: the name, EDX: the texture type out, `material` out. Returns 1. A name without exactly three
// parts gives (1, 0x15); an unknown texture type 1, an unknown material 0x15 (after the error).
int __fastcall ParseMaterial(const char* name, int* texture, int* material) {
    mfc::CString rest(name);
    StringArray parts;
    StringArrayConstruct(&parts);
    AppendChar(&rest, nullptr, '.');
    for (int dot = FindChar(&rest, nullptr, '.'); dot != -1; dot = FindChar(&rest, nullptr, '.')) {
        alignas(mfc::CString) unsigned char slot[sizeof(mfc::CString)];
        mfc::CString* part = Left(&rest, nullptr, reinterpret_cast<mfc::CString*>(slot), dot);
        StringArraySetAtGrow(&parts, nullptr, parts.size, part);
        part->~CString();
        mfc::CString* tail = Right(&rest, nullptr, reinterpret_cast<mfc::CString*>(slot), rest.GetLength() - dot - 1);
        rest = *tail;
        tail->~CString();
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
            FatalError(Text(0x007844B4), static_cast<const char*>(parts.data[1]), name);   // unknown texture type
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
            FatalError(Text(0x0078444C), static_cast<const char*>(parts.data[2]), name);   // unknown material
            *material = 0x15;
        }
    }
    StringArrayDestruct(&parts);
    return 1;
}
EI_REPLACE(0x0044A2A0, ParseMaterial);

} // namespace scene
