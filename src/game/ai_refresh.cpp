// CAIMap::Refresh (005B8FA0): recomputes the passability of a rectangle of cells after the map
// changed: per standing level the cell marks (005B8BA0) over the rectangle grown by 3 cells, packed
// as nibbles into the cells, the level's special areas (005B89F0), the second pass (005B8710), the
// map border, and with the sector map on (+2) the objects around it are taken out and put back
// around the region map's update (005AFEE0). See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"

#include "core_maths.h"

#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/x87.h"

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

constexpr int kMarks = 0x26BC;        // rows of per-cell signed marks
constexpr int kCells = 0x269C;        // rows of 9-byte cells (the first dword holds 8 nibbles)
constexpr int kSectorMap = 0x124;
constexpr int kRegionMap = 0x22F8;
constexpr std::uintptr_t kLevelHeights = 0x007912BC;   // 8 floats
constexpr std::uintptr_t kObjectListVtable = 0x0073D454;
constexpr int kLevels = 8;

// The per-level area lists: levels 5 and 6 use +0x218, 7 uses +0x22C, the others +0x204; each a
// CArray of {int, int} (data +4, size +8).
int AreaList(int level) {
    if (level >= 5 && level <= 6) return 0x218;
    if (level == 7) return 0x22C;
    return 0x204;
}

// 005B8BA0 (RET 0x14, AL). The mark of cell (x, y) for a level and its height (scratch: 0x34 bytes).
__declspec(naked) std::uint8_t __fastcall CellMarkAt(CAIMap*, void*, void* /*scratch*/, int, int, int, int) EI_ORIGINAL(0x005B8BA0)
// 005B89F0 (RET 0x1C). A level's special area over the inner rectangle.
__declspec(naked) void __fastcall ApplyArea(CAIMap*, void*, int, int, int, int, int, int, int) EI_ORIGINAL(0x005B89F0)
// 005B8710 (RET 0x10). The second pass over the inner rectangle.
__declspec(naked) void __fastcall RefreshSecondPass(CAIMap*, void*, int, int, int, int) EI_ORIGINAL(0x005B8710)
// The sector map (+0x124): objects within a radius of a point into a list (RET 0xC), Remove, Add.
__declspec(naked) void __fastcall SectorCollect(void*, void*, const float*, void*, float) EI_ORIGINAL(0x005C0BD0)
__declspec(naked) void __fastcall SectorRemove(void*, void*, void*) EI_ORIGINAL(0x005C1010)
__declspec(naked) void __fastcall SectorAdd(void*, void*, void*) EI_ORIGINAL(0x005C0DF0)
// 005AFEE0 (RET 0x14). The region map updates the blocks [x0, x1] x [y0, y1].
__declspec(naked) void __fastcall RegionsUpdate(void*, void*, CAIMap*, int, int, int, int) EI_ORIGINAL(0x005AFEE0)

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

std::uint32_t& CellWord(CAIMap* map, int x, int y) {
    return *reinterpret_cast<std::uint32_t*>(At<std::uint8_t**>(map, kCells)[y] + x * 9);
}

struct ObjectList {
    struct Node {
        Node* next;
        Node* prev;
        void* data;
    };
    void* vtable;
    Node* head;
    Node* tail;
    int count;
    Node* free;
    mfc::CPlex* blocks;
    int blockSize;
};

} // namespace

#pragma float_control(precise, on, push)

void CAIMap::RefreshArea(int x0, int x1, int y0, int y1) {
    if (x0 > x1 || y0 > y1) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (width - 1 < x1) x1 = width - 1;
    if (height - 1 < y1) y1 = height - 1;
    // The inner rectangle, and the outer one grown by 3 cells.
    int ix0 = x0, ix1 = x1, iy0 = y0, iy1 = y1;
    int ox0 = x0 - 3, ox1 = x1 + 3, oy0 = y0 - 3, oy1 = y1 + 3;
    if (ox0 < 0) ox0 = 0;
    if (oy0 < 0) oy0 = 0;
    if (width - 1 < ox1) ox1 = width - 1;
    if (height - 1 < oy1) oy1 = height - 1;

    alignas(4) std::uint8_t scratch[0x34];
    for (int level = 0; level < kLevels; ++level) {
        float h = StoreFloat(static_cast<float>(static_cast<double>(reinterpret_cast<const float*>(kLevelHeights)[level]) *
                                                heightScale));
        int levelHeight = x87::fistp(h);
        for (int y = oy0; y <= oy1; ++y)
            for (int x = ox0; x <= ox1; ++x) {
                std::uint8_t* row = At<std::uint8_t**>(this, kMarks)[y];
                row[x] = CellMarkAt(this, nullptr, scratch, level, levelHeight, x, y);
            }
        int shift = level * 4;
        std::uint32_t keep = ~(0xFu << shift);
        for (int y = iy0; y <= iy1; ++y)
            for (int x = ix0; x <= ix1; ++x) {
                CellWord(this, x, y) &= keep;
                int mark = static_cast<std::int8_t>(At<std::uint8_t**>(this, kMarks)[y][x]);
                CellWord(this, x, y) |= static_cast<std::uint32_t>(mark << shift);
            }
        auto* areas = reinterpret_cast<std::uint8_t*>(this) + AreaList(level);
        for (int i = 0; i < At<int>(areas, 8); ++i) {
            const int* entry = At<int*>(areas, 4) + i * 2;
            ApplyArea(this, nullptr, level, entry[0], entry[1], ix0, ix1, iy0, iy1);
        }
    }
    RefreshSecondPass(this, nullptr, ix0, ix1, iy0, iy1);

    // The three outermost rows and columns are never passable.
    for (int x = ox0; x <= ox1; ++x) {
        CellWord(this, x, 0) = 0;
        CellWord(this, x, height - 1) = 0;
        CellWord(this, x, 1) = 0;
        CellWord(this, x, height - 2) = 0;
        CellWord(this, x, 2) = 0;
        CellWord(this, x, height - 3) = 0;
    }
    for (int y = oy0; y <= oy1; ++y) {
        CellWord(this, 0, y) = 0;
        CellWord(this, width - 1, y) = 0;
        CellWord(this, 1, y) = 0;
        CellWord(this, width - 2, y) = 0;
        CellWord(this, 2, y) = 0;
        CellWord(this, width - 3, y) = 0;
    }

    if (!At<std::uint8_t>(this, 2)) return;
    // The region blocks (8x8 cells) around the outer rectangle.
    int bx0 = ox0 / 8 - 1, by0 = oy0 / 8 - 1;
    int bx1 = ox1 / 8 + 1, by1 = oy1 / 8 + 1;
    ObjectList objects{reinterpret_cast<void*>(kObjectListVtable), nullptr, nullptr, 0, nullptr, nullptr, 10};
    double halfW = static_cast<double>(ox1 - ox0) * GameFloat(0x0073B86C) + GameFloat(0x0073B84C);
    double halfH = static_cast<double>(oy1 - oy0) * GameFloat(0x0073B86C) + GameFloat(0x0073B84C);
    float scale = StoreFloat(static_cast<float>(static_cast<double>(GameFloat(0x0073FD18)) * GameFloat(0x0073B86C)));
    float radius = StoreFloat(static_cast<float>(x87::fsqrt(halfW * halfW + halfH * halfH) * scale));
    float centre[2];
    float cx = StoreFloat(static_cast<float>(static_cast<double>(ox0 + ox1) * scale * GameFloat(0x0073B86C)));
    float cy = StoreFloat(static_cast<float>(static_cast<double>(oy0 + oy1) * scale * GameFloat(0x0073B86C)));
    centre[0] = cx;
    centre[1] = cy;
    void* sectorMap = reinterpret_cast<std::uint8_t*>(this) + kSectorMap;
    SectorCollect(sectorMap, nullptr, centre, &objects, radius);
    for (auto* node = objects.head; node;) {
        auto* next = node->next;
        SectorRemove(sectorMap, nullptr, node->data);
        node = next;
    }
    RegionsUpdate(reinterpret_cast<std::uint8_t*>(this) + kRegionMap, nullptr, this, bx0, bx1, by0, by1);
    for (auto* node = objects.head; node;) {
        auto* next = node->next;
        SectorAdd(sectorMap, nullptr, node->data);
        node = next;
    }
    objects.vtable = reinterpret_cast<void*>(kObjectListVtable);
    objects.count = 0;
    objects.free = nullptr;
    objects.tail = nullptr;
    objects.head = nullptr;
    mfc::PlexFreeDataChain(objects.blocks);
}

// ---- CellMark ---------------------------------------------------------------------------------

namespace {

constexpr int kTerrainTypes = 0xFC;     // 0x118-byte records per terrain type (low 5 bits of the occluder word)
constexpr int kOccluders = 0x26AC;
constexpr int kSectorRecords = 0x138;   // 0x54-byte sector records
constexpr int kSectorsPerRow = 0x128;
constexpr std::uintptr_t kCostTable = 0x007912A4;    // int per occluder type: cost added by full cover
constexpr std::uintptr_t kDampTable = 0x007912B0;    // int per occluder type: the damping exponent
constexpr std::uintptr_t kDampScale = 0x007C2F8C;    // float
constexpr std::uintptr_t kMarkTable = 0x00791164;    // 5 rows x 16 dwords

// The scratch state CellMark builds (0x18 bytes used).
struct MarkState {
    int bottom;          // +0x00
    int top;             // +0x04
    int passage;         // +0x08 0x400 open, damped by occluders; negative: blocked
    int cost;            // +0x0C
    int unknown10;       // +0x10
    std::uint8_t doubled;   // +0x14 the cost counts twice (occluder type 3)
};

// passage * e^(table * a * b), stored to a float and rounded (FISTP), with the original's
// FLDL2E / F2XM1 / FSCALE sequence.
__declspec(naked) int __cdecl Damp(int /*table*/, float /*a*/, float /*b*/, int /*passage*/) {
    __asm {
        fild dword ptr [esp + 4]
        fmul dword ptr [esp + 8]
        fmul dword ptr [esp + 12]
        fldl2e
        fmulp st(1), st
        fld st(0)
        frndint
        fxch st(1)
        fsub st, st(1)
        f2xm1
        fld1
        faddp st(1), st
        fscale
        fstp st(1)
        fimul dword ptr [esp + 16]
        fstp dword ptr [esp + 8]
        fld dword ptr [esp + 8]
        fistp dword ptr [esp + 12]
        mov eax, dword ptr [esp + 12]
        ret
    }
}

int TableInt(std::uintptr_t table, int i) { return reinterpret_cast<const int*>(table)[i]; }

// One occluder of type `type` spanning heights [lowest, highest] against the cell's column.
// `dampFirst`: the order of the two factors of the exponent (the two callers differ).
void Occlude(MarkState* s, int type, int lowest, int highest, bool sectorOrder) {
    switch (type) {
    case 2:
    case 4:
        return;
    case 3:
        s->doubled = 1;
        return;
    default:
        break;
    }
    if (s->passage < 0 || lowest > s->top || highest < s->bottom) return;
    int lo = lowest > s->bottom ? lowest : s->bottom;
    int hi = highest < s->top ? highest : s->top;
    float frac = StoreFloat(static_cast<float>(static_cast<double>(hi - lo) / (s->top - s->bottom)));
    float added = StoreFloat(static_cast<float>(static_cast<double>(TableInt(kCostTable, type)) * frac));
    s->cost += x87::fistp(added);
    float scale = *reinterpret_cast<const volatile float*>(kDampScale);
    s->passage = sectorOrder ? Damp(TableInt(kDampTable, type), scale, frac, s->passage)
                             : Damp(TableInt(kDampTable, type), frac, scale, s->passage);
}

} // namespace

std::uint32_t CAIMap::CellMark(void* scratch, int level, int levelHeight, int x, int y) {
    auto* s = static_cast<MarkState*>(scratch);
    std::uint8_t* cell = At<std::uint8_t**>(this, kCells)[y] + x * 9;
    std::uint32_t* occluder = &At<std::uint32_t**>(this, kOccluders)[y][x];
    std::uint8_t* terrain = At<std::uint8_t*>(this, kTerrainTypes) + (*occluder & 0x1F) * 0x118;
    std::uint16_t extra = *reinterpret_cast<std::uint16_t*>(cell + 6);
    if ((*occluder & 0x100) && !(cell[6] & 0x3F)) return (At<std::uint32_t>(terrain, 0x114) >> (level * 4)) & 0xF;

    int bottom = *reinterpret_cast<std::uint16_t*>(cell + 4);
    if (level == 0) {
        if (extra & 0x3F) bottom += extra & 0x3F;
        s->doubled = 0;
        s->passage = 0x400;
        s->cost = 1;
        s->bottom = bottom;
        s->top = bottom + levelHeight;
        s->unknown10 = 0x7F8;
    } else {
        int base = At<int>(terrain, 0x104);
        int ceiling = level != 1 ? levelHeight : 0;
        s->doubled = 0;
        s->passage = 0x400;
        s->cost = base;
        s->bottom = bottom;
        s->top = levelHeight + bottom;
        s->unknown10 = ceiling;
        if (extra & 0x3F) {
            int depth = extra & 0x3F;
            if (depth == 0x3F || depth >= ceiling - 1) {
                s->passage = -1;
            } else if (ceiling < 0x400) {
                if (depth > ceiling / 2) {
                    s->passage = 0x320;
                    s->cost = base + 30;
                } else {
                    s->passage = 0x370;
                }
            }
        }
    }

    std::uint32_t word = *occluder;
    if (!(word & 0x100)) {
        if (word & 0x80) {
            // A chain of occluder records in the cell's sector (32x32 cells).
            std::uint8_t* sector = At<std::uint8_t*>(this, kSectorRecords) +
                                   ((y / 32) * At<int>(this, kSectorsPerRow) + x / 32) * 0x54;
            int index = static_cast<int>((word >> 9) & 0xFFFFF);
            do {
                std::uint8_t* record = At<std::uint8_t*>(sector, 0x44) + index * 8;
                int lowest = *reinterpret_cast<std::int16_t*>(record + 2);
                int type = static_cast<std::int8_t>(record[4]);
                int highest = *reinterpret_cast<std::int16_t*>(record);
                Occlude(s, type, lowest, highest, true);
                index = *reinterpret_cast<std::int16_t*>(record + 6);
            } while (index >= 0);
        } else {
            int type = static_cast<int>((word >> 9) & 7);
            int lowest = static_cast<int>(word >> 22);
            int highest = static_cast<int>((word >> 12) & 0x3FF);
            Occlude(s, type, lowest, highest, false);
        }
    }

    if (s->passage < 0) return 0;
    if (s->cost > 0x46) return 0;
    int column = (s->passage + 0x32) / 102;
    if (column > 15) column = 15;
    if (s->doubled) s->cost *= 2;
    int cost = s->cost;
    int row;
    if (cost >= 10) row = cost >= 0x23 ? 4 : 3;
    else if (cost >= 3) row = 2;
    else row = cost >= 2 ? 1 : 0;
    return reinterpret_cast<const std::uint32_t*>(kMarkTable)[row * 16 + column];
}

#pragma float_control(pop)

EI_REPLACE_METHOD(0x005B8FA0, CAIMap::RefreshArea);
EI_REPLACE_METHOD(0x005B8BA0, CAIMap::CellMark);
