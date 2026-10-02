#include "ai_map.h"

#include "sector_map.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"

#include <cstring>

int WorldToCell(float world) {
    float cell = 1.0f / (1.0f * 0.5f) * world - 0.5f;   // 0073B84C / (0073FD18 * 0073B86C)
    return x87::fistp(cell);
}

namespace {
// Standing heights of the 8 size classes (00791 69C).
const float* SizeClassHeights() { return reinterpret_cast<const float*>(0x0079169C); }

// Cell centre to world coordinate, as FindFreeCellNear computes it.
float CellToWorld(int cell) { return (static_cast<float>(cell) + 0.5f) * (1.0f * 0.5f); }

int PositiveOrZero(int v) { return v < 0 ? 0 : v; }

bool IsNaN(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return (bits & 0x7FFFFFFF) > 0x7F800000;
}

// The hash map at CAIMap + 0x10C (an STLport hash_map<int, unsigned short>).
struct BridgeHeightMap {
    struct Node {
        Node* next;
        int key;
        std::uint16_t value;
    };
    std::uint32_t unknown_0;
    Node** bucketsBegin;
    Node** bucketsEnd;

    const Node* Find(short x, short y) const {
        std::uint32_t key = static_cast<std::uint32_t>(x * 0x10000 + y);
        std::uint32_t count = static_cast<std::uint32_t>((bucketsEnd - bucketsBegin));
        const Node* node = bucketsBegin[key % count];
        while (node && static_cast<std::uint32_t>(node->key) != key) node = node->next;
        return node;
    }
};

const BridgeHeightMap* Bridges(const CAIMap* map) {
    return reinterpret_cast<const BridgeHeightMap*>(map->bridgeHeights);
}
} // namespace

int __stdcall LevelForSizeClass(int sizeClass) {
    if (sizeClass < 0 || sizeClass >= 8) return sizeClass & ~0xFF;   // XOR AL,AL
    return PositiveOrZero(x87::fistp((2.0f - SizeClassHeights()[sizeClass]) * 16.0f));
}
EI_REPLACE(0x005B7B20, LevelForSizeClass);

int __stdcall StandingLevel(const CUnit* unit) {
    return PositiveOrZero(x87::fistp((2.0f - unit->standingHeight) * 16.0f));
}
EI_REPLACE(0x005B7B70, StandingLevel);

int __fastcall BridgeHeight(const void* map, void*, short x, short y) {
    const auto* node = static_cast<const BridgeHeightMap*>(map)->Find(x, y);
    return node ? node->value : 0;
}
EI_REPLACE(0x005B3160, BridgeHeight);

// --- Cell queries --------------------------------------------------------------------------------

bool CAIMap::CanStandAt(CUnit* unit, int x, int y, int* reason) {
    if (x < 0 || x >= width || y < 0 || y >= height) return false;
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
EI_REPLACE_METHOD(0x005B80F0, CAIMap::CanStandAt);

bool CAIMap::CanStandAtFromHere(CUnit* unit, int x, int y, int* reason) {
    if (x < 0 || x >= width || y < 0 || y >= height) return false;
    int level = PositiveOrZero(x87::fistp((2.0f - unit->standingHeight) * 16.0f));
    *reason = 1;
    // The unit's own cell is not bounds-checked, as in the original.
    if (cells[y][x].level > static_cast<std::uint8_t>(level)) {
        int ux = WorldToCell(unit->positionX), uy = WorldToCell(unit->positionY);
        if (cells[y][x].level > cells[uy][ux].level) return false;
    }
    level &= 0xFF;
    *reason = 2;
    if (waterDepth[y][x] > level) {
        int ux = WorldToCell(unit->positionX), uy = WorldToCell(unit->positionY);
        if (waterDepth[y][x] > waterDepth[uy][ux]) return false;
    }
    *reason = 0;
    return true;
}
EI_REPLACE_METHOD(0x005B81B0, CAIMap::CanStandAtFromHere);

bool CAIMap::IsPassable(CUnit* unit, int x, int y) {
    if (unit->GetClassId() != kClassUnit) return true;
    int reason;
    if (!CanStandAt(unit, x, y, &reason)) return false;
    return ((cells[y][x].passable >> ((unit->sizeClass * 4) & 31)) & 0xF) != 0;
}
EI_REPLACE_METHOD(0x005B79D0, CAIMap::IsPassable);

bool CAIMap::IsPassableWithoutSelf(CUnit* unit, const Vector3* position) {
    if (unit->GetClassId() != kClassUnit) return true;
    auto* sectorMap = reinterpret_cast<CSectorMap*>(reinterpret_cast<char*>(this) + 0x124);
    sectorMap->Remove(unit);
    int y = WorldToCell(position->y);
    int x = WorldToCell(position->x);
    bool passable = true;
    if (unit->GetClassId() == kClassUnit) {
        int reason;
        passable = CanStandAt(unit, x, y, &reason) &&
                   ((cells[y][x].passable >> ((unit->sizeClass * 4) & 31)) & 0xF) != 0;
    }
    sectorMap->Add(unit);
    return passable;
}
EI_REPLACE_METHOD(0x005B7A40, CAIMap::IsPassableWithoutSelf);

bool CAIMap::IsUnitCellPassable(CUnit* unit) {
    int x = WorldToCell(unit->positionX), y = WorldToCell(unit->positionY);
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    return ((cells[y][x].passable >> ((unit->sizeClass * 4) & 31)) & 0xF) != 0;
}
EI_REPLACE_METHOD(0x005BA6E0, CAIMap::IsUnitCellPassable);

bool CAIMap::IsPassableForClass(std::uint8_t sizeClass, const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    return ((cells[y][x].passable >> ((sizeClass & 7) * 4)) & 0xF) != 0;
}
EI_REPLACE_METHOD(0x005BA790, CAIMap::IsPassableForClass);

bool CAIMap::CanLeaveCell(CUnit* unit) {
    static const int kOffsets[9][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    int x = WorldToCell(unit->positionX), y = WorldToCell(unit->positionY);
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    int sizeClass = unit->sizeClass;
    const ClimbTable& table = sizeClass == 0 ? climbSmall : climbLarge;
    const int* climbCost = table.costs + table.count / 2;
    int here = cells[y][x].groundLevel;
    for (const auto& o : kOffsets) {
        int nx = o[0] + x, ny = y + o[1];
        if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
        const AIMapCell& cell = cells[ny][nx];
        if (!((cell.passable >> ((sizeClass * 4) & 31)) & 0xF)) continue;
        if (climbCost[cell.groundLevel - here] > 0) return true;
    }
    return false;
}
EI_REPLACE_METHOD(0x005BA830, CAIMap::CanLeaveCell);

// --- Heights -------------------------------------------------------------------------------------

float CAIMap::GroundHeight(int x, int y) {
    return static_cast<float>(static_cast<int>(cells[y][x].groundLevel)) / heightScale;
}
EI_REPLACE_METHOD(0x005B77F0, CAIMap::GroundHeight);

float CAIMap::GroundHeightAt(const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 0 || y < 0 || x >= width || y >= height) return 0.0f;
    return static_cast<float>(static_cast<int>(cells[y][x].groundLevel)) / heightScale;
}
EI_REPLACE_METHOD(0x005BB960, CAIMap::GroundHeightAt);

float CAIMap::SurfaceHeightAt(const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 0 || y < 0 || x >= width || y >= height) return 0.0f;
    const AIMapCell& cell = cells[y][x];
    if (!(cell.flags & kCellBridge)) return static_cast<float>(static_cast<int>(cell.groundLevel)) / heightScale;
    const auto* node = Bridges(this)->Find(static_cast<short>(x), static_cast<short>(y));
    return static_cast<float>(node ? static_cast<int>(node->value) : 0) / heightScale;
}
EI_REPLACE_METHOD(0x005BBA10, CAIMap::SurfaceHeightAt);

float CAIMap::TopHeightAt(const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 0 || y < 0 || x >= width || y >= height) return 0.0f;
    const AIMapCell& cell = cells[y][x];
    return static_cast<float>((cell.flags & 0x3F) + cell.groundLevel) / heightScale;
}
EI_REPLACE_METHOD(0x005BBB20, CAIMap::TopHeightAt);

float CAIMap::SlopeAt(const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 1 || y < 1 || x > width - 2 || y > height - 2) return 0.0f;
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
EI_REPLACE_METHOD(0x005BC2A0, CAIMap::SlopeAt);

bool CAIMap::IsBridgeAt(const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    return (cells[y][x].flags >> 6) & 1;
}
EI_REPLACE_METHOD(0x005BBBD0, CAIMap::IsBridgeAt);

bool CAIMap::CellFlag80At(const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    return (cells[y][x].flags >> 7) & 1;
}
EI_REPLACE_METHOD(0x005BBC70, CAIMap::CellFlag80At);

namespace {
// An occluder record in a sector's list (sector + 0x44), for cells holding more than one.
#pragma pack(push, 1)
struct OccluderRecord {
    std::int16_t high;          // bits 12-21 of a single-occluder word
    std::int16_t low;           // bits 22-31
    std::int8_t kind;           // bits 9-11
    std::int8_t transparency;   // bits 5-6
    std::int16_t next;          // < 0 ends the chain
};
#pragma pack(pop)
static_assert(sizeof(OccluderRecord) == 8);

constexpr std::uint32_t kOccluderNone = 0x100;
constexpr std::uint32_t kOccluderChain = 0x80;

const OccluderRecord* OccluderRecords(const CAIMap* map, int x, int y) {
    int sector = (y / 32) * map->sectorsPerRow + x / 32;
    return *reinterpret_cast<OccluderRecord* const*>(map->sectors + sector * 0x54 + 0x44);
}
} // namespace

bool CAIMap::HasKind3Occluder(int x, int y) {
    std::uint32_t word = occluders[y][x];
    if (word & kOccluderNone) return false;
    if (!(word & kOccluderChain)) return (word & 0xE00) == 0x600;
    const OccluderRecord* records = OccluderRecords(this, x, y);
    for (int index = (word >> 9) & 0xFFFFF; index >= 0; index = records[index].next) {
        if (records[index].kind == 3) return true;
    }
    return false;
}
EI_REPLACE_METHOD(0x005BBD10, CAIMap::HasKind3Occluder);

bool CAIMap::HasKind3OccluderAt(const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    return HasKind3Occluder(x, y);
}
EI_REPLACE_METHOD(0x005BBDB0, CAIMap::HasKind3OccluderAt);

int CAIMap::MaterialAt(const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 0 || y < 0 || x >= width || y >= height) return -1;
    return occluders[y][x] & 0x1F;
}
EI_REPLACE_METHOD(0x005BBE40, CAIMap::MaterialAt);

void* CAIMap::MaterialRecordAt(const Vector3* position) {
    int x = WorldToCell(position->x), y = WorldToCell(position->y);
    if (x < 0 || y < 0 || x >= width || y >= height) return materials;
    return materials + (occluders[y][x] & 0x1F) * 0x118;
}
EI_REPLACE_METHOD(0x005BC1F0, CAIMap::MaterialRecordAt);

bool CAIMap::BridgeDeckAt(int x, int y, int* deckHeight) {
    int deck = 0;
    bool found = false;
    std::uint32_t word = occluders[y][x];
    if (!(word & kOccluderNone)) {
        if (word & kOccluderChain) {
            const OccluderRecord* records = OccluderRecords(this, x, y);
            for (int index = (word >> 9) & 0xFFFFF; index >= 0; index = records[index].next) {
                if (records[index].kind == 4) {
                    if (!(deck > records[index].high)) deck = records[index].high;
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
EI_REPLACE_METHOD(0x005B64E0, CAIMap::BridgeDeckAt);

// --- Line of sight -------------------------------------------------------------------------------

namespace {
// Transparency of an occluder class (0073 global at 00791744).
const float* Transparency() { return reinterpret_cast<const float*>(0x00791744); }
constexpr float kMinVisibility = 0.0001f;
} // namespace

float CAIMap::LineOfSight(const Vector3* from, const Vector3* to) {
    float dx = to->x - from->x, dy = to->y - from->y;
    float dz = to->z - from->z;
    float length = static_cast<float>(x87::fsqrt(dz * dz + dy * dy + dx * dx) / (1.0f * 0.5f));   // half-cells
    if (length == 0.0f || length != length) return 1.0f;   // FCOM sets "equal" for NaN too
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
        return cx < 0 || cy < 0 || cx >= width || cy >= height;
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
            visibility *= Transparency()[(word >> 5) & 3];
            if (!(visibility >= kMinVisibility)) return kMinVisibility;
            continue;
        }
        // Several occluders: a chain in the sector's record list.
        const OccluderRecord* records = OccluderRecords(this, cx, cy);
        for (int index = (word >> 9) & 0xFFFFF; index >= 0; index = records[index].next) {
            const OccluderRecord& o = records[index];
            if (o.high < cz || o.low > cz) continue;
            visibility *= Transparency()[o.transparency];
            if (!(visibility >= kMinVisibility)) return kMinVisibility;
        }
    }
    return visibility;
}
EI_REPLACE_METHOD(0x005BC3E0, CAIMap::LineOfSight);

// --- Free cell search ----------------------------------------------------------------------------

namespace {
// The game's atexit (006E727B), for the function-local static in FindFreeCellNear.
__declspec(naked) int __cdecl GameAtExit(void(__cdecl*)()) EI_ORIGINAL(0x006E727B)

// The spiral of cell offsets ordered by distance, built at startup: a binary tree mapping a radius
// (compared as raw float bits) to the number of offsets within it.
int SpiralDepth() { return *reinterpret_cast<const int*>(0x007C2FEC); }
const int* SpiralKeys() { return *reinterpret_cast<const int* const*>(0x007C2FDC); }
std::uint32_t SpiralMask() { return *reinterpret_cast<const std::uint32_t*>(0x007C2FF0); }
const int* SpiralLengthTable() { return *reinterpret_cast<const int* const*>(0x007C2FC4); }
const std::int16_t* SpiralOffsets() { return *reinterpret_cast<const std::int16_t* const*>(0x007C2FFC); }
} // namespace

const std::int16_t* SpiralCache::Get(float r, int* count) {
    if (r == radius || IsNaN(r) || IsNaN(radius)) {    // FCOMP; TEST AH,0x40: NaN compares equal
        *count = length;
        return SpiralOffsets();
    }
    std::uint32_t node = 1;
    for (int depth = SpiralDepth(); depth > 0; --depth) {
        // The radius is compared as an integer with the tree's keys.
        int bits;
        std::memcpy(&bits, &r, sizeof(bits));
        node = node * 2 + (bits > SpiralKeys()[node] ? 1 : 0);
    }
    int found = SpiralLengthTable()[SpiralMask() & node];
    *count = found;
    length = found;
    radius = r;
    return SpiralOffsets();
}
EI_REPLACE_METHOD(0x005BFC40, SpiralCache::Get);

bool CAIMap::FindFreeCellNear(CUnit* unit, const Vector3* from, Vector3* target) {
    // Function-local static of the original (guard bit 007C2F50 & 1, destructor 005BAC00).
    auto& guard = *reinterpret_cast<std::uint8_t*>(0x007C2F50);
    auto* cache = reinterpret_cast<SpiralCache*>(0x007C2F48);
    if (!(guard & 1)) {
        guard |= 1;
        cache->radius = -1.0f;
        cache->length = 0;
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x005BAC00));
    }

    int sizeClass = unit->sizeClass;
    int x = WorldToCell(from->x), y = WorldToCell(from->y);
    if (x < 1 || y < 1 || x >= width - 1 || y >= height - 1) return false;
    short region = static_cast<short>(Regions()->RegionAt(sizeClass, x, y));

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
        short r = static_cast<short>(Regions()->RegionAt(sizeClass, cx, cy));
        if (region != 0 && r != region) continue;
        if (i != 0) {
            target->x = CellToWorld(cx);
            target->y = CellToWorld(cy);
        }
        return true;
    }
    return false;
}
EI_REPLACE_METHOD(0x005BA9D0, CAIMap::FindFreeCellNear);
