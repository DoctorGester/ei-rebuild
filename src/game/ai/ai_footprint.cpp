// Object footprints in the AI map: collecting the cells an object's model parts cover, stamping
// them into the occluder grid (the per-cell occluder words, and the bridge decks), and refreshing passability
// around them.

#include "game/ai/ai_map.h"
#include "game/ai/sector_map.h"
#include "game/ai/unit_shape.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/rounding.h"
#include "lib/vecmath.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
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

void ResetCellSet(FootprintCellSet* set) {
    set->SetSize(0);
    for (int& b : set->buckets) b = -1;
}

// The sector holding a cell (the occluder records' owner).
Sector* SectorOf(AIMap* map, int x, int y) {
    return &map->sectorMap.sectors[(y / 32) * map->sectorMap.width + x / 32];
}

// (short)x << 16 | (short)y.
std::uint32_t BridgeKey(Point at) {
    return static_cast<std::uint32_t>((static_cast<int>(static_cast<short>(at.x)) << 16) + static_cast<short>(at.y));
}

// A bridge map entry's value: the ground level in the low word, the water depth in bits 16-21, the terrain type
// in bits 24-28.
std::uint32_t SavedCell(const AIMapCell& cell, std::uint32_t occluderWord) {
    return cell.groundLevel | static_cast<std::uint32_t>(cell.WaterDepth()) << 16 | occluder::Type(occluderWord) << 24;
}
int SavedDepth(std::uint32_t saved) { return static_cast<int>((saved >> 16) & kCellDepthMask); }
std::uint32_t SavedType(std::uint32_t saved) { return occluder::Type(saved >> 24); }

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
        if (index < 0) {   // a part the object's model lacks: it marks nothing
            game::Warning("Bodypart \"%s\" for object \"%s\" not found", name, object->name);
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

// --- Occluders ---------------------------------------------------------------------------------

// The per-cell occluder words (AIMap::occluders, ai_map.h's namespace occluder): adding and removing one footprint
// cell's occluder. A cell with more than one chains 8-byte records (SectorOccluder, sector_map.h) of its 32x32
// sector. See docs/notes/ai.md.
bool AIMap::AddOccluderRecord(FootprintCell* cell) {
    int x = cell->x, y = cell->y;
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    if (cell->low >= cell->high) return true;
    std::uint32_t& word = occluders[y][x];
    if (word & occluder::kNone) {
        std::uint32_t w = word & ~occluder::kNone;
        w = occluder::WithTop(w, cell->high);
        w = occluder::WithBottom(w, cell->low);
        w = occluder::WithKind(w, cell->kind);
        w = occluder::WithTransparency(w, cell->transparency);
        word = w;
        return true;
    }
    Sector* sector = SectorOf(this, x, y);
    if (!(word & occluder::kChained)) {
        int index = sector->NewOccluder();
        std::uint32_t w = word;
        SectorOccluder& first = sector->occluders[index];
        first.top = static_cast<std::int16_t>(occluder::Top(w));
        first.bottom = static_cast<std::int16_t>(occluder::Bottom(w));
        first.kind = occluder::Kind(w);
        first.transparency = static_cast<std::int8_t>(occluder::Transparency(w));
        first.next = -1;
        word = occluder::WithHead(w, index) | occluder::kChained;
    }
    int index = sector->NewOccluder();
    SectorOccluder& record = sector->occluders[index];
    record.top = static_cast<std::int16_t>(cell->high);
    record.bottom = static_cast<std::int16_t>(cell->low);
    record.kind = cell->kind;
    record.transparency = static_cast<std::int8_t>(cell->transparency);
    record.next = static_cast<std::int16_t>(word >> 9);   // the head index (bits 9-28) as a short
    word = occluder::WithHead(word, index);
    return true;
}

bool AIMap::ClearOccluderRecord(FootprintCell* cell) {
    int x = cell->x, y = cell->y;
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    if (cell->low >= cell->high) return true;
    std::uint32_t& word = occluders[y][x];
    if (word & occluder::kNone) return false;
    if (!(word & occluder::kChained)) {
        word |= occluder::kNone;
        return true;
    }
    Sector* sector = SectorOf(this, x, y);
    int head = occluder::Head(word);
    int index = sector->RemoveOccluder(head, cell->low, cell->high, cell->kind, cell->transparency);
    word = occluder::WithHead(word, index);
    index = occluder::Head(word);
    SectorOccluder& last = sector->occluders[index];
    if (last.next != -1) return true;
    // One record left: back inline (kChained cleared), the record freed.
    std::uint32_t w = word;
    w = occluder::WithBottom(w, last.bottom);
    w = occluder::WithTop(w, last.top);
    w = occluder::WithKind(w, last.kind);
    w = occluder::WithTransparency(w & ~occluder::kChained, last.transparency);
    word = w;
    sector->FreeOccluder(index);
    return true;
}

// --- Bridge decks ------------------------------------------------------------------------------

// AIMap::ApplyBridgeDecks: applies or clears the bridge decks of a footprint's bridge cells. Applying: each
// cell inside the map with low < high becomes a BridgeDeck; a grid cell not yet under a bridge has its ground
// level, its water depth and its terrain type saved in the bridge map (keyed (short)x << 16 | (short)y) unless
// already there; then the occluder is added (a failure ends the call with false), the cell's flags become
// kCellBridge alone (no water), its ground is raised to the deck (`high`, u16 maximum) and its terrain type
// becomes occluder::kBridgeType. Clearing: each such cell becomes a BridgeDeck and its occluder is removed;
// when that worked and no bridge deck is left on the grid cell, a cell that was under a bridge is restored from
// the bridge map, kCellBridge cleared and kCellWater set when the cell has a water depth; with a deck left, the
// ground becomes max(saved ground, highest deck). See ai_map.h and docs/notes/ai.md.
void BridgeHeights::SaveCell(Point at, const AIMapCell& cell, std::uint32_t occluder) {
    saved.try_emplace(BridgeKey(at), SavedCell(cell, occluder));
}

void BridgeHeights::RestoreCell(Point at, AIMapCell* cell, std::uint32_t* occluder) {
    auto node = saved.find(BridgeKey(at));
    if (node == saved.end()) return;
    std::uint32_t entry = node->second;
    cell->groundLevel = static_cast<std::uint16_t>(entry);
    cell->flags = static_cast<std::uint8_t>((cell->flags & ~kCellDepthMask) | SavedDepth(entry));
    *occluder = occluder::WithType(*occluder, SavedType(entry));
    saved.erase(node);
}

int BridgeHeights::HeightAt(Point at) const {
    auto node = saved.find(BridgeKey(at));
    return node != saved.end() ? static_cast<std::uint16_t>(node->second) : 0;
}

bool AIMap::ApplyBridgeDecks(FootprintCellSet* set, bool remove) {
    if (remove) {
        for (int i = 0; i < std::ssize(set->cells); ++i) {
            FootprintCell& c = set->cells[i];
            const Point at{c.x, c.y};
            if (!cells.Contains(at) || c.low >= c.high) continue;
            c.kind = OccluderKind::BridgeDeck;
            if (!ClearOccluderRecord(&c)) continue;
            AIMapCell* cell = &cells[at];
            int deck;
            if (!BridgeDeckAt(at, &deck)) {
                if (cell->flags & kCellBridge) bridgeHeights.RestoreCell(at, cell, &occluders[at]);
                const int depth = cell->WaterDepth();
                cell->flags = static_cast<std::uint8_t>(depth | (depth != 0 ? kCellWater : 0));
            } else {
                int saved = bridgeHeights.HeightAt(at);
                cell->groundLevel = static_cast<std::uint16_t>(saved > deck ? saved : deck);
            }
        }
        return true;
    }
    for (int i = 0; i < std::ssize(set->cells); ++i) {
        FootprintCell& c = set->cells[i];
        const Point at{c.x, c.y};
        if (!cells.Contains(at) || c.low >= c.high) continue;
        c.kind = OccluderKind::BridgeDeck;
        AIMapCell* cell = &cells[at];
        if (!(cell->flags & kCellBridge)) bridgeHeights.SaveCell(at, *cell, occluders[at]);
        if (!AddOccluderRecord(&c)) return false;
        cell->flags = kCellBridge;
        std::uint16_t deck = static_cast<std::uint16_t>(c.high);
        if (cell->groundLevel <= deck) cell->groundLevel = deck;
        std::uint32_t& occluder = occluders[at];
        occluder = occluder::WithType(occluder, occluder::kBridgeType);
    }
    return true;
}
