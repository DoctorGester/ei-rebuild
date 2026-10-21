// Object footprints in the AI map: collecting the cells an object's model parts cover, stamping
// them into the occluder grid, and refreshing passability around them.
#include "ai_map.h"
#include "ai_unit_layer.h"
#include "record_arrays.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "originals_render.h"
#include "originals_units.h"

#include <cstring>

// --- Footprint cell sets -------------------------------------------------------------------------

FootprintCellSet* FootprintCellSet::Construct() {
    cells.Construct();
    hashed = true;
    return this;
}
EI_REPLACE_METHOD(0x0067E4E0, FootprintCellSet::Construct);

// The spells' footprint set (spell_effect_internal.h), empty at the start: built by the static initialiser 0067E4C0 in the
// list's order and freed by the exit function it registers (0067E500); both ported on the name and registered at their
// addresses (docs/notes/standalone.md, "The static initialisers' objects").
namespace spellfx {
constinit lib::StaticObject<FootprintCellSet> g_spellFootprint = /*007C8478*/ {};
}
using spellfx::g_spellFootprint;

namespace {
void __cdecl ConstructSpellFootprint() {   // 0067E4C0
    g_spellFootprint->Construct();
    GameAtExit(ei::Code<void(__cdecl*)()>(0x0067E500));
}
void __cdecl FreeSpellFootprint() { g_spellFootprint->cells.FreeData(); }   // 0067E500
} // namespace

EI_REPLACE(0x0067E4C0, ConstructSpellFootprint);
EI_REPLACE(0x0067E500, FreeSpellFootprint);

void FootprintCellSet::SetSize(int size, int grow) { cells.SetSize(size, grow); }
EI_REWRITTEN(0x005BD350, FootprintCellSet::SetSize);

namespace {
FootprintCell* FindCell(FootprintCellSet* set, int x, int y) {
    for (int i = set->buckets[(x + y) & 15]; i >= 0; i = set->cells[i].next) {
        if (set->cells[i].x == x && set->cells[i].y == y) return &set->cells[i];
    }
    return nullptr;
}

void AppendCell(FootprintCellSet* set, const FootprintCell& cell) {
    int bucket = (cell.x + cell.y) & 15;
    int index = set->cells.size;
    set->SetSize(index + 1, -1);
    set->cells[index] = cell;
    set->cells[set->cells.size - 1].next = set->buckets[bucket];
    set->buckets[bucket] = set->cells.size - 1;
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
EI_REWRITTEN(0x005B2D10, FootprintCellSet::AddTop);

void FootprintCellSet::AddBottom(int x, int y, int height, std::int8_t kind, std::int8_t transparency) {
    hashed = true;
    if (FootprintCell* cell = FindCell(this, x, y)) {
        if (!(cell->low < height)) cell->low = height;
        return;
    }
    FootprintCell cell{x, y, height, 0, kind, transparency, 0, 0, 0};
    AppendCell(this, cell);
}
EI_REWRITTEN(0x005B2E10, FootprintCellSet::AddBottom);

void FootprintCellSet::GetBounds(int* bounds) const {
    bounds[0] = 100000;
    bounds[2] = 100000;
    bounds[3] = 0;
    bounds[1] = 0;
    for (int i = 0; i < cells.size; ++i) {
        int x = cells[i].x, y = cells[i].y;
        if (!(bounds[0] < x)) bounds[0] = x;
        if (!(bounds[2] < y)) bounds[2] = y;
        if (!(bounds[1] > x)) bounds[1] = x;
        if (!(bounds[3] > y)) bounds[3] = y;
    }
}
EI_REWRITTEN(0x005B2F40, FootprintCellSet::GetBounds);

// --- Footprints ----------------------------------------------------------------------------------


namespace {
// Object model helpers not rewritten yet.
__declspec(naked) Vector3* __fastcall TransformVector(const void*, void*, Vector3*, const Vector3*)
    EI_ORIGINAL(0x00560E10)
// CStringArray element by value (004698A0: a CString constructed into `out`).
mfc::CString* StringArrayGetAt(const recordarrays::PartNames* names, mfc::CString* out, int index) {
    names->CopyAt(out, index);
    return out;
}

// "Bodypart \"%s\" for object \"%s\" not found\n"
const char* PartNotFoundFormat() { return "Bodypart \"%s\" for object \"%s\" not found\n"; }

// A CStringArray (MFC: vtable, CString data, size, capacity, grow-by), as the server companion and the
// 3D object lay theirs out by hand (unit.h, map_object_figures.h).
using NameArray = recordarrays::PartNames;

void ResetCellSet(FootprintCellSet* set) {
    set->SetSize(0, -1);
    if (set->hashed) {
        for (int& b : set->buckets) b = -1;
    }
}
} // namespace

bool CAIMap::CollectFootprint(CEngineObject* object, FootprintCellSet* footprintSet, FootprintCellSet* bridgeSet) {
    // The object's transform: rotation from its orientation, translation from its position.
    CMapObject* mapObject = ObjectSlots(object);
    float* matrix = mapObject->worldMatrix.m;
    QuaternionToMatrixOriginal(&mapObject->orientation, nullptr, (float*)matrix);
    std::memcpy(&matrix[11], &mapObject->position.z, 4);
    std::memcpy(&matrix[3], &mapObject->position.x, 4);
    std::memcpy(&matrix[7], &mapObject->position.y, 4);
    matrix[14] = 0.0f;
    matrix[13] = 0.0f;
    matrix[12] = 0.0f;
    matrix[15] = 1.0f;

    // Part names: the server companion's string array (+0x80) if it has any, else the object's own.
    // The AI map lives on the server, so the companion is a CMapObjectServer (the client one is 0x58
    // bytes).
    auto* server = static_cast<CMapObjectServer*>(mapObject->companion);
    const NameArray* names = reinterpret_cast<const NameArray*>(&server->strings);
    if (names->size == 0) names = reinterpret_cast<const NameArray*>(&mapObject->partNames);

    bool hasBridge = false;
    for (int n = 0; n < names->size; ++n) {
        int index;
        {
            mfc::CString name(mfc::CString::kUnset);
            index = mapObject->FindPartIndex(*StringArrayGetAt(names, &name, n));
        }
        if (index < 0) {
            const char* objectName = mapObject->name;
            mfc::CString name(mfc::CString::kUnset);
            game::FatalError(PartNotFoundFormat(), static_cast<const char*>(*StringArrayGetAt(names, &name, n)), objectName);
            continue;
        }
        const FootprintPart* part = &mapObject->partBoxes[index];
        std::uint32_t flags = part->flags;
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
        const Vector3 &lo = part->boxMin, &hi = part->boxMax;
        Vector3 corner[8] = {{hi.x, lo.y, lo.z}, {lo.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {lo.x, hi.y, hi.z},
                             {hi.x, hi.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, lo.y, lo.z}, {lo.x, lo.y, lo.z}};
        Vector3 normal[6] = {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {0, -1, 0}};
        Vector3 position;
        std::memcpy(&position, &mapObject->position, sizeof(position));

        // Stand the object on the ground (or bridge deck) under its centre.
        int cx = WorldToCell(mapObject->position.x), cy = WorldToCell(mapObject->position.y);
        float ground;
        if (cx < 0 || cy < 0 || cx >= width || cy >= height) {
            ground = 0.0f;
        } else if (cells[cy][cx].flags & kCellBridge) {
            ground = static_cast<float>(BridgeHeight(&bridgeHeights, nullptr, static_cast<short>(cx),
                                                     static_cast<short>(cy))) / heightScale;
        } else {
            ground = GroundHeight(cx, cy);
        }
        position.z = ground + position.z;

        for (Vector3& p : corner) {
            const FootprintPart* placed = &mapObject->partBoxes[index];
            float y = placed->pivot.y + p.y;
            float z = placed->pivot.z + p.z;
            float x = p.x + placed->pivot.x + placed->offset.x;
            p.x = x;
            p.y = y + placed->offset.y;
            p.z = z + placed->offset.z;
            Vector3 world;
            TransformVector(matrix, nullptr, &world, &p);
            std::memcpy(&p, &world, sizeof(p));
            p.x = position.x + p.x;
            p.y = position.y + p.y;
            p.z = position.z + p.z;
        }
        for (Vector3& v : normal) {
            Vector3 world;
            TransformVector(matrix, nullptr, &world, &v);
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
                    if (!(x < width && y < height && x >= 0 && y >= 0)) continue;
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
    if (dirtyMinX < minX) minX = dirtyMinX;
    dirtyMinX = minX;
    if (dirtyMaxX > maxX) maxX = dirtyMaxX;
    dirtyMaxX = maxX;
    if (!(dirtyMinY < minY)) dirtyMinY = minY;
    if (!(dirtyMaxY > maxY)) dirtyMaxY = maxY;
}

bool CAIMap::MarkFootprint(CEngineObject* object, bool remove) {
    int cellX = WorldToCell(ObjectSlots(object)->position.x);
    int cellY = WorldToCell(ObjectSlots(object)->position.y);
    ResetCellSet(&footprint);
    ResetCellSet(&bridges);

    if (CUnit* unit = UnitOrNull(object)) {
        if (unit->IsDead()) return true;
        auto* shape = static_cast<const CUnitShape*>(UnitShape(unit->standingHeight));
        if (!shape) return true;
        CUnitLayer* layer = unit->IsLargeUnit() ? &unitLayerLarge : &unitLayerSmall;
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

    bool savedRefreshUnits = refreshUnits;
    refreshUnits = savedRefreshUnits && ObjectSlots(object)->KeepsMapFlag();
    QueueRefresh(minX - 3, maxX + 3, minY - 3, maxY + 3);
    refreshUnits = savedRefreshUnits;
    return fits;
}
EI_REWRITTEN(0x005B6A80, CAIMap::MarkFootprint);

bool CAIMap::ApplyFootprint(FootprintCellSet* set, bool remove) {
    if (remove) {
        for (int i = 0; i < set->cells.size; ++i) ClearOccluderRecord(&set->cells[i]);
        return true;
    }
    for (int i = 0; i < set->cells.size; ++i) {
        if (!AddOccluderRecord(&set->cells[i])) {
            for (int j = 0; j < i; ++j) ClearOccluderRecord(&set->cells[j]);
            return false;
        }
    }
    return true;
}
EI_REWRITTEN(0x005B65B0, CAIMap::ApplyFootprint);

bool CAIMap::StampFootprint(int owner, FootprintCellSet* set) {
    bool savedRefreshUnits = refreshUnits;
    refreshUnits = false;
    bool fits = ApplyFootprint(set, false);
    int r[4] = {10000, 0, 10000, 0};
    set->GetBounds(r);
    QueueRefresh(r[0] - 3, r[1] + 3, r[2] - 3, r[3] + 3);
    lastFootprintOwner = owner;
    refreshUnits = savedRefreshUnits;
    return fits;
}
EI_REWRITTEN(0x005B7820, CAIMap::StampFootprint);

bool CAIMap::ClearFootprint(FootprintCellSet* set) {
    bool savedRefreshUnits = refreshUnits;
    refreshUnits = false;
    bool cleared = ApplyFootprint(set, true);
    int r[4] = {10000, 0, 10000, 0};
    set->GetBounds(r);
    QueueRefresh(r[0] - 3, r[1] + 3, r[2] - 3, r[3] + 3);
    refreshUnits = savedRefreshUnits;
    return cleared;
}
EI_REWRITTEN(0x005B78F0, CAIMap::ClearFootprint);

void CAIMap::BeginRefresh() {
    if (batching) {
        RefreshArea(dirtyMinX, dirtyMaxX, dirtyMinY, dirtyMaxY);
        batching = false;
    }
    dirtyMaxX = 0;
    dirtyMinX = 10000;
    dirtyMinY = 10000;
    dirtyMaxY = 0;
    batching = true;
}
EI_REWRITTEN(0x005B8B30, CAIMap::BeginRefresh);

void CAIMap::EndRefresh() {
    RefreshArea(dirtyMinX, dirtyMaxX, dirtyMinY, dirtyMaxY);
    batching = false;
}
EI_REWRITTEN(0x005B8B80, CAIMap::EndRefresh);
