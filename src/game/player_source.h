#pragma once

// The worlds' player sources (world_sessions.cpp; real classes since C28g, game.exe's base vtable
// 0073F38C and the sources' retired): reference objects the
// server world's update pumps (slot 5) and asks for new connections (slot 6, AcceptPlayers
// 0058DAF0). Two kinds: the counted helper at CWorld + 0x5C (CWorldHandle, world.h: hands over a
// pending link once) and the network session at CWorld + 0x60 (netlink::DriverSession, net_link.h).

#include "core_refcount.h"

#include "lib/mfc.h"

#include <cstdint>

namespace netlink {
class NetLink;   // net_link.h
}

// The list slot 6 appends the new connections to, counted (the server world's AcceptNewPlayers'
// local, a CTypedPtrList of links).
using JoinedLinks = mfc::CTypedPtrList<netlink::NetLink>;

class CPlayerSource : public CRefObject {
public:
    // A new source: no references, no flags.
    CPlayerSource() {
        refs_ = 0;
        flags6_ = 0;
        flags7_ = 0;
    }
    ~CPlayerSource() override;   // 0055F2E0 (deleting: 0055F2F0); the base has no fields

    // 2 / 3 / 4 / 6 pure in the base.
    virtual bool Query() = 0;           // 4: the helpers 0055F250 / 005626F0 AL 0, the session 0047A220 AL 1
    virtual void Pump() {}              // 5: 0055F190 (empty) in every source
    virtual void TakeNew(JoinedLinks* joined) = 0;   // 6: new connections appended to the list
};
