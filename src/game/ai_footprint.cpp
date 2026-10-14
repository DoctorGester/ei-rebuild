// Object footprints in the AI map: collecting the cells an object's model parts cover, stamping
// them into the occluder grid, and refreshing passability around them.
#include "ai_map.h"
#include "ai_unit_layer.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_ai.h"
#include "originals_render.h"
#include "originals_units.h"

#include <cstring>

// --- Footprint cell sets -------------------------------------------------------------------------

FootprintCellSet* FootprintCellSet::Construct() {
    cells = nullptr;
    growBy = 0;
    capacity = 0;
    count = 0;
    hashed = true;
    return this;
}
EI_REPLACE_METHOD(0x0067E4E0, FootprintCellSet::Construct);

void FootprintCellSet::SetSize(int size, int grow) {
    if (grow != -1) growBy = grow;
    if (size == 0) {
        if (cells) {
            gamecrt::operator_delete(reinterpret_cast<void**>(cells)[-1]);
            cells = nullptr;
        }
        capacity = 0;
        count = 0;
        return;
    }
    // 32-byte aligned storage; the raw block is kept just before it.
    auto allocate = [](int n) {
        auto raw = reinterpret_cast<std::uintptr_t>(gamecrt::operator_new(n * sizeof(FootprintCell) + 0x24));
        auto* aligned = reinterpret_cast<FootprintCell*>((raw + 0x23) & ~std::uintptr_t{0x1F});
        reinterpret_cast<std::uintptr_t*>(aligned)[-1] = raw;
        return aligned;
    };
    if (!cells) {
        cells = allocate(size);
        capacity = size;
        count = size;
        return;
    }
    if (size <= capacity) {
        count = size;
        return;
    }
    int step = growBy;
    if (step == 0) {
        step = count / 8;
        if (step < 4) step = 4;
        else if (step > 0x400) step = 0x400;
    }
    int newCapacity = capacity + step;
    if (!(size < newCapacity)) newCapacity = size;
    FootprintCell* grown = allocate(newCapacity);
    std::memcpy(grown, cells, count * sizeof(FootprintCell));
    gamecrt::operator_delete(reinterpret_cast<void**>(cells)[-1]);
    cells = grown;
    count = size;
    capacity = newCapacity;
}
EI_REPLACE_METHOD(0x005BD350, FootprintCellSet::SetSize);

namespace {
FootprintCell* FindCell(FootprintCellSet* set, int x, int y) {
    for (int i = set->buckets[(x + y) & 15]; i >= 0; i = set->cells[i].next) {
        if (set->cells[i].x == x && set->cells[i].y == y) return &set->cells[i];
    }
    return nullptr;
}

void AppendCell(FootprintCellSet* set, const FootprintCell& cell) {
    int bucket = (cell.x + cell.y) & 15;
    int index = set->count;
    set->SetSize(index + 1, -1);
    set->cells[index] = cell;
    set->cells[set->count - 1].next = set->buckets[bucket];
    set->buckets[bucket] = set->count - 1;
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
EI_REPLACE_METHOD(0x005B2D10, FootprintCellSet::AddTop);

void FootprintCellSet::AddBottom(int x, int y, int height, std::int8_t kind, std::int8_t transparency) {
    hashed = true;
    if (FootprintCell* cell = FindCell(this, x, y)) {
        if (!(cell->low < height)) cell->low = height;
        return;
    }
    FootprintCell cell{x, y, height, 0, kind, transparency, 0, 0, 0};
    AppendCell(this, cell);
}
EI_REPLACE_METHOD(0x005B2E10, FootprintCellSet::AddBottom);

void FootprintCellSet::GetBounds(int* bounds) const {
    bounds[0] = 100000;
    bounds[2] = 100000;
    bounds[3] = 0;
    bounds[1] = 0;
    for (int i = 0; i < count; ++i) {
        int x = cells[i].x, y = cells[i].y;
        if (!(bounds[0] < x)) bounds[0] = x;
        if (!(bounds[2] < y)) bounds[2] = y;
        if (!(bounds[1] > x)) bounds[1] = x;
        if (!(bounds[3] > y)) bounds[3] = y;
    }
}
EI_REPLACE_METHOD(0x005B2F40, FootprintCellSet::GetBounds);

// --- Footprints ----------------------------------------------------------------------------------

__declspec(naked) bool CAIMap::ApplyBridges(FootprintCellSet*, bool) EI_ORIGINAL(0x005B6740)
__declspec(naked) bool CAIMap::AddOccluder(FootprintCell*) EI_ORIGINAL(0x005B61B0)
__declspec(naked) bool CAIMap::ClearOccluder(FootprintCell*) EI_ORIGINAL(0x005B6380)
__declspec(naked) void CAIMap::Refresh(int, int, int, int) EI_ORIGINAL(0x005B8FA0)

namespace {
// Object model helpers not rewritten yet.
void __fastcall QuaternionToMatrix(const void* a0, void*, void* a2) { QuaternionToMatrixOriginal(a0, nullptr, (float*)a2); }
__declspec(naked) Vector3* __fastcall TransformVector(const void*, void*, Vector3*, const Vector3*)
    EI_ORIGINAL(0x00560E10)
// CStringArray element by value (a CString constructed into `out`).
__declspec(naked) char** __fastcall StringArrayGetAt(const void*, void*, char**, int)
    EI_ORIGINAL(0x004698A0)

void DestroyString(char** s) { reinterpret_cast<mfc::CString*>(s)->~CString(); }

// The map objects and units the footprint code reads share the CObject3D layout.
CMapObject* MapObject(CEngineObject* object) { return static_cast<CMapObject*>(object); }

// "Bodypart \"%s\" for object \"%s\" not found\n"
const char* PartNotFoundFormat() { return reinterpret_cast<const char*>(0x0079189C); }

// Unit and object virtuals used by MarkFootprint (names provisional).
int IsDead(CEngineObject* unit) { return ei::VCall<int>(unit, 0xDC / 4); }
int IsLarge(CEngineObject* unit) { return ei::VCall<int>(unit, 0xD8 / 4); }
bool AffectsUnits(CEngineObject* object) { return ei::VCall<bool>(object, 0xAC / 4); }

void ResetCellSet(FootprintCellSet* set) {
    set->SetSize(0, -1);
    if (set->hashed) {
        for (int& b : set->buckets) b = -1;
    }
}
} // namespace

bool CAIMap::CollectFootprint(CEngineObject* object, FootprintCellSet* footprintSet, FootprintCellSet* bridgeSet) {
    // The object's transform: rotation from its orientation, translation from its position.
    CMapObject* mapObject = MapObject(object);
    float* matrix = mapObject->worldMatrix.m;
    QuaternionToMatrix(&mapObject->orientation, nullptr, matrix);
    std::memcpy(&matrix[11], &mapObject->position.z, 4);
    std::memcpy(&matrix[3], &mapObject->position.x, 4);
    std::memcpy(&matrix[7], &mapObject->position.y, 4);
    matrix[14] = 0.0f;
    matrix[13] = 0.0f;
    matrix[12] = 0.0f;
    matrix[15] = 1.0f;

    // Part names: the figure's list if it has one, else the object's own (CStringArray, size at +8).
    char* names = reinterpret_cast<char*>(mapObject->companion) + 0x80;
    if (*reinterpret_cast<int*>(names + 8) == 0) names = reinterpret_cast<char*>(&mapObject->partNameArrayVtable);

    bool hasBridge = false;
    for (int n = 0; n < *reinterpret_cast<int*>(names + 8); ++n) {
        char* name;
        int index = ObjectFindPartIndexOriginal(object, nullptr, *StringArrayGetAt(names, nullptr, &name, n));
        DestroyString(&name);
        if (index < 0) {
            const char* objectName = mapObject->name;
            game::FatalError(PartNotFoundFormat(), *StringArrayGetAt(names, nullptr, &name, n), objectName);
            DestroyString(&name);
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
            ground = static_cast<float>(BridgeHeight(bridgeHeights, nullptr, static_cast<short>(cx),
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
            double la = x87::fsqrt(ay * ay + ax * ax);
            int stepsA = x87::fistp(static_cast<float>((la + la) * 1.5f)) + 2;
            double lb = x87::fsqrt(by * by + bx * bx);
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
                    if (!(normal[k].z > 0.0f)) set->AddBottom(x, y, z, kind, transparency);
                    else set->AddTop(x, y, z, kind, transparency);
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
        Refresh(minX, maxX, minY, maxY);
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
    int cellX = WorldToCell(MapObject(object)->position.x);
    int cellY = WorldToCell(MapObject(object)->position.y);
    ResetCellSet(&footprint);
    ResetCellSet(&bridges);

    if (object->GetClassId() == kClassUnit) {
        auto* unit = static_cast<CUnit*>(object);
        if (IsDead(unit)) return true;
        void* shape = UnitShape(unit->standingHeight);
        if (!shape) return true;
        CUnitLayer* layer = IsLarge(unit) ? &unitLayerLarge : &unitLayerSmall;
        if (remove) UnitLayerRemoveOriginal(layer, nullptr, unit, cellX - 8, cellY - 8);
        else UnitLayerAddOriginal(layer, nullptr, unit, shape, cellX - 8, cellY - 8);
        return true;
    }

    if (CollectFootprint(object, &footprint, &bridges)) ApplyBridges(&bridges, remove);
    bool fits = ApplyFootprint(&footprint, remove);

    int a[4] = {10000, 0, 10000, 0}, b[4] = {10000, 0, 10000, 0};
    footprint.GetBounds(a);
    bridges.GetBounds(b);
    int minX = a[0] < b[0] ? a[0] : b[0];
    int maxX = a[1] > b[1] ? a[1] : b[1];
    int minY = a[2] < b[2] ? a[2] : b[2];
    int maxY = a[3] > b[3] ? a[3] : b[3];

    bool savedRefreshUnits = refreshUnits;
    refreshUnits = savedRefreshUnits && AffectsUnits(object);
    QueueRefresh(minX - 3, maxX + 3, minY - 3, maxY + 3);
    refreshUnits = savedRefreshUnits;
    return fits;
}
EI_REPLACE_METHOD(0x005B6A80, CAIMap::MarkFootprint);

bool CAIMap::ApplyFootprint(FootprintCellSet* set, bool remove) {
    if (remove) {
        for (int i = 0; i < set->count; ++i) ClearOccluder(&set->cells[i]);
        return true;
    }
    for (int i = 0; i < set->count; ++i) {
        if (!AddOccluder(&set->cells[i])) {
            for (int j = 0; j < i; ++j) ClearOccluder(&set->cells[j]);
            return false;
        }
    }
    return true;
}
EI_REPLACE_METHOD(0x005B65B0, CAIMap::ApplyFootprint);

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
EI_REPLACE_METHOD(0x005B7820, CAIMap::StampFootprint);

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
EI_REPLACE_METHOD(0x005B78F0, CAIMap::ClearFootprint);

void CAIMap::BeginRefresh() {
    if (batching) {
        Refresh(dirtyMinX, dirtyMaxX, dirtyMinY, dirtyMaxY);
        batching = false;
    }
    dirtyMaxX = 0;
    dirtyMinX = 10000;
    dirtyMinY = 10000;
    dirtyMaxY = 0;
    batching = true;
}
EI_REPLACE_METHOD(0x005B8B30, CAIMap::BeginRefresh);

void CAIMap::EndRefresh() {
    Refresh(dirtyMinX, dirtyMaxX, dirtyMinY, dirtyMaxY);
    batching = false;
}
EI_REPLACE_METHOD(0x005B8B80, CAIMap::EndRefresh);
