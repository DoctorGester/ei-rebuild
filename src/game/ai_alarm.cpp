// The step of a unit's AI alarm. A pending level above the current one becomes active for 20 steps: the AI
// log (the unitacks stream) gets "Unit <id> acknowledges with <what>" and some levels make the unit speak.
// Then, while a level is active and its timer runs, every group member on a side the owner's side is allied
// with gets its controller's alarm raised to the level. See docs/notes/ai.md.
#include "ai_alarm.h"
#include "console.h"
#include "game_strings.h"

#include "combat.h"
#include "object_diplomacy.h"
#include "unit_ai.h"
#include "world.h"

#include <cstdint>
#include "game_data.h"

namespace {

// The alarm's log: the unitacks stream.
constexpr console::Stream* kAlarmLog = /*007C2EB8*/ {&console::g_unitAcksLogOut};
console::Stream& AlarmLog() { return *kAlarmLog; }

} // namespace

static void StepAlarmOf(AIAlarm* state, CUnitServer* owner) {
    CUnitServer* server = owner;
    CWorldServer* world = owner->ServerWorld();
    if (state->pending != 0) {
        if (state->pending > state->level) {
            state->active = state->pending;
            state->timer = 20;
            CUnit* unit = server->unit;
            AlarmLog() << "Unit " << static_cast<long>(unit->id_) << " acknowledges with ";
            const char* what;
            switch (state->active) {
            case 1: world->TellParties(server->unit, 0x31); what = "idleSound = AAn4XyU"; break;
            case 2: world->TellParties(server->unit, 0x32); what = "idleSound = AAn4XyU"; break;
            case 3: what = "back from Suspection"; break;
            case 4: what = "back from Aggression"; break;
            case 5: world->TellParties(server->unit, 0x2F); what = "to Suspection"; break;
            case 6: world->TellParties(server->unit, 0x2E); what = "to Aggression"; break;
            case 7: what = "to Alarm!"; break;
            case 8: what = "ALARM ALARM ALARM"; break;
            case 9: world->TellParties(server->unit, 0x30); what = "poluchi gadina"; break;
            default: what = "error... :)"; break;
            }
            console::EndLine(AlarmLog() << what);
        }
        state->pending = 0;
    }
    int active = state->active;
    state->level = 0;
    if (!active) return;
    if (--state->timer <= 0) {
        state->active = 0;
        return;
    }
    int side = server->unit->side;
    std::uint32_t allies = world->diplomacy_.allied_[side];
    for (auto node = server->Brain()->memory.nearby.begin(); node != server->Brain()->memory.nearby.end(); ++node) {
        auto* member = *node;
        if (!(allies & (1u << (member->side & 31)))) continue;
        int& alarm = member->ServerController()->voice.level;
        if (alarm <= active) alarm = active;
    }
}

void AIAlarm::Step(CUnitServer* owner) { StepAlarmOf(this, owner); }

// ---- The state's small members ----------------------------------------------------------

void AIAlarm::Reset() {
    level = 0;
    pending = 0;
    active = 0;
    timer = 0;
}

void AIAlarm::Raise(int newLevel) { pending = newLevel > pending ? newLevel : pending; }

void AIAlarm::Load(CStream* stream) {
    stream->Read(&level, 4);
    stream->Read(&pending, 4);
    stream->Read(&active, 4);
    stream->Read(&timer, 4);
}

void AIAlarm::Save(CStream* stream) {
    stream->Write(&level, 4);
    stream->Write(&pending, 4);
    stream->Write(&active, 4);
    int steps = timer;   // keeps the original's copy of the last one
    stream->Write(&steps, 4);
}
