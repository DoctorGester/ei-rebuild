// Checks for player_sync.cpp (scenario: every one; the server syncs the local player each world
// update).
//
// - The "changed" senders and SendItems only append to the player's outgoing stream (an object's
//   pending delta is written once, by ours): ours runs, the stream's header and bytes are saved,
//   restored, the original runs, and both must match (skipped when ours grew the stream's buffer).
//   SendTimedChanged's original runs with the effects' "delta pending" bit cleared (see there).
// - The "added" / "removed" senders change the player's maps and reference counts: ours only. Added:
//   each object is in the map under its id, counted once more, the stream grew. Removed: no id is
//   left in the map, the stream grew by five bytes per object.
// - Flush sends on the link and advances the coder's window: ours only, the result is 0 or 1 and the
//   messages are empty after a send.
#include "fake_stream.h"
#include "verify.h"

#include "../../src/game/player_messages.h"
#include "../../src/game/world.h"
#include "../../src/game/world_effect.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;

struct OutState {
    BitStream header;
    std::vector<unsigned char> bytes;
    explicit OutState(CPlayerServer* p) {
        RawCopy(&header, &p->out_);
        if (p->out_.data_) bytes.assign(p->out_.data_, p->out_.data_ + p->out_.capacity_);
    }
    void Restore(CPlayerServer* p) const {
        RawCopy(&p->out_, &header);
        if (!bytes.empty()) std::memcpy(p->out_.data_, bytes.data(), bytes.size());
    }
    bool SameAs(const OutState& o) const {
        if (std::memcmp(&header, &o.header, sizeof(BitStream)) != 0) return false;
        std::size_t n = header.size_ < bytes.size() ? header.size_ : bytes.size();
        if (n > o.bytes.size()) return false;
        return n == 0 || std::memcmp(bytes.data(), o.bytes.data(), n) == 0;
    }
};

template <typename Ours, typename Original>
int CheckAppend(const char* name, CPlayerServer* self, Ours ours, Original original) {
    OutState before(self);
    int actual = ours();
    OutState after(self);
    if (after.header.data_ != before.header.data_ || after.header.capacity_ != before.header.capacity_) return actual;
    before.Restore(self);
    int expected = original();
    OutState again(self);
    ei::verify::Check(name, true, again.SameAs(after) && expected == actual, "player %p: stream size %u vs %u", self,
                      again.header.size_, after.header.size_);
    return expected;
}

using Sender = int(__fastcall*)(CPlayerServer*, void*, const PlayerObjectArray*);

// 0066DBA0 / 0066E020: SUB ESP,14h; PUSH EBX; PUSH EBP (5).
Sender s_objectsChanged;
int __fastcall ObjectsChangedVerify(CPlayerServer* self, void*, const PlayerObjectArray* objects) {
    return CheckAppend("CPlayerServer::SendObjectsChanged", self, [&] { return self->SendObjectsChanged(objects); },
                       [&] { return s_objectsChanged(self, nullptr, objects); });
}
Sender s_timedChanged;
// CEffect::SendDelta leaves flags_ bit 1 set, so the original's run would pack the changed effects
// a second time. Instead ours runs, then the original runs with bit 1 cleared on every object: it
// writes the same messages from the state fields ours just packed, and the effects' delta fields
// are not touched again (SendDelta and the field writers have their own checks). The flags are
// put back afterwards. Skipped when ours grew the player's stream or an object packed by ours is
// listed twice (its first message holds the first pack's bytes).
int __fastcall TimedChangedVerify(CPlayerServer* self, void*, const PlayerObjectArray* objects) {
    OutState before(self);
    int actual = self->SendTimedChanged(objects);
    OutState after(self);
    if (after.header.data_ != before.header.data_ || after.header.capacity_ != before.header.capacity_) return actual;
    const int stamp = static_cast<CWorldServer*>(self->Owner()->ManagerContext())->updateCount_;
    std::vector<CEngineObject*> packed;
    for (int i = 0; i < objects->size; ++i) {
        auto* effect = static_cast<CEffect*>(objects->data[i]);
        if (effect->changeStamp_ != stamp || !(effect->flags_ & 2)) continue;
        for (CEngineObject* other : packed)
            if (other == effect) return actual;
        packed.push_back(effect);
    }
    std::vector<std::uint32_t> flags;
    for (int i = 0; i < objects->size; ++i) {
        flags.push_back(objects->data[i]->flags_);
        objects->data[i]->flags_ &= ~2u;
    }
    before.Restore(self);
    int expected = s_timedChanged(self, nullptr, objects);
    for (int i = 0; i < objects->size; ++i) objects->data[i]->flags_ = flags[i];
    OutState again(self);
    ei::verify::Check("CPlayerServer::SendTimedChanged", true, again.SameAs(after) && expected == actual,
                      "player %p: stream size %u vs %u", self, again.header.size_, after.header.size_);
    return expected;
}

// 0066F080: PUSH ESI; PUSH EDI; MOV EDI,ECX; MOV EAX,[EDI+1Ch] (7).
void(__fastcall* s_sendItems)(CPlayerServer*);
void __fastcall SendItemsVerify(CPlayerServer* self) {
    CheckAppend("CPlayerServer::SendItems", self, [&] { self->SendItems(); return 0; }, [&] { s_sendItems(self); return 0; });
}

bool InMap(const CPlayer::ObjectMap& map, CEngineObject* object) {
    const CPlayer::ObjectMap::Node* node = map.Find(object->id_);
    return node != nullptr && node->value == object;
}

template <typename Send>
int CheckAdded(const char* name, CPlayerServer* self, const PlayerObjectArray* objects, CPlayer::ObjectMap& map, Send send) {
    std::vector<int> counts;
    for (int i = 0; i < objects->size; ++i) counts.push_back(objects->data[i]->refCount_);
    std::uint32_t size = self->out_.size_;
    int result = send();
    bool ok = result == 1 && (objects->size == 0 || self->out_.size_ > size);
    for (int i = 0; i < objects->size; ++i) {
        CEngineObject* object = objects->data[i];
        // An object listed twice is counted twice.
        int times = 0;
        for (int j = 0; j < objects->size; ++j) times += objects->data[j] == object;
        ok = ok && InMap(map, object) && object->refCount_ == counts[i] + times;
    }
    ei::verify::Check(name, true, ok, "player %p: %d objects", self, objects->size);
    return result;
}

template <typename Send>
int CheckRemoved(const char* name, CPlayerServer* self, const PlayerObjectArray* objects, CPlayer::ObjectMap& map, Send send) {
    std::vector<int> ids;
    for (int i = 0; i < objects->size; ++i) ids.push_back(objects->data[i]->id_);
    std::uint32_t size = self->out_.size_;
    int result = send();
    bool ok = result == 1 && self->out_.size_ == size + 5u * static_cast<std::uint32_t>(ids.size());
    for (int id : ids) ok = ok && map.Find(id) == nullptr;
    ei::verify::Check(name, true, ok, "player %p: %d objects", self, objects->size);
    return result;
}

int __fastcall ObjectsAddedVerify(CPlayerServer* self, void*, const PlayerObjectArray* objects) {
    return CheckAdded("CPlayerServer::SendObjectsAdded", self, objects, self->Owner()->knownObjects_,
                      [&] { return self->SendObjectsAdded(objects); });
}
int __fastcall TimedAddedVerify(CPlayerServer* self, void*, const PlayerObjectArray* objects) {
    return CheckAdded("CPlayerServer::SendTimedAdded", self, objects, self->Owner()->knownTimed_,
                      [&] { return self->SendTimedAdded(objects); });
}
int __fastcall ObjectsRemovedVerify(CPlayerServer* self, void*, const PlayerObjectArray* objects) {
    return CheckRemoved("CPlayerServer::SendObjectsRemoved", self, objects, self->Owner()->knownObjects_,
                        [&] { return self->SendObjectsRemoved(objects); });
}
int __fastcall TimedRemovedVerify(CPlayerServer* self, void*, const PlayerObjectArray* objects) {
    return CheckRemoved("CPlayerServer::SendTimedRemoved", self, objects, self->Owner()->knownTimed_,
                        [&] { return self->SendTimedRemoved(objects); });
}

int __fastcall FlushVerify(CPlayerServer* self) {
    bool linked = self->Owner()->link_ != nullptr;
    int result = self->Flush();
    bool ok = linked ? (result == 0 || result == 1) && self->out_.size_ == 0 : result == 0;
    ei::verify::Check("CPlayerServer::Flush", true, ok, "player %p: result %d", self, result);
    return result;
}

// 0066E430: PUSH -1; PUSH imm32 (7). Only the stream and refreshPending_ change (the shared-object
// group is built and destroyed inside): ours, both restored, original, both compared.
int(__fastcall* s_partyState)(CPlayerServer*);
int __fastcall PartyStateVerify(CPlayerServer* self) {
    int pending = self->refreshPending_;
    int oursPending = 0;
    int result = CheckAppend(
        "CPlayerServer::SendPartyState", self,
        [&] {
            int r = self->SendPartyState();
            oursPending = self->refreshPending_;
            self->refreshPending_ = pending;
            return r;
        },
        [&] { return s_partyState(self); });
    ei::verify::Check("CPlayerServer::SendPartyState.pending", oursPending, self->refreshPending_, "player %p", self);
    return result;
}

// Ours only (releases the object): gone from the map, the sent list and both groups.
int __fastcall ForgetObjectVerify(CPlayerServer* self, void*, CEngineObject* object) {
    int id = object->id_;
    int result = self->ForgetObject(object);
    bool ok = result == 1 && self->Owner()->knownObjects_.Find(id) == nullptr;
    for (int i = 0; i < self->sentObjects_.size; ++i) ok = ok && self->sentObjects_.data[i] != object;
    for (int i = 0; i < self->group50F8_.size; ++i) ok = ok && self->group50F8_.data[i].object != object;
    for (int i = 0; i < self->partyUnits_.size; ++i) ok = ok && self->partyUnits_.data[i].object != object;
    ei::verify::Check("CPlayerServer::ForgetObject", true, ok, "player %p object id %d", self, id);
    return result;
}

// 0066E110: MOV EAX,[ESP+4]; ADD ECX,510Ch (10). Pure.
BOOL(__fastcall* s_isPartyUnit)(CPlayerServer*, void*, CEngineObject*);
BOOL __fastcall IsPartyUnitVerify(CPlayerServer* self, void*, CEngineObject* object) {
    BOOL expected = s_isPartyUnit(self, nullptr, object);
    BOOL actual = self->IsPartyUnit(object);
    ei::verify::Check("CPlayerServer::IsPartyUnit", expected, actual, "player %p object %p", self, object);
    return expected;
}

// Ours only (advances the ISAAC generator): message 0x80 appended, the timer in 900..1800.
void __fastcall SendPartyVerify(CPlayerServer* self) {
    std::uint32_t size = self->out_.size_;
    std::uint32_t at = self->out_.position_;
    self->SendParty();
    bool ok = self->out_.size_ > size && self->out_.data_[at] == 0x80 && self->refreshTimer_ >= 900 &&
              self->refreshTimer_ <= 1800;
    ei::verify::Check("CPlayerServer::SendParty", true, ok, "player %p timer %d", self, self->refreshTimer_);
}

// Ours only (consumes the link and runs the handlers): the result is 0 or 1.
int __fastcall ReceiveVerify(CPlayerServer* self) {
    int result = self->ReceiveMessages();
    bool ok = result == 0 || result == 1;
    ei::verify::Check("CPlayerServer::ReceiveMessages", true, ok, "player %p: result %d state %d", self, result,
                      self->state_);
    return result;
}

// Ours only (rewrites the brains' lists): +0x24 is -1 after, and the party units hold group50F8_
// (and, when +0x24 was -1, every unit of the player).
void __fastcall UpdatePartyUnitsVerify(CPlayerServer* self) {
    bool all = self->possessedId_ == 0xFFFFFFFFu;
    self->UpdatePartyUnits();
    bool ok = self->possessedId_ == 0xFFFFFFFFu;
    for (int i = 0; i < self->group50F8_.size; ++i) {
        CEngineObject* object = self->group50F8_.data[i].object;
        ok = ok && (object == nullptr || self->partyUnits_.Contains(object));
    }
    if (all) {
        CPlayer::ObjectMap& units = self->Owner()->units_;
        for (CPlayer::ObjectMap::Node* node = units.First(); node && node->value; node = units.After(node))
            ok = ok && self->partyUnits_.Contains(node->value);
    }
    ei::verify::Check("CPlayerServer::UpdatePartyUnits", true, ok, "player %p: %d party units", self,
                      self->partyUnits_.size);
}

// 0066E7D0: MOV EAX,FS:[0] (6). Only appends to the stream (the items' Save writes into a stack
// stream): compared as the other appenders (A-H10).
void(__fastcall* s_sendItemsGiven)(CPlayerServer*, void*, const mfc::CTypedPtrList<CEngineObject>*, int);
void __fastcall SendItemsGivenVerify(CPlayerServer* self, void*, const mfc::CTypedPtrList<CEngineObject>* items,
                                     int flag) {
    CheckAppend("CPlayerServer::SendItemsGiven", self, [&] { self->SendItemsGiven(items, flag); return 0; },
                [&] { s_sendItemsGiven(self, nullptr, items, flag); return 0; });
}

} // namespace

EI_VERIFY(0x0066E7D0, SendItemsGivenVerify, 6, s_sendItemsGiven);

EI_VERIFY(0x0066E130, UpdatePartyUnitsVerify, 0, s_unused);
EI_VERIFY(0x0066D270, ReceiveVerify, 0, s_unused);
EI_VERIFY(0x0066E430, PartyStateVerify, 7, s_partyState);
EI_VERIFY(0x0066DC90, ForgetObjectVerify, 0, s_unused);
EI_VERIFY(0x0066E110, IsPartyUnitVerify, 10, s_isPartyUnit);
EI_VERIFY(0x0066EF80, SendPartyVerify, 0, s_unused);
EI_VERIFY(0x0066DBA0, ObjectsChangedVerify, 5, s_objectsChanged);
EI_VERIFY(0x0066E020, TimedChangedVerify, 5, s_timedChanged);
EI_VERIFY(0x0066F080, SendItemsVerify, 7, s_sendItems);
EI_VERIFY(0x0066D7F0, ObjectsAddedVerify, 0, s_unused);
EI_VERIFY(0x0066DDE0, TimedAddedVerify, 0, s_unused);
EI_VERIFY(0x0066DAD0, ObjectsRemovedVerify, 0, s_unused);
EI_VERIFY(0x0066DF40, TimedRemovedVerify, 0, s_unused);
EI_VERIFY(0x0066D100, FlushVerify, 0, s_unused);
