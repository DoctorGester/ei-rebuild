#pragma once

// The unit look records of a zone's state block: what the client needs to build each unit's skin and figure
// before the units themselves arrive. ReadUnitLooks (world_unit_set.cpp) reads the two lists,
// BuildUnitSkins (unit_skins.cpp) uses them, and the client's zone load (world_client_maps.cpp) owns them
// meanwhile.

#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class CStream;

using UnitIdList = std::vector<std::uint32_t>;

// A dressed unit of the zone (the second list read): its prototype, the item ids
// (armour type << 16 | material; 0xFFFFFFFF for none: [0] the helmet's row, [1] / [2] the weapons,
// [3..6] the armour) and the extra weapons, one of which (`extraWeapon`) is shown. The server's
// unit set (world.h, UnitSet) holds the same records; its figured records start with this head.
struct DressedUnitRecord {
    // Writes the prototype (2 bytes), each id list's count and dwords (the data written even when empty),
    // the byte and the dword through `stream`'s Write (also called on a FiguredUnitRecord's head).
    void Write(CStream* stream);

    std::uint16_t prototype;
    UnitIdList items;
    UnitIdList extraWeapons;        // weapon index << 16 | material index
    std::int8_t extraWeapon;
    int modelNumber;                // -1: the textures are named after the prototype
};

// Another unit of the zone (the first list read): a dressed record's head (its prototype, the
// armour ids in `items`, the weapon ids in `extraWeapons`, the weapon in hand and the model number; the
// dressed copy and Write take that head), the figure's morph coefficients and the texture names (none: from
// the prototype).
struct FiguredUnitRecord : DressedUnitRecord {
    float morphS;                   // the figure's morph coefficients (LoadFigure's s, t, u)
    float morphT;
    float morphU;
    std::vector<std::string> names;
};

// Both lists as a zone load holds them on its stack (destroyed the dressed records first).
struct UnitLooks {
    std::vector<FiguredUnitRecord> figured;
    std::vector<DressedUnitRecord> dressed;
};
