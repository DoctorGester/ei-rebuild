// The selection marker (00503A40, thiscall on the renderer at 007AF060, RET 0x10): a flat arrow
// under each selected unit, drawn by the world's draw pass (00569E40) in single-player mode. Its six
// corners lie 0.3 above the given point, in the plane of the unit's x and y axes; the device's
// flat geometry draw (004EA4A0) projects them with the scene's matrices. See docs/notes/renderer.md.
#include "figure_mesh.h"   // VertexPacket
#include "model.h"         // CSceneRenderer

#include "ei/hook.h"
#include "lib/vecmath.h"
#include "originals_render.h"

#include <bit>
#include <cstdint>

namespace {

// The arrow's proportions (the original's constants 0073D9A4, 0073BAA4, 0073CDF8, 0073BAA0,
// 0073BF34 and 0073C54C): the unit length is a third of the size.
const float kThird = std::bit_cast<float>(0x3EAAAAABu);   // 1/3
constexpr float kWing = 3.0f;          // half the width of the arrow's back
constexpr float kShoulder = 1.4f;      // the shoulders, along y
constexpr float kNotch = 4.0f;         // the notch in the back, along -y
constexpr float kTip = 5.0f;           // the tip, along -y
constexpr float kLift = 0.3f;          // above the point

// The draw: 8 vertices in two blocks (the last two unused), six triangles.
constexpr int kBlocks = 2;
constexpr int kVertices = kBlocks * 4;
constexpr std::uint32_t kColour = 0x8000FF00;     // half-transparent green
constexpr std::uint32_t kSpecular = 0xFF000000;
constexpr std::uint16_t kIndices[18] = {0, 4, 3, 0, 1, 4, 1, 5, 4, 1, 2, 5, 2, 3, 5, 2, 0, 3};

void SetVertex(VertexPacket* blocks, int index, float x, float y, float z) {
    float(&block)[4][4] = blocks[index >> 2].block;
    block[0][index & 3] = x;
    block[1][index & 3] = y;
    block[2][index & 3] = z;
    block[3][index & 3] = 1.0f;
}

} // namespace

int CSceneRenderer::DrawSelectionMarker(const Vector3* point, float size, const Vector3* axisX, const Vector3* axisY) {
    const Vector3& p = *point;
    const Vector3& x = *axisX;
    const Vector3& y = *axisY;
    float unit = size * kThird;
    float twice = unit + unit;
    float z = p.z + kLift;

    // Each corner as the original sums it: the scaled axes are rounded to floats first.
    Vector3 across = (unit * kWing) * x;
    Vector3 back = twice * y;
    Vector3 shoulder = (unit * kShoulder) * y;
    Vector3 side = twice * x;
    Vector3 corners[6] = {
        (p + across) + back,                // 0, 1: the back's ends
        (p - across) + back,
        p - (unit * kTip) * y,              // 2: the tip
        shoulder + (side + p),              // 3, 4: the shoulders
        shoulder + (p - side),
        p - (unit * kNotch) * y,            // 5: the notch
    };

    VertexPacket blocks[kBlocks];
    for (int i = 0; i < 6; ++i) SetVertex(blocks, i, corners[i].x, corners[i].y, z);
    SetVertex(blocks, 6, p.x, p.y, z);
    SetVertex(blocks, 7, p.x, p.y, z);
    float uvs[kVertices][2] = {};
    std::uint16_t indices[18];
    for (int i = 0; i < 18; ++i) indices[i] = kIndices[i];
    DeviceDrawFlatOriginal(device_, nullptr, blocks, uvs, kBlocks, indices, 18, kColour, kSpecular, nullptr, 1.0f);
    return 1;
}
EI_REPLACE_METHOD(0x00503A40, CSceneRenderer::DrawSelectionMarker);
