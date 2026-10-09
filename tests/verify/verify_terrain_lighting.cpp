// Verify wrappers for the terrain vertex lighting (terrain_lighting.cpp). Every scenario reaches them:
// each sector is lit once when it is first drawn (SectorData::Ensure with 4), and the point lights
// relight their rectangle (006CAB50, with skipSun) when they move. Each runs both versions on the
// same object, restoring what the first one changed before the second runs. The colour
// bytes come from truncated floats: a value on a rounding edge may give a rare one-step mismatch.
#include "verify.h"

#include "../../src/game/terrain_sector.h"

#include <cstring>
#include <vector>

using namespace terrain;

namespace {

constexpr int kVertices = 33 * 33;

// ---- SectorData::LightVertex -------------------------------------------------------------------
// 006C31C0: PUSH EBP; MOV EBP,ESP; SUB ESP,0xB8 (9 bytes). Writes the record's colour / specular
// words and may set the sector's hasSpecular. Ours first, then (all three restored) the original,
// whose results stay, so the original Light run below sees original per-vertex results.
void(__fastcall* s_lightVertex)(SectorData*, void*, const SectorPosition*, SectorVertex*, TerrainFields*, int);

void __fastcall LightVertexVerify(SectorData* self, void*, const SectorPosition* position, SectorVertex* record,
                                  TerrainFields* owner, int layer) {
    std::uint32_t colour = record->colour, specular = record->specular;
    std::uint8_t flag = self->hasSpecular;
    self->LightVertex(position, record, owner, layer);
    std::uint32_t colourOurs = record->colour, specularOurs = record->specular;
    std::uint8_t flagOurs = self->hasSpecular;
    record->colour = colour;
    record->specular = specular;
    self->hasSpecular = flag;
    s_lightVertex(self, nullptr, position, record, owner, layer);
    ei::verify::Check("SectorData::LightVertex", record->colour, colourOurs, "colour, layer %d", layer);
    ei::verify::Check("SectorData::LightVertex", record->specular, specularOurs, "specular, layer %d", layer);
    ei::verify::Check("SectorData::LightVertex", self->hasSpecular, flagOurs, "hasSpecular");
}
EI_VERIFY(0x006C31C0, LightVertexVerify, 9, s_lightVertex);

// ---- SectorData::ShadeSun ----------------------------------------------------------------------
// 006C36B0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x34 (6 bytes). Writes the record's lit colour: ours
// first, restored, then the original (its result stays). Both versions run the shadow test 006C92C0, which may load the sectors the ray
// crosses (the second run then finds them cached; the vertices it reads are the same).
void(__fastcall* s_shadeSun)(SectorData*, void*, CLight*, const SectorPosition*, SectorVertex*, TerrainFields*, int);

void __fastcall ShadeSunVerify(SectorData* self, void*, CLight* sun, const SectorPosition* position,
                               SectorVertex* record, TerrainFields* owner, int layer) {
    Vector3 lit, ours;
    std::memcpy(&lit, &record->lit, sizeof(lit));
    self->ShadeSun(sun, position, record, owner, layer);
    std::memcpy(&ours, &record->lit, sizeof(ours));
    std::memcpy(&record->lit, &lit, sizeof(lit));
    s_shadeSun(self, nullptr, sun, position, record, owner, layer);
    ei::verify::CheckNear("SectorData::ShadeSun", record->lit.x, ours.x, "r, layer %d", layer);
    ei::verify::CheckNear("SectorData::ShadeSun", record->lit.y, ours.y, "g, layer %d", layer);
    ei::verify::CheckNear("SectorData::ShadeSun", record->lit.z, ours.z, "b, layer %d", layer);
}
EI_VERIFY(0x006C36B0, ShadeSunVerify, 6, s_shadeSun);

// ---- SectorData::Light -------------------------------------------------------------------------
// 006C3A00: SUB ESP,0x10; PUSH EBX; PUSH EBP (5 bytes). The original first (its per-vertex calls go
// through the two wrappers above), then every record, the sector's hasSpecular / built /
// relightCountdown and the owner's lighting fields are restored and ours runs; the records' colour,
// specular and lit colour compared (mismatch count and the first one reported).
void(__fastcall* s_light)(SectorData*, void*, CLight*, TerrainFields*, int, int, int, int, int);

void __fastcall LightVerify(SectorData* self, void*, CLight* sun, TerrainFields* owner, int skipSun, int x0, int y0,
                            int x1, int y1) {
    int count = self->records.size;
    std::vector<SectorVertex> before(self->records.data, self->records.data + count);
    std::uint8_t flag = self->hasSpecular;
    std::uint32_t built = self->built;
    int stamp = self->relightCountdown;
    SectorData* sector = owner->lightingSector;
    int lx = owner->lightingX, ly = owner->lightingY;

    s_light(self, nullptr, sun, owner, skipSun, x0, y0, x1, y1);
    std::vector<SectorVertex> original(self->records.data, self->records.data + count);
    std::uint8_t flagOriginal = self->hasSpecular;
    std::uint32_t builtOriginal = self->built;
    int stampOriginal = self->relightCountdown;
    SectorData* sectorOriginal = owner->lightingSector;
    int lxOriginal = owner->lightingX, lyOriginal = owner->lightingY;

    std::memcpy(static_cast<void*>(self->records.data), before.data(), count * sizeof(SectorVertex));
    self->hasSpecular = flag;
    self->built = built;
    self->relightCountdown = stamp;
    owner->lightingSector = sector;
    owner->lightingX = lx;
    owner->lightingY = ly;
    self->Light(sun, owner, skipSun, x0, y0, x1, y1);

    int mismatches = 0, first = -1;
    for (int i = 0; i < count; ++i) {
        const SectorVertex& a = original[i];
        const SectorVertex& b = self->records.data[i];
        bool same = a.colour == b.colour && a.specular == b.specular;
        same = same && ei::verify::NearlyEqual(a.lit.x, b.lit.x, 4, 1e-6f) &&
               ei::verify::NearlyEqual(a.lit.y, b.lit.y, 4, 1e-6f) && ei::verify::NearlyEqual(a.lit.z, b.lit.z, 4, 1e-6f);
        if (!same && mismatches++ == 0) first = i;
    }
    bool state = flagOriginal == self->hasSpecular && builtOriginal == self->built &&
                 stampOriginal == self->relightCountdown && sectorOriginal == owner->lightingSector &&
                 lxOriginal == owner->lightingX && lyOriginal == owner->lightingY;
    ei::verify::Check("SectorData::Light", 0, mismatches, "(%d, %d)-(%d, %d) skip %d: first record %d", x0, y0, x1, y1,
                      skipSun, first);
    ei::verify::Check("SectorData::Light", true, state, "flags / owner fields");
}
EI_VERIFY(0x006C3A00, LightVerify, 5, s_light);

} // namespace
