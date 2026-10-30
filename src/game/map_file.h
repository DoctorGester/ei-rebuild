#pragma once

// The map file (<map>.MP in the map's archive): this header, the materials, a class word per tile texture
// (its terrain type), then the animated textures. The landscape (terrain_landscape.cpp) and the AI map
// (ai_map_load.cpp) both read it.

#include "lib/vecmath.h"

#include <cstdint>

namespace terrain {

constexpr std::uint32_t kMapMagic = 0xCE4AF672;

#pragma pack(push, 1)
struct MapHeader {
    std::uint32_t magic;            // kMapMagic
    float heightRange;              // the highest point: a vertex's altitude is a fraction of it
    std::int32_t sectorsX;
    std::int32_t sectorsY;
    std::int32_t pages;             // texture pages of 64 tile textures each
    std::int32_t pageSize;          // a page's side in pixels
    std::int32_t textures;          // tile textures: the class words that follow the materials
    std::int32_t tileSize;          // a tile's side in pixels
    std::uint16_t materials;
    std::uint32_t animated;         // animated texture entries
};
#pragma pack(pop)
static_assert(sizeof(MapHeader) == 0x26);

// A material: water when its kind is 2 or 3.
struct MapMaterial {
    int kind;
    Vector3 colour;
    float opacity;
    float light;                    // scales the colour
    float priority;
    float warpSpeed;                // (the name modding notes give it); copied, nothing reads it
    std::uint8_t unused_20[0xC];    // not read
};
static_assert(sizeof(MapMaterial) == 0x2C);

} // namespace terrain
