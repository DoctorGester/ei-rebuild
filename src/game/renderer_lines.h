#pragma once

// The old renderer's line-strip draws (renderer_lines.cpp): 005045D0 (renderer slot 20) and
// 0049B4B0 (the renderer's line lists).

#include <cstdint>

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

// 005045D0 (thiscall on the renderer, RET 8).
int __fastcall RendererPolyline(char* renderer, void*, const ScreenPoints* points, std::uint32_t colour);
// 0049B4B0 (fastcall: the device object, the points; the colour on the stack, RET 4).
long __fastcall DeviceDrawLineStrip(char* device, const ProjectedPoints* points, std::uint32_t colour);
