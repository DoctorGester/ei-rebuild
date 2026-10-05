// The game client's timed entries and commands: an object kept on the timers list (0041D1A0), a
// command message for the server (0041D260), the client's own server entry (0041D420, slot 16), the
// start of the game (0041D410, slot 15) and the ratio of a received buffer (0041D9A0).
// See net_classes.h and docs/notes/unmapped_410_470.md ("Seventh round").
#include "net_classes.h"
#include "core_refcount.h"
#include "net_coder.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

RefPtr* __fastcall RefPtrFromRaw(RefPtr* self, CRefObject* object);   // refptr_copies.cpp

namespace net {

RefPtr* __fastcall RefPtrCopyConstruct(RefPtr* self, void*, const RefPtr* other);   // net_server_awards.cpp

namespace {

// STLport (library): the timers list (+0x2604: {seconds, counted object}) end / insert / push_back,
// string reserve, allocator proxy, block allocation, range assign, erase, append.
__declspec(naked) void** __fastcall TimersEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x004210C0)
__declspec(naked) void** __fastcall TimersInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const void* /*entry*/) EI_ORIGINAL(0x00426230)
__declspec(naked) void __fastcall TimersPushBack(void* /*list*/, void*, const void* /*entry*/) EI_ORIGINAL(0x004246C0)
__declspec(naked) void __fastcall StringAllocProxy(char** /*eos*/, void*, const void* /*alloc*/, char* const* /*value*/) EI_ORIGINAL(0x00425080)
__declspec(naked) void __fastcall StringAllocateBlock(StringRep* /*s*/, void*, std::size_t /*n*/) EI_ORIGINAL(0x00424E40)
__declspec(naked) void __fastcall StringReserve(StringRep* /*s*/, void*, const void* /*alloc*/, std::size_t /*n*/) EI_ORIGINAL(0x00421800)
__declspec(naked) void __fastcall StringAssignRange(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/) EI_ORIGINAL(0x00426A80)
__declspec(naked) void __fastcall StringErase(StringRep* /*s*/, void*, char* /*first*/, char* /*last*/) EI_ORIGINAL(0x0040B3C0)
__declspec(naked) void __fastcall StringAppendTag(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/, const void* /*tag*/) EI_ORIGINAL(0x0040C280)
__declspec(naked) void* __cdecl GameMemmove(void* /*to*/, const void* /*from*/, std::size_t /*n*/) EI_ORIGINAL(0x006E79A0)
__declspec(naked) void __fastcall ServerEntryConstructBase(void* /*entry*/) EI_ORIGINAL(0x004413E0)

constexpr std::uintptr_t kCommandVtable = 0x0073BC40;       // 0x1C bytes
constexpr std::uintptr_t kServerEntryVtable = 0x0073BB28;
constexpr std::uintptr_t kEmptyText = 0x00783D6C;
constexpr std::uint32_t kCommandLifetime = 0x42700000;     // 60.0f

const char* Text(std::uintptr_t a) { return reinterpret_cast<const char*>(a); }
void SetVtable(void* p, std::uintptr_t v) { std::memcpy(p, &v, 4); }

// A timers list entry.
struct TimedEntry {
    std::uint32_t seconds;   // float
    RefPtr object;
};

// RefPtr::Destroy as the callers inline it (the release is not null-checked after the shutdown,
// and done inline: one reference less, deleted at zero).
void DestroyInline(RefPtr* p) {
    CRefObject* object = p->ptr;
    if (!object) return;
    if (object->refs_ != 1) {
        object->flags7_ |= CRefObject::kDetached;
        ++object->refs_;
        ei::VCall<int>(object, 2);   // shutdown
        --object->refs_;
    }
    CRefObject* held = p->ptr;
    --held->refs_;
    if (held->refs_ == 0 && held) ei::VCall<void>(held, 0, 1);
}

// The inlined string assign of a C string: copied over the current characters, the rest erased (the
// terminator moved down) or appended.
void AssignText(StringRep* to, const char* text, const void* tag) {
    const char* end = text + std::strlen(text);
    std::size_t length = static_cast<std::size_t>(end - text);
    std::size_t size = static_cast<std::size_t>(to->finish - to->start);
    if (length <= size) {
        std::memcpy(to->start, text, length);
        char* first = to->start + length;
        char* last = to->finish;
        if (first != last) {
            GameMemmove(first, last, static_cast<std::size_t>(last - last) + 1);
            to->finish = to->finish - last + first;
        }
    } else {
        std::memcpy(to->start, text, size);
        StringAppendTag(to, nullptr, text + (to->finish - to->start), end, tag);
    }
}

} // namespace

// 0041D1A0: keeps `object` on the timers list for `seconds` (shared into the entry).
void ClientSession::AddTimed(CRefObject* object, std::uint32_t seconds) {
    ClientSession* self = this;
    RefPtr raw;
    RefPtrFromRaw(&raw, object);
    TimedEntry entry;
    entry.seconds = seconds;
    entry.object.Share(raw.ptr);
    void* timers = &self->timers_;
    void* end;
    TimersEnd(timers, nullptr, &end);
    void* inserted;
    TimersInsert(timers, nullptr, &inserted, end, &entry);
    DestroyInline(&entry.object);
}
EI_REPLACE_METHOD(0x0041D1A0, ClientSession::AddTimed);

// 0041D260: sends the server a command (0073BC40: +0xC kind, +0x10 text), kept 60 seconds on the
// timers list.
void ClientSession::SendCommand(int kind, const char* text) {
    ClientSession* self = this;
    auto* command = static_cast<std::uint8_t*>(gamecrt::operator_new(0x1C));
    std::uint32_t zero = 0;
    std::memcpy(command + 4, &zero, 4);
    if (command) {
        SetVtable(command, kMessageVtable);
        std::uint32_t one = 1;
        std::memcpy(command + 8, &one, 4);
        auto* string = reinterpret_cast<StringRep*>(command + 0x10);
        string->start = nullptr;
        string->finish = nullptr;
        char* none = nullptr;
        std::uint8_t allocator = 0;
        StringAllocProxy(&string->eos, nullptr, &allocator, &none);
        StringAllocateBlock(string, nullptr, 8);
        *string->finish = 0;
        SetVtable(command, kCommandVtable);
    }
    std::memcpy(command + 0xC, &kind, 4);
    AssignText(reinterpret_cast<StringRep*>(command + 0x10), text, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(kind)));

    RefPtr raw;
    RefPtrFromRaw(&raw, reinterpret_cast<CRefObject*>(command));
    TimedEntry entry;
    entry.seconds = kCommandLifetime;
    RefPtrCopyConstruct(&entry.object, nullptr, &raw);
    TimersPushBack(&self->timers_, nullptr, &entry);
    entry.object.Destroy();
    raw.ptr = nullptr;
    raw.Destroy();
}
EI_REPLACE_METHOD(0x0041D260, ClientSession::SendCommand);

// 0041D420 (slot 16): makes `*out` an empty server entry (0073BB28: no name or description, kind
// 4, no players).
void ClientSession::CreateEntry(RefPtr* out) {
    auto* entry = static_cast<std::uint8_t*>(gamecrt::operator_new(0x48));
    std::uint32_t zero = 0;
    std::memcpy(entry + 4, &zero, 4);
    if (entry) {
        ServerEntryConstructBase(entry);
        auto* name = reinterpret_cast<StringRep*>(entry + 0x28);
        auto* description = reinterpret_cast<StringRep*>(entry + 0x38);
        std::uint8_t allocator = 0;
        StringReserve(name, nullptr, &allocator, 8);
        *name->finish = 0;
        StringReserve(description, nullptr, &allocator, 8);
        *description->finish = 0;
        SetVtable(entry, kServerEntryVtable);
        const char* empty = Text(kEmptyText);
        StringAssignRange(name, nullptr, empty, empty + std::strlen(empty));
        std::uint32_t four = 4;
        std::memcpy(entry + 0x34, &four, 4);
        const char* end = empty + std::strlen(empty);
        std::size_t length = static_cast<std::size_t>(end - empty);
        std::size_t size = static_cast<std::size_t>(description->finish - description->start);
        if (length <= size) {
            std::memcpy(description->start, empty, length);
            StringErase(description, nullptr, description->start + length, description->finish);
        } else {
            std::memcpy(description->start, empty, size);
            StringAppendTag(description, nullptr, empty + (description->finish - description->start), end, out);
        }
        entry[0x44] = 0;
        entry[0x45] = 0;
        entry[0x46] = 0;
    }
    if (reinterpret_cast<CRefObject*>(entry) == out->ptr) return;
    if (CRefObject* old = out->ptr) {
        if (old->refs_ != 1) {
            old->flags7_ |= CRefObject::kDetached;
            ++old->refs_;
            old->OnShutdown();
            --old->refs_;
        }
        CRefObject* held = out->ptr;
        --held->refs_;
        if (held->refs_ == 0 && held) delete held;
    }
    out->Share(reinterpret_cast<CRefObject*>(entry));
}
namespace {
void __fastcall ClientDescribeHook(ClientSession* self, void*, RefPtr* out) { self->ClientSession::CreateEntry(out); }
} // namespace
EI_REPLACE(0x0041D420, ClientDescribeHook);


namespace {
// 0057F280: GameFlow::BeginMultiplayerGame (world_game.cpp).
__declspec(naked) void __fastcall FlowBeginMultiplayerGame(void* /*flow*/) EI_ORIGINAL(0x0057F280)
} // namespace

// 0041D410 (slot 15): the game flow (007AFE20) begins the multiplayer game.
void ClientSession::OnJoined() { FlowBeginMultiplayerGame(reinterpret_cast<void*>(0x007AFE20)); }
namespace {
void __fastcall ClientBeginGameHook(ClientSession* self) { self->ClientSession::OnJoined(); }
} // namespace
EI_REPLACE(0x0041D410, ClientBeginGameHook);

// 0041D9A0 (called with the client by the transfer screen 0061E4E0): a buffer's stream size (its
// base plus the highest written position, from `begin`) over the dword at its start (1 when 0),
// both as signed integers; 0 for fewer than 4 bytes. Reading the dword moves the read position to
// 4 (Seek(0) through slot 6, then a 4-byte read as BufferRead does it). Returned unrounded: the
// caller multiplies ST0. Plain C++ division (rewrite-guide rule 7).
double __fastcall ClientBufferRatio(ClientSession*, void*, GameBuffer* buffer) {
    constexpr int kSlotSeek = 6;
    auto streamSize = [buffer] {
        if (buffer->cur > buffer->high) buffer->high = buffer->cur;
        return static_cast<std::int32_t>(buffer->base - reinterpret_cast<std::intptr_t>(buffer->begin) +
                                         reinterpret_cast<std::intptr_t>(buffer->high));
    };
    if (streamSize() < 4) {
        float zero;
        std::memcpy(&zero, reinterpret_cast<const void*>(0x0073B858), sizeof(zero));
        return zero;
    }
    ei::VCall<int>(buffer, kSlotSeek, 0);
    std::uint32_t stored;
    BufferRead(buffer, nullptr, &stored, 4);
    std::int32_t divisor = stored != 0 ? static_cast<std::int32_t>(stored) : 1;
    return static_cast<double>(streamSize()) / static_cast<double>(divisor);
}
EI_REPLACE(0x0041D9A0, ClientBufferRatio);

} // namespace net
