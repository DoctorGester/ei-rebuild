// Point lights on the landscape (006BF4A0 / 006BF580, 006C4EE0-006C5230, 006CA5D0-006CAF68).
//
// The landscape keeps a table of the point lights it was given (by id; the light held, and the
// area it reaches: a range of sectors and the first / last vertex inside the edge sectors). Each
// vertex record of a built sector has a list of the lights whose range reaches it (pooled 16-byte
// LightRef nodes); the lighting (SectorData::Light, terrain_lighting.cpp) adds those lights to the
// vertex colours. Adding, removing or moving a light updates the lists of the area's built sectors
// and relights its lit ones; a sector built later picks up the lights covering it (LightSector).
#include "terrain.h"
#include "terrain_sector.h"
#include "spell.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <bit>
#include <cstdint>

using terrain::LightRef;
using terrain::LightRefBlock;
using terrain::SectorData;
using terrain::SectorVertex;

namespace {

constexpr int kRow = 33;          // vertices per sector row
constexpr int kLastVertex = 32;   // the last vertex of a sector row / column
constexpr int kSectorCells = 32;

// The pool's two block lists (see LightRefBlock).
LightRefBlock*& BlocksWithFree() { return *reinterpret_cast<LightRefBlock**>(0x007C8840); }
LightRefBlock*& FullBlocks() { return *reinterpret_cast<LightRefBlock**>(0x007C8844); }

void Unlink(LightRefBlock* block, LightRefBlock*& head) {
    if (block->prev) block->prev->next = block->next;
    else head = block->next;
    if (block->next) block->next->prev = block->prev;
}

void PushFront(LightRefBlock* block, LightRefBlock*& head) {
    if (head) head->prev = block;
    block->next = head;
    block->prev = nullptr;
    head = block;
}

// The cell size (00744160; 1) the light bounds are divided by.
float CellSize() { return *reinterpret_cast<const float*>(0x00744160); }

bool SignSet(float v) { return std::bit_cast<std::uint32_t>(v) >> 31 != 0; }

// The vertex range of `area` inside the sector at (sx, sy): the area's first / last vertex in its
// edge sectors, the whole row or column elsewhere.
struct VertexRange {
    int x0, y0, x1, y1;
};
VertexRange RangeIn(const LightArea* area, int sx, int sy) {
    return {sx == area->firstSectorX ? area->firstX : 0, sy == area->firstSectorY ? area->firstY : 0,
            sx == area->lastSectorX ? area->lastX : kLastVertex, sy == area->lastSectorY ? area->lastY : kLastVertex};
}

// The area and vertex updates are called through their addresses, so that their verify wrappers run.
__declspec(naked) void __fastcall LightAreaOfAt(Terrain*, void*, CLight*, const Vector3*, LightArea*)
    EI_ORIGINAL(0x006CAA70)
__declspec(naked) void __stdcall UpdateVertexLightsAt(SectorData*, CLight*, const Vector3*, const LightArea*, int)
    EI_ORIGINAL(0x006CAC10)

SectorData* BuiltSector(Terrain* terrain, int sx, int sy, std::uint32_t wanted) {
    return reinterpret_cast<SectorData*>(terrain->SectorAt(sx, sy, wanted, 0));
}

// UpdateVertexLights for every built sector of the area.
void UpdateArea(Terrain* terrain, CLight* light, const Vector3* position, const LightArea* area, int remove) {
    for (int sy = area->firstSectorY; sy <= area->lastSectorY; ++sy)
        for (int sx = area->firstSectorX; sx <= area->lastSectorX; ++sx)
            if (SectorData* sector = BuiltSector(terrain, sx, sy, 2))
                UpdateVertexLightsAt(sector, light, position, area, remove);
}

} // namespace

// ---- The LightRef pool ------------------------------------------------------------------------------

namespace terrain {

LightRef* AllocLightRef() {
    if (!BlocksWithFree()) {
        auto* block = static_cast<LightRefBlock*>(gamecrt::operator_new(sizeof(LightRefBlock)));
        BlocksWithFree() = block;
        block->next = nullptr;
        block->prev = nullptr;
        block->freeList = &block->refs[0];
        block->freeCount = LightRefBlock::kRefs;
        for (int i = 0; i < LightRefBlock::kRefs - 1; ++i) {
            block->refs[i].next = &block->refs[i + 1];
            block->refs[i].block = block;
        }
        block->refs[LightRefBlock::kRefs - 1].next = nullptr;
        block->refs[LightRefBlock::kRefs - 1].block = block;
    }
    LightRefBlock* block = BlocksWithFree();
    LightRef* ref = block->freeList;
    block->freeList = ref->next;
    if (--block->freeCount == 0) {
        // Full: it moves to the front of the full list (it is the head of the other one).
        if (block->next) block->next->prev = nullptr;
        LightRefBlock* rest = block->next;
        PushFront(block, FullBlocks());
        BlocksWithFree() = rest;
    }
    return ref;
}

void __stdcall FreeLightRef(LightRef* ref) {
    LightRefBlock* block = ref->block;
    ref->next = block->freeList;
    block->freeList = ref;
    int free = ++block->freeCount;
    if (free == 1) {
        Unlink(block, FullBlocks());
        PushFront(block, BlocksWithFree());
    } else if (free == LightRefBlock::kRefs) {
        Unlink(block, BlocksWithFree());
        gamecrt::operator_delete(block);
    }
}
EI_REPLACE(0x006C5230, FreeLightRef);

void SectorVertex::ReleaseLights() {
    while (lights) {
        LightRef* next = lights->next;
        FreeLightRef(lights);
        lights = next;
    }
}
EI_REPLACE_METHOD(0x006C5150, SectorVertex::ReleaseLights);

void __stdcall DestroyRecords(SectorVertex* records, int count) {
    for (int i = 0; i < count; ++i) records[i].ReleaseLights();
}
EI_REPLACE(0x006C4EE0, DestroyRecords);

void __stdcall UpdateVertexLights(SectorData* sector, CLight* light, const Vector3* position, const LightArea* area,
                                  int remove) {
    VertexRange range = RangeIn(area, sector->sx, sector->sy);
    float rangeSquared = RoundToFloat(light->range_ * light->range_);
    for (int y = range.y0; y <= range.y1; ++y) {
        const SectorPosition* vertex = &sector->vertices.data[y * kRow + range.x0];
        SectorVertex* record = &sector->records.data[y * kRow + range.x0];
        for (int x = range.x0; x <= range.x1; ++x, ++vertex, ++record) {
            Vector3 offset = Vector3{vertex->x, vertex->y, vertex->z} - *position;
            LightRef* head = record->lights;
            LightRef* found = head;
            while (found && found->light != light) found = found->next;
            if (remove || LengthSquaredZYX(offset) >= rangeSquared) {   // NaN: kept or added
                if (!found) continue;
                if (found == head) record->lights = found->next;
                else found->prev->next = found->next;
                if (found->next) found->next->prev = found->prev;
                FreeLightRef(found);
            } else if (!found) {
                LightRef* ref = AllocLightRef();
                ref->next = head;
                ref->light = light;
                ref->prev = nullptr;
                if (record->lights) record->lights->prev = ref;
                record->lights = ref;
            }
        }
    }
}
EI_REPLACE(0x006CAC10, UpdateVertexLights);

} // namespace terrain

// ---- Areas -------------------------------------------------------------------------------------------

void __fastcall SetLightArea(int x0, int y0, int x1, int y1, int sectorsX, int sectorsY, LightArea* area) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 > sectorsX * kSectorCells) x0 = sectorsX * kSectorCells;
    if (y0 > sectorsY * kSectorCells) y0 = sectorsY * kSectorCells;
    area->firstSectorX = x0 / kSectorCells;
    area->firstSectorY = y0 / kSectorCells;
    area->firstX = x0 % kSectorCells;
    area->firstY = y0 % kSectorCells;
    if (area->firstX == 0 && area->firstSectorX > 0) {
        --area->firstSectorX;
        area->firstX = kLastVertex;
    }
    if (area->firstY == 0 && area->firstSectorY > 0) {
        --area->firstSectorY;
        area->firstY = kLastVertex;
    }
    area->lastSectorX = x1 / kSectorCells;
    area->lastSectorY = y1 / kSectorCells;
    area->lastX = x1 % kSectorCells;
    area->lastY = y1 % kSectorCells;
}
EI_REPLACE(0x006BF4A0, SetLightArea);

void __fastcall LightAreaFromBounds(int sectorsX, int sectorsY, float minX, float minY, float maxX, float maxY,
                                    LightArea* area) {
    int lastY = static_cast<int>(gamecrt::Ftol(maxY / CellSize() + 0.5f));
    int lastX = static_cast<int>(gamecrt::Ftol(maxX / CellSize() + 0.5f));
    int firstY = static_cast<int>(gamecrt::Ftol(minY / CellSize()));
    int firstX = static_cast<int>(gamecrt::Ftol(minX / CellSize()));
    SetLightArea(firstX, firstY, lastX, lastY, sectorsX, sectorsY, area);
}
EI_REPLACE(0x006BF580, LightAreaFromBounds);

void Terrain::LightAreaOf(CLight* light, const Vector3* delta, LightArea* area) {
    float range = light->range_;
    Vector3 position = light->position_;
    float minX = position.x - range;
    float minY = position.y - range;
    float maxX = position.x + range;
    float maxY = position.y + range;
    if (delta) {
        if (SignSet(delta->x)) minX = minX + delta->x;
        else maxX = maxX + delta->x;
        if (SignSet(delta->y)) minY = minY + delta->y;
        else maxY = maxY + delta->y;
    }
    LightAreaFromBounds(sectorsX, sectorsY, minX, minY, maxX, maxY, area);
}
EI_REPLACE_METHOD(0x006CAA70, Terrain::LightAreaOf);

void Terrain::RelightArea(const LightArea* area) {
    auto* fields = reinterpret_cast<terrain::TerrainFields*>(this);
    for (int sy = area->firstSectorY; sy <= area->lastSectorY; ++sy) {
        for (int sx = area->firstSectorX; sx <= area->lastSectorX; ++sx) {
            SectorData* sector = BuiltSector(this, sx, sy, 4);
            if (!sector) continue;
            VertexRange range = RangeIn(area, sx, sy);
            sector->LightAtOriginal(sun, fields, 1, range.x0, range.y0, range.x1, range.y1);
        }
    }
}
EI_REPLACE_METHOD(0x006CAB50, Terrain::RelightArea);

// ---- The light table -----------------------------------------------------------------------------

void Terrain::AddLight(CLight* light) {
    if (!mapLoaded) return;
    ++light->refCount_;
    LandscapeLight entry;
    entry.light = light;
    LightAreaOfAt(this, nullptr, light, nullptr, &entry.area);
    int id = light->id_;
    if (id > maxLightId) maxLightId = id;
    lights[id] = entry;
    Vector3 position = light->position_;
    UpdateArea(this, light, &position, &entry.area, 0);
    RelightArea(&entry.area);
}
EI_REPLACE_METHOD(0x006CA5D0, Terrain::AddLight);

void Terrain::RemoveLight(CLight* light) {
    if (!mapLoaded) return;
    // The original leaves the area uninitialised when the light was not registered (it loops over
    // whatever its stack held); ours then takes it off nothing and relights nothing.
    LightArea area = {0, 0, -1, -1, 0, 0, 0, 0};
    if (LightTable::Node* node = lights.Find(light->id_)) {
        area = node->value.area;
        lights.Erase(node);
    }
    Vector3 position = light->position_;
    UpdateArea(this, light, &position, &area, 1);
    RelightArea(&area);
    light->Release();
}
EI_REPLACE_METHOD(0x006CA720, Terrain::RemoveLight);

void Terrain::MoveLight(CLight* light, const Vector3* delta) {
    if (!mapLoaded) return;
    // The original takes the entry's area without checking that the light is registered (an
    // unregistered light writes its area to address 4); ours does the same through a null entry.
    LightTable::Node* node = lights.Find(light->id_);
    LightArea* area = node ? &node->value.area : nullptr;
    LightAreaOfAt(this, nullptr, light, delta, area);
    Vector3 position = light->position_;
    if (delta) position = position + *delta;
    UpdateArea(this, light, &position, area, 0);
    Vector3 old = light->position_;
    light->position_ = old + *delta;
    RelightArea(area);
    LightAreaOfAt(this, nullptr, light, nullptr, area);
    light->position_ = old;
}
EI_REPLACE_METHOD(0x006CA830, Terrain::MoveLight);

void Terrain::LightSector(SectorData* sector) {
    for (LightTable::Node* node = lights.First(); node; node = lights.After(node)) {
        const LightArea& area = node->value.area;
        if (area.firstSectorX <= sector->sx && sector->sx <= area.lastSectorX && area.firstSectorY <= sector->sy &&
            sector->sy <= area.lastSectorY)
            UpdateVertexLightsAt(sector, node->value.light, &node->value.light->position_, &area, 0);
    }
}
EI_REPLACE_METHOD(0x006CA9C0, Terrain::LightSector);
