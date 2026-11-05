#include "game/sector_map.h"

#include "game/ai_map.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/rounding.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <vector>

int Sector::NewOccluder() {
    if (freeOccluder >= 0) {
        int index = freeOccluder;
        freeOccluder = occluders[index].next;
        return index;
    }
    occluders.resize(std::ssize(occluders) + 1);
    return lib::Count(occluders) - 1;
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

void SectorMap::Init(int cellsX, int cellsY, AIMap* map) {
    width = cellsX / 32;
    height = cellsY / 32;
    added = 0;
    sectors.resize(width * height);
    aiMap = map;
}



bool SectorMap::Add(MapObject* object) {
    const Point cell = WorldToCell(object->localPosition);
    int sx = cell.x / 32, sy = cell.y / 32;
    if (sx >= width || sy >= height || sx < 0 || sy < 0) return false;

    Sector& sector = sectors[width * sy + sx];
    if (Unit* unit = UnitOrNull(object)) sector.units.push_back(unit);
    else sector.objects.push_back(object);

    bool fits = aiMap->MarkFootprint(object, false);
    if (fits) {
        ++added;
        object->flags |= MapObjectFlag::InSectorMap;
        object->sectorCellY = cell.y;
        object->sectorCellX = cell.x;
        return true;
    }

    // Does not fit: take it out of the list again.
    Sector& again = sectors[width * sy + sx];
    if (Unit* unit = UnitOrNull(object)) {
        if (auto node = std::ranges::find(again.units, unit); node != again.units.end()) again.units.erase(node);
    } else {
        if (auto node = std::ranges::find(again.objects, object); node != again.objects.end()) again.objects.erase(node);
    }
    return false;
}

std::vector<Unit*>* SectorMap::UnitsAt(int sx, int sy) {
    if (sx < 0 || sy < 0 || sx >= width || sy >= height) return nullptr;
    return &sectors[width * sy + sx].units;
}

bool SectorMap::Remove(MapObject* object) {
    int sx = object->sectorCellX / 32, sy = object->sectorCellY / 32;
    if (sx >= width || sy >= height) return false;

    Sector& sector = sectors[width * sy + sx];
    if (Unit* unit = UnitOrNull(object)) {
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
        object->sectorCellX = -1;
        object->sectorCellY = -1;
    }
    return cleared;
}

std::vector<Unit*> SectorMap::FindUnitsInRadius(const Vector3& position, float radius) {
    std::vector<Unit*> result;
    const SectorSpiral& spiral = SectorSpiralTables();
    int length = spiral.lengths[spiral.radii.Find(radius * 0.03125f)];
    const std::int16_t* offset = &spiral.offsets.data()->x;

    const Point cell = WorldToCell(position);
    int sx = cell.x / 32, sy = cell.y / 32;
    float radiusSquared = radius * radius;

    // The units live in the AI map's own sector grid.
    SectorMap* grid = &aiMap->sectorMap;
    for (int i = 0; i < length; ++i, offset += 2) {
        int gx = offset[0] + sx, gy = offset[1] + sy;
        if (gx < 0 || gy < 0 || gx >= grid->width || gy >= grid->height) continue;
        Sector& sector = grid->sectors[grid->width * gy + gx];
        for (Unit* unit : sector.units) {
            if (DistanceSquaredXY(unit->localPosition, position) < radiusSquared) result.push_back(unit);
        }
    }
    std::ranges::reverse(result);
    return result;
}

bool SectorMap::Move(MapObject* object, const Vector3& position) {
    // No move to a negative (or NaN) x or y (Rebirth's FixMoveObject).
    if (!(position.x >= 0.0f) || !(position.y >= 0.0f)) return false;
    Vector3& current = object->localPosition;
    const Point cell = WorldToCell(position);
    const Vector3 saved = current;

    int oldSx = object->sectorCellX / 32, oldSy = object->sectorCellY / 32;
    if (oldSx >= width || oldSy >= height) return false;
    int newSx = cell.x / 32, newSy = cell.y / 32;
    if (newSx >= width || newSy >= height) return false;
    bool changesSector = newSx != oldSx || newSy != oldSy;

    // Lift the footprint, move, and stamp it again; put it back where it was if it does not fit.
    bool moved = false;
    if (aiMap->MarkFootprint(object, true)) {
        current = position;
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
        if (Unit* unit = UnitOrNull(object)) {
            if (auto node = std::ranges::find(from.units, unit); node != from.units.end()) from.units.erase(node);
            to.units.push_back(unit);
        } else {
            if (auto node = std::ranges::find(from.objects, object); node != from.objects.end()) from.objects.erase(node);
            to.objects.push_back(object);
        }
    }
    object->flags |= MapObjectFlag::InSectorMap;
    object->sectorCellY = cell.y;
    object->sectorCellX = cell.x;
    return true;
}

// --- The sector spiral ----------------------------------------------------------------------------------

// The sector spiral's tables (sector_map.h): the offsets of a 21 x 21 square of sectors ordered by distance,
// the number of offsets within each distinct distance and the tree that finds a distance's place among them,
// built on first use.

namespace {

constexpr int kSpiralReach = 10;                                    // sectors either way
constexpr int kSpiralCells = (kSpiralReach * 2 + 1) * (kSpiralReach * 2 + 1);
constexpr float kBeyondSpiral = 1.0e30f;                            // 7149F2CA: the last radius

// A sector of the square and how far it is: the gap between the sectors, in sectors.
struct SpiralCell {
    SpiralOffset offset;
    float distance = 0.0f;
};

// The sort's comparison: by distance, ascending.
int ByDistance(const SpiralCell& a, const SpiralCell& b) {
    const float da = a.distance;
    const float db = b.distance;
    if (da > db) return 1;
    return da == db ? 0 : -1;
}

// |v| - 1, 0 for 0: the whole sectors between two sectors `v` apart.
int Gap(int v) { return v == 0 ? 0 : (v < 0 ? -v : v) - 1; }

// The tables as the original's static initialiser builds them.
SectorSpiral BuildSectorSpiral() {
    SectorSpiral spiral;
    std::vector<SpiralCell> cells;
    cells.resize(kSpiralCells);
    spiral.offsets.resize(kSpiralCells);
    int filled = 0;
    for (int y = -kSpiralReach; y <= kSpiralReach; ++y) {
        for (int x = -kSpiralReach; x <= kSpiralReach; ++x) {
            // The squares are summed as integers (rooted, stored as a float); the gaps
            // are 9 at most, so the float sum is the same number.
            const Vector2 gap{static_cast<float>(Gap(x)), static_cast<float>(Gap(y))};
            SpiralCell& cell = cells[filled++];
            cell.offset.x = static_cast<std::int16_t>(x);
            cell.offset.y = static_cast<std::int16_t>(y);
            cell.distance = Length2D(gap);
        }
    }
    // The game's own qsort: it decides the order of the sectors at the same distance.
    libcrt::Qsort(cells.data(), cells.size(), ByDistance);
    for (int i = 0; i < std::ssize(cells); ++i) spiral.offsets[i] = cells[i].offset;

    // A length per distinct distance: the offsets up to and including that distance.
    std::vector<float> radii;
    std::vector<int>& lengths = spiral.lengths;
    radii.push_back(0.0f);
    lengths.push_back(1);
    for (int i = 0; i < std::ssize(cells); ++i) {
        const float distance = cells[i].distance;
        if (distance == radii.back()) {
            lengths[std::ssize(lengths) - 1] = i + 1;
        } else {
            radii.push_back(distance);
            lengths.push_back(i + 1);
        }
    }
    radii.push_back(kBeyondSpiral);
    lengths.push_back(lib::Count(spiral.offsets));
    spiral.radii.Build(radii.data(), static_cast<int>(radii.size()));
    return spiral;
}

} // namespace

int RadiusTree::Find(float radius) const {
    std::int32_t key;
    key = std::bit_cast<std::int32_t>(radius);
    int node = 1;
    for (int level = depth; level > 0; --level) {
        std::int32_t nodeKey;
        nodeKey = std::bit_cast<std::int32_t>(keys[node]);
        node = node * 2 + (key > nodeKey ? 1 : 0);
    }
    return node & mask;
}

void RadiusTree::Build(const float* values, int count) {
    depth = 1;
    if (count > 2) {
        do ++depth;
        while ((1 << depth) < count);
    }
    keys.resize(2 << depth);
    mask = (1 << depth) - 1;
    for (int level = 0; level <= depth; ++level) {
        const int width = 1 << level;
        const int offset = depth == level ? 0 : (1 << (depth - level - 1)) - 1;
        for (int k = 0; k < width; ++k) {
            int index = (1 << (depth - level)) * k + offset;
            if (index >= count) index = count - 1;
            keys[width + k] = values[index];
        }
    }
}

const SectorSpiral& SectorSpiralTables() {
    static const SectorSpiral spiral = BuildSectorSpiral();
    return spiral;
}
