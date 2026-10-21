// 005AEA80 (thiscall on an AI alarm state {level, pending, active, timer}, RET 4): the step of a
// unit's alarm. A pending level above the current one becomes active for 20 steps: the AI log
// (the ostream at 007C2EB8) gets "Unit <id> acknowledges with <what>" and some levels make the
// unit speak (the world's 0058D340 with a remark code). Then, while a level is active and its
// timer runs, every group member (owner + 0xA78) on a side the owner's side is allied with gets
// its controller's alarm (+0x240 -> +0xB10) raised to the level. See docs/notes/ai.md.
#include "ai_alarm.h"
#include "game_strings.h"

#include "combat.h"
#include "object_diplomacy.h"
#include "unit_ai.h"
#include "world.h"

#include "ei/hook.h"

#include <cstdint>
#include "originals_engine.h"
#include "originals_library.h"
#include "lib/crt_iostream.h"
#include "game_data.h"

namespace {

// The game's ostream (MSVC 6 iostream).
struct AiLogStream : libcrt::Ostream {
    AiLogStream* Write(const char* text) { PutText(text); return this; }                       // 006FC31D
    AiLogStream* Write(long value) { PutInt(value); return this; }                             // 006FC9C6
    AiLogStream* Put(int c) { libcrt::Ostream::PutChar(static_cast<char>(c)); return this; }   // 006FC0B0
    AiLogStream* Apply(void* manipulator) {                                                    // 00434FE0
        libcrt::Ostream::Apply(reinterpret_cast<libcrt::Manipulator>(manipulator));
        return this;
    }
};
AiLogStream* AlarmLog() { return ei::GameData<AiLogStream*>(0x007C2EB8); }

// 00455940: the owner's manager context (the world server).
static inline CWorldServer* ManagerContextAt(void* object) { return static_cast<CWorldServer*>(ManagerContextOf(object)); }

} // namespace

static void StepAlarmOf(AIAlarm* state, CUnitServer* owner) {
    CUnitServer* server = owner;
    CWorldServer* world = ManagerContextAt(owner);
    if (state->pending != 0) {
        if (state->pending > state->level) {
            state->active = state->pending;
            state->timer = 20;
            CUnit* unit = server->unit;
            AlarmLog()->Write("Unit ")->Write(static_cast<long>(unit->id_))->Write(" acknowledges with ");
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
            AlarmLog()->Write(what)->Put(10)->Apply(reinterpret_cast<void*>(0x00435000));
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
    for (auto* node = server->Brain()->memory.nearby.head; node; node = node->next) {
        auto* member = static_cast<CUnit*>(node->data);
        if (!(allies & (1u << (member->side & 31)))) continue;
        int& alarm = static_cast<CUnitServer*>(member->controller)->voice.level;
        if (alarm <= active) alarm = active;
    }
}

void AIAlarm::Step(CUnitServer* owner) { StepAlarmOf(this, owner); }
EI_REPLACE_METHOD(0x005AEA80, AIAlarm::Step);

// ---- The state's small members (A-W14) ----------------------------------------------------------

void AIAlarm::Reset() {
    level = 0;
    pending = 0;
    active = 0;
    timer = 0;
}
EI_REPLACE_METHOD(0x005AEA40, AIAlarm::Reset);

AIAlarm* AIAlarm::Construct() {
    Reset();
    return this;
}
EI_REPLACE_METHOD(0x005AEA50, AIAlarm::Construct);

void AIAlarm::Raise(int newLevel) { pending = newLevel > pending ? newLevel : pending; }
EI_REPLACE_METHOD(0x005AEA60, AIAlarm::Raise);

void AIAlarm::Load(CStream* stream) {
    stream->Read(&level, 4);
    stream->Read(&pending, 4);
    stream->Read(&active, 4);
    stream->Read(&timer, 4);
}
EI_REPLACE_METHOD(0x005AEC40, AIAlarm::Load);

void AIAlarm::Save(CStream* stream) {
    stream->Write(&level, 4);
    stream->Write(&pending, 4);
    stream->Write(&active, 4);
    int steps = timer;   // the original writes a copy of the last one
    stream->Write(&steps, 4);
}
EI_REPLACE_METHOD(0x005AEC80, AIAlarm::Save);
