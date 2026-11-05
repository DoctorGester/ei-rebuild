// The "netstate" step in the image (image_side.h; harness_netstate.cpp).
#include "image_side.h"

#include "../harness_netstate_read.h"

#include "game/net_classes.h"

namespace ei::verify::image {

std::vector<std::string> NetState() { return pair::NetLines<net::ServerEntry>(net::g_gameServer.Get(), net::g_gameClient.Get()); }

int ServerAwards() {
    const net::ServerSession* server = net::g_gameServer.Get();
    return server ? static_cast<int>(server->awards.size()) : -1;
}

int SessionPlayerLimit() {
    const net::ClientSession* client = net::g_gameClient.Get();
    return client ? client->PlayerLimit() : -1;
}

} // namespace ei::verify::image
