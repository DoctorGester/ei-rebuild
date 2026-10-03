// Checks for the animation tracks (map_object_animation.cpp). Evaluate writes only the track's
// time, position and rotation and the morphed vertices: ours, those saved and restored, the
// original; compared.
#include "verify.h"

#include "../../src/game/map_object_animation.h"

#include <cstring>

namespace {
constexpr int kMaxVertices = 1024;   // larger morphs: only the track's own values are compared
}

// 0050ACD0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x38 (6 bytes).
static void(__fastcall* s_evaluate)(AnimationTrack*, void*, float);
static void __fastcall EvaluateVerify(AnimationTrack* self, void*, float time) {
    constexpr std::size_t kValues = 0x6C - 0x48;
    auto* values = reinterpret_cast<unsigned char*>(&self->time);
    unsigned char before[kValues];
    unsigned char ours[kValues];
    float verticesBefore[kMaxVertices * 3];
    float verticesOurs[kMaxVertices * 3];
    bool morphs = self->frameCount != 0 && self->vertices && self->vertexCount > 0 && self->vertexCount <= kMaxVertices;
    std::size_t vertexBytes = morphs ? static_cast<std::size_t>(self->vertexCount) * 12 : 0;
    std::memcpy(before, values, kValues);
    if (vertexBytes) std::memcpy(verticesBefore, self->vertices, vertexBytes);
    self->Evaluate(time);
    std::memcpy(ours, values, kValues);
    if (vertexBytes) std::memcpy(verticesOurs, self->vertices, vertexBytes);
    std::memcpy(values, before, kValues);
    if (vertexBytes) std::memcpy(self->vertices, verticesBefore, vertexBytes);
    s_evaluate(self, nullptr, time);
    bool same = std::memcmp(ours, values, kValues) == 0 &&
                (!vertexBytes || std::memcmp(verticesOurs, self->vertices, vertexBytes) == 0);
    ei::verify::Check("AnimationTrack::Evaluate", true, same, "time %g, %d vertices", time, self->vertexCount);
}
EI_VERIFY(0x0050ACD0, EvaluateVerify, 6, s_evaluate);
