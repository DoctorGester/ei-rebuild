// The CList<T> instantiations that destroy game elements (game_lists.h; the plain ones are in
// lib/mfc_list.cpp).
#include "game/game_lists.h"
#include "game/game_slots.h"
#include "game/record_arrays.h"

#include "ei/hook.h"
#include "lib/static_object.h"
#include "lib/mfc_array.h"
#include "game_data.h"

#include <cstdint>
#include <memory>
#include <new>

namespace gamelists {

EI_REWRITTEN(0x0059A050, EngineObjects::RemoveAt);
EI_REWRITTEN(0x00429E50, EngineObjects::RemoveAll);
CountedEngineList::Node* CountedEngineList::AddTailCounted(CEngineObject* object) {
    Node* node = NewNode(tail, nullptr);
    HeldEngineObject::Of(&node->data)->Assign(object);
    if (tail) tail->next = node;
    else head = node;
    tail = node;
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
EI_REWRITTEN(0x00599FA0, CountedEngineList::AddTailCounted);

// ---- The script lists with the game's copy semantics ----------------------------------------

namespace {

// A command block's copy constructor: the array's constructor, SetSize to the source's size, each command a
// GetAt copy assigned and destroyed; then the id.
void CopyConstructDecl(ScriptDecl* to, const ScriptDecl* from) {
    mfc::impl::ConstructHeader<mfc::impl::ScriptCommandsClass>(&to->commands);
    mfc::impl::ArrayAs<recordarrays::Cmds>(to->commands).SetSize(from->commands.size, -1);
    for (int i = 0; i < from->commands.size; ++i) {
        lib::InPlace<SSCCmd> storage;
        auto* copy = storage.Get();
        from->commands.CopyAt(copy, i);
        to->commands.data[i].ScriptCmdAssign(copy);
        ScriptCmdDestruct(copy);
    }
    to->id = from->id;
}

// A definition's copy constructor: the pairs read through GetAt (8 bytes).
void CopyConstructDefinition(ScriptDefinition* to, const ScriptDefinition* from) {
    to->index = from->index;
    std::construct_at(&to->name, from->name);
    std::construct_at(&to->params, from->params);
    mfc::impl::ConstructHeader<mfc::impl::ScriptIfThenPairsClass>(&to->ifThen);
    to->ifThen.SetSize(from->ifThen.size, -1);
    for (int i = 0; i < from->ifThen.size; ++i) to->ifThen.data[i] = from->ifThen.data[i];
    to->vars.ScriptVarsCopyConstruct(&from->vars);
}

// A run's copy constructor: the variables through the four value-and-names copy constructors.
void CopyConstructRun(ScriptRun* to, const ScriptRun* from) {
    to->block = from->block;
    to->index = from->index;
    to->ticks = from->ticks;
    to->sleepStart = from->sleepStart;
    to->killFlag = from->killFlag;
    NamedValues(&to->vars.floats)->CopyConstruct(NamedValues(&from->vars.floats));
    NamedValues(&to->vars.strings)->CopyConstruct(NamedValues(&from->vars.strings));
    NamedValues(&to->vars.objects)->CopyConstruct(NamedValues(&from->vars.objects));
    NamedValues(&to->vars.groups)->CopyConstruct(NamedValues(&from->vars.groups));
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
    std::construct_at(&p->name);
    std::construct_at(&p->params);
    mfc::impl::ConstructHeader<mfc::impl::ScriptIfThenPairsClass>(&p->ifThen);
    p->vars.ScriptVarsConstruct();
    p->index = 0;
}
void InitRun(ScriptRun* p) { p->vars.ScriptVarsConstruct(); }

void DestroySharedObject(SharedObject* p) {
    p->objects.vtable = gamearrays::ObjectPtrArrayBaseClass::Vtable();
    p->objects.ReleaseAll();
}

EI_REWRITTEN(0x004CD930, Definitions::NewNode);
EI_REWRITTEN(0x004CDA30, Runs::NewNode);









EI_REWRITTEN(0x006BD4F0, Waves::NewNode);
EI_REWRITTEN(0x006BD560, Waves::FreeNode);
} // namespace gamelists

// ---- Members of the script lists (script.h) -------------------------------------------------------

namespace {
// The script lists are CLists of plain elements (script.h); their members build and destroy the elements as the
// game's instantiations do (`Elements`, the policies of game_lists.h): a new node's element constructed, a
// removed node's element destroyed before RemoveAt frees the node.
template <typename Elements, typename List>
typename List::Node* NewElementNode(List& list, typename List::Node* prev, typename List::Node* next) {
    typename List::Node* node = list.NewNode(prev, next);
    Elements::Construct(&node->data, 1);
    return node;
}
template <typename Elements, typename List>
void RemoveElement(List& list, typename List::Node* node) {
    Elements::Destruct(&node->data, 1);
    list.RemoveAt(node);
}
} // namespace

ScriptDecl* ScriptDecls::PopHead(ScriptDecl* out) {
    gamelists::CopyConstructDecl(out, &head->data);
    RemoveElement<gamelists::DeclElements>(*this, head);
    return out;
}
EI_REWRITTEN(0x004C69B0, ScriptDecls::PopHead);

ScriptDefinition* ScriptInstances::PopHead(ScriptDefinition* out) {
    gamelists::CopyConstructDefinition(out, &head->data);
    RemoveElement<gamelists::DefinitionElements>(*this, head);
    return out;
}
EI_REWRITTEN(0x004C7A10, ScriptInstances::PopHead);

ScriptRun* ScriptRuns::PopHead(ScriptRun* out) {
    gamelists::CopyConstructRun(out, &head->data);
    RemoveElement<gamelists::RunElements>(*this, head);
    return out;
}
EI_REWRITTEN(0x004C82B0, ScriptRuns::PopHead);

ScriptInstances::Node* ScriptInstances::PushHead(const ScriptDefinition* value) {
    Node* node = NewElementNode<gamelists::DefinitionElements>(*this, nullptr, head);
    node->data.ScriptDefinitionAssign(value);
    return gamelists::LinkHead(this, node);
}
EI_REWRITTEN(0x004C7C90, ScriptInstances::PushHead);

ScriptTempObjects::Node* ScriptTempObjects::PushHead(const ScriptObjectRef* value) {
    Node* node = NewElementNode<gamelists::ObjectRefElements>(*this, nullptr, head);
    gamearrays::ReleaseAndClear(&node->data);
    CEngineObject* object = value->object;
    node->data.object = object;
    if (object) ++object->refCount_;
    return gamelists::LinkHead(this, node);
}
EI_REWRITTEN(0x004C9210, ScriptTempObjects::PushHead);

ScriptObjectRef* ScriptTempObjects::PopTail(ScriptObjectRef* out) {
    CEngineObject* object = tail->data.object;
    if (object) ++object->refCount_;   // a temporary copy
    RemoveElement<gamelists::ObjectRefElements>(*this, tail);
    out->object = object;
    if (object) {
        ++object->refCount_;
        object->Release();   // the temporary destroyed
    }
    return out;
}
EI_REWRITTEN(0x004C9100, ScriptTempObjects::PopTail);

ScriptTempGroups::Node* ScriptTempGroups::PushHead(const ScriptGroup* value) {
    Node* node = NewElementNode<gamelists::GroupElements>(*this, nullptr, head);
    node->data.CopyFrom(*value);
    return gamelists::LinkHead(this, node);
}
EI_REWRITTEN(0x004C8CE0, ScriptTempGroups::PushHead);

ScriptTempGroups::Node* ScriptTempGroups::PushTail(const ScriptGroup* value) {
    Node* node = NewElementNode<gamelists::GroupElements>(*this, tail, nullptr);
    node->data.CopyFrom(*value);
    return gamelists::LinkTail(this, node);
}
EI_REWRITTEN(0x004C8D80, ScriptTempGroups::PushTail);

ScriptGroup* ScriptTempGroups::PopTail(ScriptGroup* out) {
    gamelists::CopyConstructGroup(out, &tail->data);
    RemoveElement<gamelists::GroupElements>(*this, tail);
    return out;
}
EI_REWRITTEN(0x004C8AF0, ScriptTempGroups::PopTail);
