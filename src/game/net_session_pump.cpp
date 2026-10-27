// The connection pump of a network session and its start: a driver link
// (net_link.h) per remote player. The session keeps two lists of the drivers (owning, and holders_).
// See docs/notes/network.md.
#include "net_classes.h"
#include "net_link.h"

#include "core_refcount.h"
#include "core_streams.h"
#include "net_connection.h"
#include "net_messages.h"

#include "lib/stlp_list_refs.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>

namespace netlink {

namespace {

using RefList = stlp::list<RefPtr>;
void __fastcall NodeFree(void* a0, std::uint32_t a1) { libcrt::NodeDeallocate(a0, (std::size_t)a1); }

std::int32_t Addr(const std::uint8_t* p) { return static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(p)); }

// A new driver for `player`, put at the end of the owning list.
DriverLink* NewDriver(DriverSession* self, CRefObject* player) {
    auto* driver = net::NewCounted<DriverLink>();
    driver->player_.Assign(player);
    RefPtr held{driver};   // taken without a reference; the list's copy adds one
    RefList::Insert<stlp::SharedRef>(stlp::AsRefNode(self->drivers_), held);
    return driver;
}

} // namespace

// A driver for every connection of `session` this one has none for, when the connection has something queued
// and the game server knows it; then each driver pumped, and those whose player is gone or detached erased.
void DriverSession::PumpDrivers(net::SessionBase* session) {
    if (!IsLive(session)) return;
    for (net::ClientNode* node = session->clients_->next; node != session->clients_; node = node->next) {
        CRefObject* player = node->connection;
        bool known = false;
        for (DriverNode* it = drivers_->next, *end = drivers_; it != end; it = it->next) {
            if (it->Driver()->player_.ptr == player) {
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
        DriverLink* driver = NewDriver(this, player);
        // The original then destroys the emptied temporary holder.
        RefPtr held{driver};
        RefList::Insert<stlp::CountedRef>(stlp::AsRefNode(holders_), held);
    }
    for (DriverNode* it = drivers_->next; it != drivers_;) {
        it->Driver()->Pump();
        if (IsLive(it->Driver()->player_.ptr)) {
            it = it->next;
        } else {
            it = reinterpret_cast<DriverNode*>(RefList::Erase<stlp::SharedRef>(stlp::AsRefNode(it)));
        }
    }
}
EI_REWRITTEN(0x00479E30, DriverSession::PumpDrivers);

// Both lists emptied, then one driver for `player`: started, and its pending output
// drained into throwaway memory streams while slot 6 answers true. Returns the driver.
DriverLink* DriverSession::Start(net::Connection* player) {
    for (DriverNode* node = drivers_->next; node != drivers_;) {
        DriverNode* next = node->next;
        node->value.Destroy();
        stlp::deallocate(node, 12);
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
    DriverLink* driver = NewDriver(this, player);
    driver->Pump();
    while (driver->HasIncoming()) {
        CMemoryStream stream;
        driver->Receive(&stream);
    }
    return driver;
}
EI_REWRITTEN(0x0047A080, DriverSession::Start);

} // namespace netlink
