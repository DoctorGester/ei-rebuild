// The script runtime around command execution: starting script instances (004BB6A0), running an
// instance's if-then pairs (004BB520), running a command block (004BC420), freeing evaluation
// temporaries (004BEC80) and the per-tick driver (004BF000). See script.h and docs/notes/script.md.
#include "script.h"

#include "base_manager.h"
#include "random.h"
#include "world.h"
#include "world_effect.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "originals_script.h"
#include "originals_effects.h"

#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // inline assembly: no optimisation across it

namespace {

using RunList = mfc::CList<ScriptRun>;
using InstanceList = mfc::CList<ScriptDefinition>;

// FatalError formats of ScriptBindCall ("Wrong number of script %s float / string / object / group
// parameters").
const char* const kWrongFloatCount = reinterpret_cast<const char*>(0x0078BBA0);
const char* const kWrongStringCount = reinterpret_cast<const char*>(0x0078BB74);
const char* const kWrongObjectCount = reinterpret_cast<const char*>(0x0078BB48);
const char* const kWrongGroupCount = reinterpret_cast<const char*>(0x0078BB1C);

// Class id of the sound object an FX source places (CBaseManager::CreateObject).
constexpr int kClassSound = 0x48;   // CEffectSound

// FCOMP against 0.0 (0073B858): C3, equal or unordered.
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

// ---- Original code ------------------------------------------------------------------------------

__declspec(naked) void __fastcall CalcFloatOrig(SSCCmd*, void*, CScript*, ScriptVars*, float*) EI_ORIGINAL(0x004B41E0)
__declspec(naked) void __fastcall RunCommandsOrig(CScript*, void*, ScriptDecl*, ScriptDefinition*) EI_ORIGINAL(0x004BC420)
__declspec(naked) void __fastcall IfThenRunOrig(const ScriptIfThen*, void*, CScript*, ScriptDefinition*) EI_ORIGINAL(0x004BB520)
__declspec(naked) void __fastcall ReleaseIfDeletedOrig(ScriptObjectRef*) EI_ORIGINAL(0x004B2570)
__declspec(naked) void __fastcall GroupRemoveDeletedOrig(ScriptGroup*) EI_ORIGINAL(0x004B3110)
void __fastcall GroupSetSizeOrig(ScriptGroup* a0, void*, int a2, int a3) { PtrArraySetSize((void*)a0, nullptr, a2, a3); }
inline void __fastcall GroupCopy(ScriptGroup* a0, void*, const ScriptGroup* a2) { GroupAssignOriginal((void*)a0, nullptr, (const void*)a2); }
// CArray<ScriptObjectRef> DestructElements (releases each reference).
void __stdcall DestructRefs(ScriptObjectRef* a0, int a1) { ScriptRefElementsDestruct((void*)a0, a1); }
// The temporaries lists' RemoveTail (values returned through a hidden pointer).
ScriptGroup* TempGroupsRemoveTail(mfc::CList<ScriptGroup>* list, ScriptGroup* out) {
    return static_cast<ScriptGroup*>(TempGroupsRemoveTailOriginal(list, nullptr, out));
}
ScriptObjectRef* TempObjectsRemoveTail(mfc::CList<ScriptObjectRef>* list, ScriptObjectRef* out) {
    return static_cast<ScriptObjectRef*>(TempObjectsRemoveTailOriginal(list, nullptr, out));
}
// ScriptVars constructor / destructor and the value arrays' Copy.
__declspec(naked) void __fastcall VarsConstruct(ScriptVars*) EI_ORIGINAL(0x004C00D0)
__declspec(naked) void __fastcall VarsDestruct(ScriptVars*) EI_ORIGINAL(0x004BE090)
__declspec(naked) void __fastcall FloatsCopy(void*, void*, const void*) EI_ORIGINAL(0x004C1A80)
// ScriptRun's operator=.
__declspec(naked) void __fastcall RunAssign(ScriptRun*, void*, const ScriptRun*) EI_ORIGINAL(0x004CB570)
// FreeNode of the run list and of the instance list (each destroys the element).
__declspec(naked) void __fastcall RunListFreeNode(RunList*, void*, RunList::Node*) EI_ORIGINAL(0x004CDAB0)
__declspec(naked) void __fastcall InstanceListFreeNode(InstanceList*, void*, InstanceList::Node*) EI_ORIGINAL(0x004CD9B0)
// The run list's NewNode(prev, next) (004CDA30): a node from the free list, its ScriptRun
// constructed (ConstructElements 004CECB0).
RunList::Node* RunListNewNode(RunList* list, RunList::Node* prev, RunList::Node* next) {
    return static_cast<RunList::Node*>(ScriptRunListNewNodeOriginal(list, nullptr, prev, next));
}
// ScriptVars::operator= (all eight arrays).
__declspec(naked) void __fastcall VarsAssign(ScriptVars*, void*, const ScriptVars*) EI_ORIGINAL(0x004CBAE0)

// The world's manager of timed objects (spells and effects; CWorld::manager48_, 007AFFA8).
CBaseManager* EffectManager() { return static_cast<CBaseManager*>(WorldServer()->manager48_); }

// 00675CA0: an effect object took a new position.
void EffectMoved(CEngineObject* object) { ObjectMarkNewOriginal(object); }

// FX source position: (r - (-r)) * random * (1 / (2^32 - 1)) + (-r), with the random value loaded
// as a 64-bit integer; for x the source's x is added before rounding, for y the rounded offset is
// added to the source's y afterwards (as the original does).
float FxOffset(float radius, std::uint32_t random) {
    std::uint32_t wide[2] = {random, 0};
    float negative;
    float result;
    __asm {
        fld radius
        fchs
        fstp negative
        fild qword ptr [wide]
        fld radius
        fsub negative
        fmulp st(1), st
        fmul qword ptr ds:[0x0073C468]
        fadd negative
        fstp result
    }
    return result;
}
float FxOffsetPlus(float radius, std::uint32_t random, float origin) {
    std::uint32_t wide[2] = {random, 0};
    float negative;
    float result;
    __asm {
        fld radius
        fchs
        fstp negative
        fild qword ptr [wide]
        fld radius
        fsub negative
        fmulp st(1), st
        fmul qword ptr ds:[0x0073C468]
        fadd negative
        fadd origin
        fstp result
    }
    return result;
}

// The particle source list as the tick's inlined RemoveAt sees it: destroying an element drops its
// effect object (004B1850). Same layout as CScript::particles_.
struct ParticleSourceElements {
    static void Construct(ScriptParticleSource*, int) {}
    static void Destruct(ScriptParticleSource* sources, int count) {
        for (int i = 0; i < count; ++i) ScriptParticleSourceDestruct(&sources[i]);
    }
};
using ParticleSources = mfc::CList<ScriptParticleSource, ParticleSourceElements>;
static_assert(sizeof(ParticleSources::Node) == 0x2C);

void RespawnFxSources(CScript* script) {
    auto* next = script->fxSources_.head;
    while (next) {
        ScriptFxSource& fx = next->data;
        next = next->next;
        if (fx.idle) continue;
        if (--fx.countdown > 0) continue;
        int minDelay = fx.minDelay;
        fx.countdown = static_cast<int>(IsaacRandom::Global().Next() % static_cast<std::uint32_t>(fx.maxDelay - minDelay + 1)) + minDelay;
        if (fx.object) {
            WorldServer()->RemoveTimedObject(fx.object->id_);
            fx.object->Release();
        }
        fx.object = nullptr;
        auto* sound = static_cast<CEffectSound*>(EffectManager()->CreateObject(kClassSound, -1));
        fx.object = sound;
        float yOffset = FxOffset(fx.spread, IsaacRandom::Global().Next());
        float x = FxOffsetPlus(fx.spread, IsaacRandom::Global().Next(), fx.position[0]);
        float y = yOffset + fx.position[1];
        float z = fx.position[2];
        sound->localPosition_.x = x;
        sound->localPosition_.y = y;
        sound->localPosition_.z = z;
        std::memcpy(&sound->volume_, &fx.volume, 4);
        std::memcpy(&sound->radius_, &fx.radius, 4);
        ++sound->refCount_;
        const char* name = fx.name;
        std::memcpy(sound->name_, name, std::strlen(name) + 1);
        WorldServer()->AddTimedObject(sound);
    }
}

void UpdatePointLights(CScript* script) {
    mfc::CArray<ScriptPointLight>& lights = script->pointLights_;
    for (int i = 0; i < lights.size; ++i) {
        ScriptPointLight* light = &lights.data[i];
        if (!light->object || !light->moving) continue;
        int count = light->steps;
        light->steps = count - 1;
        if (count <= 0) {
            light = &lights.data[i];
            light->moving = 0;
            if (!light->removeWhenDone) continue;
            light = &lights.data[i];
            WorldServer()->RemoveTimedObject(light->object->id_);
            lights.data[i].object->Release();
            lights.data[i].object = nullptr;
            continue;
        }
        light = &lights.data[i];
        Vector3& position = static_cast<CEffectPointLight*>(light->object)->localPosition_;
        position.x = position.x + light->step[0];
        position.y = position.y + light->step[1];
        position.z = position.z + light->step[2];
        EffectMoved(lights.data[i].object);
    }
}

void UpdateParticleSources(CScript* script) {
    auto& list = reinterpret_cast<ParticleSources&>(script->particles_);
    auto* next = list.head;
    while (next) {
        ParticleSources::Node* node = next;
        ScriptParticleSource& source = node->data;
        next = next->next;
        int lifetime = source.lifetime;
        bool remove = lifetime == 0;
        if (lifetime > 0) source.lifetime = lifetime - 1;
        if (source.moving) {
            int count = source.steps;
            source.steps = count - 1;
            if (count > 0) {
                Vector3& position = static_cast<CEffectParticle*>(source.object)->localPosition_;
                position.x = source.step[0] + position.x;
                position.y = source.step[1] + position.y;
                position.z = source.step[2] + position.z;
                EffectMoved(source.object);
            } else {
                source.moving = 0;
                if (source.deleteAfterMove) remove = true;
            }
        }
        // RemoveAt (inlined by the original): unlink, drop the effect, free the node; the last
        // node drops every effect and frees all blocks.
        if (remove) list.RemoveAt(node);
    }
}

// CList::RemoveAt's unlink of `node` (the original then frees the node out of line).
template <typename T>
void Unlink(mfc::CList<T>& list, typename mfc::CList<T>::Node* node) {
    if (node == list.head) list.head = node->next;
    else node->prev->next = node->next;
    if (node == list.tail) list.tail = node->prev;
    else node->next->prev = node->prev;
}

} // namespace

// ---- Script instances ---------------------------------------------------------------------------

void __fastcall ScriptBindCall(ScriptDefinition* instance, void*, const SSCCmd* call) {
    ScriptVars& vars = instance->vars;
    if (call->floatArgs.size - 1 != vars.floats.size) {
        game::FatalError(kWrongFloatCount, static_cast<const char*>(instance->name));
        return;
    }
    if (call->stringArgs.size - 1 != vars.strings.size) {
        game::FatalError(kWrongStringCount, static_cast<const char*>(instance->name));
        return;
    }
    if (call->objectArgs.size != vars.objects.size) {
        game::FatalError(kWrongObjectCount, static_cast<const char*>(instance->name));
        return;
    }
    if (call->groupArgs.size != vars.groups.size) {
        game::FatalError(kWrongGroupCount, static_cast<const char*>(instance->name));
        return;
    }
    // Float 0 and string 0 name the script; the rest are the parameters.
    for (int i = 1; i < call->floatArgs.size; ++i) std::memcpy(&vars.floats.data[i - 1], &call->floatArgs.data[i], 4);
    for (int i = 1; i < call->stringArgs.size; ++i) vars.strings.data[i - 1] = call->stringArgs.data[i];
    for (int i = 0; i < call->objectArgs.size; ++i) {
        const ScriptObjectRef* value = call->objectArgs.data[i];
        ScriptObjectRef* slot = &vars.objects.data[i];
        if (slot->object) slot->object->Release();
        slot->object = nullptr;
        slot->object = value->object;
        if (slot->object) ++slot->object->refCount_;
    }
    for (int i = 0; i < call->groupArgs.size; ++i) GroupCopy(&vars.groups.data[i], nullptr, call->groupArgs.data[i]);
}
EI_REPLACE(0x004BB6A0, ScriptBindCall);

void __fastcall ScriptIfThenRun(const ScriptIfThen* pair, void*, CScript* script, ScriptDefinition* instance) {
    for (int i = 0; i < pair->conditions->commands.size; ++i) {
        float result;
        CalcFloatOrig(&pair->conditions->commands.data[i], nullptr, script, &instance->vars, &result);
        FreeTemporariesOrig(script);
        if (ZeroOrNaN(result)) return;
    }
    RunCommandsOrig(script, nullptr, pair->commands, instance);
}
EI_REPLACE(0x004BB520, ScriptIfThenRun);

void __fastcall ScriptRunCommands(CScript* script, void*, ScriptDecl* block, ScriptDefinition* instance) {
    alignas(4) unsigned char storage[sizeof(ScriptRun)];
    auto* run = reinterpret_cast<ScriptRun*>(storage);
    run->block = nullptr;
    run->index = 0;
    run->ticks = 0;
    run->killFlag = nullptr;
    VarsConstruct(&run->vars);
    run->block = block;
    if (instance) {
        ScriptVars& from = instance->vars;
        FloatsCopy(&run->vars.floats, nullptr, &from.floats);
        StringsCopyOriginal(&run->vars.strings, nullptr, &from.strings);
        GroupCopy(reinterpret_cast<ScriptGroup*>(&run->vars.objects), nullptr,
                  reinterpret_cast<const ScriptGroup*>(&from.objects));
        GroupsCopy(&run->vars.groups, nullptr, &from.groups);
    }
    run->index = 0;
    run->ticks = 0;
    run->sleepStart = -1;
    run->killFlag = reinterpret_cast<int*>(instance);
    if (ExecuteOrig(run, nullptr, script)) {
        // Not finished: a copy goes to the head of the run list (AddHead inlined; the original
        // also inlines NewNode, which is 004CDA30's code).
        RunList& runs = script->runs_;
        RunList::Node* node = RunListNewNode(&runs, nullptr, runs.head);
        RunAssign(&node->data, nullptr, run);
        if (runs.head) runs.head->prev = node;
        else runs.tail = node;
        runs.head = node;
    }
    VarsDestruct(&run->vars);
}
EI_REPLACE(0x004BC420, ScriptRunCommands);

void __fastcall ScriptRunWorldScript(CScript* script, void*, ScriptDecl* block) {
    alignas(4) unsigned char storage[sizeof(ScriptRun)];
    auto* run = reinterpret_cast<ScriptRun*>(storage);
    run->block = nullptr;
    run->index = 0;
    run->ticks = 0;
    run->killFlag = nullptr;
    VarsConstruct(&run->vars);
    run->block = block;
    run->index = 0;
    run->ticks = 0;
    run->sleepStart = -1;
    // A new run at the head of the run list, not executed yet.
    RunList& runs = script->runs_;
    RunList::Node* node = RunListNewNode(&runs, nullptr, runs.head);
    ScriptRun* copy = &node->data;
    copy->block = run->block;
    copy->index = run->index;
    copy->ticks = run->ticks;
    copy->sleepStart = run->sleepStart;
    copy->killFlag = run->killFlag;
    VarsAssign(&copy->vars, nullptr, &run->vars);
    if (runs.head) runs.head->prev = node;
    else runs.tail = node;
    runs.head = node;
    VarsDestruct(&run->vars);
}
EI_REPLACE(0x004BCC60, ScriptRunWorldScript);

void __fastcall ScriptFreeTemporaries(CScript* script) {
    if (script->forDepth_ > 0) return;
    // Temporary groups: all but one freed, the last one emptied.
    mfc::CList<ScriptGroup>& groups = script->tempGroups_;
    while (groups.count > 1) {
        ScriptGroup group;
        TempGroupsRemoveTail(&groups, &group);
        group.vtable = scriptvtable::kRefsBase;
        if (group.data) {
            DestructRefs(group.data, group.size);
            gamecrt::operator_delete(group.data);
        }
    }
    GroupSetSizeOrig(&groups.tail->data, nullptr, 0, -1);
    // Temporary object references.
    mfc::CList<ScriptObjectRef>& objects = script->tempObjects_;
    while (objects.count > 1) {
        ScriptObjectRef ref;
        TempObjectsRemoveTail(&objects, &ref);
        if (ref.object) ref.object->Release();
        ref.object = nullptr;
    }
    ScriptObjectRef* last = &objects.tail->data;
    if (last->object) last->object->Release();
    last->object = nullptr;
}
EI_REPLACE(0x004BEC80, ScriptFreeTemporaries);

void __fastcall ScriptTick(CScript* script) {
    if (!script->loaded_) return;

    // Waiting runs: continue each; a run that had already ended is removed.
    RunList& runs = script->runs_;
    RunList::Node* nextRun = runs.head;
    while (nextRun) {
        RunList::Node* node = nextRun;
        nextRun = nextRun->next;
        if (ExecuteOrig(&node->data, nullptr, script)) continue;
        Unlink(runs, node);
        RunListFreeNode(&runs, nullptr, node);
    }

    // Script instances: every if-then pair until the instance is killed; killed instances are
    // removed.
    InstanceList& instances = script->instances_;
    InstanceList::Node* nextInstance = instances.head;
    while (nextInstance) {
        InstanceList::Node* node = nextInstance;
        nextInstance = nextInstance->next;
        ScriptDefinition* instance = &node->data;
        RemoveDeletedOrig(&instance->vars);
        for (int i = 0; i < instance->ifThen.size; ++i) {
            IfThenRunOrig(&instance->ifThen.data[i], nullptr, script, instance);
            if (instance->index != 0) break;
        }
        if (instance->index == 0) continue;
        Unlink(instances, node);
        InstanceListFreeNode(&instances, nullptr, node);
    }

    // Global variables: drop deleted objects.
    ScriptVars* globals = script->Globals();
    for (int i = 0; i < globals->objects.size; ++i) ReleaseIfDeletedOrig(&globals->objects.data[i]);
    for (int i = 0; i < globals->groups.size; ++i) GroupRemoveDeletedOrig(&globals->groups.data[i]);

    RespawnFxSources(script);
    UpdatePointLights(script);
    UpdateParticleSources(script);
    FreeTemporariesOrig(script);
}
EI_REPLACE(0x004BF000, ScriptTick);
