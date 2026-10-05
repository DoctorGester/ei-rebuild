// The game client's timed entries and commands: an object kept on the timers list (0041D1A0), a
// command message for the server (0041D260), the client's own server entry (0041D420, slot 16), the
// start of the game (0041D410, slot 15) and the ratio of a received buffer (0041D9A0).
// See net_classes.h and docs/notes/unmapped_410_470.md ("Seventh round").
#include "net_messages.h"
#include "core_refcount.h"
#include "net_coder.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_library.h"

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
void __fastcall StringAllocProxy(char** a0, void*, const void* a2, char* const* a3) { StringEndOfStorageOriginal((void*)a0, nullptr, a2, (const void*)a3); }
__declspec(naked) void __fastcall StringAllocateBlock(StringRep* /*s*/, void*, std::size_t /*n*/) EI_ORIGINAL(0x00424E40)
inline void __fastcall StringReserve(StringRep* a0, void*, const void* a2, std::size_t a3) { StringReserveOriginal((void*)a0, nullptr, a2, a3); }
void __fastcall StringAssignRange(StringRep* a0, void*, const char* a2, const char* a3) { StringAssignRangeOriginal((void*)a0, nullptr, a2, a3); }
void __fastcall StringErase(StringRep* a0, void*, char* a2, char* a3) { StringEraseOriginal((void*)a0, nullptr, a2, a3); }
__declspec(naked) void __fastcall StringAppendTag(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/, const void* /*tag*/) EI_ORIGINAL(0x0040C280)
__declspec(naked) void __fastcall ServerEntryConstructBase(ServerEntry* /*entry*/) EI_ORIGINAL(0x004413E0)

constexpr std::uintptr_t kEmptyText = 0x00783D6C;
constexpr std::uint32_t kCommandLifetime = 0x42700000;     // 60.0f

const char* Text(std::uintptr_t a) { return reinterpret_cast<const char*>(a); }

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
        object->OnShutdown();
        --object->refs_;
    }
    CRefObject* held = p->ptr;
    --held->refs_;
    if (held->refs_ == 0 && held) delete held;
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

// 0041D260: sends the server a command message, kept 60 seconds on the timers list.
void ClientSession::SendCommand(int kind, const char* text) {
    ClientSession* self = this;
    auto* command = NewMessageMemory<CommandMessage>();
    if (command) {
        ei::SetVtable(command, kMessageVtable);
        command->version_ = 1;
        StringRep* string = &command->text_;
        string->start = nullptr;
        string->finish = nullptr;
        char* none = nullptr;
        std::uint8_t allocator = 0;
        StringAllocProxy(&string->eos, nullptr, &allocator, &none);
        StringAllocateBlock(string, nullptr, 8);
        *string->finish = 0;
        ei::SetVtable(command, CommandMessage::kVtableAddress);
    }
    command->kind_ = static_cast<std::uint32_t>(kind);
    AssignText(&command->text_, text, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(kind)));

    RefPtr raw;
    RefPtrFromRaw(&raw, command);
    TimedEntry entry;
    entry.seconds = kCommandLifetime;
    RefPtrCopyConstruct(&entry.object, nullptr, &raw);
    TimersPushBack(&self->timers_, nullptr, &entry);
    entry.object.Destroy();
    raw.ptr = nullptr;
    raw.Destroy();
}
EI_REPLACE_METHOD(0x0041D260, ClientSession::SendCommand);

// 0041D420 (slot 16): makes `*out` an empty server entry (no name or description, kind 4, no
// players).
void ClientSession::CreateEntry(RefPtr* out) {
    auto* entry = NewMessageMemory<ServerEntry>();
    if (entry) {
        ServerEntryConstructBase(entry);
        StringRep* name = &entry->name_;
        StringRep* description = &entry->description_;
        std::uint8_t allocator = 0;
        StringReserve(name, nullptr, &allocator, 8);
        *name->finish = 0;
        StringReserve(description, nullptr, &allocator, 8);
        *description->finish = 0;
        ei::SetVtable(entry, ServerEntry::kVtableAddress);
        const char* empty = Text(kEmptyText);
        StringAssignRange(name, nullptr, empty, empty + std::strlen(empty));
        entry->kind_ = 4;
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
        entry->players_ = 0;
        entry->maxPlayers_ = 0;
        entry->password_ = 0;
    }
    if (entry == out->ptr) return;
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
    out->Share(entry);
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
    buffer->Seek(0);
    std::uint32_t stored;
    BufferRead(buffer, nullptr, &stored, 4);
    std::int32_t divisor = stored != 0 ? static_cast<std::int32_t>(stored) : 1;
    return static_cast<double>(streamSize()) / static_cast<double>(divisor);
}
EI_REPLACE(0x0041D9A0, ClientBufferRatio);

} // namespace net
