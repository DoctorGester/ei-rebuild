#pragma once

// An AI unit's alarm state (the unit controller's +0xB10, 16 bytes; the "voice"): a level raised
// by the unit's AI (AIAlarm::Raise) is taken up by the step (AIAlarm::Step, 005AEA80), which makes the
// unit speak and passes it to the allied members of its group. ai_alarm.cpp; docs/notes/ai.md.

#include <cstdint>

class CStream;       // core_streams.h
class CUnitServer;   // combat.h

struct AIAlarm {
    // 005AEA40. Everything cleared.
    void Reset();
    // 005AEA50. The same, returning the state (the controller's constructor).
    AIAlarm* Construct();
    // 005AEA60 (RET 4). The pending level becomes at least `level`.
    void Raise(int level);
    // 005AEA80 (RET 4; ai_alarm.cpp). The step: a raised alarm taken up (the unit acknowledges), the
    // level of the owner's group passed on while it lasts.
    void Step(CUnitServer* owner);
    // 005AEC40 / 005AEC80 (RET 4). The four values from / to a save (4 bytes each).
    void Load(CStream* stream);
    void Save(CStream* stream);

    int level;      // +0x00 what the group's members raised this step
    int pending;    // +0x04 raised by the unit's own AI since the last step
    int active;     // +0x08 the level being passed on
    int timer;      // +0x0C steps left of it
};
static_assert(sizeof(AIAlarm) == 0x10);

// 005AEA80 (fastcall: ECX the alarm, the owner on the stack; RET 4): the step of `state`, the alarm of
// `owner` (its +0xB10).
// (005AEA80 is AIAlarm::Step.)
