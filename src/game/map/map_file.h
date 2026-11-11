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
    std::uint32_t magic = 0;        // kMapMagic
    float heightRange = 0.0f;       // the highest point: a vertex's altitude is a fraction of it
    std::int32_t sectorsX = 0;
    std::int32_t sectorsY = 0;
    std::int32_t pages = 0;         // texture pages of 64 tile textures each
    std::int32_t pageSize = 0;      // a page's side in pixels
    std::int32_t textures = 0;      // tile textures: the class words that follow the materials
    std::int32_t tileSize = 0;      // a tile's side in pixels
    std::uint16_t materials = 0;
    std::uint32_t animated = 0;     // animated texture entries
};
#pragma pack(pop)
static_assert(sizeof(MapHeader) == 0x26);
constexpr bool NoPadding(const MapHeader*) {
    return lib::FieldsFill<&MapHeader::magic, &MapHeader::heightRange, &MapHeader::sectorsX, &MapHeader::sectorsY,
                           &MapHeader::pages, &MapHeader::pageSize, &MapHeader::textures, &MapHeader::tileSize,
                           &MapHeader::materials, &MapHeader::animated>();
}

// A material: water when its kind is 2 or 3.
struct MapMaterial {
    int kind = 0;
    Vector3 colour;
    float opacity = 0.0f;
    float light = 0.0f;             // scales the colour
    float priority = 0.0f;
    float warpSpeed = 0.0f;         // (the name modding notes give it); copied, nothing reads it
    std::uint8_t reserved[0xC] = {};  // not read
};
static_assert(sizeof(MapMaterial) == 0x2C);
constexpr bool NoPadding(const MapMaterial*) {
    return lib::FieldsFill<&MapMaterial::kind, &MapMaterial::colour, &MapMaterial::opacity, &MapMaterial::light,
                           &MapMaterial::priority, &MapMaterial::warpSpeed, &MapMaterial::reserved>();
}

// An animated tile texture: the texture and its frame count (the entries after the class words).
struct MapAnimatedTexture {
    std::uint16_t texture = 0;
    std::uint16_t frames = 0;
};
static_assert(sizeof(MapAnimatedTexture) == 4);
constexpr bool NoPadding(const MapAnimatedTexture*) {
    return lib::FieldsFill<&MapAnimatedTexture::texture, &MapAnimatedTexture::frames>();
}

} // namespace terrain
