// The CList<T> instantiations that destroy game elements (game_lists.h), registered at their library
// addresses (ticket S7; the plain ones are in lib/mfc_list.cpp).
#include "game/game_lists.h"
#include "game/originals_script.h"
#include "game/record_arrays.h"

#include "ei/hook.h"

#include <cstdint>
#include <new>

namespace gamelists {

// Engine object pointers released through slot 4 (the slot left as it is).
EI_REPLACE(0x0059A050, EngineObjects::RemoveAt);
EI_REPLACE(0x00429E50, EngineObjects::RemoveAll);

EngineObjects::Node* __fastcall CountedAddTail(EngineObjects::List* self, void*, CEngineObject* object) {
    EngineObjects::Node* node = self->NewNode(self->tail, nullptr);
    ObjectHolderAssign(&node->data, nullptr, object);
    if (self->tail) self->tail->next = node;
    else self->head = node;
    self->tail = node;
    return node;
}
EI_REPLACE(0x00599FA0, CountedAddTail);

// The magic trap's victims: released through slot 4, the slot cleared.
EI_REPLACE(0x0052CE20, ObjectRefs::FreeNode);

// Counted pointers: the plain release (the world's missiles, a session list), the owner's.
EI_REPLACE(0x0059C810, Refs::FreeNode);
EI_REPLACE(0x00599CC0, Refs::RemoveAt);
EI_REPLACE(0x00598EF0, Refs::RemoveAll);
EI_REPLACE(0x005575F0, OwnedRefs::Destruct<OwnedRefListClass>);

// The armour spells (12-byte records holding an engine object).
EI_REPLACE(0x00558B80, Holders::FreeNode);
EI_REPLACE(0x00558020, Holders::RemoveAll);

// The world's shared objects.
EI_REPLACE(0x0059CAD0, SharedObjects::FreeNode);
EI_REPLACE(0x0059A3C0, SharedObjects::Destruct<SharedObjectListClass>);

// The script's effects, tokens, definitions and runs.
EI_REPLACE(0x004C6040, ParticleSources::RemoveAt);
EI_REPLACE(0x004C6370, FxSources::RemoveAt);
EI_REPLACE(0x004C6680, Lightnings::RemoveAt);
EI_REPLACE(0x004CA260, Tokens::Destruct<TokenListClass>);
EI_REPLACE(0x004CD9B0, Definitions::FreeNode);
EI_REPLACE(0x004CDAB0, Runs::FreeNode);
EI_REPLACE(0x004C54A0, ScriptDeclDestruct);   // the command block list's element destructor

// CScript's lists' destructors (S13b; CScript::Destruct, world_destructors.cpp).
EI_REPLACE(0x004C92C0, ObjectRefs::Destruct<TempObjectListClass>);
EI_REPLACE(0x004C8E20, Groups::Destruct<TempGroupListClass>);
EI_REPLACE(0x004C7560, Decls::Destruct<DeclListClass>);
EI_REPLACE(0x004C8520, Runs::Destruct<RunListClass>);
EI_REPLACE(0x004C7E30, Definitions::Destruct<InstanceListClass>);
EI_REPLACE(0x004C6760, Lightnings::Destruct<LightningListClass>);
EI_REPLACE(0x004C6410, FxSources::Destruct<FxSourceListClass>);
EI_REPLACE(0x004C6120, ParticleSources::Destruct<ParticleSourceListClass>);

// ---- The script lists with the game's copy semantics (S7a) ----------------------------------------

namespace {

// The if-then array of a definition the lists build (the array's derived vtable).
struct DefinitionIfThenClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D430); }
};

gamearrays::ObjectRefs::Array* RefArray(ScriptGroup* group) {
    return reinterpret_cast<gamearrays::ObjectRefs::Array*>(group);
}
const gamearrays::ObjectRefs::Array* RefArray(const ScriptGroup* group) {
    return reinterpret_cast<const gamearrays::ObjectRefs::Array*>(group);
}

// A command block's copy constructor (inlined by 004C69B0): the array's constructor, SetSize to the
// source's size, each command a GetAt copy assigned and destroyed; then the id.
void CopyConstructDecl(ScriptDecl* to, const ScriptDecl* from) {
    mfc::impl::ConstructHeader<mfc::impl::ScriptCommandsClass>(&to->commands);
    mfc::impl::ArrayAs<recordarrays::Cmds>(to->commands).SetSize(from->commands.size, -1);
    for (int i = 0; i < from->commands.size; ++i) {
        alignas(SSCCmd) std::uint8_t storage[sizeof(SSCCmd)];
        auto* copy = reinterpret_cast<SSCCmd*>(storage);
        ScriptCmdCopyAt(&from->commands, nullptr, copy, i);
        to->commands.data[i].ScriptCmdAssign(copy);
        ScriptCmdDestruct(copy);
    }
    to->id = from->id;
}

// A definition's copy constructor (inlined by 004C7A10): the pairs read through GetAt (8 bytes).
void CopyConstructDefinition(ScriptDefinition* to, const ScriptDefinition* from) {
    to->index = from->index;
    new (&to->name) mfc::CString(from->name);
    new (&to->params) mfc::CString(from->params);
    mfc::impl::ConstructHeader<DefinitionIfThenClass>(&to->ifThen);
    to->ifThen.SetSize(from->ifThen.size, -1);
    for (int i = 0; i < from->ifThen.size; ++i) to->ifThen.data[i] = from->ifThen.data[i];
    to->vars.ScriptVarsCopyConstruct(&from->vars);
}

// A run's copy constructor (inlined by 004C82B0): the variables through the four value-and-names
// copy constructors.
template <typename T>
ScriptNamedValues<T>* Pair(mfc::CArray<T>* values) {
    return reinterpret_cast<ScriptNamedValues<T>*>(values);
}
template <typename T>
const ScriptNamedValues<T>* Pair(const mfc::CArray<T>* values) {
    return reinterpret_cast<const ScriptNamedValues<T>*>(values);
}
void CopyConstructRun(ScriptRun* to, const ScriptRun* from) {
    to->block = from->block;
    to->index = from->index;
    to->ticks = from->ticks;
    to->sleepStart = from->sleepStart;
    to->killFlag = from->killFlag;
    Pair(&to->vars.floats)->CopyConstruct(Pair(&from->vars.floats));
    Pair(&to->vars.strings)->CopyConstruct(Pair(&from->vars.strings));
    Pair(&to->vars.objects)->CopyConstruct(Pair(&from->vars.objects));
    Pair(&to->vars.groups)->CopyConstruct(Pair(&from->vars.groups));
}

// A group's copy constructor: an empty group, then the references copied.
void CopyConstructGroup(ScriptGroup* to, const ScriptGroup* from) {
    gamearrays::InitGroup(to);
    gamearrays::ObjectRefCopy(RefArray(to), nullptr, RefArray(from));
}

// AddHead / AddTail as the instantiations inline them: the new node linked after its element is set.
template <typename List>
typename List::Node* LinkHead(List* self, typename List::Node* node) {
    if (self->head) self->head->prev = node;
    else self->tail = node;
    self->head = node;
    return node;
}
template <typename List>
typename List::Node* LinkTail(List* self, typename List::Node* node) {
    if (self->tail) self->tail->next = node;
    else self->head = node;
    self->tail = node;
    return node;
}

using DefinitionArray = mfc::impl::ArrayAbi<ScriptDefinition, DefinitionElements>;
using RunArray = mfc::impl::ArrayAbi<ScriptRun, RunElements>;
using DefineArray = mfc::impl::ArrayAbi<ScriptDefine, ScriptDefineElements>;   // CList<ScriptDefine>'s

} // namespace

void InitDefinition(ScriptDefinition* p) {
    new (&p->name) mfc::CString();
    new (&p->params) mfc::CString();
    mfc::impl::ConstructHeader<DefinitionIfThenClass>(&p->ifThen);
    p->vars.ScriptVarsConstruct();
    p->index = 0;
}
void InitRun(ScriptRun* p) { p->vars.ScriptVarsConstruct(); }

EI_REPLACE(0x004CEB80, DefinitionArray::ConstructElements);
EI_REPLACE(0x004CECB0, RunArray::ConstructElements);
EI_REPLACE(0x004D6840, DefineArray::ConstructElements);   // the zero fill, then both strings empty
EI_REPLACE(0x004CD930, Definitions::NewNode);
EI_REPLACE(0x004CDA30, Runs::NewNode);

ScriptDecl* __fastcall DeclRemoveHead(Decls::List* self, void*, ScriptDecl* out) {
    CopyConstructDecl(out, &self->head->data);
    self->RemoveAt(self->head);
    return out;
}
EI_REPLACE(0x004C69B0, DeclRemoveHead);

ScriptDefinition* __fastcall InstanceRemoveHead(Definitions::List* self, void*, ScriptDefinition* out) {
    CopyConstructDefinition(out, &self->head->data);
    self->RemoveAt(self->head);
    return out;
}
EI_REPLACE(0x004C7A10, InstanceRemoveHead);

ScriptRun* __fastcall RunRemoveHead(Runs::List* self, void*, ScriptRun* out) {
    CopyConstructRun(out, &self->head->data);
    self->RemoveAt(self->head);
    return out;
}
EI_REPLACE(0x004C82B0, RunRemoveHead);

Definitions::Node* __fastcall InstanceAddHead(Definitions::List* self, void*, const ScriptDefinition* value) {
    Definitions::Node* node = self->NewNode(nullptr, self->head);
    node->data.ScriptDefinitionAssign(value);
    return LinkHead(self, node);
}
EI_REPLACE(0x004C7C90, InstanceAddHead);

ObjectRefs::Node* __fastcall ObjectRefAddHead(ObjectRefs::List* self, void*, const ScriptObjectRef* value) {
    ObjectRefs::Node* node = self->NewNode(nullptr, self->head);
    gamearrays::ReleaseAndClear(&node->data);
    CEngineObject* object = value->object;
    node->data.object = object;
    if (object) ++object->refCount_;
    return LinkHead(self, node);
}
EI_REPLACE(0x004C9210, ObjectRefAddHead);

ScriptObjectRef* __fastcall ObjectRefRemoveTail(ObjectRefs::List* self, void*, ScriptObjectRef* out) {
    CEngineObject* object = self->tail->data.object;
    if (object) ++object->refCount_;   // the original's temporary copy
    self->RemoveAt(self->tail);
    out->object = object;
    if (object) {
        ++object->refCount_;
        object->Release();   // the temporary destroyed
    }
    return out;
}
EI_REPLACE(0x004C9100, ObjectRefRemoveTail);

Groups::Node* __fastcall GroupAddHead(Groups::List* self, void*, const ScriptGroup* value) {
    Groups::Node* node = self->NewNode(nullptr, self->head);
    gamearrays::ObjectRefCopy(RefArray(&node->data), nullptr, RefArray(value));
    return LinkHead(self, node);
}
EI_REPLACE(0x004C8CE0, GroupAddHead);

Groups::Node* __fastcall GroupAddTail(Groups::List* self, void*, const ScriptGroup* value) {
    Groups::Node* node = self->NewNode(self->tail, nullptr);
    gamearrays::ObjectRefCopy(RefArray(&node->data), nullptr, RefArray(value));
    return LinkTail(self, node);
}
EI_REPLACE(0x004C8D80, GroupAddTail);

ScriptGroup* __fastcall GroupRemoveTail(Groups::List* self, void*, ScriptGroup* out) {
    CopyConstructGroup(out, &self->tail->data);
    self->RemoveAt(self->tail);
    return out;
}
EI_REPLACE(0x004C8AF0, GroupRemoveTail);

// The sound manager's wave cache.
EI_REPLACE(0x006BD4F0, Waves::NewNode);
EI_REPLACE(0x006BD560, Waves::FreeNode);

} // namespace gamelists
