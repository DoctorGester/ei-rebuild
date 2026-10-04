// Checks for CAIMap::RefreshArea (src/game/ai_refresh.cpp). The original first, on the real map;
// its cell words and marks over the outer rectangle are kept, then ours runs from the state the
// original left (every value it writes is recomputed from the terrain, not from itself) and the
// same words and marks are compared.
#include "verify.h"

#include "../../src/game/ai_map.h"

#include <cstring>
#include <vector>

namespace {
using RefreshFn = void(__fastcall*)(CAIMap*, void*, int, int, int, int);
RefreshFn s_originalRefresh;

std::uint32_t Word(CAIMap* map, int x, int y) {
    auto** rows = *reinterpret_cast<std::uint8_t***>(reinterpret_cast<std::uint8_t*>(map) + 0x269C);
    std::uint32_t w;
    std::memcpy(&w, rows[y] + x * 9, 4);
    return w;
}
std::uint8_t Mark(CAIMap* map, int x, int y) {
    auto** rows = *reinterpret_cast<std::uint8_t***>(reinterpret_cast<std::uint8_t*>(map) + 0x26BC);
    return rows[y][x];
}
} // namespace

// 005B8FA0.
static void __fastcall RefreshAreaVerify(CAIMap* self, void*, int x0, int x1, int y0, int y1) {
    s_originalRefresh(self, nullptr, x0, x1, y0, y1);
    int ox0 = x0 - 3 < 0 ? 0 : x0 - 3, oy0 = y0 - 3 < 0 ? 0 : y0 - 3;
    int ox1 = x1 + 3 > self->width - 1 ? self->width - 1 : x1 + 3;
    int oy1 = y1 + 3 > self->height - 1 ? self->height - 1 : y1 + 3;
    std::vector<std::uint32_t> words;
    std::vector<std::uint8_t> marks;
    for (int y = oy0; y <= oy1; ++y)
        for (int x = ox0; x <= ox1; ++x) {
            words.push_back(Word(self, x, y));
            marks.push_back(Mark(self, x, y));
        }
    self->RefreshArea(x0, x1, y0, y1);
    std::size_t i = 0;
    int differences = 0;
    for (int y = oy0; y <= oy1; ++y)
        for (int x = ox0; x <= ox1; ++x, ++i)
            if (words[i] != Word(self, x, y) || marks[i] != Mark(self, x, y)) ++differences;
    ei::verify::Check("CAIMap::RefreshArea", 0, differences, "[%d,%d]x[%d,%d]", x0, x1, y0, y1);
}
EI_VERIFY(0x005B8FA0, RefreshAreaVerify, 5, s_originalRefresh);
