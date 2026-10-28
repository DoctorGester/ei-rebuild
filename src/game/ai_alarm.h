#pragma once

// An AI unit's alarm state (16 bytes in the unit controller; the "voice"): a level raised by the unit's AI
// (AIAlarm::Raise) is taken up by the step (AIAlarm::Step), which makes the unit speak and passes it to the
// allied members of its group. ai_alarm.cpp; docs/notes/ai.md.

#include <cstdint>

class CStream;
class CUnitServer;

struct AIAlarm {
    void Reset();
    // The pending level becomes at least `level`.
    void Raise(int level);
    // The step: a raised alarm taken up (the unit acknowledges), the level of the owner's group passed on
    // while it lasts.
    void Step(CUnitServer* owner);
    // The four values from / to a save (4 bytes each).
    void Load(CStream* stream);
    void Save(CStream* stream);

    int level;      // what the group's members raised this step
    int pending;    // raised by the unit's own AI since the last step
    int active;     // the level being passed on
    int timer;      // steps left of it
};
static_assert(sizeof(AIAlarm) == 0x10);

// The step of `state`, the alarm of `owner`.
