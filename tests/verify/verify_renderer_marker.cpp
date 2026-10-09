// Checks for the selection marker (renderer_marker.cpp).
#include "verify.h"
#include "flat_draw_capture.h"

#include "../../src/game/model.h"

#include <cstdint>
#include <cstring>

// 00503A40: its only output is the flat geometry draw (004EA4A0). The original runs first with the
// draw recorded and skipped (flat_draw_capture.h), then ours, recorded and drawn; the two calls'
// arguments are compared: vertex positions within a few ULPs (CheckNear's tolerance), everything
// else exactly, and both results (1). Reached by every scenario that selects a unit in single
// player (ui_battle_commands, clickunit). Needs 004EA4A0's wrapper installed: without it nothing is
// recorded and the check fails with "not recorded".
static void* s_marker;
static int __fastcall DrawSelectionMarkerVerify(CSceneRenderer* renderer, void*, const Vector3* point, float size,
                                                const Vector3* axisX, const Vector3* axisY) {
    using Original = int(__fastcall*)(CSceneRenderer*, void*, const Vector3*, float, const Vector3*, const Vector3*);
    flatcapture::Recording& recording = flatcapture::Current();
    flatcapture::Call expected{}, actual{};
    recording.record = &expected;
    recording.skipDraw = true;
    int originalResult = reinterpret_cast<Original>(s_marker)(renderer, nullptr, point, size, axisX, axisY);
    recording.record = &actual;
    recording.skipDraw = false;
    int result = renderer->DrawSelectionMarker(point, size, axisX, axisY);
    recording.record = nullptr;

    bool ok = expected.seen && actual.seen && originalResult == result && expected.blocks == actual.blocks &&
              expected.indexCount == actual.indexCount && expected.colour == actual.colour &&
              expected.specular == actual.specular && expected.bounds == actual.bounds &&
              std::memcmp(&expected.zScale, &actual.zScale, 4) == 0 &&
              std::memcmp(expected.uvs, actual.uvs, sizeof(expected.uvs)) == 0 &&
              std::memcmp(expected.indices, actual.indices, sizeof(expected.indices)) == 0;
    int worst = -1;
    for (int b = 0; ok && b < expected.blocks && b < 4; ++b)
        for (int i = 0; i < 16; ++i)
            if (!ei::verify::NearlyEqual(expected.positions[b][i], actual.positions[b][i], 4, 1e-6f)) {
                ok = false;
                worst = b * 16 + i;
                break;
            }
    ei::verify::Check("DrawSelectionMarker", true, ok, "%s at (%g, %g, %g) size %g; float %d: %g vs %g",
                      expected.seen ? "mismatch" : "not recorded", point->x, point->y, point->z, size, worst,
                      worst >= 0 ? expected.positions[worst / 16][worst % 16] : 0.0f,
                      worst >= 0 ? actual.positions[worst / 16][worst % 16] : 0.0f);
    return result;
}
EI_VERIFY(0x00503A40, DrawSelectionMarkerVerify, 6, s_marker);
