#include "ai_map.h"
#include "sector_map.h"

#include "ei/rebirth.h"
#include "ei/x87.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <cstring>
#include <list>
#include <vector>

void Sector::Clear() {
    freeOccluder = -1;
    occluders.clear();
    objects.clear();
    units.clear();
}

int Sector::NewOccluder() {
    if (freeOccluder >= 0) {
        int index = freeOccluder;
        freeOccluder = occluders[index].next;
        return index;
    }
    occluders.resize(std::ssize(occluders) + 1);
    return std::ssize(occluders) - 1;
}

int Sector::RemoveOccluder(int head, int bottom, int top, OccluderKind kind, int transparency) {
    std::int16_t first = static_cast<std::int16_t>(head);
    std::int16_t* link = &first;
    for (std::int16_t index = first; index >= 0;) {
        SectorOccluder& record = occluders[index];
        if (record.top == top && record.bottom == bottom && record.kind == kind && record.transparency == transparency) {
            std::int16_t next = record.next;
            record.next = static_cast<std::int16_t>(freeOccluder);
            freeOccluder = *link;   // the record's own index
            *link = next;
            break;
        }
        link = &record.next;
        index = record.next;
    }
    return first;
}

void Sector::FreeOccluder(int index) {
    occluders[index].next = static_cast<std::int16_t>(freeOccluder);
    freeOccluder = index;
}

Sector::Sector() {
    Clear();
}

void CSectorMap::Init(int cellsX, int cellsY, CAIMap* map) {
    width = cellsX / 32;
    height = cellsY / 32;
    added = 0;
    sectors.resize(width * height);
    aiMap = map;
}

CSectorMap::CSectorMap() {
    aiMap = nullptr;
    height = 0;
    added = 0;
    width = 0;
}

CSectorMap::~CSectorMap() = default;

bool CSectorMap::Add(CMapObject* object) {
    int cellX = x87::fistp(2.0f * object->localPosition_.x - 0.5f);
    int cellY = x87::fistp(2.0f * object->localPosition_.y - 0.5f);
    int sx = cellX / 32, sy = cellY / 32;
    if (sx >= width || sy >= height || sx < 0 || sy < 0) return false;

    Sector& sector = sectors[width * sy + sx];
    if (CUnit* unit = UnitOrNull(object)) sector.units.push_back(unit);
    else sector.objects.push_back(object);

    bool fits = aiMap->MarkFootprint(object, false);
    if (fits) {
        ++added;
        object->flags_ |= kInSectorMap;
        object->sectorCellY = cellY;
        object->sectorCellX = cellX;
        return true;
    }

    // Does not fit: take it out of the list again.
    Sector& again = sectors[width * sy + sx];
    if (CUnit* unit = UnitOrNull(object)) {
        if (auto node = std::ranges::find(again.units, unit); node != again.units.end()) again.units.erase(node);
    } else {
        if (auto node = std::ranges::find(again.objects, object); node != again.objects.end()) again.objects.erase(node);
    }
    return false;
}

std::list<CUnit*>* CSectorMap::UnitsAt(int sx, int sy) {
    if (sx < 0 || sy < 0 || sx >= width || sy >= height) return nullptr;
    return &sectors[width * sy + sx].units;
}

bool CSectorMap::Remove(CMapObject* object) {
    int sx = object->sectorCellX / 32, sy = object->sectorCellY / 32;
    if (sx >= width || sy >= height) return false;

    Sector& sector = sectors[width * sy + sx];
    if (CUnit* unit = UnitOrNull(object)) {
        auto node = std::ranges::find(sector.units, unit);
        if (node == sector.units.end()) return false;
        sector.units.erase(node);
    } else {
        auto node = std::ranges::find(sector.objects, object);
        if (node == sector.objects.end()) return false;
        sector.objects.erase(node);
    }

    bool cleared = aiMap->MarkFootprint(object, true);
    if (cleared) {
        object->NotifyCompanion();
        object->sectorCellX = -1;
        object->sectorCellY = -1;
    }
    return cleared;
}

void CSectorMap::FindUnitsInRadius(const Vector3* position, std::list<CUnit*>* result, float radius) {
    const SectorSpiral& spiral = SectorSpiralTables();
    int length = spiral.lengths[spiral.radii.Find(radius * 0.03125f)];
    const std::int16_t* offset = &spiral.offsets.data()->x;
    result->clear();

    int cellX = x87::fistp(2.0f * position->x - 0.5f);
    int cellY = x87::fistp(2.0f * position->y - 0.5f);
    int sx = cellX / 32, sy = cellY / 32;
    float radiusSquared = radius * radius;
    float px = position->x, py = position->y;

    // The units live in the AI map's own sector grid.
    CSectorMap* grid = &aiMap->sectorMap;
    for (int i = 0; i < length; ++i, offset += 2) {
        int gx = offset[0] + sx, gy = offset[1] + sy;
        if (gx < 0 || gy < 0 || gx >= grid->width || gy >= grid->height) continue;
        Sector& sector = grid->sectors[grid->width * gy + gx];
        for (auto node = sector.units.begin(); node != sector.units.end();) {
            CUnit* unit = *node;
            ++node;
            float dx = unit->localPosition_.x - px, dy = unit->localPosition_.y - py;
            const auto distance = LengthSquared2DUnrounded(dy, dx);
            EI_NAN_AUDIT(distance);
            if (distance < radiusSquared) result->push_front(unit);
        }
    }
}

bool CSectorMap::Move(CMapObject* object, const Vector3* position) {
    // Rebirth's FixMoveObject runs first: no move to a negative (or NaN) x or y.
    // (It also logs the refusal and failed moves to addon.log; nothing the game shows.)
    if (ei::rebirth::Enabled(ei::rebirth::Patch::FixMoveObject) && (!(position->x >= 0.0f) || !(position->y >= 0.0f)))
        return false;
    Vector3& current = object->localPosition_;
    int cellX = x87::fistp(2.0f * position->x - 0.5f);
    int cellY = x87::fistp(2.0f * position->y - 0.5f);
    Vector3 saved;
    saved = current;   // bitwise (no x87 round trip)

    int oldSx = object->sectorCellX / 32, oldSy = object->sectorCellY / 32;
    if (oldSx >= width || oldSy >= height) return false;
    int newSx = cellX / 32, newSy = cellY / 32;
    if (newSx >= width || newSy >= height) return false;
    bool changesSector = newSx != oldSx || newSy != oldSy;

    // Lift the footprint, move, and stamp it again; put it back where it was if it does not fit.
    bool moved = false;
    if (aiMap->MarkFootprint(object, true)) {
        current = *position;
        moved = aiMap->MarkFootprint(object, false);
        if (!moved) {
            current = saved;
            aiMap->MarkFootprint(object, false);
        }
    }
    if (!moved) return false;

    if (changesSector) {
        Sector& from = sectors[width * oldSy + oldSx];
        Sector& to = sectors[width * newSy + newSx];
        if (CUnit* unit = UnitOrNull(object)) {
            if (auto node = std::ranges::find(from.units, unit); node != from.units.end()) from.units.erase(node);
            to.units.push_back(unit);
        } else {
            if (auto node = std::ranges::find(from.objects, object); node != from.objects.end()) from.objects.erase(node);
            to.objects.push_back(object);
        }
    }
    object->flags_ |= kInSectorMap;
    object->sectorCellY = cellY;
    object->sectorCellX = cellX;
    return true;
}
