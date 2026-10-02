#include "ai_map.h"

#include "ei/hook.h"
#include "ei/x87.h"

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

bool CAIMap::IsPassable(CUnit* unit, int x, int y) {
    if (unit->GetClassId() != kClassUnit) return true;
    int reason;
    if (!CanStandAt(unit, x, y, &reason)) return false;
    return ((cells[y][x].passable >> ((unit->sizeClass * 4) & 31)) & 0xF) != 0;
}
EI_REPLACE_METHOD(0x005B79D0, CAIMap::IsPassable);
