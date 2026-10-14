#pragma once

// The old renderer's line-strip draws (renderer_lines.cpp): 005045D0 (renderer slot 20) and
// 0049B4B0 (the renderer's line lists).

#include "lib/containers.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

class CSceneRenderer;       // model.h
class CTextureDevice;       // texture.h
class CTransformPipeline;   // model.h

// A pre-transformed vertex (FVF 0xC4: XYZRHW | DIFFUSE | SPECULAR).
struct LineVertex {
    float x, y, z, rhw;
    std::uint32_t colour, specular;
};
static_assert(sizeof(LineVertex) == 0x18);

// The integer screen points of 005045D0: x, y pairs.
struct ScreenPoints {
    const std::int32_t* xy;
    int count;
};

// The projected points of 0049B4B0: x, y, z, rhw each (the transform pipeline's output).
struct ProjectedPoints {
    const float (*xyzw)[4];
    int count;
};

// The vertex buffer of 0049B4B0 in the exe's data (007A9CE0; zeroed by 0049B460, freed at exit
// by 0049B490). Grows like an MFC array, without keeping its old contents.
struct LineBuffer {
    LineVertex* data;
    int size;
    int capacity;
    int growBy;
};
static_assert(sizeof(LineBuffer) == 0x10);
inline LineBuffer& LineVertices() { return *reinterpret_cast<LineBuffer*>(0x007A9CE0); }

// Four points of a line list as the transform object takes them (0x40 bytes).
struct LinePacket {
    float x[4];
    float y[4];
    float z[4];
    float w[4];
};
static_assert(sizeof(LinePacket) == 0x40);

// A line list (0x20 bytes; line_batch.cpp, A-E1): the figure parts' debug bounds (005A7290) and the
// world client's lists (00569D60). The points sit four to a packet; the unused points of the last
// packet repeat the last point.
struct LineBatch {
    int unknown_00;                          // -1
    lib::AlignedArray<LinePacket> packets;   // +0x04
    int count;                               // +0x14 points
    std::uint32_t colour;                    // +0x18
    int unknown_1C;

    // 0049B140 (RET 4). Appends the point (x, y, z; w 1), with a new packet when the last one is
    // full, and repeats it through the rest of its packet. Returns the new count.
    int AddPoint(const float* point);
    // 0049B780 (RET 8). Projects the packets with the top of the pipeline's clip stack (the
    // transform object's ProjectClipped into the work arrays below) and, when everything is on
    // screen (result 0), draws the strip in the list's colour (DeviceDrawLineStrip). Returns 1.
    int Draw(CTransformPipeline* pipeline, CTextureDevice* device);
};
static_assert(offsetof(LineBatch, count) == 0x14 && sizeof(LineBatch) == 0x20);

// The line lists' work arrays in the exe's data: the projected points (007A9CB8; x, y, z, rhw each,
// resized without keeping its contents) and their clip outcodes (007A9CC8, an MFC CByteArray).
struct ProjectedPoint {
    float x, y, z, rhw;
};
inline lib::AlignedArray<ProjectedPoint>& LineProjected() {
    return *reinterpret_cast<lib::AlignedArray<ProjectedPoint>*>(0x007A9CB8);
}
inline mfc::CArray<std::uint8_t>& LineOutcodes() { return *reinterpret_cast<mfc::CArray<std::uint8_t>*>(0x007A9CC8); }

// 005045D0 (thiscall on the renderer, RET 8).
int __fastcall RendererPolyline(CSceneRenderer* renderer, void*, const ScreenPoints* points, std::uint32_t colour);
// 0049B4B0 (fastcall: the device object, the points; the colour on the stack, RET 4).
long __fastcall DeviceDrawLineStrip(CTextureDevice* device, const ProjectedPoints* points, std::uint32_t colour);
