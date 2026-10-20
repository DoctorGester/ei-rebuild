// The connection pump of a network session (00479E30) and its start (0047A080): a driver link
// (net_link.h) per remote player. The session keeps two STLport lists of the drivers (+8 owning,
// +0xC plain holders); the lists' members are library code, called at their original addresses.
// See docs/notes/network.md.
#include "net_classes.h"
#include "net_link.h"

#include "core_refcount.h"
#include "core_streams.h"
#include "net_connection.h"
#include "net_messages.h"

#include "lib/stlp_list_refs.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstdint>
#include "originals_library.h"

namespace netlink {

namespace {

bool IsLive(const CRefCounted* object) { return object && !(object->flags7_ & CRefCounted::kDetached); }

// list<RefPtr>::insert of the drivers (0047A450, shared) and the holders (0047A480, counted), the
// drivers' erase (0047A380); the nodes' deallocation (0047A650, 12 bytes back to the pool) and the
// pool's free (00404D70).
using RefList = stlp::list<RefPtr>;
void __fastcall NodeFree(void* a0, std::uint32_t a1) { StlNodeFreeOriginal(a0, (std::size_t)a1); }

std::int32_t Addr(const std::uint8_t* p) { return static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(p)); }

// A new driver for `player` (allocated with 00413250 or operator new, as each caller does), put at
// the end of the owning list.
DriverLink* NewDriver(DriverSession* self, CRefObject* player, void* raw) {
    auto* driver = raw ? static_cast<DriverLink*>(raw)->Construct() : nullptr;
    reinterpret_cast<RefPtr*>(&driver->player_)->Assign(player);
    RefPtr held{driver};   // taken without a reference; the list's copy adds one
    RefList::Insert<stlp::SharedRef>(stlp::AsRefNode(self->drivers_), held);
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
        net::BufferUpdateHigh(in);
        if (in->base - Addr(in->begin) + Addr(in->high) <= 0) continue;
        net::ServerSession* server = net::GameServer();
        if (!IsLive(server)) continue;
        if (!server->PlayerStateIs3(node->connection)) continue;
        player = node->connection;
        DriverLink* driver = NewDriver(this, player, net::NewRefObject(sizeof(DriverLink)));
        // The original then destroys the emptied temporary holder (00413270 on null: nothing).
        RefPtr held{driver};
        RefList::Insert<stlp::CountedRef>(stlp::AsRefNode(holders_), held);
    }
    for (DriverNode* it = drivers_->next; it != drivers_;) {
        it->Driver()->Pump();
        if (IsLive(it->Driver()->player_)) {
            it = it->next;
        } else {
            it = reinterpret_cast<DriverNode*>(RefList::Erase<stlp::SharedRef>(stlp::AsRefNode(it)));
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
        stlp::deallocate(node, 12);   // 0047A650
        node = next;
    }
    drivers_->next = drivers_;
    drivers_->prev = drivers_;
    for (DriverNode* node = holders_->next; node != holders_;) {
        DriverNode* next = node->next;
        node->value.DeleteReleased(0);
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
        ei::SetVtable(&stream, CMemoryStream::kQueueVtableAddress);
        driver->Receive(&stream);
        ei::SetVtable(&stream, kMemoryStreamVtable);
        if (void* data = stream.data_) gamecrt::operator_delete(data);
        ei::SetVtable(&stream, kStreamVtable);
    }
    return driver;
}
EI_REPLACE_METHOD(0x0047A080, DriverSession::Start);

} // namespace netlink
