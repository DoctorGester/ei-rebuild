// Consistency checks for script command execution and the script runtime (src/game/script_exec*.cpp).
//
// Every function here changes the world or the script state and cannot run twice, so each runs ours
// only and checks the state afterwards. ScriptRun::Execute additionally installs a command observer
// (g_scriptCommandObserver) that checks each command's effect right after it ran.
#include "verify.h"

#include "../../src/game/script.h"
#include "../../src/lib/crt.h"

#include "ei/x87.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {
void* s_unused;

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

struct ListNode {
    ListNode* next;
    ListNode* prev;
};

__declspec(naked) int __cdecl Ftol(float) {
    __asm {
        fld dword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

// Equal to zero or NaN, as the commands test their flag arguments (FCOM C3).
bool ZeroOrNaN(float value) {
    int status;
    __asm {
        fld value
        fcomp dword ptr ds:[0x0073B858]
        fnstsw ax
        movzx eax, ah
        mov status, eax
    }
    return (status & 0x40) != 0;
}

struct World {
    void* GetPlayer(int index);   // 00594180
};
__declspec(naked) void* World::GetPlayer(int) EI_ORIGINAL(0x00594180)
World* TheWorld() { return reinterpret_cast<World*>(0x007AFF60); }

constexpr int kClassUnit = 0x50;

bool IsWait(int id) { return id == 0x07 || id == 0x09 || id == 0xDE || id == 0xB9; }

bool SameGroup(const ScriptGroup& a, const ScriptGroup& b) {
    if (a.size != b.size) return false;
    for (int i = 0; i < a.size; ++i)
        if (a.data[i].object != b.data[i].object) return false;
    return true;
}

bool Sorted(const ScriptGroup& g) {
    for (int i = 1; i < g.size; ++i)
        if (reinterpret_cast<std::uintptr_t>(g.data[i - 1].object) >= reinterpret_cast<std::uintptr_t>(g.data[i].object))
            return false;
    return true;
}

bool Contains(const ScriptGroup& g, const CEngineObject* object) {
    for (int i = 0; i < g.size; ++i)
        if (g.data[i].object == object) return true;
    return false;
}

// A CList (head at list + 4, tail + 8, count + 0xC) is well formed: links agree, the walk from the
// head ends at the tail after `count` nodes.
bool ListConsistent(void* list) {
    auto* head = Field<ListNode*>(list, 4);
    auto* tail = Field<ListNode*>(list, 8);
    int count = Field<int>(list, 0xC);
    ListNode* prev = nullptr;
    int n = 0;
    for (ListNode* node = head; node; node = node->next) {
        if (node->prev != prev || ++n > count) return false;
        prev = node;
    }
    return n == count && prev == tail;
}

// Outside For loops the temporaries are down to one empty group and one empty reference.
bool TemporariesFreed(CScript* script) {
    if (Field<int>(script, 0x1E8) > 0) return true;
    int groups = Field<int>(script, 0x1BC);
    int objects = Field<int>(script, 0x1D8);
    if (groups > 1 || objects > 1) return false;
    if (groups == 1 && reinterpret_cast<ScriptGroup*>(Field<ListNode*>(script, 0x1B8) + 1)->size != 0) return false;
    if (objects == 1 && reinterpret_cast<ScriptObjectRef*>(Field<ListNode*>(script, 0x1D4) + 1)->object) return false;
    return true;
}

// ---- Per-command checks (ScriptRun::Execute's observer) -----------------------------------------

struct Snapshot {
    SSCCmd* cmd;
    ScriptDecl* block;
    int index;
    int ticks;
    int sleepStart;
    int depth;
    int instances;
};
std::vector<Snapshot> s_snapshots;

void BeforeCommand(ScriptRun* run, CScript* script, SSCCmd* cmd) {
    s_snapshots.push_back({cmd, run->block, run->index, run->ticks, run->sleepStart, Field<int>(script, 0x1E8),
                           Field<int>(script, 0x14C)});
}

// The effect each command must have had. `detail` describes a failure.
bool CommandEffect(const Snapshot& s, ScriptRun* run, CScript* script, SSCCmd* cmd, int cont) {
    const float* f = cmd->floatArgs.data;
    switch (cmd->id) {
    case 0x07: {   // Sleep
        int start = s.sleepStart < 0 ? run->ticks : s.sleepStart;
        bool waits = run->ticks - start < x87::fistp(f[0]);
        return cont == (waits ? 0 : 1) && (waits ? run->sleepStart == start : run->sleepStart == -1);
    }
    case 0x09:
        return cont == (run->ticks < x87::fistp(f[0]) ? 0 : 1);
    case 0xDE:
        return cont == (ZeroOrNaN(f[0]) ? 0 : 1);
    case 0xB9: {
        CEngineObject* unit = cmd->objectArgs.data[0]->object;
        bool busy = unit && unit->GetClassId() == kClassUnit && Field<int>(Field<void*>(unit, 0x240), 0x984) != 0;
        return cont == (busy ? 0 : 1);
    }
    case 0x79:   // KillScript
        return run->killFlag && *run->killFlag == 1;
    case 0x08: {   // script call: a new instance at the tail, alive, with the call's floats
        if (Field<int>(script, 0x14C) != s.instances + 1) return false;
        auto* instance = reinterpret_cast<ScriptDefinition*>(Field<ListNode*>(script, 0x148) + 1);
        if (instance->index != 0 || instance->vars.floats.size != cmd->floatArgs.size - 1) return false;
        for (int i = 1; i < cmd->floatArgs.size; ++i)
            if (std::memcmp(&instance->vars.floats.data[i - 1], &f[i], 4) != 0) return false;
        return true;
    }
    case kCmdAssign: {
        ScriptVars* globals = script->Globals();
        int index = Ftol(f[0]);
        switch (Ftol(f[1])) {
        case 0: return std::memcmp(&globals->floats.data[index], &f[2], 4) == 0;
        case 1: return std::strcmp(globals->strings.data[index], cmd->stringArgs.data[0]) == 0;
        case 2: return globals->objects.data[index].object == cmd->objectArgs.data[0]->object;
        case 3: return SameGroup(globals->groups.data[index], *cmd->groupArgs.data[0]);
        default: return true;
        }
    }
    case 0x22: {   // AddObject
        CEngineObject* object = cmd->objectArgs.data[0]->object;
        const ScriptGroup& g = *cmd->groupArgs.data[0];
        return Sorted(g) && (!object || Contains(g, object));
    }
    case 0x23: {   // RemoveObject
        CEngineObject* object = cmd->objectArgs.data[0]->object;
        return !object || !Contains(*cmd->groupArgs.data[0], object);
    }
    case 0x32: {   // BlockUnit
        CEngineObject* unit = cmd->objectArgs.data[0]->object;
        if (!unit || unit->GetClassId() != kClassUnit) return true;
        return ((unit->flags_ & 0x800000u) != 0) == !ZeroOrNaN(f[0]);
    }
    case 0x95: {   // ActivateTrap
        CEngineObject* object = cmd->objectArgs.data[0]->object;
        return !object || ((object->flags_ & 0x80000u) != 0) == !ZeroOrNaN(f[0]);
    }
    case 0xC2: {   // HideObject
        CEngineObject* object = cmd->objectArgs.data[0]->object;
        return !object || ((object->flags_ & 0x10000u) != 0) == !ZeroOrNaN(f[0]);
    }
    case 0x36: {   // SetPlayer: the side (+0x250)
        CEngineObject* unit = cmd->objectArgs.data[0]->object;
        if (!unit || unit->GetClassId() != kClassUnit) return true;
        return Field<int>(unit, 0x250) == Ftol(f[0]);
    }
    case 0x38: {   // SetEnemy: the player's bit is in the hostile mask
        CEngineObject* unit = cmd->objectArgs.data[0]->object;
        if (!unit || unit->GetClassId() != kClassUnit) return true;
        return (Field<std::uint32_t>(unit, 0x244) & (1u << (Ftol(f[0]) & 31))) != 0;
    }
    case 0x97: {   // GodMode
        void* player = TheWorld()->GetPlayer(Ftol(f[0]));
        return !player || Field<std::uint8_t>(player, 0x108) == static_cast<std::uint8_t>(Ftol(f[1]));
    }
    default:
        return true;
    }
}

void AfterCommand(ScriptRun* run, CScript* script, SSCCmd* cmd, int cont) {
    Snapshot s = s_snapshots.back();
    s_snapshots.pop_back();
    // A command leaves the run's position alone (For and ForIf restore it), only the waiting
    // commands stop the run, and For loops leave the loop depth as they found it.
    bool ok = s.cmd == cmd && run->block == s.block && run->index == s.index && run->ticks == s.ticks &&
              Field<int>(script, 0x1E8) == s.depth && (cont == 1 || IsWait(cmd->id)) &&
              (cmd->id == 0x07 || run->sleepStart == s.sleepStart);
    ok = ok && CommandEffect(s, run, script, cmd, cont);
    ei::verify::Check("ScriptRun::Execute command", true, ok, "command %X (%s) cont %d", cmd->id,
                      KeywordName(cmd->id), cont);
}

ScriptCommandObserver s_observer = {BeforeCommand, AfterCommand};
struct InstallObserver {
    InstallObserver() { g_scriptCommandObserver = &s_observer; }
} s_installObserver;

} // namespace

// ---- ScriptRun::Execute (004B6760; Rebirth patches it: vanilla only) ----------------------------

// The run is counted; a run already at its end returns 0; otherwise it stops only at a waiting
// command or at the end. Block, kill flag and loop depth are unchanged.
static BOOL __fastcall ExecuteVerify(ScriptRun* self, void*, CScript* script) {
    ScriptDecl* block = self->block;
    int* killFlag = self->killFlag;
    int index = self->index;
    int ticks = self->ticks;
    int size = block->commands.size;
    int depth = Field<int>(script, 0x1E8);
    BOOL result = self->Execute(script);
    bool ok = self->ticks == ticks + 1 && self->block == block && self->killFlag == killFlag &&
              Field<int>(script, 0x1E8) == depth;
    if (index >= size) {
        ok = ok && result == 0 && self->index == index;
    } else {
        ok = ok && result == 1 && self->index >= index && self->index <= size;
        if (ok && self->index < size) ok = IsWait(block->commands.data[self->index].id);
    }
    ei::verify::Check("ScriptRun::Execute", true, ok, "index %d -> %d of %d, result %d", index, self->index, size, result);
    return result;
}
EI_VERIFY(0x004B6760, ExecuteVerify, 0, s_unused);

// ---- Runtime ------------------------------------------------------------------------------------

// The instance's parameters hold the call's arguments (when the counts match; otherwise the
// original reports a fatal error).
static void __fastcall BindCallVerify(ScriptDefinition* self, void*, const SSCCmd* call) {
    ScriptBindCall(self, nullptr, call);
    ScriptVars& vars = self->vars;
    bool counts = call->floatArgs.size - 1 == vars.floats.size && call->stringArgs.size - 1 == vars.strings.size &&
                  call->objectArgs.size == vars.objects.size && call->groupArgs.size == vars.groups.size;
    bool ok = true;
    if (counts) {
        for (int i = 1; i < call->floatArgs.size; ++i)
            ok = ok && std::memcmp(&vars.floats.data[i - 1], &call->floatArgs.data[i], 4) == 0;
        for (int i = 1; i < call->stringArgs.size; ++i)
            ok = ok && std::strcmp(vars.strings.data[i - 1], call->stringArgs.data[i]) == 0;
        for (int i = 0; i < call->objectArgs.size; ++i)
            ok = ok && vars.objects.data[i].object == call->objectArgs.data[i]->object;
        for (int i = 0; i < call->groupArgs.size; ++i)
            ok = ok && SameGroup(vars.groups.data[i], *call->groupArgs.data[i]);
    }
    ei::verify::Check("ScriptBindCall", true, ok, "%s", static_cast<const char*>(self->name));
}
EI_VERIFY(0x004BB6A0, BindCallVerify, 0, s_unused);

// After the conditions (and the commands, if they all held) the temporaries are freed and the run
// list is still well formed.
static void __fastcall IfThenRunVerify(const ScriptIfThen* self, void*, CScript* script, ScriptDefinition* instance) {
    ScriptIfThenRun(self, nullptr, script, instance);
    bool ok = TemporariesFreed(script) && ListConsistent(script->Field(0x15C));
    ei::verify::Check("ScriptIfThenRun", true, ok, "%s", instance ? static_cast<const char*>(instance->name) : "");
}
EI_VERIFY(0x004BB520, IfThenRunVerify, 0, s_unused);

// A run that did not finish is added at the head of the run list, with the block, the instance's
// kill flag and a copy of the instance's variables, after its first Execute.
static void __fastcall RunCommandsVerify(CScript* script, void*, ScriptDecl* block, ScriptDefinition* instance) {
    int runs = Field<int>(script, 0x168);
    ScriptRunCommands(script, nullptr, block, instance);
    int added = Field<int>(script, 0x168) - runs;
    bool ok = (added == 0 || added == 1) && ListConsistent(script->Field(0x15C));
    if (ok && added == 1) {
        auto* run = reinterpret_cast<ScriptRun*>(Field<ListNode*>(script, 0x160) + 1);
        ok = run->block == block && run->killFlag == reinterpret_cast<int*>(instance) && run->ticks == 1 &&
             run->index < block->commands.size && IsWait(block->commands.data[run->index].id);
        if (ok && instance)
            ok = run->vars.floats.size == instance->vars.floats.size && run->vars.strings.size == instance->vars.strings.size &&
                 run->vars.objects.size == instance->vars.objects.size && run->vars.groups.size == instance->vars.groups.size;
    }
    ei::verify::Check("ScriptRunCommands", true, ok, "added %d", added);
}
EI_VERIFY(0x004BC420, RunCommandsVerify, 0, s_unused);

// Outside For loops the temporaries are down to one empty entry each; inside nothing changes.
static void __fastcall FreeTemporariesVerify(CScript* script) {
    int depth = Field<int>(script, 0x1E8);
    int groups = Field<int>(script, 0x1BC);
    int objects = Field<int>(script, 0x1D8);
    ScriptFreeTemporaries(script);
    bool ok = depth > 0 ? Field<int>(script, 0x1BC) == groups && Field<int>(script, 0x1D8) == objects
                        : TemporariesFreed(script);
    ok = ok && ListConsistent(script->Field(0x1B0)) && ListConsistent(script->Field(0x1CC));
    ei::verify::Check("ScriptFreeTemporaries", true, ok, "depth %d, %d groups, %d objects", depth, groups, objects);
}
EI_VERIFY(0x004BEC80, FreeTemporariesVerify, 0, s_unused);

// After a tick: the run, instance and particle lists are well formed, no instance is killed, the
// globals hold no deleted objects and the temporaries are freed.
static void __fastcall TickVerify(CScript* script) {
    ScriptTick(script);
    bool ok = true;
    if (script->LoadedFlag()) {
        ok = ListConsistent(script->Field(0x15C)) && ListConsistent(script->Field(0x140)) &&
             ListConsistent(script->Field(0x14)) && TemporariesFreed(script);
        for (ListNode* node = Field<ListNode*>(script, 0x144); ok && node; node = node->next)
            ok = reinterpret_cast<ScriptDefinition*>(node + 1)->index == 0;
        ScriptVars* globals = script->Globals();
        for (int i = 0; ok && i < globals->objects.size; ++i) {
            CEngineObject* object = globals->objects.data[i].object;
            ok = !object || !(object->flags_ & 1u);
        }
        for (int i = 0; ok && i < globals->groups.size; ++i)
            for (int j = 0; ok && j < globals->groups.data[i].size; ++j) {
                CEngineObject* object = globals->groups.data[i].data[j].object;
                ok = object && !(object->flags_ & 1u);
            }
    }
    ei::verify::Check("ScriptTick", true, ok, "%d runs, %d instances", Field<int>(script, 0x168), Field<int>(script, 0x14C));
}
EI_VERIFY(0x004BF000, TickVerify, 0, s_unused);
