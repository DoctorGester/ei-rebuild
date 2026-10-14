// Checks for the AI map's cost tables (src/game/ai_cost_tables.cpp). Differential: the original
// fills them, the 0xC0 bytes are kept, ours fills them again; compared.
#include "verify.h"
#include "../../src/game/ai_map.h"

#include <cstdint>
#include <cstring>


namespace {
using InitFn = void(__fastcall*)(CAIMap*);
InitFn s_original;
}

// 005B6070.
static void __fastcall InitCostTablesVerify(CAIMap* map) {
    s_original(map);
    // The three tables (passCosts, passCostsAlt, stepCosts), 0xC0 bytes from +0x14.
    std::uint8_t expected[0xC0];
    std::memcpy(expected, map->passCosts, sizeof(expected));
    InitCostTables(map);
    ei::verify::Check("InitCostTables", true, std::memcmp(expected, map->passCosts, sizeof(expected)) == 0, "");
}
EI_VERIFY(0x005B6070, InitCostTablesVerify, 5, s_original);
