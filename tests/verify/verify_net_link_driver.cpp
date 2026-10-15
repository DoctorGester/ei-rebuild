// Checks for the framed link (0073C9C8) and the per-player driver (0073C990) in net_link.cpp.
//
// Nothing a scenario does reaches them: drivers are made for the connections of remote players
// (00479E30 / 0047A080), and harness runs have none. The checks are written for a network game.
//
// Memory streams (the inbox at +0x28, the streams passed in) are deep-copied for the original's
// run: header plus a private buffer from the game heap; compared field by field (the buffer
// pointer excepted) and up to the size. Ours runs first: the copies' bytes are kept in our memory
// before it and their game-heap buffers made after it (the game reads the heap's address order,
// SV2f).
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
#include <vector>

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
    std::uint32_t bytes = src->capacity_ > src->size_ ? src->capacity_ : src->size_;
    if (bytes == 0) bytes = 1;
    dst->data_ = static_cast<std::uint8_t*>(gamecrt::operator_new(bytes));
    std::memset(dst->data_, 0, bytes);
    std::memcpy(dst->data_, src->data_, src->size_);
}
void FreeStream(CMemoryStream* s) {
    if (s->data_) gamecrt::operator_delete(s->data_);
    s->data_ = nullptr;
}
// A stream's fields and bytes kept in our memory before ours runs; CloneTo gives a copy of it a
// game-heap buffer of its own afterwards (as CloneStream would have before), so that ours' blocks
// land where the original's would without the check (the game reads the heap's address order,
// SV2f).
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
    return VtableOf(&record) == kFileStreamVtable && record.ok_ == 1 && record.version_ == 2000000000u && FileClosed(record.file_);
}

bool Alive(const CRefObject* o) { return o && !(o->flags7_ & CRefObject::kDetached); }

// A copy of a framed link (the driver's size) whose inbox has a buffer of its own. The constructor
// keeps the bytes in our memory (taken before ours runs); Clone, called after ours ran, gives the
// copy its game-heap inbox buffer (SV2f).
struct LinkCopy {
    alignas(8) std::uint8_t bytes[sizeof(DriverLink)];
    KeptStream inbox;
    explicit LinkCopy(const FramedLink* link) : inbox(&link->inbox_) {
        std::memcpy(bytes, link, sizeof(bytes));
        Link()->inbox_.data_ = nullptr;   // until Clone
    }
    void Clone() { inbox.CloneTo(&Link()->inbox_); }
    ~LinkCopy() { FreeStream(&Link()->inbox_); }
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
    FileStream* ours = reinterpret_cast<FileStream*>(copy)->CopyConstruct(other);
    ei::verify::Check("FileStreamCopyConstruct", true,
                      result == self && ours == reinterpret_cast<FileStream*>(copy) && std::memcmp(copy, self, sizeof(copy)) == 0, "");
    return result;
}

// 00479B90: PUSH -1 (2), PUSH imm32 (5). With a closed file the original on a copy (its inbox
// buffer a copy, which it frees), ours on the real link; all but the freed buffer's pointer
// compared.
void(__fastcall* s_framedDestruct)(FramedLink*);
void __fastcall FramedDestructVerify(FramedLink* self) {
    if (!FileClosed(self->record_.file_)) {
        self->Destruct();
        return;
    }
    alignas(8) std::uint8_t copy[sizeof(FramedLink)];
    std::memcpy(copy, self, sizeof(copy));
    auto* link = reinterpret_cast<FramedLink*>(copy);
    CloneStream(&link->inbox_, &self->inbox_);
    s_framedDestruct(link);
    self->Destruct();
    constexpr std::size_t kData = offsetof(FramedLink, inbox_) + offsetof(CMemoryStream, data_);
    const auto* real = reinterpret_cast<const std::uint8_t*>(self);
    bool same = std::memcmp(copy, real, kData) == 0 && std::memcmp(copy + kData + 4, real + kData + 4, sizeof(copy) - kData - 4) == 0;
    ei::verify::Check("FramedDestruct", true, same, "");
}

// 00479B70: a CALL at +3: ours only.
void* __fastcall FramedDeletingDestructorVerify(FramedLink* self, void*, unsigned flags) {
    void* result = static_cast<void*>(self->DeletingDestruct(flags));
    ei::verify::Check("FramedDeletingDestructor", static_cast<void*>(self), result, "");
    return result;
}

// 0047B980: PUSH ESI (1), LEA ESI,[ECX+0x28] (3), PUSH EDI (1).
void(__fastcall* s_append)(FramedLink*, void*, const void*, unsigned);
void __fastcall FramedAppendVerify(FramedLink* self, void*, const void* data, unsigned size) {
    if (!IsMemoryStream(&self->inbox_)) {
        self->Append(data, size);
        return;
    }
    LinkCopy copy(self);
    self->Append(data, size);
    copy.Clone();
    s_append(copy.Link(), nullptr, data, size);
    ei::verify::Check("FramedAppend", true, SameStream(&copy.Link()->inbox_, &self->inbox_), "%u bytes", size);
}

// 0047B9B0: PUSH EBX, PUSH ESI, PUSH EDI (3), MOV EDI,[ESP+0x10] (4).
void(__fastcall* s_takeAll)(FramedLink*, void*, void*);
void __fastcall FramedTakeAllVerify(FramedLink* self, void*, void* out) {
    if (!IsMemoryStream(&self->inbox_) || !IsMemoryStream(out)) {
        self->TakeAll(static_cast<CStream*>(out));
        return;
    }
    LinkCopy copy(self);
    KeptStream keptTarget(static_cast<CMemoryStream*>(out));
    unsigned before = self->inbox_.size_;
    self->TakeAll(static_cast<CStream*>(out));
    copy.Clone();
    CMemoryStream target;
    keptTarget.CloneTo(&target);
    s_takeAll(copy.Link(), nullptr, &target);
    bool same = SameStream(&copy.Link()->inbox_, &self->inbox_) && SameStream(&target, static_cast<CMemoryStream*>(out));
    ei::verify::Check("FramedTakeAll", true, same, "%u bytes", before);
    FreeStream(&target);
}

// The inbox queries: the original on a copy with its own inbox; the result and the inbox (the
// position they leave) compared. 0047B9F0 / 0047BA40: PUSH ECX (1), MOV EAX,[ECX+0x28] (3),
// PUSH ESI / PUSH EBX (1).
template <typename Ours, typename Original>
int InboxQuery(const char* name, FramedLink* self, Ours ours, Original original) {
    if (!IsMemoryStream(&self->inbox_)) return ours(self);
    LinkCopy copy(self);
    int actual = ours(self);
    copy.Clone();
    int expected = original(copy.Link());
    ei::verify::Check(name, true, expected == actual && SameStream(&copy.Link()->inbox_, &self->inbox_), "%d / %d, %u bytes", expected,
                      actual, self->inbox_.size_);
    return actual;
}
int(__fastcall* s_hasMessage)(FramedLink*);
int __fastcall FramedHasMessageVerify(FramedLink* self) {
    return InboxQuery("FramedHasMessage", self, [](FramedLink* l) { return (l->FramedLink::HasIncoming() ? 1 : 0); },
                      [](FramedLink* l) { return s_hasMessage(l); });
}
int(__fastcall* s_messageCount)(FramedLink*);
int __fastcall FramedMessageCountVerify(FramedLink* self) {
    return InboxQuery("FramedMessageCount", self, [](FramedLink* l) { return l->FramedLink::IncomingCount(); },
                      [](FramedLink* l) { return s_messageCount(l); });
}

// 0047BB70: PUSH ECX, PUSH EBX (2), MOV EBX,ECX (2), PUSH ESI (1). Copies of the inbox and of the
// target; the result and both streams compared.
bool(__fastcall* s_receive)(FramedLink*, void*, CMemoryStream*);
bool __fastcall FramedReceiveVerify(FramedLink* self, void*, CMemoryStream* stream) {
    if (!IsMemoryStream(&self->inbox_) || !IsMemoryStream(stream)) return self->FramedLink::Receive(stream);
    LinkCopy copy(self);
    KeptStream keptTarget(stream);
    unsigned before = self->inbox_.size_;
    bool actual = self->FramedLink::Receive(stream);
    copy.Clone();
    CMemoryStream target;
    keptTarget.CloneTo(&target);
    bool expected = s_receive(copy.Link(), nullptr, &target);
    bool same = expected == actual && SameStream(&copy.Link()->inbox_, &self->inbox_) && SameStream(&target, stream);
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
    if (g_sendBusy || !IsMemoryStream(stream) || (vt != kDriverVtable && vt != 0x0073CA10)) return self->FramedLink::Send(static_cast<CStream*>(stream));
    g_sendBusy = true;
    alignas(8) std::uint8_t copy[sizeof(DriverLink)];
    std::memcpy(copy, self, sizeof(copy));
    KeptStream keptSource(static_cast<CMemoryStream*>(stream));

    auto* real = reinterpret_cast<void* const*>(vt);
    g_send.recording = true;
    g_send.realVtable = real;
    g_send.sends = g_send.disconnects = g_send.replayedSends = g_send.replayedDisconnects = 0;
    g_send.mismatch = false;
    SetVtablePtr(self, RecorderVtable(real));
    bool actual = self->FramedLink::Send(static_cast<CStream*>(stream));
    SetVtablePtr(self, real);

    g_send.recording = false;
    CMemoryStream source;
    keptSource.CloneTo(&source);   // after ours (SV2f)
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
    int actual = (self->DriverLink::Connected() ? 1 : 0);
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
    if (!Alive(self->player_)) {
        s_pump(self);
        self->DriverLink::Pump();
        ei::verify::Check("DriverPump", true, true, "");
        return;
    }
    if (!IsMemoryStream(&self->inbox_)) {
        self->DriverLink::Pump();
        return;
    }
    // Ours first (Pump only reads the player's incoming block and resets its fields), then the
    // original on the copies taken before (SV2f).
    LinkCopy copy(self);
    std::memcpy(g_player, self->player_, sizeof(g_player));
    copy.Driver()->player_ = reinterpret_cast<net::Connection*>(g_player);
    GameBuffer& in = self->player_->reliableIn_;
    unsigned before = static_cast<unsigned>(in.cur - in.begin);
    self->DriverLink::Pump();
    copy.Clone();
    s_pump(copy.Driver());
    const GameBuffer& expectedIn = reinterpret_cast<net::Connection*>(g_player)->reliableIn_;
    bool same = SameStream(&copy.Link()->inbox_, &self->inbox_) && SameBufferFields(expectedIn, in);
    ei::verify::Check("DriverPump", true, same, "%u bytes", before);
}

// 004796B0: PUSH EBX (1), MOV EBX,ECX (2), PUSH EBP (1), PUSH ESI (1).
int(__fastcall* s_sendBytes)(DriverLink*, void*, const void*, unsigned);
int __fastcall DriverSendBytesVerify(DriverLink* self, void*, const void* data, unsigned size) {
    if (!Alive(self->player_)) {
        int expected = s_sendBytes(self, nullptr, data, size);
        int actual = (self->DriverLink::SendBytes(data, size) ? 1 : 0);
        ei::verify::Check("DriverSendBytes", expected, actual, "no player");
        return actual;
    }
    GameBuffer& out = self->player_->reliableOut_;
    if (VtableOf(&out) != kGrowingBufferVtable) return (self->DriverLink::SendBytes(data, size) ? 1 : 0);
    alignas(8) std::uint8_t copy[sizeof(DriverLink)];
    std::memcpy(copy, self, sizeof(copy));
    std::memcpy(g_player, self->player_, sizeof(g_player));
    reinterpret_cast<DriverLink*>(copy)->player_ = reinterpret_cast<net::Connection*>(g_player);
    // The outgoing buffer is kept in our memory (its fields relocated into the kept bytes); ours
    // runs first and the scratch's game-heap copy of it is made after (SV2f).
    alignas(8) std::uint8_t keptRaw[sizeof(GameBuffer)];
    std::memcpy(keptRaw, &out, sizeof(GameBuffer));
    auto& keptOut = *reinterpret_cast<GameBuffer*>(keptRaw);
    std::vector<std::uint8_t> keptBytes;
    if (out.begin) {
        keptBytes.assign(out.begin, out.end);
        keptBytes.push_back(0);   // never empty
        keptOut.cur = keptBytes.data() + (out.cur - out.begin);
        keptOut.high = keptBytes.data() + (out.high - out.begin);
        keptOut.end = keptBytes.data() + (out.end - out.begin);
        keptOut.begin = keptBytes.data();
    }

    int actual = (self->DriverLink::SendBytes(data, size) ? 1 : 0);
    GameBuffer& scratch = reinterpret_cast<net::Connection*>(g_player)->reliableOut_;
    GameBufferCopy(&scratch, nullptr, &keptOut);
    scratch.base = keptOut.base;
    int expected = s_sendBytes(reinterpret_cast<DriverLink*>(copy), nullptr, data, size);
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
    net::Connection* player = self->player_;
    bool checkStream = Alive(player) && player->refs_ > 1 && self->inbox_.size_ != 0;
    std::uint32_t unread = self->inbox_.size_;
    std::uint32_t incoming = 0;
    if (checkStream) {
        GameBuffer& in = player->reliableIn_;
        const std::uint8_t* high = in.cur > in.high ? in.cur : in.high;
        incoming = static_cast<std::uint32_t>(in.base + (high - in.begin));
        checkStream = in.base == 0 && unread <= kKeep && incoming <= kKeep;
        if (checkStream) {
            std::memcpy(g_unread, self->inbox_.data_, unread);
            std::memcpy(g_incoming, in.begin, incoming);
        }
    }
    self->DriverLink::Disconnect();
    bool ok = self->player_ == nullptr && self->inbox_.size_ == 0;
    if (ok && checkStream) {
        GameBuffer& in = player->reliableIn_;
        ok = static_cast<std::uint32_t>(in.cur - in.begin) == unread + incoming && std::memcmp(in.begin, g_unread, unread) == 0 &&
             std::memcmp(in.begin + unread, g_incoming, incoming) == 0;
    }
    ei::verify::Check("DriverDisconnect", true, ok, "%u unread, %u incoming", unread, incoming);
}

// 00479D40: ours only; the root, CStream and CStream vtables left, no player.
void __fastcall DriverDestructVerify(DriverLink* self) {
    self->Destruct();
    bool ok = VtableOf(self) == 0x0073BB68 && VtableOf(&self->record_) == 0x0073BDB0 && VtableOf(&self->inbox_) == 0x0073BDB0 &&
              self->player_ == nullptr;
    ei::verify::Check("DriverDestruct", true, ok, "");
}

// 00479D20: a CALL at +3: ours only.
void* __fastcall DriverDeletingDestructorVerify(DriverLink* self, void*, unsigned flags) {
    void* result = static_cast<void*>(self->DeletingDestruct(flags));
    ei::verify::Check("DriverDeletingDestructor", static_cast<void*>(self), result, "");
    return result;
}

// An empty inbox as the driver's constructor leaves it.
bool FreshInbox(const CMemoryStream& s) {
    return VtableOf(&s) == 0x0073BD90 && s.ok_ == 1 && s.version_ == 2000000000u && s.bitMask_ == 0x80 && s.bitsIndex_ == -1 &&
           s.data_ == nullptr && s.capacity_ == 0 && s.position_ == 0 && s.size_ == 0;
}

// 00479C10: ours only; afterwards a fresh driver with the same count, flags and +9.
void __fastcall DriverResetVerify(DriverLink* self) {
    std::uint32_t header = At<std::uint32_t>(self, 4);
    std::uint8_t skip = self->skipNext_;
    self->DriverLink::OnShutdown();
    bool ok = VtableOf(self) == kDriverVtable && At<std::uint32_t>(self, 4) == header && self->skipNext_ == skip &&
              self->recording_ == 0 && FreshRecord(self->record_) && FreshInbox(self->inbox_) && self->player_ == nullptr;
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
    if (!IsMemoryStream(&self->inbox_)) return self->DriverLink::Clone();
    LinkCopy source(self);
    DriverLink* actual = self->DriverLink::Clone();   // ours first (SV2f)
    source.Clone();
    DriverLink* expected = s_clone(source.Driver());
    if (expected && actual) {
        const auto* x = reinterpret_cast<const std::uint8_t*>(&expected->record_);
        const auto* y = reinterpret_cast<const std::uint8_t*>(&actual->record_);
        bool same = std::memcmp(expected, actual, 10) == 0 && std::memcmp(x, y, 4) == 0 && x[5] == y[5] &&
                    std::memcmp(x + 8, y + 8, 4 + 0x10) == 0 && SameCloneInbox(expected->inbox_, actual->inbox_) &&
                    expected->player_ == actual->player_;
        ei::verify::Check("DriverClone", true, same, "%u bytes", self->inbox_.size_);
    }
    if (expected) {
        if (expected->inbox_.data_) gamecrt::operator_delete(expected->inbox_.data_);
        if (expected->player_) --expected->player_->refs_;
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
