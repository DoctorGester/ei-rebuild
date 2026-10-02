#include "sector_map.h"

#include "ei/hook.h"
#include "ei/x87.h"

__declspec(naked) void CUnit::OnLeftSectorMap() EI_ORIGINAL(0x0050E930)
__declspec(naked) bool CAIMap::MarkFootprint(CEngineObject*, int) EI_ORIGINAL(0x005B6A80)

namespace {
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

    bool fits = aiMap->MarkFootprint(object, 0);
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

bool CSectorMap::Remove(CEngineObject* object) {
    CUnit* unit = AsUnit(object);
    int sx = unit->sectorCellX / 32, sy = unit->sectorCellY / 32;
    if (sx >= width || sy >= height) return false;

    Sector& sector = sectors[width * sy + sx];
    auto& list = object->GetClassId() == kClassUnit ? sector.units : sector.objects;
    auto* node = list.Find(object);
    if (!node) return false;
    list.RemoveAt(node);

    bool cleared = aiMap->MarkFootprint(object, 1);
    if (cleared) {
        unit->OnLeftSectorMap();
        unit->sectorCellX = -1;
        unit->sectorCellY = -1;
    }
    return cleared;
}
EI_REPLACE_METHOD(0x005C1010, CSectorMap::Remove);
