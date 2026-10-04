// Checks for the device's indexed draw with the current material (device_draw.cpp).
#include "verify.h"

#include <cstdint>

int __fastcall DeviceDrawIndexed(char* device, void*, int primitive, int fvf, void* vertices, int vertexCount,
                                 const std::uint16_t* indices, unsigned indexCount, int flags);

// 004EB550 calls Direct3D: ours only. The result is 1 and the counters grow by one call per pass
// (several only with a material whose technique has more than one state block), each with the
// vertices and indexCount / 3 triangles.
static void* s_drawIndexed;
static int __fastcall DeviceDrawIndexedVerify(char* device, void*, int primitive, int fvf, void* vertices, int vertexCount,
                                              const std::uint16_t* indices, unsigned indexCount, int flags) {
    auto at = [device](int offset) -> int& { return *reinterpret_cast<int*>(device + offset); };
    int passes = 1;
    if (char* material = *reinterpret_cast<char**>(device + 0x14C)) {
        char* technique = *reinterpret_cast<char**>(material + 0x14);
        int count = static_cast<int>(*reinterpret_cast<std::uint32_t**>(technique + 0xC) -
                                     *reinterpret_cast<std::uint32_t**>(technique + 8));
        if (count > 1) passes = count;
    }
    int vertexTotal = at(0x16C);
    unsigned triangleTotal = static_cast<unsigned>(at(0x170));
    int calls = at(0x174);
    int result = DeviceDrawIndexed(device, nullptr, primitive, fvf, vertices, vertexCount, indices, indexCount, flags);
    bool ok = result == 1 && at(0x174) - calls == passes && at(0x16C) - vertexTotal == passes * vertexCount &&
              static_cast<unsigned>(at(0x170)) - triangleTotal == static_cast<unsigned>(passes) * (indexCount / 3);
    ei::verify::Check("DeviceDrawIndexed", true, ok, "passes %d vertices %d indices %u", passes, vertexCount, indexCount);
    return result;
}
EI_VERIFY(0x004EB550, DeviceDrawIndexedVerify, 0, s_drawIndexed);

int __fastcall DeviceDrawTwoTextures(char* device, void*, void* first, void* second, int primitive, int fvf, void* vertices,
                                     int vertexCount, const std::uint16_t* indices, unsigned indexCount, int flags);

// 004EB370: ours only. Two calls with the vertices each, `second` left on stage 0.
static void* s_drawTwo;
static int __fastcall DeviceDrawTwoTexturesVerify(char* device, void*, void* first, void* second, int primitive, int fvf,
                                                  void* vertices, int vertexCount, const std::uint16_t* indices,
                                                  unsigned indexCount, int flags) {
    auto at = [device](int offset) -> int& { return *reinterpret_cast<int*>(device + offset); };
    int vertexTotal = at(0x16C);
    int calls = at(0x174);
    int result = DeviceDrawTwoTextures(device, nullptr, first, second, primitive, fvf, vertices, vertexCount, indices,
                                       indexCount, flags);
    bool ok = result == 1 && at(0x174) - calls == 2 && at(0x16C) - vertexTotal == 2 * vertexCount &&
              *reinterpret_cast<void**>(device + 0x12C) == second;
    ei::verify::Check("DeviceDrawTwoTextures", true, ok, "vertices %d indices %u", vertexCount, indexCount);
    return result;
}
EI_VERIFY(0x004EB370, DeviceDrawTwoTexturesVerify, 0, s_drawTwo);
