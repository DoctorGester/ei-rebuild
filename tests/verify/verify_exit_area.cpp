// Checks ZoneExit::Contains (exit_area.cpp) against the original (pure). Reached by the client's
// exit search (CWorldClient::FindExitAt, world_client.cpp, every move order near an exit) and by
// 005677F0 (whether a group's units stand in one exit; `zone_base_bz1g`, `newgame` with the zone1
// exits). A point within a rounding step of a turned exit's edge may rarely differ (rule 7).
#include "verify.h"

#include "../../src/game/world.h"

namespace {

// 004E59F0: FLD [ECX+0x24] (3), FCOMP [0x0073B858] (6).
bool(__fastcall* s_contains)(const ZoneExit*, void*, float, float);
bool __fastcall ContainsVerify(const ZoneExit* self, void*, float x, float y) {
    bool expected = s_contains(self, nullptr, x, y);
    bool actual = self->Contains(x, y);
    ei::verify::Check("ZoneExit::Contains", expected, actual, "(%g, %g) in %g %g %g %g angle %g", x, y, self->x1,
                      self->y1, self->x2, self->y2, self->angle);
    return actual;
}

} // namespace

EI_VERIFY(0x004E59F0, ContainsVerify, 9, s_contains);
