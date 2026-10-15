// The CList<T> instantiations that destroy game elements (game_lists.h), registered at their library
// addresses (ticket S7; the plain ones are in lib/mfc_list.cpp).
#include "game/game_lists.h"

#include "ei/hook.h"

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

// The sound manager's wave cache.
EI_REPLACE(0x006BD4F0, Waves::NewNode);
EI_REPLACE(0x006BD560, Waves::FreeNode);

} // namespace gamelists
