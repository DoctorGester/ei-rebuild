// Checks for the device's mesh draws (device_mesh.cpp).
#include "verify.h"

#include "mesh_capture.h"

#include "../../src/game/clip.h"

#include <cstdint>
#include <cstring>

class CTextureDevice;

int __fastcall DeviceDrawMesh(CTextureDevice* device, void*, const void* positions, int blocks, int unused5, int colourCount,
                              std::uint32_t* colours, std::uint32_t* speculars, const void* uvs, int unused10,
                              const void* corners, int cornerCount, int unused13, int unused14,
                              std::uint16_t* indices, int indexCount, float uOffset, float vOffset, int alpha,
                              int flags);
int __fastcall DeviceDrawMesh2(CTextureDevice* device, void*, void* firstTexture, void* secondTexture, const void* positions,
                               int blocks, int unused5, int colourCount, std::uint32_t* colours,
                               std::uint32_t* speculars, const void* uvs, int unused10, const void* corners,
                               int cornerCount, int unused13, int unused14, std::uint16_t* indices, int indexCount,
                               float uOffset, float vOffset, int alpha, int flags);

namespace {

struct AlignedArray {
    char* data;
    int size;
    int capacity;
    int growBy;
};
const AlignedArray& At(std::uintptr_t address) { return *reinterpret_cast<const AlignedArray*>(address); }

bool ZeroOrNaN(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return (bits & 0x7FFFFFFFu) == 0 || ((bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0);
}

// Drawn on screen: the TL vertices (007AEE00) are the corners - position from the projected array
// (007AEE10), colour and specular, and the texture coordinate when there are no offsets.
bool VerticesAreCorners(const std::uint32_t* colours, const std::uint32_t* speculars, const void* uvs,
                        const std::uint16_t* corners, int cornerCount, float uOffset, float vOffset) {
    const AlignedArray& vertices = At(0x007AEE00);
    if (vertices.size != cornerCount) return false;
    bool plainUv = ZeroOrNaN(uOffset) && ZeroOrNaN(vOffset);
    for (int i = 0; i < cornerCount; ++i) {
        const std::uint16_t* c = corners + i * 3;
        const auto* v = reinterpret_cast<const TLVertex*>(vertices.data) + i;
        if (std::memcmp(&v->x, At(0x007AEE10).data + c[0] * 16, 16) != 0) return false;
        if (v->color != colours[c[1]] || v->specular != speculars[c[1]]) return false;
        if (plainUv && std::memcmp(&v->u, static_cast<const char*>(uvs) + c[2] * 8, 8) != 0) return false;
    }
    return true;
}

// Clipped: the corners' outcodes (007AEE20) are their vertices' (007AEE30).
bool OutcodesCopied(const std::uint16_t* corners, int cornerCount) {
    const auto* outcodes = reinterpret_cast<const std::uint8_t*>(At(0x007AEE30).data);
    const auto* cornerCodes = reinterpret_cast<const std::uint8_t*>(At(0x007AEE20).data);
    for (int i = 0; i < cornerCount; ++i) {
        if (cornerCodes[i] != outcodes[corners[i * 3]]) return false;
    }
    return true;
}

// The draws run once, so only ours runs. Without flag 2 (no clipping) a drawn mesh must have its
// corners as vertices; with it, either that or (clipped) the corners' outcodes copied.
bool CheckCorners(int result, const std::uint32_t* colours, const std::uint32_t* speculars, const void* uvs,
                  const std::uint16_t* corners, int cornerCount, float uOffset, float vOffset, int flags) {
    if (result == 2) return true;
    if (VerticesAreCorners(colours, speculars, uvs, corners, cornerCount, uOffset, vOffset)) return true;
    return (flags & 2) != 0 && OutcodesCopied(corners, cornerCount);
}

// The tap (mesh_capture.h): the arguments as the dwords the caller pushed, and the positions.
void Capture(bool textured, void* first, void* second, const void* positions, int blocks, int unused5,
             int colourCount, std::uint32_t* colours, std::uint32_t* speculars, const void* uvs, int unused10,
             const void* corners, int cornerCount, int unused13, int unused14, std::uint16_t* indices,
             int indexCount, const float& uOffset, const float& vOffset, int alpha, int flags) {
    auto word = [](const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); };
    auto bits = [](const float& f) {
        std::uint32_t b;
        std::memcpy(&b, &f, 4);
        return b;
    };
    meshcapture::Draw& d = meshcapture::Tap().draw;
    ++d.calls;
    d.textured = textured;
    d.textures[0] = word(first);
    d.textures[1] = word(second);
    const std::uint32_t arguments[18] = {word(positions), static_cast<std::uint32_t>(blocks),
                                         static_cast<std::uint32_t>(unused5), static_cast<std::uint32_t>(colourCount),
                                         word(colours), word(speculars), word(uvs),
                                         static_cast<std::uint32_t>(unused10), word(corners),
                                         static_cast<std::uint32_t>(cornerCount), static_cast<std::uint32_t>(unused13),
                                         static_cast<std::uint32_t>(unused14), word(indices),
                                         static_cast<std::uint32_t>(indexCount), bits(uOffset), bits(vOffset),
                                         static_cast<std::uint32_t>(alpha), static_cast<std::uint32_t>(flags)};
    std::memcpy(d.arguments, arguments, sizeof(arguments));
    std::uint32_t sum = 0;
    const auto* rows = static_cast<const std::uint32_t*>(positions);
    for (int b = 0; b < blocks; ++b)
        for (int k = 0; k < 12; ++k) sum = sum * 31 + rows[b * 16 + k];   // the w row is not the caller's
    d.positionSum = sum;
}

} // namespace

meshcapture::State& meshcapture::Tap() {
    static State state{};
    return state;
}

static void* s_mesh;
static int __fastcall DeviceDrawMeshVerify(CTextureDevice* device, void*, const void* positions, int blocks, int unused5,
                                           int colourCount, std::uint32_t* colours, std::uint32_t* speculars,
                                           const void* uvs, int unused10, const void* corners, int cornerCount,
                                           int unused13, int unused14, std::uint16_t* indices, int indexCount,
                                           float uOffset, float vOffset, int alpha, int flags) {
    if (meshcapture::Tap().mode != meshcapture::Mode::Off) {
        Capture(false, nullptr, nullptr, positions, blocks, unused5, colourCount, colours, speculars, uvs, unused10,
                corners, cornerCount, unused13, unused14, indices, indexCount, uOffset, vOffset, alpha, flags);
        if (meshcapture::Tap().mode == meshcapture::Mode::Skip) return meshcapture::Tap().result;
    }
    int result = DeviceDrawMesh(device, nullptr, positions, blocks, unused5, colourCount, colours, speculars, uvs,
                                unused10, corners, cornerCount, unused13, unused14, indices, indexCount, uOffset,
                                vOffset, alpha, flags);
    bool ok = CheckCorners(result, colours, speculars, uvs, static_cast<const std::uint16_t*>(corners), cornerCount,
                           uOffset, vOffset, flags);
    ei::verify::Check("DeviceDrawMesh", true, ok, "result %d corners %d flags %x", result, cornerCount, flags);
    return result;
}
EI_VERIFY(0x004E9010, DeviceDrawMeshVerify, 0, s_mesh);

static void* s_mesh2;
static int __fastcall DeviceDrawMesh2Verify(CTextureDevice* device, void*, void* firstTexture, void* secondTexture,
                                            const void* positions, int blocks, int unused5, int colourCount,
                                            std::uint32_t* colours, std::uint32_t* speculars, const void* uvs,
                                            int unused10, const void* corners, int cornerCount, int unused13,
                                            int unused14, std::uint16_t* indices, int indexCount, float uOffset,
                                            float vOffset, int alpha, int flags) {
    if (meshcapture::Tap().mode != meshcapture::Mode::Off) {
        Capture(true, firstTexture, secondTexture, positions, blocks, unused5, colourCount, colours, speculars, uvs,
                unused10, corners, cornerCount, unused13, unused14, indices, indexCount, uOffset, vOffset, alpha,
                flags);
        if (meshcapture::Tap().mode == meshcapture::Mode::Skip) return meshcapture::Tap().result;
    }
    int result = DeviceDrawMesh2(device, nullptr, firstTexture, secondTexture, positions, blocks, unused5,
                                 colourCount, colours, speculars, uvs, unused10, corners, cornerCount, unused13,
                                 unused14, indices, indexCount, uOffset, vOffset, alpha, flags);
    bool ok = CheckCorners(result, colours, speculars, uvs, static_cast<const std::uint16_t*>(corners), cornerCount,
                           uOffset, vOffset, flags);
    ei::verify::Check("DeviceDrawMesh2", true, ok, "result %d corners %d flags %x", result, cornerCount, flags);
    return result;
}
EI_VERIFY(0x004E9A50, DeviceDrawMesh2Verify, 0, s_mesh2);
