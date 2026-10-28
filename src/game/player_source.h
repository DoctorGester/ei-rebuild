#pragma once

// The worlds' player sources (world_sessions.cpp): reference objects the
// server world's update pumps and asks for new connections (AcceptPlayers).
// Two kinds: the counted helper at CWorld + 0x5C (CWorldHandle, world.h: hands over a
// pending link once) and the network session at CWorld + 0x60 (netlink::DriverSession, net_link.h).

#include "core_refcount.h"

#include "lib/mfc.h"

#include <cstdint>
#include <list>

namespace netlink {
class NetLink;
}

// The list slot 6 appends the new connections to, counted (the server world's AcceptNewPlayers'
// local, a CTypedPtrList of links).
using JoinedLinks = std::list<netlink::NetLink*>;

class CPlayerSource : public CRefObject {
public:
    CPlayerSource() {
        refs_ = 0;
        flags6_ = 0;
        flags7_ = 0;
    }
    ~CPlayerSource() override;   // The destructor; the base has no fields

    // 2 / 3 / 4 / 6 pure in the base.
    virtual bool Query() = 0;           // Virtual 4: the helpers answer 0, the session 1
    virtual void Pump() {}              // Virtual 5: empty in every source
    virtual void TakeNew(JoinedLinks* joined) = 0;   // 6: new connections appended to the list
};
