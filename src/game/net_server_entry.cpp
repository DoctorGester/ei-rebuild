// The game server's description of itself (0041A7E0, slot 15): a server entry like those the
// client reads from the server list (net_server_list.cpp). See net_classes.h and
// docs/notes/unmapped_410_470.md ("Seventh round").
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace net {

namespace {

__declspec(naked) void __fastcall StringConstruct(StringRep* /*s*/) EI_ORIGINAL(0x0041F460)
__declspec(naked) void __fastcall StringAssignText(StringRep* /*s*/, void*, const char* /*text*/) EI_ORIGINAL(0x0041F550)
// STLport strings (library): reserve(n) with an allocator, assign(first, last, tag),
// assign(first, last), erase(first, last), append(first, last, tag).
__declspec(naked) void __fastcall StringReserve(StringRep* /*s*/, void*, const void* /*alloc*/, std::size_t /*n*/) EI_ORIGINAL(0x00421800)
__declspec(naked) void __fastcall StringAssign(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/, const void* /*tag*/) EI_ORIGINAL(0x0042A4F0)
__declspec(naked) void __fastcall StringAssignRange(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/) EI_ORIGINAL(0x00426A80)
__declspec(naked) void __fastcall StringErase(StringRep* /*s*/, void*, char* /*first*/, char* /*last*/) EI_ORIGINAL(0x0040B3C0)
__declspec(naked) void __fastcall StringAppendTag(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/, const void* /*tag*/) EI_ORIGINAL(0x0040C280)
// The server entry's base constructor (client base, outside this range).
__declspec(naked) void __fastcall ServerEntryConstructBase(void* /*entry*/) EI_ORIGINAL(0x004413E0)
__declspec(naked) void* __fastcall WorldMapTitle(void* /*world*/, void*, void* /*out CString*/) EI_ORIGINAL(0x005814C0)
__declspec(naked) void __fastcall CStringDestroy(void* /*s*/) EI_ORIGINAL(0x00700970)
// A sockaddr_in from a host name and a port (the socket driver, 00440000).
__declspec(naked) void __fastcall MasterAddressInit(void* /*address*/, void*, const char* /*host*/, int /*port*/) EI_ORIGINAL(0x00440440)

constexpr std::uintptr_t kServerEntryVtable = 0x0073BB28;
constexpr std::uintptr_t kEmptyText = 0x00783D6C;
constexpr std::uintptr_t kEmptyText2 = 0x0079B298;
constexpr std::uintptr_t kWorld = 0x007AFE20;

const char* Text(std::uintptr_t a) { return reinterpret_cast<const char*>(a); }

} // namespace

// 0041A7E0 (slot 15): makes `*out` a new server entry (vtable 0073BB28, 0x48 bytes): the name
// (+0x25C8), the kind (+0x25E4), the current map's title as the description when a map message is
// held (else empty), the number of clients, the maximum (+0x25E0) and whether +0x25E8 is set.
void ServerSession::Describe(RefPtr* out) {
    auto* entry = static_cast<std::uint8_t*>(gamecrt::operator_new(0x48));
    std::uint32_t zero = 0;
    std::memcpy(entry + 4, &zero, 4);
    if (entry) {
        ServerEntryConstructBase(entry);
        auto* name = reinterpret_cast<StringRep*>(entry + 0x28);
        std::uint8_t allocator = 0;
        StringReserve(name, nullptr, &allocator, 8);
        *name->finish = 0;
        StringConstruct(reinterpret_cast<StringRep*>(entry + 0x38));
        std::uintptr_t vtable = kServerEntryVtable;
        std::memcpy(entry, &vtable, 4);
        StringAssignText(name, nullptr, Text(kEmptyText));
        std::uint32_t four = 4;
        std::memcpy(entry + 0x34, &four, 4);
        StringAssignText(reinterpret_cast<StringRep*>(entry + 0x38), nullptr, Text(kEmptyText));
        entry[0x44] = 0;
        entry[0x45] = 0;
        entry[0x46] = 0;
    }
    auto* name = reinterpret_cast<StringRep*>(entry + 0x28);
    if (&string_25C8 != name) StringAssign(name, nullptr, string_25C8.start, string_25C8.finish, out);
    std::memcpy(entry + 0x34, &unknown_25E4, 4);
    auto* description = reinterpret_cast<StringRep*>(entry + 0x38);
    const char* empty = Text(kEmptyText2);
    StringAssignRange(description, nullptr, empty, empty + std::strlen(empty));

    auto* map = reinterpret_cast<CRefObject*>(unknown_25A8);
    if (map && !(map->flags7_ & CRefObject::kDetached)) {
        std::uint32_t title = 0;   // CString
        const char* text = *static_cast<const char**>(WorldMapTitle(reinterpret_cast<void*>(kWorld), nullptr, &title));
        const char* end = text + std::strlen(text);
        std::size_t length = static_cast<std::size_t>(end - text);
        std::size_t size = static_cast<std::size_t>(description->finish - description->start);
        if (length <= size) {
            std::memcpy(description->start, text, length);
            StringErase(description, nullptr, description->start + length, description->finish);
        } else {
            std::memcpy(description->start, text, size);
            StringAppendTag(description, nullptr, text + (description->finish - description->start), end, out);
        }
        CStringDestroy(&title);
    }

    std::uint8_t clients = 0;
    void** sentinel = static_cast<void**>(base.clients);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) ++clients;
    entry[0x44] = clients;
    entry[0x45] = static_cast<std::uint8_t>(unknown_25E0);
    entry[0x46] = static_cast<std::uint8_t>(unknown_25E8[0]) != 0;

    if (reinterpret_cast<CRefObject*>(entry) != out->ptr) {
        if (CRefObject* old = out->ptr) {
            if (old->refs_ != 1) {
                old->flags7_ |= CRefObject::kDetached;
                old->Shutdown();
            }
            out->ptr->Release();
        }
        out->Share(reinterpret_cast<CRefObject*>(entry));
    }
}
// 00417E10: whether the server is announced to the master server (+0x2574); when it is, the
// master's address (+0x257C) is set from the host name at 0079BED8 and port 28004.
void ServerSession::SetPublic(bool announce) {
    unknown_2574 = announce;
    if (announce) MasterAddressInit(unknown_257C, nullptr, *reinterpret_cast<const char* const*>(0x0079BED8), 0x6D64);
}

namespace {
void __fastcall SetPublicHook(ServerSession* self, void*, bool announce) { self->SetPublic(announce); }
void __fastcall DescribeHook(ServerSession* self, void*, RefPtr* out) { self->Describe(out); }
} // namespace
EI_REPLACE(0x0041A7E0, DescribeHook);
EI_REPLACE(0x00417E10, SetPublicHook);

} // namespace net
