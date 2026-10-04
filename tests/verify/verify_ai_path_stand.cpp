// Checks for the stand orders (src/game/ai_path_stand.cpp). Ours only (they rewrite the path):
// afterwards no cells are left, the flags are {1, 0, 0, 1} and the target is the unit's cell.
#include "verify.h"

#include "../../src/game/ai_map.h"

#include <cstdint>

void __fastcall StandAndTurn(AIPath* path, void*, int time, std::uint8_t* unit, std::uint32_t direction,
                             std::uint32_t rate);
void __fastcall StandAndTrack(AIPath* path, void*, int time, std::uint8_t* unit, std::uint32_t id, std::uint32_t from,
                              std::uint32_t to);
void __fastcall StandAndTurnUntil(AIPath* path, void*, int time, std::uint8_t* unit, std::uint32_t direction,
                                  std::uint32_t a, std::uint32_t b);
void __fastcall StandStill(AIPath* path, void*, std::uint8_t* unit);

namespace {
void* s_unused;

void CheckStand(const char* name, const AIPath* path, const std::uint8_t* unit) {
    const float* position = reinterpret_cast<const float*>(unit + 0x1C);
    bool ok = path->cells.count == 0 && path->reached && !path->failed && !path->flag6E && path->flag6F &&
              path->targetX == position[0] * 2.0f && path->targetY == position[1] * 2.0f;
    ei::verify::Check(name, true, ok, "target (%g, %g)", path->targetX, path->targetY);
}
} // namespace

// 005B1DE0.
static void __fastcall StandAndTurnVerify(AIPath* path, void*, int time, std::uint8_t* unit, std::uint32_t direction,
                                          std::uint32_t rate) {
    StandAndTurn(path, nullptr, time, unit, direction, rate);
    CheckStand("StandAndTurn", path, unit);
}
EI_VERIFY(0x005B1DE0, StandAndTurnVerify, 0, s_unused);

// 005B1F10.
static void __fastcall StandAndTrackVerify(AIPath* path, void*, int time, std::uint8_t* unit, std::uint32_t id,
                                           std::uint32_t from, std::uint32_t to) {
    StandAndTrack(path, nullptr, time, unit, id, from, to);
    CheckStand("StandAndTrack", path, unit);
}
EI_VERIFY(0x005B1F10, StandAndTrackVerify, 0, s_unused);

// 005B2050.
static void __fastcall StandAndTurnUntilVerify(AIPath* path, void*, int time, std::uint8_t* unit,
                                               std::uint32_t direction, std::uint32_t a, std::uint32_t b) {
    StandAndTurnUntil(path, nullptr, time, unit, direction, a, b);
    CheckStand("StandAndTurnUntil", path, unit);
}
EI_VERIFY(0x005B2050, StandAndTurnUntilVerify, 0, s_unused);

// 005B2190.
static void __fastcall StandStillVerify(AIPath* path, void*, std::uint8_t* unit) {
    StandStill(path, nullptr, unit);
    CheckStand("StandStill", path, unit);
}
EI_VERIFY(0x005B2190, StandStillVerify, 0, s_unused);
