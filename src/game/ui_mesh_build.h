#pragma once

// The interface sprites' shape builders (ui_mesh_build.cpp; see docs/notes/ui.md, "Sprite shapes").
// Each one fills a sprite (CUiMesh, ui.h) from scratch: its quad positions (in the sprite's own units:
// layout pixels / scale), the vertex records, the triangle indices, the texture coordinates (texels /
// 256, nudged by half a texel), the placement (position and the three scales) and hands the arrays to
// the renderer (Commit). They also size three per-vertex arrays (one block of 0x40 bytes: four 0s,
// four 0s, four 1s, four 1s; two with four entries each).

#include "ui.h"

class CUiMeshBuilder : public CUiMesh {
public:
    // A framed rectangle: an outer quad (left, top)-(right, bottom) and an inner
    // one `border` pixels in, 8 vertices and 24 indices forming the frame between them; texels
    // (u0, v0)-(u1, v1) for the outer edge, moved in proportionally for the inner one.
    void Framed(float depth, float scale, float left, float top, float right, float bottom, float border, int u0,
                int v0, int u1, int v1);
    // A disc of `count` points around the layout point (x, y), radius `radius`; texels on the circle of
    // radius `uvRadius` around (u, v); a fan of count - 2 triangles.
    void Disc(float depth, float scale, float x, float y, float radius, int count, int u, int v, int uvRadius);
    // A ring of `count` segments around (x, y) between `radius - width` and
    // `radius`: 4 vertices and 6 indices per segment (the last segment closes on vertices 0 and 1),
    // count / 2 quads; texels (u0, v0)-(u1, v1) for every segment.
    void Ring(float depth, float scale, float x, float y, float radius, int count, float width, int u0, int v0, int u1,
              int v1);
    // Only the per-vertex arrays: one block (four 0s, four 0s, four 1s, four 1s) and two with four
    // entries (the builders' common start).
    void ResetColours();
};
static_assert(sizeof(CUiMeshBuilder) == sizeof(CUiMesh));
