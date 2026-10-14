// Checks for player.cpp (CPlayer's life cycle). Class name and id compare with the original; the
// rest allocate, release or call into the party, units and connection, so they run ours only and
// check the state left. Every new game constructs, creates and clears the players (newgame).
// - Construct (00664BF0): vtable, no link or connection, id -1, three empty maps with buckets,
//   empty lists of block size 10 with their vtables, the party created (an item manager).
// - DestructBody (00664E20): CEngineObject's vtable back, both lists and the unit map empty.
// - DeletingDestruct (00664E00): returns this (the object may be freed; nothing else is read).
// - CreatePlayer / DestroyPlayer (00665010, 006650E0, 00665380) and Clear (00665200): what they
//   leave (empty lists and maps, no link or connection after Destroy).
// - SetConnection (00665950), SetPlayerId (00665A60): the fields set.
#include "verify.h"

#include "../../src/game/party.h"
#include "../../src/game/player_messages.h"
#include "../../src/game/unit.h"

#include "ei/vcall.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

using NameFn = const char*(__fastcall*)(CPlayer*);
using IdFn = int(__fastcall*)(CPlayer*);
NameFn s_className;
IdFn s_classId;

std::uintptr_t VtableOf(const void* object) {
    std::uintptr_t vtable;
    std::memcpy(&vtable, object, sizeof(vtable));
    return vtable;
}

bool ListEmpty(const mfc::CTypedPtrList<void>& list) {
    return list.count == 0 && list.head == nullptr && list.tail == nullptr && list.blocks == nullptr;
}
bool ListEmpty(const mfc::CTypedPtrList<CEngineObject>& list) {
    return list.count == 0 && list.head == nullptr && list.tail == nullptr && list.blocks == nullptr;
}

// What Clear leaves.
bool Cleared(const CPlayer* p) {
    return ListEmpty(p->items_) && ListEmpty(p->objects_) && p->units_.Size() == 0 && p->maxUnitId_ == 0 &&
           p->knownObjects_.Size() == 0 && p->knownTimed_.Size() == 0 && p->maxObjectId_ == 0 &&
           p->maxTimedId_ == 0;
}

// What DestroyPlayer leaves.
bool Destroyed(const CPlayer* p) {
    return Cleared(p) && p->link_ == nullptr && p->connection_ == nullptr && p->playerId_ == -1 &&
           p->lastTick_ == -1 && p->dirty_ == 0 && p->godMode_ == 0 && p->sessionKind100_ == 0 &&
           p->lastScreen_ == 0;
}
} // namespace

int __fastcall PlayerMessageError(const char* format, std::uint32_t previous, std::uint32_t type);

// 00664B70: ours only (FatalError may end the game); counted before it runs. No scenario (a broken
// message stream).
static int __fastcall PlayerMessageErrorVerify(const char* format, std::uint32_t previous, std::uint32_t type) {
    ei::verify::Check("PlayerMessageError", true, format != nullptr, "prev %02X type %02X", previous & 0xFF, type & 0xFF);
    return PlayerMessageError(format, previous, type);
}
EI_VERIFY(0x00664B70, PlayerMessageErrorVerify, 0, s_unused);

const char* __fastcall PlayerClassName(CPlayer*);
int __fastcall PlayerClassId(CPlayer*);

static const char* __fastcall PlayerClassNameVerify(CPlayer* self) {
    const char* expected = s_className(self);
    ei::verify::Check("CPlayer::GetClassName", expected, PlayerClassName(self), "player %p", self);
    return expected;
}
EI_VERIFY(0x00664DE0, PlayerClassNameVerify, 5, s_className);

static int __fastcall PlayerClassIdVerify(CPlayer* self) {
    int expected = s_classId(self);
    ei::verify::Check("CPlayer::GetClassId", expected, PlayerClassId(self), "player %p", self);
    return expected;
}
EI_VERIFY(0x00664DF0, PlayerClassIdVerify, 5, s_classId);

static CPlayer* __fastcall PlayerConstructVerify(CPlayer* self) {
    CPlayer* result = self->Construct();
    bool ok = result == self && VtableOf(self) == CPlayer::kVtableAddress && Destroyed(self) &&
              self->knownObjects_.BucketCount() >= 100 && self->units_.BucketCount() >= 100 &&
              self->knownTimed_.BucketCount() >= 100 && self->items_.blockSize == 10 &&
              self->objects_.blockSize == 10 && self->logTraffic_ == 0 && self->partyId_ == 0 &&
              self->Party()->itemManager_ != nullptr;
    ei::verify::Check("CPlayer::Construct", true, ok, "player %p: units %u known %u/%u", self,
                      static_cast<unsigned>(self->units_.Size()), static_cast<unsigned>(self->knownObjects_.Size()),
                      static_cast<unsigned>(self->knownTimed_.Size()));
    return result;
}
EI_VERIFY(0x00664BF0, PlayerConstructVerify, 0, s_unused);

static void __fastcall PlayerDestructVerify(CPlayer* self) {
    self->DestructBody();
    bool ok = VtableOf(self) == CEngineObject::kVtableAddress && ListEmpty(self->items_) && ListEmpty(self->objects_);
    ei::verify::Check("CPlayer::DestructBody", true, ok, "player %p", self);
}
EI_VERIFY(0x00664E20, PlayerDestructVerify, 0, s_unused);

static CPlayer* __fastcall PlayerDeletingDestructVerify(CPlayer* self, void*, unsigned flags) {
    CPlayer* result = self->DeletingDestruct(flags);
    ei::verify::Check("CPlayer::DeletingDestruct", self, result, "flags %u", flags);
    return result;
}
EI_VERIFY(0x00664E00, PlayerDeletingDestructVerify, 0, s_unused);

static BOOL __fastcall PlayerCreateVerify(CPlayer* self) {
    BOOL result = self->CreatePlayer();
    ei::verify::Check("CPlayer::CreatePlayer", true, result == 1 && Destroyed(self), "player %p", self);
    return result;
}
EI_VERIFY(0x00665010, PlayerCreateVerify, 0, s_unused);

static void __fastcall PlayerDestroyVerify(CPlayer* self) {
    self->DestroyPlayer();
    ei::verify::Check("CPlayer::DestroyPlayer", true, Destroyed(self), "player %p", self);
}
EI_VERIFY(0x006650E0, PlayerDestroyVerify, 0, s_unused);
EI_VERIFY(0x00665380, PlayerDestroyVerify, 0, s_unused);

static void __fastcall PlayerClearVerify(CPlayer* self) {
    self->Clear();
    ei::verify::Check("CPlayer::Clear", true, Cleared(self), "player %p: units %u", self,
                      static_cast<unsigned>(self->units_.Size()));
}
EI_VERIFY(0x00665200, PlayerClearVerify, 0, s_unused);

static void __fastcall PlayerSetConnectionVerify(CPlayer* self, void*, CPlayerConnection* connection) {
    self->SetConnection(connection);
    bool ok = self->connection_ == connection && (connection == nullptr || connection->player_ == self);
    ei::verify::Check("CPlayer::SetConnection", true, ok, "player %p connection %p", self, connection);
}
EI_VERIFY(0x00665950, PlayerSetConnectionVerify, 0, s_unused);

static void __fastcall PlayerSetIdVerify(CPlayer* self, void*, int id) {
    self->SetPlayerId(id);
    ei::verify::Check("CPlayer::SetPlayerId", id, self->playerId_, "player %p", self);
}
EI_VERIFY(0x00665A60, PlayerSetIdVerify, 0, s_unused);

// --- The link, the given items and the units (all newgame / zone changes unless said) ---------------
// - Connect (00665990): the new link kept, cleared lists and maps. Disconnect (00665A10): no link,
//   cleared. ForgetKnownObjects (006651D0, from 0066FAA0): known maps empty.
// - IsLeader (00665440) and HasLoot (00665640) read only: compared with the original.
// - GiveItems (00665470): the list grew by the given count; TakeAllItems (00665550): empty;
//   TakeLoot / TakeItem (00665780 / 00665870): the count dropped by the result.
// - AddUnit / RemoveUnit (00665A70 / 00665B50, from CUnit::SetPlayer): the unit is / is not in
//   units_. ItemUsed (00665BE0): the id is in the recharge list. AssignParty (00665CC0): returns
//   the party, the recharge list empty. ReleaseTamedUnits (00665D20, taming): every unit left
//   still has this player.

namespace {
using IsLeaderFn = BOOL(__fastcall*)(CPlayer*, void*, const CEngineObject*);
using HasLootFn = BOOL(__fastcall*)(CPlayer*, void*, int);
IsLeaderFn s_isLeader;
HasLootFn s_hasLoot;
} // namespace

static void __fastcall PlayerConnectVerify(CPlayer* self, void*, netlink::NetLink* link) {
    self->Connect(link);
    ei::verify::Check("CPlayer::Connect", true, self->link_ == link && Cleared(self), "player %p link %p", self, link);
}
EI_VERIFY(0x00665990, PlayerConnectVerify, 0, s_unused);

static void __fastcall PlayerDisconnectVerify(CPlayer* self) {
    self->Disconnect();
    ei::verify::Check("CPlayer::Disconnect", true, self->link_ == nullptr && Cleared(self), "player %p", self);
}
EI_VERIFY(0x00665A10, PlayerDisconnectVerify, 0, s_unused);

static void __fastcall PlayerForgetKnownVerify(CPlayer* self) {
    self->ForgetKnownObjects();
    bool ok = self->knownObjects_.Size() == 0 && self->knownTimed_.Size() == 0 && self->maxObjectId_ == 0 &&
              self->maxTimedId_ == 0 && self->dirty_ == -1;
    ei::verify::Check("CPlayer::ForgetKnownObjects", true, ok, "player %p", self);
}
EI_VERIFY(0x006651D0, PlayerForgetKnownVerify, 0, s_unused);

static BOOL __fastcall PlayerIsLeaderVerify(CPlayer* self, void*, const CEngineObject* unit) {
    BOOL expected = s_isLeader(self, nullptr, unit);
    ei::verify::Check("CPlayer::IsLeader", expected, self->IsLeader(unit), "player %p unit %p", self, unit);
    return expected;
}
EI_VERIFY(0x00665440, PlayerIsLeaderVerify, 6, s_isLeader);

static BOOL __fastcall PlayerHasLootVerify(CPlayer* self, void*, int kind) {
    BOOL expected = s_hasLoot(self, nullptr, kind);
    ei::verify::Check("CPlayer::HasLoot", expected, self->HasLoot(kind), "player %p kind %d", self, kind);
    return expected;
}
EI_VERIFY(0x00665640, PlayerHasLootVerify, 6, s_hasLoot);

static void __fastcall PlayerGiveItemsVerify(CPlayer* self, void*, const mfc::CTypedPtrList<CEngineObject>* items,
                                             int flag) {
    const int before = self->items_.count;
    self->GiveItems(items, flag);
    ei::verify::Check("CPlayer::GiveItems", before + items->count, self->items_.count, "player %p", self);
}
EI_VERIFY(0x00665470, PlayerGiveItemsVerify, 0, s_unused);

static void __fastcall PlayerTakeAllItemsVerify(CPlayer* self) {
    self->TakeAllItems();
    ei::verify::Check("CPlayer::TakeAllItems", true, ListEmpty(self->items_), "player %p", self);
}
EI_VERIFY(0x00665550, PlayerTakeAllItemsVerify, 0, s_unused);

static BOOL __fastcall PlayerTakeLootVerify(CPlayer* self, void*, int kind) {
    const int before = self->items_.count;
    BOOL result = self->TakeLoot(kind);
    ei::verify::Check("CPlayer::TakeLoot", before - result, self->items_.count, "player %p kind %d", self, kind);
    return result;
}
EI_VERIFY(0x00665780, PlayerTakeLootVerify, 0, s_unused);

static BOOL __fastcall PlayerTakeItemVerify(CPlayer* self, void*, int id) {
    const int before = self->items_.count;
    BOOL result = self->TakeItem(id);
    ei::verify::Check("CPlayer::TakeItem", before - result, self->items_.count, "player %p id %d", self, id);
    return result;
}
EI_VERIFY(0x00665870, PlayerTakeItemVerify, 0, s_unused);

static void __fastcall PlayerAddUnitVerify(CPlayer* self, void*, CUnit* unit) {
    self->AddUnit(unit);
    bool ok = unit == nullptr || self->units_.Find(unit->id_) != nullptr;
    ei::verify::Check("CPlayer::AddUnit", true, ok, "player %p unit %p", self, unit);
}
EI_VERIFY(0x00665A70, PlayerAddUnitVerify, 0, s_unused);

static void __fastcall PlayerRemoveUnitVerify(CPlayer* self, void*, CUnit* unit) {
    // The unit may be freed by its release; only its id is read before.
    const int id = unit != nullptr ? unit->id_ : 0;
    self->RemoveUnit(unit);
    bool ok = unit == nullptr || self->units_.Find(id) == nullptr;
    ei::verify::Check("CPlayer::RemoveUnit", true, ok, "player %p unit %p", self, unit);
}
EI_VERIFY(0x00665B50, PlayerRemoveUnitVerify, 0, s_unused);

static void __fastcall PlayerItemUsedVerify(CPlayer* self, void*, CEngineObject* item) {
    self->ItemUsed(item);
    void* id = reinterpret_cast<void*>(static_cast<std::intptr_t>(item->id_));
    ei::verify::Check("CPlayer::ItemUsed", true, self->objects_.Find(id) != nullptr, "player %p item %p", self, item);
}
EI_VERIFY(0x00665BE0, PlayerItemUsedVerify, 0, s_unused);

static CParty* __fastcall PlayerAssignPartyVerify(CPlayer* self, void*, CParty* source) {
    CParty* result = self->AssignParty(source);
    ei::verify::Check("CPlayer::AssignParty", true, result == self->Party() && ListEmpty(self->objects_),
                      "player %p source %p", self, source);
    return result;
}
EI_VERIFY(0x00665CC0, PlayerAssignPartyVerify, 0, s_unused);

static void __fastcall PlayerReleaseTamedVerify(CPlayer* self) {
    self->ReleaseTamedUnits();
    bool ok = true;
    for (auto* node = self->units_.First(); node != nullptr && node->value != nullptr; node = self->units_.After(node))
        ok = ok && static_cast<CUnit*>(node->value)->player == reinterpret_cast<char*>(self);
    ei::verify::Check("CPlayer::ReleaseTamedUnits", true, ok, "player %p units %u", self,
                      static_cast<unsigned>(self->units_.Size()));
}
EI_VERIFY(0x00665D20, PlayerReleaseTamedVerify, 0, s_unused);
