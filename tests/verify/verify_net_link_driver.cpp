// Checks for the framed link (0073C9C8) and the per-player driver (0073C990) in net_link.cpp.
//
// Nothing a scenario does reaches them: drivers are made for the connections of remote players
// (00479E30 / 0047A080), and harness runs have none. The checks are written for a network game.
//
// Memory streams (the inbox at +0x28, the streams passed in) are deep-copied for the original's
// run: header plus a private buffer from the game heap; compared field by field (the buffer
// pointer excepted) and up to the size.
//
//  - The inbox's own work (append 0047B980, take everything 0047B9B0, has message, count,
//    Receive): the original on a copy of the link with a deep copy of the inbox (and of a memory
//    stream argument), ours on the real link; results and streams compared.
//  - Send (0047BAD0): ours on the real link with its vtable swapped for a recorder whose slots 13
//    (the bytes out) and 11 (Disconnect) forward to the real ones and log the call; the original
//    on a byte copy whose vtable replays the log (the bytes handed to slot 13 compared, the logged
//    result returned); the argument stream deep-copied.
//  - Driver Pump: the original on a copy of the driver (deep-copied inbox) whose player is a byte
//    copy of the player (Pump only reads its incoming buffer's memory); the inbox and the incoming
//    buffer's fields compared. SendBytes: the same with the player copy's outgoing buffer a real
//    copy (004DA9E0; the write may grow it); the buffers' fields and bytes compared.
//  - Connected: side by side. The file stream copy constructor: the original on the real object,
//    ours on a copy taken before. The framed link's destructor body: with a closed file the
//    original on a copy (deep-copied inbox), ours on the real link.
//  - Clone: both clone (the original from a copy of the source with a deep-copied inbox: the clone
//    overwrites the source's bytes with its own unwritten buffer); the clones compared but for
//    those bytes; the original's taken apart by hand (its destructor would disconnect).
//  - Disconnect, the driver's destructor body, Reset and the deleting destructors: ours only, the
//    results checked (Disconnect: the unread bytes and then the old incoming bytes now at the
//    start of the player's incoming stream, when the player outlives the call).
#include "verify.h"

#include "../../src/game/net_connection.h"
#include "../../src/game/net_link.h"
#include "../../src/lib/crt.h"

#include "ei/vcall.h"

#include <cstddef>
#include <cstring>

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

// --- Callees --------------------------------------------------------------------------------------
// GameBuffer copy constructor (004DA9E0, RET 4) and the growing buffer's destructor (004DA910).
__declspec(naked) void __fastcall GameBufferCopy(GameBuffer* /*to*/, void*, const GameBuffer* /*from*/) EI_ORIGINAL(0x004DA9E0)
__declspec(naked) void __fastcall GrowingBufferDestruct(GameBuffer* /*buffer*/) EI_ORIGINAL(0x004DA910)

// --- Memory streams -------------------------------------------------------------------------------
bool IsMemoryStream(const void* s) {
    std::uintptr_t v = VtableOf(s);
    return v == 0x0073BD90 || v == 0x0073C960;
}
void CloneStream(CMemoryStream* dst, const CMemoryStream* src) {
    std::memcpy(dst, src, sizeof(CMemoryStream));
    if (!src->data_) return;
    std::uint32_t bytes = src->unknown_1C > src->size_ ? src->unknown_1C : src->size_;
    if (bytes == 0) bytes = 1;
    dst->data_ = static_cast<std::uint8_t*>(gamecrt::operator_new(bytes));
    std::memset(dst->data_, 0, bytes);
    std::memcpy(dst->data_, src->data_, src->size_);
}
void FreeStream(CMemoryStream* s) {
    if (s->data_) gamecrt::operator_delete(s->data_);
    s->data_ = nullptr;
}
bool SameBytes(const CMemoryStream* a, const CMemoryStream* b) {
    if (a->size_ != b->size_) return false;
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

// MFC 4.2 CFile: no handle and the shared empty name (nothing to release).
bool FileClosed(const void* file) {
    const char* name = At<const char*>(file, 0xC);
    return At<std::uint32_t>(file, 4) == 0xFFFFFFFFu && name && reinterpret_cast<const long*>(name)[-3] == -1;
}
bool FreshRecord(const FileStream& record) {
    return VtableOf(&record) == kFileStreamVtable && record.ok == 1 && record.limit == 2000000000u && FileClosed(record.file);
}

bool Alive(const CRefObject* o) { return o && !(o->flags7_ & CRefObject::kDetached); }
net::Connection* ConnectionOf(CRefObject* player) { return reinterpret_cast<net::Connection*>(player); }

// A copy of a framed link (the driver's size) whose inbox has a buffer of its own.
struct LinkCopy {
    alignas(8) std::uint8_t bytes[sizeof(DriverLink)];
    explicit LinkCopy(const FramedLink* link) {
        std::memcpy(bytes, link, sizeof(bytes));
        CloneStream(&Link()->inbox, &link->inbox);
    }
    ~LinkCopy() { FreeStream(&Link()->inbox); }
    LinkCopy(const LinkCopy&) = delete;
    LinkCopy& operator=(const LinkCopy&) = delete;
    FramedLink* Link() { return reinterpret_cast<FramedLink*>(bytes); }
    DriverLink* Driver() { return reinterpret_cast<DriverLink*>(bytes); }
};

// ================================================================================================
// FramedLink

// 00479B10: PUSH -1 (2), PUSH imm32 (5). The original on the real object, ours on a copy.
FileStream*(__fastcall* s_fsCopy)(FileStream*, void*, const FileStream*);
FileStream* __fastcall FileStreamCopyConstructVerify(FileStream* self, void*, const FileStream* other) {
    alignas(8) std::uint8_t copy[sizeof(FileStream)];
    std::memcpy(copy, self, sizeof(copy));
    FileStream* result = s_fsCopy(self, nullptr, other);
    FileStream* ours = FileStreamCopyConstruct(reinterpret_cast<FileStream*>(copy), nullptr, other);
    ei::verify::Check("FileStreamCopyConstruct", true,
                      result == self && ours == reinterpret_cast<FileStream*>(copy) && std::memcmp(copy, self, sizeof(copy)) == 0, "");
    return result;
}

// 00479B90: PUSH -1 (2), PUSH imm32 (5). With a closed file the original on a copy (its inbox
// buffer a copy, which it frees), ours on the real link; all but the freed buffer's pointer
// compared.
void(__fastcall* s_framedDestruct)(FramedLink*);
void __fastcall FramedDestructVerify(FramedLink* self) {
    if (!FileClosed(self->base.record.file)) {
        FramedDestruct(self);
        return;
    }
    alignas(8) std::uint8_t copy[sizeof(FramedLink)];
    std::memcpy(copy, self, sizeof(copy));
    auto* link = reinterpret_cast<FramedLink*>(copy);
    CloneStream(&link->inbox, &self->inbox);
    s_framedDestruct(link);
    FramedDestruct(self);
    constexpr std::size_t kData = offsetof(FramedLink, inbox) + offsetof(CMemoryStream, data_);
    const auto* real = reinterpret_cast<const std::uint8_t*>(self);
    bool same = std::memcmp(copy, real, kData) == 0 && std::memcmp(copy + kData + 4, real + kData + 4, sizeof(copy) - kData - 4) == 0;
    ei::verify::Check("FramedDestruct", true, same, "");
}

// 00479B70: a CALL at +3: ours only.
void* __fastcall FramedDeletingDestructorVerify(FramedLink* self, void*, unsigned flags) {
    void* result = FramedDeletingDestructor(self, nullptr, flags);
    ei::verify::Check("FramedDeletingDestructor", static_cast<void*>(self), result, "");
    return result;
}

// 0047B980: PUSH ESI (1), LEA ESI,[ECX+0x28] (3), PUSH EDI (1).
void(__fastcall* s_append)(FramedLink*, void*, const void*, unsigned);
void __fastcall FramedAppendVerify(FramedLink* self, void*, const void* data, unsigned size) {
    if (!IsMemoryStream(&self->inbox)) {
        FramedAppend(self, nullptr, data, size);
        return;
    }
    LinkCopy copy(self);
    s_append(copy.Link(), nullptr, data, size);
    FramedAppend(self, nullptr, data, size);
    ei::verify::Check("FramedAppend", true, SameStream(&copy.Link()->inbox, &self->inbox), "%u bytes", size);
}

// 0047B9B0: PUSH EBX, PUSH ESI, PUSH EDI (3), MOV EDI,[ESP+0x10] (4).
void(__fastcall* s_takeAll)(FramedLink*, void*, void*);
void __fastcall FramedTakeAllVerify(FramedLink* self, void*, void* out) {
    if (!IsMemoryStream(&self->inbox) || !IsMemoryStream(out)) {
        FramedTakeAll(self, nullptr, out);
        return;
    }
    LinkCopy copy(self);
    CMemoryStream target;
    CloneStream(&target, static_cast<CMemoryStream*>(out));
    unsigned before = self->inbox.size_;
    s_takeAll(copy.Link(), nullptr, &target);
    FramedTakeAll(self, nullptr, out);
    bool same = SameStream(&copy.Link()->inbox, &self->inbox) && SameStream(&target, static_cast<CMemoryStream*>(out));
    ei::verify::Check("FramedTakeAll", true, same, "%u bytes", before);
    FreeStream(&target);
}

// The inbox queries: the original on a copy with its own inbox; the result and the inbox (the
// position they leave) compared. 0047B9F0 / 0047BA40: PUSH ECX (1), MOV EAX,[ECX+0x28] (3),
// PUSH ESI / PUSH EBX (1).
template <typename Ours, typename Original>
int InboxQuery(const char* name, FramedLink* self, Ours ours, Original original) {
    if (!IsMemoryStream(&self->inbox)) return ours(self);
    LinkCopy copy(self);
    int expected = original(copy.Link());
    int actual = ours(self);
    ei::verify::Check(name, true, expected == actual && SameStream(&copy.Link()->inbox, &self->inbox), "%d / %d, %u bytes", expected,
                      actual, self->inbox.size_);
    return actual;
}
int(__fastcall* s_hasMessage)(FramedLink*);
int __fastcall FramedHasMessageVerify(FramedLink* self) {
    return InboxQuery("FramedHasMessage", self, [](FramedLink* l) { return FramedHasMessage(l); },
                      [](FramedLink* l) { return s_hasMessage(l); });
}
int(__fastcall* s_messageCount)(FramedLink*);
int __fastcall FramedMessageCountVerify(FramedLink* self) {
    return InboxQuery("FramedMessageCount", self, [](FramedLink* l) { return FramedMessageCount(l); },
                      [](FramedLink* l) { return s_messageCount(l); });
}

// 0047BB70: PUSH ECX, PUSH EBX (2), MOV EBX,ECX (2), PUSH ESI (1). Copies of the inbox and of the
// target; the result and both streams compared.
bool(__fastcall* s_receive)(FramedLink*, void*, CMemoryStream*);
bool __fastcall FramedReceiveVerify(FramedLink* self, void*, CMemoryStream* stream) {
    if (!IsMemoryStream(&self->inbox) || !IsMemoryStream(stream)) return FramedReceive(self, nullptr, stream);
    LinkCopy copy(self);
    CMemoryStream target;
    CloneStream(&target, stream);
    unsigned before = self->inbox.size_;
    bool expected = s_receive(copy.Link(), nullptr, &target);
    bool actual = FramedReceive(self, nullptr, stream);
    bool same = expected == actual && SameStream(&copy.Link()->inbox, &self->inbox) && SameStream(&target, stream);
    ei::verify::Check("FramedReceive", true, same, "%d / %d, %u bytes queued", expected, actual, before);
    FreeStream(&target);
    return actual;
}

// --- Send: the link's slots 13 and 11 recorded and replayed --------------------------------------
constexpr int kLinkSlots = 14;
struct SendLog {
    bool recording;
    void* const* realVtable;
    int sends;                    // slot 13 calls (the original makes at most one)
    unsigned size;
    std::uint8_t bytes[0x8000];
    int result;
    int disconnects;              // slot 11 calls
    int replayedSends;
    int replayedDisconnects;
    bool mismatch;
};
SendLog g_send;

int __fastcall RecordSendBytes(FramedLink* self, void*, const void* data, unsigned size) {
    if (g_send.recording) {
        void* proxy;
        std::memcpy(&proxy, self, sizeof(proxy));
        SetVtablePtr(self, g_send.realVtable);
        int r = ei::VCall<int>(self, 13, data, size);
        SetVtablePtr(self, proxy);
        if (g_send.sends++ == 0) {
            g_send.size = size;
            std::memcpy(g_send.bytes, data, size <= sizeof(g_send.bytes) ? size : sizeof(g_send.bytes));
            g_send.result = r;
        }
        return r;
    }
    if (g_send.replayedSends++ != 0 || size != g_send.size ||
        std::memcmp(g_send.bytes, data, size <= sizeof(g_send.bytes) ? size : sizeof(g_send.bytes)) != 0)
        g_send.mismatch = true;
    return g_send.result;
}
void __fastcall RecordDisconnect(FramedLink* self) {
    if (g_send.recording) {
        void* proxy;
        std::memcpy(&proxy, self, sizeof(proxy));
        SetVtablePtr(self, g_send.realVtable);
        ei::VCall<void>(self, 11);
        SetVtablePtr(self, proxy);
        ++g_send.disconnects;
        return;
    }
    ++g_send.replayedDisconnects;
}
void* const* RecorderVtable(void* const* real) {
    static void* vtable[kLinkSlots];
    for (int i = 0; i < kLinkSlots; ++i) vtable[i] = real[i];
    vtable[11] = reinterpret_cast<void*>(&RecordDisconnect);
    vtable[13] = reinterpret_cast<void*>(&RecordSendBytes);
    return vtable;
}
bool g_sendBusy;

// 0047BAD0: MOV EAX,0x8000 (5).
bool(__fastcall* s_send)(FramedLink*, void*, void*);
bool __fastcall FramedSendVerify(FramedLink* self, void*, void* stream) {
    std::uintptr_t vt = VtableOf(self);
    if (g_sendBusy || !IsMemoryStream(stream) || (vt != kDriverVtable && vt != 0x0073CA10)) return FramedSend(self, nullptr, stream);
    g_sendBusy = true;
    alignas(8) std::uint8_t copy[sizeof(DriverLink)];
    std::memcpy(copy, self, sizeof(copy));
    CMemoryStream source;
    CloneStream(&source, static_cast<CMemoryStream*>(stream));

    auto* real = reinterpret_cast<void* const*>(vt);
    g_send.recording = true;
    g_send.realVtable = real;
    g_send.sends = g_send.disconnects = g_send.replayedSends = g_send.replayedDisconnects = 0;
    g_send.mismatch = false;
    SetVtablePtr(self, RecorderVtable(real));
    bool actual = FramedSend(self, nullptr, stream);
    SetVtablePtr(self, real);

    g_send.recording = false;
    SetVtablePtr(copy, RecorderVtable(real));
    bool expected = s_send(reinterpret_cast<FramedLink*>(copy), nullptr, &source);
    bool same = expected == actual && !g_send.mismatch && g_send.replayedSends == g_send.sends &&
                g_send.replayedDisconnects == g_send.disconnects && SameStream(&source, static_cast<CMemoryStream*>(stream));
    ei::verify::Check("FramedSend", true, same, "%d / %d, %u bytes, %d sends, %d disconnects", expected, actual,
                      static_cast<CMemoryStream*>(stream)->size_, g_send.sends, g_send.disconnects);
    FreeStream(&source);
    g_sendBusy = false;
    return actual;
}

// ================================================================================================
// DriverLink

// 00479780: MOV EAX,[ECX+0x50] (3), TEST EAX,EAX (2).
int(__fastcall* s_connected)(DriverLink*);
int __fastcall DriverConnectedVerify(DriverLink* self) {
    int expected = s_connected(self);
    int actual = DriverConnected(self);
    ei::verify::Check("DriverConnected", expected, actual, "");
    return actual;
}

// A byte copy of the player for the original's run (Pump and SendBytes read it and change only
// its reliable buffers' fields).
alignas(8) std::uint8_t g_player[sizeof(net::Connection)];

bool SameBufferFields(const GameBuffer& a, const GameBuffer& b) { return std::memcmp(&a, &b, sizeof(GameBuffer)) == 0; }

// 004797A0: PUSH ESI (1), MOV ESI,[ECX+0x50] (3), TEST ESI,ESI (2).
void(__fastcall* s_pump)(DriverLink*);
void __fastcall DriverPumpVerify(DriverLink* self) {
    if (!Alive(self->player)) {
        s_pump(self);
        DriverPump(self);
        ei::verify::Check("DriverPump", true, true, "");
        return;
    }
    if (!IsMemoryStream(&self->base.inbox)) {
        DriverPump(self);
        return;
    }
    LinkCopy copy(&self->base);
    std::memcpy(g_player, self->player, sizeof(g_player));
    copy.Driver()->player = reinterpret_cast<CRefObject*>(g_player);
    GameBuffer& in = ConnectionOf(self->player)->reliableIn;
    unsigned before = static_cast<unsigned>(in.cur - in.begin);
    s_pump(copy.Driver());
    DriverPump(self);
    const GameBuffer& expectedIn = reinterpret_cast<net::Connection*>(g_player)->reliableIn;
    bool same = SameStream(&copy.Link()->inbox, &self->base.inbox) && SameBufferFields(expectedIn, in);
    ei::verify::Check("DriverPump", true, same, "%u bytes", before);
}

// 004796B0: PUSH EBX (1), MOV EBX,ECX (2), PUSH EBP (1), PUSH ESI (1).
int(__fastcall* s_sendBytes)(DriverLink*, void*, const void*, unsigned);
int __fastcall DriverSendBytesVerify(DriverLink* self, void*, const void* data, unsigned size) {
    if (!Alive(self->player)) {
        int expected = s_sendBytes(self, nullptr, data, size);
        int actual = DriverSendBytes(self, nullptr, data, size);
        ei::verify::Check("DriverSendBytes", expected, actual, "no player");
        return actual;
    }
    GameBuffer& out = ConnectionOf(self->player)->reliableOut;
    if (VtableOf(&out) != kGrowingBufferVtable) return DriverSendBytes(self, nullptr, data, size);
    alignas(8) std::uint8_t copy[sizeof(DriverLink)];
    std::memcpy(copy, self, sizeof(copy));
    std::memcpy(g_player, self->player, sizeof(g_player));
    reinterpret_cast<DriverLink*>(copy)->player = reinterpret_cast<CRefObject*>(g_player);
    GameBuffer& scratch = reinterpret_cast<net::Connection*>(g_player)->reliableOut;
    GameBufferCopy(&scratch, nullptr, &out);
    scratch.base = out.base;

    int expected = s_sendBytes(reinterpret_cast<DriverLink*>(copy), nullptr, data, size);
    int actual = DriverSendBytes(self, nullptr, data, size);
    auto offset = [](const GameBuffer& b, const std::uint8_t* p) { return static_cast<std::ptrdiff_t>(p - b.begin); };
    bool same = expected == actual && VtableOf(&scratch) == VtableOf(&out) && offset(scratch, scratch.cur) == offset(out, out.cur) &&
                offset(scratch, scratch.high) == offset(out, out.high) && offset(scratch, scratch.end) == offset(out, out.end) &&
                scratch.base == out.base && scratch.flags == out.flags;
    if (same) {
        std::ptrdiff_t used = offset(out, out.cur > out.high ? out.cur : out.high);
        same = used <= 0 || std::memcmp(scratch.begin, out.begin, static_cast<std::size_t>(used)) == 0;
    }
    ei::verify::Check("DriverSendBytes", true, same, "%d / %d, %u bytes", expected, actual, size);
    GrowingBufferDestruct(&scratch);
    return actual;
}

// 00479810: ours only. No player afterwards and an empty inbox. When the player was alive and
// outlives the call (another reference) and the inbox held bytes, its incoming stream now starts
// with those bytes, followed by what it held before.
constexpr std::uint32_t kKeep = 0x10000;
std::uint8_t g_unread[kKeep];
std::uint8_t g_incoming[kKeep];
void __fastcall DriverDisconnectVerify(DriverLink* self) {
    CRefObject* player = self->player;
    bool checkStream = Alive(player) && player->refs_ > 1 && self->base.inbox.size_ != 0;
    std::uint32_t unread = self->base.inbox.size_;
    std::uint32_t incoming = 0;
    if (checkStream) {
        GameBuffer& in = ConnectionOf(player)->reliableIn;
        const std::uint8_t* high = in.cur > in.high ? in.cur : in.high;
        incoming = static_cast<std::uint32_t>(in.base + (high - in.begin));
        checkStream = in.base == 0 && unread <= kKeep && incoming <= kKeep;
        if (checkStream) {
            std::memcpy(g_unread, self->base.inbox.data_, unread);
            std::memcpy(g_incoming, in.begin, incoming);
        }
    }
    DriverDisconnect(self);
    bool ok = self->player == nullptr && self->base.inbox.size_ == 0;
    if (ok && checkStream) {
        GameBuffer& in = ConnectionOf(player)->reliableIn;
        ok = static_cast<std::uint32_t>(in.cur - in.begin) == unread + incoming && std::memcmp(in.begin, g_unread, unread) == 0 &&
             std::memcmp(in.begin + unread, g_incoming, incoming) == 0;
    }
    ei::verify::Check("DriverDisconnect", true, ok, "%u unread, %u incoming", unread, incoming);
}

// 00479D40: ours only; the root, CStream and CStream vtables left, no player.
void __fastcall DriverDestructVerify(DriverLink* self) {
    DriverDestruct(self);
    bool ok = VtableOf(self) == 0x0073BB68 && VtableOf(&self->base.base.record) == 0x0073BDB0 && VtableOf(&self->base.inbox) == 0x0073BDB0 &&
              self->player == nullptr;
    ei::verify::Check("DriverDestruct", true, ok, "");
}

// 00479D20: a CALL at +3: ours only.
void* __fastcall DriverDeletingDestructorVerify(DriverLink* self, void*, unsigned flags) {
    void* result = DriverDeletingDestructor(self, nullptr, flags);
    ei::verify::Check("DriverDeletingDestructor", static_cast<void*>(self), result, "");
    return result;
}

// An empty inbox as the driver's constructor leaves it.
bool FreshInbox(const CMemoryStream& s) {
    return VtableOf(&s) == 0x0073BD90 && s.ok_ == 1 && s.unknown_8 == 2000000000u && s.growBy_ == 0x80 && s.unknown_14 == -1 &&
           s.data_ == nullptr && s.unknown_1C == 0 && s.position_ == 0 && s.size_ == 0;
}

// 00479C10: ours only; afterwards a fresh driver with the same count, flags and +9.
void __fastcall DriverResetVerify(DriverLink* self) {
    std::uint32_t header = At<std::uint32_t>(self, 4);
    std::uint8_t skip = self->base.base.skipNext;
    DriverReset(self);
    bool ok = VtableOf(self) == kDriverVtable && At<std::uint32_t>(self, 4) == header && self->base.base.skipNext == skip &&
              self->base.base.recording == 0 && FreshRecord(self->base.base.record) && FreshInbox(self->base.inbox) && self->player == nullptr;
    ei::verify::Check("DriverReset", true, ok, "");
}

// 00479A30: PUSH -1 (2), PUSH imm32 (5). Both clone, the original from a copy of the source with
// its own inbox buffer. The clones compared: count, flags, the record, the inbox's fields (its
// bytes are the allocation's leftovers) and the player. The original's clone taken apart by hand:
// its buffer freed, its reference to the player dropped (the source still holds one), the block
// freed (its file is closed, its name the shared empty string).
DriverLink*(__fastcall* s_clone)(DriverLink*);
bool SameCloneInbox(const CMemoryStream& a, const CMemoryStream& b) {
    const auto* x = reinterpret_cast<const std::uint8_t*>(&a);
    const auto* y = reinterpret_cast<const std::uint8_t*>(&b);
    return std::memcmp(x, y, 4) == 0 && x[5] == y[5] && std::memcmp(x + 8, y + 8, 4) == 0 && std::memcmp(x + 0x10, y + 0x10, 8) == 0 &&
           std::memcmp(x + 0x1C, y + 0x1C, 0xC) == 0;
}
DriverLink* __fastcall DriverCloneVerify(DriverLink* self) {
    if (!IsMemoryStream(&self->base.inbox)) return DriverClone(self);
    LinkCopy source(&self->base);
    DriverLink* expected = s_clone(source.Driver());
    DriverLink* actual = DriverClone(self);
    if (expected && actual) {
        const auto* x = reinterpret_cast<const std::uint8_t*>(&expected->base.base.record);
        const auto* y = reinterpret_cast<const std::uint8_t*>(&actual->base.base.record);
        bool same = std::memcmp(expected, actual, 10) == 0 && std::memcmp(x, y, 4) == 0 && x[5] == y[5] &&
                    std::memcmp(x + 8, y + 8, 4 + 0x10) == 0 && SameCloneInbox(expected->base.inbox, actual->base.inbox) &&
                    expected->player == actual->player;
        ei::verify::Check("DriverClone", true, same, "%u bytes", self->base.inbox.size_);
    }
    if (expected) {
        if (expected->base.inbox.data_) gamecrt::operator_delete(expected->base.inbox.data_);
        if (expected->player) --expected->player->refs_;
        gamecrt::operator_delete(expected);
    }
    return actual;
}

} // namespace

EI_VERIFY(0x00479B10, FileStreamCopyConstructVerify, 7, s_fsCopy);
EI_VERIFY(0x00479B90, FramedDestructVerify, 7, s_framedDestruct);
EI_VERIFY(0x00479B70, FramedDeletingDestructorVerify, 0, s_unused);
EI_VERIFY(0x0047B980, FramedAppendVerify, 5, s_append);
EI_VERIFY(0x0047B9B0, FramedTakeAllVerify, 7, s_takeAll);
EI_VERIFY(0x0047B9F0, FramedHasMessageVerify, 5, s_hasMessage);
EI_VERIFY(0x0047BA40, FramedMessageCountVerify, 5, s_messageCount);
EI_VERIFY(0x0047BB70, FramedReceiveVerify, 5, s_receive);
EI_VERIFY(0x0047BAD0, FramedSendVerify, 5, s_send);
EI_VERIFY(0x00479780, DriverConnectedVerify, 5, s_connected);
EI_VERIFY(0x004797A0, DriverPumpVerify, 6, s_pump);
EI_VERIFY(0x004796B0, DriverSendBytesVerify, 5, s_sendBytes);
EI_VERIFY(0x00479810, DriverDisconnectVerify, 0, s_unused);
EI_VERIFY(0x00479D40, DriverDestructVerify, 0, s_unused);
EI_VERIFY(0x00479D20, DriverDeletingDestructorVerify, 0, s_unused);
EI_VERIFY(0x00479C10, DriverResetVerify, 0, s_unused);
EI_VERIFY(0x00479A30, DriverCloneVerify, 7, s_clone);
