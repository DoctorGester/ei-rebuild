// The CList<T> instantiations that destroy game elements (game_lists.h), registered at their library
// addresses (ticket S7; the plain ones are in lib/mfc_list.cpp).
#include "game/game_lists.h"
#include "game/game_slots.h"
#include "game/originals_script.h"
#include "game/record_arrays.h"

#include "ei/hook.h"
#include "lib/mfc_array.h"
#include "game_data.h"

#include <cstdint>
#include <new>

namespace gamelists {

EI_REWRITTEN(0x0059A050, EngineObjects::RemoveAt);
EI_REWRITTEN(0x00429E50, EngineObjects::RemoveAll);
EngineObjects::Node* __fastcall CountedAddTail(EngineObjects::List* self, void*, CEngineObject* object) {
    EngineObjects::Node* node = self->NewNode(self->tail, nullptr);
    ObjectHolderAssign(&node->data, nullptr, object);
    if (self->tail) self->tail->next = node;
    else self->head = node;
    self->tail = node;
    return node;
}
EI_REWRITTEN(0x004C92C0, ObjectRefs::Destruct<gameslots::ObjectRefListClass>);
EI_REWRITTEN(0x004C8E20, Groups::Destruct<gameslots::GroupListClass>);
EI_REWRITTEN(0x004C7560, Decls::Destruct<gameslots::DeclListClass>);
EI_REWRITTEN(0x004C8520, Runs::Destruct<gameslots::RunListClass>);
EI_REWRITTEN(0x004C7E30, Definitions::Destruct<gameslots::DefinitionListClass>);
EI_REWRITTEN(0x004C6760, Lightnings::Destruct<gameslots::LightningListClass>);
EI_REWRITTEN(0x004C6410, FxSources::Destruct<gameslots::FxSourceListClass>);
EI_REWRITTEN(0x004C6120, ParticleSources::Destruct<gameslots::ParticleSourceListClass>);
EI_REWRITTEN(0x004C6040, ParticleSources::RemoveAt);
EI_REWRITTEN(0x004C6370, FxSources::RemoveAt);
EI_REWRITTEN(0x004C6680, Lightnings::RemoveAt);
EI_REWRITTEN(0x004CA260, Tokens::Destruct<gameslots::TokenListClass>);
EI_REWRITTEN(0x004CD9B0, Definitions::FreeNode);
EI_REWRITTEN(0x004CDAB0, Runs::FreeNode);
EI_REWRITTEN(0x004C54A0, ScriptDeclDestruct);
EI_REWRITTEN(0x0059CAD0, SharedObjects::FreeNode);
EI_REWRITTEN(0x0059A3C0, SharedObjects::Destruct<gameslots::SharedObjectListClass>);
EI_REWRITTEN(0x00558B80, Holders::FreeNode);
EI_REWRITTEN(0x00558020, Holders::RemoveAll);
EI_REWRITTEN(0x0059C810, Refs::FreeNode);
EI_REWRITTEN(0x00599CC0, Refs::RemoveAt);
EI_REWRITTEN(0x00598EF0, Refs::RemoveAll);
EI_REWRITTEN(0x005575F0, OwnedRefs::Destruct<gameslots::OwnedRefListClass>);
EI_REWRITTEN(0x0052CE20, ObjectRefs::FreeNode);
EI_REWRITTEN(0x00599FA0, CountedAddTail);

// ---- The script lists with the game's copy semantics (S7a) ----------------------------------------

namespace {

// The if-then array of a definition the lists build (the array's derived vtable).
struct DefinitionIfThenClass {
    static void* Vtable() { return ei::GamePointer<void>(0x0073D430); }
};

// A command block's copy constructor (inlined by 004C69B0): the array's constructor, SetSize to the
// source's size, each command a GetAt copy assigned and destroyed; then the id.
void CopyConstructDecl(ScriptDecl* to, const ScriptDecl* from) {
    mfc::impl::ConstructHeader<mfc::impl::ScriptCommandsClass>(&to->commands);
    mfc::impl::ArrayAs<recordarrays::Cmds>(to->commands).SetSize(from->commands.size, -1);
    for (int i = 0; i < from->commands.size; ++i) {
        alignas(SSCCmd) std::uint8_t storage[sizeof(SSCCmd)];
        auto* copy = reinterpret_cast<SSCCmd*>(storage);
        from->commands.CopyAt(copy, i);
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
    to->CopyFrom(*from);
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


} // namespace

void InitDefinition(ScriptDefinition* p) {
    new (&p->name) mfc::CString();
    new (&p->params) mfc::CString();
    mfc::impl::ConstructHeader<DefinitionIfThenClass>(&p->ifThen);
    p->vars.ScriptVarsConstruct();
    p->index = 0;
}
void InitRun(ScriptRun* p) { p->vars.ScriptVarsConstruct(); }

EI_REWRITTEN(0x004CD930, Definitions::NewNode);
EI_REWRITTEN(0x004CDA30, Runs::NewNode);









EI_REWRITTEN(0x006BD4F0, Waves::NewNode);
EI_REWRITTEN(0x006BD560, Waves::FreeNode);
} // namespace gamelists

// ---- Members of the script lists (script.h) -------------------------------------------------------

namespace {
template <typename Abi, typename List>
typename Abi::List* As(List* list) {
    return reinterpret_cast<typename Abi::List*>(list);
}
} // namespace

ScriptDecl* ScriptDecls::PopHead(ScriptDecl* out) {
    auto* list = As<gamelists::Decls>(this);
    gamelists::CopyConstructDecl(out, &list->head->data);
    list->RemoveAt(list->head);
    return out;
}
EI_REWRITTEN(0x004C69B0, ScriptDecls::PopHead);

ScriptDefinition* ScriptInstances::PopHead(ScriptDefinition* out) {
    auto* list = As<gamelists::Definitions>(this);
    gamelists::CopyConstructDefinition(out, &list->head->data);
    list->RemoveAt(list->head);
    return out;
}
EI_REWRITTEN(0x004C7A10, ScriptInstances::PopHead);

ScriptRun* ScriptRuns::PopHead(ScriptRun* out) {
    auto* list = As<gamelists::Runs>(this);
    gamelists::CopyConstructRun(out, &list->head->data);
    list->RemoveAt(list->head);
    return out;
}
EI_REWRITTEN(0x004C82B0, ScriptRuns::PopHead);

ScriptInstances::Node* ScriptInstances::PushHead(const ScriptDefinition* value) {
    auto* list = As<gamelists::Definitions>(this);
    gamelists::Definitions::Node* node = list->NewNode(nullptr, list->head);
    node->data.ScriptDefinitionAssign(value);
    return reinterpret_cast<Node*>(gamelists::LinkHead(list, node));
}
EI_REWRITTEN(0x004C7C90, ScriptInstances::PushHead);

ScriptTempObjects::Node* ScriptTempObjects::PushHead(const ScriptObjectRef* value) {
    auto* list = As<gamelists::ObjectRefs>(this);
    gamelists::ObjectRefs::Node* node = list->NewNode(nullptr, list->head);
    gamearrays::ReleaseAndClear(&node->data);
    CEngineObject* object = value->object;
    node->data.object = object;
    if (object) ++object->refCount_;
    return reinterpret_cast<Node*>(gamelists::LinkHead(list, node));
}
EI_REWRITTEN(0x004C9210, ScriptTempObjects::PushHead);

ScriptObjectRef* ScriptTempObjects::PopTail(ScriptObjectRef* out) {
    auto* list = As<gamelists::ObjectRefs>(this);
    CEngineObject* object = list->tail->data.object;
    if (object) ++object->refCount_;   // the original's temporary copy
    list->RemoveAt(list->tail);
    out->object = object;
    if (object) {
        ++object->refCount_;
        object->Release();   // the temporary destroyed
    }
    return out;
}
EI_REWRITTEN(0x004C9100, ScriptTempObjects::PopTail);

ScriptTempGroups::Node* ScriptTempGroups::PushHead(const ScriptGroup* value) {
    auto* list = As<gamelists::Groups>(this);
    gamelists::Groups::Node* node = list->NewNode(nullptr, list->head);
    node->data.CopyFrom(*value);
    return reinterpret_cast<Node*>(gamelists::LinkHead(list, node));
}
EI_REWRITTEN(0x004C8CE0, ScriptTempGroups::PushHead);

ScriptTempGroups::Node* ScriptTempGroups::PushTail(const ScriptGroup* value) {
    auto* list = As<gamelists::Groups>(this);
    gamelists::Groups::Node* node = list->NewNode(list->tail, nullptr);
    node->data.CopyFrom(*value);
    return reinterpret_cast<Node*>(gamelists::LinkTail(list, node));
}
EI_REWRITTEN(0x004C8D80, ScriptTempGroups::PushTail);

ScriptGroup* ScriptTempGroups::PopTail(ScriptGroup* out) {
    auto* list = As<gamelists::Groups>(this);
    gamelists::CopyConstructGroup(out, &list->tail->data);
    list->RemoveAt(list->tail);
    return out;
}
EI_REWRITTEN(0x004C8AF0, ScriptTempGroups::PopTail);
