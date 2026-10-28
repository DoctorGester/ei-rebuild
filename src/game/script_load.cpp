// Loading the mission script from a savegame (CScript::Load) and the loaders of its parts:
// the particle / FX / lightning lists, command blocks, WorldScript blocks, if-then
// arrays, the four kinds of variables, script instances and runs. The mirror image of script_save.cpp; see
// docs/notes/script.md. Every piece is read through the stream's Read in the save's order and in its pieces.
#include "base_manager.h"
#include "core_streams.h"
#include "script.h"
#include "script_array.h"
#include "world_effect.h"

#include "lib/crt.h"
#include "lib/mfc_array.h"
#include "lib/mfc_string.h"
#include "record_arrays.h"

#include "game_arrays.h"
#include "game_lists.h"
#include "game_data.h"
#include "game_singletons.h"
#include "game_slots.h"
#include "world.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

// The id registry (the caller raises the reference count).
CBaseManager* Registry() { return TheServerWorld()->manager48_; }

} // namespace

// ---- Inlined template code -------------------------------------------------------------------------

namespace scriptarray {

namespace {

} // namespace

const ElementKind kFloats{ConstructAs<mfc::impl::Plain4>, nullptr, nullptr};
const ElementKind kRefs{ConstructAs<mfc::impl::Plain4>, DestroyEach<ScriptObjectRef, &ScriptObjectRef::DestroyElement>, DestructAs<gamearrays::ObjectRefs>};
const ElementKind kGroups{ConstructAs<mfc::impl::ScriptGroups>, DestroyEach<ScriptGroup, &ScriptGroup::DestroyElement>, DestructAs<gamearrays::Groups>};
const ElementKind kCmds{ConstructAs<recordarrays::Cmds>, DestroyEach<SSCCmd, &SSCCmd::DestroyElement>, DestroyEach<SSCCmd, &SSCCmd::DestroyElement>};
const ElementKind kIfThen{ConstructAs<mfc::impl::Plain8>, nullptr, nullptr};
const ElementKind kPointers{ConstructAs<mfc::impl::Plain4>, nullptr, nullptr};

} // namespace scriptarray

namespace {

using namespace scriptarray;

// CList::AddTail as the loaders inline it: the new node's element is filled by `fill` before it is linked.
template <typename Abi, typename T, typename E, typename Fill>
void AppendNode(mfc::CList<T, E>* list, Fill fill) {
    auto* node = mfc::impl::NewNodeAs<Abi>(list, list->tail, nullptr);
    fill(&node->data);
    if (list->tail) list->tail->next = node;
    else list->head = node;
    list->tail = node;
}

// ---- Small helpers -------------------------------------------------------------------------------

int ReadInt(ScriptStream* stream) {
    int value = 0;
    stream->Read(&value, 4);
    return value;
}

// Areas are saved from 0x57 on.
int StreamVersion(const ScriptStream* stream) { return static_cast<int>(stream->version_); }


// -1 is none, else the registry's object with its reference count raised.
template <class T>
T* AcquireById(int id) {
    if (id == -1) return nullptr;
    auto* object = Registry()->FindObjectAs<T>(id);
    if (object) ++object->refCount_;
    return object;
}

// The world's first script (the only script a savegame holds): block
// ids resolve in its lists whatever script is loaded.
const CScript* SavedScript() { return TheServerWorld()->MainScript(); }

ScriptDecl* FindBlock(int id) { return SavedScript()->FindBlock(id); }

void LoadVariables(ScriptVars* vars, ScriptStream* stream) {
    vars->floats.Load(stream);
    vars->strings.Load(stream);
    vars->objects.Load(stream);
    vars->groups.Load(stream);
}

} // namespace


template <> void ScriptNamedValues<float>::Load(ScriptStream* stream) {
    int count = ReadInt(stream);
    InlineSetSize(&values, count, kFloats);
    names.SetSize(count, -1);
    for (int i = 0; i < values.size; ++i) {
        stream->Read(&values.data[i], 4);
        ScriptStringLoad(&names.data[i], stream);
    }
}

template <> void ScriptNamedValues<std::string>::Load(ScriptStream* stream) {
    int count = ReadInt(stream);
    values.SetSize(count, -1);
    names.SetSize(count, -1);
    for (int i = 0; i < values.size; ++i) {
        ScriptStringLoad(&values.data[i], stream);
        ScriptStringLoad(&names.data[i], stream);
    }
}

// An object variable: the world's object with the saved id (FindObject), -1 = none. The old
// reference is released after the lookup.
template <> void ScriptNamedValues<ScriptObjectRef>::Load(ScriptStream* stream) {
    int count = ReadInt(stream);
    InlineSetSize(&values, count, kRefs);
    names.SetSize(count, -1);
    for (int i = 0; i < values.size; ++i) {
        ScriptObjectRef* ref = &values.data[i];
        int id = 0;
        stream->Read(&id, 4);
        if (id == -1) {
            ref->Reset();
            ref->object = nullptr;
        } else {
            CEngineObject* object = TheServerWorld()->FindObject(id);
            ref->Reset();
            if (object) ++object->refCount_;
            ref->object = object;
        }
        ScriptStringLoad(&names.data[i], stream);
    }
}

// A group variable: its size, the member ids (each assigned in place, missing objects stay null),
// then sorted.
template <> void ScriptNamedValues<ScriptGroup>::Load(ScriptStream* stream) {
    int count = ReadInt(stream);
    InlineSetSize(&values, count, kGroups);
    names.SetSize(count, -1);
    for (int i = 0; i < values.size; ++i) {
        ScriptGroup* group = &values.data[i];
        int size = 0;
        stream->Read(&size, 4);
        group->SetSize(size, -1);
        for (int k = 0; k < size; ++k) {
            int id = 0;
            stream->Read(&id, 4);
            CEngineObject* object =
                id == -1 ? nullptr : TheServerWorld()->FindObject(id);
            group->data[k].Assign(object);
        }
        group->Sort();
        ScriptStringLoad(&names.data[i], stream);
    }
}

// ---- SSCCmd::Load -----------------------------------------------------------------------------------

// The mirror of SSCCmd::Save. Object and group arguments are saved as counts only: the
// slots are made and left null (only the object array's SetSize is inlined, not the group
// array's). The called block is found in the lists of the world's first script (-1, an unset block,
// finds a WorldScript block with id -1 if any).
void SSCCmd::Load(ScriptStream* stream) {
    stream->Read(&id, 4);

    floatArgs.SetSize(ReadInt(stream), -1);
    for (int i = 0; i < floatArgs.size; ++i) stream->Read(&floatArgs.data[i], 4);

    stringArgs.SetSize(ReadInt(stream), -1);
    for (int i = 0; i < stringArgs.size; ++i) ScriptStringLoad(&stringArgs.data[i], stream);

    int objects = ReadInt(stream);
    InlineSetSize(&objectArgs, objects, kPointers);
    for (int i = 0; i < objects; ++i) objectArgs.data[i] = nullptr;

    int groups = ReadInt(stream);
    groupArgs.SetSize(groups, -1);
    for (int i = 0; i < groups; ++i) groupArgs.data[i] = nullptr;

    localBindings.SetSize(ReadInt(stream), -1);
    for (int i = 0; i < localBindings.size; ++i) stream->Read(&localBindings.data[i], 12);
    globalBindings.SetSize(ReadInt(stream), -1);
    for (int i = 0; i < globalBindings.size; ++i) stream->Read(&globalBindings.data[i], 12);

    mfc::impl::ArrayAs<recordarrays::SubCalls>(subCalls).SetSize(ReadInt(stream), -1);
    for (int i = 0; i < subCalls.size; ++i) subCalls.data[i].Load(stream);

    calledScript = SavedScript()->FindBlock(ReadInt(stream));
}

void ScriptSubCall::Load(ScriptStream* stream) {
    stream->Read(&type, 4);
    stream->Read(&argIndex, 4);
    cmd->Load(stream);
}

void __fastcall ScriptStringLoad(std::string* out, ScriptStream* stream) {
    mfc::CString temporary(mfc::CString::kUnset);
    *out = stream->ReadString(&temporary)->GetString();
}

// ---- Command blocks and if-then pairs --------------------------------------------------------------

// The commands of a block (its id is read by the caller): count, then each SSCCmd.
void __fastcall ScriptDeclCommandsLoad(ScriptDecl* block, ScriptStream* stream) {
    int count = ReadInt(stream);
    InlineSetSize(&block->commands, count, kCmds);
    for (int i = 0; i < block->commands.size; ++i) block->commands.data[i].Load(stream);
}

// A definition's if-then pairs: the ids of the condition and command blocks, resolved in the
// saved script's block lists (FindBlock). The first id defaults to -1 when the read leaves it.
void __fastcall ScriptIfThenLoad(mfc::CArray<ScriptIfThen>* pairs, ScriptStream* stream) {
    int count = ReadInt(stream);
    InlineSetSize(pairs, count, kIfThen);
    for (int i = 0; i < pairs->size; ++i) {
        ScriptIfThen* pair = &pairs->data[i];
        int id = -1;
        stream->Read(&id, 4);
        pair->conditions = FindBlock(id);
        stream->Read(&id, 4);
        pair->commands = FindBlock(id);
    }
}

// One pair of an instance (the instance loader calls it per pair): as ScriptIfThenLoad's loop.
void ScriptIfThen::Load(ScriptStream* stream) {
    int id = -1;
    stream->Read(&id, 4);
    conditions = FindBlock(id);
    stream->Read(&id, 4);   // the same variable: keeps the first id when the read leaves it
    commands = FindBlock(id);
}

// Negative ids are WorldScript blocks.
ScriptDecl* CScript::FindBlock(int id) const {
    const auto& list = id < 0 ? worldBlocks_ : blocks_;
    for (auto* node = list.head; node;) {
        auto* block = &node->data;
        node = node->next;
        if (block->id == id) return block;
    }
    return nullptr;
}

// ---- ScriptVars::Load ------------------------------------------------------------------------------

// The variable loaders' format for all four kinds, with the arrays' out-of-line SetSize,
// objects assigned through the reference's assignment and group members read with
// ScriptObjectRef::Load. One stack variable holds the float count and index and then receives every
// object id and group size (it is never reset in between); the other counts start at 0.
void ScriptVars::Load(ScriptStream* stream) {
    int slot = 0;
    stream->Read(&slot, 4);
    floats.values.SetSize(slot, -1);
    floats.names.SetSize(slot, -1);
    for (slot = 0; slot < floats.values.size; ++slot) {
        stream->Read(&floats.values.data[slot], 4);
        ScriptStringLoad(&floats.names.data[slot], stream);
    }

    int count = 0;
    stream->Read(&count, 4);
    strings.values.SetSize(count, -1);
    strings.names.SetSize(count, -1);
    for (int i = 0; i < strings.values.size; ++i) {
        ScriptStringLoad(&strings.values.data[i], stream);
        ScriptStringLoad(&strings.names.data[i], stream);
    }

    count = 0;
    stream->Read(&count, 4);
    mfc::impl::ArrayAs<gamearrays::ObjectRefs>(objects.values).SetSize(count, -1);
    objects.names.SetSize(count, -1);
    for (int i = 0; i < objects.values.size; ++i) {
        ScriptObjectRef* ref = &objects.values.data[i];
        stream->Read(&slot, 4);
        if (slot == -1) {
            ref->Reset();
            ref->object = nullptr;
        } else {
            ref->Assign(TheServerWorld()->FindObject(slot));
        }
        ScriptStringLoad(&objects.names.data[i], stream);
    }

    count = 0;
    stream->Read(&count, 4);
    mfc::impl::ArrayAs<gamearrays::Groups>(groups.values).SetSize(count, -1);
    groups.names.SetSize(count, -1);
    for (int i = 0; i < groups.values.size; ++i) {
        ScriptGroup* group = &groups.values.data[i];
        stream->Read(&slot, 4);
        group->SetSize(slot, -1);
        for (int k = 0; k < slot; ++k) group->data[k].Load(stream);
        group->Sort();
        ScriptStringLoad(&groups.names.data[i], stream);
    }
}

// Each list loader reads the count, appends that many default nodes, then reads every node of the
// list from the head (a list that was not empty would have its old nodes read again).

// Particle sources: source object, id, flags, moving, steps, step (12 bytes), lifetime.
void __fastcall ScriptParticlesLoad(mfc::CList<ScriptParticleSource>* list, ScriptStream* stream) {
    int count = ReadInt(stream);
    for (int i = 0; i < count; ++i) {
        AppendNode<mfc::impl::ParticleSourceList>(list, [](ScriptParticleSource* source) {
            std::memset(source, 0, sizeof(*source));
        });
    }
    for (auto* node = list->head; node;) {
        ScriptParticleSource* source = &node->data;
        node = node->next;
        source->object = AcquireById<CEffectParticle>(ReadInt(stream));
        stream->Read(&source->id, 4);
        stream->Read(&source->deleteAfterMove, 4);
        stream->Read(&source->moving, 4);
        stream->Read(&source->steps, 4);
        stream->Read(&source->step, 0xC);
        stream->Read(&source->lifetime, 4);
    }
}

// FX sources: each node is filled by the FX source loader.
void __fastcall ScriptFxSourcesLoad(mfc::CList<ScriptFxSource>* list, ScriptStream* stream) {
    int count = ReadInt(stream);
    for (int i = 0; i < count; ++i) {
        // A temporary element (effect 0, empty name, the rest unset) is copied
        // into the node and destroyed (releasing its effect, which is always null).
        std::string emptyName;
        AppendNode<gameslots::FxSourceNodes>(list, [&](ScriptFxSource* source) {
            source->object = nullptr;
            source->id = 0;
            source->name = emptyName;
            std::memset(&source->position, 0, sizeof(*source) - offsetof(ScriptFxSource, position));   // .+0x33
        });
    }
    for (auto* node = list->head; node;) {
        ScriptFxSource* source = &node->data;
        node = node->next;
        source->ScriptFxSourceLoad(stream);
    }
}

// Lightning: object, id.
void __fastcall ScriptLightningLoad(mfc::CList<ScriptLightning>* list, ScriptStream* stream) {
    int count = ReadInt(stream);
    for (int i = 0; i < count; ++i) {
        AppendNode<mfc::impl::LightningList>(list, [](ScriptLightning* bolt) {
            bolt->object = nullptr;
            bolt->id = -1;
        });
    }
    for (auto* node = list->head; node;) {
        ScriptLightning* bolt = &node->data;
        node = node->next;
        bolt->object = AcquireById<CEffectLightning>(ReadInt(stream));
        stream->Read(&bolt->id, 4);
    }
}

// WorldScript blocks: id, count, each SSCCmd.
void __fastcall ScriptWorldBlocksLoad(ScriptDecls* list, ScriptStream* stream) {
    int count = ReadInt(stream);
    for (int i = 0; i < count; ++i) {
        // A temporary empty block (id unset) is copied into the node.
        ScriptDecl temp{};
        temp.commands.vtable = mfc::impl::ScriptCommandsClass::Vtable();
        AppendNode<mfc::impl::CommandBlockList>(list, [&](ScriptDecl* block) {
            block->commands.CopyFrom(temp.commands);
            block->id = temp.id;
        });
        // Its destructor frees nothing (no commands).
    }
    for (auto* node = list->head; node;) {
        ScriptDecl* block = &node->data;
        node = node->next;
        stream->Read(&block->id, 4);
        int size = ReadInt(stream);
        mfc::impl::ArrayAs<recordarrays::Cmds>(block->commands).SetSize(size, -1);
        for (int i = 0; i < block->commands.size; ++i) block->commands.data[i].Load(stream);
    }
}

// Script instances: killed flag, name, parameter types, if-then pairs, variables.
void __fastcall ScriptInstancesLoad(ScriptInstances* list, ScriptStream* stream) {
    int count = ReadInt(stream);
    for (int i = 0; i < count; ++i) {
        // A temporary empty definition is copied into the node, then destroyed (frees nothing).
        struct {
            int index;
            std::string name;
            std::string params;
            mfc::CArray<ScriptIfThen> ifThen;
            ScriptVars vars;
        } temp{};
        temp.ifThen.vtable = mfc::impl::ScriptIfThenPairsClass::Vtable();
        temp.vars.ScriptVarsConstruct();
        AppendNode<gamelists::Definitions>(list, [&](ScriptDefinition* instance) {
            instance->index = temp.index;
            instance->name = temp.name;
            instance->params = temp.params;
            instance->ifThen.Copy(temp.ifThen);
            instance->vars.ScriptVarsAssign(&temp.vars);
        });
        ScriptVarsDestruct(&temp.vars);
    }
    for (auto* node = list->head; node;) {
        ScriptDefinition* instance = &node->data;
        node = node->next;
        stream->Read(&instance->index, 4);
        // Each string is read into a temporary and assigned.
        ScriptStringLoad(&instance->name, stream);
        ScriptStringLoad(&instance->params, stream);
        int pairs = ReadInt(stream);
        instance->ifThen.SetSize(pairs, -1);
        for (int k = 0; k < instance->ifThen.size; ++k) instance->ifThen.data[k].Load(stream);
        LoadVariables(&instance->vars, stream);
    }
}

// Runs: block id, index, ticks, sleep start, the index of its instance in the loading script's instance list
// (-1 or out of range: none), vars.
void __fastcall ScriptRunsLoad(ScriptRuns* list, ScriptStream* stream) {
    int count = ReadInt(stream);
    for (int i = 0; i < count; ++i) {
        // A temporary run (sleep start unset) is copied into the node, then destroyed.
        ScriptRun temp{};
        temp.vars.ScriptVarsConstruct();
        AppendNode<gamelists::Runs>(list, [&](ScriptRun* run) {
            run->block = temp.block;
            run->index = temp.index;
            run->ticks = temp.ticks;
            run->sleepStart = temp.sleepStart;
            run->killFlag = temp.killFlag;
            run->vars.ScriptVarsAssign(&temp.vars);
        });
        ScriptVarsDestruct(&temp.vars);
    }
    for (auto* node = list->head; node;) {
        ScriptRun* run = &node->data;
        node = node->next;
        int id = 0;
        stream->Read(&id, 4);
        run->block = FindBlock(id);
        stream->Read(&run->index, 4);
        stream->Read(&run->ticks, 4);
        stream->Read(&run->sleepStart, 4);
        int instanceIndex = 0;
        stream->Read(&instanceIndex, 4);
        int* killFlag = nullptr;
        int k = 0;
        for (auto* inst = g_streamedScript->instances_.head; inst; ++k) {
            ScriptDefinition* instance = &inst->data;
            inst = inst->next;
            if (k == instanceIndex) {
                killFlag = &instance->index;   // an instance's index is its killed flag
                break;
            }
        }
        run->killFlag = killFlag;
        run->vars.Load(stream);
    }
}

// ---- CScript::Load --------------------------------------------------------------------------------

// Reads what ScriptSave wrote, in the same order. Sets the script pointer the loaders use to the
// script while the runs are read. Areas are read only from stream version 0x57 on.
void CScript::LoadState(ScriptStream* stream) {
    g_streamedScript = this;

    mfc::impl::ArrayAs<gamearrays::PointLights>(pointLights_).SetSize(ReadInt(stream), -1);
    for (int i = 0; i < pointLights_.size; ++i) pointLights_.data[i].ScriptPointLightLoad(stream);

    ScriptParticlesLoad(&particles_, stream);
    ScriptFxSourcesLoad(&fxSources_, stream);
    ScriptLightningLoad(&lightning_, stream);
    stream->Read(&nextParticleId_, 4);
    stream->Read(&nextFxId_, 4);

    // Command blocks: created empty, then each block's id and commands.
    int blocks = ReadInt(stream);
    for (int i = 0; i < blocks; ++i) NewCommandList();
    for (auto* node = blocks_.head; node;) {
        auto* block = &node->data;
        node = node->next;
        stream->Read(&block->id, 4);
        ScriptDeclCommandsLoad(block, stream);
    }
    ScriptWorldBlocksLoad(&worldBlocks_, stream);

    // Definitions.
    auto& scripts = definitions_;
    mfc::impl::ArrayAs<recordarrays::Definitions>(scripts).SetSize(ReadInt(stream), -1);
    for (int i = 0; i < scripts.size; ++i) {
        ScriptDefinition* definition = &scripts.data[i];
        stream->Read(&definition->index, 4);
        ScriptStringLoad(&definition->name, stream);
        ScriptStringLoad(&definition->params, stream);
        ScriptIfThenLoad(&definition->ifThen, stream);
        definition->vars.Load(stream);
    }

    LoadVariables(Globals(), stream);
    ScriptInstancesLoad(&instances_, stream);
    ScriptRunsLoad(&runs_, stream);
    stream->Read(&loaded_, 4);
    stream->Read(&fileCount_, 4);
    g_streamedScript = nullptr;

    if (StreamVersion(stream) < 0x57) return;
    auto& areas = areas_;
    mfc::impl::ArrayAs<mfc::impl::ScriptAreas>(areas).SetSize(ReadInt(stream), -1);
    for (int i = 0; i < areas.size; ++i) {
        ScriptArea* area = &areas.data[i];
        stream->Read(&area->id, 4);
        area->parts.SetSize(ReadInt(stream), -1);
        for (int k = 0; k < area->parts.size; ++k) stream->Read(&area->parts.data[k], sizeof(ScriptAreaPart));
    }
}
