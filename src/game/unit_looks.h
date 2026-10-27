#pragma once

// The unit look records of a zone's state block: what the client needs to build each unit's skin and figure
// before the units themselves arrive. ReadUnitLooks (world_unit_set.cpp) reads the two lists,
// BuildUnitSkins (unit_skins.cpp) uses them, and the client's zone load (world_client_maps.cpp) owns them
// meanwhile.

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>
#include <memory>

class CStream;

using UnitIdList = stlp::vector<std::uint32_t>;

// A dressed unit of the zone (the second list read): its prototype, the item ids
// (armour type << 16 | material; 0xFFFFFFFF for none: [0] the helmet's row, [1] / [2] the weapons,
// [3..6] the armour) and the extra weapons, one of which (`extraWeapon`) is shown. The server's
// unit set (world.h, UnitSet) holds the same records; its figured records start with this head.
struct DressedUnitRecord {
    // Body of a virtual (registered by member pointer).
    DressedUnitRecord* CopyConstruct(const DressedUnitRecord* from);
    // Writes the prototype (2 bytes), each id list's count and dwords (the data written even when empty),
    // the byte and the dword through `stream`'s Write (also called on a FiguredUnitRecord's head).
    void Write(CStream* stream);

    std::uint16_t prototype;
    std::uint16_t unused_02;
    UnitIdList items;
    UnitIdList extraWeapons;        // weapon index << 16 | material index
    std::int8_t extraWeapon;
    std::uint8_t unused_1D[3];
    int modelNumber;                // -1: the textures are named after the prototype
};
static_assert(offsetof(DressedUnitRecord, extraWeapons) == 0x10);
static_assert(sizeof(DressedUnitRecord) == 0x24);

// Another unit of the zone (the first list read): a dressed record's head (its prototype, the
// armour ids in `items`, the weapon ids in `extraWeapons`, the weapon in hand and the model number; the
// dressed copy and Write take that head), the figure's morph coefficients and the texture names (none: from
// the prototype).
struct FiguredUnitRecord : DressedUnitRecord {
    // Body of a virtual (registered by member pointer).
    FiguredUnitRecord* CopyConstruct(const FiguredUnitRecord* from);
    float morphS;                   // the figure's morph coefficients (LoadFigure's s, t, u)
    float morphT;
    float morphU;
    stlp::vector<mfc::CString> names;
};
static_assert(offsetof(FiguredUnitRecord, morphS) == 0x24);
static_assert(sizeof(FiguredUnitRecord) == 0x3C);

// Both lists as a zone load holds them on its stack: empty vectors, destroyed as the game's inlined
// destructors do it (the dressed records first; per record the id lists from the back, a figured record's
// names before them; then each list's storage).
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
            for (mfc::CString* name = r->names.Start(); name != r->names.Finish(); ++name) std::destroy_at(name);
            r->names.FreeStorage();
            r->extraWeapons.FreeStorage();
            r->items.FreeStorage();
        }
        figured.FreeStorage();
    }

    stlp::vector<FiguredUnitRecord> figured;
    stlp::vector<DressedUnitRecord> dressed;
};
