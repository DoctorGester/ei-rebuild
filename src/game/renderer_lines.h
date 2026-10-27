#pragma once

// The old renderer's line-strip draws (renderer_lines.cpp): the strip through integer points and
// the strip of the renderer's line lists.

#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/static_object.h"
#include "game_data.h"

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

// The integer screen points: x, y pairs.
struct ScreenPoints {
    const std::int32_t* xy;
    int count;
};

// The projected points: x, y, z, rhw each (the transform pipeline's output).
struct ProjectedPoints {
    const float (*xyzw)[4];
    int count;
};

// The vertex buffer in the exe's data zeroed, freed at exit
// Grows like an MFC array, without keeping its old contents.
struct LineBuffer {
    LineVertex* data;
    int size;
    int capacity;
    int growBy;
};
static_assert(sizeof(LineBuffer) == 0x10);
extern LineBuffer g_lineVertices;
inline LineBuffer& LineVertices() { return g_lineVertices; }

// Four points of a line list as the transform object takes them (0x40 bytes).
struct LinePacket {
    float x[4];
    float y[4];
    float z[4];
    float w[4];
};
static_assert(sizeof(LinePacket) == 0x40);

// A line list (0x20 bytes; line_batch.cpp): the figure parts' debug bounds and the
// world client's lists. The points sit four to a packet; the unused points of the last
// packet repeat the last point.
struct LineBatch {
    int spare00;                             // +0x00 -1 (the destructor); nothing reads it
    lib::AlignedArray<LinePacket> packets;   // +0x04
    int count;                               // +0x14 points
    std::uint32_t colour;                    // +0x18
    int spare1C;                             // +0x1C nothing reads it

    // Appends the point (x, y, z; w 1), with a new packet when the last one is
    // full, and repeats it through the rest of its packet. Returns the new count.
    int AddPoint(const float* point);
    // Projects the packets with the top of the pipeline's clip stack (the
    // transform object's ProjectClipped into the work arrays below) and, when everything is on
    // screen (result 0), draws the strip in the list's colour (DeviceDrawLineStrip). Returns 1.
    int Draw(CTransformPipeline* pipeline, CTextureDevice* device);
};
static_assert(offsetof(LineBatch, count) == 0x14 && sizeof(LineBatch) == 0x20);

// The line lists' work arrays: the projected points (x, y, z, rhw each, resized without keeping its contents) and
// their clip outcodes (an MFC CByteArray built by a static initialiser).
struct ProjectedPoint {
    float x, y, z, rhw;
};
extern lib::AlignedArray<ProjectedPoint> g_lineProjected;
inline lib::AlignedArray<ProjectedPoint>& LineProjected() { return g_lineProjected; }
extern lib::StaticObject<mfc::CArray<std::uint8_t>> g_lineOutcodes;
inline mfc::CArray<std::uint8_t>& LineOutcodes() { return *g_lineOutcodes; }

// (fastcall: the device object, the points; the colour on the stack).
long __fastcall DeviceDrawLineStrip(CTextureDevice* device, const ProjectedPoints* points, std::uint32_t colour);
