#pragma once

// The worlds' player sources (world_sessions.cpp): reference objects the
// server world's update pumps and asks for new connections (AcceptPlayers).
// Two kinds: the counted helper at World + 0x5C (WorldHandle, world.h: hands over a
// pending link once) and the network session at World + 0x60 (netlink::DriverSession, net_link.h).

#include "core_refcount.h"

#include <cstdint>
#include <vector>

namespace netlink {
class NetLink;
}

class PlayerSource : public RefObject {
public:
    PlayerSource() {
        refs = 0;
        flags6 = 0;
        flags7 = 0;
    }
    ~PlayerSource() override;   // The destructor; the base has no fields

    // 2 / 3 / 4 / 6 pure in the base.
    virtual bool Query() = 0;           // Virtual 4: the helpers answer 0, the session 1
    virtual void Pump() {}              // Virtual 5: empty in every source
    virtual void TakeNew(std::vector<HeldRef<netlink::NetLink>>* joined) = 0;   // 6: new connections appended to the list
};
