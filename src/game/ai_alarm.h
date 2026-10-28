#pragma once

// An AI unit's alarm state (16 bytes in the unit controller; the "voice"): a level raised by the unit's AI
// (AIAlarm::Raise) is taken up by the step (AIAlarm::Step), which makes the unit speak and passes it to the
// allied members of its group. ai_alarm.cpp; docs/notes/ai.md.

#include <cstdint>

class CStream;
class CUnitServer;

// The levels an AI raises (AIAlarm::Raise), as the alarm's log names them when the unit takes one up.
enum AIRemark : int {
    kRemarkIdle = 1,              // "idleSound"; tells the parties AckKind::Rest
    kRemarkFightingIdle = 2,      // "idleSound" in aggression; tells the parties AckKind::FightingRest
    kRemarkCalmFromSuspicion = 3,
    kRemarkCalmFromAggression = 4,
    kRemarkSuspicion = 5,         // tells the parties AckKind::Suspicion
    kRemarkAggression = 6,        // tells the parties AckKind::Aggression
    kRemarkToAlarm = 7,
    kRemarkAlarm = 8,             // "ALARM ALARM ALARM"
    kRemarkKill = 9,              // an AI unit that killed a party unit ("poluchi gadina"); tells the parties AckKind::Kill
};

struct AIAlarm {
    void Reset();
    // The pending level becomes at least `level`.
    void Raise(AIRemark level);
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

// The step of `state`, the alarm of `owner`.
