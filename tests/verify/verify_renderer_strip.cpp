// Checks for the renderer's ribbon draw (renderer_strip.cpp).
#include "verify.h"

#include "../../src/game/clip.h"
#include "../../src/game/model.h"
#include "../../src/game/texture.h"

#include <cstdint>
#include <cstring>

namespace {
struct AlignedArray {
    char* data;
    int size;
    int capacity;
    int growBy;
};
} // namespace

float* __fastcall StripDirection(float* out, int index, int mode, const void* points);
int __fastcall DrawRibbon(CSceneRenderer* renderer, void*, const int* points, float width, int mode, float uLength,
                          float vStart, float vSize);

// 00502B90: MOV EAX,ECX; PUSH ESI; MOV ECX,[ESP+8] (7 bytes). Pure: both directions compared.
static float*(__fastcall* s_direction)(float*, int, int, const void*);
static float* __fastcall StripDirectionVerify(float* out, int index, int mode, const void* points) {
    float expected[2];
    s_direction(expected, index, mode, points);
    StripDirection(out, index, mode, points);
    ei::verify::Check("StripDirection.x", expected[0], out[0], "index %d mode %d", index, mode);
    ei::verify::Check("StripDirection.y", expected[1], out[1], "index %d mode %d", index, mode);
    return out;
}
EI_VERIFY(0x00502B90, StripDirectionVerify, 7, s_direction);

// 00502AC0: PUSH ECX; MOV EAX,[ESP+0Ch] (5 bytes). The original writes its two vertices into a scratch
// array, ours into the real one; the 64 bytes are compared.
static void(__fastcall* s_section)(const TLVertex*, const float*, TLVertex**, int*, float);
void __fastcall EmitSection(const TLVertex* src, const float* direction, TLVertex** array,
                                                  int* count, float width);
static void __fastcall EmitSectionVerify(const TLVertex* src, const float* direction, TLVertex** array, int* count,
                                         float width) {
    TLVertex scratch[2];
    TLVertex* scratchArray = scratch;
    int scratchCount = 0;
    s_section(src, direction, &scratchArray, &scratchCount, width);
    int at = *count;
    EmitSection(src, direction, array, count, width);
    bool same = std::memcmp(scratch, *array + at, sizeof(scratch)) == 0 && *count == at + 2 && scratchCount == 2;
    ei::verify::Check("EmitSection", true, same, "at %d width %g", at, static_cast<double>(width));
}
EI_VERIFY(0x00502AC0, EmitSectionVerify, 5, s_section);

// 00502A20 draws: ours only; the draw call counter goes up by one.
static void* s_drawTl;
static int __fastcall DrawTlTrianglesVerify(CTextureDevice* device, void*, TLVertex* vertices, int vertexCount,
                                            std::uint16_t* indices, int indexCount) {
    int calls = device->drawCount_;
    int result = device->DrawTlTriangles(vertices, vertexCount, indices, indexCount);
    ei::verify::Check("DrawTlTriangles", calls + 1, device->drawCount_, "vertices %d indices %d", vertexCount,
                      indexCount);
    return result;
}
EI_VERIFY(0x00502A20, DrawTlTrianglesVerify, 0, s_drawTl);

// 00501D90 draws: ours only; the result is 0 or 1 and the index list (007AF030) holds whole quads.
static void* s_ribbon;
static int __fastcall DrawRibbonVerify(CSceneRenderer* renderer, void*, const int* points, float width, int mode,
                                       float uLength, float vStart, float vSize) {
    int result = DrawRibbon(renderer, nullptr, points, width, mode, uLength, vStart, vSize);
    int indices = reinterpret_cast<const AlignedArray*>(0x007AF030)->size;
    bool ok = (result == 0 || result == 1) && indices % 6 == 0;
    ei::verify::Check("DrawRibbon", true, ok, "points %d mode %d indices %d", points[1], mode, indices);
    return result;
}
EI_VERIFY(0x00501D90, DrawRibbonVerify, 0, s_ribbon);
