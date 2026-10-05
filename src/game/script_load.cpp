// Loading the mission script from a savegame (CScript::Load 004C0D00) and the loaders of its parts
// in this range: the particle / FX / lightning lists, command blocks, WorldScript blocks, if-then
// arrays, the four kinds of variables, script instances and runs. The mirror image of
// script_save.cpp; see script.h and docs/notes/script.md ("Savegame reader").
//
// Every piece is read through the stream's Read (vtable slot 1) in the original's order and in the
// original's pieces. The original inlines CArray::SetSize(n, -1) and CList::AddTail into most
// loaders; both are written out here (InlineSetSize, AppendNode) with the instantiation's own
// element helpers, which are called through their addresses (MFC template code, config/library.csv).
#include "script.h"
#include "script_array.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_script.h"

#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

// ---- Original code called by the loaders ---------------------------------------------------------

// Loaders of the 004B range, __thiscall with the stream. 004B18B0 point light (0x20 slot) and
// 004B1B40 FX source (list node data): not rewritten. 004B3AD0 ScriptVars, 004B5FE0 SSCCmd and
// 004BB590 one if-then pair of an instance: rewritten below, called through their addresses.
__declspec(naked) void __fastcall PointLightLoad(void*, void*, ScriptStream*) EI_ORIGINAL(0x004B18B0)
__declspec(naked) void __fastcall FxSourceLoad(void*, void*, ScriptStream*) EI_ORIGINAL(0x004B1B40)
__declspec(naked) void __fastcall VarsLoad(ScriptVars*, void*, ScriptStream*) EI_ORIGINAL(0x004B3AD0)
__declspec(naked) void __fastcall CmdLoad(SSCCmd*, void*, ScriptStream*) EI_ORIGINAL(0x004B5FE0)
__declspec(naked) void __fastcall IfThenPairLoad(ScriptIfThen*, void*, ScriptStream*) EI_ORIGINAL(0x004BB590)
// 004B3D10 (__fastcall): reads a string (00461190: bytes up to a zero) and assigns it.
__declspec(naked) void __fastcall StringLoad(mfc::CString*, ScriptStream*) EI_ORIGINAL(0x004B3D10)
// 00461190 (__thiscall on the stream, RET 4; rewritten in core_streams.cpp, called through its
// address): bytes up to a zero (or a failed read) into a CString constructed at `out`; returns out.
__declspec(naked) mfc::CString* __fastcall StreamReadString(ScriptStream*, void*, void* /*out*/) EI_ORIGINAL(0x00461190)
// 004BF540 CScript::NewCommandList: a new empty command block in the +0x70 list.
__declspec(naked) ScriptDecl* __fastcall NewCommandList(CScript*) EI_ORIGINAL(0x004BF540)
// 004BFA50 CScript::FindBlock (rewritten below; SSCCmd::Load calls it out of line).
__declspec(naked) ScriptDecl* __fastcall FindBlockOrig(const CScript*, void*, int) EI_ORIGINAL(0x004BFA50)
// ScriptObjectRef::Load (rewritten in script_group.cpp) and the group values' SetSize.
__declspec(naked) void __fastcall RefLoad(ScriptObjectRef*, void*, ScriptStream*) EI_ORIGINAL(0x004B2590)
__declspec(naked) void __fastcall GroupValuesSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C3980)

// Object lookups: 00403F40 the id registry at *007AFFA8 (refcount raised by the caller),
// 00593AA0 the world's FindObject (007AFF60).
__declspec(naked) CEngineObject* __fastcall WorldFindObject(void*, void*, int) EI_ORIGINAL(0x00593AA0)
CEngineObject* RegistryObject(int id) { return ManagerFindObject(*reinterpret_cast<void**>(0x007AFFA8), nullptr, id); }

// Rewritten script helpers, called through their addresses (their verify wrappers run).
__declspec(naked) void __fastcall RefReset(ScriptObjectRef*) EI_ORIGINAL(0x004B24B0)
__declspec(naked) void __fastcall RefAssign(ScriptObjectRef*, void*, CEngineObject*) EI_ORIGINAL(0x004B24D0)
__declspec(naked) void __fastcall GroupSort(ScriptGroup*) EI_ORIGINAL(0x004B2D60)
void __fastcall GroupSetSize(ScriptGroup* a0, void*, int a2, int a3) { PtrArraySetSize((void*)a0, nullptr, a2, a3); }

// CArray template members (out of line), __thiscall (size, growBy).
__declspec(naked) void __fastcall LightsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C5CB0)      // 0x20 slots
__declspec(naked) void __fastcall DefinitionsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004CD770) // 0xC0
__declspec(naked) void __fastcall AreasSetSize(void*, void*, int, int) EI_ORIGINAL(0x004CDB30)       // ScriptArea
__declspec(naked) void __fastcall PartsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004CC640)       // ScriptAreaPart
__declspec(naked) void __fastcall CmdsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004CD2B0)        // SSCCmd
__declspec(naked) void __fastcall IfThenSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C59C0)      // ScriptIfThen
// Copies (CArray::Copy / ScriptVars assignment), __thiscall (source).
__declspec(naked) void __fastcall IfThenCopy(void*, void*, const void*) EI_ORIGINAL(0x004CC4A0)
__declspec(naked) void __fastcall VarsCopy(void*, void*, const void*) EI_ORIGINAL(0x004CBAE0)
// ScriptVars constructor / destructor body.
__declspec(naked) void __fastcall VarsConstruct(ScriptVars*) EI_ORIGINAL(0x004C00D0)
__declspec(naked) void __fastcall VarsDestruct(ScriptVars*) EI_ORIGINAL(0x004BE090)

// ConstructElements / DestructElements instantiations (__stdcall (first, count)) and element
// destructors (__thiscall (flags); 0 = destroy in place).
__declspec(naked) void __stdcall ZeroFloats(void*, int) EI_ORIGINAL(0x004CC870)
__declspec(naked) void __stdcall ConstructStrings(void*, int) EI_ORIGINAL(0x006FE268)
void __stdcall DestructStrings(void* a0, int a1) { CStringElementsDestruct(a0, a1); }
__declspec(naked) void __stdcall ConstructRefs(void*, int) EI_ORIGINAL(0x004CC800)
void __stdcall DestructRefs(void* a0, int a1) { ScriptRefElementsDestruct(a0, a1); }
__declspec(naked) void __stdcall ConstructGroups(void*, int) EI_ORIGINAL(0x004CC9B0)
__declspec(naked) void __stdcall DestructGroups(void*, int) EI_ORIGINAL(0x004CC980)
__declspec(naked) void __stdcall ConstructCmds(void*, int) EI_ORIGINAL(0x004CE920)
__declspec(naked) void __stdcall ConstructIfThen(void*, int) EI_ORIGINAL(0x004CD4A0)
__declspec(naked) void __fastcall RefDestroy(void*, void*, int) EI_ORIGINAL(0x004CE1E0)
__declspec(naked) void __fastcall GroupDestroy(void*, void*, int) EI_ORIGINAL(0x004CE210)
__declspec(naked) void __fastcall CmdDestroy(void*, void*, int) EI_ORIGINAL(0x004BF620)
// 004CCA10: ConstructElements of the pointer arrays (zeroes).
__declspec(naked) void __stdcall ZeroPointers(void*, int) EI_ORIGINAL(0x004CCA10)

// SSCCmd's argument arrays (out of line), __thiscall (size, growBy): floats, group pointers,
// bindings (both arrays), nested calls (a new SSCCmd per element).
__declspec(naked) void __fastcall FloatArgsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C4410)
__declspec(naked) void __fastcall GroupArgsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C49F0)
__declspec(naked) void __fastcall BindingsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C4CE0)
__declspec(naked) void __fastcall SubCallsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C4FD0)
// 004B6330 (__thiscall on the element): type, argument index, then SSCCmd::Load into its command.
__declspec(naked) void __fastcall SubCallLoad(ScriptSubCall*, void*, ScriptStream*) EI_ORIGINAL(0x004B6330)

} // namespace

// ---- Inlined template code (script_array.h) ------------------------------------------------------

namespace scriptarray {

namespace {
template <void(__fastcall* Destroy)(void*, void*, int), int kSize>
void DestroyEach(char* first, int count) {
    for (int i = 0; i < count; ++i, first += kSize) Destroy(first, nullptr, 0);
}
template <RangeFn Destruct>
void DestroyRange(char* first, int count) { Destruct(first, count); }

} // namespace

const ElementKind kFloats{4, ZeroFloats, nullptr, nullptr};
const ElementKind kStrings{4, ConstructStrings, DestroyRange<DestructStrings>, DestroyRange<DestructStrings>};
const ElementKind kRefs{4, ConstructRefs, DestroyEach<RefDestroy, 4>, DestroyRange<DestructRefs>};
const ElementKind kGroups{0x14, ConstructGroups, DestroyEach<GroupDestroy, 0x14>, DestroyRange<DestructGroups>};
const ElementKind kCmds{0x94, ConstructCmds, DestroyEach<CmdDestroy, 0x94>, DestroyEach<CmdDestroy, 0x94>};
const ElementKind kIfThen{8, ConstructIfThen, nullptr, nullptr};
const ElementKind kPointers{4, ZeroPointers, nullptr, nullptr};

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
__declspec(naked) ListNode* __fastcall NewParticleNode(RawList*, void*, ListNode*, ListNode*) EI_ORIGINAL(0x004CD540)
__declspec(naked) ListNode* __fastcall NewFxNode(RawList*, void*, ListNode*, ListNode*) EI_ORIGINAL(0x004CD5B0)
__declspec(naked) ListNode* __fastcall NewLightningNode(RawList*, void*, ListNode*, ListNode*) EI_ORIGINAL(0x004CD620)
__declspec(naked) ListNode* __fastcall NewBlockNode(RawList*, void*, ListNode*, ListNode*) EI_ORIGINAL(0x004CD6D0)
__declspec(naked) ListNode* __fastcall NewInstanceNode(RawList*, void*, ListNode*, ListNode*) EI_ORIGINAL(0x004CD930)
__declspec(naked) ListNode* __fastcall NewRunNode(RawList*, void*, ListNode*, ListNode*) EI_ORIGINAL(0x004CDA30)

// CList::AddTail as inlined: a new node after the tail, its element filled by `fill`, then linked.
template <typename Fill>
void AppendNode(RawList* list, NewNodeFn newNode, Fill fill) {
    ListNode* node = newNode(list, nullptr, list->tail, nullptr);
    fill(node->Data());
    if (list->tail) list->tail->next = node;
    else list->head = node;
    list->tail = node;
}

// ---- Small helpers -------------------------------------------------------------------------------

template <typename T>
T& At(void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

int ReadInt(ScriptStream* stream) {
    int value = 0;
    stream->Read(&value, 4);
    return value;
}

// The stream's format version (+8); areas are saved from 0x57 on.
int StreamVersion(const ScriptStream* stream) { return reinterpret_cast<const int*>(stream)[2]; }

// The script being loaded (as in Save; the run loader finds a run's instance through it).
CScript*& LoadingScript() { return *reinterpret_cast<CScript**>(0x007AA620); }

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
const CScript* SavedScript() { return reinterpret_cast<const CScript*>(0x007C27D4); }

// CScript::FindBlock on the saved script, as the loaders inline it.
ScriptDecl* FindBlock(int id) { return ScriptFindBlock(SavedScript(), nullptr, id); }

// The loaders of this file, called from each other through their addresses (verify wrappers run).
__declspec(naked) void __fastcall FloatsLoadOrig(void*, void*, ScriptStream*) EI_ORIGINAL(0x004C2D40)
__declspec(naked) void __fastcall StringsLoadOrig(void*, void*, ScriptStream*) EI_ORIGINAL(0x004C3200)
__declspec(naked) void __fastcall ObjectsLoadOrig(void*, void*, ScriptStream*) EI_ORIGINAL(0x004C36B0)
__declspec(naked) void __fastcall GroupsLoadOrig(void*, void*, ScriptStream*) EI_ORIGINAL(0x004C40D0)
__declspec(naked) void __fastcall IfThenLoadOrig(void*, ScriptStream*) EI_ORIGINAL(0x004CA000)
__declspec(naked) void __fastcall DeclCommandsLoadOrig(void*, ScriptStream*) EI_ORIGINAL(0x004C57C0)
__declspec(naked) void __fastcall ParticlesLoadOrig(void*, ScriptStream*) EI_ORIGINAL(0x004CA480)
__declspec(naked) void __fastcall FxSourcesLoadOrig(void*, ScriptStream*) EI_ORIGINAL(0x004CA5D0)
__declspec(naked) void __fastcall LightningLoadOrig(void*, ScriptStream*) EI_ORIGINAL(0x004CA720)
__declspec(naked) void __fastcall WorldBlocksLoadOrig(void*, ScriptStream*) EI_ORIGINAL(0x004CA810)
__declspec(naked) void __fastcall InstancesLoadOrig(void*, ScriptStream*) EI_ORIGINAL(0x004CA980)
__declspec(naked) void __fastcall RunsLoadOrig(void*, ScriptStream*) EI_ORIGINAL(0x004CAC00)

// The four variable loaders of one ScriptVars (globals, instances).
void LoadVariables(ScriptVars* vars, ScriptStream* stream) {
    FloatsLoadOrig(&vars->floats, nullptr, stream);
    StringsLoadOrig(&vars->strings, nullptr, stream);
    ObjectsLoadOrig(&vars->objects, nullptr, stream);
    GroupsLoadOrig(&vars->groups, nullptr, stream);
}

} // namespace

// ---- Variables -------------------------------------------------------------------------------------

void __fastcall ScriptFloatsLoad(ScriptNamedValues<float>* vars, void*, ScriptStream* stream) {
    int count = ReadInt(stream);
    InlineSetSize(Raw(&vars->values), count, kFloats);
    NamesSetSizeOriginal(&vars->names, nullptr, count, -1);
    for (int i = 0; i < vars->values.size; ++i) {
        stream->Read(&vars->values.data[i], 4);
        StringLoad(&vars->names.data[i], stream);
    }
}
EI_REPLACE(0x004C2D40, ScriptFloatsLoad);

void __fastcall ScriptStringsLoad(ScriptNamedValues<mfc::CString>* vars, void*, ScriptStream* stream) {
    int count = ReadInt(stream);
    InlineSetSize(Raw(&vars->values), count, kStrings);
    NamesSetSizeOriginal(&vars->names, nullptr, count, -1);
    for (int i = 0; i < vars->values.size; ++i) {
        StringLoad(&vars->values.data[i], stream);
        StringLoad(&vars->names.data[i], stream);
    }
}
EI_REPLACE(0x004C3200, ScriptStringsLoad);

// An object variable: the world's object with the saved id (FindObject), -1 = none. The old
// reference is released after the lookup.
void __fastcall ScriptObjectsLoad(ScriptNamedValues<ScriptObjectRef>* vars, void*, ScriptStream* stream) {
    int count = ReadInt(stream);
    InlineSetSize(Raw(&vars->values), count, kRefs);
    NamesSetSizeOriginal(&vars->names, nullptr, count, -1);
    for (int i = 0; i < vars->values.size; ++i) {
        ScriptObjectRef* ref = &vars->values.data[i];
        int id = 0;
        stream->Read(&id, 4);
        if (id == -1) {
            RefReset(ref);
            ref->object = nullptr;
        } else {
            CEngineObject* object = WorldFindObject(reinterpret_cast<void*>(0x007AFF60), nullptr, id);
            RefReset(ref);
            if (object) ++object->refCount_;
            ref->object = object;
        }
        StringLoad(&vars->names.data[i], stream);
    }
}
EI_REPLACE(0x004C36B0, ScriptObjectsLoad);

// A group variable: its size, the member ids (each assigned in place, missing objects stay null),
// then sorted.
void __fastcall ScriptGroupsLoad(ScriptNamedValues<ScriptGroup>* vars, void*, ScriptStream* stream) {
    int count = ReadInt(stream);
    InlineSetSize(Raw(&vars->values), count, kGroups);
    NamesSetSizeOriginal(&vars->names, nullptr, count, -1);
    for (int i = 0; i < vars->values.size; ++i) {
        ScriptGroup* group = &vars->values.data[i];
        int size = 0;
        stream->Read(&size, 4);
        GroupSetSize(group, nullptr, size, -1);
        for (int k = 0; k < size; ++k) {
            int id = 0;
            stream->Read(&id, 4);
            CEngineObject* object =
                id == -1 ? nullptr : WorldFindObject(reinterpret_cast<void*>(0x007AFF60), nullptr, id);
            RefAssign(&group->data[k], nullptr, object);
        }
        GroupSort(group);
        StringLoad(&vars->names.data[i], stream);
    }
}
EI_REPLACE(0x004C40D0, ScriptGroupsLoad);

// ---- SSCCmd::Load -----------------------------------------------------------------------------------

// The mirror of SSCCmd::Save (004B6360). Object and group arguments are saved as counts only: the
// slots are made and left null (the original inlines the object array's SetSize, not the group
// array's). The called block is found with 004BFA50 in the lists of 007C27D4 (-1, an unset block,
// finds a WorldScript block with id -1 if any).
void __fastcall ScriptCmdLoad(SSCCmd* cmd, void*, ScriptStream* stream) {
    stream->Read(&cmd->id, 4);

    FloatArgsSetSize(&cmd->floatArgs, nullptr, ReadInt(stream), -1);
    for (int i = 0; i < cmd->floatArgs.size; ++i) stream->Read(&cmd->floatArgs.data[i], 4);

    NamesSetSizeOriginal(&cmd->stringArgs, nullptr, ReadInt(stream), -1);
    for (int i = 0; i < cmd->stringArgs.size; ++i) StringLoad(&cmd->stringArgs.data[i], stream);

    int objects = ReadInt(stream);
    InlineSetSize(Raw(&cmd->objectArgs), objects, kPointers);
    for (int i = 0; i < objects; ++i) cmd->objectArgs.data[i] = nullptr;

    int groups = ReadInt(stream);
    GroupArgsSetSize(&cmd->groupArgs, nullptr, groups, -1);
    for (int i = 0; i < groups; ++i) cmd->groupArgs.data[i] = nullptr;

    BindingsSetSize(&cmd->localBindings, nullptr, ReadInt(stream), -1);
    for (int i = 0; i < cmd->localBindings.size; ++i) stream->Read(&cmd->localBindings.data[i], 12);
    BindingsSetSize(&cmd->globalBindings, nullptr, ReadInt(stream), -1);
    for (int i = 0; i < cmd->globalBindings.size; ++i) stream->Read(&cmd->globalBindings.data[i], 12);

    SubCallsSetSize(&cmd->subCalls, nullptr, ReadInt(stream), -1);
    for (int i = 0; i < cmd->subCalls.size; ++i) SubCallLoad(&cmd->subCalls.data[i], nullptr, stream);

    cmd->calledScript = FindBlockOrig(SavedScript(), nullptr, ReadInt(stream));
}
EI_REPLACE(0x004B5FE0, ScriptCmdLoad);

// One nested call element (SSCCmd::Load calls it per element): type, argument index, then the
// element's command through SSCCmd::Load's address.
void __fastcall ScriptSubCallLoad(ScriptSubCall* call, void*, ScriptStream* stream) {
    stream->Read(&call->type, 4);
    stream->Read(&call->argIndex, 4);
    CmdLoad(call->cmd, nullptr, stream);
}
EI_REPLACE(0x004B6330, ScriptSubCallLoad);

// A string: read into a temporary CString (00461190 constructs it), assigned, the temporary destroyed.
void __fastcall ScriptStringLoad(mfc::CString* out, ScriptStream* stream) {
    alignas(4) unsigned char temporary[sizeof(mfc::CString)];
    *out = *StreamReadString(stream, nullptr, temporary);
    reinterpret_cast<mfc::CString*>(temporary)->~CString();
}
EI_REPLACE(0x004B3D10, ScriptStringLoad);

// ---- Command blocks and if-then pairs --------------------------------------------------------------

// The commands of a block (its id is read by the caller): count, then each SSCCmd.
void __fastcall ScriptDeclCommandsLoad(ScriptDecl* block, ScriptStream* stream) {
    int count = ReadInt(stream);
    InlineSetSize(Raw(&block->commands), count, kCmds);
    for (int i = 0; i < block->commands.size; ++i) CmdLoad(&block->commands.data[i], nullptr, stream);
}
EI_REPLACE(0x004C57C0, ScriptDeclCommandsLoad);

// A definition's if-then pairs: the ids of the condition and command blocks, resolved in the
// saved script's block lists (FindBlock). The first id defaults to -1 when the read leaves it.
void __fastcall ScriptIfThenLoad(MfcArray<ScriptIfThen>* pairs, ScriptStream* stream) {
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
void __fastcall ScriptIfThenPairLoad(ScriptIfThen* pair, void*, ScriptStream* stream) {
    int id = -1;
    stream->Read(&id, 4);
    pair->conditions = FindBlock(id);
    stream->Read(&id, 4);   // the same variable: keeps the first id when the read leaves it
    pair->commands = FindBlock(id);
}
EI_REPLACE(0x004BB590, ScriptIfThenPairLoad);

// The block lists are MFC CLists of ScriptDecl; the id is compared as the block's +0x14.
ScriptDecl* __fastcall ScriptFindBlock(const CScript* script, void*, int id) {
    std::size_t offset = id < 0 ? 0x178 : 0x70;
    const auto* list = reinterpret_cast<const RawList*>(reinterpret_cast<const char*>(script) + offset);
    for (ListNode* node = list->head; node;) {
        auto* block = reinterpret_cast<ScriptDecl*>(node->Data());
        node = node->next;
        if (block->id == id) return block;
    }
    return nullptr;
}
EI_REPLACE(0x004BFA50, ScriptFindBlock);

// ---- ScriptVars::Load ------------------------------------------------------------------------------

// The variable loaders' format for all four kinds, with the arrays' out-of-line SetSize (004C2880,
// 004C2510, 004C2050, 004C3980), objects assigned through 004B24D0 and group members read with
// ScriptObjectRef::Load. One stack variable holds the float count and index and then receives every
// object id and group size (it is never reset in between); the other counts start at 0.
void __fastcall ScriptVarsLoad(ScriptVars* vars, void*, ScriptStream* stream) {
    int slot = 0;
    stream->Read(&slot, 4);
    FloatsSetSize(&vars->floats, nullptr, slot, -1);
    NamesSetSizeOriginal(&vars->floatNames, nullptr, slot, -1);
    for (slot = 0; slot < vars->floats.size; ++slot) {
        stream->Read(&vars->floats.data[slot], 4);
        StringLoad(&vars->floatNames.data[slot], stream);
    }

    int count = 0;
    stream->Read(&count, 4);
    NamesSetSizeOriginal(&vars->strings, nullptr, count, -1);
    NamesSetSizeOriginal(&vars->stringNames, nullptr, count, -1);
    for (int i = 0; i < vars->strings.size; ++i) {
        StringLoad(&vars->strings.data[i], stream);
        StringLoad(&vars->stringNames.data[i], stream);
    }

    count = 0;
    stream->Read(&count, 4);
    GroupSetSize(static_cast<ScriptGroup*>(static_cast<void*>(&vars->objects)), nullptr, count, -1);
    NamesSetSizeOriginal(&vars->objectNames, nullptr, count, -1);
    for (int i = 0; i < vars->objects.size; ++i) {
        ScriptObjectRef* ref = &vars->objects.data[i];
        stream->Read(&slot, 4);
        if (slot == -1) {
            RefReset(ref);
            ref->object = nullptr;
        } else {
            RefAssign(ref, nullptr, WorldFindObject(reinterpret_cast<void*>(0x007AFF60), nullptr, slot));
        }
        StringLoad(&vars->objectNames.data[i], stream);
    }

    count = 0;
    stream->Read(&count, 4);
    GroupValuesSetSize(&vars->groups, nullptr, count, -1);
    NamesSetSizeOriginal(&vars->groupNames, nullptr, count, -1);
    for (int i = 0; i < vars->groups.size; ++i) {
        ScriptGroup* group = &vars->groups.data[i];
        stream->Read(&slot, 4);
        GroupSetSize(group, nullptr, slot, -1);
        for (int k = 0; k < slot; ++k) RefLoad(&group->data[k], nullptr, stream);
        GroupSort(group);
        StringLoad(&vars->groupNames.data[i], stream);
    }
}
EI_REPLACE(0x004B3AD0, ScriptVarsLoad);

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
        char* data = node->Data();
        node = node->next;
        At<CEngineObject*>(data, 0x00) = AcquireById(ReadInt(stream));
        stream->Read(data + 0x04, 4);
        stream->Read(data + 0x08, 4);
        stream->Read(data + 0x0C, 4);
        stream->Read(data + 0x10, 4);
        stream->Read(data + 0x14, 0xC);
        stream->Read(data + 0x20, 4);
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
            At<void*>(data, 0x00) = nullptr;
            At<int>(data, 0x04) = 0;
            At<mfc::CString>(data, 0x08) = emptyName;
            std::memset(data + 0x0C, 0, 0x28);
        });
    }
    for (ListNode* node = list->head; node;) {
        char* data = node->Data();
        node = node->next;
        FxSourceLoad(data, nullptr, stream);
    }
}
EI_REPLACE(0x004CA5D0, ScriptFxSourcesLoad);

// Lightning (CScript +0x4C; element: object, id).
void __fastcall ScriptLightningLoad(RawList* list, ScriptStream* stream) {
    int count = ReadInt(stream);
    for (int i = 0; i < count; ++i) {
        AppendNode(list, NewLightningNode, [](char* data) {
            At<void*>(data, 0x00) = nullptr;
            At<int>(data, 0x04) = -1;
        });
    }
    for (ListNode* node = list->head; node;) {
        char* data = node->Data();
        node = node->next;
        At<CEngineObject*>(data, 0x00) = AcquireById(ReadInt(stream));
        stream->Read(data + 0x04, 4);
    }
}
EI_REPLACE(0x004CA720, ScriptLightningLoad);

// WorldScript blocks (CScript +0x178; element ScriptDecl): id, count, each SSCCmd.
void __fastcall ScriptWorldBlocksLoad(RawList* list, ScriptStream* stream) {
    int count = ReadInt(stream);
    for (int i = 0; i < count; ++i) {
        // A temporary empty block (vtable 0073D3DC, id unset) is copied into the node.
        ScriptDecl temp{};
        temp.commands.vtable = reinterpret_cast<void*>(0x0073D3DC);
        AppendNode(list, NewBlockNode, [&](char* data) {
            auto* block = reinterpret_cast<ScriptDecl*>(data);
            CommandsCopy(&block->commands, nullptr, &temp.commands);
            block->id = temp.id;
        });
        // Its destructor frees nothing (no commands).
    }
    for (ListNode* node = list->head; node;) {
        auto* block = reinterpret_cast<ScriptDecl*>(node->Data());
        node = node->next;
        stream->Read(&block->id, 4);
        int size = ReadInt(stream);
        CmdsSetSize(&block->commands, nullptr, size, -1);
        for (int i = 0; i < block->commands.size; ++i) CmdLoad(&block->commands.data[i], nullptr, stream);
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
            MfcArray<ScriptIfThen> ifThen;
            ScriptVars vars;
        } temp{};
        temp.ifThen.vtable = reinterpret_cast<void*>(0x0073D430);
        VarsConstruct(&temp.vars);
        AppendNode(list, NewInstanceNode, [&](char* data) {
            auto* instance = reinterpret_cast<ScriptDefinition*>(data);
            instance->index = temp.index;
            instance->name = temp.name;
            instance->params = temp.params;
            IfThenCopy(&instance->ifThen, nullptr, &temp.ifThen);
            VarsCopy(&instance->vars, nullptr, &temp.vars);
        });
        VarsDestruct(&temp.vars);
    }
    for (ListNode* node = list->head; node;) {
        auto* instance = reinterpret_cast<ScriptDefinition*>(node->Data());
        node = node->next;
        stream->Read(&instance->index, 4);
        // The original reads each string (00461190) into a temporary and assigns it: 004B3D10.
        StringLoad(&instance->name, stream);
        StringLoad(&instance->params, stream);
        int pairs = ReadInt(stream);
        IfThenSetSize(&instance->ifThen, nullptr, pairs, -1);
        for (int k = 0; k < instance->ifThen.size; ++k) IfThenPairLoad(&instance->ifThen.data[k], nullptr, stream);
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
        VarsConstruct(&temp.vars);
        AppendNode(list, NewRunNode, [&](char* data) {
            auto* run = reinterpret_cast<ScriptRun*>(data);
            run->block = temp.block;
            run->index = temp.index;
            run->ticks = temp.ticks;
            run->sleepStart = temp.sleepStart;
            run->killFlag = temp.killFlag;
            VarsCopy(&run->vars, nullptr, &temp.vars);
        });
        VarsDestruct(&temp.vars);
    }
    for (ListNode* node = list->head; node;) {
        auto* run = reinterpret_cast<ScriptRun*>(node->Data());
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
        for (ListNode* inst = At<RawList>(LoadingScript(), 0x140).head; inst; ++k) {
            char* data = inst->Data();
            inst = inst->next;
            if (k == instanceIndex) {
                killFlag = reinterpret_cast<int*>(data);
                break;
            }
        }
        run->killFlag = killFlag;
        VarsLoad(&run->vars, nullptr, stream);
    }
}
EI_REPLACE(0x004CAC00, ScriptRunsLoad);

// ---- CScript::Load --------------------------------------------------------------------------------

// Reads what ScriptSave (004C0F90) wrote, in the same order. Sets 007AA620 to the script while
// the runs are read. Areas are read only from stream version 0x57 on.
void __fastcall ScriptLoad(CScript* script, void*, ScriptStream* stream) {
    LoadingScript() = script;

    // Point lights (+0x00; 0x20-byte slots).
    auto* lights = &At<RawArray>(script, 0x00);
    LightsSetSize(lights, nullptr, ReadInt(stream), -1);
    for (int i = 0; i < lights->size; ++i) PointLightLoad(lights->data + i * 0x20, nullptr, stream);

    ParticlesLoadOrig(&At<RawList>(script, 0x14), stream);
    FxSourcesLoadOrig(&At<RawList>(script, 0x30), stream);
    LightningLoadOrig(&At<RawList>(script, 0x4C), stream);
    stream->Read(&At<int>(script, 0x68), 4);
    stream->Read(&At<int>(script, 0x6C), 4);

    // Command blocks (+0x70): created empty, then each block's id and commands.
    int blocks = ReadInt(stream);
    for (int i = 0; i < blocks; ++i) NewCommandList(script);
    for (ListNode* node = At<RawList>(script, 0x70).head; node;) {
        auto* block = reinterpret_cast<ScriptDecl*>(node->Data());
        node = node->next;
        stream->Read(&block->id, 4);
        DeclCommandsLoadOrig(block, stream);
    }
    WorldBlocksLoadOrig(&At<RawList>(script, 0x178), stream);

    // Definitions (+0x8C).
    auto& scripts = At<MfcArray<ScriptDefinition>>(script, 0x8C);
    DefinitionsSetSize(&scripts, nullptr, ReadInt(stream), -1);
    for (int i = 0; i < scripts.size; ++i) {
        ScriptDefinition* definition = &scripts.data[i];
        stream->Read(&definition->index, 4);
        StringLoad(&definition->name, stream);
        StringLoad(&definition->params, stream);
        IfThenLoadOrig(&definition->ifThen, stream);
        VarsLoad(&definition->vars, nullptr, stream);
    }

    LoadVariables(script->Globals(), stream);
    InstancesLoadOrig(&At<RawList>(script, 0x140), stream);
    RunsLoadOrig(&At<RawList>(script, 0x15C), stream);
    stream->Read(&At<int>(script, 0x1A8), 4);
    stream->Read(&At<int>(script, 0x1AC), 4);
    LoadingScript() = nullptr;

    if (StreamVersion(stream) < 0x57) return;
    auto& areas = At<MfcArray<ScriptArea>>(script, 0x194);
    AreasSetSize(&areas, nullptr, ReadInt(stream), -1);
    for (int i = 0; i < areas.size; ++i) {
        ScriptArea* area = &areas.data[i];
        stream->Read(&area->id, 4);
        PartsSetSize(&area->parts, nullptr, ReadInt(stream), -1);
        for (int k = 0; k < area->parts.size; ++k) stream->Read(&area->parts.data[k], sizeof(ScriptAreaPart));
    }
}
EI_REPLACE(0x004C0D00, ScriptLoad);
