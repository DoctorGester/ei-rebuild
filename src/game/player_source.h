#pragma once

// The worlds' player sources (base vtable 0073F38C; world_sessions.cpp): reference objects the
// server world's update pumps (slot 5) and asks for new connections (slot 6, AcceptPlayers
// 0058DAF0). Two kinds: the counted helper at CWorld + 0x5C (CWorldHandle, world.h: hands over a
// pending link once) and the network session at CWorld + 0x60 (netlink::DriverSession, net_link.h).

#include "core_refcount.h"

#include <cstdint>

class CPlayerSource : public CRefObject {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073F38C);

    // 0: 0055F2F0 (DeletingDestructBase); 2 / 3 / 4 / 6 pure in the base.
    virtual bool Query();               // 4: the helpers 0055F250 / 005626F0 AL 0, the session 0047A220 AL 1
    virtual void Pump();                // 5: 0055F190 (empty) in every source
    virtual void TakeNew(void* joined); // 6: new connections appended to an MFC list of links

    // 0055F2E0. The destructor body: the root vtable stored (the base has no fields).
    void DestructBase();
    // 0055F2F0 (slot 0 of the base vtable). DestructBase, then operator delete when bit 0 of
    // `flags` is set. Returns this.
    CPlayerSource* DeletingDestructBase(unsigned flags);
};
static_assert(sizeof(CPlayerSource) == 8);
