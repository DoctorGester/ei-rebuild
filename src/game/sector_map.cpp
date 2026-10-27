#include "ai_map.h"
#include "sector_map.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/compiled_vtable.h"
#include "lib/crt.h"
#include "game_data.h"
#include "record_arrays.h"
#include "lib/nan_audit.h"
#include "lib/mfc_slots.h"

#include <cstring>


namespace {

constexpr float kCellSize = 1.0f;
constexpr float kZero = 0.0f;

} // namespace

// ---- Sectors ------------------------------------------------------------------------------------

void Sector::Clear() {
    freeOccluder = -1;
    occluders.SetSize(0, -1);
    objects.RemoveAll();
    units.RemoveAll();
}
EI_REWRITTEN(0x005BFE00, Sector::Clear);

int Sector::NewOccluder() {
    if (freeOccluder >= 0) {
        int index = freeOccluder;
        freeOccluder = occluders[index].next;
        return index;
    }
    occluders.SetSize(occluders.size + 1, -1);
    return occluders.size - 1;
}
EI_REWRITTEN(0x005BFE70, Sector::NewOccluder);

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
EI_REWRITTEN(0x005BFFD0, Sector::RemoveOccluder);

void Sector::FreeOccluder(int index) {
    occluders[index].next = static_cast<std::int16_t>(freeOccluder);
    freeOccluder = index;
}
EI_REWRITTEN(0x005C0060, Sector::FreeOccluder);

Sector::Sector() {
    objects = {mfc::impl::ClientObjectListClass::Vtable(), nullptr, nullptr, 0, nullptr, nullptr, 10};
    units = {mfc::impl::PtrListClass::Vtable(), nullptr, nullptr, 0, nullptr, nullptr, 10};
    occluders.Construct(mfc::impl::SectorOccluderArrayClass::Vtable());
    Clear();
}

// CArray<Sector>'s ConstructElements with the constructor inlined.
void __stdcall ConstructSectors(Sector* sectors, int count) {
    std::memset(static_cast<void*>(sectors), 0, sizeof(Sector) * static_cast<unsigned>(count));
    for (; count != 0; --count, ++sectors) new (sectors) Sector();
}
EI_REWRITTEN(0x0059DF90, ConstructSectors);

void SectorElements::Construct(Sector* sectors, int count) { ConstructSectors(sectors, count); }

void CSectorMap::Init(int cellsX, int cellsY, CAIMap* map) {
    width = cellsX / 32;
    height = cellsY / 32;
    added = 0;
    sectors.SetSize(width * height, -1);
    aiMap = map;
}
EI_REWRITTEN(0x005C06F0, CSectorMap::Init);

void __fastcall CSectorMap::DestructSectors(mfc::CArray<Sector, SectorElements>* sectors) {
    sectors->vtable = mfc::impl::SectorArrayClass::Vtable();
    if (!sectors->data) return;
    SectorElements::Destruct(sectors->data, sectors->size);
    gamecrt::operator_delete(sectors->data);
}
EI_REWRITTEN(0x00599330, CSectorMap::DestructSectors);
EI_REWRITTEN(0x005BD1C0, mfc::impl::ArrayHeader<mfc::impl::SectorArrayClass>::Construct);   // the sectors array's constructor (SectorArrayClass is built from DestructSectors: not in ei_vectors)

CSectorMap::CSectorMap() {
    mfc::impl::ConstructArrayOf<mfc::impl::SectorArrayClass>(sectors);
    aiMap = nullptr;
    height = 0;
    added = 0;
    width = 0;
}

CSectorMap::~CSectorMap() { DestructSectors(&sectors); }
EI_REWRITTEN_SPECIAL(0x0058A700, CSectorMap::~CSectorMap);
EI_REWRITTEN_SPECIAL(0x0058A780, CSectorMap::~CSectorMap);

bool CSectorMap::Add(CMapObject* object) {
    int cellX = x87::fistp(1.0f / (kCellSize * 0.5f) * object->localPosition_.x - 0.5f);
    int cellY = x87::fistp(1.0f / (kCellSize * 0.5f) * object->localPosition_.y - 0.5f);
    int sx = cellX / 32, sy = cellY / 32;
    if (sx >= width || sy >= height || sx < 0 || sy < 0) return false;

    Sector& sector = sectors[width * sy + sx];
    if (CUnit* unit = UnitOrNull(object)) sector.units.AddTail(unit);
    else sector.objects.AddTail(object);

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
        if (auto* node = again.units.Find(unit)) again.units.RemoveAt(node);
    } else {
        if (auto* node = again.objects.Find(object)) again.objects.RemoveAt(node);
    }
    return false;
}
EI_REWRITTEN(0x005C0DF0, CSectorMap::Add);

mfc::CTypedPtrList<CUnit>* CSectorMap::UnitsAt(int sx, int sy) {
    if (sx < 0 || sy < 0 || sx >= width || sy >= height) return nullptr;
    return &sectors[width * sy + sx].units;
}
EI_REWRITTEN(0x005C0930, CSectorMap::UnitsAt);

bool CSectorMap::Remove(CMapObject* object) {
    int sx = object->sectorCellX / 32, sy = object->sectorCellY / 32;
    if (sx >= width || sy >= height) return false;

    Sector& sector = sectors[width * sy + sx];
    if (CUnit* unit = UnitOrNull(object)) {
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
        object->NotifyCompanion();
        object->sectorCellX = -1;
        object->sectorCellY = -1;
    }
    return cleared;
}
EI_REPLACE_METHOD(0x005C1010, CSectorMap::Remove);

namespace {
// Spiral of sector offsets ordered by distance, with a radius-to-length table (sector_spiral.cpp);
// RadiusTree::Find maps a radius to a table index.
const RadiusTree* Spiral() { return SpiralRadiusTree(); }
const int* SpiralLengths() { return SpiralLengthArray()->data; }
const std::int16_t* SpiralOffsets() { return &SpiralOffsetArray()->data->x; }
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
EI_REWRITTEN(0x005BF7D0, RadiusTree::Find);

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
EI_REWRITTEN(0x005BF5F0, RadiusTree::Build);

// ---- The spiral's tables ------------------------------------------------------------------------
// Built by a static initialiser at start-up; the builder is also registered at its
// addresses, but a running game never calls it.

namespace {

constexpr int kSpiralReach = 10;                                    // sectors either way
constexpr int kSpiralCells = (kSpiralReach * 2 + 1) * (kSpiralReach * 2 + 1);
constexpr float kBeyondSpiral = 1.0e30f;                            // 7149F2CA: the last radius

// A sector of the square and how far it is: the gap between the sectors, in sectors.
struct SpiralCell {
    SpiralOffset offset;
    float distance;
};
static_assert(sizeof(SpiralCell) == 8);

// The sort's comparison: by distance, ascending.
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
    cells.Construct(mfc::impl::SpiralCellArrayClass::Vtable());
    cells.SetSize(kSpiralCells, -1);
    offsets->SetSize(kSpiralCells, -1);
    int filled = 0;
    for (int y = -kSpiralReach; y <= kSpiralReach; ++y) {
        for (int x = -kSpiralReach; x <= kSpiralReach; ++x) {
            // The squares are summed as integers (FILD, FSQRT, stored as a float); the gaps
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
    radii.Construct(mfc::impl::FloatArrayClass::Vtable());
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

// Builds the spiral in the tables (the static initialiser's object has no fields; returns it).
void* __fastcall BuildSectorSpiral(void* self) {
    BuildSectorSpiralAt(SpiralLengthArray(), SpiralRadiusTree(), SpiralOffsetArray());
    return self;
}
EI_REWRITTEN(0x005BF8F0, BuildSectorSpiral);

// The static initialiser that builds the spiral at its place in the list. The object it passes
// has no fields and nothing reads it.
void BuildSectorSpiralStatic() { BuildSectorSpiral(nullptr); }
EI_REPLACE(0x005BF8E0, BuildSectorSpiralStatic);

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
            float dx = unit->localPosition_.x - px, dy = unit->localPosition_.y - py;
            const auto distance = LengthSquared2DUnrounded(dy, dx);
            EI_NAN_AUDIT(distance);
            if (distance < radiusSquared) result->AddHead(unit);
        }
    }
}
EI_REWRITTEN(0x005C0BD0, CSectorMap::FindUnitsInRadius);

bool CSectorMap::Move(CMapObject* object, const Vector3* position) {
    // Rebirth's FixMoveObject runs first: no move to a negative (or NaN) x or y.
    // (It also logs the refusal and failed moves to addon.log; nothing the game shows.)
    if (ei::rebirth::Enabled(ei::rebirth::Patch::FixMoveObject) && (!(position->x >= kZero) || !(position->y >= kZero)))
        return false;
    Vector3& current = object->localPosition_;
    int cellX = x87::fistp(1.0f / (kCellSize * 0.5f) * position->x - 0.5f);
    int cellY = x87::fistp(1.0f / (kCellSize * 0.5f) * position->y - 0.5f);
    Vector3 saved;
    std::memcpy(&saved, &current, sizeof(saved));   // bitwise (no x87 round trip)

    int oldSx = object->sectorCellX / 32, oldSy = object->sectorCellY / 32;
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
        if (CUnit* unit = UnitOrNull(object)) {
            if (auto* node = from.units.Find(unit)) from.units.RemoveAt(node);
            to.units.AddTail(unit);
        } else {
            if (auto* node = from.objects.Find(object)) from.objects.RemoveAt(node);
            to.objects.AddTail(object);
        }
    }
    object->flags_ |= kInSectorMap;
    object->sectorCellY = cellY;
    object->sectorCellX = cellX;
    return true;
}
EI_REWRITTEN(0x005C11D0, CSectorMap::Move);
