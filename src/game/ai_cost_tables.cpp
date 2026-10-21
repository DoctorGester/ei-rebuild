// 005B6070 (thiscall on CAIMap): the three passability cost tables, 16 ints each indexed by a
// cell's passability nibble (entry 0, impassable, is -1): +0x14 the per-nibble costs, +0x54 a flat
// 0x400, +0x94 the step costs (PassCostContext and SetStepSpeeds use them). See ai_map.h.
#include "ai_map.h"

#include "ei/hook.h"

#include <cstdint>

namespace {
constexpr int kCosts[16] = {-1,    0x6400, 0x77EC, 0x8BD8, 0x2FF8, 0x37F0, 0x3FFC, 0x0DFC,
                            0x0FFF, 0x11FD, 0x1400, 0x0666, 0x0732, 0x0800, 0x0400, 0x04CC};
constexpr int kStepCosts[16] = {-1,    0x200, 0x266, 0x2CC, 0x266, 0x2CC, 0x333, 0x2CC,
                                0x333, 0x399, 0x400, 0x333, 0x399, 0x400, 0x400, 0x4CC};
} // namespace

void __fastcall InitCostTables(CAIMap* map) {
    int* costs = map->passCosts;
    int* flat = map->passCostsAlt;
    int* steps = map->stepCosts;
    for (int i = 0; i < 16; ++i) {
        costs[i] = kCosts[i];
        flat[i] = i == 0 ? -1 : 0x400;
        steps[i] = kStepCosts[i];
    }
}

EI_REWRITTEN(0x005B6070, InitCostTables);
