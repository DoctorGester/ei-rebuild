// Checks for the device's indexed draw with the current material (device_draw.cpp).
#include "verify.h"

#include "../../src/game/device_material.h"
#include "../../src/game/model.h"
#include "../../src/game/texture.h"

#include <cstdint>


// 004EB550 calls Direct3D: ours only. The result is 1 and the counters grow by one call per pass
// (several only with a material whose technique has more than one state block), each with the
// vertices and indexCount / 3 triangles.
static void* s_drawIndexed;
static int __fastcall DeviceDrawIndexedVerify(CTextureDevice* device, void*, int primitive, int fvf, void* vertices,
                                              int vertexCount, const std::uint16_t* indices, unsigned indexCount,
                                              int flags) {
    int passes = 1;
    if (const CDeviceMaterial* material = device->material_) {
        int count = static_cast<int>(material->preset->blocks_.size());
        if (count > 1) passes = count;
    }
    int vertexTotal = device->vertexCount_;
    unsigned triangleTotal = static_cast<unsigned>(device->primitiveCount_);
    int calls = device->drawCount_;
    int result = device->DrawIndexed(primitive, fvf, vertices, vertexCount, indices, indexCount, flags);
    bool ok = result == 1 && device->drawCount_ - calls == passes && device->vertexCount_ - vertexTotal == passes * vertexCount &&
              static_cast<unsigned>(device->primitiveCount_) - triangleTotal == static_cast<unsigned>(passes) * (indexCount / 3);
    ei::verify::Check("DeviceDrawIndexed", true, ok, "passes %d vertices %d indices %u", passes, vertexCount, indexCount);
    return result;
}
EI_VERIFY(0x004EB550, DeviceDrawIndexedVerify, 0, s_drawIndexed);


// 004EB370: ours only. Two calls with the vertices each, `second` left on stage 0.
static void* s_drawTwo;
static int __fastcall DeviceDrawTwoTexturesVerify(CTextureDevice* device, void*, CDXSurface* first, CDXSurface* second,
                                                  int primitive, int fvf, void* vertices, int vertexCount,
                                                  const std::uint16_t* indices, unsigned indexCount, int flags) {
    int vertexTotal = device->vertexCount_;
    int calls = device->drawCount_;
    int result = device->DrawTwoTextures(first, second, primitive, fvf, vertices, vertexCount, indices, indexCount, flags);
    bool ok = result == 1 && device->drawCount_ - calls == 2 && device->vertexCount_ - vertexTotal == 2 * vertexCount &&
              device->stageSurfaces_[0] == second;
    ei::verify::Check("DeviceDrawTwoTextures", true, ok, "vertices %d indices %u", vertexCount, indexCount);
    return result;
}
EI_VERIFY(0x004EB370, DeviceDrawTwoTexturesVerify, 0, s_drawTwo);
