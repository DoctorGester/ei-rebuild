// Checks for the device's flat geometry draw (device_shadow.cpp).
#include "verify.h"

#include "../../src/game/clip.h"

#include <cstdint>
#include <cstring>

int __fastcall DeviceDrawFlat(char* device, void*, const void* positions, const char* uvs, int blocks,
                              const std::uint16_t* indices, int indexCount, std::uint32_t colour, std::uint32_t specular,
                              std::uint32_t last, float zScale);

// 004EA4A0 draws: ours only. The result is 0 or 2; when the vertex buffer holds one vertex per
// projected position (drawn without clipping, or clipped to the same count) every vertex has the
// colour pair, and without clipping also its texture coordinate.
static void* s_flat;
static int __fastcall DeviceDrawFlatVerify(char* device, void*, const void* positions, const char* uvs, int blocks,
                                           const std::uint16_t* indices, int indexCount, std::uint32_t colour,
                                           std::uint32_t specular, std::uint32_t last, float zScale) {
    int result = DeviceDrawFlat(device, nullptr, positions, uvs, blocks, indices, indexCount, colour, specular, last, zScale);
    bool ok = result == 0 || result == 2;
    struct Array {
        const TLVertex* data;
        int size;
    };
    const Array& vertices = *reinterpret_cast<const Array*>(0x007AEE00);
    if (result == 0 && vertices.size == blocks * 4) {
        for (int i = 0; ok && i < vertices.size; ++i) ok = vertices.data[i].color == colour && vertices.data[i].specular == specular;
    }
    ei::verify::Check("DeviceDrawFlat", true, ok, "result %d blocks %d indices %d", result, blocks, indexCount);
    return result;
}
EI_VERIFY(0x004EA4A0, DeviceDrawFlatVerify, 0, s_flat);
