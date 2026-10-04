// Checks for net_connection.cpp.
//  - IdPool::Allocate: the original on a copy of the pool (it only reads the id storage), ours on
//    the real one; ids and pools compared.
//  - WriteAcks: the original on a scratch connection header with a private copy of the received
//    sequences and a scratch output; ours on the real ones; written bytes, cursor advance and the
//    remaining sequences compared.
//  - TypeRegistry::Create: both run; the two new messages must have the same class (vtable); the
//    original's is deleted again (a new message has no references).
//  - The others change the connection's containers or send packets: ours only, then the state they
//    leave is checked (AttachRecord: the record is found with its id and type; QueueHeader: a chunk
//    is queued and the oldest is at the front; RollBack: the history entry is marked).
#include "net_verify.h"

#include "../../src/game/net_connection.h"

#include "ei/vcall.h"

#include <cstring>

namespace {

using namespace net;

void* s_unused;

// 00435F10: MOV EAX,[ECX+4] (3), MOV EDX,[ECX] (2).
std::uint16_t(__fastcall* s_allocate)(IdPool*);
std::uint16_t __fastcall AllocateVerify(IdPool* self) {
    IdPool copy = *self;
    std::uint16_t expected = s_allocate(&copy);
    std::uint16_t actual = self->Allocate();
    bool same = expected == actual && std::memcmp(&copy, self, sizeof(IdPool)) == 0;
    ei::verify::Check("IdPool::Allocate", true, same, "%u / %u", expected, actual);
    return actual;
}

// 00434C20: PUSH ECX (1), MOV EDX,ECX (2), PUSH EBX (1), PUSH EBP (1).
void(__fastcall* s_writeAcks)(Connection*, void*, std::uint8_t**);
void __fastcall WriteAcksVerify(Connection* self, void*, std::uint8_t** p) {
    std::size_t n = static_cast<std::size_t>(self->receivedEnd - self->received);
    if (n > 256) {   // more than ever kept: ours alone
        self->WriteAcks(p);
        return;
    }
    alignas(4) std::uint8_t header[0x20];
    std::memcpy(header, self, sizeof(header));
    std::int32_t values[256];
    if (n) std::memcpy(values, self->received, n * 4);
    auto* fake = reinterpret_cast<Connection*>(header);
    fake->received = values;
    fake->receivedEnd = values + n;
    fake->receivedEos = values + 256;
    std::uint8_t output[8] = {};
    std::uint8_t* q = output;
    s_writeAcks(fake, nullptr, &q);
    std::uint8_t* before = *p;
    self->WriteAcks(p);
    std::size_t kept = static_cast<std::size_t>(self->receivedEnd - self->received);
    bool same = q - output == *p - before && std::memcmp(output, before, 6) == 0 &&
                kept == static_cast<std::size_t>(fake->receivedEnd - values) &&
                (kept == 0 || std::memcmp(values, self->received, kept * 4) == 0);
    ei::verify::Check("Connection::WriteAcks", true, same, "%u sequences", unsigned(n));
}

// 00438930: SUB ESP,8 (3), PUSH EBX (1), PUSH ESI (1).
Message*(__fastcall* s_create)(TypeRegistry*, void*, std::uint32_t);
Message* __fastcall TypeCreateVerify(TypeRegistry* self, void*, std::uint32_t type) {
    Message* expected = s_create(self, nullptr, type);
    Message* actual = self->Create(type);
    bool same = (expected == nullptr) == (actual == nullptr) && (!expected || expected->vtable == actual->vtable);
    ei::verify::Check("TypeRegistry::Create", true, same, "type %u", type);
    if (expected) ei::VCall<void*>(expected, 0, 1);
    return actual;
}

// 004352F0: ours only; the object's record must hold the id and type.
void __fastcall AttachRecordVerify(Connection* self, void*, CRefObject* object, std::uint32_t type, std::int32_t key) {
    self->AttachRecord(object, type, key);
    // Walk the tree from the root for the object (keys compare unsigned).
    auto* header = static_cast<std::uint8_t*>(self->attached.header);
    auto* node = *reinterpret_cast<std::uint8_t**>(header + 4);
    while (node && *reinterpret_cast<CRefObject**>(node + 0x10) != object)
        node = *reinterpret_cast<std::uint8_t**>(node + (reinterpret_cast<std::uintptr_t>(object) <
                                                                 *reinterpret_cast<std::uintptr_t*>(node + 0x10)
                                                             ? 8
                                                             : 0xC));
    bool ok = node && *reinterpret_cast<std::uint16_t*>(node + 0x14) == static_cast<std::uint16_t>(key) &&
              node[0x16] == static_cast<std::uint8_t>(type);
    ei::verify::Check("Connection::AttachRecord", true, ok, "key %08X", key);
}

// 00434990: ours only. Afterwards a chunk is queued and none waits longer than the front one.
void __fastcall QueueHeaderVerify(Connection* self, void*, int budget) {
    self->QueueHeader(budget);
    auto* head = *static_cast<HeaderNode**>(self->headers);
    HeaderNode* front = head->next;
    bool ok = front != head;
    if (ok) {
        std::uint16_t frontSeq;
        std::memcpy(&frontSeq, front->data, 2);
        for (HeaderNode* n = front->next; n != head; n = n->next) {
            std::uint16_t seq;
            std::memcpy(&seq, n->data, 2);
            ok &= static_cast<std::uint16_t>(seq - frontSeq) <= 0x8000;
        }
    }
    ei::verify::Check("Connection::QueueHeader", true, ok, "budget %d", budget);
}

// 00433F20: ours only. The history entry ends marked as rolled back.
void __fastcall RollBackVerify(Connection* self, void*, std::uint8_t* data) {
    self->RollBack(data);
    ei::verify::Check("Connection::RollBack", true, reinterpret_cast<HistoryNode*>(data - 8)->rolledBack == 1, "");
}

// 00435A00 / 00435F30: ours only (packets go out, records and handlers run).
bool __fastcall SendUpdateVerify(Connection* self, void*, int budget) {
    bool result = self->SendUpdate(budget);
    ei::verify::Check("Connection::SendUpdate", true, true, "");
    return result;
}
bool __fastcall ReadRecordVerify(Connection* self, void*, BufferCursor* cursor, std::uint32_t version) {
    std::uint8_t* before = cursor->p;
    bool result = self->ReadRecord(cursor, version);
    ei::verify::Check("Connection::ReadRecord", true, !result || cursor->p >= before + 3, "");
    return result;
}

// 00434110: ours only. A live object ends up tracked (byObject holds at most one more entry).
void __fastcall TrackVerify(Connection* self, void*, Replicated* item) {
    std::uint32_t before = self->byObject.count;
    auto* object = static_cast<CRefObject*>(item->object);
    bool live = object && !(object->flags7_ & CRefObject::kDetached);
    self->Track(item);
    std::uint32_t after = self->byObject.count;
    ei::verify::Check("Connection::Track", true, live ? (after == before || after == before + 1) : after == before, "");
}

// 004346D0: ours only. Afterwards every tracked entry's object is one the session listed: byObject
// has no more entries than the static list (0079B8D0).
void __fastcall CollectChangesVerify(Connection* self) {
    self->CollectChanges();
    auto* items = reinterpret_cast<ReplicatedVector*>(0x0079B8D0);
    ei::verify::Check("Connection::CollectChanges", true,
                      self->byObject.count <= static_cast<std::uint32_t>(items->end - items->begin), "");
}

// 004357C0: MOV EAX,ECX (2), MOV CL,[EAX+0xC] (3). The original on a copy of the cursor first (it
// writes the same byte to the same place as ours), then ours; the cursors must match.
void(__fastcall* s_putBit)(BufferCursor*, void*, std::uint32_t);
void __fastcall PutBitVerify(BufferCursor* self, void*, std::uint32_t bit) {
    BufferCursor copy = *self;
    s_putBit(&copy, nullptr, bit);
    self->PutBit(bit);
    ei::verify::Check("BufferCursor::PutBit", true, std::memcmp(&copy, self, sizeof(copy)) == 0, "bit %u", bit);
}

// 004355B0 / 00435820: ours only (ids are assigned, objects created). A reference takes 3 bytes,
// plus one when its bit starts a new byte.
void __fastcall WriteObjectRefVerify(Connection* self, void*, BufferCursor* cursor, CRefObject* object) {
    std::uint8_t* before = cursor->p;
    self->WriteObjectRef(cursor, object);
    std::ptrdiff_t used = cursor->p - before;
    ei::verify::Check("Connection::WriteObjectRef", true, used == 3 || used == 4, "%d bytes", static_cast<int>(used));
}
CRefObject* __fastcall ReadObjectRefVerify(Connection* self, void*, BufferCursor* cursor) {
    std::uint8_t* before = cursor->p;
    CRefObject* result = self->ReadObjectRef(cursor);
    std::ptrdiff_t used = cursor->p - before;
    ei::verify::Check("Connection::ReadObjectRef", true, used == 3 || used == 4, "%d bytes", static_cast<int>(used));
    return result;
}

} // namespace

EI_VERIFY(0x004357C0, PutBitVerify, 5, s_putBit);
EI_VERIFY(0x004355B0, WriteObjectRefVerify, 0, s_unused);
EI_VERIFY(0x00435820, ReadObjectRefVerify, 0, s_unused);
EI_VERIFY(0x00435F10, AllocateVerify, 5, s_allocate);
EI_VERIFY(0x00434C20, WriteAcksVerify, 5, s_writeAcks);
EI_VERIFY(0x00438930, TypeCreateVerify, 5, s_create);
EI_VERIFY(0x004352F0, AttachRecordVerify, 0, s_unused);
EI_VERIFY(0x00434990, QueueHeaderVerify, 0, s_unused);
EI_VERIFY(0x00433F20, RollBackVerify, 0, s_unused);
EI_VERIFY(0x00435A00, SendUpdateVerify, 0, s_unused);
EI_VERIFY(0x00435F30, ReadRecordVerify, 0, s_unused);
EI_VERIFY(0x00434110, TrackVerify, 0, s_unused);
EI_VERIFY(0x004346D0, CollectChangesVerify, 0, s_unused);

namespace {
// 00433BD0: ours only (it sends packets and changes the connection); the current connection is
// cleared again afterwards.
bool __fastcall TickVerify(net::Connection* self, void*, double dt) {
    bool alive = self->Tick(dt);
    ei::verify::Check("Connection::Tick", true, *reinterpret_cast<void**>(0x0079B924) == nullptr, "");
    return alive;
}
void* s_unusedTick;
} // namespace
EI_VERIFY(0x00433BD0, TickVerify, 0, s_unusedTick);

namespace {
// The receive side (00433FA0, 00434CE0, 00434D70, 00434E50, 00434AE0, 00436470): ours only (they
// change the connection's lists and streams).
void __fastcall DeliveredVerify(net::Connection* self, void*, std::uint8_t* historyData) {
    self->Delivered(historyData);
    ei::verify::Check("Connection::Delivered", true, true, "");
}
void __fastcall AcknowledgeVerify(net::Connection* self, void*, std::uint16_t sequence) {
    self->Acknowledge(sequence);
    ei::verify::Check("Connection::Acknowledge", true, true, "");
}
void __fastcall ReadAcksVerify(net::Connection* self, void*, BufferCursor* cursor) {
    std::uint8_t* before = cursor->p;
    self->ReadAcks(cursor);
    ei::verify::Check("Connection::ReadAcks", 6, static_cast<int>(cursor->p - before), "");
}
bool __fastcall AcceptVerify(net::Connection* self, void*, std::uint32_t sequence) {
    bool accepted = self->Accept(sequence);
    ei::verify::Check("Connection::Accept", true, !accepted || self->lastReceived >= sequence, "seq %u", sequence);
    return accepted;
}
void __fastcall ProcessHeadersVerify(net::Connection* self) {
    self->ProcessHeaders();
    ei::verify::Check("Connection::ProcessHeaders", true, true, "");
}
bool __fastcall ReadUpdateVerify(net::Connection* self, void*, BufferCursor* cursor) {
    bool ok = self->ReadUpdate(cursor);
    ei::verify::Check("Connection::ReadUpdate", true, *reinterpret_cast<void**>(0x0079B924) == nullptr, "");
    return ok;
}
void* s_unusedReceive;
} // namespace
EI_VERIFY(0x00433FA0, DeliveredVerify, 0, s_unusedReceive);
EI_VERIFY(0x00434CE0, AcknowledgeVerify, 0, s_unusedReceive);
EI_VERIFY(0x00434D70, ReadAcksVerify, 0, s_unusedReceive);
EI_VERIFY(0x00434E50, AcceptVerify, 0, s_unusedReceive);
EI_VERIFY(0x00434AE0, ProcessHeadersVerify, 0, s_unusedReceive);
EI_VERIFY(0x00436470, ReadUpdateVerify, 0, s_unusedReceive);
