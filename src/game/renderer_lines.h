#pragma once

// The old renderer's line-strip draws (renderer_lines.cpp): the strip through integer points and
// the strip of the renderer's line lists.

#include "lib/containers.h"
#include <cstdint>
#include "lib/static_object.h"

#include <cstddef>
#include <vector>

class CTextureDevice;
class CTransformPipeline;

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
struct ProjectedPoint {
    float x, y, z, rhw;
};
struct ProjectedPoints {
    const ProjectedPoint* points;
    int count;
};

// The vertex buffer of the projected line strips, freed at exit.
extern std::vector<LineVertex> g_lineVertices;

// Four points of a line list as the transform object takes them.
struct LinePacket {
    float x[4];
    float y[4];
    float z[4];
    float w[4];
};
static_assert(sizeof(LinePacket) == 0x40);

// A line list (line_batch.cpp): the figure parts' debug bounds and the world client's lists. The points sit
// four to a packet; the unused points of the last packet repeat the last point.
struct LineBatch {
    std::vector<LinePacket> packets;
    int count;                               // points
    std::uint32_t colour;

    // Appends the point (x, y, z; w 1), with a new packet when the last one is
    // full, and repeats it through the rest of its packet. Returns the new count.
    int AddPoint(const float* point);
    // Projects the packets with the top of the pipeline's clip stack (the
    // transform object's ProjectClipped into the work arrays below) and, when everything is on
    // screen (result 0), draws the strip in the list's colour (DeviceDrawLineStrip). Returns 1.
    int Draw(CTransformPipeline* pipeline, CTextureDevice* device);
};

// The line lists' work arrays: the projected points (x, y, z, rhw each, resized without keeping its contents) and
// their clip outcodes (an MFC CByteArray built by a static initialiser).
extern std::vector<ProjectedPoint> g_lineProjected;
extern lib::StaticObject<std::vector<std::uint8_t>> g_lineOutcodes;
inline std::vector<std::uint8_t>& LineOutcodes() { return *g_lineOutcodes; }

// Draws the points as a connected line strip in `colour`.
long DeviceDrawLineStrip(CTextureDevice* device, const ProjectedPoints* points, std::uint32_t colour);
