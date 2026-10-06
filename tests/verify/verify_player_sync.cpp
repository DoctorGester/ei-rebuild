// Checks for player_sync.cpp (scenario: every one; the server syncs the local player each world
// update).
//
// - The "changed" senders and SendItems only append to the player's outgoing stream (an object's
//   pending delta is written once, by ours): ours runs, the stream's header and bytes are saved,
//   restored, the original runs, and both must match (skipped when ours grew the stream's buffer).
// - The "added" / "removed" senders change the player's maps and reference counts: ours only. Added:
//   each object is in the map under its id, counted once more, the stream grew. Removed: no id is
//   left in the map, the stream grew by five bytes per object.
// - Flush sends on the link and advances the coder's window: ours only, the result is 0 or 1 and the
//   messages are empty after a send.
#include "fake_stream.h"
#include "verify.h"

#include "../../src/game/player_messages.h"

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
int __fastcall TimedChangedVerify(CPlayerServer* self, void*, const PlayerObjectArray* objects) {
    return CheckAppend("CPlayerServer::SendTimedChanged", self, [&] { return self->SendTimedChanged(objects); },
                       [&] { return s_timedChanged(self, nullptr, objects); });
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

} // namespace

EI_VERIFY(0x0066DBA0, ObjectsChangedVerify, 5, s_objectsChanged);
EI_VERIFY(0x0066E020, TimedChangedVerify, 5, s_timedChanged);
EI_VERIFY(0x0066F080, SendItemsVerify, 7, s_sendItems);
EI_VERIFY(0x0066D7F0, ObjectsAddedVerify, 0, s_unused);
EI_VERIFY(0x0066DDE0, TimedAddedVerify, 0, s_unused);
EI_VERIFY(0x0066DAD0, ObjectsRemovedVerify, 0, s_unused);
EI_VERIFY(0x0066DF40, TimedRemovedVerify, 0, s_unused);
EI_VERIFY(0x0066D100, FlushVerify, 0, s_unused);
