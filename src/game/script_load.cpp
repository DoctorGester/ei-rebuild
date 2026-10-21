// Loading the mission script from a savegame (CScript::Load 004C0D00) and the loaders of its parts
// in this range: the particle / FX / lightning lists, command blocks, WorldScript blocks, if-then
// arrays, the four kinds of variables, script instances and runs. The mirror image of
// script_save.cpp; see script.h and docs/notes/script.md.
//
// Every piece is read through the stream's Read (vtable slot 1) in the original's order and in the
// original's pieces. The original inlines CArray::SetSize(n, -1) and CList::AddTail into most
// loaders; both are written out here (InlineSetSize, AppendNode) with the instantiation's own
// element helpers, which are called through their addresses (MFC template code, config/library.csv).
#include "base_manager.h"
#include "core_streams.h"
#include "script.h"
#include "script_array.h"

#include "lib/crt.h"
#include "lib/mfc_array.h"
#include "lib/mfc_string.h"
#include "record_arrays.h"

#include "ei/hook.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_script.h"
#include "originals_world.h"
#include "game_arrays.h"
#include "game_lists.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

// ---- Original code called by the loaders ---------------------------------------------------------

// Loaders of the 004B range, __thiscall with the stream: 004B18B0 point light (0x20 slot) and
// 004B1B40 FX source (list node data) are script_effects.cpp's; 004B3AD0 ScriptVars, 004B5FE0 SSCCmd
// and 004BB590 one if-then pair of an instance are rewritten below. 00461190 (CStream::ReadString):
// bytes up to a zero (or a failed read) into a CString constructed at `out`; returns out.

// Object lookups: 00403F40 the id registry at *007AFFA8 (refcount raised by the caller),
// 00593AA0 the world's FindObject (007AFF60).
CEngineObject* RegistryObject(int id) { return static_cast<CBaseManager*>(ei::GameData<void*>(0x007AFFA8))->FindObject(id); }

// CArray template members (out of line), __thiscall (size, growBy).
// Copies (CArray::Copy / ScriptVars assignment), __thiscall (source).
// ScriptVars constructor / destructor body.

// ConstructElements / DestructElements instantiations (__stdcall (first, count)) and element
// destructors (__thiscall (flags); 0 = destroy in place).
void __stdcall ConstructStrings(void* a0, int a1) { mfc::impl::ConstructStrings(reinterpret_cast<mfc::impl::CString*>(a0), a1); }
void __stdcall DestructStrings(void* a0, int a1) { mfc::impl::DestructStrings(reinterpret_cast<mfc::impl::CString*>(a0), a1); }
void __stdcall DestructRefs(void* a0, int a1) { gamearrays::ObjectRefs::DestructElements(static_cast<ScriptObjectRef*>(a0), a1); }
void __stdcall DestructGroups(void* a0, int a1) { gamearrays::Groups::DestructElements(static_cast<ScriptGroup*>(a0), a1); }

// SSCCmd's argument arrays (out of line), __thiscall (size, growBy): floats, group pointers,
// bindings (both arrays), nested calls (a new SSCCmd per element).

} // namespace

// ---- Inlined template code (script_array.h) ------------------------------------------------------

namespace scriptarray {

namespace {
template <typename T, auto Destroy, int kSize>
void DestroyEach(char* first, int count) {
    for (int i = 0; i < count; ++i, first += kSize) (reinterpret_cast<T*>(first)->*Destroy)(0);
}
template <RangeFn Destruct>
void DestroyRange(char* first, int count) { Destruct(first, count); }

} // namespace

const ElementKind kFloats{4, ConstructAs<mfc::impl::Plain4>, nullptr, nullptr};
const ElementKind kStrings{4, ConstructStrings, DestroyRange<DestructStrings>, DestroyRange<DestructStrings>};
const ElementKind kRefs{4, ConstructAs<mfc::impl::Plain4>, DestroyEach<ScriptObjectRef, &ScriptObjectRef::DestroyElement, 4>, DestroyRange<DestructRefs>};
const ElementKind kGroups{0x14, ConstructAs<mfc::impl::ScriptGroups>, DestroyEach<ScriptGroup, &ScriptGroup::DestroyElement, 0x14>, DestroyRange<DestructGroups>};
const ElementKind kCmds{0x94, ConstructAs<recordarrays::Cmds>, DestroyEach<SSCCmd, &SSCCmd::DestroyElement, 0x94>, DestroyEach<SSCCmd, &SSCCmd::DestroyElement, 0x94>};
const ElementKind kIfThen{8, ConstructAs<mfc::impl::Plain8>, nullptr, nullptr};
const ElementKind kPointers{4, ConstructAs<mfc::impl::Plain4>, nullptr, nullptr};

// CArray<T>::SetSize(n, -1) as the loaders inline it.
void InlineSetSize(RawArray* a, int n, const ElementKind& kind) {
    const unsigned elem = static_cast<unsigned>(kind.size);
    if (n == 0) {
        if (a->data) {
            if (kind.destroyAll) kind.destroyAll(a->data, a->size);
            gamecrt::operator_delete(a->data);
            a->data = nullptr;
        }
        a->maxSize = 0;
        a->size = 0;
        return;
    }
    if (!a->data) {
        a->data = static_cast<char*>(gamecrt::operator_new(static_cast<unsigned>(n) * elem));
        kind.construct(a->data, n);
        a->maxSize = n;
        a->size = n;
        return;
    }
    if (n <= a->maxSize) {
        if (a->size < n) {
            kind.construct(a->data + a->size * elem, n - a->size);
        } else if (n < a->size && kind.destroyTail) {
            kind.destroyTail(a->data + n * elem, a->size - n);
        }
        a->size = n;
        return;
    }
    int grow = a->growBy;
    if (grow == 0) {
        grow = a->size / 8;
        if (grow < 4) grow = 4;
        else if (grow > 0x400) grow = 0x400;
    }
    int newMax = a->maxSize + grow;
    if (n >= newMax) newMax = n;
    char* fresh = static_cast<char*>(gamecrt::operator_new(static_cast<unsigned>(newMax) * elem));
    std::memcpy(fresh, a->data, static_cast<unsigned>(a->size) * elem);
    kind.construct(fresh + a->size * elem, n - a->size);
    gamecrt::operator_delete(a->data);
    a->data = fresh;
    a->size = n;
    a->maxSize = newMax;
}

} // namespace scriptarray

namespace {

using namespace scriptarray;

using ListNode = ScriptListNode;
using RawList = ScriptList;

// CList::NewNode instantiations (__thiscall (prev, next); the element is default-constructed).
using NewNodeFn = ListNode*(__fastcall*)(RawList*, void*, ListNode* prev, ListNode* next);
// 004CD540 particle sources, 004CD5B0 FX sources, 004CD620 lightning, 004CD6D0 command blocks,
// 004CD930 instances, 004CDA30 runs.
template <typename Abi>
ListNode* __fastcall NewNode(RawList* list, void*, ListNode* prev, ListNode* next) {
    return mfc::impl::NewNodeAs<Abi>(list, prev, next);
}
constexpr NewNodeFn NewParticleNode = NewNode<mfc::impl::ParticleSourceList>;
constexpr NewNodeFn NewFxNode = NewNode<mfc::impl::FxSourceList>;
constexpr NewNodeFn NewLightningNode = NewNode<mfc::impl::LightningList>;
constexpr NewNodeFn NewBlockNode = NewNode<mfc::impl::CommandBlockList>;
constexpr NewNodeFn NewInstanceNode = NewNode<gamelists::Definitions>;
constexpr NewNodeFn NewRunNode = NewNode<gamelists::Runs>;

// CList::AddTail as inlined: a new node after the tail, its element filled by `fill`, then linked.
template <typename Fill>
void AppendNode(RawList* list, NewNodeFn newNode, Fill fill) {
    ListNode* node = newNode(list, nullptr, list->tail, nullptr);
    fill(node->data.bytes);
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

// The stream's format version (+8); areas are saved from 0x57 on.
int StreamVersion(const ScriptStream* stream) { return reinterpret_cast<const int*>(stream)[2]; }

// The script being loaded (as in Save; the run loader finds a run's instance through it).
CScript*& LoadingScript() { return ei::GameData<CScript*>(0x007AA620); }

// An object id as the list loaders resolve it: -1 is none, else the registry's object with its
// reference count raised.
CEngineObject* AcquireById(int id) {
    if (id == -1) return nullptr;
    CEngineObject* object = RegistryObject(id);
    if (object) ++object->refCount_;
    return object;
}

// The world's first script (007C27D4 = world +0x12874, the only script a savegame holds): block
// ids resolve in its lists whatever script is loaded.
const CScript* SavedScript() { return ei::GamePointer<const CScript>(0x007C27D4); }

// CScript::FindBlock on the saved script, as the loaders inline it.
ScriptDecl* FindBlock(int id) { return SavedScript()->FindBlock(id); }

// The four variable loaders of one ScriptVars (globals, instances).
void LoadVariables(ScriptVars* vars, ScriptStream* stream) {
    NamedValues(&vars->floats)->Load(stream);
    NamedValues(&vars->strings)->Load(stream);
    NamedValues(&vars->objects)->Load(stream);
    NamedValues(&vars->groups)->Load(stream);
}

} // namespace

// ---- Variables -------------------------------------------------------------------------------------

template <> void ScriptNamedValues<float>::Load(ScriptStream* stream) {
    int count = ReadInt(stream);
    InlineSetSize(Raw(&values), count, kFloats);
    names.SetSize(count, -1);
    for (int i = 0; i < values.size; ++i) {
        stream->Read(&values.data[i], 4);
        ScriptStringLoad(&names.data[i], stream);
    }
}

template <> void ScriptNamedValues<mfc::CString>::Load(ScriptStream* stream) {
    int count = ReadInt(stream);
    InlineSetSize(Raw(&values), count, kStrings);
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
    InlineSetSize(Raw(&values), count, kRefs);
    names.SetSize(count, -1);
    for (int i = 0; i < values.size; ++i) {
        ScriptObjectRef* ref = &values.data[i];
        int id = 0;
        stream->Read(&id, 4);
        if (id == -1) {
            ref->Reset();
            ref->object = nullptr;
        } else {
            CEngineObject* object = WorldFindObjectOriginal(TheServerWorld(), nullptr, id);
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
    InlineSetSize(Raw(&values), count, kGroups);
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
                id == -1 ? nullptr : WorldFindObjectOriginal(TheServerWorld(), nullptr, id);
            group->data[k].Assign(object);
        }
        group->Sort();
        ScriptStringLoad(&names.data[i], stream);
    }
}

// ---- SSCCmd::Load -----------------------------------------------------------------------------------

// The mirror of SSCCmd::Save (004B6360). Object and group arguments are saved as counts only: the
// slots are made and left null (the original inlines the object array's SetSize, not the group
// array's). The called block is found with 004BFA50 in the lists of 007C27D4 (-1, an unset block,
// finds a WorldScript block with id -1 if any).
void SSCCmd::Load(ScriptStream* stream) {
    stream->Read(&id, 4);

    floatArgs.SetSize(ReadInt(stream), -1);   // 004C4410
    for (int i = 0; i < floatArgs.size; ++i) stream->Read(&floatArgs.data[i], 4);

    stringArgs.SetSize(ReadInt(stream), -1);
    for (int i = 0; i < stringArgs.size; ++i) ScriptStringLoad(&stringArgs.data[i], stream);

    int objects = ReadInt(stream);
    InlineSetSize(Raw(&objectArgs), objects, kPointers);
    for (int i = 0; i < objects; ++i) objectArgs.data[i] = nullptr;

    int groups = ReadInt(stream);
    groupArgs.SetSize(groups, -1);   // 004C49F0
    for (int i = 0; i < groups; ++i) groupArgs.data[i] = nullptr;

    localBindings.SetSize(ReadInt(stream), -1);   // 004C4CE0
    for (int i = 0; i < localBindings.size; ++i) stream->Read(&localBindings.data[i], 12);
    globalBindings.SetSize(ReadInt(stream), -1);
    for (int i = 0; i < globalBindings.size; ++i) stream->Read(&globalBindings.data[i], 12);

    mfc::impl::ArrayAs<recordarrays::SubCalls>(subCalls).SetSize(ReadInt(stream), -1);
    for (int i = 0; i < subCalls.size; ++i) subCalls.data[i].Load(stream);

    calledScript = SavedScript()->FindBlock(ReadInt(stream));
}
EI_REPLACE_METHOD(0x004B5FE0, SSCCmd::Load);

// One nested call element (SSCCmd::Load calls it per element): type, argument index, then the
// element's command through SSCCmd::Load's address.
void ScriptSubCall::Load(ScriptStream* stream) {
    stream->Read(&type, 4);
    stream->Read(&argIndex, 4);
    cmd->Load(stream);
}

// A string: read into a temporary CString (00461190 constructs it), assigned, the temporary destroyed.
void __fastcall ScriptStringLoad(mfc::CString* out, ScriptStream* stream) {
    alignas(4) unsigned char temporary[sizeof(mfc::CString)];
    *out = *reinterpret_cast<CStream*>(stream)->ReadString(temporary);
    reinterpret_cast<mfc::CString*>(temporary)->~CString();
}
EI_REPLACE(0x004B3D10, ScriptStringLoad);

// ---- Command blocks and if-then pairs --------------------------------------------------------------

// The commands of a block (its id is read by the caller): count, then each SSCCmd.
void __fastcall ScriptDeclCommandsLoad(ScriptDecl* block, ScriptStream* stream) {
    int count = ReadInt(stream);
    InlineSetSize(Raw(&block->commands), count, kCmds);
    for (int i = 0; i < block->commands.size; ++i) block->commands.data[i].Load(stream);
}
EI_REPLACE(0x004C57C0, ScriptDeclCommandsLoad);

// A definition's if-then pairs: the ids of the condition and command blocks, resolved in the
// saved script's block lists (FindBlock). The first id defaults to -1 when the read leaves it.
void __fastcall ScriptIfThenLoad(mfc::CArray<ScriptIfThen>* pairs, ScriptStream* stream) {
    int count = ReadInt(stream);
    InlineSetSize(Raw(pairs), count, kIfThen);
    for (int i = 0; i < pairs->size; ++i) {
        ScriptIfThen* pair = &pairs->data[i];
        int id = -1;
        stream->Read(&id, 4);
        pair->conditions = FindBlock(id);
        stream->Read(&id, 4);
        pair->commands = FindBlock(id);
    }
}
EI_REPLACE(0x004CA000, ScriptIfThenLoad);

// One pair of an instance (the instance loader calls it per pair): as ScriptIfThenLoad's loop.
void ScriptIfThen::Load(ScriptStream* stream) {
    int id = -1;
    stream->Read(&id, 4);
    conditions = FindBlock(id);
    stream->Read(&id, 4);   // the same variable: keeps the first id when the read leaves it
    commands = FindBlock(id);
}

// The block lists are MFC CLists of ScriptDecl; the id is compared as the block's +0x14.
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

// The variable loaders' format for all four kinds, with the arrays' out-of-line SetSize (004C2880,
// 004C2510, 004C2050, 004C3980), objects assigned through 004B24D0 and group members read with
// ScriptObjectRef::Load. One stack variable holds the float count and index and then receives every
// object id and group size (it is never reset in between); the other counts start at 0.
void ScriptVars::Load(ScriptStream* stream) {
    int slot = 0;
    stream->Read(&slot, 4);
    floats.SetSize(slot, -1);   // 004C2880
    floatNames.SetSize(slot, -1);
    for (slot = 0; slot < floats.size; ++slot) {
        stream->Read(&floats.data[slot], 4);
        ScriptStringLoad(&floatNames.data[slot], stream);
    }

    int count = 0;
    stream->Read(&count, 4);
    strings.SetSize(count, -1);
    stringNames.SetSize(count, -1);
    for (int i = 0; i < strings.size; ++i) {
        ScriptStringLoad(&strings.data[i], stream);
        ScriptStringLoad(&stringNames.data[i], stream);
    }

    count = 0;
    stream->Read(&count, 4);
    mfc::impl::ArrayAs<gamearrays::ObjectRefs>(objects).SetSize(count, -1);
    objectNames.SetSize(count, -1);
    for (int i = 0; i < objects.size; ++i) {
        ScriptObjectRef* ref = &objects.data[i];
        stream->Read(&slot, 4);
        if (slot == -1) {
            ref->Reset();
            ref->object = nullptr;
        } else {
            ref->Assign(WorldFindObjectOriginal(TheServerWorld(), nullptr, slot));
        }
        ScriptStringLoad(&objectNames.data[i], stream);
    }

    count = 0;
    stream->Read(&count, 4);
    mfc::impl::ArrayAs<gamearrays::Groups>(groups).SetSize(count, -1);
    groupNames.SetSize(count, -1);
    for (int i = 0; i < groups.size; ++i) {
        ScriptGroup* group = &groups.data[i];
        stream->Read(&slot, 4);
        group->SetSize(slot, -1);
        for (int k = 0; k < slot; ++k) group->data[k].Load(stream);
        group->Sort();
        ScriptStringLoad(&groupNames.data[i], stream);
    }
}
EI_REPLACE_METHOD(0x004B3AD0, ScriptVars::Load);

// ---- Lists ---------------------------------------------------------------------------------------
// Each list loader reads the count, appends that many default nodes, then reads every node of the
// list from the head (a list that was not empty would have its old nodes read again).

// Particle sources (CScript +0x14; element 0x24 bytes at node +8): source object, id, flags,
// moving, steps, step (12 bytes), lifetime.
void __fastcall ScriptParticlesLoad(RawList* list, ScriptStream* stream) {
    int count = ReadInt(stream);
    for (int i = 0; i < count; ++i) {
        AppendNode(list, NewParticleNode, [](char* data) {
            std::memset(data, 0, 0x24);   // the original copies a stack temporary: +0 = 0, the rest unset
        });
    }
    for (ListNode* node = list->head; node;) {
        auto* source = reinterpret_cast<ScriptParticleSource*>(node->data.bytes);
        node = node->next;
        source->object = AcquireById(ReadInt(stream));
        stream->Read(&source->id, 4);
        stream->Read(&source->deleteAfterMove, 4);
        stream->Read(&source->moving, 4);
        stream->Read(&source->steps, 4);
        stream->Read(&source->step, 0xC);
        stream->Read(&source->lifetime, 4);
    }
}
EI_REPLACE(0x004CA480, ScriptParticlesLoad);

// FX sources (CScript +0x30; element 0x34 bytes): each node is filled by 004B1B40.
void __fastcall ScriptFxSourcesLoad(RawList* list, ScriptStream* stream) {
    int count = ReadInt(stream);
    for (int i = 0; i < count; ++i) {
        // The original builds a temporary element (effect 0, empty name, the rest unset), copies it
        // into the node and destroys it (releasing its effect, which is always null).
        mfc::CString emptyName;
        AppendNode(list, NewFxNode, [&](char* data) {
            auto* source = reinterpret_cast<ScriptFxSource*>(data);
            source->object = nullptr;
            source->id = 0;
            source->name = emptyName;
            std::memset(data + 0x0C, 0, 0x28);
        });
    }
    for (ListNode* node = list->head; node;) {
        char* data = node->data.bytes;
        node = node->next;
        reinterpret_cast<ScriptFxSource*>(data)->ScriptFxSourceLoad(stream);
    }
}
EI_REPLACE(0x004CA5D0, ScriptFxSourcesLoad);

// Lightning (CScript +0x4C; element: object, id).
void __fastcall ScriptLightningLoad(RawList* list, ScriptStream* stream) {
    int count = ReadInt(stream);
    for (int i = 0; i < count; ++i) {
        AppendNode(list, NewLightningNode, [](char* data) {
            auto* bolt = reinterpret_cast<ScriptLightning*>(data);
            bolt->object = nullptr;
            bolt->id = -1;
        });
    }
    for (ListNode* node = list->head; node;) {
        auto* bolt = reinterpret_cast<ScriptLightning*>(node->data.bytes);
        node = node->next;
        bolt->object = AcquireById(ReadInt(stream));
        stream->Read(&bolt->id, 4);
    }
}
EI_REPLACE(0x004CA720, ScriptLightningLoad);

// WorldScript blocks (CScript +0x178; element ScriptDecl): id, count, each SSCCmd.
void __fastcall ScriptWorldBlocksLoad(RawList* list, ScriptStream* stream) {
    int count = ReadInt(stream);
    for (int i = 0; i < count; ++i) {
        // A temporary empty block (vtable 0073D3DC, id unset) is copied into the node.
        ScriptDecl temp{};
        temp.commands.vtable = ei::GamePointer<void>(0x0073D3DC);
        AppendNode(list, NewBlockNode, [&](char* data) {
            auto* block = reinterpret_cast<ScriptDecl*>(data);
            block->commands.CopyFrom(temp.commands);
            block->id = temp.id;
        });
        // Its destructor frees nothing (no commands).
    }
    for (ListNode* node = list->head; node;) {
        auto* block = reinterpret_cast<ScriptDecl*>(node->data.bytes);
        node = node->next;
        stream->Read(&block->id, 4);
        int size = ReadInt(stream);
        mfc::impl::ArrayAs<recordarrays::Cmds>(block->commands).SetSize(size, -1);
        for (int i = 0; i < block->commands.size; ++i) block->commands.data[i].Load(stream);
    }
}
EI_REPLACE(0x004CA810, ScriptWorldBlocksLoad);

// Script instances (CScript +0x140; element ScriptDefinition): killed flag, name, parameter
// types, if-then pairs (004BB590 each), variables.
void __fastcall ScriptInstancesLoad(RawList* list, ScriptStream* stream) {
    int count = ReadInt(stream);
    for (int i = 0; i < count; ++i) {
        // A temporary empty definition is copied into the node, then destroyed (frees nothing).
        struct {
            int index;
            mfc::CString name;
            mfc::CString params;
            mfc::CArray<ScriptIfThen> ifThen;
            ScriptVars vars;
        } temp{};
        temp.ifThen.vtable = ei::GamePointer<void>(0x0073D430);
        temp.vars.ScriptVarsConstruct();
        AppendNode(list, NewInstanceNode, [&](char* data) {
            auto* instance = reinterpret_cast<ScriptDefinition*>(data);
            instance->index = temp.index;
            instance->name = temp.name;
            instance->params = temp.params;
            instance->ifThen.Copy(temp.ifThen);   // 004CC4A0
            instance->vars.ScriptVarsAssign(&temp.vars);
        });
        ScriptVarsDestruct(&temp.vars);
    }
    for (ListNode* node = list->head; node;) {
        auto* instance = reinterpret_cast<ScriptDefinition*>(node->data.bytes);
        node = node->next;
        stream->Read(&instance->index, 4);
        // The original reads each string (00461190) into a temporary and assigns it: 004B3D10.
        ScriptStringLoad(&instance->name, stream);
        ScriptStringLoad(&instance->params, stream);
        int pairs = ReadInt(stream);
        instance->ifThen.SetSize(pairs, -1);   // 004C59C0
        for (int k = 0; k < instance->ifThen.size; ++k) instance->ifThen.data[k].Load(stream);
        LoadVariables(&instance->vars, stream);
    }
}
EI_REPLACE(0x004CA980, ScriptInstancesLoad);

// Runs (CScript +0x15C; element ScriptRun): block id (FindBlock), index, ticks, sleep start, the
// index of its instance in the loading script's instance list (-1 or out of range: none), vars.
void __fastcall ScriptRunsLoad(RawList* list, ScriptStream* stream) {
    int count = ReadInt(stream);
    for (int i = 0; i < count; ++i) {
        // A temporary run (sleep start unset) is copied into the node, then destroyed.
        ScriptRun temp{};
        temp.vars.ScriptVarsConstruct();
        AppendNode(list, NewRunNode, [&](char* data) {
            auto* run = reinterpret_cast<ScriptRun*>(data);
            run->block = temp.block;
            run->index = temp.index;
            run->ticks = temp.ticks;
            run->sleepStart = temp.sleepStart;
            run->killFlag = temp.killFlag;
            run->vars.ScriptVarsAssign(&temp.vars);
        });
        ScriptVarsDestruct(&temp.vars);
    }
    for (ListNode* node = list->head; node;) {
        auto* run = reinterpret_cast<ScriptRun*>(node->data.bytes);
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
        for (auto* inst = LoadingScript()->instances_.head; inst; ++k) {
            char* data = reinterpret_cast<char*>(&inst->data);
            inst = inst->next;
            if (k == instanceIndex) {
                killFlag = reinterpret_cast<int*>(data);
                break;
            }
        }
        run->killFlag = killFlag;
        run->vars.Load(stream);
    }
}
EI_REPLACE(0x004CAC00, ScriptRunsLoad);

// ---- CScript::Load --------------------------------------------------------------------------------

// Reads what ScriptSave (004C0F90) wrote, in the same order. Sets 007AA620 to the script while
// the runs are read. Areas are read only from stream version 0x57 on.
void CScript::LoadState(ScriptStream* stream) {
    LoadingScript() = this;

    // Point lights (+0x00; 0x20-byte slots).
    mfc::impl::ArrayAs<gamearrays::PointLights>(pointLights_).SetSize(ReadInt(stream), -1);   // 004C5CB0
    for (int i = 0; i < pointLights_.size; ++i) pointLights_.data[i].ScriptPointLightLoad(stream);

    ScriptParticlesLoad(reinterpret_cast<RawList*>(&particles_), stream);
    ScriptFxSourcesLoad(reinterpret_cast<RawList*>(&fxSources_), stream);
    ScriptLightningLoad(reinterpret_cast<RawList*>(&lightning_), stream);
    stream->Read(&nextParticleId_, 4);
    stream->Read(&nextFxId_, 4);

    // Command blocks (+0x70): created empty, then each block's id and commands.
    int blocks = ReadInt(stream);
    for (int i = 0; i < blocks; ++i) NewCommandList();
    for (auto* node = blocks_.head; node;) {
        auto* block = &node->data;
        node = node->next;
        stream->Read(&block->id, 4);
        ScriptDeclCommandsLoad(block, stream);
    }
    ScriptWorldBlocksLoad(reinterpret_cast<RawList*>(&worldBlocks_), stream);

    // Definitions (+0x8C).
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
    ScriptInstancesLoad(reinterpret_cast<RawList*>(&instances_), stream);
    ScriptRunsLoad(reinterpret_cast<RawList*>(&runs_), stream);
    stream->Read(&loaded_, 4);
    stream->Read(&fileCount_, 4);
    LoadingScript() = nullptr;

    if (StreamVersion(stream) < 0x57) return;
    auto& areas = areas_;
    mfc::impl::ArrayAs<mfc::impl::ScriptAreas>(areas).SetSize(ReadInt(stream), -1);
    for (int i = 0; i < areas.size; ++i) {
        ScriptArea* area = &areas.data[i];
        stream->Read(&area->id, 4);
        area->parts.SetSize(ReadInt(stream), -1);   // 004CC640
        for (int k = 0; k < area->parts.size; ++k) stream->Read(&area->parts.data[k], sizeof(ScriptAreaPart));
    }
}
EI_REPLACE_METHOD(0x004C0D00, CScript::LoadState);
