#pragma once

// The shadow rasteriser's target (texture.h's CPixelConverter triangle rasterisers,
// CFigureMesh::Rasterise), without the Windows headers texture.h brings.

#include <cstddef>
#include <cstdint>

// The start of the target the pixel converter's triangle rasterisers read: a shadow
// triangle's three corners, each a screen and a texture point (moved as qwords), and the keyed
// texture's texels; the surface follows (RasterTarget).
struct RasterTriangle {
    struct Point {
        int x;
        int y;
    };
    struct Corner {
        Point screen;
        Point texture;
    };
    Corner corners[3];
    // A keyed texture's texels (CTexture::block_), or null. Set: the figure meshes rasterise through
    // the triangle virtual, which samples them as 16-bit 1:5:5:5 texels at the corners' texture points
    // (wrapping at the row pitch, RasterTarget::keyPitch) and shadows only where the top bit is set.
    const std::uint8_t* keyTexels;
};
static_assert(sizeof(RasterTriangle::Point) == 8 && offsetof(RasterTriangle, keyTexels) == 0x30);

// The whole target a shadow's parts are rasterised into (map_object_shadow.cpp fills it): the
// triangle the callees write, then the surface's bits.
struct RasterTarget : RasterTriangle {
    void* bits;
    int keyPitch;              // the key texels' row pitch in bytes (only that virtual reads it)
    int pitch;
};
static_assert(offsetof(RasterTarget, bits) == 0x34 && sizeof(RasterTarget) == 0x40);
