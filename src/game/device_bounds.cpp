// The Direct3D device's screen bounds of a mesh draw (mesh draw flag 1): the smallest and largest
// x and y of its clipped TL vertices (004E86B0) or of its projected positions (004E8900), rounded
// into the device's drawn rectangle (+0x15C), which the figures' and the 3D library's draws read
// back. A-S12; see docs/notes/renderer.md (section 12).
#include "clip.h"            // TLVertex
#include "device_buffers.h"  // ProjectedVertex
#include "texture.h"         // CTextureDevice

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/containers.h"
#include "lib/gameapi.h"

#include <cstdint>
#include <cfloat>

namespace {

// FCOMP, then the C0 bit as a mask: "less, or unordered".
bool LessOrUnordered(float a, float b) { return a < b || x87::IsNaN(a) || x87::IsNaN(b); }

// The bounds so far: they start inside out, and a coordinate replaces a bound it is not inside of
// (a NaN never does).
struct ScreenBounds {
    float minX = FLT_MAX;
    float minY = FLT_MAX;
    float maxX = -FLT_MAX;
    float maxY = -FLT_MAX;

    void Include(float x, float y) {
        minX = LessOrUnordered(minX, x) ? minX : x;
        maxX = LessOrUnordered(x, maxX) ? maxX : x;
        minY = LessOrUnordered(minY, y) ? minY : y;
        maxY = LessOrUnordered(y, maxY) ? maxY : y;
    }

    // The bounds rounded (FISTP) into the device's rectangle; SetRect's result.
    int Store(CTextureDevice* device) const {
        const int bottom = x87::fistp(maxY);
        const int right = x87::fistp(maxX);
        const int top = x87::fistp(minY);
        const int left = x87::fistp(minX);
        return gameapi::SetRect(&device->drawnRect_, left, top, right, bottom);
    }
};

constexpr int kBlock = 4;   // projected positions come in blocks of four

} // namespace

// 004E86B0 (thiscall, RET 8): the bounds of TL vertices; with `outcodes` only of those whose
// outcode is 0 (the clipper's output: on screen).
int CTextureDevice::BoundsOfVertices(const lib::AlignedArray<TLVertex>* vertices,
                                      const std::uint8_t* outcodes) {
    ScreenBounds bounds;
    for (int i = 0; i < vertices->size; ++i) {
        if (outcodes && outcodes[i] != 0) continue;
        const TLVertex& vertex = vertices->data[i];
        bounds.Include(vertex.x, vertex.y);
    }
    return bounds.Store(this);
}
EI_REPLACE_METHOD(0x004E86B0, CTextureDevice::BoundsOfVertices);

// 004E8900 (thiscall, RET 8): the same of projected positions, whole blocks of four at a time (a
// size that is not a multiple of four reads to the end of its last block).
int CTextureDevice::BoundsOfProjected(const lib::AlignedArray<devicebuffers::ProjectedVertex>* positions,
                                       const std::uint8_t* outcodes) {
    ScreenBounds bounds;
    for (int i = 0; i < positions->size; i += kBlock) {
        for (int k = i; k < i + kBlock; ++k) {
            if (outcodes && outcodes[k] != 0) continue;
            const devicebuffers::ProjectedVertex& position = positions->data[k];
            bounds.Include(position.x, position.y);
        }
    }
    return bounds.Store(this);
}
EI_REPLACE_METHOD(0x004E8900, CTextureDevice::BoundsOfProjected);
