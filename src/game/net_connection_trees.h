#pragma once

// The connection's five STLport maps. The types (value policies and stlp::TreeAbi types) are declared with the
// connection, whose fields they are (net_connection.h).

#include "net_connection.h"

namespace net::trees {

static_assert(sizeof(SentIds::Node) == 0x18 && sizeof(Records::Node) == 0x1C && sizeof(Attached::Node) == 0x18);
static_assert(sizeof(SentIds::Tree) == 0xC && sizeof(Records) == sizeof(Records::Tree));

} // namespace net::trees
