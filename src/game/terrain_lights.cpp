// Point lights on the landscape. The landscape keeps a table of the point lights it was given (by id; the
// light held, and the area it reaches: a range of sectors and the first / last vertex inside the edge
// sectors). Each vertex record of a built sector has a list of the lights whose range reaches it (pooled
// 16-byte LightRef nodes); the lighting (SectorData::Light, terrain_lighting.cpp) adds those lights to the
// vertex colours. Adding, removing or moving a light updates the lists of the area's built sectors and
// relights its lit ones; a sector built later picks up the lights covering it (LightSector).
#include "terrain.h"
#include "terrain_sector.h"
#include "spell.h"

#include "lib/crt.h"
#include "lib/float_select.h"

#include <bit>
#include <cstdint>
#include <utility>

using terrain::LightRef;
using terrain::LightRefBlock;
using terrain::SectorData;
using terrain::SectorVertex;

namespace {

using terrain::kLastVertex;
using terrain::kSectorCells;

// The pool's two block lists (see LightRefBlock).
LightRefBlock* g_blocksWithFree = /*007C8840*/ {nullptr};
LightRefBlock* g_fullBlocks = /*007C8844*/ {nullptr};

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

// The vertex range of `area` inside the sector at (sx, sy): the area's first / last vertex in its
// edge sectors, the whole row or column elsewhere.
struct VertexRange {
    int x0, y0, x1, y1;
};
VertexRange RangeIn(const LightArea* area, int sx, int sy) {
    return {sx == area->firstSectorX ? area->firstX : 0, sy == area->firstSectorY ? area->firstY : 0,
            sx == area->lastSectorX ? area->lastX : kLastVertex, sy == area->lastSectorY ? area->lastY : kLastVertex};
}

// UpdateVertexLights for every built sector of the area.
void UpdateArea(Terrain* terrain, CLight* light, const Vector3* position, const LightArea* area, bool remove) {
    for (int sy = area->firstSectorY; sy <= area->lastSectorY; ++sy)
        for (int sx = area->firstSectorX; sx <= area->lastSectorX; ++sx)
            if (SectorData* sector = terrain->SectorAt(sx, sy, SectorStage::Built, false))
                terrain::UpdateVertexLights(sector, light, position, area, remove);
}

// (x0, y0) is clamped to the map; a first vertex on a sector edge counts as vertex 32 of the sector before.
LightArea SetLightArea(int x0, int y0, int x1, int y1, int sectorsX, int sectorsY) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 > sectorsX * kSectorCells) x0 = sectorsX * kSectorCells;
    if (y0 > sectorsY * kSectorCells) y0 = sectorsY * kSectorCells;
    LightArea area;
    area.firstSectorX = x0 / kSectorCells;
    area.firstSectorY = y0 / kSectorCells;
    area.firstX = x0 % kSectorCells;
    area.firstY = y0 % kSectorCells;
    if (area.firstX == 0 && area.firstSectorX > 0) {
        --area.firstSectorX;
        area.firstX = kLastVertex;
    }
    if (area.firstY == 0 && area.firstSectorY > 0) {
        --area.firstSectorY;
        area.firstY = kLastVertex;
    }
    area.lastSectorX = x1 / kSectorCells;
    area.lastSectorY = y1 / kSectorCells;
    area.lastX = x1 % kSectorCells;
    area.lastY = y1 % kSectorCells;
    return area;
}

// The first vertex truncated, the last rounded.
LightArea LightAreaFromBounds(int sectorsX, int sectorsY, float minX, float minY, float maxX, float maxY) {
    int lastY = static_cast<int>(libcrt::Ftol(maxY + 0.5f));
    int lastX = static_cast<int>(libcrt::Ftol(maxX + 0.5f));
    int firstY = static_cast<int>(libcrt::Ftol(minY));
    int firstX = static_cast<int>(libcrt::Ftol(minX));
    return SetLightArea(firstX, firstY, lastX, lastY, sectorsX, sectorsY);
}

} // namespace

// ---- The LightRef pool ------------------------------------------------------------------------------

namespace terrain {

LightRef* AllocLightRef() {
    if (!g_blocksWithFree) {
        auto* block = new LightRefBlock;
        g_blocksWithFree = block;
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
    LightRefBlock* block = g_blocksWithFree;
    LightRef* ref = block->freeList;
    block->freeList = ref->next;
    if (--block->freeCount == 0) {
        // Full: it moves to the front of the full list (it is the head of the other one).
        if (block->next) block->next->prev = nullptr;
        LightRefBlock* rest = block->next;
        PushFront(block, g_fullBlocks);
        g_blocksWithFree = rest;
    }
    return ref;
}

void FreeLightRef(LightRef* ref) {
    LightRefBlock* block = ref->block;
    ref->next = block->freeList;
    block->freeList = ref;
    int free = ++block->freeCount;
    if (free == 1) {
        Unlink(block, g_fullBlocks);
        PushFront(block, g_blocksWithFree);
    } else if (free == LightRefBlock::kRefs) {
        Unlink(block, g_blocksWithFree);
        delete block;
    }
}

void SectorVertex::ReleaseLights() {
    while (lights) {
        LightRef* next = lights->next;
        FreeLightRef(lights);
        lights = next;
    }
}

SectorVertex::SectorVertex(SectorVertex&& from) noexcept
    : projected(from.projected), outcode(from.outcode), colour(from.colour), specular(from.specular),
      position(from.position), waterMaterial(from.waterMaterial), depth(from.depth), normal(from.normal),
      lit(from.lit), materialColour(from.materialColour), materialByte(from.materialByte),
      lights(std::exchange(from.lights, nullptr)) {}

void UpdateVertexLights(SectorData* sector, CLight* light, const Vector3* position, const LightArea* area, bool remove) {
    VertexRange range = RangeIn(area, sector->sx, sector->sy);
    float rangeSquared = static_cast<float>(light->range_ * light->range_);
    for (int y = range.y0; y <= range.y1; ++y) {
        const SectorPosition* vertex = &sector->vertices[y * kSectorRow + range.x0];
        SectorVertex* record = &sector->records[y * kSectorRow + range.x0];
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

} // namespace terrain

LightArea Terrain::LightAreaOf(CLight* light, const Vector3* delta) const {
    float range = light->range_;
    Vector3 position = light->localPosition_;
    float minX = position.x - range;
    float minY = position.y - range;
    float maxX = position.x + range;
    float maxY = position.y + range;
    if (delta) {
        if (SignBit(delta->x)) minX = minX + delta->x;
        else maxX = maxX + delta->x;
        if (SignBit(delta->y)) minY = minY + delta->y;
        else maxY = maxY + delta->y;
    }
    return LightAreaFromBounds(sectorsX, sectorsY, minX, minY, maxX, maxY);
}

void Terrain::RelightArea(const LightArea* area) {
    for (int sy = area->firstSectorY; sy <= area->lastSectorY; ++sy) {
        for (int sx = area->firstSectorX; sx <= area->lastSectorX; ++sx) {
            SectorData* sector = SectorAt(sx, sy, SectorStage::Lit, false);
            if (!sector) continue;
            VertexRange range = RangeIn(area, sx, sy);
            sector->Light(true, range.x0, range.y0, range.x1, range.y1);
        }
    }
}

// ---- The light table -----------------------------------------------------------------------------

void Terrain::AddLight(CLight* light) {
    if (!mapLoaded) return;
    light->AddRef();
    LandscapeLight entry;
    entry.light = light;
    entry.area = LightAreaOf(light, nullptr);
    lights[light->id_] = entry;
    Vector3 position = light->localPosition_;
    UpdateArea(this, light, &position, &entry.area, false);
    RelightArea(&entry.area);
}

void Terrain::RemoveLight(CLight* light) {
    if (!mapLoaded) return;
    // The game leaves the area uninitialised when the light was not registered (it loops over whatever its
    // stack held); this then takes it off nothing and relights nothing.
    LightArea area = {0, 0, -1, -1, 0, 0, 0, 0};
    if (auto node = lights.find(light->id_); node != lights.end()) {
        area = node->second.area;
        lights.erase(node);
    }
    Vector3 position = light->localPosition_;
    UpdateArea(this, light, &position, &area, true);
    RelightArea(&area);
    light->Release();
}

void Terrain::MoveLight(CLight* light, const Vector3* delta) {
    if (!mapLoaded) return;
    // The game takes the entry's area without checking that the light is registered (an unregistered light
    // writes its area to address 4); this does the same through a null entry.
    auto node = lights.find(light->id_);
    LightArea* area = node != lights.end() ? &node->second.area : nullptr;
    *area = LightAreaOf(light, delta);
    Vector3 position = light->localPosition_;
    if (delta) position = position + *delta;
    UpdateArea(this, light, &position, area, false);
    Vector3 old = light->localPosition_;
    light->localPosition_ = old + *delta;
    RelightArea(area);
    *area = LightAreaOf(light, nullptr);
    light->localPosition_ = old;
}

void Terrain::LightSector(SectorData* sector) {
    for (auto& [id, entry] : lights) {
        const LightArea& area = entry.area;
        if (area.firstSectorX <= sector->sx && sector->sx <= area.lastSectorX && area.firstSectorY <= sector->sy &&
            sector->sy <= area.lastSectorY)
            terrain::UpdateVertexLights(sector, entry.light, &entry.light->localPosition_, &area, false);
    }
}
