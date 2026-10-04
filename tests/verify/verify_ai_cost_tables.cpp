// Checks for the AI map's cost tables (src/game/ai_cost_tables.cpp). Differential: the original
// fills them, the 0xC0 bytes are kept, ours fills them again; compared.
#include "verify.h"

#include <cstdint>
#include <cstring>

void __fastcall InitCostTables(std::uint8_t* map);

namespace {
using InitFn = void(__fastcall*)(std::uint8_t*);
InitFn s_original;
}

// 005B6070.
static void __fastcall InitCostTablesVerify(std::uint8_t* map) {
    s_original(map);
    std::uint8_t expected[0xC0];
    std::memcpy(expected, map + 0x14, sizeof(expected));
    InitCostTables(map);
    ei::verify::Check("InitCostTables", true, std::memcmp(expected, map + 0x14, sizeof(expected)) == 0, "");
}
EI_VERIFY(0x005B6070, InitCostTablesVerify, 5, s_original);
