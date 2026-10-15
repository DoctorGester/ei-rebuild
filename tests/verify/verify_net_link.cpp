// Checks for net_link.cpp (the file stream 0073C8D8, the link base 0073C8A4, the loopback link
// 0073C8F8).
//
// The embedded CFile, recorded and replayed: ours runs on the real object with the CFile's vtable
// pointer swapped for a recorder that forwards each call to the real CFile (its own vtable put
// back for the call) and logs slot, arguments, result and the bytes written. The original then
// runs on a byte copy of the object taken before, whose CFile answers from the log: the same calls
// in the same order with the same arguments and bytes, the logged results handed back. The file
// stream's slots 1-6, the link's recording Open / Close / Record, a recording Receive and the
// replay link's slots use it. Calls nest (the record writes through the file stream's slots,
// whose wrappers then run ours on whichever CFile they are given). The bytes read are logged and
// copied into the replay's buffer (the same one for the file stream's Read, a fresh one for
// 004A51C0).
//
// Memory streams the functions touch (only the two CMemoryStream classes) are deep-copied for the
// original's run: header plus a private buffer from the game heap; compared field by field (the
// buffer pointer excepted) and up to the size. Ours runs first: the streams' bytes are kept in our
// memory before it and the copies made after it (the game reads the heap's address order, SV2f). The queue's streams are compared without +4, +6,
// +7 and +0xC, which their copy constructor never writes (pool leftovers).
//
//  - FileStream constructor: the original on the real object, ours on a copy taken before (the
//    CFile constructor allocates nothing). Destructor bodies of the file stream and the link: with
//    a closed CFile (no handle, the shared empty name: nothing to release) the original on a copy,
//    ours on the real object; else ours only.
//  - Link slots 7 / 8 and the loopback's queries (slots 4-7): side by side on the real object
//    (00478000 has a call in its first bytes: a copy of its 12-byte body runs as the original).
//  - Loopback Send: the original on a copy of the link whose peer is a copy of the peer with an
//    empty scratch queue (pool nodes), with a deep copy of the stream; ours on the real objects;
//    the peer's new last stream, the stream and the result compared. Receive: the original on a
//    copy of the link whose queue is a scratch list holding a deep copy of the front stream,
//    reading into a deep copy of the target; the target, the queue and the flags compared.
//  - Clone: both clone; the copies compared (queue streams one by one), the original's deleted.
//  - Pair creation, reset, disconnect, the loopback's destructor and the deleting destructors:
//    ours only, the resulting links checked.
//  - The replay link (0073C92C): Create ours only (it opens the recording; a second modeRead open
//    would be refused by the share mode), the link checked; Clone both, compared, the original's
//    deleted; Reset and the deleting destructor ours only; the destructor body as the link's;
//    Alive / HasIncoming / Receive / Disconnect through the recorded CFile (Receive with a deep
//    copy of the target stream).
#include "verify.h"

#include "../../src/game/net_link.h"
#include "../../src/lib/crt.h"

#include "ei/vcall.h"

#include <cstddef>
#include <cstring>
#include <vector>

#include <windows.h>

namespace {

using namespace netlink;

void* s_unused;

template <typename T>
T At(const void* p, std::size_t offset) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, sizeof(T));
    return v;
}
std::uintptr_t VtableOf(const void* p) { return At<std::uintptr_t>(p, 0); }
void SetVtablePtr(void* p, const void* vtable) { std::memcpy(p, &vtable, sizeof(vtable)); }

// --- Library callees (the queue's list, the node pool) -------------------------------------------
__declspec(naked) void* __fastcall NodeAllocate(std::size_t /*bytes*/) EI_ORIGINAL(0x00405950)
__declspec(naked) void __fastcall NodeFree(void* /*p*/, std::size_t /*bytes*/) EI_ORIGINAL(0x00404D70)
__declspec(naked) void __fastcall QueueClear(QueueNode** /*list*/) EI_ORIGINAL(0x00479570)

// --- Memory streams -------------------------------------------------------------------------------
bool IsMemoryStream(const void* s) {
    std::uintptr_t v = VtableOf(s);
    return v == 0x0073BD90 || v == 0x0073C960;
}
void FreeStream(CMemoryStream* s) {
    if (s->data_) gamecrt::operator_delete(s->data_);
    s->data_ = nullptr;
}
// A stream's fields and bytes kept in our memory before ours runs; CloneTo makes the deep copy
// (as CloneStream) after ours ran, so that ours' blocks land where the original's would without
// the check (the game reads the heap's address order, SV2f).
struct KeptStream {
    alignas(8) std::uint8_t header[sizeof(CMemoryStream)];
    std::vector<std::uint8_t> data;
    std::uint32_t bytes = 0;
    bool hasData = false;
    explicit KeptStream(const CMemoryStream* src) {
        std::memcpy(header, src, sizeof(header));
        if (!src->data_) return;
        hasData = true;
        bytes = src->capacity_ > src->size_ ? src->capacity_ : src->size_;
        if (bytes == 0) bytes = 1;
        data.assign(src->data_, src->data_ + src->size_);
    }
    void CloneTo(CMemoryStream* dst) const {
        std::memcpy(dst, header, sizeof(header));
        if (!hasData) return;
        dst->data_ = static_cast<std::uint8_t*>(gamecrt::operator_new(bytes));
        std::memset(dst->data_, 0, bytes);
        if (!data.empty()) std::memcpy(dst->data_, data.data(), data.size());
    }
};
// A deep copy made from a KeptStream when the original is about to run, freed at the end of the
// scope if it was made.
struct LateStream {
    alignas(8) std::uint8_t bytes[sizeof(CMemoryStream)];
    bool made = false;
    CMemoryStream* get() { return reinterpret_cast<CMemoryStream*>(bytes); }
    void Make(const KeptStream& kept) {
        kept.CloneTo(get());
        made = true;
    }
    ~LateStream() {
        if (made) FreeStream(get());
    }
};
bool SameBytes(const CMemoryStream* a, const CMemoryStream* b) {
    if (a->size_ == 0) return true;
    if (!a->data_ || !b->data_) return a->data_ == b->data_;
    return std::memcmp(a->data_, b->data_, a->size_) == 0;
}
// Every field but the buffer pointer, and the bytes.
bool SameStream(const CMemoryStream* a, const CMemoryStream* b) {
    constexpr std::size_t kData = offsetof(CMemoryStream, data_);
    const auto* x = reinterpret_cast<const std::uint8_t*>(a);
    const auto* y = reinterpret_cast<const std::uint8_t*>(b);
    return std::memcmp(x, y, kData) == 0 && std::memcmp(x + kData + 4, y + kData + 4, sizeof(CMemoryStream) - kData - 4) == 0 &&
           SameBytes(a, b);
}
// A queued stream: vtable, ok, limit, +0x10..+0x17, capacity, position, size, and the bytes.
bool SameQueuedStream(const CMemoryStream* a, const CMemoryStream* b) {
    const auto* x = reinterpret_cast<const std::uint8_t*>(a);
    const auto* y = reinterpret_cast<const std::uint8_t*>(b);
    return std::memcmp(x, y, 4) == 0 && x[5] == y[5] && std::memcmp(x + 8, y + 8, 4) == 0 &&
           std::memcmp(x + 0x10, y + 0x10, 8) == 0 && std::memcmp(x + 0x1C, y + 0x1C, 0xC) == 0 && SameBytes(a, b);
}

// --- The CFile ------------------------------------------------------------------------------------
// MFC 4.2 CFile: vtable, handle (-1: none), close-on-delete, name (CString; the shared empty string
// has the reference count -1).
bool FileClosed(const void* file) {
    const char* name = At<const char*>(file, 0xC);
    return At<std::uint32_t>(file, 4) == 0xFFFFFFFFu && name && reinterpret_cast<const long*>(name)[-3] == -1;
}

struct FileOp {
    int slot;
    std::uint32_t a, b, c, result;
    std::uint32_t pointer;            // Write: the source buffer
    std::uint32_t dataAt, dataSize;   // Write: the bytes (kNotStored: too many to keep)
};
constexpr std::uint32_t kNotStored = 0xFFFFFFFFu;
struct FileLog {
    static constexpr int kOps = 32;
    static constexpr std::uint32_t kData = 64 * 1024;
    FileOp ops[kOps];
    int count;
    std::uint8_t data[kData];
    std::uint32_t used;
    bool overflow;
};
FileLog g_log;

struct FileMode {
    void* const* realVtable;   // recording: the real CFile's vtable
    bool recording;
    int next;                  // replay position
    bool mismatch;
};
FileMode g_mode;
bool g_busy;   // the file checks do not nest

std::uint32_t U(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

FileOp* Log(int slot, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t result) {
    if (g_log.count >= FileLog::kOps) {
        g_log.overflow = true;
        return nullptr;
    }
    FileOp& op = g_log.ops[g_log.count++];
    op = {slot, a, b, c, result, 0, g_log.used, 0};
    return &op;
}
// Bytes that do not fit are not kept: their replay then compares the buffer's address instead.
void LogData(FileOp* op, const void* p, std::uint32_t n) {
    if (!op) return;
    op->pointer = U(p);
    if (n > FileLog::kData - g_log.used) {
        op->dataSize = kNotStored;
        return;
    }
    std::memcpy(g_log.data + g_log.used, p, n);
    op->dataAt = g_log.used;
    op->dataSize = n;
    g_log.used += n;
}
const FileOp* Expect(int slot, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    if (g_mode.next >= g_log.count) {
        g_mode.mismatch = true;
        return nullptr;
    }
    const FileOp& op = g_log.ops[g_mode.next++];
    if (op.slot != slot || op.a != a || op.b != b || op.c != c) {
        g_mode.mismatch = true;
        return nullptr;
    }
    return &op;
}

// Calls the real CFile's slot with its own vtable in place.
template <typename R, typename... Args>
R Forward(void* file, int slot, Args... args) {
    void* proxy;
    std::memcpy(&proxy, file, sizeof(proxy));
    SetVtablePtr(file, g_mode.realVtable);
    if constexpr (std::is_void_v<R>) {
        ei::VCall<void>(file, slot, args...);
        SetVtablePtr(file, proxy);
    } else {
        R r = ei::VCall<R>(file, slot, args...);
        SetVtablePtr(file, proxy);
        return r;
    }
}

std::uint32_t __fastcall ProxyGetPosition(void* f) {
    if (g_mode.recording) {
        std::uint32_t r = Forward<std::uint32_t>(f, 3);
        Log(3, 0, 0, 0, r);
        return r;
    }
    const FileOp* op = Expect(3, 0, 0, 0);
    return op ? op->result : 0;
}
int __fastcall ProxyOpen(void* f, void*, const char* path, unsigned flags, void* error) {
    if (g_mode.recording) {
        int r = Forward<int>(f, 8, path, flags, error);
        Log(8, U(path), flags, U(error), static_cast<std::uint32_t>(r));
        return r;
    }
    const FileOp* op = Expect(8, U(path), flags, U(error));
    return op ? static_cast<int>(op->result) : 0;
}
long __fastcall ProxySeek(void* f, void*, long offset, unsigned from) {
    if (g_mode.recording) {
        long r = Forward<long>(f, 10, offset, from);
        Log(10, static_cast<std::uint32_t>(offset), from, 0, static_cast<std::uint32_t>(r));
        return r;
    }
    const FileOp* op = Expect(10, static_cast<std::uint32_t>(offset), from, 0);
    return op ? static_cast<long>(op->result) : 0;
}
std::uint32_t __fastcall ProxyGetLength(void* f) {
    if (g_mode.recording) {
        std::uint32_t r = Forward<std::uint32_t>(f, 12);
        Log(12, 0, 0, 0, r);
        return r;
    }
    const FileOp* op = Expect(12, 0, 0, 0);
    return op ? op->result : 0;
}
// Reads may go to different buffers (004A51C0 reads into a stack word and a fresh allocation): the
// bytes read are kept and the replay copies them into its buffer. A read too large to keep must
// use the same buffer, which ours filled (the file stream's Read passes the caller's).
std::uint32_t __fastcall ProxyRead(void* f, void*, void* p, unsigned n) {
    if (g_mode.recording) {
        std::uint32_t r = Forward<std::uint32_t>(f, 13, p, n);
        LogData(Log(13, n, 0, 0, r), p, r <= n ? r : n);
        return r;
    }
    const FileOp* op = Expect(13, n, 0, 0);
    if (!op) return 0;
    if (op->dataSize == kNotStored) {
        if (op->pointer != U(p)) g_mode.mismatch = true;
    } else {
        std::memcpy(p, g_log.data + op->dataAt, op->dataSize);
    }
    return op->result;
}
// Writes may come from different buffers (stack, fresh allocations): the bytes are compared (the
// buffer's address for a write too large to keep; the file stream's own Write passes the caller's).
void __fastcall ProxyWrite(void* f, void*, const void* p, unsigned n) {
    if (g_mode.recording) {
        Forward<void>(f, 14, p, n);
        LogData(Log(14, n, 0, 0, 0), p, n);
        return;
    }
    const FileOp* op = Expect(14, n, 0, 0);
    if (!op) return;
    bool same = op->dataSize == kNotStored ? op->pointer == U(p)
                                           : op->dataSize == n && std::memcmp(g_log.data + op->dataAt, p, n) == 0;
    if (!same) g_mode.mismatch = true;
}
void __fastcall ProxyClose(void* f) {
    if (g_mode.recording) {
        Forward<void>(f, 19);
        Log(19, 0, 0, 0, 0);
        return;
    }
    Expect(19, 0, 0, 0);
}
void* const* ProxyVtable() {
    static void* vtable[20] = {};
    vtable[3] = reinterpret_cast<void*>(&ProxyGetPosition);
    vtable[8] = reinterpret_cast<void*>(&ProxyOpen);
    vtable[10] = reinterpret_cast<void*>(&ProxySeek);
    vtable[12] = reinterpret_cast<void*>(&ProxyGetLength);
    vtable[13] = reinterpret_cast<void*>(&ProxyRead);
    vtable[14] = reinterpret_cast<void*>(&ProxyWrite);
    vtable[19] = reinterpret_cast<void*>(&ProxyClose);
    return vtable;
}

// Ours with the real CFile recorded, then (when nothing overflowed) the original with the copy's
// CFile replaying; `compare` gets whether the replay matched. Nested: ours alone.
template <typename Ours, typename Original, typename Compare>
void RunRecorded(void* realFile, void* copyFile, Ours ours, Original original, Compare compare) {
    if (g_busy) {
        ours();
        return;
    }
    g_busy = true;
    g_log.count = 0;
    g_log.used = 0;
    g_log.overflow = false;
    auto* realVtable = reinterpret_cast<void* const*>(VtableOf(realFile));
    g_mode = {realVtable, true, 0, false};
    SetVtablePtr(realFile, ProxyVtable());
    ours();
    SetVtablePtr(realFile, realVtable);
    if (!g_log.overflow) {
        g_mode.recording = false;
        SetVtablePtr(copyFile, ProxyVtable());
        original();
        compare(!g_mode.mismatch && g_mode.next == g_log.count);
    }
    g_busy = false;
}

// ================================================================================================
// FileStream

// 00478470: PUSH -1 (2), PUSH imm32 (5).
FileStream*(__fastcall* s_fsConstruct)(FileStream*);
FileStream* __fastcall FileStreamConstructVerify(FileStream* self) {
    alignas(8) std::uint8_t copy[sizeof(FileStream)];
    std::memcpy(copy, self, sizeof(copy));
    FileStream* result = s_fsConstruct(self);
    FileStream* ours = reinterpret_cast<FileStream*>(copy)->Construct();
    ei::verify::Check("FileStreamConstruct", true,
                      result == self && ours == reinterpret_cast<FileStream*>(copy) && std::memcmp(copy, self, sizeof(copy)) == 0, "");
    return result;
}

// 00477FB0: PUSH -1 (2), PUSH imm32 (5).
void(__fastcall* s_fsDestruct)(FileStream*);
void __fastcall FileStreamDestructVerify(FileStream* self) {
    if (!FileClosed(self->file_)) {
        self->Destruct();
        return;
    }
    alignas(8) std::uint8_t copy[sizeof(FileStream)];
    std::memcpy(copy, self, sizeof(copy));
    s_fsDestruct(reinterpret_cast<FileStream*>(copy));
    self->Destruct();
    ei::verify::Check("FileStreamDestruct", true, std::memcmp(copy, self, sizeof(copy)) == 0, "");
}

// 00477F90: a CALL at +3 (no trampoline): ours only.
void* __fastcall FileStreamDeletingDestructorVerify(FileStream* self, void*, unsigned flags) {
    void* result = static_cast<void*>(self->DeletingDestruct(flags));
    ei::verify::Check("FileStreamDeletingDestructor", static_cast<void*>(self), result, "");
    return result;
}

// The slots through the recorded CFile; the stream's header (vtable .. limit) and the result
// compared.
template <typename R, typename Ours, typename Original>
R StreamSlot(const char* name, FileStream* self, Ours ours, Original original) {
    FileStream copy;
    std::memcpy(&copy, self, sizeof(copy));
    R result{};
    R expected{};
    RunRecorded(
        self->file_, copy.file_, [&] { result = ours(self); }, [&] { expected = original(&copy); },
        [&](bool replayed) {
            bool same = replayed && std::memcmp(&expected, &result, sizeof(R)) == 0 &&
                        std::memcmp(&copy, self, offsetof(FileStream, file_)) == 0;
            ei::verify::Check(name, true, same, "%s, %u / %u", replayed ? "replayed" : "calls differ",
                              static_cast<unsigned>(expected), static_cast<unsigned>(result));
        });
    return result;
}

// 00477F00: MOV EDX,[ESP+4] (4), PUSH ESI (1).
unsigned(__fastcall* s_fsRead)(FileStream*, void*, void*, unsigned);
unsigned __fastcall FileStreamReadVerify(FileStream* self, void*, void* buffer, unsigned count) {
    return StreamSlot<unsigned>(
        "FileStreamRead", self, [&](FileStream* s) { return s->FileStream::ReadImpl(buffer, count); },
        [&](FileStream* s) { return s_fsRead(s, nullptr, buffer, count); });
}

// 00477F30: MOV EDX,[ESP+8] (4), MOV EAX,[ECX+0xC] (3).
void(__fastcall* s_fsWrite)(FileStream*, void*, const void*, unsigned);
void __fastcall FileStreamWriteVerify(FileStream* self, void*, const void* buffer, unsigned count) {
    StreamSlot<int>(
        "FileStreamWrite", self,
        [&](FileStream* s) {
            s->FileStream::WriteImpl(buffer, count);
            return 0;
        },
        [&](FileStream* s) {
            s_fsWrite(s, nullptr, buffer, count);
            return 0;
        });
}

// 00477F50: MOV EDX,[ESP+4] (4), MOV EAX,[ECX+0xC] (3).
long(__fastcall* s_fsSeek)(FileStream*, void*, long);
long __fastcall FileStreamSeekVerify(FileStream* self, void*, long position) {
    return StreamSlot<long>(
        "FileStreamSeek", self, [&](FileStream* s) { return s->SeekFrom(position); },
        [&](FileStream* s) { return s_fsSeek(s, nullptr, position); });
}

// 00477F70 / 00477F80: MOV EAX,[ECX+0xC] (3), ADD ECX,0xC (3).
unsigned(__fastcall* s_fsSize)(FileStream*);
unsigned __fastcall FileStreamSizeVerify(FileStream* self) {
    return StreamSlot<unsigned>("FileStreamSize", self, [](FileStream* s) { return s->FileStream::Size(); },
                                [](FileStream* s) { return s_fsSize(s); });
}
unsigned(__fastcall* s_fsPosition)(FileStream*);
unsigned __fastcall FileStreamPositionVerify(FileStream* self) {
    return StreamSlot<unsigned>("FileStreamPosition", self, [](FileStream* s) { return s->FileStream::Position(); },
                                [](FileStream* s) { return s_fsPosition(s); });
}

// 00477EE0: MOV EAX,[ECX+0xC] (3), MOV byte [ECX+5],1 (4).
long(__fastcall* s_fsRewind)(FileStream*);
long __fastcall FileStreamRewindVerify(FileStream* self) {
    return StreamSlot<long>("FileStreamRewind", self, [](FileStream* s) { return s->Restart(); },
                            [](FileStream* s) { return s_fsRewind(s); });
}

// ================================================================================================
// NetLink

// 00478050: PUSH -1 (2), PUSH imm32 (5).
void(__fastcall* s_linkDestruct)(NetLink*);
void __fastcall LinkDestructVerify(NetLink* self) {
    if (!FileClosed(self->record_.file_)) {
        self->Destruct();
        return;
    }
    alignas(8) std::uint8_t copy[sizeof(NetLink)];
    std::memcpy(copy, self, sizeof(copy));
    s_linkDestruct(reinterpret_cast<NetLink*>(copy));
    self->Destruct();
    ei::verify::Check("LinkDestruct", true, std::memcmp(copy, self, sizeof(copy)) == 0, "");
}

// 00478030: a CALL at +3: ours only.
void* __fastcall LinkDeletingDestructorVerify(NetLink* self, void*, unsigned flags) {
    void* result = static_cast<void*>(self->DeletingDestruct(flags));
    ei::verify::Check("LinkDeletingDestructor", static_cast<void*>(self), result, "");
    return result;
}

// 00478000 calls slot 6 in its first 5 bytes: its 12-byte body (one indirect call, no relative
// operand) is copied when this DLL loads and runs as the original. Slot 6 is a query in every
// class that uses this slot 7.
using CountFn = int(__fastcall*)(NetLink*);
CountFn CopyIncomingCount() {
    constexpr std::size_t kSize = 12;
    void* copy = ::VirtualAlloc(nullptr, kSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (copy) std::memcpy(copy, reinterpret_cast<const void*>(0x00478000), kSize);
    return reinterpret_cast<CountFn>(copy);
}
CountFn s_linkCountCopy = CopyIncomingCount();
int __fastcall LinkIncomingCountVerify(NetLink* self) {
    if (!s_linkCountCopy) return self->NetLink::IncomingCount();
    int expected = s_linkCountCopy(self);
    int actual = self->NetLink::IncomingCount();
    ei::verify::Check("LinkIncomingCount", expected, actual, "");
    return actual;
}

// 00478100: MOV AL,1 (2), RET 4 (3).
bool(__fastcall* s_linkSend)(NetLink*, void*, void*);
bool __fastcall LinkSendVerify(NetLink* self, void*, void* stream) {
    bool expected = s_linkSend(self, nullptr, stream);
    bool actual = self->NetLink::SendImpl(static_cast<CStream*>(stream));
    ei::verify::Check("LinkSend", expected, actual, "");
    return actual;
}

// The link header (vtable .. the record's limit) of a copy and the real link.
bool SameLinkHeader(const void* a, const void* b) { return std::memcmp(a, b, offsetof(NetLink, record_) + offsetof(FileStream, file_)) == 0; }

// 00478110: MOV AL,[ECX+8] (3), TEST AL,AL (2).
bool(__fastcall* s_linkRecord)(NetLink*, void*, void*);
bool __fastcall LinkRecordVerify(NetLink* self, void*, void* stream) {
    bool writes = self->recording_ && !self->skipNext_;
    if (writes && !IsMemoryStream(stream)) return self->NetLink::ReceiveImpl(static_cast<CStream*>(stream));
    alignas(8) std::uint8_t copy[sizeof(NetLink)];
    std::memcpy(copy, self, sizeof(copy));
    auto* link = reinterpret_cast<NetLink*>(copy);
    if (!writes) {
        bool expected = s_linkRecord(link, nullptr, stream);
        bool actual = self->NetLink::ReceiveImpl(static_cast<CStream*>(stream));
        ei::verify::Check("LinkRecord", true, expected == actual && SameLinkHeader(copy, self), "");
        return actual;
    }
    KeptStream keptSource(static_cast<CMemoryStream*>(stream));
    LateStream source;   // made after ours (SV2f)
    bool actual = false;
    bool expected = false;
    RunRecorded(
        self->record_.file_, link->record_.file_, [&] { actual = self->NetLink::ReceiveImpl(static_cast<CStream*>(stream)); },
        [&] {
            source.Make(keptSource);
            expected = s_linkRecord(link, nullptr, source.get());
        },
        [&](bool replayed) {
            bool same = replayed && expected == actual && SameLinkHeader(copy, self) &&
                        SameStream(source.get(), static_cast<CMemoryStream*>(stream));
            ei::verify::Check("LinkRecord", true, same, replayed ? "" : "calls differ");
        });
    return actual;
}

// 004780B0: MOV EDX,[ESP+4] (4), PUSH ESI (1).
void(__fastcall* s_linkOpen)(NetLink*, void*, const char*);
void __fastcall LinkOpenRecordingVerify(NetLink* self, void*, const char* path) {
    alignas(8) std::uint8_t copy[sizeof(NetLink)];
    std::memcpy(copy, self, sizeof(copy));
    auto* link = reinterpret_cast<NetLink*>(copy);
    RunRecorded(
        self->record_.file_, link->record_.file_, [&] { self->OpenRecording(path); },
        [&] { s_linkOpen(link, nullptr, path); },
        [&](bool replayed) {
            ei::verify::Check("LinkOpenRecording", true, replayed && SameLinkHeader(copy, self), replayed ? "" : "calls differ");
        });
}

// 004780E0: PUSH ESI (1), MOV ESI,ECX (2), MOV EAX,[ESI+0x18] (3).
void(__fastcall* s_linkClose)(NetLink*);
void __fastcall LinkCloseRecordingVerify(NetLink* self) {
    alignas(8) std::uint8_t copy[sizeof(NetLink)];
    std::memcpy(copy, self, sizeof(copy));
    auto* link = reinterpret_cast<NetLink*>(copy);
    RunRecorded(
        self->record_.file_, link->record_.file_, [&] { self->CloseRecording(); }, [&] { s_linkClose(link); },
        [&](bool replayed) {
            ei::verify::Check("LinkCloseRecording", true, replayed && SameLinkHeader(copy, self), replayed ? "" : "calls differ");
        });
}

// ================================================================================================
// LoopbackLink

bool PeerAlive(const LoopbackLink* self) { return self->peer_ && !(self->peer_->flags7_ & CRefObject::kDetached); }
int QueueCount(const QueueNode* header) {
    int n = 0;
    for (const QueueNode* node = header->next; node != header && n < 100000; node = node->next) ++n;
    return n;
}
bool EmptyQueue(const LoopbackLink* self) { return self->queue_ && self->queue_->next == self->queue_ && self->queue_->prev == self->queue_; }
bool FreshRecord(const FileStream& record) {
    return VtableOf(&record) == kFileStreamVtable && record.ok_ == 1 && record.version_ == 2000000000u && FileClosed(record.file_);
}

// 004782E0: ours only. The returned link and the player's are loopbacks joined to each other,
// with empty queues and closed files; the player's has two references, the returned one one.
LoopbackLink* __fastcall CreatePairVerify(void* unused, void*, std::uint8_t* player) {
    LoopbackLink* second = LoopbackLink::CreatePair(unused, nullptr, player);
    if (!player) {
        ei::verify::Check("LoopbackCreatePair", static_cast<LoopbackLink*>(nullptr), second, "null player");
        return second;
    }
    auto* first = At<LoopbackLink*>(player, 8);
    bool ok = first && second && VtableOf(first) == kLoopbackVtable && VtableOf(second) == kLoopbackVtable &&
              first->peer_ == second && second->peer_ == first &&
              first->refs_ == 2 && second->refs_ == 1 && EmptyQueue(first) && EmptyQueue(second) &&
              first->recording_ == 0 && second->recording_ == 0 && FreshRecord(first->record_) &&
              FreshRecord(second->record_);
    ei::verify::Check("LoopbackCreatePair", true, ok, "");
    return second;
}

// 004787A0: ours only; the root and CStream vtables left.
void __fastcall LoopbackDestructVerify(LoopbackLink* self) {
    self->Destruct();
    bool ok = VtableOf(self) == 0x0073BB68 && VtableOf(&self->record_) == 0x0073BDB0;
    ei::verify::Check("LoopbackDestruct", true, ok, "");
}

// 00478780: a CALL at +3: ours only.
void* __fastcall LoopbackDeletingDestructorVerify(LoopbackLink* self, void*, unsigned flags) {
    void* result = static_cast<void*>(self->DeletingDestruct(flags));
    ei::verify::Check("LoopbackDeletingDestructor", static_cast<void*>(self), result, "");
    return result;
}

// 00478610: ours only; afterwards a fresh loopback with the same reference count.
void __fastcall LoopbackResetVerify(LoopbackLink* self) {
    std::int16_t refs = self->refs_;
    self->LoopbackLink::OnShutdown();
    bool ok = VtableOf(self) == kLoopbackVtable && self->refs_ == refs && self->recording_ == 0 && !self->peer_ &&
              EmptyQueue(self) && FreshRecord(self->record_);
    ei::verify::Check("LoopbackReset", true, ok, "");
}

// 004784D0: PUSH -1 (2), PUSH imm32 (5). Both clone; the original's copy is deleted afterwards.
LoopbackLink*(__fastcall* s_clone)(LoopbackLink*);
bool SameClone(const LoopbackLink* a, const LoopbackLink* b) {
    if (std::memcmp(a, b, 10) != 0) return false;   // vtable, count and flags, recording, skip
    const auto* x = reinterpret_cast<const std::uint8_t*>(&a->record_);
    const auto* y = reinterpret_cast<const std::uint8_t*>(&b->record_);
    if (std::memcmp(x, y, 4) != 0 || x[5] != y[5] || std::memcmp(x + 8, y + 8, 4 + 0x10) != 0) return false;
    if (a->peer_ != b->peer_) return false;
    const QueueNode* p = a->queue_->next;
    const QueueNode* q = b->queue_->next;
    for (; p != a->queue_ && q != b->queue_; p = p->next, q = q->next)
        if (!SameQueuedStream(&p->stream, &q->stream)) return false;
    return p == a->queue_ && q == b->queue_;
}
LoopbackLink* __fastcall LoopbackCloneVerify(LoopbackLink* self) {
    // Ours first, so that its copy lands where the original's would without the check (SV2f).
    LoopbackLink* actual = self->LoopbackLink::Clone();
    LoopbackLink* expected = s_clone(self);
    if (expected && actual) {
        ei::verify::Check("LoopbackClone", true, SameClone(expected, actual), "%d queued", QueueCount(self->queue_));
    }
    if (expected) expected->DeletingDestruct(1);
    return actual;
}

// The queries, side by side. 00478710 / 00478730 / 00478750 / 00478760: MOV r,[ECX+0x2C or 0x28]
// (3) and a 2-byte TEST / CMP / XOR.
int(__fastcall* s_connected)(LoopbackLink*);
int __fastcall LoopbackConnectedVerify(LoopbackLink* self) {
    int expected = s_connected(self);
    int actual = (self->LoopbackLink::Connected() ? 1 : 0);
    ei::verify::Check("LoopbackConnected", expected, actual, "");
    return actual;
}
int(__fastcall* s_alive)(LoopbackLink*);
int __fastcall LoopbackAliveVerify(LoopbackLink* self) {
    int expected = s_alive(self);
    int actual = (self->LoopbackLink::Alive() ? 1 : 0);
    ei::verify::Check("LoopbackAlive", expected, actual, "");
    return actual;
}
bool(__fastcall* s_hasIncoming)(LoopbackLink*);
bool __fastcall LoopbackHasIncomingVerify(LoopbackLink* self) {
    bool expected = s_hasIncoming(self);
    bool actual = self->LoopbackLink::HasIncoming();
    ei::verify::Check("LoopbackHasIncoming", expected, actual, "");
    return actual;
}
int(__fastcall* s_count)(LoopbackLink*);
int __fastcall LoopbackIncomingCountVerify(LoopbackLink* self) {
    int expected = s_count(self);
    int actual = self->LoopbackLink::IncomingCount();
    ei::verify::Check("LoopbackIncomingCount", expected, actual, "");
    return actual;
}

// An empty scratch queue from the node pool, and its release (elements destroyed, nodes back).
QueueNode* ScratchQueue() {
    auto* header = static_cast<QueueNode*>(NodeAllocate(sizeof(QueueNode)));
    header->next = header;
    header->prev = header;
    return header;
}
void FreeScratchQueue(QueueNode* header) {
    QueueClear(&header);
    NodeFree(header, sizeof(QueueNode));
}

// 00478220: PUSH ECX, PUSH ESI, MOV ESI,ECX (4), MOV EAX,[ESI+0x2C] (3).
bool(__fastcall* s_send)(LoopbackLink*, void*, void*);
bool __fastcall LoopbackSendVerify(LoopbackLink* self, void*, void* stream) {
    if (!PeerAlive(self)) {
        bool expected = s_send(self, nullptr, stream);
        bool actual = self->LoopbackLink::SendImpl(static_cast<CStream*>(stream));
        ei::verify::Check("LoopbackSend", expected, actual, "no peer");
        return actual;
    }
    if (!IsMemoryStream(stream)) return self->LoopbackLink::SendImpl(static_cast<CStream*>(stream));
    KeptStream keptSource(static_cast<CMemoryStream*>(stream));
    bool actual = self->LoopbackLink::SendImpl(static_cast<CStream*>(stream));
    CMemoryStream source;
    keptSource.CloneTo(&source);   // after ours (SV2f)

    alignas(8) std::uint8_t peerCopy[sizeof(LoopbackLink)];
    alignas(8) std::uint8_t selfCopy[sizeof(LoopbackLink)];
    std::memcpy(peerCopy, self->peer_, sizeof(peerCopy));
    std::memcpy(selfCopy, self, sizeof(selfCopy));
    auto* scratchPeer = reinterpret_cast<LoopbackLink*>(peerCopy);
    auto* scratchSelf = reinterpret_cast<LoopbackLink*>(selfCopy);
    scratchPeer->queue_ = ScratchQueue();
    scratchSelf->peer_ = scratchPeer;
    bool expected = s_send(scratchSelf, nullptr, &source);

    const QueueNode* realQueue = self->peer_->queue_;
    const QueueNode* scratch = scratchPeer->queue_;
    bool same = expected == actual && QueueCount(scratch) == 1 && realQueue->prev != realQueue &&
                SameQueuedStream(&scratch->next->stream, &realQueue->prev->stream) &&
                SameStream(&source, static_cast<CMemoryStream*>(stream));
    ei::verify::Check("LoopbackSend", true, same, "%u bytes", static_cast<CMemoryStream*>(stream)->size_);
    FreeScratchQueue(scratchPeer->queue_);
    FreeStream(&source);
    return actual;
}

// 00478270: PUSH EBX, PUSH ESI, PUSH EDI (3), MOV EDI,ECX (2).
bool(__fastcall* s_receive)(LoopbackLink*, void*, void*);
bool __fastcall LoopbackReceiveVerify(LoopbackLink* self, void*, void* stream) {
    if (self->queue_->next == self->queue_) {
        bool expected = s_receive(self, nullptr, stream);
        bool actual = self->LoopbackLink::ReceiveImpl(static_cast<CStream*>(stream));
        ei::verify::Check("LoopbackReceive", expected, actual, "empty");
        return actual;
    }
    if (!IsMemoryStream(stream) || !IsMemoryStream(&self->queue_->next->stream)) return self->LoopbackLink::ReceiveImpl(static_cast<CStream*>(stream));
    int before = QueueCount(self->queue_);

    // The original's link: a copy whose queue holds a copy of the front stream (pool nodes: its
    // erase gives the node back to the pool and frees the stream's buffer). Ours runs first: the
    // link's bytes and the two streams are kept in our memory before it, the scratch queue and the
    // streams' game-heap copies made after it (the game reads the heap's address order, SV2f).
    alignas(8) std::uint8_t selfCopy[sizeof(LoopbackLink)];
    std::memcpy(selfCopy, self, sizeof(selfCopy));
    auto* scratchSelf = reinterpret_cast<LoopbackLink*>(selfCopy);
    KeptStream keptFront(&self->queue_->next->stream);
    KeptStream keptTarget(static_cast<CMemoryStream*>(stream));
    QueueNode* header = nullptr;
    LateStream target;
    auto setUp = [&] {
        header = ScratchQueue();
        auto* node = static_cast<QueueNode*>(NodeAllocate(sizeof(QueueNode)));
        keptFront.CloneTo(&node->stream);
        node->next = header;
        node->prev = header;
        header->next = node;
        header->prev = node;
        scratchSelf->queue_ = header;
        target.Make(keptTarget);
    };

    bool actual = false;
    bool expected = false;
    auto compare = [&](bool replayed) {
        bool same = replayed && expected == actual && QueueCount(header) == 0 && QueueCount(self->queue_) == before - 1 &&
                    scratchSelf->skipNext_ == self->skipNext_ &&
                    SameStream(target.get(), static_cast<CMemoryStream*>(stream));
        ei::verify::Check("LoopbackReceive", true, same, "%s, %d queued", replayed ? "replayed" : "calls differ", before);
    };
    if (self->recording_ && !self->skipNext_) {
        RunRecorded(
            self->record_.file_, scratchSelf->record_.file_, [&] { actual = self->LoopbackLink::ReceiveImpl(static_cast<CStream*>(stream)); },
            [&] {
                setUp();
                expected = s_receive(scratchSelf, nullptr, target.get());
            },
            compare);
    } else {
        actual = self->LoopbackLink::ReceiveImpl(static_cast<CStream*>(stream));
        setUp();
        expected = s_receive(scratchSelf, nullptr, target.get());
        compare(true);
    }
    if (header) {
        if (header->next != header) QueueClear(&header);   // the original did not empty it
        NodeFree(header, sizeof(QueueNode));
    }
    return actual;
}

// 00478140: ours only; no peer afterwards.
void __fastcall LoopbackDisconnectVerify(LoopbackLink* self) {
    self->LoopbackLink::Disconnect();
    ei::verify::Check("LoopbackDisconnect", static_cast<LoopbackLink*>(nullptr), self->peer_, "");
}

// ================================================================================================
// ReplayLink

// The header of a file stream two constructions should share: vtable, ok, limit and the CFile.
bool SameFileStream(const FileStream& a, const FileStream& b) {
    const auto* x = reinterpret_cast<const std::uint8_t*>(&a);
    const auto* y = reinterpret_cast<const std::uint8_t*>(&b);
    return std::memcmp(x, y, 4) == 0 && x[5] == y[5] && std::memcmp(x + 8, y + 8, 4 + 0x10) == 0;
}
// The replay link up to its replay stream's CFile.
constexpr std::size_t kReplayHeader = offsetof(ReplayLink, replay_) + offsetof(FileStream, file_);

// 00478960: ours only. A replay link (count 0, not recording, a closed record file) whose replay
// stream reads and is ok exactly when its CFile opened.
ReplayLink* __fastcall ReplayCreateVerify(void* unused, void*, const char* path) {
    ReplayLink* link = ReplayLink::Create(unused, nullptr, path);
    bool open = At<std::uint32_t>(link->replay_.file_, 4) != 0xFFFFFFFFu;
    bool ok = VtableOf(link) == kReplayVtable && At<std::uint32_t>(link, 4) == 0 && link->recording_ == 0 &&
              FreshRecord(link->record_) && VtableOf(&link->replay_) == kFileStreamVtable && link->replay_.reading_ == 1 &&
              link->replay_.version_ == 2000000000u && link->replay_.ok_ == (open ? 1 : 0);
    ei::verify::Check("ReplayCreate", true, ok, "%s", open ? "opened" : "not opened");
    return link;
}

// 00478CC0: PUSH -1 (2), PUSH imm32 (5). With both files closed the original on a copy, ours on
// the real link; else ours only.
void(__fastcall* s_replayDestruct)(ReplayLink*);
void __fastcall ReplayDestructVerify(ReplayLink* self) {
    if (!FileClosed(self->record_.file_) || !FileClosed(self->replay_.file_)) {
        self->Destruct();
        return;
    }
    alignas(8) std::uint8_t copy[sizeof(ReplayLink)];
    std::memcpy(copy, self, sizeof(copy));
    s_replayDestruct(reinterpret_cast<ReplayLink*>(copy));
    self->Destruct();
    ei::verify::Check("ReplayDestruct", true, std::memcmp(copy, self, sizeof(copy)) == 0, "");
}

// 00478CA0: a CALL at +3: ours only.
void* __fastcall ReplayDeletingDestructorVerify(ReplayLink* self, void*, unsigned flags) {
    void* result = static_cast<void*>(self->DeletingDestruct(flags));
    ei::verify::Check("ReplayDeletingDestructor", static_cast<void*>(self), result, "");
    return result;
}

// 00478B10: ours only; afterwards a fresh replay link with the same count, flags and +9.
void __fastcall ReplayResetVerify(ReplayLink* self) {
    std::uint32_t header = At<std::uint32_t>(self, 4);
    std::uint8_t skip = self->skipNext_;
    self->ReplayLink::OnShutdown();
    bool ok = VtableOf(self) == kReplayVtable && At<std::uint32_t>(self, 4) == header && self->skipNext_ == skip &&
              self->recording_ == 0 && FreshRecord(self->record_) && FreshRecord(self->replay_);
    ei::verify::Check("ReplayReset", true, ok, "");
}

// 00478A50: PUSH -1 (2), PUSH imm32 (5). Both clone; the copies compared (count, flags, the two
// fresh file streams), the original's deleted.
ReplayLink*(__fastcall* s_replayClone)(ReplayLink*);
ReplayLink* __fastcall ReplayCloneVerify(ReplayLink* self) {
    ReplayLink* actual = self->ReplayLink::Clone();   // ours first (SV2f)
    ReplayLink* expected = s_replayClone(self);
    if (expected && actual) {
        bool same = std::memcmp(expected, actual, 10) == 0 && SameFileStream(expected->record_, actual->record_) &&
                    SameFileStream(expected->replay_, actual->replay_);
        ei::verify::Check("ReplayClone", true, same, "");
    }
    if (expected) expected->DeletingDestruct(1);
    return actual;
}

// 00478C30 / 00478C50: MOV EAX,[ECX+0x28] (3), PUSH ESI (1), LEA ESI,[ECX+0x28] (3). The
// recorded CFile (GetPosition, GetLength through the file stream's slots 5 and 4).
template <typename Ours, typename Original>
int ReplayQuery(const char* name, ReplayLink* self, Ours ours, Original original) {
    alignas(8) std::uint8_t copy[sizeof(ReplayLink)];
    std::memcpy(copy, self, sizeof(copy));
    auto* link = reinterpret_cast<ReplayLink*>(copy);
    int actual = 0;
    int expected = 0;
    RunRecorded(
        self->replay_.file_, link->replay_.file_, [&] { actual = ours(self); }, [&] { expected = original(link); },
        [&](bool replayed) {
            ei::verify::Check(name, true, replayed && expected == actual, "%s, %d / %d", replayed ? "replayed" : "calls differ", expected,
                              actual);
        });
    return actual;
}
int(__fastcall* s_replayAlive)(ReplayLink*);
int __fastcall ReplayAliveVerify(ReplayLink* self) {
    return ReplayQuery("ReplayAlive", self, [](ReplayLink* l) { return (l->ReplayLink::Alive() ? 1 : 0); }, [](ReplayLink* l) { return s_replayAlive(l); });
}
int(__fastcall* s_replayHasIncoming)(ReplayLink*);
int __fastcall ReplayHasIncomingVerify(ReplayLink* self) {
    return ReplayQuery("ReplayHasIncoming", self, [](ReplayLink* l) { return (l->ReplayLink::HasIncoming() ? 1 : 0); },
                       [](ReplayLink* l) { return s_replayHasIncoming(l); });
}

// 00478C70: MOV EAX,[ESP+4] (4), PUSH ESI (1). The recorded CFile (004A51C0 reads the size into a
// stack word and the bytes into a buffer of its own) with a deep copy of a memory stream target;
// the result, the target and the link's header compared.
bool(__fastcall* s_replayReceive)(ReplayLink*, void*, void*);
bool __fastcall ReplayReceiveVerify(ReplayLink* self, void*, void* stream) {
    if (!IsMemoryStream(stream)) return self->ReplayLink::ReceiveImpl(static_cast<CStream*>(stream));
    alignas(8) std::uint8_t copy[sizeof(ReplayLink)];
    std::memcpy(copy, self, sizeof(copy));
    auto* link = reinterpret_cast<ReplayLink*>(copy);
    KeptStream keptTarget(static_cast<CMemoryStream*>(stream));
    LateStream target;   // made after ours (SV2f)
    bool actual = false;
    bool expected = false;
    RunRecorded(
        self->replay_.file_, link->replay_.file_, [&] { actual = self->ReplayLink::ReceiveImpl(static_cast<CStream*>(stream)); },
        [&] {
            target.Make(keptTarget);
            expected = s_replayReceive(link, nullptr, target.get());
        },
        [&](bool replayed) {
            bool same = replayed && expected == actual && std::memcmp(copy, self, kReplayHeader) == 0 &&
                        SameStream(target.get(), static_cast<CMemoryStream*>(stream));
            ei::verify::Check("ReplayReceive", true, same, "%s, %u bytes", replayed ? "replayed" : "calls differ",
                              static_cast<CMemoryStream*>(stream)->size_);
        });
    return actual;
}

// 00478C90: MOV EAX,[ECX+0x34] (3), ADD ECX,0x34 (3). The recorded CFile (Close); the link's
// header compared.
void(__fastcall* s_replayDisconnect)(ReplayLink*);
void __fastcall ReplayDisconnectVerify(ReplayLink* self) {
    alignas(8) std::uint8_t copy[sizeof(ReplayLink)];
    std::memcpy(copy, self, sizeof(copy));
    auto* link = reinterpret_cast<ReplayLink*>(copy);
    RunRecorded(
        self->replay_.file_, link->replay_.file_, [&] { self->ReplayLink::Disconnect(); }, [&] { s_replayDisconnect(link); },
        [&](bool replayed) {
            ei::verify::Check("ReplayDisconnect", true, replayed && std::memcmp(copy, self, kReplayHeader) == 0,
                              replayed ? "" : "calls differ");
        });
}

} // namespace

EI_VERIFY(0x00478470, FileStreamConstructVerify, 7, s_fsConstruct);
EI_VERIFY(0x00477FB0, FileStreamDestructVerify, 7, s_fsDestruct);
EI_VERIFY(0x00477F90, FileStreamDeletingDestructorVerify, 0, s_unused);
EI_VERIFY(0x00477F00, FileStreamReadVerify, 5, s_fsRead);
EI_VERIFY(0x00477F30, FileStreamWriteVerify, 7, s_fsWrite);
EI_VERIFY(0x00477F50, FileStreamSeekVerify, 7, s_fsSeek);
EI_VERIFY(0x00477F70, FileStreamSizeVerify, 6, s_fsSize);
EI_VERIFY(0x00477F80, FileStreamPositionVerify, 6, s_fsPosition);
EI_VERIFY(0x00477EE0, FileStreamRewindVerify, 7, s_fsRewind);
EI_VERIFY(0x00478050, LinkDestructVerify, 7, s_linkDestruct);
EI_VERIFY(0x00478030, LinkDeletingDestructorVerify, 0, s_unused);
EI_VERIFY(0x00478000, LinkIncomingCountVerify, 0, s_unused);
EI_VERIFY(0x00478100, LinkSendVerify, 5, s_linkSend);
EI_VERIFY(0x00478110, LinkRecordVerify, 5, s_linkRecord);
EI_VERIFY(0x004780B0, LinkOpenRecordingVerify, 5, s_linkOpen);
EI_VERIFY(0x004780E0, LinkCloseRecordingVerify, 6, s_linkClose);
EI_VERIFY(0x004782E0, CreatePairVerify, 0, s_unused);
EI_VERIFY(0x004787A0, LoopbackDestructVerify, 0, s_unused);
EI_VERIFY(0x00478780, LoopbackDeletingDestructorVerify, 0, s_unused);
EI_VERIFY(0x00478610, LoopbackResetVerify, 0, s_unused);
EI_VERIFY(0x004784D0, LoopbackCloneVerify, 7, s_clone);
EI_VERIFY(0x00478710, LoopbackConnectedVerify, 5, s_connected);
EI_VERIFY(0x00478730, LoopbackAliveVerify, 5, s_alive);
EI_VERIFY(0x00478750, LoopbackHasIncomingVerify, 5, s_hasIncoming);
EI_VERIFY(0x00478760, LoopbackIncomingCountVerify, 5, s_count);
EI_VERIFY(0x00478220, LoopbackSendVerify, 7, s_send);
EI_VERIFY(0x00478270, LoopbackReceiveVerify, 5, s_receive);
EI_VERIFY(0x00478140, LoopbackDisconnectVerify, 0, s_unused);
EI_VERIFY(0x00478960, ReplayCreateVerify, 0, s_unused);
EI_VERIFY(0x00478CC0, ReplayDestructVerify, 7, s_replayDestruct);
EI_VERIFY(0x00478CA0, ReplayDeletingDestructorVerify, 0, s_unused);
EI_VERIFY(0x00478B10, ReplayResetVerify, 0, s_unused);
EI_VERIFY(0x00478A50, ReplayCloneVerify, 7, s_replayClone);
EI_VERIFY(0x00478C30, ReplayAliveVerify, 7, s_replayAlive);
EI_VERIFY(0x00478C50, ReplayHasIncomingVerify, 7, s_replayHasIncoming);
EI_VERIFY(0x00478C70, ReplayReceiveVerify, 5, s_replayReceive);
EI_VERIFY(0x00478C90, ReplayDisconnectVerify, 6, s_replayDisconnect);
