#include "sector_map.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cstring>

__declspec(naked) void CUnit::OnLeftSectorMap() EI_ORIGINAL(0x0050E930)

namespace {
// MarkFootprint through its original address, so the verify build's wrapper sees these calls.
__declspec(naked) bool __fastcall MarkFootprint(CAIMap*, void*, CEngineObject*, bool) EI_ORIGINAL(0x005B6A80)

// The world position of an object (+0x1C, +0x20; common to all objects in the sector map).
float PosX(const CEngineObject* o) { return *reinterpret_cast<const float*>(reinterpret_cast<const char*>(o) + 0x1C); }
float PosY(const CEngineObject* o) { return *reinterpret_cast<const float*>(reinterpret_cast<const char*>(o) + 0x20); }
CUnit* AsUnit(CEngineObject* o) { return static_cast<CUnit*>(o); }

constexpr float kCellSize = 1.0f;   // 0073FDD0
} // namespace

bool CSectorMap::Add(CEngineObject* object) {
    int cellX = x87::fistp(1.0f / (kCellSize * 0.5f) * PosX(object) - 0.5f);
    int cellY = x87::fistp(1.0f / (kCellSize * 0.5f) * PosY(object) - 0.5f);
    int sx = cellX / 32, sy = cellY / 32;
    if (sx >= width || sy >= height || sx < 0 || sy < 0) return false;

    Sector& sector = sectors[width * sy + sx];
    bool isUnit = object->GetClassId() == kClassUnit;
    (isUnit ? sector.units : sector.objects).AddTail(object);

    bool fits = MarkFootprint(aiMap, nullptr, object, false);
    if (fits) {
        ++added;
        object->flags_ |= kInSectorMap;
        AsUnit(object)->sectorCellY = cellY;
        AsUnit(object)->sectorCellX = cellX;
        return true;
    }

    // Does not fit: take it out of the list again.
    Sector& again = sectors[width * sy + sx];
    if (object->GetClassId() == kClassUnit) {
        if (auto* node = again.units.Find(object)) again.units.RemoveAt(node);
    } else {
        if (auto* node = again.objects.Find(object)) again.objects.RemoveAt(node);
    }
    return false;
}
EI_REPLACE_METHOD(0x005C0DF0, CSectorMap::Add);

mfc::CTypedPtrList<CEngineObject>* CSectorMap::UnitsAt(int sx, int sy) {
    if (sx < 0 || sy < 0 || sx >= width || sy >= height) return nullptr;
    return &sectors[width * sy + sx].units;
}
EI_REPLACE_METHOD(0x005C0930, CSectorMap::UnitsAt);

bool CSectorMap::Remove(CEngineObject* object) {
    CUnit* unit = AsUnit(object);
    int sx = unit->sectorCellX / 32, sy = unit->sectorCellY / 32;
    if (sx >= width || sy >= height) return false;

    Sector& sector = sectors[width * sy + sx];
    auto& list = object->GetClassId() == kClassUnit ? sector.units : sector.objects;
    auto* node = list.Find(object);
    if (!node) return false;
    list.RemoveAt(node);

    bool cleared = MarkFootprint(aiMap, nullptr, object, true);
    if (cleared) {
        unit->OnLeftSectorMap();
        unit->sectorCellX = -1;
        unit->sectorCellY = -1;
    }
    return cleared;
}
EI_REPLACE_METHOD(0x005C1010, CSectorMap::Remove);

namespace {
// Spiral of sector offsets ordered by distance, with a radius-to-length table. Globals at
// 007C2FC4 (lengths) and 007C2FFC (offset pairs); 005BF7D0 maps a radius to a table index.
struct SpiralTable;
__declspec(naked) int __fastcall SpiralIndex(SpiralTable*, void*, float) EI_ORIGINAL(0x005BF7D0)
SpiralTable* Spiral() { return reinterpret_cast<SpiralTable*>(0x007C2FD8); }
const int* SpiralLengths() { return *reinterpret_cast<const int* const*>(0x007C2FC4); }
const std::int16_t* SpiralOffsets() { return *reinterpret_cast<const std::int16_t* const*>(0x007C2FFC); }
} // namespace

void CSectorMap::FindUnitsInRadius(const Vector3* position, mfc::CTypedPtrList<CEngineObject>* result, float radius) {
    int length = SpiralLengths()[SpiralIndex(Spiral(), nullptr, radius * 0.03125f)];
    const std::int16_t* offset = SpiralOffsets();
    result->RemoveAll();

    int cellX = x87::fistp(1.0f / (kCellSize * 0.5f) * position->x - 0.5f);
    int cellY = x87::fistp(1.0f / (kCellSize * 0.5f) * position->y - 0.5f);
    int sx = cellX / 32, sy = cellY / 32;
    float radiusSquared = radius * radius;
    float px = position->x, py = position->y;

    // The units live in the AI map's own sector grid.
    auto* grid = reinterpret_cast<CSectorMap*>(reinterpret_cast<char*>(aiMap) + 0x124);
    for (int i = 0; i < length; ++i, offset += 2) {
        int gx = offset[0] + sx, gy = offset[1] + sy;
        if (gx < 0 || gy < 0 || gx >= grid->width || gy >= grid->height) continue;
        Sector& sector = grid->sectors[grid->width * gy + gx];
        for (auto* node = sector.units.head; node;) {
            CEngineObject* unit = node->data;
            node = node->next;
            float dx = PosX(unit) - px, dy = PosY(unit) - py;
            if (!(dy * dy + dx * dx >= radiusSquared)) result->AddHead(unit);
        }
    }
}
EI_REPLACE_METHOD(0x005C0BD0, CSectorMap::FindUnitsInRadius);

bool CSectorMap::Move(CEngineObject* object, const Vector3* position) {
    CUnit* unit = AsUnit(object);
    Vector3& current = *reinterpret_cast<Vector3*>(reinterpret_cast<char*>(object) + 0x1C);
    int cellX = x87::fistp(1.0f / (kCellSize * 0.5f) * position->x - 0.5f);
    int cellY = x87::fistp(1.0f / (kCellSize * 0.5f) * position->y - 0.5f);
    Vector3 saved;
    std::memcpy(&saved, &current, sizeof(saved));   // bitwise, like the original (no x87 round trip)

    int oldSx = unit->sectorCellX / 32, oldSy = unit->sectorCellY / 32;
    if (oldSx >= width || oldSy >= height) return false;
    int newSx = cellX / 32, newSy = cellY / 32;
    if (newSx >= width || newSy >= height) return false;
    bool changesSector = newSx != oldSx || newSy != oldSy;

    // Lift the footprint, move, and stamp it again; put it back where it was if it does not fit.
    bool moved = false;
    if (MarkFootprint(aiMap, nullptr, object, true)) {
        std::memcpy(&current, position, sizeof(current));
        moved = MarkFootprint(aiMap, nullptr, object, false);
        if (!moved) {
            std::memcpy(&current, &saved, sizeof(current));
            MarkFootprint(aiMap, nullptr, object, false);
        }
    }
    if (!moved) return false;

    if (changesSector) {
        Sector& from = sectors[width * oldSy + oldSx];
        auto& fromList = object->GetClassId() == kClassUnit ? from.units : from.objects;
        if (auto* node = fromList.Find(object)) fromList.RemoveAt(node);
        Sector& to = sectors[width * newSy + newSx];
        (object->GetClassId() == kClassUnit ? to.units : to.objects).AddTail(object);
    }
    object->flags_ |= kInSectorMap;
    unit->sectorCellY = cellY;
    unit->sectorCellX = cellX;
    return true;
}
EI_REPLACE_METHOD(0x005C11D0, CSectorMap::Move);
