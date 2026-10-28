#include "ai_map.h"

#include "map_object.h"
#include "sector_map.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/stlp.h"

#include <cstring>
#include "game_data.h"

int WorldToCell(float world) {
    float cell = 1.0f / (1.0f * 0.5f) * world - 0.5f;
    return x87::fistp(cell);
}

namespace {
constexpr float kSizeClassHeights[8] = /*0079169C*/ {0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.7f, 0.7f, 1.5f};

float CellToWorld(int cell) { return (static_cast<float>(cell) + 0.5f) * (1.0f * 0.5f); }

int PositiveOrZero(int v) { return v < 0 ? 0 : v; }

// The entry of the cell (x, y): the key is (short)x << 16 | (short)y.
const BridgeMap::Node* FindBridgeHeight(const BridgeMap* map, short x, short y) {
    return map->Find(static_cast<std::uint32_t>(x * 0x10000 + y));
}

} // namespace

int __stdcall LevelForSizeClass(int sizeClass) {
    if (sizeClass < 0 || sizeClass >= 8) return sizeClass & ~0xFF;   // XOR AL,AL
    return PositiveOrZero(x87::fistp((2.0f - kSizeClassHeights[sizeClass]) * 16.0f));
}

int __stdcall StandingLevel(const CUnit* unit) {
    return PositiveOrZero(x87::fistp((2.0f - unit->standingHeight) * 16.0f));
}

int BridgeHeights::HeightAt(short x, short y) const {
    const auto* node = FindBridgeHeight(this, x, y);
    return node ? static_cast<std::uint16_t>(node->value) : 0;
}

// --- Cell queries --------------------------------------------------------------------------------

bool CAIMap::CanStandAt(CUnit* unit, int x, int y, int* reason) {
    if (x < 0 || x >= cells.width || y < 0 || y >= cells.height) return false;
    if (unit->GetClassId() == kClassUnit) {
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

bool CAIMap::CanStandAtFromHere(CUnit* unit, int x, int y, int* reason) {
    if (x < 0 || x >= cells.width || y < 0 || y >= cells.height) return false;
    int level = PositiveOrZero(x87::fistp((2.0f - unit->standingHeight) * 16.0f));
    *reason = 1;
    // The unit's own cell is not bounds-checked, like the original.
    if (cells[y][x].level > static_cast<std::uint8_t>(level)) {
        int ux = WorldToCell(unit->localPosition_.x), uy = WorldToCell(unit->localPosition_.y);
        if (cells[y][x].level > cells[uy][ux].level) return false;
    }
    level &= 0xFF;
    *reason = 2;
    if (waterDepth[y][x] > level) {
        int ux = WorldToCell(unit->localPosition_.x), uy = WorldToCell(unit->localPosition_.y);
        if (waterDepth[y][x] > waterDepth[uy][ux]) return false;
    }
    *reason = 0;
    return true;
}

bool CAIMap::IsPassableWithoutSelf(CUnit* unit, const Vector3* position) {
    if (unit->GetClassId() != kClassUnit) return true;
    sectorMap.Remove(unit);
    int y = WorldToCell(position->y);
    int x = WorldToCell(position->x);
    bool passable = true;
    if (unit->GetClassId() == kClassUnit) {
        int reason;
        passable = CanStandAt(unit, x, y, &reason) &&
                   ((cells[y][x].passable >> ((unit->sizeClass * 4) & 31)) & 0xF) != 0;
    }
    sectorMap.Add(unit);
    return passable;
}

bool CAIMap::IsUnitCellPassable(CUnit* unit) {
    int x = WorldToCell(unit->localPosition_.x), y = WorldToCell(unit->localPosition_.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    return ((cells[y][x].passable >> ((unit->sizeClass * 4) & 31)) & 0xF) != 0;
}

bool CAIMap::IsPassableForClass(std::uint8_t sizeClass, const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    return ((cells[y][x].passable >> ((sizeClass & 7) * 4)) & 0xF) != 0;
}

bool CAIMap::CanLeaveCell(CUnit* unit) {
    static const int kOffsets[9][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    int x = WorldToCell(unit->localPosition_.x), y = WorldToCell(unit->localPosition_.y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    int sizeClass = unit->sizeClass;
    const ClimbTable& table = sizeClass == 0 ? climbSmall : climbLarge;
    const int* climbCost = table.data + table.size / 2;
    int here = cells[y][x].groundLevel;
    for (const auto& o : kOffsets) {
        int nx = o[0] + x, ny = y + o[1];
        if (nx < 0 || ny < 0 || nx >= cells.width || ny >= cells.height) continue;
        const AIMapCell& cell = cells[ny][nx];
        if (!((cell.passable >> ((sizeClass * 4) & 31)) & 0xF)) continue;
        if (climbCost[cell.groundLevel - here] > 0) return true;
    }
    return false;
}

// --- Heights -------------------------------------------------------------------------------------

float CAIMap::GroundHeight(int x, int y) {
    return static_cast<float>(static_cast<int>(cells[y][x].groundLevel)) / heightScale;
}

float CAIMap::TopHeightAt(const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return 0.0f;
    const AIMapCell& cell = cells[y][x];
    return static_cast<float>((cell.flags & 0x3F) + cell.groundLevel) / heightScale;
}

float CAIMap::SlopeAt(const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
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

bool CAIMap::CellFlag80At(const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    return (cells[y][x].flags >> 7) & 1;
}

namespace {
constexpr std::uint32_t kOccluderNone = 0x100;
constexpr std::uint32_t kOccluderChain = 0x80;

// The occluder records of the sector holding cell (x, y), for cells holding more than one: `top` is bits
// 12-21 of a single-occluder word, `bottom` bits 22-31, `kind` bits 9-11, `transparency` bits 5-6.
const SectorOccluder* OccluderRecords(const CAIMap* map, int x, int y) {
    const CSectorMap& sectors = map->sectorMap;
    return sectors.sectors.data[(y / 32) * sectors.width + x / 32].occluders.data;
}
} // namespace

bool CAIMap::HasKind3Occluder(int x, int y) {
    std::uint32_t word = occluders[y][x];
    if (word & kOccluderNone) return false;
    if (!(word & kOccluderChain)) return (word & 0xE00) == 0x600;
    const SectorOccluder* records = OccluderRecords(this, x, y);
    for (int index = (word >> 9) & 0xFFFFF; index >= 0; index = records[index].next) {
        if (records[index].kind == 3) return true;
    }
    return false;
}

bool CAIMap::HasKind3OccluderAt(const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    return HasKind3Occluder(x, y);
}

int CAIMap::MaterialAt(const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return -1;
    return occluders[y][x] & 0x1F;
}

AITerrainType* CAIMap::MaterialRecordAt(const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return materials.data();
    return materials.data() + (occluders[y][x] & 0x1F);
}

void CAIMap::TurnObject(CMapObject* object, const Quaternion* rotation) {
    CSectorMap* map = SectorMap();
    map->Remove(object);
    object->localRotation_ = *rotation;
    map->Add(object);
}

bool CAIMap::BridgeDeckAt(int x, int y, int* deckHeight) {
    int deck = 0;
    bool found = false;
    std::uint32_t word = occluders[y][x];
    if (!(word & kOccluderNone)) {
        if (word & kOccluderChain) {
            const SectorOccluder* records = OccluderRecords(this, x, y);
            for (int index = (word >> 9) & 0xFFFFF; index >= 0; index = records[index].next) {
                if (records[index].kind == 4) {
                    if (!(deck > records[index].top)) deck = records[index].top;
                    found = true;
                }
            }
        } else if ((word & 0xE00) == 0x800) {
            deck = (word >> 12) & 0x3FF;
            found = true;
        }
    }
    if (deckHeight) *deckHeight = deck;
    return found;
}

// --- Line of sight -------------------------------------------------------------------------------

namespace {
constexpr float kTransparency[4] = /*00791744*/ {0.0f, 0.98f, 0.99f, 1.0f};
constexpr float kMinVisibility = 0.0001f;
} // namespace

float CAIMap::LineOfSight(const Vector3* from, const Vector3* to) {
    float dx = to->x - from->x, dy = to->y - from->y;
    float dz = to->z - from->z;
    float length = static_cast<float>(std::sqrt(static_cast<double>(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz))) / (1.0f * 0.5f));   // half-cells
    EI_NAN_AUDIT(length);
    if (length == 0.0f) return 1.0f;
    float inverse = 1.0f / length;
    int steps = x87::fistp(length);

    // 16.16 fixed point position and step, in half-cells (x, y) and AI height units (z).
    int x = x87::fistp(from->x * 65536.0f / (1.0f * 0.5f));
    int y = x87::fistp(from->y * 65536.0f / (1.0f * 0.5f));
    int z = x87::fistp(from->z * heightScale) << 16;
    int stepX = x87::fistp(dx * inverse * 65536.0f / (1.0f * 0.5f));
    int stepY = x87::fistp(dy * inverse * 65536.0f / (1.0f * 0.5f));
    int toZ = x87::fistp(to->z * heightScale);
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
        if (word & kOccluderNone) continue;
        if (!(word & kOccluderChain)) {
            // A single occluder packed into the word.
            int high = (word >> 12) & 0x3FF, low = static_cast<int>(word >> 22);
            if (high < cz || low > cz) continue;
            visibility *= kTransparency[(word >> 5) & 3];
            EI_NAN_AUDIT(visibility);
            if (visibility < kMinVisibility) return kMinVisibility;
            continue;
        }
        // Several occluders: a chain in the sector's record list.
        const SectorOccluder* records = OccluderRecords(this, cx, cy);
        for (int index = (word >> 9) & 0xFFFFF; index >= 0; index = records[index].next) {
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

namespace {
// The game's atexit, for the function-local static in FindFreeCellNear.

// The spiral of cell offsets ordered by distance, built at startup: a binary tree mapping a radius (compared
// as raw float bits) to the number of offsets within it. (sector_map.h's tables, sector_spiral.cpp).
int SpiralDepth() { return SpiralRadiusTree()->depth; }
std::int32_t SpiralKey(std::uint32_t node) {
    std::int32_t key;
    std::memcpy(&key, &SpiralRadiusTree()->keys.data[node], sizeof(key));
    return key;
}
std::uint32_t SpiralMask() { return static_cast<std::uint32_t>(SpiralRadiusTree()->mask); }
const int* SpiralLengthTable() { return SpiralLengthArray()->data; }
const std::int16_t* SpiralOffsets() { return &SpiralOffsetArray()->data->x; }

SpiralCache g_findFreeSpiral = /*007C2F48*/ {};   // FindFreeCellNear's function-local static
std::uint8_t g_findFreeGuard = /*007C2F50*/ {};
} // namespace

const std::int16_t* SpiralCache::Get(float r, int* count) {
    EI_NAN_AUDIT(r, radius);
    if (r == radius) {
        *count = length;
        return SpiralOffsets();
    }
    std::uint32_t node = 1;
    for (int depth = SpiralDepth(); depth > 0; --depth) {
        // The radius is compared as an integer with the tree's keys.
        int bits;
        std::memcpy(&bits, &r, sizeof(bits));
        node = node * 2 + (bits > SpiralKey(node) ? 1 : 0);
    }
    int found = SpiralLengthTable()[SpiralMask() & node];
    *count = found;
    length = found;
    radius = r;
    return SpiralOffsets();
}

bool CAIMap::FindFreeCellNear(CUnit* unit, const Vector3* from, Vector3* target) {
    auto& guard = g_findFreeGuard;
    auto* cache = &g_findFreeSpiral;
    if (!(guard & 1)) {
        guard |= 1;
        cache->radius = -1.0f;
        cache->length = 0;
        libcrt::AtExit([] {});   // nothing to destroy
    }

    int sizeClass = unit->sizeClass;
    int x = WorldToCell(from->x), y = WorldToCell(from->y);
    if (x < 1 || y < 1 || x >= cells.width - 1 || y >= cells.height - 1) return false;
    short region = static_cast<short>(Regions()->DominantRegion(sizeClass, x, y));

    int tx = WorldToCell(target->x), ty = WorldToCell(target->y);
    int count;
    float radius;
    std::uint32_t radiusBits = 0x60AD78EC;   // about 1e20: the whole spiral
    std::memcpy(&radius, &radiusBits, sizeof(radius));
    const std::int16_t* offset = cache->Get(radius, &count);
    for (int i = 0; i < count; ++i, offset += 2) {
        int cx = offset[0] + tx, cy = offset[1] + ty;
        int reason;
        if (!CanStandAt(unit, cx, cy, &reason)) continue;
        if (!((cells[cy][cx].passable >> ((sizeClass * 4) & 31)) & 0xF)) continue;
        short r = static_cast<short>(Regions()->DominantRegion(sizeClass, cx, cy));
        if (region != 0 && r != region) continue;
        if (i != 0) {
            target->x = CellToWorld(cx);
            target->y = CellToWorld(cy);
        }
        return true;
    }
    return false;
}
