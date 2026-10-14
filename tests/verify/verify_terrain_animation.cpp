// Verify wrappers for the landscape's per-frame work (terrain_animation.cpp). Every scenario reaches
// them: the client world's update runs Terrain::RunTick each frame (zone1 has animated tiles and
// water), each newly lit sector goes through the relight countdown, and SetSun runs at zone load and
// with every hour of game time (world_client_lights.cpp).
#include "verify.h"

#include "../../src/game/random.h"
#include "../../src/game/terrain.h"
#include "../../src/game/terrain_sector.h"

#include <cstring>
#include <vector>

using namespace terrain;

namespace {

constexpr int kVertices = 33 * 33;

struct RandomState {
    std::vector<std::uint8_t> bytes;
    void Save() {
        bytes.resize(sizeof(IsaacRandom));
        std::memcpy(bytes.data(), &IsaacRandom::Global(), sizeof(IsaacRandom));
    }
    void Restore() const { std::memcpy(&IsaacRandom::Global(), bytes.data(), sizeof(IsaacRandom)); }
    bool Same() const { return std::memcmp(bytes.data(), &IsaacRandom::Global(), sizeof(IsaacRandom)) == 0; }
};

// ---- SectorData::ResetFields -------------------------------------------------------------------
// 006C3D70: OR EDX,-1; XOR EAX,EAX (5 bytes). Only writes fields: the original on a raw copy of
// the sector, ours on the sector, all 0x394 bytes compared.
void(__fastcall* s_resetFields)(SectorData*, void*);

void __fastcall ResetFieldsVerify(SectorData* self, void*) {
    std::vector<std::uint8_t> scratch(sizeof(SectorData));
    std::memcpy(scratch.data(), self, sizeof(SectorData));
    s_resetFields(reinterpret_cast<SectorData*>(scratch.data()), nullptr);
    self->ResetFields();
    ei::verify::Check("SectorData::ResetFields", 0, std::memcmp(scratch.data(), self, sizeof(SectorData)),
                      "sector bytes");
}
EI_VERIFY(0x006C3D70, ResetFieldsVerify, 5, s_resetFields);

// ---- SectorData::Clear -------------------------------------------------------------------------
// Frees the records and per-page arrays: ours only, then the arrays checked empty and the sector
// reset.
void(__fastcall* s_clear)(SectorData*, void*);

void __fastcall ClearVerify(SectorData* self, void*) {
    self->Clear();
    bool empty = self->records.data == nullptr && self->records.size == 0 && self->records.maxSize == 0;
    for (int q = 0; q < 4; ++q) {
        for (int i = 0; i < self->tileFlags[q].size; ++i) empty = empty && self->tileFlags[q][i].data == nullptr;
        for (int i = 0; i < self->tiles[q].size; ++i) empty = empty && self->tiles[q][i].data == nullptr;
    }
    ei::verify::Check("SectorData::Clear", true, empty, "arrays emptied");
    ei::verify::Check("SectorData::Clear", true, self->sx == -1 && self->built == 0 && self->clipMode == 2,
                      "fields reset");
}
EI_VERIFY(0x006C3C80, ClearVerify, 0, s_clear);

// ---- SectorData::ScheduleRelight / RelightTick -----------------------------------------------------
// 006C3E30 / 006C3E50: MOV EAX,[ECX+0x388] (6 bytes). The original first, its countdown and built
// flags kept aside and the fields restored, then ours; both compared.
void(__fastcall* s_scheduleRelight)(SectorData*, void*, int);

void __fastcall ScheduleRelightVerify(SectorData* self, void*, int frames) {
    int before = self->relightCountdown;
    s_scheduleRelight(self, nullptr, frames);
    int original = self->relightCountdown;
    self->relightCountdown = before;
    self->ScheduleRelight(frames);
    ei::verify::Check("SectorData::ScheduleRelight", original, self->relightCountdown, "%d frames from %d", frames,
                      before);
}
EI_VERIFY(0x006C3E30, ScheduleRelightVerify, 6, s_scheduleRelight);

void(__fastcall* s_relightTick)(SectorData*, void*);

void __fastcall RelightTickVerify(SectorData* self, void*) {
    int before = self->relightCountdown;
    std::uint32_t built = self->built;
    s_relightTick(self, nullptr);
    int original = self->relightCountdown;
    std::uint32_t builtOriginal = self->built;
    self->relightCountdown = before;
    self->built = built;
    self->RelightTick();
    ei::verify::Check("SectorData::RelightTick", original, self->relightCountdown, "countdown from %d", before);
    ei::verify::Check("SectorData::RelightTick", builtOriginal, self->built, "built from %d", before);
}
EI_VERIFY(0x006C3E50, RelightTickVerify, 6, s_relightTick);

// ---- SectorData::Animate -----------------------------------------------------------------------
// 006C3E80: PUSH EBP; MOV EBP,ESP; SUB ESP,0x6C (6 bytes). The original first; the animated tiles
// (frames and their 9 texture coordinates), the water vertices, the water records' specular words
// and the random generator are kept, restored, and ours runs; all compared. The wave indices come
// from FISTPs of rounded products: a value on a .5 edge may give a rare mismatch.
void(__fastcall* s_animate)(SectorData*, void*, TerrainFields*, int);

struct AnimateState {
    std::vector<int> frames;
    std::vector<TileVertex> uv;
    std::vector<SectorPosition> water;
    std::vector<std::uint32_t> specular;

    void Save(SectorData* s) {
        frames.clear();
        uv.clear();
        for (int i = 0; i < s->animated.size; ++i) {
            frames.push_back(s->animated[i].frame);
            uv.insert(uv.end(), s->animated[i].vertices, s->animated[i].vertices + 9);
        }
        water.clear();
        specular.clear();
        if (s->hasWater && s->vertices.size >= 2 * kVertices && s->records.size >= 2 * kVertices) {
            water.assign(&s->vertices[kVertices], &s->vertices[kVertices] + kVertices);
            for (int i = 0; i < kVertices; ++i) specular.push_back(s->records[kVertices + i].specular);
        }
    }
    void Restore(SectorData* s) const {
        for (int i = s->animated.size - 1; i >= 0; --i) {
            s->animated[i].frame = frames[i];
            std::memcpy(s->animated[i].vertices, &uv[i * 9], 9 * sizeof(TileVertex));
        }
        if (!water.empty()) {
            std::memcpy(&s->vertices[kVertices], water.data(), kVertices * sizeof(SectorPosition));
            for (int i = 0; i < kVertices; ++i) s->records[kVertices + i].specular = specular[i];
        }
    }
};

void __fastcall AnimateVerify(SectorData* self, void*, TerrainFields* owner, int frame) {
    AnimateState before, original, ours;
    RandomState random, randomOriginal;
    before.Save(self);
    random.Save();
    s_animate(self, nullptr, owner, frame);
    original.Save(self);
    randomOriginal.Save();
    before.Restore(self);
    random.Restore();
    self->Animate(owner, frame);
    ours.Save(self);

    int tiles = 0;
    for (std::size_t i = 0; i < original.frames.size(); ++i) {
        if (original.frames[i] != ours.frames[i] ||
            std::memcmp(&original.uv[i * 9], &ours.uv[i * 9], 9 * sizeof(TileVertex)) != 0) {
            ++tiles;
        }
    }
    int positions = 0, speculars = 0, first = -1;
    for (std::size_t i = 0; i < original.water.size(); ++i) {
        const Vector3& o = original.water[i];
        const Vector3& u = ours.water[i];
        bool same = ei::verify::NearlyEqual(o.x, u.x, 4, 1e-5f) && ei::verify::NearlyEqual(o.y, u.y, 4, 1e-5f) &&
                    ei::verify::NearlyEqual(o.z, u.z, 4, 1e-5f);
        if (!same && positions++ == 0) first = static_cast<int>(i);
        if (original.specular[i] != ours.specular[i]) ++speculars;
    }
    ei::verify::Check("SectorData::Animate", 0, tiles, "animated tiles, frame %d", frame);
    ei::verify::Check("SectorData::Animate", 0, positions, "water vertices (first %d), frame %d", first, frame);
    ei::verify::Check("SectorData::Animate", 0, speculars, "crest speculars, frame %d", frame);
    ei::verify::Check("SectorData::Animate", true, randomOriginal.Same(), "random generator");
}
EI_VERIFY(0x006C3E80, AnimateVerify, 6, s_animate);

// ---- Terrain::SetSun -----------------------------------------------------------------------------
// 006C8ED0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x18 (6 bytes). The original first; the terrain's
// ambient, ray steps and sun, and every cached sector's built flags and relight countdown (in cache
// order) are kept, restored, and ours runs; all compared. Both use the original's static sort
// array (007C8938).
void(__fastcall* s_setSun)(Terrain*, void*, CLight*, const Vector4*);

struct SunState {
    std::vector<std::uint8_t> fields;   // +0x224..+0x24C
    CLight* sun = nullptr;
    std::vector<std::uint32_t> built;
    std::vector<int> countdown;

    void Save(Terrain* t) {
        auto* bytes = reinterpret_cast<std::uint8_t*>(&t->ambient);
        fields.assign(bytes, bytes + 0x28);
        sun = t->sun;
        built.clear();
        countdown.clear();
        for (Terrain::SectorCache::Node* n = t->sectorCache.head; n; n = n->next) {
            built.push_back(n->data->built);
            countdown.push_back(n->data->relightCountdown);
        }
    }
    void Restore(Terrain* t) const {
        std::memcpy(&t->ambient, fields.data(), fields.size());
        t->sun = sun;
        std::size_t i = 0;
        for (Terrain::SectorCache::Node* n = t->sectorCache.head; n; n = n->next, ++i) {
            n->data->built = built[i];
            n->data->relightCountdown = countdown[i];
        }
    }
};

void __fastcall SetSunVerify(Terrain* self, void*, CLight* sun, const Vector4* ambient) {
    SunState before, original, ours;
    before.Save(self);
    s_setSun(self, nullptr, sun, ambient);
    original.Save(self);
    before.Restore(self);
    self->SetSun(sun, ambient);
    ours.Save(self);
    const float* a = reinterpret_cast<const float*>(original.fields.data());
    const float* b = reinterpret_cast<const float*>(ours.fields.data());
    int fields = 0;
    for (int i = 0; i < 8; ++i) fields += ei::verify::NearlyEqual(a[i], b[i], 4, 1e-6f) ? 0 : 1;
    fields += std::memcmp(a + 8, b + 8, 8) != 0 ? 1 : 0;   // the integer ray steps
    ei::verify::Check("Terrain::SetSun", 0, fields, "ambient / ray steps");
    ei::verify::Check("Terrain::SetSun", true, original.sun == ours.sun, "sun");
    ei::verify::Check("Terrain::SetSun", true, original.built == ours.built && original.countdown == ours.countdown,
                      "sector relight countdowns (%d sectors)", static_cast<int>(ours.built.size()));
}
EI_VERIFY(0x006C8ED0, SetSunVerify, 6, s_setSun);

// ---- Terrain::RunTick ----------------------------------------------------------------------------
// Plays ambient sounds and moves sectors in the cache: ours only (its parts are compared above);
// the frame counter and the result checked.
int(__fastcall* s_runTick)(Terrain*, void*);

int __fastcall RunTickVerify(Terrain* self, void*) {
    int frame = self->frame;
    int result = self->Terrain::RunTick();
    ei::verify::Check("Terrain::RunTick", 1, result, "result");
    ei::verify::Check("Terrain::RunTick", frame + 1, self->frame, "frame counter");
    return result;
}
EI_VERIFY(0x006CB1E0, RunTickVerify, 0, s_runTick);

} // namespace
