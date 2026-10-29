// The step of a unit's AI alarm. A pending level above the current one becomes active for 20 steps: the AI
// log (the unitacks stream) gets "Unit <id> acknowledges with <what>" and some levels make the unit speak.
// Then, while a level is active and its timer runs, every group member on a side the owner's side is allied
// with gets its controller's alarm raised to the level. See docs/notes/ai.md.
#include "ai_alarm.h"

#include "combat.h"
#include "console.h"
#include "object_diplomacy.h"
#include "unit_ai.h"
#include "world.h"

#include <cstdint>

namespace {

// The alarm's log: the unitacks stream.
constexpr console::Stream* kAlarmLog = /*007C2EB8*/ {&console::g_unitAcksLogOut};

} // namespace

static void StepAlarmOf(AIAlarm* state, CUnitServer* owner) {
    CUnitServer* server = owner;
    CWorldServer* world = owner->ServerWorld();
    if (state->pending != AIRemark::None) {
        if (state->pending > state->level) {
            state->active = state->pending;
            state->timer = 20;
            CUnit* unit = server->unit;
            *kAlarmLog << "Unit " << static_cast<long>(unit->id_) << " acknowledges with ";
            const char* what;
            switch (state->active) {
            case AIRemark::Idle: world->TellParties(server->unit, AckKind::Rest); what = "idleSound = AAn4XyU"; break;
            case AIRemark::FightingIdle: world->TellParties(server->unit, AckKind::FightingRest); what = "idleSound = AAn4XyU"; break;
            case AIRemark::CalmFromSuspicion: what = "back from Suspection"; break;
            case AIRemark::CalmFromAggression: what = "back from Aggression"; break;
            case AIRemark::Suspicion: world->TellParties(server->unit, AckKind::Suspicion); what = "to Suspection"; break;
            case AIRemark::Aggression: world->TellParties(server->unit, AckKind::Aggression); what = "to Aggression"; break;
            case AIRemark::ToAlarm: what = "to Alarm!"; break;
            case AIRemark::Alarm: what = "ALARM ALARM ALARM"; break;
            case AIRemark::Kill: world->TellParties(server->unit, AckKind::Kill); what = "poluchi gadina"; break;
            default: what = "error... :)"; break;
            }
            console::EndLine(*kAlarmLog << what);
        }
        state->pending = AIRemark::None;
    }
    AIRemark active = state->active;
    state->level = AIRemark::None;
    if (active == AIRemark::None) return;
    if (--state->timer <= 0) {
        state->active = AIRemark::None;
        return;
    }
    int side = server->unit->side;
    for (auto& node : server->brain.memory.nearby) {
        CUnit* member = node;
        if (!world->diplomacy_.IsAllied(side, member->side)) continue;
        AIRemark& alarm = member->ServerController()->voice.level;
        if (alarm <= active) alarm = active;
    }
}

void AIAlarm::Step(CUnitServer* owner) { StepAlarmOf(this, owner); }

// ---- The state's small members ----------------------------------------------------------

void AIAlarm::Reset() {
    level = AIRemark::None;
    pending = AIRemark::None;
    active = AIRemark::None;
    timer = 0;
}

void AIAlarm::Raise(AIRemark newLevel) { pending = newLevel > pending ? newLevel : pending; }

void AIAlarm::Load(CStream* stream) {
    stream->ReadValue(level);
    stream->ReadValue(pending);
    stream->ReadValue(active);
    stream->ReadValue(timer);
}

void AIAlarm::Save(CStream* stream) {
    stream->WriteValue(level);
    stream->WriteValue(pending);
    stream->WriteValue(active);
    int steps = timer;   // keeps the original's copy of the last one
    stream->WriteValue(steps);
}
