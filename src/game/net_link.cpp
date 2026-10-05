// Message links over streams (net_link.h): the file stream 0073C8D8, the link base 0073C8A4, the
// loopback link 0073C8F8, the replay link 0073C92C, the framed link 0073C9C8 and the per-player
// driver 0073C990 (00477EE0-0047BC10). The virtual slots are free functions registered at their
// addresses (rewrite-guide rule 4). The queue's STLport list members are library code, called at
// their original addresses. See docs/notes/unmapped_410_470.md ("Tenth round", "Eleventh round").
#include "net_link.h"

#include "core_streams.h"
#include "net_connection.h"

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

// ================================================================================================
// ReplayLink (0073C92C)

namespace {
constexpr unsigned kOpenRead = 0;   // CFile::modeRead
// The file stream's slots 4 Size and 5 Position.
constexpr int kFileStreamSize = 4;
constexpr int kFileStreamPosition = 5;

// 004A51C0 (RET 4): reads a stream written by 004A5250 (size prefix, then the bytes) from the
// stream in ECX into `to`, which is emptied first (its slot 6).
__declspec(naked) void __fastcall StreamReadSized(void* /*from*/, void*, void* /*to*/) EI_ORIGINAL(0x004A51C0)

// A new replay link in place as 00478960 / 00478B10 inline it: the base (not recording), then
// the second file stream.
void ReplayConstructInline(ReplayLink* self) {
    LinkConstructInline(&self->base);
    FileStreamConstruct(&self->replay);
    SetVtable(self, kReplayVtable);
}
} // namespace

// 00478960 (thiscall with an unused this, RET 4): a replay link reading the recording `path`
// (the CFile's virtual Open, modeRead); the stream's ok tells whether it opened. Without memory
// the original crashes (on the reference count's write before the null check); so does this.
ReplayLink* __fastcall ReplayCreate(void*, void*, const char* path) {
    auto* self = static_cast<ReplayLink*>(gamecrt::operator_new(sizeof(ReplayLink)));
    reinterpret_cast<std::uint32_t*>(self)[1] = 0;
    if (self) ReplayConstructInline(self);
    self->replay.reading = 1;
    int opened = ei::VCall<int>(self->replay.file, kFileOpen, path, kOpenRead, static_cast<void*>(nullptr));
    self->replay.ok = opened != 0 ? 1 : 0;
    return self;
}
EI_REPLACE(0x00478960, ReplayCreate);

// 00478CC0: both file streams destroyed (the replay's first), the root vtable left.
void __fastcall ReplayDestruct(ReplayLink* self) {
    SetVtable(self, kReplayVtable);
    FileStreamDestructInline(&self->replay);
    FileStreamDestructInline(&self->base.record);
    SetVtable(self, kRefObjectRootVtable);
}
EI_REPLACE(0x00478CC0, ReplayDestruct);

// 00478CA0 (slot 0, RET 4).
void* __fastcall ReplayDeletingDestructor(ReplayLink* self, void*, unsigned flags) {
    ReplayDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00478CA0, ReplayDeletingDestructor);

// 00478B10 (slot 2): destroyed and built again in place (the reference count and +9 kept): two
// closed files, not recording.
void __fastcall ReplayReset(ReplayLink* self) {
    ReplayDestruct(self);
    if (!self) return;
    ReplayConstructInline(self);
}
EI_REPLACE(0x00478B10, ReplayReset);

// 00478A50 (slot 3): a copy with the recording flags and two closed files of its own. Null when
// the allocation fails.
ReplayLink* __fastcall ReplayClone(ReplayLink* self) {
    auto* copy = static_cast<ReplayLink*>(gamecrt::operator_new(sizeof(ReplayLink)));
    reinterpret_cast<std::uint32_t*>(copy)[1] = 0;
    if (!copy) return nullptr;
    SetVtable(copy, kRefValueVtable);
    copy->base.recording = self->base.recording;
    copy->base.skipNext = self->base.skipNext;
    FileStreamConstruct(&copy->base.record);
    SetVtable(copy, kLinkVtable);
    FileStreamConstruct(&copy->replay);
    SetVtable(copy, kReplayVtable);
    return copy;
}
EI_REPLACE(0x00478A50, ReplayClone);

// 00478C30 (slot 5) and 00478C50 (slot 6, the same code): whether the recording has more to read
// (position, then size, through the file stream's slots).
int __fastcall ReplayAlive(ReplayLink* self) {
    unsigned position = ei::VCall<unsigned>(&self->replay, kFileStreamPosition);
    unsigned size = ei::VCall<unsigned>(&self->replay, kFileStreamSize);
    return position < size ? 1 : 0;
}
EI_REPLACE(0x00478C30, ReplayAlive);

int __fastcall ReplayHasIncoming(ReplayLink* self) { return ReplayAlive(self); }
EI_REPLACE(0x00478C50, ReplayHasIncoming);

// 00478C70 (slot 9, RET 4): the next recorded message into `stream`; the replay stream's ok.
bool __fastcall ReplayReceive(ReplayLink* self, void*, void* stream) {
    StreamReadSized(&self->replay, nullptr, stream);
    return self->replay.ok != 0;
}
EI_REPLACE(0x00478C70, ReplayReceive);

// 00478C90 (slot 11): closes the recording (the CFile's virtual Close).
void __fastcall ReplayDisconnect(ReplayLink* self) { ei::VCall<void>(self->replay.file, kFileClose); }
EI_REPLACE(0x00478C90, ReplayDisconnect);

// ================================================================================================
// FramedLink (0073C9C8)

namespace {
// CMemoryStream's slots: 1 Read, 2 Write, 3 Seek, 4 Size, 5 Position, 6 Clear.
constexpr int kStreamRead = 1;
constexpr int kStreamWrite = 2;
constexpr int kStreamSeek = 3;
constexpr int kStreamSize = 4;
constexpr int kStreamPosition = 5;
constexpr int kStreamClear = 6;
// The link's slots 11 Disconnect and 13 (the driver: bytes to the connection).
constexpr int kLinkDisconnect = 11;
constexpr int kLinkSendBytes = 13;
// GameBuffer's slots 4 (write past the end, growing) and 6 Seek.
constexpr int kBufferWriteMore = 4;
constexpr int kBufferSeek = 6;
// The largest message Send takes (its stack buffer holds the length and the bytes).
constexpr unsigned kMaxMessage = 0x7FFC;

// CMemoryStream members called directly: Seek 004A5490 (slot 3), Resize 004A54D0 (size, position
// 0; grows to 1.5x + 0x10) and SetSize 004A5570 (grows to the exact size; position 0).
__declspec(naked) void __fastcall MemoryStreamSeek(CMemoryStream* /*stream*/, void*, std::uint32_t /*position*/) EI_ORIGINAL(0x004A5490)
__declspec(naked) void __fastcall MemoryStreamResize(CMemoryStream* /*stream*/, void*, std::uint32_t /*size*/) EI_ORIGINAL(0x004A54D0)
__declspec(naked) void __fastcall MemoryStreamSetSize(CMemoryStream* /*stream*/, void*, std::uint32_t /*size*/) EI_ORIGINAL(0x004A5570)
// The game's memmove (006E79A0).
__declspec(naked) void* __cdecl GameMemmove(void* /*to*/, const void* /*from*/, std::size_t /*n*/) EI_ORIGINAL(0x006E79A0)

std::int32_t Addr(const std::uint8_t* p) { return static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(p)); }

net::Connection* ConnectionOf(CRefObject* player) { return reinterpret_cast<net::Connection*>(player); }

// The number of bytes a reliable stream holds (its high-water mark raised first), as inlined.
unsigned BufferFill(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
    return static_cast<unsigned>(b->base - Addr(b->begin) + Addr(b->high));
}

// A write the way these functions inline it: in place (marking the buffer written) when it fits
// before `end`, otherwise through slot 4.
void BufferPut(GameBuffer* b, const void* from, unsigned size) {
    if (b->cur + size > b->end) {
        ei::VCall<int>(b, kBufferWriteMore, from, size);
        return;
    }
    b->flags |= 1;
    std::memcpy(b->cur, from, size);
    b->cur += size;
}

// The framed link's destructor body (00479B90) as the driver inlines it: the inbox's buffer
// freed, then the base's file stream; the root vtable left.
void FramedDestructInline(FramedLink* self) {
    SetVtable(&self->inbox, kMemoryStreamVtable);
    if (self->inbox.data_) gamecrt::operator_delete(self->inbox.data_);
    SetVtable(&self->inbox, kStreamVtable);
    FileStreamDestructInline(&self->base.record);
    SetVtable(self, kRefObjectRootVtable);
}
} // namespace

// 00479B10 (RET 4): a fresh file stream; the stream copied from is not read (the copied link
// gets a closed file of its own).
FileStream* __fastcall FileStreamCopyConstruct(FileStream* self, void*, const FileStream*) { return FileStreamConstruct(self); }
EI_REPLACE(0x00479B10, FileStreamCopyConstruct);

// 00479B90: the destructor body (its own vtable is not set first).
void __fastcall FramedDestruct(FramedLink* self) { FramedDestructInline(self); }
EI_REPLACE(0x00479B90, FramedDestruct);

// 00479B70 (slot 0, RET 4).
void* __fastcall FramedDeletingDestructor(FramedLink* self, void*, unsigned flags) {
    FramedDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00479B70, FramedDeletingDestructor);

// 0047B980 (RET 8): `size` bytes appended at the end of the inbox.
void __fastcall FramedAppend(FramedLink* self, void*, const void* data, unsigned size) {
    CMemoryStream* inbox = &self->inbox;
    unsigned end = ei::VCall<unsigned>(inbox, kStreamSize);
    ei::VCall<void>(inbox, kStreamSeek, end);
    ei::VCall<void>(inbox, kStreamWrite, data, size);
}
EI_REPLACE(0x0047B980, FramedAppend);

// 0047B9B0 (RET 4): `out` emptied (its slot 6), then given everything in the inbox, which is
// emptied when it held anything.
void __fastcall FramedTakeAll(FramedLink* self, void*, void* out) {
    ei::VCall<void>(out, kStreamClear);
    CMemoryStream* inbox = &self->inbox;
    if (ei::VCall<unsigned>(inbox, kStreamSize) == 0) return;
    const void* data = inbox->data_;
    unsigned size = ei::VCall<unsigned>(inbox, kStreamSize);
    ei::VCall<void>(out, kStreamWrite, data, size);
    ei::VCall<void>(inbox, kStreamClear);
}
EI_REPLACE(0x0047B9B0, FramedTakeAll);

// 0047B9F0 (slot 6): whether the inbox holds a whole message (a length and that many bytes).
int __fastcall FramedHasMessage(FramedLink* self) {
    CMemoryStream* inbox = &self->inbox;
    if (ei::VCall<unsigned>(inbox, kStreamSize) < 4) return 0;
    ei::VCall<void>(inbox, kStreamSeek, 0u);
    std::uint32_t length = 0;   // the original's slot holds the pushed ECX; the read always fills it
    ei::VCall<unsigned>(inbox, kStreamRead, static_cast<void*>(&length), 4u);
    unsigned size = ei::VCall<unsigned>(inbox, kStreamSize);
    return size >= length + 4 ? 1 : 0;
}
EI_REPLACE(0x0047B9F0, FramedHasMessage);

// 0047BA40 (slot 7): the number of whole messages in the inbox, counted from the start; stops at
// a length that runs past the end or is not positive.
int __fastcall FramedMessageCount(FramedLink* self) {
    CMemoryStream* inbox = &self->inbox;
    int count = 0;
    ei::VCall<void>(inbox, kStreamSeek, 0u);
    for (;;) {
        unsigned at = ei::VCall<unsigned>(inbox, kStreamPosition) + 4;
        if (at > ei::VCall<unsigned>(inbox, kStreamSize)) break;
        std::int32_t length = 0;   // as in 0047B9F0: always filled
        ei::VCall<unsigned>(inbox, kStreamRead, static_cast<void*>(&length), 4u);
        unsigned end = ei::VCall<unsigned>(inbox, kStreamPosition) + static_cast<unsigned>(length);
        if (end > ei::VCall<unsigned>(inbox, kStreamSize)) break;
        if (length <= 0) break;
        unsigned next = ei::VCall<unsigned>(inbox, kStreamPosition) + static_cast<unsigned>(length);
        ei::VCall<void>(inbox, kStreamSeek, next);
        ++count;
    }
    return count;
}
EI_REPLACE(0x0047BA40, FramedMessageCount);

// 0047BAD0 (slot 8, RET 4): the message framed (its length, then its bytes, read from the start)
// in a stack buffer and handed to slot 13; when that fails the link disconnects (slot 11). False
// for a message over 0x7FFC bytes or a failed send.
bool __fastcall FramedSend(FramedLink* self, void*, void* stream) {
    std::uint8_t buffer[kMaxMessage + 4];
    if (ei::VCall<unsigned>(stream, kStreamSize) > kMaxMessage) return false;
    std::uint32_t size = ei::VCall<unsigned>(stream, kStreamSize);
    std::memcpy(buffer, &size, sizeof(size));
    ei::VCall<void>(stream, kStreamSeek, 0u);
    unsigned count = ei::VCall<unsigned>(stream, kStreamSize);
    ei::VCall<unsigned>(stream, kStreamRead, static_cast<void*>(buffer + 4), count);
    unsigned total = ei::VCall<unsigned>(stream, kStreamSize) + 4;
    if (ei::VCall<bool>(self, kLinkSendBytes, static_cast<const void*>(buffer), total)) return true;
    ei::VCall<void>(self, kLinkDisconnect);
    return false;
}
EI_REPLACE(0x0047BAD0, FramedSend);

// 0047BB70 (slot 9, RET 4): the first message's bytes into `stream` (resized to them), the rest of
// the inbox moved to its start. False when no whole message is there.
bool __fastcall FramedReceive(FramedLink* self, void*, CMemoryStream* stream) {
    CMemoryStream* inbox = &self->inbox;
    std::uint32_t length = 0;
    ei::VCall<void>(inbox, kStreamSeek, 0u);
    ei::VCall<unsigned>(inbox, kStreamRead, static_cast<void*>(&length), 4u);
    if (ei::VCall<unsigned>(inbox, kStreamSize) < length + 4) return false;
    MemoryStreamResize(stream, nullptr, length);
    ei::VCall<unsigned>(inbox, kStreamRead, static_cast<void*>(stream->data_), length);
    unsigned rest = ei::VCall<unsigned>(inbox, kStreamSize) - 4 - length;
    std::uint8_t* data = inbox->data_;
    GameMemmove(data, data + length + 4, rest);
    MemoryStreamResize(inbox, nullptr, rest);
    return true;
}
EI_REPLACE(0x0047BB70, FramedReceive);

} // namespace netlink

// ================================================================================================
// DriverLink (0073C990)

namespace netsession {
void* __fastcall DriverConstruct(std::uint8_t* self);   // 0047A030 (net_session_pump.cpp)
} // namespace netsession

namespace netlink {

// 00479810 (slot 11): the inbox's unread bytes, with the player's incoming reliable bytes after
// them, written back over the start of that stream (when there were any and the player is
// alive); then the player let go.
void __fastcall DriverDisconnect(DriverLink* self) {
    CMemoryStream pending;   // the original's stack stream: +4, +6, +7 and +0xC never written
    pending.version_ = kStreamLimit;
    pending.ok_ = 1;
    SetVtable(&pending, kMemoryStreamVtable);
    pending.data_ = nullptr;
    pending.Reset();
    FramedTakeAll(&self->base, nullptr, &pending);
    if (pending.size_ != 0 && Alive(self->player)) {
        MemoryStreamSeek(&pending, nullptr, pending.size_);
        unsigned count = BufferFill(&ConnectionOf(self->player)->reliableIn_);
        ei::VCall<void>(&pending, kStreamWrite, static_cast<const void*>(ConnectionOf(self->player)->reliableIn_.begin), count);
        ei::VCall<int>(&ConnectionOf(self->player)->reliableIn_, kBufferSeek, 0);
        BufferPut(&ConnectionOf(self->player)->reliableIn_, pending.data_, pending.size_);
    }
    CRefObject* player = self->player;
    self->player = nullptr;
    Release(player);
    SetVtable(&pending, kMemoryStreamVtable);
    if (pending.data_) gamecrt::operator_delete(pending.data_);
}
EI_REPLACE(0x00479810, DriverDisconnect);

// 00479D40: disconnected (00479810, called directly), then the framed link's destructor inlined.
void __fastcall DriverDestruct(DriverLink* self) {
    SetVtable(self, kDriverVtable);
    DriverDisconnect(self);
    Release(self->player);
    FramedDestructInline(&self->base);
}
EI_REPLACE(0x00479D40, DriverDestruct);

// 00479D20 (slot 0, RET 4).
void* __fastcall DriverDeletingDestructor(DriverLink* self, void*, unsigned flags) {
    DriverDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00479D20, DriverDeletingDestructor);

// 00479C10 (slot 2): destroyed (disconnecting) and built again in place with the constructor's
// code (0047A030): the reference count and +9 kept, no player, an empty inbox, a closed file.
void __fastcall DriverReset(DriverLink* self) {
    DriverDestruct(self);
    if (!self) return;
    netsession::DriverConstruct(reinterpret_cast<std::uint8_t*>(self));
}
EI_REPLACE(0x00479C10, DriverReset);

// 00479A30 (slot 3): a copy with the recording flags, a closed file of its own, an inbox of the
// same size and the same player (counted). Null when the allocation fails. The original reads the
// copy's new buffer (never written) into the source's inbox instead of the other way round: the
// copy's bytes are whatever the allocation held and the source's are overwritten with them. Kept.
DriverLink* __fastcall DriverClone(DriverLink* self) {
    auto* copy = static_cast<DriverLink*>(gamecrt::operator_new(sizeof(DriverLink)));
    reinterpret_cast<std::uint32_t*>(copy)[1] = 0;
    if (!copy) return nullptr;
    SetVtable(copy, kRefValueVtable);
    copy->base.base.recording = self->base.base.recording;
    copy->base.base.skipNext = self->base.base.skipNext;
    FileStreamCopyConstruct(&copy->base.base.record, nullptr, &self->base.base.record);
    SetVtable(copy, kLinkVtable);
    CMemoryStream* inbox = &copy->base.inbox;
    inbox->version_ = kStreamLimit;
    inbox->ok_ = 1;
    SetVtable(inbox, kMemoryStreamVtable);
    inbox->data_ = nullptr;
    inbox->Reset();
    MemoryStreamSetSize(inbox, nullptr, self->base.inbox.size_);
    ei::VCall<unsigned>(inbox, kStreamRead, static_cast<void*>(self->base.inbox.data_), inbox->size_);
    SetVtable(copy, kFramedVtable);
    copy->player = self->player;
    if (copy->player) ++copy->player->refs_;
    SetVtable(copy, kDriverVtable);
    return copy;
}
EI_REPLACE(0x00479A30, DriverClone);

// 00479780 (slot 4): whether the player is there and not detached.
int __fastcall DriverConnected(DriverLink* self) { return Alive(self->player) ? 1 : 0; }
EI_REPLACE(0x00479780, DriverConnected);

// 004797A0 (slot 10): the player's incoming reliable bytes appended to the inbox (0047B980), and
// that stream emptied (flags 6).
void __fastcall DriverPump(DriverLink* self) {
    CRefObject* player = self->player;
    if (!Alive(player)) return;
    GameBuffer* in = &ConnectionOf(player)->reliableIn_;
    unsigned count = BufferFill(in);
    FramedAppend(&self->base, nullptr, in->begin, count);
    in->flags = 6;
    in->high = in->begin;
    in->cur = in->begin;
}
EI_REPLACE(0x004797A0, DriverPump);

// 004796B0 (slot 13, RET 8): `size` bytes written at the end of the player's outgoing reliable
// stream (when the player is alive); whether the player is (still) alive.
int __fastcall DriverSendBytes(DriverLink* self, void*, const void* data, unsigned size) {
    if (Alive(self->player)) {
        unsigned fill = BufferFill(&ConnectionOf(self->player)->reliableOut_);
        ei::VCall<int>(&ConnectionOf(self->player)->reliableOut_, kBufferSeek, fill);
        BufferPut(&ConnectionOf(self->player)->reliableOut_, data, size);
    }
    return Alive(self->player) ? 1 : 0;
}
EI_REPLACE(0x004796B0, DriverSendBytes);

} // namespace netlink
