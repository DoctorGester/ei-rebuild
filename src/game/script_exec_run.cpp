// The script runtime around command execution: starting script instances (004BB6A0), running an
// instance's if-then pairs (004BB520), running a command block (004BC420), freeing evaluation
// temporaries (004BEC80) and the per-tick driver (004BF000). See script.h and docs/notes/script.md.
#include "script.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // inline assembly: no optimisation across it

namespace {

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

struct ListNode {
    ListNode* next;
    ListNode* prev;
};
template <typename T>
T* NodeData(ListNode* node) {
    return reinterpret_cast<T*>(node + 1);
}

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

__declspec(naked) BOOL __fastcall ExecuteOrig(ScriptRun*, void*, CScript*) EI_ORIGINAL(0x004B6760)
__declspec(naked) void __fastcall CalcFloatOrig(SSCCmd*, void*, CScript*, ScriptVars*, float*) EI_ORIGINAL(0x004B41E0)
__declspec(naked) void __fastcall FreeTemporariesOrig(CScript*) EI_ORIGINAL(0x004BEC80)
__declspec(naked) void __fastcall RunCommandsOrig(CScript*, void*, ScriptDecl*, ScriptDefinition*) EI_ORIGINAL(0x004BC420)
__declspec(naked) void __fastcall IfThenRunOrig(const ScriptIfThen*, void*, CScript*, ScriptDefinition*) EI_ORIGINAL(0x004BB520)
__declspec(naked) void __fastcall RemoveDeletedOrig(ScriptVars*) EI_ORIGINAL(0x004B3940)
__declspec(naked) void __fastcall ReleaseIfDeletedOrig(ScriptObjectRef*) EI_ORIGINAL(0x004B2570)
__declspec(naked) void __fastcall GroupRemoveDeletedOrig(ScriptGroup*) EI_ORIGINAL(0x004B3110)
__declspec(naked) void __fastcall GroupSetSizeOrig(ScriptGroup*, void*, int, int) EI_ORIGINAL(0x004C2050)
__declspec(naked) void __fastcall GroupCopy(ScriptGroup*, void*, const ScriptGroup*) EI_ORIGINAL(0x004C1290)
// CArray<ScriptObjectRef> DestructElements (releases each reference).
__declspec(naked) void __stdcall DestructRefs(ScriptObjectRef*, int) EI_ORIGINAL(0x004CC7D0)
// The temporaries lists' RemoveTail (values returned through a hidden pointer).
__declspec(naked) ScriptGroup* __fastcall TempGroupsRemoveTail(void*, void*, ScriptGroup*) EI_ORIGINAL(0x004C8AF0)
__declspec(naked) ScriptObjectRef* __fastcall TempObjectsRemoveTail(void*, void*, ScriptObjectRef*) EI_ORIGINAL(0x004C9100)
// ScriptVars constructor / destructor and the value arrays' Copy.
__declspec(naked) void __fastcall VarsConstruct(ScriptVars*) EI_ORIGINAL(0x004C00D0)
__declspec(naked) void __fastcall VarsDestruct(ScriptVars*) EI_ORIGINAL(0x004BE090)
__declspec(naked) void __fastcall FloatsCopy(void*, void*, const void*) EI_ORIGINAL(0x004C1A80)
__declspec(naked) void __fastcall StringsCopy(void*, void*, const void*) EI_ORIGINAL(0x004C14A0)
__declspec(naked) void __fastcall GroupsCopy(void*, void*, const void*) EI_ORIGINAL(0x004C1BE0)
// ScriptRun list elements: ConstructElements(p, n) and operator=; FreeNode of the run list
// (+0x15C) and of the instance list (+0x140) (each destroys the element).
__declspec(naked) void __stdcall RunConstructElements(ScriptRun*, int) EI_ORIGINAL(0x004CECB0)
__declspec(naked) void __fastcall RunAssign(ScriptRun*, void*, const ScriptRun*) EI_ORIGINAL(0x004CB570)
__declspec(naked) void __fastcall RunListFreeNode(void*, void*, ListNode*) EI_ORIGINAL(0x004CDAB0)
__declspec(naked) void __fastcall InstanceListFreeNode(void*, void*, ListNode*) EI_ORIGINAL(0x004CD9B0)

// The world (007AFF60): timed objects (effects that update every tick).
struct World {
    void RemoveTimedObject(int id);       // 0058E5D0
    void AddTimedObject(void* object);    // 0058E520
};
__declspec(naked) void World::RemoveTimedObject(int) EI_ORIGINAL(0x0058E5D0)
__declspec(naked) void World::AddTimedObject(void*) EI_ORIGINAL(0x0058E520)
World* TheWorld() { return reinterpret_cast<World*>(0x007AFF60); }

// 00675CA0: an effect object took a new position.
__declspec(naked) void __fastcall EffectMoved(void*) EI_ORIGINAL(0x00675CA0)

struct Random {
    std::uint32_t Next();                 // 004667F0
};
__declspec(naked) std::uint32_t Random::Next() EI_ORIGINAL(0x004667F0)
Random* TheRandom() { return reinterpret_cast<Random*>(0x007A9D40); }

// Removes a timed effect object from the world and releases it.
void DropEffect(void** slot) {
    auto* object = static_cast<CEngineObject*>(*slot);
    if (object) {
        TheWorld()->RemoveTimedObject(object->id_);
        object->Release();
    }
    *slot = nullptr;
}

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

// An FX source (CScript +0x30 list; offsets from the node).
struct FxSourceNode {
    ListNode link;
    void* object;           // +0x08 the current effect
    std::uint32_t unknown_0C;
    const char* name;       // +0x10 effect name
    float x, y, z;          // +0x14
    std::uint32_t param1;   // +0x20 copied to the effect's +0x24C
    std::uint32_t param2;   // +0x24 copied to +0x250
    int disabled;           // +0x28
    float radius;           // +0x2C
    int minDelay;           // +0x30
    int maxDelay;           // +0x34
    int countdown;          // +0x38
};
static_assert(offsetof(FxSourceNode, countdown) == 0x38);

// A point light (CScript +0x04 array, 0x20 each).
struct PointLight {
    void* object;           // +0x00
    std::uint32_t unknown_04;
    int removeWhenDone;     // +0x08 (MovePointLight) remove the light when the move ends
    int active;             // +0x0C
    int moveCount;          // +0x10
    float dx, dy, dz;       // +0x14
};
static_assert(sizeof(PointLight) == 0x20);

// A particle source (CScript +0x14 list; offsets from the node; one 0x24-byte element).
struct ParticleNode {
    ListNode link;
    void* object;           // +0x08
    std::uint32_t unknown_0C;
    int deleteAfterMove;    // +0x10
    int moving;             // +0x14
    int moveCount;          // +0x18
    float dx, dy, dz;       // +0x1C
    int lifetime;           // +0x28 0: delete now; > 0 counts down
};
static_assert(offsetof(ParticleNode, lifetime) == 0x28);

void RespawnFxSources(CScript* script) {
    ListNode* next = Field<ListNode*>(script, 0x34);
    while (next) {
        auto* fx = reinterpret_cast<FxSourceNode*>(next);
        next = next->next;
        if (fx->disabled) continue;
        if (--fx->countdown > 0) continue;
        int minDelay = fx->minDelay;
        fx->countdown = static_cast<int>(TheRandom()->Next() % static_cast<std::uint32_t>(fx->maxDelay - minDelay + 1)) + minDelay;
        if (fx->object) {
            TheWorld()->RemoveTimedObject(static_cast<CEngineObject*>(fx->object)->id_);
            static_cast<CEngineObject*>(fx->object)->Release();
        }
        fx->object = nullptr;
        void* factory = *reinterpret_cast<void**>(0x007AFFA8);
        void* object = ei::VCall<void*>(factory, 0x5C / 4, 0x48, -1);
        fx->object = object;
        float yOffset = FxOffset(fx->radius, TheRandom()->Next());
        float x = FxOffsetPlus(fx->radius, TheRandom()->Next(), fx->x);
        float y = yOffset + fx->y;
        float z = fx->z;
        Field<float>(object, 0x1C) = x;
        Field<float>(object, 0x20) = y;
        Field<float>(object, 0x24) = z;
        Field<std::uint32_t>(fx->object, 0x24C) = fx->param1;
        Field<std::uint32_t>(fx->object, 0x250) = fx->param2;
        ++Field<int>(fx->object, 4);
        std::memcpy(static_cast<char*>(fx->object) + 0x14C, fx->name, std::strlen(fx->name) + 1);
        TheWorld()->AddTimedObject(fx->object);
    }
}

void UpdatePointLights(CScript* script) {
    for (int i = 0; i < Field<int>(script, 0x08); ++i) {
        PointLight* light = &Field<PointLight*>(script, 0x04)[i];
        if (!light->object || !light->active) continue;
        int count = light->moveCount;
        light->moveCount = count - 1;
        if (count <= 0) {
            light = &Field<PointLight*>(script, 0x04)[i];
            light->active = 0;
            if (!light->removeWhenDone) continue;
            light = &Field<PointLight*>(script, 0x04)[i];
            TheWorld()->RemoveTimedObject(static_cast<CEngineObject*>(light->object)->id_);
            static_cast<CEngineObject*>(Field<PointLight*>(script, 0x04)[i].object)->Release();
            Field<PointLight*>(script, 0x04)[i].object = nullptr;
            continue;
        }
        light = &Field<PointLight*>(script, 0x04)[i];
        float* position = &Field<float>(light->object, 0x1C);
        position[0] = position[0] + light->dx;
        position[1] = position[1] + light->dy;
        position[2] = position[2] + light->dz;
        EffectMoved(Field<PointLight*>(script, 0x04)[i].object);
    }
}

void UpdateParticleSources(CScript* script) {
    ListNode* next = Field<ListNode*>(script, 0x18);
    while (next) {
        auto* source = reinterpret_cast<ParticleNode*>(next);
        next = next->next;
        int lifetime = source->lifetime;
        bool remove = lifetime == 0;
        if (lifetime > 0) source->lifetime = lifetime - 1;
        if (source->moving) {
            int count = source->moveCount;
            source->moveCount = count - 1;
            if (count > 0) {
                float* position = &Field<float>(source->object, 0x1C);
                position[0] = source->dx + position[0];
                position[1] = source->dy + position[1];
                position[2] = source->dz + position[2];
                EffectMoved(source->object);
            } else {
                source->moving = 0;
                if (source->deleteAfterMove) remove = true;
            }
        }
        if (!remove) continue;
        // RemoveAt, inlined: unlink, destroy the element, free the node (the last one frees all).
        ListNode* node = &source->link;
        if (node == Field<ListNode*>(script, 0x18)) Field<ListNode*>(script, 0x18) = node->next;
        else node->prev->next = node->next;
        if (node == Field<ListNode*>(script, 0x1C)) Field<ListNode*>(script, 0x1C) = node->prev;
        else node->next->prev = node->prev;
        DropEffect(&source->object);
        node->next = Field<ListNode*>(script, 0x24);
        Field<ListNode*>(script, 0x24) = node;
        if (--Field<int>(script, 0x20) == 0) {
            for (ListNode* n = Field<ListNode*>(script, 0x18); n; n = n->next)
                DropEffect(&reinterpret_cast<ParticleNode*>(n)->object);
            Field<int>(script, 0x20) = 0;
            Field<ListNode*>(script, 0x24) = nullptr;
            Field<ListNode*>(script, 0x1C) = nullptr;
            Field<ListNode*>(script, 0x18) = nullptr;
            mfc::PlexFreeDataChain(Field<mfc::CPlex*>(script, 0x28));
            Field<mfc::CPlex*>(script, 0x28) = nullptr;
        }
    }
}

// CList unlink of `node` from the list whose head is at `list` + 4 and tail at + 8.
void Unlink(void* list, ListNode* node) {
    ListNode*& head = Field<ListNode*>(list, 4);
    ListNode*& tail = Field<ListNode*>(list, 8);
    if (node == head) head = node->next;
    else node->prev->next = node->next;
    if (node == tail) tail = node->prev;
    else node->next->prev = node->prev;
}

} // namespace

// ---- Script instances ---------------------------------------------------------------------------

void __fastcall ScriptBindCall(ScriptDefinition* instance, void*, const SSCCmd* call) {
    ScriptVars& vars = instance->vars;
    if (call->floatArgs.size - 1 != vars.floats.size) {
        game::FatalError(reinterpret_cast<const char*>(0x0078BBA0), static_cast<const char*>(instance->name));
        return;
    }
    if (call->stringArgs.size - 1 != vars.strings.size) {
        game::FatalError(reinterpret_cast<const char*>(0x0078BB74), static_cast<const char*>(instance->name));
        return;
    }
    if (call->objectArgs.size != vars.objects.size) {
        game::FatalError(reinterpret_cast<const char*>(0x0078BB48), static_cast<const char*>(instance->name));
        return;
    }
    if (call->groupArgs.size != vars.groups.size) {
        game::FatalError(reinterpret_cast<const char*>(0x0078BB1C), static_cast<const char*>(instance->name));
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
        StringsCopy(&run->vars.strings, nullptr, &from.strings);
        GroupCopy(reinterpret_cast<ScriptGroup*>(&run->vars.objects), nullptr,
                  reinterpret_cast<const ScriptGroup*>(&from.objects));
        GroupsCopy(&run->vars.groups, nullptr, &from.groups);
    }
    run->index = 0;
    run->ticks = 0;
    run->sleepStart = -1;
    run->killFlag = reinterpret_cast<int*>(instance);
    if (ExecuteOrig(run, nullptr, script)) {
        // Not finished: a copy goes to the head of the run list (CList<ScriptRun> at +0x15C:
        // head +0x160, tail +0x164, count +0x168, free +0x16C, blocks +0x170, block size +0x174).
        ListNode* head = Field<ListNode*>(script, 0x160);
        if (!Field<ListNode*>(script, 0x16C)) {
            int blockSize = Field<int>(script, 0x174);
            mfc::CPlex* plex = mfc::PlexCreate(Field<mfc::CPlex*>(script, 0x170), blockSize, 0xBC);
            auto* node = reinterpret_cast<ListNode*>(reinterpret_cast<char*>(plex + 1) + (blockSize - 1) * 0xBC);
            for (int i = blockSize - 1; i >= 0; --i) {
                node->next = Field<ListNode*>(script, 0x16C);
                Field<ListNode*>(script, 0x16C) = node;
                node = reinterpret_cast<ListNode*>(reinterpret_cast<char*>(node) - 0xBC);
            }
        }
        ListNode* node = Field<ListNode*>(script, 0x16C);
        Field<ListNode*>(script, 0x16C) = node->next;
        node->prev = nullptr;
        node->next = head;
        ++Field<int>(script, 0x168);
        ScriptRun* copy = NodeData<ScriptRun>(node);
        RunConstructElements(copy, 1);
        RunAssign(copy, nullptr, run);
        if (Field<ListNode*>(script, 0x160)) Field<ListNode*>(script, 0x160)->prev = node;
        else Field<ListNode*>(script, 0x164) = node;
        Field<ListNode*>(script, 0x160) = node;
    }
    VarsDestruct(&run->vars);
}
EI_REPLACE(0x004BC420, ScriptRunCommands);

void __fastcall ScriptFreeTemporaries(CScript* script) {
    if (Field<int>(script, 0x1E8) > 0) return;
    // Temporary groups (CList at +0x1B0: tail +0x1B8, count +0x1BC): all but one freed, the last
    // one emptied.
    while (Field<int>(script, 0x1BC) > 1) {
        ScriptGroup group;
        TempGroupsRemoveTail(script->Field(0x1B0), nullptr, &group);
        group.vtable = reinterpret_cast<void*>(0x0073D11C);
        if (group.data) {
            DestructRefs(group.data, group.size);
            gamecrt::operator_delete(group.data);
        }
    }
    GroupSetSizeOrig(NodeData<ScriptGroup>(Field<ListNode*>(script, 0x1B8)), nullptr, 0, -1);
    // Temporary object references (CList at +0x1CC: tail +0x1D4, count +0x1D8).
    while (Field<int>(script, 0x1D8) > 1) {
        ScriptObjectRef ref;
        TempObjectsRemoveTail(script->Field(0x1CC), nullptr, &ref);
        if (ref.object) ref.object->Release();
        ref.object = nullptr;
    }
    ScriptObjectRef* last = NodeData<ScriptObjectRef>(Field<ListNode*>(script, 0x1D4));
    if (last->object) last->object->Release();
    last->object = nullptr;
}
EI_REPLACE(0x004BEC80, ScriptFreeTemporaries);

void __fastcall ScriptTick(CScript* script) {
    if (!script->LoadedFlag()) return;

    // Waiting runs (+0x15C): continue each; a run that had already ended is removed.
    ListNode* next = Field<ListNode*>(script, 0x160);
    while (next) {
        ListNode* node = next;
        next = next->next;
        if (ExecuteOrig(NodeData<ScriptRun>(node), nullptr, script)) continue;
        Unlink(script->Field(0x15C), node);
        RunListFreeNode(script->Field(0x15C), nullptr, node);
    }

    // Script instances (+0x140): every if-then pair until the instance is killed; killed
    // instances are removed.
    next = Field<ListNode*>(script, 0x144);
    while (next) {
        ListNode* node = next;
        next = next->next;
        auto* instance = NodeData<ScriptDefinition>(node);
        RemoveDeletedOrig(&instance->vars);
        for (int i = 0; i < instance->ifThen.size; ++i) {
            IfThenRunOrig(&instance->ifThen.data[i], nullptr, script, instance);
            if (instance->index != 0) break;
        }
        if (instance->index == 0) continue;
        Unlink(script->Field(0x140), node);
        InstanceListFreeNode(script->Field(0x140), nullptr, node);
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
