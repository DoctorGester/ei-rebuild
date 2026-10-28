// The connection pump of a network session and its start: a driver link (net_link.h) per remote player. The
// session keeps two lists of the drivers (owning, and holders_). See docs/notes/network.md.
#include "net_classes.h"
#include "net_link.h"

#include "core_refcount.h"
#include "core_streams.h"
#include "net_connection.h"
#include "net_messages.h"

#include <cstdint>

namespace netlink {

namespace {

// A new driver for `player`, put at the end of the owning list (shared there).
DriverLink* NewDriver(DriverSession* self, CRefObject* player) {
    auto* driver = net::NewCounted<DriverLink>();
    driver->player_.Assign(player);
    self->drivers_.emplace_back(driver);
    return driver;
}

} // namespace

// A driver for every connection of `session` this one has none for, when the connection has something queued
// and the game server knows it; then each driver pumped, and those whose player is gone or detached erased.
void DriverSession::PumpDrivers(net::SessionBase* session) {
    if (!IsLive(session)) return;
    for (auto node = session->clients_.begin(); node != session->clients_.end(); ++node) {
        CRefObject* player = node->ptr;
        bool known = false;
        for (auto it = drivers_.begin(); it != drivers_.end(); ++it) {
            if (it->Get()->player_.ptr == player) {
                known = true;
                break;
            }
        }
        if (known) continue;
        GameBuffer* in = &node->Get()->reliableIn_;
        net::BufferUpdateHigh(in);
        if (in->PositionOf(in->high) <= 0) continue;
        net::ServerSession* server = net::g_gameServer;
        if (!IsLive(server)) continue;
        if (!server->PlayerStateIs3(node->Get())) continue;
        player = node->Get();
        DriverLink* driver = NewDriver(this, player);
        holders_.emplace_back(driver);
    }
    for (auto it = drivers_.begin(); it != drivers_.end();) {
        it->Get()->Pump();
        if (IsLive(it->Get()->player_.ptr)) {
            ++it;
        } else {
            it = drivers_.erase(it);
        }
    }
}

// Both lists emptied, then one driver for `player`: started, and its pending output
// drained into throwaway memory streams while its virtual check answers true. Returns the driver.
DriverLink* DriverSession::Start(net::Connection* player) {
    drivers_.clear();
    holders_.clear();
    DriverLink* driver = NewDriver(this, player);
    driver->Pump();
    while (driver->HasIncoming()) {
        CMemoryStream stream;
        driver->Receive(&stream);
    }
    return driver;
}

} // namespace netlink
