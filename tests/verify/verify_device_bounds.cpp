// Checks for device_bounds.cpp (A-S12): the mesh draws' screen bounds, 004E86B0 (clipped TL
// vertices and their outcodes) and 004E8900 (projected positions). Side by side on every mesh drawn
// with flag 1 (figures and the 3D library's meshes: models, units, map_objects): the original first,
// the device's rectangle put back, then ours; the rectangle and the result (SetRect's) are compared.
// Each call also runs the pair on the branch the caller did not ask for (no outcodes for the TL
// vertices; the projected vertices' outcodes for the positions, when the buffer covers them), with
// the rectangle put back afterwards. Main thread.
#include "verify.h"

#include "../../src/game/clip.h"
#include "../../src/game/device_buffers.h"
#include "../../src/game/texture.h"
#include "../../src/lib/containers.h"

#include <cstdint>
#include <cstring>


namespace {

// Runs the original and the rewrite from the same rectangle; ours' rectangle and result stay.
template <typename Array, typename Fn>
int Compare(const char* name, Fn original, Fn ours, CTextureDevice* device, const Array* array,
            const std::uint8_t* outcodes) {
    const RECT before = device->drawnRect_;
    const int expected = original(device, nullptr, array, outcodes);
    const RECT theirs = device->drawnRect_;
    device->drawnRect_ = before;
    const int result = ours(device, nullptr, array, outcodes);
    const RECT& mine = device->drawnRect_;
    ei::verify::Check(name, true, expected == result && std::memcmp(&theirs, &mine, sizeof(RECT)) == 0,
                      "%d elements: (%ld, %ld, %ld, %ld) vs (%ld, %ld, %ld, %ld), result %d vs %d", array->size,
                      theirs.left, theirs.top, theirs.right, theirs.bottom, mine.left, mine.top, mine.right,
                      mine.bottom, expected, result);
    return result;
}

// Ours, through the registered members.
int __fastcall BoundsOfVerticesOurs(CTextureDevice* device, void*, const lib::AlignedArray<TLVertex>* vertices,
                                    const std::uint8_t* outcodes) {
    return device->BoundsOfVertices(vertices, outcodes);
}
int __fastcall BoundsOfProjectedOurs(CTextureDevice* device, void*,
                                     const lib::AlignedArray<devicebuffers::ProjectedVertex>* positions,
                                     const std::uint8_t* outcodes) {
    return device->BoundsOfProjected(positions, outcodes);
}

// 004E86B0: PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0x30 (3).
using VerticesFn = int(__fastcall*)(CTextureDevice*, void*, const lib::AlignedArray<TLVertex>*, const std::uint8_t*);
VerticesFn s_vertices;
int __fastcall BoundsOfVerticesVerify(CTextureDevice* device, void*, const lib::AlignedArray<TLVertex>* vertices,
                                      const std::uint8_t* outcodes) {
    const RECT before = device->drawnRect_;
    if (outcodes) {
        Compare("device bounds of TL vertices (004E86B0), no outcodes", s_vertices, &BoundsOfVerticesOurs, device,
                vertices, nullptr);
        device->drawnRect_ = before;
    }
    return Compare(outcodes ? "device bounds of TL vertices (004E86B0), outcodes" : "device bounds of TL vertices (004E86B0), no outcodes",
                   s_vertices, &BoundsOfVerticesOurs, device, vertices, outcodes);
}

// 004E8900: PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0x90 (6).
using ProjectedFn = int(__fastcall*)(CTextureDevice*, void*, const lib::AlignedArray<devicebuffers::ProjectedVertex>*,
                                     const std::uint8_t*);
ProjectedFn s_projected;
int __fastcall BoundsOfProjectedVerify(CTextureDevice* device, void*,
                                       const lib::AlignedArray<devicebuffers::ProjectedVertex>* positions,
                                       const std::uint8_t* outcodes) {
    const RECT before = device->drawnRect_;
    const int blocks = (positions->size + 3) & ~3;
    if (!outcodes && devicebuffers::Outcodes().data && devicebuffers::Outcodes().size >= blocks) {
        Compare("device bounds of projected positions (004E8900), outcodes", s_projected, &BoundsOfProjectedOurs,
                device, positions, devicebuffers::Outcodes().data);
        device->drawnRect_ = before;
    }
    return Compare(outcodes ? "device bounds of projected positions (004E8900), outcodes"
                            : "device bounds of projected positions (004E8900), no outcodes",
                   s_projected, &BoundsOfProjectedOurs, device, positions, outcodes);
}

} // namespace

EI_VERIFY(0x004E86B0, BoundsOfVerticesVerify, 6, s_vertices);
EI_VERIFY(0x004E8900, BoundsOfProjectedVerify, 9, s_projected);
