// The constructors of the unit AI modes the script's UM* commands create (Guard, Sentry, Follow,
// Suspection, Aggression / Revenge, CorpseWatcher) and of a patrol point's look entry. The command
// allocates the mode (operator new) and constructs it here; the layouts are in unit_ai.h. See
// docs/notes/script.md ("Unit mode constructors").
#include "unit_ai.h"

#include "ei/hook.h"

#include <cstdint>

// 004BA1C0 (RET 0xC)
GuardMode* __fastcall ScriptGuardModeConstruct(GuardMode* mode, void*, const Vector3* point, float radius, float wander) {
    mode->vtable = aimode::kGuardVtable;
    mode->Set(point, radius, wander);
    return mode;
}

// 004BA360 (RET 4)
SentryMode* __fastcall ScriptSentryModeConstruct(SentryMode* mode, void*, const Vector3* point) {
    mode->vtable = aimode::kSentryVtable;
    mode->Set(point);
    return mode;
}

// 004BA450 (RET 4). The leader is referenced.
FollowMode* __fastcall ScriptFollowModeConstruct(FollowMode* mode, void*, CEngineObject* leader) {
    mode->vtable = aimode::kBaseVtable;
    mode->leader = nullptr;
    if (leader) {
        ++leader->refCount_;
        mode->leader = leader;
    }
    mode->vtable = aimode::kFollowVtable;
    return mode;
}

// 004BA610
SuspectionMode* __fastcall ScriptSuspectionModeConstruct(SuspectionMode* mode) {
    mode->vtable = aimode::kSuspectionVtable;
    return mode;
}

// 004BA710: no enemy, the whole unit as the part to hit, enemy not fixed.
AggressionMode* __fastcall ScriptAggressionModeConstruct(AggressionMode* mode) {
    mode->enemy = nullptr;
    mode->chosen = nullptr;
    mode->enemyPart = -1;
    mode->vtable = aimode::kAggressionVtable;
    mode->fixed = 0;
    return mode;
}

// 004BA830 (RET 4; UMRevenge): Aggression with `fixed` from the argument's low byte.
AggressionMode* __fastcall ScriptRevengeModeConstruct(AggressionMode* mode, void*, int fixed) {
    mode->enemy = nullptr;
    mode->chosen = nullptr;
    mode->enemyPart = -1;
    mode->vtable = aimode::kAggressionVtable;
    mode->fixed = static_cast<std::uint8_t>(fixed);
    return mode;
}

// 004BA920
CorpseWatcherMode* __fastcall ScriptCorpseWatcherModeConstruct(CorpseWatcherMode* mode) {
    mode->vtable = aimode::kCorpseWatcherVtable;
    return mode;
}

// 004BA1A0 (RET 0x10; UMPatrolLook's entry): angle, wait, turning time, stop first (low byte). The
// arguments come in a different order from the fields: the second goes to +0x08, the third to +0x04.
AILook* __fastcall ScriptAILookConstruct(AILook* look, void*, float angle, float wait, float time, int stopFirst) {
    look->angle = angle;
    look->time = time;
    look->wait = wait;
    look->stopFirst = static_cast<std::uint8_t>(stopFirst);
    return look;
}

EI_REPLACE(0x004BA1C0, ScriptGuardModeConstruct);
EI_REPLACE(0x004BA360, ScriptSentryModeConstruct);
EI_REPLACE(0x004BA450, ScriptFollowModeConstruct);
EI_REPLACE(0x004BA610, ScriptSuspectionModeConstruct);
EI_REPLACE(0x004BA710, ScriptAggressionModeConstruct);
EI_REPLACE(0x004BA830, ScriptRevengeModeConstruct);
EI_REPLACE(0x004BA920, ScriptCorpseWatcherModeConstruct);
EI_REPLACE(0x004BA1A0, ScriptAILookConstruct);
