// The "netstate" step in the image (image_side.h; harness_netstate.cpp).
#include "image_side.h"

#include "../harness_netstate_read.h"

#include "game/net_classes.h"

namespace ei::verify::image {

std::vector<std::string> NetState() { return pair::NetLines<net::ServerEntry>(net::g_gameServer.Get(), net::g_gameClient.Get()); }

} // namespace ei::verify::image
