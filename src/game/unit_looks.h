#pragma once

// The unit look records of a zone's state block: what the client needs to build each unit's skin
// and figure before the units themselves arrive. ReadUnitLooks (0055A190, world_unit_set.cpp)
// reads the two lists, BuildUnitSkins (00568800, unit_skins.cpp) uses them, and the client's zone
// load (00567AB0, world_client_maps.cpp) owns them meanwhile.

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CStream;   // core_streams.h

using UnitIdList = stlp::vector<std::uint32_t>;

// A dressed unit of the zone (the second list read, 0x24 bytes): its prototype, the item ids
// (armour type << 16 | material; 0xFFFFFFFF for none: [0] the helmet's row, [1] / [2] the weapons,
// [3..6] the armour) and the extra weapons, one of which (`extraWeapon`) is shown. The server's
// unit set (world.h, UnitSet) holds the same records; its figured records start with this head.
struct DressedUnitRecord {
    // 00558FA0 (RET 4; world_unit_set.cpp, A-U7). Writes the prototype (2 bytes), each id list's
    // count and dwords (the data written even when empty), the byte and the dword through
    // `stream`'s slot 2 (also called on a FiguredUnitRecord's head).
    void Write(CStream* stream);

    std::uint16_t prototype;        // +0x00
    std::uint16_t unused_02;
    UnitIdList items;               // +0x04
    UnitIdList extraWeapons;        // +0x10 weapon index << 16 | material index
    std::int8_t extraWeapon;        // +0x1C
    std::uint8_t unused_1D[3];
    int modelNumber;                // +0x20 -1: the textures are named after the prototype
};
static_assert(offsetof(DressedUnitRecord, extraWeapons) == 0x10);
static_assert(sizeof(DressedUnitRecord) == 0x24);

// Another unit of the zone (the first list read, 0x3C bytes): its prototype, armour and weapon
// ids, the figure's morph coefficients and the texture names (none: from the prototype, 00535CB0).
struct FiguredUnitRecord {
    std::uint16_t prototype;        // +0x00
    std::uint16_t unused_02;
    UnitIdList armourIds;           // +0x04
    UnitIdList weaponIds;           // +0x10
    std::uint8_t extraWeapon;        // +0x1C (DressedUnitRecord::extraWeapon's place)
    std::uint8_t unused_1D[3];
    std::uint32_t modelNumber;       // +0x20 (DressedUnitRecord::modelNumber's place)
    float morphS;                   // +0x24 the figure's morph coefficients (LoadFigure's s, t, u)
    float morphT;
    float morphU;
    stlp::vector<const char*> names;    // +0x30 CStrings
};
static_assert(offsetof(FiguredUnitRecord, morphS) == 0x24);
static_assert(sizeof(FiguredUnitRecord) == 0x3C);

// Both lists as a zone load holds them on its stack: empty vectors, destroyed as the original's
// inlined destructors do it (the dressed records first; per record the id lists from the back, a
// figured record's names before them; then each list's storage).
struct UnitLooks {
    UnitLooks() = default;
    UnitLooks(const UnitLooks&) = delete;
    ~UnitLooks() {
        for (DressedUnitRecord* r = dressed.Start(); r != dressed.Finish(); ++r) {
            r->extraWeapons.FreeStorage();
            r->items.FreeStorage();
        }
        dressed.FreeStorage();
        for (FiguredUnitRecord* r = figured.Start(); r != figured.Finish(); ++r) {
            for (const char** name = r->names.Start(); name != r->names.Finish(); ++name)
                reinterpret_cast<mfc::CString*>(name)->~CString();
            r->names.FreeStorage();
            r->weaponIds.FreeStorage();
            r->armourIds.FreeStorage();
        }
        figured.FreeStorage();
    }

    stlp::vector<FiguredUnitRecord> figured;
    stlp::vector<DressedUnitRecord> dressed;
};
