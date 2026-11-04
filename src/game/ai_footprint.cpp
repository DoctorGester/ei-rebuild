// Object footprints in the AI map: collecting the cells an object's model parts cover, stamping
// them into the occluder grid, and refreshing passability around them.
#include "ai_map.h"
#include "ai_unit_layer.h"

#include "lib/containers.h"
#include "lib/rounding.h"
#include "lib/crt.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include "lib/vecmath.h"

#include <cstdlib>
#include <vector>

// --- Footprint cell sets -------------------------------------------------------------------------

// The spells' footprint set, empty at the start.
namespace spellfx {
FootprintCellSet g_spellFootprint;
}

namespace {
FootprintCell* FindCell(FootprintCellSet* set, int x, int y) {
    for (int i = set->buckets[(x + y) & 15]; i >= 0; i = set->cells[i].next) {
        if (set->cells[i].x == x && set->cells[i].y == y) return &set->cells[i];
    }
    return nullptr;
}

void AppendCell(FootprintCellSet* set, const FootprintCell& cell) {
    int bucket = (cell.x + cell.y) & 15;
    int index = lib::Count(set->cells);
    set->SetSize(index + 1);
    set->cells[index] = cell;
    set->cells[std::ssize(set->cells) - 1].next = set->buckets[bucket];
    set->buckets[bucket] = lib::Count(set->cells) - 1;
}

const char* PartNotFoundFormat() { return "Bodypart \"%s\" for object \"%s\" not found\n"; }

void ResetCellSet(FootprintCellSet* set) {
    set->SetSize(0);
    for (int& b : set->buckets) b = -1;
}

} // namespace

void FootprintCellSet::SetSize(int size) { cells.resize(static_cast<std::size_t>(size)); }

void FootprintCellSet::AddTop(int x, int y, int height, OccluderKind kind, std::int8_t transparency) {
    if (FootprintCell* cell = FindCell(this, x, y)) {
        if (!(cell->high > height)) cell->high = height;
        if (kind < cell->kind) cell->kind = kind;
        if (transparency < cell->transparency) cell->transparency = transparency;
        return;
    }
    FootprintCell cell{x, y, 0x7FFFFFFF, height, kind, transparency, 0, 0, 0};
    AppendCell(this, cell);
}

void FootprintCellSet::AddBottom(int x, int y, int height, OccluderKind kind, std::int8_t transparency) {
    if (FootprintCell* cell = FindCell(this, x, y)) {
        if (!(cell->low < height)) cell->low = height;
        return;
    }
    FootprintCell cell{x, y, height, 0, kind, transparency, 0, 0, 0};
    AppendCell(this, cell);
}

std::array<int, 4> FootprintCellSet::GetBounds() const {
    std::array<int, 4> bounds;
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
    return bounds;
}

// --- Footprints ----------------------------------------------------------------------------------

bool AIMap::CollectFootprint(MapObject* object, FootprintCellSet* footprintSet, FootprintCellSet* bridgeSet) {
    // The object's transform: rotation from its orientation, translation from its position.
    float* matrix = object->worldMatrix.m;
    object->localRotation.ToMatrix((float*)matrix);
    matrix[11] = object->localPosition.z;
    matrix[3] = object->localPosition.x;
    matrix[7] = object->localPosition.y;
    matrix[14] = 0.0f;
    matrix[13] = 0.0f;
    matrix[12] = 0.0f;
    matrix[15] = 1.0f;

    // Part names: the server companion's string array if it has any, else the object's own (the AI map lives on
    // the server, so the companion is a MapObjectServer).
    auto* server = object->ServerCompanion();
    const std::vector<std::string>* names = &server->strings;
    if (names->empty()) names = &object->partNames;

    bool hasBridge = false;
    for (const std::string& name : *names) {
        int index = object->FindPartIndex(name);
        if (index < 0) {
            game::FatalError(PartNotFoundFormat(), name, object->name);
            continue;
        }
        const FootprintPart* part = &object->partBoxes[index];
        std::uint32_t flags = part->bounds.flags;
        if (lib::Has(flags, BoundsFlag::NoFootprint)) continue;
        OccluderKind kind = BoundsOccluderKind(flags);
        std::int8_t transparency = BoundsTransparency(flags);
        FootprintCellSet* set = footprintSet;
        if (lib::Has(flags, BoundsFlag::Base)) {
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
        position = object->localPosition;

        // Stand the object on the ground (or bridge deck) under its centre.
        const Point under = WorldToCell(object->localPosition);
        float ground;
        if (!InBounds(under)) {
            ground = 0.0f;
        } else if (cells[under].flags & kCellBridge) {
            ground = static_cast<float>(bridgeHeights.HeightAt(under)) / heightScale;
        } else {
            ground = GroundHeight(under);
        }
        position.z = ground + position.z;

        for (Vector3& p : corner) {
            const FootprintPart* placed = &object->partBoxes[index];
            p = TransformColumnNormal(object->worldMatrix, p + placed->pivot + placed->bounds.center) + position;
        }
        for (Vector3& v : normal) v = TransformColumnNormal(object->worldMatrix, v);

        // Sample each face on a grid fine enough to hit every cell it covers.
        for (int k = 0; k < 6; ++k) {
            const Vector3 &p0 = corner[k], &p1 = corner[k + 1], &p2 = corner[k + 2];
            const Vector3 a = p0 - p1;
            const Vector3 b = p2 - p1;
            float la = std::sqrt(LengthSquared2D(a.y, a.x));
            int stepsA = lib::RoundEven((la + la) * 1.5f) + 2;
            float lb = std::sqrt(LengthSquared2D(b.y, b.x));
            int stepsB = lib::RoundEven((lb + lb) * 1.5f) + 2;
            const Vector3 da = a / static_cast<float>(stepsA);
            const Vector3 db = b / static_cast<float>(stepsB);

            for (int i = 0; i < stepsA; ++i) {
                const Vector3 ia = da * (static_cast<float>(i) + 0.5f);
                for (int j = 0; j < stepsB; ++j) {
                    const Vector3 s = ia + p1 + db * (static_cast<float>(j) + 0.5f);
                    int x = WorldToCell(s.x), y = WorldToCell(s.y);
                    if (!(x < cells.width && y < cells.height && x >= 0 && y >= 0)) continue;
                    int z = lib::RoundEven(s.z * heightScale);
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

void AIMap::QueueRefresh(Point first, Point last) {
    if (!batching) {
        RefreshArea(first, last);
        return;
    }
    dirty.first = Min(dirty.first, first);
    dirty.last = Max(dirty.last, last);
}

bool AIMap::MarkFootprint(MapObject* object, bool remove) {
    const Point cell = WorldToCell(object->localPosition);
    ResetCellSet(&footprint);
    ResetCellSet(&bridges);

    if (Unit* unit = UnitOrNull(object)) {
        if (unit->IsDead()) return true;
        const UnitShape* shape = ShapeForHeight(unit->standingHeight);
        if (!shape) return true;
        UnitLayer* layer = &unitLayerSmall;
        if (unit->IsLargeUnit()) layer = &unitLayerLarge;
        const Point topLeft = cell - Point{8, 8};
        if (remove) layer->Remove(unit, topLeft);
        else layer->Add(unit, shape, topLeft);
        return true;
    }

    if (CollectFootprint(object, &footprint, &bridges)) ApplyBridgeDecks(&bridges, remove);
    bool fits = ApplyFootprint(&footprint, remove);

    auto a = footprint.GetBounds();
    auto b = bridges.GetBounds();
    int minX = a[0] < b[0] ? a[0] : b[0];
    int maxX = a[1] > b[1] ? a[1] : b[1];
    int minY = a[2] < b[2] ? a[2] : b[2];
    int maxY = a[3] > b[3] ? a[3] : b[3];

    std::uint8_t savedRefreshUnits = refreshUnits;
    refreshUnits = savedRefreshUnits && object->KeepsMapFlag();
    QueueRefresh({minX - 3, minY - 3}, {maxX + 3, maxY + 3});
    refreshUnits = savedRefreshUnits;
    return fits;
}

bool AIMap::ApplyFootprint(FootprintCellSet* set, bool remove) {
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

bool AIMap::StampFootprint(int tick, FootprintCellSet* set) {
    std::uint8_t savedRefreshUnits = refreshUnits;
    refreshUnits = false;
    bool fits = ApplyFootprint(set, false);
    auto r = set->GetBounds();
    QueueRefresh({r[0] - 3, r[2] - 3}, {r[1] + 3, r[3] + 3});
    footprintTick = tick;
    refreshUnits = savedRefreshUnits;
    return fits;
}

bool AIMap::ClearFootprint(FootprintCellSet* set) {
    std::uint8_t savedRefreshUnits = refreshUnits;
    refreshUnits = false;
    bool cleared = ApplyFootprint(set, true);
    auto r = set->GetBounds();
    QueueRefresh({r[0] - 3, r[2] - 3}, {r[1] + 3, r[3] + 3});
    refreshUnits = savedRefreshUnits;
    return cleared;
}

void AIMap::BeginRefresh() {
    if (batching) {
        RefreshArea(dirty.first, dirty.last);
        batching = false;
    }
    dirty.Reset();
    batching = true;
}

void AIMap::EndRefresh() {
    RefreshArea(dirty.first, dirty.last);
    batching = false;
}
