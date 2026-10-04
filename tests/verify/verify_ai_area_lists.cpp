// Checks for the CAIMap area lists (src/game/ai_area_lists.cpp). Differential: the original fills
// the lists, their contents are kept, ours refills them (emptying them first); compared.
#include "verify.h"

#include "../../src/game/ai_map.h"

#include <cstdint>
#include <vector>

void __fastcall FillAreaLists(CAIMap* map);

namespace {
using FillFn = void(__fastcall*)(CAIMap*);
FillFn s_original;

std::vector<int> Contents(CAIMap* map) {
    std::vector<int> out;
    for (int i = 0; i < 6; ++i) {
        auto* list = reinterpret_cast<std::uint8_t*>(map) + 0x204 + i * 0x14;
        const int* data = *reinterpret_cast<int**>(list + 4);
        int size = *reinterpret_cast<int*>(list + 8);
        out.push_back(size);
        for (int k = 0; k < size * 2; ++k) out.push_back(data[k]);
    }
    return out;
}
} // namespace

// 005B4150.
static void __fastcall FillAreaListsVerify(CAIMap* map) {
    s_original(map);
    std::vector<int> expected = Contents(map);
    FillAreaLists(map);
    ei::verify::Check("FillAreaLists", true, Contents(map) == expected, "%d values", static_cast<int>(expected.size()));
}
EI_VERIFY(0x005B4150, FillAreaListsVerify, 5, s_original);
