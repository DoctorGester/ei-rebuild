// Object footprints in the AI map: collecting the cells an object's model parts cover, stamping
// them into the occluder grid, and refreshing passability around them.
#include "ai_map.h"
#include "ai_unit_layer.h"
#include "record_arrays.h"

#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/vecmath.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

// --- Footprint cell sets -------------------------------------------------------------------------

FootprintCellSet* FootprintCellSet::Construct() {
    std::construct_at(&cells);
    hashed = true;
    return this;
}

// The spells' footprint set, empty at the start: built by a static initialiser, freed by its exit function.
namespace spellfx {
constinit lib::StaticObject<FootprintCellSet> g_spellFootprint = /*007C8478*/ {};
}
using spellfx::g_spellFootprint;

namespace {
void __cdecl FreeSpellFootprint() { lib::FreeStorage(g_spellFootprint->cells); }
} // namespace

void __cdecl ConstructSpellFootprint() {
    g_spellFootprint->Construct();
    libcrt::AtExit(FreeSpellFootprint);
}

void FootprintCellSet::SetSize(int size) { cells.resize(static_cast<std::size_t>(size)); }

namespace {
FootprintCell* FindCell(FootprintCellSet* set, int x, int y) {
    for (int i = set->buckets[(x + y) & 15]; i >= 0; i = set->cells[i].next) {
        if (set->cells[i].x == x && set->cells[i].y == y) return &set->cells[i];
    }
    return nullptr;
}

void AppendCell(FootprintCellSet* set, const FootprintCell& cell) {
    int bucket = (cell.x + cell.y) & 15;
    int index = std::ssize(set->cells);
    set->SetSize(index + 1);
    set->cells[index] = cell;
    set->cells[std::ssize(set->cells) - 1].next = set->buckets[bucket];
    set->buckets[bucket] = std::ssize(set->cells) - 1;
}
} // namespace

void FootprintCellSet::AddTop(int x, int y, int height, std::int8_t kind, std::int8_t transparency) {
    hashed = true;
    if (FootprintCell* cell = FindCell(this, x, y)) {
        if (!(cell->high > height)) cell->high = height;
        if (kind < cell->kind) cell->kind = kind;
        if (transparency < cell->transparency) cell->transparency = transparency;
        return;
    }
    FootprintCell cell{x, y, 0x7FFFFFFF, height, kind, transparency, 0, 0, 0};
    AppendCell(this, cell);
}

void FootprintCellSet::AddBottom(int x, int y, int height, std::int8_t kind, std::int8_t transparency) {
    hashed = true;
    if (FootprintCell* cell = FindCell(this, x, y)) {
        if (!(cell->low < height)) cell->low = height;
        return;
    }
    FootprintCell cell{x, y, height, 0, kind, transparency, 0, 0, 0};
    AppendCell(this, cell);
}

void FootprintCellSet::GetBounds(int* bounds) const {
    bounds[0] = 100000;
    bounds[2] = 100000;
    bounds[3] = 0;
    bounds[1] = 0;
    for (int i = 0; i < std::ssize(cells); ++i) {
        int x = cells[i].x, y = cells[i].y;
        if (!(bounds[0] < x)) bounds[0] = x;
        if (!(bounds[2] < y)) bounds[2] = y;
        if (!(bounds[1] > x)) bounds[1] = x;
        if (!(bounds[3] > y)) bounds[3] = y;
    }
}

// --- Footprints ----------------------------------------------------------------------------------

namespace {
const char* PartNotFoundFormat() { return "Bodypart \"%s\" for object \"%s\" not found\n"; }

void ResetCellSet(FootprintCellSet* set) {
    set->SetSize(0);
    if (set->hashed) {
        for (int& b : set->buckets) b = -1;
    }
}
} // namespace

bool CAIMap::CollectFootprint(CMapObject* object, FootprintCellSet* footprintSet, FootprintCellSet* bridgeSet) {
    // The object's transform: rotation from its orientation, translation from its position.
    float* matrix = object->worldMatrix.m;
    object->localRotation_.ToMatrix((float*)matrix);
    std::memcpy(&matrix[11], &object->localPosition_.z, 4);
    std::memcpy(&matrix[3], &object->localPosition_.x, 4);
    std::memcpy(&matrix[7], &object->localPosition_.y, 4);
    matrix[14] = 0.0f;
    matrix[13] = 0.0f;
    matrix[12] = 0.0f;
    matrix[15] = 1.0f;

    // Part names: the server companion's string array if it has any, else the object's own (the AI map lives on
    // the server, so the companion is a CMapObjectServer).
    auto* server = object->ServerCompanion();
    const std::vector<std::string>* names = &server->strings;
    if (names->empty()) names = &object->partNames;

    bool hasBridge = false;
    for (const std::string& name : *names) {
        int index = object->FindPartIndex(name.c_str());
        if (index < 0) {
            game::FatalError(PartNotFoundFormat(), name.c_str(), object->name.c_str());
            continue;
        }
        const FootprintPart* part = &object->partBoxes[index];
        std::uint32_t flags = part->bounds.flags;
        if (flags & 0x800) continue;
        std::int8_t kind = static_cast<std::int8_t>((flags >> 3) & 7);
        std::int8_t transparency = static_cast<std::int8_t>((flags >> 6) & 0xF);
        FootprintCellSet* set = footprintSet;
        if (flags & 0x400) {
            hasBridge = true;
            set = bridgeSet;
        }

        // The box as a strip of 8 corners: faces (k, k+1, k+2) for k = 0..5 cover its 6 sides,
        // each with an outward normal.
        const Vector3 &lo = part->bounds.boxMin, &hi = part->bounds.boxMax;
        Vector3 corner[8] = {{hi.x, lo.y, lo.z}, {lo.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {lo.x, hi.y, hi.z},
                             {hi.x, hi.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, lo.y, lo.z}, {lo.x, lo.y, lo.z}};
        Vector3 normal[6] = {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {0, -1, 0}};
        Vector3 position;
        std::memcpy(&position, &object->localPosition_, sizeof(position));

        // Stand the object on the ground (or bridge deck) under its centre.
        int cx = WorldToCell(object->localPosition_.x), cy = WorldToCell(object->localPosition_.y);
        float ground;
        if (cx < 0 || cy < 0 || cx >= cells.width || cy >= cells.height) {
            ground = 0.0f;
        } else if (cells[cy][cx].flags & kCellBridge) {
            ground = static_cast<float>(bridgeHeights.HeightAt(static_cast<short>(cx), static_cast<short>(cy))) / heightScale;
        } else {
            ground = GroundHeight(cx, cy);
        }
        position.z = ground + position.z;

        for (Vector3& p : corner) {
            const FootprintPart* placed = &object->partBoxes[index];
            float y = placed->pivot.y + p.y;
            float z = placed->pivot.z + p.z;
            float x = p.x + placed->pivot.x + placed->bounds.center.x;
            p.x = x;
            p.y = y + placed->bounds.center.y;
            p.z = z + placed->bounds.center.z;
            Vector3 world;
            object->worldMatrix.rows.Transform(&world, &p);
            std::memcpy(&p, &world, sizeof(p));
            p.x = position.x + p.x;
            p.y = position.y + p.y;
            p.z = position.z + p.z;
        }
        for (Vector3& v : normal) {
            Vector3 world;
            object->worldMatrix.rows.Transform(&world, &v);
            std::memcpy(&v, &world, sizeof(v));
        }

        // Sample each face on a grid fine enough to hit every cell it covers.
        for (int k = 0; k < 6; ++k) {
            const Vector3 &p0 = corner[k], &p1 = corner[k + 1], &p2 = corner[k + 2];
            float ax = p0.x - p1.x, ay = p0.y - p1.y, az = p0.z - p1.z;
            float bx = p2.x - p1.x, by = p2.y - p1.y, bz = p2.z - p1.z;
            double la = std::sqrt(LengthSquared2DUnrounded(ay, ax));
            int stepsA = x87::fistp(static_cast<float>((la + la) * 1.5f)) + 2;
            double lb = std::sqrt(LengthSquared2DUnrounded(by, bx));
            int stepsB = x87::fistp(static_cast<float>((lb + lb) * 1.5f)) + 2;
            float countA = static_cast<float>(stepsA);
            float dax = ax / countA, day = ay / countA, daz = az / countA;
            float countB = static_cast<float>(stepsB);
            float dbx = bx / countB, dby = by / countB, dbz = bz / countB;

            for (int i = 0; i < stepsA; ++i) {
                float fi = static_cast<float>(i) + 0.5f;
                float iax = fi * dax, iay = day * fi, iaz = daz * fi;
                for (int j = 0; j < stepsB; ++j) {
                    float fj = static_cast<float>(j) + 0.5f;
                    float jbx = dbx * fj, jby = dby * fj, jbz = fj * dbz;
                    float sx = iax + p1.x + jbx;
                    float sy = iay + p1.y + jby;
                    float sz = iaz + p1.z + jbz;
                    int x = WorldToCell(sx), y = WorldToCell(sy);
                    if (!(x < cells.width && y < cells.height && x >= 0 && y >= 0)) continue;
                    int z = x87::fistp(sz * heightScale);
                    if (z > 0x3FC) z = 0x3FC;
                    else if (z < 0) z = 0;
                    if (normal[k].z > 0.0f) set->AddTop(x, y, z, kind, transparency);
                    else set->AddBottom(x, y, z, kind, transparency);   // also NaN
                    if (i == 0 || j == 0 || i == stepsA - 1 || j == stepsB - 1) {
                        if (FootprintCell* cell = FindCell(set, x, y)) cell->edge = 1;
                    }
                }
            }
        }
    }
    return hasBridge;
}

void CAIMap::QueueRefresh(int minX, int maxX, int minY, int maxY) {
    if (!batching) {
        RefreshArea(minX, maxX, minY, maxY);
        return;
    }
    if (dirty.minX < minX) minX = dirty.minX;
    dirty.minX = minX;
    if (dirty.maxX > maxX) maxX = dirty.maxX;
    dirty.maxX = maxX;
    if (!(dirty.minY < minY)) dirty.minY = minY;
    if (!(dirty.maxY > maxY)) dirty.maxY = maxY;
}

bool CAIMap::MarkFootprint(CMapObject* object, bool remove) {
    int cellX = WorldToCell(object->localPosition_.x);
    int cellY = WorldToCell(object->localPosition_.y);
    ResetCellSet(&footprint);
    ResetCellSet(&bridges);

    if (CUnit* unit = UnitOrNull(object)) {
        if (unit->IsDead()) return true;
        auto* shape = static_cast<const CUnitShape*>(UnitShape(unit->standingHeight));
        if (!shape) return true;
        CUnitLayer* layer = unit->IsLargeUnit() ? static_cast<CUnitLayer*>(&unitLayerLarge) : &unitLayerSmall;
        if (remove) layer->Remove(unit, cellX - 8, cellY - 8);
        else layer->Add(unit, shape, cellX - 8, cellY - 8);
        return true;
    }

    if (CollectFootprint(object, &footprint, &bridges)) ApplyBridgeDecks(&bridges, remove);
    bool fits = ApplyFootprint(&footprint, remove);

    int a[4] = {10000, 0, 10000, 0}, b[4] = {10000, 0, 10000, 0};
    footprint.GetBounds(a);
    bridges.GetBounds(b);
    int minX = a[0] < b[0] ? a[0] : b[0];
    int maxX = a[1] > b[1] ? a[1] : b[1];
    int minY = a[2] < b[2] ? a[2] : b[2];
    int maxY = a[3] > b[3] ? a[3] : b[3];

    std::uint8_t savedRefreshUnits = refreshUnits;
    refreshUnits = savedRefreshUnits && object->KeepsMapFlag();
    QueueRefresh(minX - 3, maxX + 3, minY - 3, maxY + 3);
    refreshUnits = savedRefreshUnits;
    return fits;
}

bool CAIMap::ApplyFootprint(FootprintCellSet* set, bool remove) {
    if (remove) {
        for (int i = 0; i < std::ssize(set->cells); ++i) ClearOccluderRecord(&set->cells[i]);
        return true;
    }
    for (int i = 0; i < std::ssize(set->cells); ++i) {
        if (!AddOccluderRecord(&set->cells[i])) {
            for (int j = 0; j < i; ++j) ClearOccluderRecord(&set->cells[j]);
            return false;
        }
    }
    return true;
}

bool CAIMap::StampFootprint(int tick, FootprintCellSet* set) {
    std::uint8_t savedRefreshUnits = refreshUnits;
    refreshUnits = false;
    bool fits = ApplyFootprint(set, false);
    int r[4] = {10000, 0, 10000, 0};
    set->GetBounds(r);
    QueueRefresh(r[0] - 3, r[1] + 3, r[2] - 3, r[3] + 3);
    footprintTick = tick;
    refreshUnits = savedRefreshUnits;
    return fits;
}

bool CAIMap::ClearFootprint(FootprintCellSet* set) {
    std::uint8_t savedRefreshUnits = refreshUnits;
    refreshUnits = false;
    bool cleared = ApplyFootprint(set, true);
    int r[4] = {10000, 0, 10000, 0};
    set->GetBounds(r);
    QueueRefresh(r[0] - 3, r[1] + 3, r[2] - 3, r[3] + 3);
    refreshUnits = savedRefreshUnits;
    return cleared;
}

void CAIMap::BeginRefresh() {
    if (batching) {
        RefreshArea(dirty.minX, dirty.maxX, dirty.minY, dirty.maxY);
        batching = false;
    }
    dirty.Reset();
    batching = true;
}

void CAIMap::EndRefresh() {
    RefreshArea(dirty.minX, dirty.maxX, dirty.minY, dirty.maxY);
    batching = false;
}
