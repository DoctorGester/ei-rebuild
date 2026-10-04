// Checks for the AI alarm step (src/game/ai_alarm.cpp). Ours only (it logs, makes units speak and
// raises the group's alarms): afterwards nothing is pending, the level is cleared, a pending
// level above the old one is active, and an active level has its timer running.
#include "verify.h"

#include <cstdint>

void __fastcall StepAlarm(int* state, void*, std::uint8_t* owner);

namespace {
void* s_unused;
}

// 005AEA80.
static void __fastcall StepAlarmVerify(int* state, void*, std::uint8_t* owner) {
    int level = state[0], pending = state[1];
    StepAlarm(state, nullptr, owner);
    bool ok = state[0] == 0 && state[1] == 0;
    if (pending != 0 && pending > level) ok = ok && (state[2] == pending || state[2] == 0);
    if (state[2] != 0) ok = ok && state[3] > 0;
    ei::verify::Check("StepAlarm", true, ok, "level %d pending %d -> active %d timer %d", level, pending, state[2],
                      state[3]);
}
EI_VERIFY(0x005AEA80, StepAlarmVerify, 0, s_unused);
