// The connection pump of a network session and its start: a driver link (net_link.h) per remote player. The
// session keeps two lists of the drivers (owning, and holders). See docs/notes/network.md.
#include "core_refcount.h"
#include "core_streams.h"
#include "net_classes.h"
#include "net_connection.h"
#include "net_link.h"
#include "net_messages.h"

#include <cstdint>

namespace netlink {

namespace {

// A new driver for `player`, put at the end of the owning list (shared there).
DriverLink* NewDriver(DriverSession* self, net::Connection* player) {
    auto* driver = net::NewCounted<DriverLink>();
    driver->player.Assign(player);
    self->drivers.emplace_back(driver);
    return driver;
}

} // namespace

// A driver for every connection of `session` this one has none for, when the connection has something queued
// and the game server knows it; then each driver pumped, and those whose player is gone or detached erased.
void DriverSession::PumpDrivers(net::SessionBase* session) {
    if (!IsLive(session)) return;
    for (auto& node : session->clients) {
        net::Connection* player = node.Get();
        bool known = false;
        for (auto& it : drivers) {
            if (it.Get()->player.Get() == player) {
                known = true;
                break;
            }
        }
        if (known) continue;
        GameBuffer* in = &node.Get()->reliableIn;
        net::BufferUpdateHigh(in);
        if (in->PositionOf(in->high) <= 0) continue;
        net::ServerSession* server = net::g_gameServer;
        if (!IsLive(server)) continue;
        if (!server->PlayerStateIs3(node.Get())) continue;
        player = node.Get();
        DriverLink* driver = NewDriver(this, player);
        holders.emplace_back(driver);
    }
    for (auto it = drivers.begin(); it != drivers.end();) {
        it->Get()->Pump();
        if (IsLive(it->Get()->player.Get())) {
            ++it;
        } else {
            it = drivers.erase(it);
        }
    }
}

// Both lists emptied, then one driver for `player`: started, and its pending output
// drained into throwaway memory streams while its virtual check answers true. Returns the driver.
DriverLink* DriverSession::Start(net::Connection* player) {
    drivers.clear();
    holders.clear();
    DriverLink* driver = NewDriver(this, player);
    driver->Pump();
    while (driver->HasIncoming()) {
        MemoryStream stream;
        driver->Receive(&stream);
    }
    return driver;
}

} // namespace netlink
