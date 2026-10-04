// Message links over streams (net_link.h): the file stream 0073C8D8, the link base 0073C8A4 and the
// loopback link 0073C8F8 (00477EE0-004787A0). The virtual slots are free functions registered at
// their addresses (rewrite-guide rule 4). The queue's STLport list members are library code,
// called at their original addresses. See docs/notes/unmapped_410_470.md ("Tenth round").
#include "net_link.h"

#include "core_streams.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstring>
#include <new>

namespace netlink {

namespace {

constexpr std::uint32_t kStreamLimit = 2000000000;
constexpr unsigned kOpenWrite = 0x1001;   // CFile::modeCreate | modeWrite

// The embedded CFile's virtual slots (MFC 4.2): 3 GetPosition, 8 Open, 10 Seek, 12 GetLength,
// 13 Read, 14 Write, 19 Close. The original calls them through the CFile's own vtable.
constexpr int kFileGetPosition = 3;
constexpr int kFileOpen = 8;
constexpr int kFileSeek = 10;
constexpr int kFileGetLength = 12;
constexpr int kFileRead = 13;
constexpr int kFileWrite = 14;
constexpr int kFileClose = 19;

// The streams' slots: 0 deleting destructor, 7 copy from another stream.
constexpr int kStreamDelete = 0;
constexpr int kStreamCopyFrom = 7;

// --- Library (STLport list<CMemoryStream> of the queue) and other callees -----------------------
// List constructors (00478D50 / 00479150, RET 4: the allocator), insert of a default element
// (00478FF0, RET 8: out iterator, position), erase (004790F0, RET 8), clear (00479570), range
// insert (004795B0, RET 0xC: position, first, last); the node pool (00405950 / 00404D70) and the
// header's deallocation (00479530, n nodes).
__declspec(naked) void __fastcall QueueConstructA(QueueNode** /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00478D50)
__declspec(naked) void __fastcall QueueConstructB(QueueNode** /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00479150)
__declspec(naked) QueueNode** __fastcall QueueInsertDefault(QueueNode** /*list*/, void*, QueueNode** /*out*/, QueueNode* /*pos*/) EI_ORIGINAL(0x00478FF0)
__declspec(naked) QueueNode** __fastcall QueueErase(QueueNode** /*list*/, void*, QueueNode** /*out*/, QueueNode* /*pos*/) EI_ORIGINAL(0x004790F0)
__declspec(naked) void __fastcall QueueClear(QueueNode** /*list*/) EI_ORIGINAL(0x00479570)
__declspec(naked) void __fastcall QueueInsertRange(QueueNode** /*list*/, void*, QueueNode* /*pos*/, QueueNode* /*first*/, QueueNode* /*last*/) EI_ORIGINAL(0x004795B0)
__declspec(naked) void* __fastcall NodeAllocate(std::size_t /*bytes*/) EI_ORIGINAL(0x00405950)
__declspec(naked) void __fastcall NodeFree(void* /*p*/, std::size_t /*bytes*/) EI_ORIGINAL(0x00404D70)
__declspec(naked) void __fastcall NodesDeallocate(QueueNode* /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x00479530)
// 004A5250 (RET 4): writes a stream (its size as a 2- or 4-byte prefix, then its bytes) into the
// stream in ECX.
__declspec(naked) void __fastcall StreamWriteSized(void* /*to*/, void*, void* /*from*/) EI_ORIGINAL(0x004A5250)

void SetVtable(void* object, std::uintptr_t vtable) { std::memcpy(object, &vtable, sizeof(vtable)); }

bool Alive(const CRefObject* object) { return object && !(object->flags7_ & CRefObject::kDetached); }

// The counted pointer release the classes inline: the count dropped, the object deleted at zero.
void Release(CRefObject* object) {
    if (!object) return;
    --object->refs_;
    if (object->refs_ == 0) ei::VCall<void*>(object, 0, 1u);
}

// The file stream's destructor as the classes inline it.
void FileStreamDestructInline(FileStream* stream) {
    SetVtable(stream, kFileStreamVtable);
    reinterpret_cast<mfc::CFile*>(stream->file)->~CFile();
    SetVtable(stream, kStreamVtable);
}

// The queue's destructor as 004787A0 inlines it: every element's deleting destructor (flags 0),
// the node back to the pool; then the header.
void QueueDestroyInline(QueueNode** list) {
    QueueNode* header = *list;
    for (QueueNode* node = header->next; node != *list;) {
        QueueNode* current = node;
        node = node->next;
        ei::VCall<void*>(&current->stream, kStreamDelete, 0u);
        if (current) NodeFree(current, sizeof(QueueNode));
    }
    (*list)->next = *list;
    (*list)->prev = *list;
    if (*list) NodeFree(*list, sizeof(QueueNode));
}

// The base part of a new link (0073BB58, the file stream, 0073C8A4, not recording).
void LinkConstructInline(NetLink* self) {
    SetVtable(self, kRefValueVtable);
    FileStreamConstruct(&self->record);
    SetVtable(self, kLinkVtable);
    self->recording = 0;
}

// What a link does with a message it took in while recording: the first after Open is dropped,
// the others written to the file with their size.
void RecordMessage(NetLink* self, void* stream) {
    if (!self->recording) return;
    if (self->skipNext) {
        self->skipNext = 0;
        return;
    }
    StreamWriteSized(&self->record, nullptr, stream);
}

} // namespace

// ================================================================================================
// FileStream (0073C8D8)

// 00478470: CStream's fields (limit, ok), the CFile constructed, the vtable. Returns this.
FileStream* __fastcall FileStreamConstruct(FileStream* self) {
    SetVtable(self, kStreamVtable);
    self->limit = kStreamLimit;
    self->ok = 1;
    new (self->file) mfc::CFile();
    SetVtable(self, kFileStreamVtable);
    return self;
}
EI_REPLACE(0x00478470, FileStreamConstruct);

// 00477FB0: the CFile destroyed (closing an open file), CStream's vtable left.
void __fastcall FileStreamDestruct(FileStream* self) { FileStreamDestructInline(self); }
EI_REPLACE(0x00477FB0, FileStreamDestruct);

// 00477F90 (slot 0, RET 4).
void* __fastcall FileStreamDeletingDestructor(FileStream* self, void*, unsigned flags) {
    FileStreamDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00477F90, FileStreamDeletingDestructor);

// 00477F00 (slot 1, RET 8): reads through the CFile; a short read clears ok. Returns the count read.
unsigned __fastcall FileStreamRead(FileStream* self, void*, void* buffer, unsigned count) {
    unsigned read = ei::VCall<unsigned>(self->file, kFileRead, buffer, count);
    self->ok = static_cast<std::uint8_t>(self->ok & (read == count ? 1 : 0));
    return read;
}
EI_REPLACE(0x00477F00, FileStreamRead);

// 00477F30 (slot 2, RET 8).
void __fastcall FileStreamWrite(FileStream* self, void*, const void* buffer, unsigned count) {
    ei::VCall<void>(self->file, kFileWrite, buffer, count);
}
EI_REPLACE(0x00477F30, FileStreamWrite);

// 00477F50 (slot 3, RET 4): seeks from the start; returns the CFile's result.
long __fastcall FileStreamSeek(FileStream* self, void*, long position) {
    return ei::VCall<long>(self->file, kFileSeek, position, 0u);
}
EI_REPLACE(0x00477F50, FileStreamSeek);

// 00477F70 (slot 4): the file's length.
unsigned __fastcall FileStreamSize(FileStream* self) { return ei::VCall<unsigned>(self->file, kFileGetLength); }
EI_REPLACE(0x00477F70, FileStreamSize);

// 00477F80 (slot 5): the file's position.
unsigned __fastcall FileStreamPosition(FileStream* self) { return ei::VCall<unsigned>(self->file, kFileGetPosition); }
EI_REPLACE(0x00477F80, FileStreamPosition);

// 00477EE0 (slot 6): ok again, back to the start; returns the seek's result.
long __fastcall FileStreamRewind(FileStream* self) {
    self->ok = 1;
    return ei::VCall<long>(self->file, kFileSeek, 0L, 0u);
}
EI_REPLACE(0x00477EE0, FileStreamRewind);

// ================================================================================================
// NetLink (0073C8A4)

// 00478050: the file stream destroyed, the root vtable left (the record's own vtable is not set).
void __fastcall LinkDestruct(NetLink* self) {
    FileStreamDestructInline(&self->record);
    SetVtable(self, kRefObjectRootVtable);
}
EI_REPLACE(0x00478050, LinkDestruct);

// 00478030 (slot 0, RET 4).
void* __fastcall LinkDeletingDestructor(NetLink* self, void*, unsigned flags) {
    LinkDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00478030, LinkDeletingDestructor);

// 00478000 (slot 7): 1 when slot 6 says a message is waiting, else 0.
int __fastcall LinkIncomingCount(NetLink* self) { return ei::VCall<bool>(self, 6) ? 1 : 0; }
EI_REPLACE(0x00478000, LinkIncomingCount);

// 00478100 (slot 8, RET 4): the base link drops what it is sent.
bool __fastcall LinkSend(NetLink*, void*, void*) { return true; }
EI_REPLACE(0x00478100, LinkSend);

// 00478110 (slot 9, RET 4): records the stream (when recording); always true.
bool __fastcall LinkRecord(NetLink* self, void*, void* stream) {
    RecordMessage(self, stream);
    return true;
}
EI_REPLACE(0x00478110, LinkRecord);

// 004780B0 (RET 4): starts recording into a new file `path` (the CFile's virtual Open with
// modeCreate | modeWrite); the stream's ok tells whether it opened. The first message is dropped.
void __fastcall LinkOpenRecording(NetLink* self, void*, const char* path) {
    self->record.reading = 0;
    int opened = ei::VCall<int>(self->record.file, kFileOpen, path, kOpenWrite, static_cast<void*>(nullptr));
    self->record.ok = opened != 0 ? 1 : 0;
    self->skipNext = 1;
    self->recording = 1;
}
EI_REPLACE(0x004780B0, LinkOpenRecording);

// 004780E0: closes the file (virtual Close) and stops recording.
void __fastcall LinkCloseRecording(NetLink* self) {
    ei::VCall<void>(self->record.file, kFileClose);
    self->recording = 0;
}
EI_REPLACE(0x004780E0, LinkCloseRecording);

// ================================================================================================
// LoopbackLink (0073C8F8)

namespace {
// A new loopback link (operator new, its count cleared before the null check, as the original);
// built as 004782E0 inlines it. Null when the allocation fails.
LoopbackLink* NewLoopback() {
    auto* self = static_cast<LoopbackLink*>(gamecrt::operator_new(sizeof(LoopbackLink)));
    reinterpret_cast<std::uint32_t*>(self)[1] = 0;
    if (!self) return nullptr;
    RefValueConstructBase(self);
    FileStreamConstruct(&self->base.record);
    SetVtable(self, kLinkVtable);
    self->base.recording = 0;
    std::uint8_t alloc = 0;
    QueueConstructA(&self->queue, nullptr, &alloc);
    self->peer = nullptr;   // 00478FB0, the null counted pointer
    SetVtable(self, kLoopbackVtable);
    return self;
}

// A counted pointer assignment as 004782E0 inlines it (the new object counted first).
void AssignPeer(CRefObject** slot, CRefObject* object) {
    CRefObject* previous = *slot;
    *slot = object;
    if (object) ++object->refs_;
    Release(previous);
}
} // namespace

// 004782E0 (thiscall with an unused this, RET 4): two loopback links joined to each other; the
// first goes to the player's link (+8), the second is returned (null for a null player). Without
// memory for the second the original crashes; so does this.
LoopbackLink* __fastcall LoopbackCreatePair(void*, void*, std::uint8_t* player) {
    if (!player) return nullptr;
    auto*& held = *reinterpret_cast<CRefObject**>(player + 8);
    LoopbackLink* first = NewLoopback();
    CRefObject* previous = held;
    held = reinterpret_cast<CRefObject*>(first);
    if (first) ++reinterpret_cast<CRefObject*>(first)->refs_;
    if (previous) previous->Release();
    LoopbackLink* second = NewLoopback();
    AssignPeer(&reinterpret_cast<LoopbackLink*>(held)->peer, reinterpret_cast<CRefObject*>(second));
    AssignPeer(&second->peer, held);
    return second;
}
EI_REPLACE(0x004782E0, LoopbackCreatePair);

// 004787A0: the peer released, the queue destroyed, then the base's file stream; the root vtable
// left.
void __fastcall LoopbackDestruct(LoopbackLink* self) {
    SetVtable(self, kLoopbackVtable);
    Release(self->peer);
    QueueDestroyInline(&self->queue);
    FileStreamDestructInline(&self->base.record);
    SetVtable(self, kRefObjectRootVtable);
}
EI_REPLACE(0x004787A0, LoopbackDestruct);

// 00478780 (slot 0, RET 4).
void* __fastcall LoopbackDeletingDestructor(LoopbackLink* self, void*, unsigned flags) {
    LoopbackDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00478780, LoopbackDeletingDestructor);

// 00478610 (slot 2): destroyed and built again in place (the reference count and +9 kept): no
// peer, an empty queue, a closed file, not recording.
void __fastcall LoopbackReset(LoopbackLink* self) {
    SetVtable(self, kLoopbackVtable);
    Release(self->peer);
    QueueClear(&self->queue);
    NodesDeallocate(self->queue, 1);
    FileStreamDestructInline(&self->base.record);
    SetVtable(self, kRefObjectRootVtable);
    if (!self) return;
    LinkConstructInline(&self->base);
    std::uint8_t alloc = 0;
    QueueConstructB(&self->queue, nullptr, &alloc);
    self->peer = nullptr;
    SetVtable(self, kLoopbackVtable);
}
EI_REPLACE(0x00478610, LoopbackReset);

// 004784D0 (slot 3): a copy with the recording flags, a closed file of its own, a copy of every
// queued stream and the same peer (counted). Null when the allocation fails.
LoopbackLink* __fastcall LoopbackClone(LoopbackLink* self) {
    auto* copy = static_cast<LoopbackLink*>(gamecrt::operator_new(sizeof(LoopbackLink)));
    reinterpret_cast<std::uint32_t*>(copy)[1] = 0;
    if (!copy) return nullptr;
    SetVtable(copy, kRefValueVtable);
    copy->base.recording = self->base.recording;
    copy->base.skipNext = self->base.skipNext;
    FileStreamConstruct(&copy->base.record);
    SetVtable(copy, kLinkVtable);
    copy->queue = nullptr;
    QueueNode* header = static_cast<QueueNode*>(NodeAllocate(sizeof(QueueNode)));
    copy->queue = header;
    header->next = header;
    copy->queue->prev = copy->queue;
    QueueInsertRange(&copy->queue, nullptr, copy->queue->next, self->queue->next, self->queue);
    copy->peer = self->peer;
    if (copy->peer) ++copy->peer->refs_;
    SetVtable(copy, kLoopbackVtable);
    return copy;
}
EI_REPLACE(0x004784D0, LoopbackClone);

// 00478710 (slot 4): whether the peer is there and not detached.
int __fastcall LoopbackConnected(LoopbackLink* self) { return Alive(self->peer) ? 1 : 0; }
EI_REPLACE(0x00478710, LoopbackConnected);

// 00478730 (slot 5): something is still queued, or the peer is there.
int __fastcall LoopbackAlive(LoopbackLink* self) {
    if (self->queue->next != self->queue) return 1;
    return Alive(self->peer) ? 1 : 0;
}
EI_REPLACE(0x00478730, LoopbackAlive);

// 00478750 (slot 6): whether a message is queued.
bool __fastcall LoopbackHasIncoming(LoopbackLink* self) { return self->queue->next != self->queue; }
EI_REPLACE(0x00478750, LoopbackHasIncoming);

// 00478760 (slot 7): the number of queued messages.
int __fastcall LoopbackIncomingCount(LoopbackLink* self) {
    int count = 0;
    for (QueueNode* node = self->queue->next; node != self->queue; node = node->next) ++count;
    return count;
}
EI_REPLACE(0x00478760, LoopbackIncomingCount);

// 00478220 (slot 8, RET 4): a new empty stream at the end of the peer's queue takes a copy of
// `stream` (its slot 7). False without a live peer.
bool __fastcall LoopbackSend(LoopbackLink* self, void*, void* stream) {
    CRefObject* peer = self->peer;
    if (!Alive(peer)) return false;
    auto* target = reinterpret_cast<LoopbackLink*>(peer);
    QueueNode* inserted;
    QueueInsertDefault(&target->queue, nullptr, &inserted, target->queue);
    QueueNode* last = reinterpret_cast<LoopbackLink*>(self->peer)->queue->prev;
    ei::VCall<void>(&last->stream, kStreamCopyFrom, stream);
    return true;
}
EI_REPLACE(0x00478220, LoopbackSend);

// 00478270 (slot 9, RET 4): the front of the queue copied into `stream` (its slot 7) and dropped,
// then recorded as the base does. False when nothing is queued.
bool __fastcall LoopbackReceive(LoopbackLink* self, void*, void* stream) {
    if (self->queue->next == self->queue) return false;
    ei::VCall<void>(stream, kStreamCopyFrom, static_cast<void*>(&self->queue->next->stream));
    QueueNode* next;
    QueueErase(&self->queue, nullptr, &next, self->queue->next);
    RecordMessage(&self->base, stream);
    return true;
}
EI_REPLACE(0x00478270, LoopbackReceive);

// 00478140 (slot 11): lets go of the peer, then has the peer let go of this one (a reference held
// across its slot 11).
void __fastcall LoopbackDisconnect(LoopbackLink* self) {
    CRefObject* peer = self->peer;
    if (peer) ++peer->refs_;
    CRefObject* previous = self->peer;
    self->peer = nullptr;
    Release(previous);
    if (peer) ei::VCall<void>(peer, 11);
    Release(peer);
}
EI_REPLACE(0x00478140, LoopbackDisconnect);

} // namespace netlink
