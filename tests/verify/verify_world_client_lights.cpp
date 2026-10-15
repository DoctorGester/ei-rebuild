// Checks for the client world's light by the time of day (src/game/world_client_lights.cpp).
// UpdateLighting runs every frame from Render (and at each zone load), so every scenario with a
// zone reaches both functions; the cave branch only in a cave zone.
#include "verify.h"

#include "../../src/game/effects.h"
#include "../../src/game/model.h"
#include "../../src/game/originals_render.h"
#include "../../src/game/spell.h"
#include "../../src/game/world.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {
float& LastLitHour() { return *reinterpret_cast<float*>(0x0078FF38); }

bool Near(float a, float b) { return ei::verify::NearlyEqual(a, b, 4, 1e-6f); }
bool NearColour(const LightColour& a, const LightColour& b) { return Near(a.r, b.r) && Near(a.g, b.g) && Near(a.b, b.b); }
// Packed colours: each byte within 1 (a channel on a rounding edge of x * 255).
bool NearPacked(std::uint32_t a, std::uint32_t b) {
    for (int shift = 0; shift < 32; shift += 8)
        if (std::abs(int((a >> shift) & 0xFF) - int((b >> shift) & 0xFF)) > 1) return false;
    return true;
}

// What UpdateLighting writes, as far as it is not stack garbage (frameSun_'s fourth float).
struct LightState {
    LightColour ambient, skyAmbient, sun;
    int lightingMark;
    float lastLit;
    float colour[3];
    Vector3 direction;
    std::uint32_t fog;

    void Take(CWorldClient* client, CSceneRenderer* display) {
        ambient = client->frameAmbient_;
        skyAmbient = client->skyAmbient_;
        sun = client->frameSun_;
        lightingMark = client->lightingMark_;
        lastLit = LastLitHour();
        std::memcpy(colour, display->sun_->color_, sizeof(colour));
        direction = display->sun_->direction_;
        fog = display->fogColour_;
    }
    void Put(CWorldClient* client, CSceneRenderer* display) const {
        client->frameAmbient_ = ambient;
        client->skyAmbient_ = skyAmbient;
        client->frameSun_ = sun;
        client->lightingMark_ = lightingMark;
        LastLitHour() = lastLit;
        std::memcpy(display->sun_->color_, colour, sizeof(colour));
        display->sun_->direction_ = direction;
        display->fogColour_ = fog;
    }
};
} // namespace

// 00566360 UpdateLighting: PUSH EBP; MOV EBP,ESP; SUB ESP,0x58 (6 bytes). Differential: the
// state it writes is saved, the original runs with the terrain hidden (its sun call 006C8ED0
// spreads relighting over frames and is not idempotent), its results are recorded and the saved
// state put back; then ours (with the terrain). The renderer calls (depth range, sun, fog colour,
// sky lighting) run twice with the same values. Compared: the frame's ambient, sky ambient and sun
// colours, +0x6DC, the last lit hour, the sun's colour and direction (CheckNear's ULPs) and the
// fog colour (a byte may differ by 1). Return values are not read by the callers.
static int(__fastcall* s_updateLighting)(CWorldClient*);
static int __fastcall UpdateLightingVerify(CWorldClient* self) {
    auto* display = static_cast<CSceneRenderer*>(DisplayObject());
    if (!(display->flags_ & 1) || !display->sun_) return s_updateLighting(self);
    LightState before, theirs, ours;
    before.Take(self, display);
    Terrain* terrain = self->terrain_;
    self->terrain_ = nullptr;
    s_updateLighting(self);
    self->terrain_ = terrain;
    theirs.Take(self, display);
    before.Put(self, display);
    int result = self->UpdateLighting();
    ours.Take(self, display);
    bool ok = NearColour(theirs.ambient, ours.ambient) && NearColour(theirs.skyAmbient, ours.skyAmbient) &&
              Near(theirs.skyAmbient.a, ours.skyAmbient.a) && NearColour(theirs.sun, ours.sun) &&
              theirs.lightingMark == ours.lightingMark && Near(theirs.lastLit, ours.lastLit) &&
              Near(theirs.colour[0], ours.colour[0]) && Near(theirs.colour[1], ours.colour[1]) &&
              Near(theirs.colour[2], ours.colour[2]) && Near(theirs.direction.x, ours.direction.x) &&
              Near(theirs.direction.y, ours.direction.y) && Near(theirs.direction.z, ours.direction.z) &&
              NearPacked(theirs.fog, ours.fog);
    ei::verify::Check("CWorldClient::UpdateLighting", true, ok,
                      "cave %d sun %g %g %g vs %g %g %g, dir %g %g %g vs %g %g %g, fog %08X vs %08X, last %g vs %g",
                      self->cave_, theirs.sun.r, theirs.sun.g, theirs.sun.b, ours.sun.r, ours.sun.g, ours.sun.b,
                      theirs.direction.x, theirs.direction.y, theirs.direction.z, ours.direction.x, ours.direction.y,
                      ours.direction.z, theirs.fog, ours.fog, theirs.lastLit, ours.lastLit);
    return result;
}
EI_VERIFY(0x00566360, UpdateLightingVerify, 6, s_updateLighting);

// 00567680 UpdateExitMarkers: MOV EAX,FS:[0] (6 bytes). Differential: the markers' extents saved,
// the original run, its extents recorded and the old ones put back, then ours; compared bitwise.
static void(__fastcall* s_updateExitMarkers)(CWorldClient*);
static void __fastcall UpdateExitMarkersVerify(CWorldClient* self) {
    constexpr int kMaxMarkers = 64;
    const int count = self->countedA0_.GetSize();
    if (count > kMaxMarkers || count != self->exitCount_) {
        s_updateExitMarkers(self);
        return;
    }
    auto marker = [self](int i) { return reinterpret_cast<CParticleSource*>(self->countedA0_[i]); };
    Vector3 before[kMaxMarkers], theirs[kMaxMarkers];
    for (int i = 0; i < count; ++i)
        if (marker(i)) before[i] = marker(i)->extent;
    s_updateExitMarkers(self);
    for (int i = 0; i < count; ++i)
        if (marker(i)) {
            theirs[i] = marker(i)->extent;
            marker(i)->extent = before[i];
        }
    self->UpdateExitMarkers();
    int first = -1;
    for (int i = 0; i < count && first < 0; ++i)
        if (marker(i) && std::memcmp(&theirs[i], &marker(i)->extent, sizeof(Vector3)) != 0) first = i;
    ei::verify::Check("CWorldClient::UpdateExitMarkers", true, first < 0, "%d markers, first difference %d", count,
                      first);
}
EI_VERIFY(0x00567680, UpdateExitMarkersVerify, 6, s_updateExitMarkers);
