// The connection pump of a network session (00479E30) and its start (0047A080): a driver link
// (net_link.h) per remote player. The session keeps two STLport lists of the drivers (+8 owning,
// +0xC plain holders); the lists' members are library code, called at their original addresses.
// See docs/notes/network.md.
#include "net_link.h"

#include "core_refcount.h"
#include "core_streams.h"
#include "net_connection.h"
#include "net_messages.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstdint>
#include "originals_library.h"

namespace netlink {

namespace {

bool IsLive(const CRefCounted* object) { return object && !(object->flags7_ & CRefCounted::kDetached); }

// list::insert (0047A450 / 0047A480: RET 0xC) and list::erase (0047A380, RET 8); the nodes'
// deallocation (0047A650, 12 bytes back to the pool) and the pool's free (00404D70).
__declspec(naked) DriverNode** __fastcall DriversInsert(DriverNode** /*list*/, void*, DriverNode** /*out*/, DriverNode* /*pos*/, const RefPtr*) EI_ORIGINAL(0x0047A450)
__declspec(naked) DriverNode** __fastcall HoldersInsert(DriverNode** /*list*/, void*, DriverNode** /*out*/, DriverNode* /*pos*/, const RefPtr*) EI_ORIGINAL(0x0047A480)
__declspec(naked) DriverNode** __fastcall DriversErase(DriverNode** /*list*/, void*, DriverNode** /*out*/, DriverNode* /*pos*/) EI_ORIGINAL(0x0047A380)
__declspec(naked) void __fastcall NodeDeallocate(DriverNode*, int /*count*/) EI_ORIGINAL(0x0047A650)
void __fastcall NodeFree(void* a0, std::uint32_t a1) { StlNodeFreeOriginal(a0, (std::size_t)a1); }
// The holder's deleting destructor (0047A6D0, refptr_copies.cpp), called with 0 as the original does.
__declspec(naked) void* __fastcall HolderDelete(RefPtr*, void*, unsigned) EI_ORIGINAL(0x0047A6D0)
// 00419CC0: raises a reliable stream's high-water mark.
__declspec(naked) void __fastcall BufferUpdateHigh(GameBuffer*) EI_ORIGINAL(0x00419CC0)
// 00417E40 (thiscall on the game server, RET 4): whether the connection is one of its players.
__declspec(naked) bool __fastcall ServerHasConnection(net::ServerSession*, void*, net::Connection*) EI_ORIGINAL(0x00417E40)
// 00413250 (__fastcall size): operator new with the reference count cleared.
__declspec(naked) void* __fastcall AllocRefObject(std::size_t) EI_ORIGINAL(0x00413250)

std::int32_t Addr(const std::uint8_t* p) { return static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(p)); }

// A new driver for `player` (allocated with 00413250 or operator new, as each caller does), put at
// the end of the owning list.
DriverLink* NewDriver(DriverSession* self, CRefObject* player, void* raw) {
    auto* driver = raw ? static_cast<DriverLink*>(raw)->Construct() : nullptr;
    reinterpret_cast<RefPtr*>(&driver->player_)->Assign(player);
    RefPtr held{driver};   // taken without a reference; the list's copy adds one
    DriverNode* out;
    DriversInsert(&self->drivers_, nullptr, &out, self->drivers_, &held);
    return driver;
}

} // namespace

// A driver for every connection of `session` this one has none for, when the connection has
// something queued and the game server (0079B5DC) knows it; then each driver pumped (slot 10), and
// those whose player is gone or detached erased.
void DriverSession::PumpDrivers(net::SessionBase* session) {   // 00479E30
    if (!IsLive(session)) return;
    for (net::ClientNode* node = session->clients_->next; node != session->clients_; node = node->next) {
        CRefObject* player = node->connection;
        bool known = false;
        for (DriverNode* it = drivers_->next, *end = drivers_; it != end; it = it->next) {
            if (it->Driver()->player_ == player) {
                known = true;
                break;
            }
        }
        if (known) continue;
        GameBuffer* in = &node->connection->reliableIn_;
        BufferUpdateHigh(in);
        if (in->base - Addr(in->begin) + Addr(in->high) <= 0) continue;
        net::ServerSession* server = net::GameServer();
        if (!IsLive(server)) continue;
        if (!ServerHasConnection(server, nullptr, node->connection)) continue;
        player = node->connection;
        DriverLink* driver = NewDriver(this, player, AllocRefObject(sizeof(DriverLink)));
        // The original then destroys the emptied temporary holder (00413270 on null: nothing).
        RefPtr held{driver};
        DriverNode* out;
        HoldersInsert(&holders_, nullptr, &out, holders_, &held);
    }
    for (DriverNode* it = drivers_->next; it != drivers_;) {
        it->Driver()->Pump();
        if (IsLive(it->Driver()->player_)) {
            it = it->next;
        } else {
            DriverNode* out;
            it = *DriversErase(&drivers_, nullptr, &out, it);
        }
    }
}
EI_REPLACE_METHOD(0x00479E30, DriverSession::PumpDrivers);

// Both lists emptied, then one driver for `player`: started (slot 10), and its pending output
// drained into throwaway memory streams (slot 9) while slot 6 answers true. Returns the driver.
DriverLink* DriverSession::Start(net::Connection* player) {   // 0047A080
    for (DriverNode* node = drivers_->next; node != drivers_;) {
        DriverNode* next = node->next;
        node->value.Destroy();
        NodeDeallocate(node, 1);
        node = next;
    }
    drivers_->next = drivers_;
    drivers_->prev = drivers_;
    for (DriverNode* node = holders_->next; node != holders_;) {
        DriverNode* next = node->next;
        HolderDelete(&node->value, nullptr, 0);
        if (node) NodeFree(node, 0xC);
        node = next;
    }
    holders_->next = holders_;
    holders_->prev = holders_;
    void* raw = net::NewMessageMemory<DriverLink>();
    DriverLink* driver = NewDriver(this, player, raw);
    driver->Pump();
    while (driver->HasIncoming()) {
        CMemoryStream stream;
        stream.ConstructBase();
        ei::SetVtable(&stream, kMemoryStreamVtable);
        stream.data_ = nullptr;
        stream.Reset();
        ei::SetVtable(&stream, kNodeStreamVtable);
        driver->Receive(&stream);
        ei::SetVtable(&stream, kMemoryStreamVtable);
        if (void* data = stream.data_) gamecrt::operator_delete(data);
        ei::SetVtable(&stream, kStreamVtable);
    }
    return driver;
}
EI_REPLACE_METHOD(0x0047A080, DriverSession::Start);

} // namespace netlink
