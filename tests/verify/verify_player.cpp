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
           p->unknown_110 == -1 && p->unknown_130 == 0 && p->unknown_108 == 0 && p->sessionKind100_ == 0 &&
           p->unknown_104 == 0;
}
} // namespace

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
              self->objects_.blockSize == 10 && self->logTraffic_ == 0 && self->unknownFC_ == 0 &&
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
