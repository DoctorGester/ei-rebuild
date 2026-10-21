#include "ai_map.h"
#include "sector_map.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "game_data.h"
#include "ei/vcall.h"
#include "lib/nan_audit.h"

#include <cstring>


namespace {

// The world position of an object (+0x1C, +0x20; common to all objects in the sector map).
float PosX(const CEngineObject* o) { return static_cast<const CMapObject*>(o)->position.x; }
float PosY(const CEngineObject* o) { return static_cast<const CMapObject*>(o)->position.y; }
CUnit* AsUnit(CEngineObject* o) { return static_cast<CUnit*>(o); }

constexpr float kCellSize = 1.0f;   // 0073FDD0
constexpr float kZero = 0.0f;
} // namespace

// ---- Sectors ------------------------------------------------------------------------------------

void Sector::Clear() {
    freeOccluder = -1;
    occluders.SetSize(0, -1);
    objects.RemoveAll();
    units.RemoveAll();
}
EI_REPLACE_METHOD(0x005BFE00, Sector::Clear);

int Sector::NewOccluder() {
    if (freeOccluder >= 0) {
        int index = freeOccluder;
        freeOccluder = occluders[index].next;
        return index;
    }
    occluders.SetSize(occluders.size + 1, -1);
    return occluders.size - 1;
}
EI_REPLACE_METHOD(0x005BFE70, Sector::NewOccluder);

int Sector::RemoveOccluder(int head, int bottom, int top, int kind, int transparency) {
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

// 0059DF90: CArray<Sector>'s ConstructElements with the constructor inlined.
void __stdcall ConstructSectors(Sector* sectors, int count) {
    std::memset(static_cast<void*>(sectors), 0, sizeof(Sector) * static_cast<unsigned>(count));
    for (; count != 0; --count, ++sectors) {
        sectors->objects = {ei::GamePointer<void>(kSectorObjectListVtable), nullptr, nullptr, 0, nullptr, nullptr, 10};
        sectors->units = {mfc::PtrListVtable(), nullptr, nullptr, 0, nullptr, nullptr, 10};
        sectors->occluders.Construct(kSectorOccluderArrayVtable);
        ei::SetVtable(sectors, kSectorVtable);
        sectors->Clear();
    }
}
EI_REPLACE(0x0059DF90, ConstructSectors);

void SectorElements::Construct(Sector* sectors, int count) { ConstructSectors(sectors, count); }

void CSectorMap::Init(int cellsX, int cellsY, CAIMap* map) {
    width = cellsX / 32;
    height = cellsY / 32;
    added = 0;
    sectors.SetSize(width * height, -1);
    aiMap = map;
}
EI_REPLACE_METHOD(0x005C06F0, CSectorMap::Init);

void __fastcall CSectorMap::DestructSectors(mfc::CArray<Sector, SectorElements>* sectors) {
    sectors->vtable = ei::GamePointer<void>(kSectorArrayVtable);
    if (!sectors->data) return;
    SectorElements::Destruct(sectors->data, sectors->size);
    gamecrt::operator_delete(sectors->data);
}
EI_REPLACE(0x00599330, CSectorMap::DestructSectors);

void CSectorMap::DestructBody() {
    vtable = ei::GamePointer<void>(kVtableAddress);
    DestructSectors(&sectors);
}
EI_REPLACE_METHOD(0x0058A700, CSectorMap::DestructBody);

CSectorMap* CSectorMap::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0058A780, CSectorMap::DeletingDestruct);

bool CSectorMap::Add(CEngineObject* object) {
    int cellX = x87::fistp(1.0f / (kCellSize * 0.5f) * PosX(object) - 0.5f);
    int cellY = x87::fistp(1.0f / (kCellSize * 0.5f) * PosY(object) - 0.5f);
    int sx = cellX / 32, sy = cellY / 32;
    if (sx >= width || sy >= height || sx < 0 || sy < 0) return false;

    Sector& sector = sectors[width * sy + sx];
    if (object->GetClassId() == kClassUnit) sector.units.AddTail(AsUnit(object));
    else sector.objects.AddTail(object);

    bool fits = aiMap->MarkFootprint(object, false);
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
        if (auto* node = again.units.Find(AsUnit(object))) again.units.RemoveAt(node);
    } else {
        if (auto* node = again.objects.Find(object)) again.objects.RemoveAt(node);
    }
    return false;
}
EI_REPLACE_METHOD(0x005C0DF0, CSectorMap::Add);

mfc::CTypedPtrList<CUnit>* CSectorMap::UnitsAt(int sx, int sy) {
    if (sx < 0 || sy < 0 || sx >= width || sy >= height) return nullptr;
    return &sectors[width * sy + sx].units;
}

bool CSectorMap::Remove(CEngineObject* object) {
    CUnit* unit = AsUnit(object);
    int sx = unit->sectorCellX / 32, sy = unit->sectorCellY / 32;
    if (sx >= width || sy >= height) return false;

    Sector& sector = sectors[width * sy + sx];
    if (object->GetClassId() == kClassUnit) {
        auto* node = sector.units.Find(unit);
        if (!node) return false;
        sector.units.RemoveAt(node);
    } else {
        auto* node = sector.objects.Find(object);
        if (!node) return false;
        sector.objects.RemoveAt(node);
    }

    bool cleared = aiMap->MarkFootprint(object, true);
    if (cleared) {
        unit->NotifyCompanion();   // 0050E930
        unit->sectorCellX = -1;
        unit->sectorCellY = -1;
    }
    return cleared;
}
EI_REPLACE_METHOD(0x005C1010, CSectorMap::Remove);

namespace {
// Spiral of sector offsets ordered by distance, with a radius-to-length table. Globals at
// 007C2FC4 (lengths) and 007C2FFC (offset pairs); 005BF7D0 (RadiusTree::Find) maps a radius to
// a table index.
const RadiusTree* Spiral() { return ei::GamePointer<const RadiusTree>(0x007C2FD8); }
const int* SpiralLengths() { return ei::GameData<const int* const>(0x007C2FC4); }
const std::int16_t* SpiralOffsets() { return ei::GameData<const std::int16_t* const>(0x007C2FFC); }
} // namespace

int RadiusTree::Find(float radius) const {
    std::int32_t key;
    std::memcpy(&key, &radius, sizeof(key));
    int node = 1;
    for (int level = depth; level > 0; --level) {
        std::int32_t nodeKey;
        std::memcpy(&nodeKey, &keys[node], sizeof(nodeKey));
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
    keys.SetSize(2 << depth, -1);
    mask = (1 << depth) - 1;
    for (int level = 0; level <= depth; ++level) {
        const int width = 1 << level;
        const int offset = depth == level ? 0 : (1 << (depth - level - 1)) - 1;
        for (int k = 0; k < width; ++k) {
            int index = (1 << (depth - level)) * k + offset;
            if (index >= count) index = count - 1;
            keys.data[width + k] = values[index];
        }
    }
}
EI_REPLACE_METHOD(0x005BF5F0, RadiusTree::Build);

// ---- The spiral's tables ------------------------------------------------------------------------
//
// Built by a static initialiser, before the hooks are installed: the builder is registered at the
// original's addresses, but a running game never calls it. What it builds was compared with the
// original's tables once at start-up (verify_ai_static_tables.cpp until S21d).

namespace {

constexpr int kSpiralReach = 10;                                    // sectors either way
constexpr int kSpiralCells = (kSpiralReach * 2 + 1) * (kSpiralReach * 2 + 1);
constexpr float kBeyondSpiral = 1.0e30f;                            // 7149F2CA: the last radius
// The vtables of the builder's own arrays: the cells (CArray of 8-byte records) and the radii.
constexpr std::uintptr_t kSpiralCellArrayVtable = std::uintptr_t(0x0073FDE0);
constexpr std::uintptr_t kFloatArrayVtable = std::uintptr_t(0x0073D3E8);

// A sector of the square and how far it is: the gap between the sectors, in sectors.
struct SpiralCell {
    SpiralOffset offset;
    float distance;
};
static_assert(sizeof(SpiralCell) == 8);

// 005BFC10 (the sort's comparison): by distance, ascending.
int __cdecl ByDistance(const void* a, const void* b) {
    const float da = static_cast<const SpiralCell*>(a)->distance;
    const float db = static_cast<const SpiralCell*>(b)->distance;
    if (da > db) return 1;
    return da == db ? 0 : -1;
}

// |v| - 1, 0 for 0: the whole sectors between two sectors `v` apart.
int Gap(int v) { return v == 0 ? 0 : (v < 0 ? -v : v) - 1; }

} // namespace

void BuildSectorSpiralAt(mfc::CArray<int>* lengths, RadiusTree* tree, mfc::CArray<SpiralOffset>* offsets) {
    mfc::CArray<SpiralCell> cells;
    cells.Construct(kSpiralCellArrayVtable);
    cells.SetSize(kSpiralCells, -1);
    offsets->SetSize(kSpiralCells, -1);
    int filled = 0;
    for (int y = -kSpiralReach; y <= kSpiralReach; ++y) {
        for (int x = -kSpiralReach; x <= kSpiralReach; ++x) {
            // The original sums the squares as integers (FILD, FSQRT, stored as a float); the gaps
            // are 9 at most, so the float sum is the same number.
            const Vector2 gap{static_cast<float>(Gap(x)), static_cast<float>(Gap(y))};
            SpiralCell& cell = cells.data[filled++];
            cell.offset.x = static_cast<std::int16_t>(x);
            cell.offset.y = static_cast<std::int16_t>(y);
            cell.distance = Length2D(gap);
        }
    }
    // The game's own qsort: it decides the order of the sectors at the same distance.
    libcrt::Qsort(cells.data, static_cast<std::size_t>(cells.size), sizeof(SpiralCell), ByDistance);
    for (int i = 0; i < cells.size; ++i) offsets->data[i] = cells.data[i].offset;

    // A length per distinct distance: the offsets up to and including that distance.
    mfc::CArray<float> radii;
    radii.Construct(kFloatArrayVtable);
    lengths->SetSize(0, -1);
    radii.Add(0.0f);
    lengths->Add(1);
    for (int i = 0; i < cells.size; ++i) {
        const float distance = cells.data[i].distance;
        if (distance == radii.data[radii.size - 1]) {
            lengths->data[lengths->size - 1] = i + 1;
        } else {
            radii.Add(distance);
            lengths->Add(i + 1);
        }
    }
    radii.Add(kBeyondSpiral);
    lengths->Add(offsets->size);
    tree->Build(radii.data, radii.size);
    radii.FreeData();
    cells.FreeData();
}

// 005BF8F0 (thiscall on the initialiser's own object at 007C2FB8, which has no fields; returns it).
void* __fastcall BuildSectorSpiral(void* self) {
    BuildSectorSpiralAt(SpiralLengthArray(), SpiralRadiusTree(), SpiralOffsetArray());
    return self;
}
EI_REPLACE(0x005BF8F0, BuildSectorSpiral);

void CSectorMap::FindUnitsInRadius(const Vector3* position, mfc::CTypedPtrList<CUnit>* result, float radius) {
    int length = SpiralLengths()[Spiral()->Find(radius * 0.03125f)];
    const std::int16_t* offset = SpiralOffsets();
    result->RemoveAll();

    int cellX = x87::fistp(1.0f / (kCellSize * 0.5f) * position->x - 0.5f);
    int cellY = x87::fistp(1.0f / (kCellSize * 0.5f) * position->y - 0.5f);
    int sx = cellX / 32, sy = cellY / 32;
    float radiusSquared = radius * radius;
    float px = position->x, py = position->y;

    // The units live in the AI map's own sector grid.
    CSectorMap* grid = &aiMap->sectorMap;
    for (int i = 0; i < length; ++i, offset += 2) {
        int gx = offset[0] + sx, gy = offset[1] + sy;
        if (gx < 0 || gy < 0 || gx >= grid->width || gy >= grid->height) continue;
        Sector& sector = grid->sectors[grid->width * gy + gx];
        for (auto* node = sector.units.head; node;) {
            CUnit* unit = node->data;
            node = node->next;
            float dx = PosX(unit) - px, dy = PosY(unit) - py;
            const auto distance = LengthSquared2DUnrounded(dy, dx);
            EI_NAN_AUDIT(distance);
            if (distance < radiusSquared) result->AddHead(unit);
        }
    }
}
EI_REPLACE_METHOD(0x005C0BD0, CSectorMap::FindUnitsInRadius);

bool CSectorMap::Move(CEngineObject* object, const Vector3* position) {
    // Rebirth's FixMoveObject runs before the original: no move to a negative (or NaN) x or y.
    // (It also logs the refusal and failed moves to addon.log; nothing the game shows.)
    if (ei::rebirth::Enabled(ei::rebirth::Patch::FixMoveObject) && (!(position->x >= kZero) || !(position->y >= kZero)))
        return false;
    CUnit* unit = AsUnit(object);
    Vector3& current = static_cast<CMapObject*>(object)->position;
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
    if (aiMap->MarkFootprint(object, true)) {
        std::memcpy(&current, position, sizeof(current));
        moved = aiMap->MarkFootprint(object, false);
        if (!moved) {
            std::memcpy(&current, &saved, sizeof(current));
            aiMap->MarkFootprint(object, false);
        }
    }
    if (!moved) return false;

    if (changesSector) {
        Sector& from = sectors[width * oldSy + oldSx];
        Sector& to = sectors[width * newSy + newSx];
        if (object->GetClassId() == kClassUnit) {
            if (auto* node = from.units.Find(unit)) from.units.RemoveAt(node);
            to.units.AddTail(unit);
        } else {
            if (auto* node = from.objects.Find(object)) from.objects.RemoveAt(node);
            to.objects.AddTail(object);
        }
    }
    object->flags_ |= kInSectorMap;
    unit->sectorCellY = cellY;
    unit->sectorCellX = cellX;
    return true;
}
EI_REPLACE_METHOD(0x005C11D0, CSectorMap::Move);
