// Verify wrappers for the landscape's point lights (terrain_lights.cpp). Lights are added when a
// zone's lit objects load (torches, fires, lit units) and by spells; sectors pick them up when they
// are built and drop their nodes when freed: every scenario that loads zone1 reaches them.
#include "verify.h"

#include "../../src/game/spell.h"
#include "../../src/game/terrain.h"
#include "../../src/game/terrain_sector.h"

#include <cstring>

using namespace terrain;

namespace {

bool SameArea(const LightArea& a, const LightArea& b) { return std::memcmp(&a, &b, sizeof(LightArea)) == 0; }

void CheckArea(const char* name, const LightArea& expected, const LightArea& actual) {
    ei::verify::Check(name, true, SameArea(expected, actual),
                      "sectors %d,%d..%d,%d / %d,%d..%d,%d vertices %d,%d..%d,%d / %d,%d..%d,%d",
                      expected.firstSectorX, expected.firstSectorY, expected.lastSectorX, expected.lastSectorY,
                      actual.firstSectorX, actual.firstSectorY, actual.lastSectorX, actual.lastSectorY, expected.firstX,
                      expected.firstY, expected.lastX, expected.lastY, actual.firstX, actual.firstY, actual.lastX,
                      actual.lastY);
}

// ---- Areas: the original into a scratch area, ours into the caller's; compared ----------------------

// 006BF4A0: PUSH EBX; PUSH ESI; TEST ECX,ECX; PUSH EDI (5 bytes). Reached from the original
// 006BF580 run by the wrapper below.
void(__fastcall* s_setArea)(int, int, int, int, int, int, LightArea*);

void __fastcall SetLightAreaVerify(int x0, int y0, int x1, int y1, int sectorsX, int sectorsY, LightArea* area) {
    LightArea expected;
    s_setArea(x0, y0, x1, y1, sectorsX, sectorsY, &expected);
    SetLightArea(x0, y0, x1, y1, sectorsX, sectorsY, area);
    CheckArea("SetLightArea", expected, *area);
}
EI_VERIFY(0x006BF4A0, SetLightAreaVerify, 5, s_setArea);

// 006BF580: FLD [ESP+0x10]; FDIV [00744160] (10 bytes). Reached from the original 006CAA70 run by
// the wrapper below.
void(__fastcall* s_areaFromBounds)(int, int, float, float, float, float, LightArea*);

void __fastcall AreaFromBoundsVerify(int sectorsX, int sectorsY, float minX, float minY, float maxX, float maxY,
                                     LightArea* area) {
    LightArea expected;
    s_areaFromBounds(sectorsX, sectorsY, minX, minY, maxX, maxY, &expected);
    LightAreaFromBounds(sectorsX, sectorsY, minX, minY, maxX, maxY, area);
    CheckArea("LightAreaFromBounds", expected, *area);
}
EI_VERIFY(0x006BF580, AreaFromBoundsVerify, 10, s_areaFromBounds);

// 006CAA70: SUB ESP,0x18; MOV EAX,[ESP+0x1C] (7 bytes). Every light added or moved.
void(__fastcall* s_lightAreaOf)(Terrain*, void*, CLight*, const Vector3*, LightArea*);

void __fastcall LightAreaOfVerify(Terrain* self, void*, CLight* light, const Vector3* delta, LightArea* area) {
    LightArea expected;
    s_lightAreaOf(self, nullptr, light, delta, &expected);
    self->LightAreaOf(light, delta, area);
    CheckArea("Terrain::LightAreaOf", expected, *area);
}
EI_VERIFY(0x006CAA70, LightAreaOfVerify, 7, s_lightAreaOf);

// ---- The pool and the vertex lists: ours only (they change the global pool) ------------------------

// A record's list: well linked (prev pointers, a null-terminated walk) and how often `light` is in it.
int CountInList(const SectorVertex& record, const CLight* light, bool* linked) {
    int count = 0, steps = 0;
    const LightRef* prev = nullptr;
    for (const LightRef* ref = record.lights; ref && steps < 100000; ref = ref->next, ++steps) {
        if (ref->prev != prev || !ref->block) *linked = false;
        if (ref->light == light) ++count;
        prev = ref;
    }
    if (steps >= 100000) *linked = false;
    return count;
}

// 006C5230: the node is back on its block's free list (unless the block was freed).
void(__stdcall* s_freeRef)(LightRef*);

void __stdcall FreeLightRefVerify(LightRef* ref) {
    LightRefBlock* block = ref->block;
    int before = block->freeCount;
    FreeLightRef(ref);
    if (before + 1 < LightRefBlock::kRefs)
        ei::verify::Check("FreeLightRef", true, block->freeList == ref && block->freeCount == before + 1,
                          "free %d", before);
}
EI_VERIFY(0x006C5230, FreeLightRefVerify, 0, s_freeRef);

// 006C5150 / 006C4EE0: the lists are empty afterwards.
void(__fastcall* s_releaseLights)(SectorVertex*, void*);

void __fastcall ReleaseLightsVerify(SectorVertex* self, void*) {
    self->ReleaseLights();
    ei::verify::Check("SectorVertex::ReleaseLights", true, self->lights == nullptr, "list emptied");
}
EI_VERIFY(0x006C5150, ReleaseLightsVerify, 0, s_releaseLights);

void(__stdcall* s_destroyRecords)(SectorVertex*, int);

void __stdcall DestroyRecordsVerify(SectorVertex* records, int count) {
    DestroyRecords(records, count);
    int left = 0;
    for (int i = 0; i < count; ++i)
        if (records[i].lights) ++left;
    ei::verify::Check("DestroyRecords", 0, left, "records with lights (of %d)", count);
}
EI_VERIFY(0x006C4EE0, DestroyRecordsVerify, 0, s_destroyRecords);

// 006CAC10: afterwards every vertex of the range has the light once when it is added and in range
// (the squared distance, ZYX, below the squared range), else not at all; the lists stay linked.
void(__stdcall* s_updateVertexLights)(SectorData*, CLight*, const Vector3*, const LightArea*, int);

void __stdcall UpdateVertexLightsVerify(SectorData* sector, CLight* light, const Vector3* position,
                                        const LightArea* area, int remove) {
    UpdateVertexLights(sector, light, position, area, remove);
    int x0 = sector->sx == area->firstSectorX ? area->firstX : 0;
    int y0 = sector->sy == area->firstSectorY ? area->firstY : 0;
    int x1 = sector->sx == area->lastSectorX ? area->lastX : 32;
    int y1 = sector->sy == area->lastSectorY ? area->lastY : 32;
    float rangeSquared = RoundToFloat(light->range_ * light->range_);
    int wrong = 0, firstWrong = -1;
    bool linked = true;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const SectorPosition& p = sector->vertices.data[y * 33 + x];
            Vector3 offset = Vector3{p.x, p.y, p.z} - *position;
            int expected = remove || LengthSquaredZYX(offset) >= rangeSquared ? 0 : 1;
            if (CountInList(sector->records.data[y * 33 + x], light, &linked) != expected) {
                if (firstWrong < 0) firstWrong = y * 33 + x;
                ++wrong;
            }
        }
    }
    ei::verify::Check("UpdateVertexLights", true, wrong == 0 && linked,
                      "sector %d,%d vertices %d,%d..%d,%d remove %d: %d wrong (first %d), linked %d", sector->sx,
                      sector->sy, x0, y0, x1, y1, remove, wrong, firstWrong, linked);
}
EI_VERIFY(0x006CAC10, UpdateVertexLightsVerify, 0, s_updateVertexLights);

// ---- The light table: ours only; the entries and areas checked (areas through the original
// 006CAA70) -----------------------------------------------------------------------------------------

LightArea OriginalArea(Terrain* self, CLight* light) {
    LightArea area;
    s_lightAreaOf(self, nullptr, light, nullptr, &area);
    return area;
}

// 006CA5D0: the light registered with the area of its position and held once more.
void(__fastcall* s_addLight)(Terrain*, void*, CLight*);

void __fastcall AddLightVerify(Terrain* self, void*, CLight* light) {
    int refs = light->refCount_;
    self->AddLight(light);
    if (!self->mapLoaded) return;
    Terrain::LightTable::Node* node = self->lights.Find(light->id_);
    ei::verify::Check("Terrain::AddLight", true,
                      node && node->value.light == light && light->refCount_ == refs + 1 && self->maxLightId >= light->id_,
                      "light %d", light->id_);
    if (node) CheckArea("Terrain::AddLight area", OriginalArea(self, light), node->value.area);
}
EI_VERIFY(0x006CA5D0, AddLightVerify, 0, s_addLight);

// 006CA720: the registration gone.
void(__fastcall* s_removeLight)(Terrain*, void*, CLight*);

void __fastcall RemoveLightVerify(Terrain* self, void*, CLight* light) {
    int id = light->id_;
    bool loaded = self->mapLoaded != 0;
    self->RemoveLight(light);
    if (loaded) ei::verify::Check("Terrain::RemoveLight", true, self->lights.Find(id) == nullptr, "light %d", id);
}
EI_VERIFY(0x006CA720, RemoveLightVerify, 0, s_removeLight);

// 006CA830: the light left where it was, its area the one at the moved position.
void(__fastcall* s_moveLight)(Terrain*, void*, CLight*, const Vector3*);

void __fastcall MoveLightVerify(Terrain* self, void*, CLight* light, const Vector3* delta) {
    Vector3 before = light->position_;
    self->MoveLight(light, delta);
    if (!self->mapLoaded) return;
    ei::verify::Check("Terrain::MoveLight", true, std::memcmp(&before, &light->position_, sizeof(Vector3)) == 0,
                      "position kept");
    if (Terrain::LightTable::Node* node = self->lights.Find(light->id_)) {
        Vector3 moved = before + *delta;
        light->position_ = moved;
        LightArea expected = OriginalArea(self, light);
        light->position_ = before;
        CheckArea("Terrain::MoveLight area", expected, node->value.area);
    }
}
EI_VERIFY(0x006CA830, MoveLightVerify, 0, s_moveLight);

// 006CA9C0 (a sector being built) / 006CAB50: ours only; the vertex updates they make are checked
// by UpdateVertexLightsVerify, RelightArea's lighting by SectorData::Light's wrapper.
void(__fastcall* s_lightSector)(Terrain*, void*, SectorData*);

void __fastcall LightSectorVerify(Terrain* self, void*, SectorData* sector) { self->LightSector(sector); }
EI_VERIFY(0x006CA9C0, LightSectorVerify, 0, s_lightSector);

void(__fastcall* s_relightArea)(Terrain*, void*, const LightArea*);

void __fastcall RelightAreaVerify(Terrain* self, void*, const LightArea* area) { self->RelightArea(area); }
EI_VERIFY(0x006CAB50, RelightAreaVerify, 0, s_relightArea);

} // namespace
