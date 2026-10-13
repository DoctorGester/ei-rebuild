// Checks for weather_particles.cpp (A-S13): the rain draw 004E4240 and the snow draw 004E4C80.
// Main thread, every frame while it rains or snows (effects: SetWeather "rain", then "snow").
//  - Every other call side by side: ours builds the vertices (BuildRain / BuildSnow: everything but
//    the draw) and what it leaves is noted; then the original builds them again from the same
//    state (it starts from the wind, the time, the camera and the tables, none of which the build
//    changes) and draws. Compared bit for bit: the number of corners, each corner's x, y and z,
//    the particles' texture frames, the outcodes, the number of vertices and each vertex's x, y, z,
//    rhw, u and v. The colours are not compared: neither version writes them, and the original's
//    fog pass changes the specular. The first comparison with particles on screen is logged with
//    its counts ("weather-particles:" in ei_hook.log).
//  - The other calls run ours whole, so the draw half runs too: ours only. Checked: the result,
//    three vertices per particle drawn, the device's counts raised by exactly this draw, the
//    draw's result.
#include "verify.h"

#include "../../src/game/clip.h"
#include "../../src/game/effects.h"
#include "../../src/game/figure_mesh.h"
#include "../../src/game/model.h"
#include "../../src/game/texture.h"
#include "../../src/hook/log.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

// What a build leaves.
struct Built {
    int corners = 0;
    int packets = 0;
    int projected = 0;
    int vertexCount = 0;
    std::vector<float> positions;         // x, y, z per corner
    std::vector<std::uint8_t> frames;     // per particle
    std::vector<std::uint8_t> outcodes;   // per corner
    std::vector<float> vertices;          // x, y, z, rhw, u, v per vertex
};

Built Note(const CRainSnow* layer, int cornersPerParticle) {
    Built built;
    built.corners = layer->particleCount;
    built.packets = layer->positions.size;
    built.projected = layer->projected.size;
    built.vertexCount = layer->vertices.size;
    for (int i = 0; i < built.corners; ++i) {
        const VertexPacket& packet = layer->positions.data[i >> 2];
        for (int axis = 0; axis < 3; ++axis) built.positions.push_back(packet.block[axis][i & 3]);
    }
    const int particles = built.corners / cornersPerParticle;
    built.frames.assign(layer->frames.data, layer->frames.data + particles);
    built.outcodes.assign(layer->outcodes.data, layer->outcodes.data + built.corners);
    for (int i = 0; i < built.vertexCount; ++i) {
        const TLVertex& vertex = layer->vertices.data[i];
        for (float value : {vertex.x, vertex.y, vertex.z, vertex.rhw, vertex.u, vertex.v}) built.vertices.push_back(value);
    }
    return built;
}

// How many of the floats differ in their bits.
int Different(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return -1;
    int different = 0;
    for (std::size_t i = 0; i < a.size(); ++i) different += std::memcmp(&a[i], &b[i], sizeof(float)) != 0;
    return different;
}

using Draw = int(__fastcall*)(CRainSnow*, void*, CSceneRenderer*);

struct Seen {
    unsigned calls;
    bool logged;
};

int Verify(const char* name, CRainSnow* self, CSceneRenderer* renderer, Draw original, int (CRainSnow::*build)(CSceneRenderer*),
           int (CRainSnow::*draw)(CSceneRenderer*), int cornersPerParticle, Seen& seen) {
    if (seen.calls++ & 1) {
        const CTextureDevice* device = renderer->device_;
        const int vertexCount = device->vertexCount_, primitiveCount = device->primitiveCount_, drawCount = device->drawCount_;
        const int result = (self->*draw)(renderer);
        const int drawn = self->vertices.size;
        const bool right = result == 1 && drawn % 3 == 0 && drawn <= self->particleCount * 3 / cornersPerParticle &&
                           device->vertexCount_ == vertexCount + drawn && device->primitiveCount_ == primitiveCount + drawn / 3 &&
                           device->drawCount_ == drawCount + (drawn != 0 ? 1 : 0) && (drawn == 0 || device->lastLoadResult_ == 0);
        ei::verify::Check(name, true, right, "ours only: result %d, %d vertices of %d corners, draw result %08X", result, drawn,
                          self->particleCount, static_cast<unsigned>(device->lastLoadResult_));
        return result;
    }
    const int built = (self->*build)(renderer);
    const Built ours = Note(self, cornersPerParticle);
    const int result = original(self, nullptr, renderer);
    const Built theirs = Note(self, cornersPerParticle);
    const int positions = Different(theirs.positions, ours.positions), vertices = Different(theirs.vertices, ours.vertices);
    const bool same = result == 1 && built == ours.vertexCount && ours.corners == theirs.corners && ours.packets == theirs.packets &&
                      ours.projected == theirs.projected && ours.vertexCount == theirs.vertexCount && positions == 0 &&
                      vertices == 0 && ours.frames == theirs.frames && ours.outcodes == theirs.outcodes;
    ei::verify::Check(name, true, same, "corners %d / %d, vertices %d / %d, floats that differ: %d of the corners', %d of the vertices'",
                      theirs.corners, ours.corners, theirs.vertexCount, ours.vertexCount, positions, vertices);
    if (!seen.logged && theirs.vertexCount != 0) {
        seen.logged = true;
        ei::Log("weather-particles: %s compared with %d corners and %d vertices (density %d)", name, theirs.corners,
                theirs.vertexCount, self->density);
    }
    return result;
}

// 004E4240: PUSH EBP (1), MOV EBP, ESP (2), SUB ESP, 0x60 (3).
Draw s_drawRain;
int __fastcall DrawRainVerify(CRainSnow* self, void*, CSceneRenderer* renderer) {
    static Seen seen;
    return Verify("CRainSnow::DrawRain (004E4240)", self, renderer, s_drawRain, &CRainSnow::BuildRain, &CRainSnow::DrawRain, 3, seen);
}

// 004E4C80: PUSH EBP (1), MOV EBP, ESP (2), SUB ESP, 0x64 (3).
Draw s_drawSnow;
int __fastcall DrawSnowVerify(CRainSnow* self, void*, CSceneRenderer* renderer) {
    static Seen seen;
    return Verify("CRainSnow::DrawSnow (004E4C80)", self, renderer, s_drawSnow, &CRainSnow::BuildSnow, &CRainSnow::DrawSnow, 1, seen);
}

} // namespace

EI_VERIFY(0x004E4240, DrawRainVerify, 6, s_drawRain);
EI_VERIFY(0x004E4C80, DrawSnowVerify, 6, s_drawSnow);
