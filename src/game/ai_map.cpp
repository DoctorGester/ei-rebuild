#include "ai_map.h"

#include "map_object.h"
#include "sector_map.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cstring>

int WorldToCell(float world) {
    float cell = 2.0f * world - 0.5f;
    return x87::fistp(cell);
}

namespace {
constexpr float kSizeClassHeights[8] = /*0079169C*/ {0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.7f, 0.7f, 1.5f};

float CellToWorld(int cell) { return (static_cast<float>(cell) + 0.5f) * 0.5f; }

int PositiveOrZero(int v) { return v < 0 ? 0 : v; }

// The occluder records of the sector holding cell (x, y), for cells holding more than one (occluder::kChained).
const SectorOccluder* OccluderRecords(const AIMap* map, int x, int y) {
    const SectorMap& sectors = map->sectorMap;
    return sectors.sectors[(y / 32) * sectors.width + x / 32].occluders.data();
}

constexpr float kTransparency[4] = /*00791744*/ {0.0f, 0.98f, 0.99f, 1.0f};
constexpr float kMinVisibility = 0.0001f;

// The spiral of cell offsets ordered by distance (sector_map.h's tables, built on first use): a binary tree
// mapping a radius (compared as raw float bits) to the number of offsets within it.
const std::int16_t* SpiralOffsets() { return &SectorSpiralTables().offsets.data()->x; }


} // namespace

int LevelForSizeClass(int sizeClass) {
    if (sizeClass < 0 || sizeClass >= 8) return sizeClass & ~0xFF;   // XOR AL,AL
    return PositiveOrZero(x87::fistp((2.0f - kSizeClassHeights[sizeClass]) * 16.0f));
}

int StandingLevel(const CUnit* unit) {
    return PositiveOrZero(x87::fistp((2.0f - unit->standingHeight) * 16.0f));
}

int BridgeHeights::HeightAt(short x, short y) const {
    auto node = saved.find(static_cast<std::uint32_t>(x * 0x10000 + y));   // the key is (short)x << 16 | (short)y
    return node != saved.end() ? static_cast<std::uint16_t>(node->second) : 0;
}

// --- Cell queries --------------------------------------------------------------------------------

bool AIMap::CanStandAt(CUnit* unit, int x, int y, int* reason) {
    if (x < 0 || x >= cells.width || y < 0 || y >= cells.height) return false;
    if (unit->GetClassId() == ClassId::Unit) {
        int level = x87::fistp((2.0f - unit->standingHeight) * 16.0f);
        if (level < 0) level = 0;
        *reason = 1;
        if (cells[y][x].level > static_cast<std::uint8_t>(level)) return false;
        *reason = 2;
        if (waterDepth[y][x] > (level & 0xFF)) return false;
    }
    *reason = 0;
    return true;
}

bool AIMap::CanStandAtFromHere(CUnit* unit, int x, int y, int* reason) {
    if (x < 0 || x >= cells.width || y < 0 || y >= cells.height) return false;
    int level = PositiveOrZero(x87::fistp((2.0f - unit->standingHeight) * 16.0f));
    *reason = 1;
    // The unit's own cell is not bounds-checked, like the original.
    if (cells[y][x].level > static_cast<std::uint8_t>(level)) {
        int ux = WorldToCell(unit->localPosition.x), uy = WorldToCell(unit->localPosition.y);
        if (cells[y][x].level > cells[uy][ux].level) return false;
    }
    level &= 0xFF;
    *reason = 2;
    if (waterDepth[y][x] > level) {
        int ux = WorldToCell(unit->localPosition.x), uy = WorldToCell(unit->localPosition.y);
        if (waterDepth[y][x] > waterDepth[uy][ux]) return false;
    }
    *reason = 0;
    return true;
}

bool AIMap::IsPassableWithoutSelf(CUnit* unit, const Vector3& position) {
    if (unit->GetClassId() != ClassId::Unit) return true;
    sectorMap.Remove(unit);
    int y = WorldToCell(position.y);
    int x = WorldToCell(position.x);
    bool passable = true;
    if (unit->GetClassId() == ClassId::Unit) {
        int reason;
        passable = CanStandAt(unit, x, y, &reason) &&
                   cells[y][x].Passability(unit->sizeClass * 4) != 0;
    }
    sectorMap.Add(unit);
    return passable;
}

bool AIMap::IsUnitCellPassable(CUnit* unit) {
    int x = WorldToCell(unit->localPosition.x), y = WorldToCell(unit->localPosition.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    return cells[y][x].Passability(unit->sizeClass * 4) != 0;
}

bool AIMap::IsPassableForClass(std::uint8_t sizeClass, const Vector3& position) {
    int x = WorldToCell(position.x), y = WorldToCell(position.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    return cells[y][x].Passability(sizeClass * 4) != 0;
}

bool AIMap::CanLeaveCell(CUnit* unit) {
    static const int kOffsets[9][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    int x = WorldToCell(unit->localPosition.x), y = WorldToCell(unit->localPosition.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    int sizeClass = unit->sizeClass;
    const std::vector<int>& table = sizeClass == 0 ? climbSmall : climbLarge;
    const int* climbCost = table.data() + std::ssize(table) / 2;
    int here = cells[y][x].groundLevel;
    for (const auto& o : kOffsets) {
        int nx = o[0] + x, ny = y + o[1];
        if (nx < 0 || ny < 0 || nx >= cells.width || ny >= cells.height) continue;
        const AIMapCell& cell = cells[ny][nx];
        if (cell.Passability(sizeClass * 4) == 0) continue;
        if (climbCost[cell.groundLevel - here] > 0) return true;
    }
    return false;
}

// --- Heights -------------------------------------------------------------------------------------

float AIMap::GroundHeight(int x, int y) {
    return static_cast<float>(static_cast<int>(cells[y][x].groundLevel)) / heightScale;
}

float AIMap::TopHeightAt(const Vector3& position) {
    int x = WorldToCell(position.x), y = WorldToCell(position.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return 0.0f;
    const AIMapCell& cell = cells[y][x];
    return static_cast<float>(cell.WaterDepth() + cell.groundLevel) / heightScale;
}

float AIMap::SlopeAt(const Vector3& position) {
    int x = WorldToCell(position.x), y = WorldToCell(position.y);
    if (x < 1 || y < 1 || x > cells.width - 2 || y > cells.height - 2) return 0.0f;
    int here = cells[y][x].groundLevel;
    auto step = [here](int level) {
        int d = level - here;
        return d < 0 ? -d : d;
    };
    int slope = 0;
    int d = step(cells[y + 1][x].groundLevel);
    if (d >= 0) slope = d;
    d = step(cells[y - 1][x].groundLevel);
    if (!(slope > d)) slope = d;
    d = step(cells[y][x + 1].groundLevel);
    if (!(slope > d)) slope = d;
    d = step(cells[y][x - 1].groundLevel);
    if (!(slope > d)) slope = d;
    return static_cast<float>(slope) / heightScale;
}

bool AIMap::InWaterAt(const Vector3& position) {
    int x = WorldToCell(position.x), y = WorldToCell(position.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    return cells[y][x].InWater();
}

bool AIMap::HasSpellArea(int x, int y) {
    std::uint32_t word = occluders[y][x];
    if (word & occluder::kNone) return false;
    if (!(word & occluder::kChained)) return occluder::Kind(word) == OccluderKind::SpellArea;
    const SectorOccluder* records = OccluderRecords(this, x, y);
    for (int index = occluder::Head(word); index >= 0; index = records[index].next) {
        if (records[index].kind == OccluderKind::SpellArea) return true;
    }
    return false;
}

bool AIMap::HasSpellAreaAt(const Vector3& position) {
    int x = WorldToCell(position.x), y = WorldToCell(position.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    return HasSpellArea(x, y);
}

int AIMap::MaterialAt(const Vector3& position) {
    int x = WorldToCell(position.x), y = WorldToCell(position.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return -1;
    return static_cast<int>(occluder::Type(occluders[y][x]));
}

AITerrainType* AIMap::MaterialRecordAt(const Vector3& position) {
    int x = WorldToCell(position.x), y = WorldToCell(position.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return materials.data();
    return materials.data() + occluder::Type(occluders[y][x]);
}

void AIMap::TurnObject(CMapObject* object, const Quaternion* rotation) {
    SectorMap* map = &sectorMap;
    map->Remove(object);
    object->localRotation = *rotation;
    map->Add(object);
}

bool AIMap::BridgeDeckAt(int x, int y, int* deckHeight) {
    int deck = 0;
    bool found = false;
    std::uint32_t word = occluders[y][x];
    if (!(word & occluder::kNone)) {
        if (word & occluder::kChained) {
            const SectorOccluder* records = OccluderRecords(this, x, y);
            for (int index = occluder::Head(word); index >= 0; index = records[index].next) {
                if (records[index].kind == OccluderKind::BridgeDeck) {
                    if (!(deck > records[index].top)) deck = records[index].top;
                    found = true;
                }
            }
        } else if (occluder::Kind(word) == OccluderKind::BridgeDeck) {
            deck = occluder::Top(word);
            found = true;
        }
    }
    if (deckHeight) *deckHeight = deck;
    return found;
}

// --- Line of sight -------------------------------------------------------------------------------

float AIMap::LineOfSight(const Vector3& from, const Vector3& to) {
    float dx = to.x - from.x, dy = to.y - from.y;
    float dz = to.z - from.z;
    float length = static_cast<float>(std::sqrt(static_cast<double>(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz))) / 0.5f);   // half-cells
    EI_NAN_AUDIT(length);
    if (length == 0.0f) return 1.0f;
    float inverse = 1.0f / length;
    int steps = x87::fistp(length);

    // 16.16 fixed point position and step, in half-cells (x, y) and AI height units (z).
    int x = x87::fistp(from.x * 65536.0f / 0.5f);
    int y = x87::fistp(from.y * 65536.0f / 0.5f);
    int z = x87::fistp(from.z * heightScale) << 16;
    int stepX = x87::fistp(dx * inverse * 65536.0f / 0.5f);
    int stepY = x87::fistp(dy * inverse * 65536.0f / 0.5f);
    int toZ = x87::fistp(to.z * heightScale);
    int stepZ = x87::fistp(static_cast<float>(static_cast<double>((toZ - (z >> 16)) << 16) * inverse));

    auto outside = [this](int fx, int fy) {
        int cx = fx >> 16, cy = fy >> 16;
        return cx < 0 || cy < 0 || cx >= cells.width || cy >= cells.height;
    };
    if (outside(x, y) || outside(stepX * steps + x, stepY * steps + y)) return 0.0f;

    float visibility = 1.0f;
    x += stepX;
    y += stepY;
    z += stepZ;
    for (int i = 0; i < steps; ++i, x += stepX, y += stepY, z += stepZ) {
        int cx = x >> 16, cy = y >> 16, cz = z >> 16;
        if (cells[cy][cx].groundLevel >= cz) return 0.0f;

        std::uint32_t word = occluders[cy][cx];
        if (word & occluder::kNone) continue;
        if (!(word & occluder::kChained)) {
            // A single occluder packed into the word.
            int high = occluder::Top(word), low = occluder::Bottom(word);
            if (high < cz || low > cz) continue;
            visibility *= kTransparency[occluder::Transparency(word)];
            EI_NAN_AUDIT(visibility);
            if (visibility < kMinVisibility) return kMinVisibility;
            continue;
        }
        // Several occluders: a chain in the sector's record list.
        const SectorOccluder* records = OccluderRecords(this, cx, cy);
        for (int index = occluder::Head(word); index >= 0; index = records[index].next) {
            const SectorOccluder& o = records[index];
            if (o.top < cz || o.bottom > cz) continue;
            visibility *= kTransparency[o.transparency];
            EI_NAN_AUDIT(visibility);
            if (visibility < kMinVisibility) return kMinVisibility;
        }
    }
    return visibility;
}

// --- Free cell search ----------------------------------------------------------------------------

const std::int16_t* SpiralCache::Get(float r, int* count) {
    EI_NAN_AUDIT(r, radius);
    if (r == radius) {
        *count = length;
        return SpiralOffsets();
    }
    const SectorSpiral& spiral = SectorSpiralTables();
    int found = spiral.lengths[spiral.radii.Find(r)];
    *count = found;
    length = found;
    radius = r;
    return SpiralOffsets();
}

bool AIMap::FindFreeCellNear(CUnit* unit, const Vector3& from, Vector3* target) {
    static SpiralCache cache{0, -1.0f};

    int sizeClass = unit->sizeClass;
    int x = WorldToCell(from.x), y = WorldToCell(from.y);
    if (x < 1 || y < 1 || x >= cells.width - 1 || y >= cells.height - 1) return false;
    short region = static_cast<short>(regionMap.DominantRegion(sizeClass, x, y));

    int tx = WorldToCell(target->x), ty = WorldToCell(target->y);
    int count;
    float radius;
    std::uint32_t radiusBits = 0x60AD78EC;   // about 1e20: the whole spiral
    radius = std::bit_cast<float>(radiusBits);
    const std::int16_t* offset = cache.Get(radius, &count);
    for (int i = 0; i < count; ++i, offset += 2) {
        int cx = offset[0] + tx, cy = offset[1] + ty;
        int reason;
        if (!CanStandAt(unit, cx, cy, &reason)) continue;
        if (cells[cy][cx].Passability(sizeClass * 4) == 0) continue;
        short r = static_cast<short>(regionMap.DominantRegion(sizeClass, cx, cy));
        if (region != 0 && r != region) continue;
        if (i != 0) {
            target->x = CellToWorld(cx);
            target->y = CellToWorld(cy);
        }
        return true;
    }
    return false;
}
