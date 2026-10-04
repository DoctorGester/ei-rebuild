// The game server's players and their connections: lookups (0041A020, 0041A170, 0041A270,
// 0041A2E0), admission and the password (0041A050 slot 16, 0041A140), kicking and banning
// (0041A550, 0041A620), the departure reports (0041A700 slot 11, 0041A770 slot 12) and the lines to
// every guest (0041A500). See net_classes.h and docs/notes/unmapped_410_470.md ("Seventh round").
//
// Players (+0x2590): +0x0C name, +0x28 host (byte), +0x2C id, +0x30 serial. Bindings (+0x25A4):
// +0x0C the player, +0x10 the connection (net_server_join.cpp).
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstring>

namespace net {

void** __fastcall SessionFindClient(SessionBase* self, void*, void** out, const void* address);   // net_session_tick.cpp
void __fastcall SessionSendLeave(SessionBase* self, void*, const void* address, std::uint32_t id);  // net_session_tick.cpp

namespace {

__declspec(naked) char* __cdecl GameStrncpy(char* /*to*/, const char* /*from*/, std::size_t /*n*/) EI_ORIGINAL(0x006E7E80)
__declspec(naked) int __cdecl GameStrnicmp(const char* /*a*/, const char* /*b*/, std::size_t /*n*/) EI_ORIGINAL(0x006FA1A0)
// The banned addresses (+0x25C4, STLport list<sockaddr_in>; library): end, begin, find, insert.
__declspec(naked) void** __fastcall BannedEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x0041FCD0)
__declspec(naked) void** __fastcall BannedBegin(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x0041FCC0)
__declspec(naked) void** __fastcall BannedFind(void** /*out*/, void*, void* /*first*/, void* /*last*/, std::uint32_t /*value*/) EI_ORIGINAL(0x00427FF0)
__declspec(naked) void** __fastcall BannedInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const void* /*value*/) EI_ORIGINAL(0x00421AF0)

constexpr int kSlotRemoveClient = 2;
constexpr std::size_t kPasswordSize = 0x14;

template <typename T>
T& At(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}
bool Live(const void* object) {
    return object && !(static_cast<const CRefObject*>(object)->flags7_ & CRefObject::kDetached);
}
void** First(void* list) { return static_cast<void**>(static_cast<void**>(list)[0]); }
void* Value(void** node) { return node[2]; }

// The connection of the first binding whose player is live and the host, or null.
void* HostConnection(void** bindings) {
    for (void** node = First(bindings); node != bindings; node = static_cast<void**>(node[0])) {
        void* player = At<void*>(Value(node), 0xC);
        if (Live(player) && At<std::uint8_t>(player, 0x28)) return At<void*>(Value(node), 0x10);
    }
    return nullptr;
}

// The common tail of kicking and banning: the client is told to leave and removed (slot 2).
void DropClient(ServerSession* self, void* connection) {
    auto* address = static_cast<std::uint8_t*>(connection) + 0x2C;
    SessionSendLeave(&self->base, nullptr, address, At<std::uint32_t>(connection, 0x20));
    void* node;
    SessionFindClient(&self->base, nullptr, &node, address);
    ei::VCall<void>(self, kSlotRemoveClient, node);
}

} // namespace

// 0041A020: the id of the first host player, -1 without one.
int ServerSession::LeaderId() {
    void** players = static_cast<void**>(list_2590);
    for (void** node = First(players); node != players; node = static_cast<void**>(node[0]))
        if (At<std::uint8_t>(Value(node), 0x28)) return At<int>(Value(node), 0x2C);
    return -1;
}

// 0041A050 (slot 16): whether a client from `address` may join. Refused when the address is banned
// (reason 2), when a password is set and the 20 bytes at `*password` differ (reason 3; the pointer
// moves past them either way), or when the clients and the pending ones (+0x2568) reach the
// maximum (reason 1).
bool ServerSession::Admit(std::uint32_t address, const std::uint8_t** password, std::uint8_t* reason) {
    void* banned = &list_25C4;
    *reason = 2;
    void* sentinel = list_25C4;
    void* last;
    BannedEnd(banned, nullptr, &last);
    void* first;
    BannedBegin(banned, nullptr, &first);
    void* found;
    if (*BannedFind(&found, nullptr, first, last, address) != sentinel) return false;

    auto* stored = reinterpret_cast<std::uint8_t*>(unknown_25E8);
    if (stored[0]) {
        *reason = 3;
        std::uint8_t given[kPasswordSize] = {};
        std::memcpy(given, *password, kPasswordSize);
        *password += kPasswordSize;
        if (std::memcmp(given, stored, kPasswordSize) != 0) return false;
    }
    *reason = 1;
    std::uint32_t clients = 0;
    void** list = static_cast<void**>(base.clients);
    for (void** node = First(list); node != list; node = static_cast<void**>(node[0])) ++clients;
    std::uint32_t pending = 0;
    list = static_cast<void**>(list_2568);
    for (void** node = First(list); node != list; node = static_cast<void**>(node[0])) ++pending;
    return pending + clients < unknown_25E0;
}

// 0041A140: the join password (at most 20 bytes, zero-filled).
void ServerSession::SetPassword(const char* text) {
    char* stored = reinterpret_cast<char*>(unknown_25E8);
    std::memset(stored, 0, kPasswordSize);
    GameStrncpy(stored, text, kPasswordSize);
}

// 0041A170: the connection of the first player whose name starts with `name` (case-insensitive)
// and whose connection is not also the host's; null without one.
void* ServerSession::FindGuestConnection(const StringRep* name) {
    const std::size_t length = static_cast<std::size_t>(name->finish - name->start);
    for (void** node = First(list_2590); node != list_2590; node = static_cast<void**>(node[0])) {
        void* player = Value(node);
        if (GameStrnicmp(At<const char*>(player, 0xC), name->start, length) != 0) continue;
        void** bindings = static_cast<void**>(list_25A4);
        void** first = First(bindings);
        for (void** binding = first; binding != bindings; binding = static_cast<void**>(binding[0])) {
            if (At<void*>(Value(binding), 0xC) != Value(node)) continue;
            void* connection = At<void*>(Value(binding), 0x10);
            bool hostsToo = false;
            for (void** other = first; other != bindings; other = static_cast<void**>(other[0])) {
                if (At<void*>(Value(other), 0x10) != connection) continue;
                void* owner = At<void*>(Value(other), 0xC);
                if (owner && !(static_cast<CRefObject*>(owner)->flags7_ & CRefObject::kDetached) && At<std::uint8_t>(owner, 0x28)) {
                    hostsToo = true;
                    break;
                }
            }
            if (!hostsToo) return At<void*>(Value(binding), 0x10);
            break;
        }
    }
    return nullptr;
}

// 0041A270: the connection of the player with serial `serial`, or null.
void* ServerSession::FindConnectionBySerial(int serial) {
    void** bindings = static_cast<void**>(list_25A4);
    for (void** node = First(bindings); node != bindings; node = static_cast<void**>(node[0]))
        if (At<int>(At<void*>(Value(node), 0xC), 0x30) == serial) return At<void*>(Value(node), 0x10);
    return nullptr;
}

// 0041A2E0: the player with id `id`, or null.
void* ServerSession::FindPlayer(int id) {
    void** players = static_cast<void**>(list_2590);
    for (void** node = First(players); node != players; node = static_cast<void**>(node[0]))
        if (At<int>(Value(node), 0x2C) == id) return Value(node);
    return nullptr;
}

// 0041A500: a line (text `kind` with `text`) for every player that is not the host.
void ServerSession::AnnounceToGuests(int kind, const char* text) {
    for (void** node = First(list_2590); node != list_2590; node = static_cast<void**>(node[0])) {
        void* player = Value(node);
        if (!At<std::uint8_t>(player, 0x28)) AnnounceText(kind, text, static_cast<CRefObject*>(player));
    }
}

// 0041A550: kicks the client on `connection` (unless it is the host's): its player is announced
// (text 2), then the client is told to leave and removed.
void ServerSession::Kick(void* connection) {
    if (!Live(connection)) return;
    void** bindings = static_cast<void**>(list_25A4);
    void* host = HostConnection(bindings);
    if (connection == host) return;
    for (void** node = First(bindings); node != bindings; node = static_cast<void**>(node[0])) {
        if (At<void*>(Value(node), 0x10) != connection) continue;
        void* binding = Value(node);
        if (binding) {
            void* player = At<void*>(binding, 0xC);
            if (Live(player)) AnnounceText(2, At<const char*>(player, 0xC), nullptr);
        }
        break;
    }
    DropClient(this, connection);
}

// 0041A620: bans the client on `connection` (unless it is the host's): its player is announced
// (text 3), its address added to the banned list, then the client is told to leave and removed.
void ServerSession::Ban(void* connection) {
    if (!Live(connection)) return;
    void** bindings = static_cast<void**>(list_25A4);
    void* host = HostConnection(bindings);
    if (connection == host) return;
    for (void** node = First(bindings); node != bindings; node = static_cast<void**>(node[0])) {
        if (At<void*>(Value(node), 0x10) != connection) continue;
        void* binding = Value(node);
        if (binding) {
            void* player = At<void*>(binding, 0xC);
            if (Live(player)) AnnounceText(3, At<const char*>(player, 0xC), nullptr);
        }
        break;
    }
    auto* address = static_cast<std::uint8_t*>(connection) + 0x2C;
    void* end;
    BannedEnd(&list_25C4, nullptr, &end);
    void* inserted;
    BannedInsert(&list_25C4, nullptr, &inserted, end, address);
    DropClient(this, connection);
}

// 0041A700 (slot 11, a client lost) and 0041A770 (slot 12, a client left): the client's player is
// announced (text 4 / text 1).
void ServerSession::ReportDeparture(void* connection, int text) {
    if (!Live(connection)) return;
    void** bindings = static_cast<void**>(list_25A4);
    for (void** node = First(bindings); node != bindings; node = static_cast<void**>(node[0])) {
        if (At<void*>(Value(node), 0x10) != connection) continue;
        void* binding = Value(node);
        if (!Live(binding)) return;
        void* player = At<void*>(binding, 0xC);
        if (!Live(player)) return;
        AnnounceText(text, At<const char*>(player, 0xC), nullptr);
        return;
    }
}

namespace {
int __fastcall LeaderIdHook(ServerSession* self) { return self->LeaderId(); }
bool __fastcall AdmitHook(ServerSession* self, void*, std::uint32_t address, const std::uint8_t** password, std::uint8_t* reason) {
    return self->Admit(address, password, reason);
}
void __fastcall SetPasswordHook(ServerSession* self, void*, const char* text) { self->SetPassword(text); }
void* __fastcall FindGuestConnectionHook(ServerSession* self, void*, const StringRep* name) { return self->FindGuestConnection(name); }
void* __fastcall FindConnectionBySerialHook(ServerSession* self, void*, int serial) { return self->FindConnectionBySerial(serial); }
void* __fastcall FindPlayerHook(ServerSession* self, void*, int id) { return self->FindPlayer(id); }
void __fastcall AnnounceToGuestsHook(ServerSession* self, void*, int kind, const char* text) { self->AnnounceToGuests(kind, text); }
void __fastcall KickHook(ServerSession* self, void*, void* connection) { self->Kick(connection); }
void __fastcall BanHook(ServerSession* self, void*, void* connection) { self->Ban(connection); }
void __fastcall ClientLostHook(ServerSession* self, void*, void* connection) { self->ReportDeparture(connection, 4); }
void __fastcall ClientLeftHook(ServerSession* self, void*, void* connection) { self->ReportDeparture(connection, 1); }
} // namespace
EI_REPLACE(0x0041A020, LeaderIdHook);
EI_REPLACE(0x0041A050, AdmitHook);
EI_REPLACE(0x0041A140, SetPasswordHook);
EI_REPLACE(0x0041A170, FindGuestConnectionHook);
EI_REPLACE(0x0041A270, FindConnectionBySerialHook);
EI_REPLACE(0x0041A2E0, FindPlayerHook);
EI_REPLACE(0x0041A500, AnnounceToGuestsHook);
EI_REPLACE(0x0041A550, KickHook);
EI_REPLACE(0x0041A620, BanHook);
EI_REPLACE(0x0041A700, ClientLostHook);
EI_REPLACE(0x0041A770, ClientLeftHook);

} // namespace net
