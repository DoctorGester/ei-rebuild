#pragma once

// The shadow rasteriser's target (texture.h's CPixelConverter triangle rasterisers,
// CFigureMesh::Rasterise), without the Windows headers texture.h brings.

#include <cstddef>
#include <cstdint>

// The start of the target the pixel converter's triangle rasterisers read: a shadow
// triangle's three corners, each a screen and a texture point, and the keyed
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
    // A keyed texture's texels, or null (always: nothing allocates a keyed texture's texels, the former
    // CTexture::block_). Set, the DLL's keyed triangle virtual would sample them as 16-bit 1:5:5:5 texels at the
    // corners' texture points (wrapping at the row pitch, RasterTarget::keyPitch) and shadow only where the top
    // bit is set. The layout is the DLL's.
    const std::uint8_t* keyTexels;
};

// The whole target a shadow's parts are rasterised into (map_object_shadow.cpp fills it): the
// triangle the callees write, then the surface's bits.
struct RasterTarget : RasterTriangle {
    void* bits;
    int keyPitch;              // the key texels' row pitch in bytes (only that virtual reads it)
    int pitch;
};
