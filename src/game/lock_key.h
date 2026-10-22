#pragma once

// A key and the lock it is tested against (levers and traps keep their lock at +0x244; a zone
// file's lever record holds one, map_spec.h's LeverSetup): {mask, id, level}. Names provisional.

#include "lib/mfc.h"   // BOOL

#include <cstdint>

class CStream;   // core_streams.h

struct LockKey {
    // 00522A80. Do the masks overlap, and either (bit 3 in common) the ids match, or (bits 0-2 in
    // common) this level is at least the lock's (unsigned)? Rebirth's FixLeversActivate compares
    // signed, and for a lock of level 0 returns this level + 1.
    mfc::BOOL Opens(const LockKey* lock);
    // 00522AD0 (trap_object.cpp). Always 1: placed traps take any key.
    mfc::BOOL AcceptedByTrap() const;
    // 005229F0 / 00522A30 (RET 4; lever_object.cpp, A-U11; the lever's Load / Save). Reads / writes
    // the three dwords (the writes through a stack copy each).
    void ReadFrom(CStream* stream);
    void WriteTo(CStream* stream) const;
    // 005F5F90 (ui_small_members.cpp). The constructor of the battle screen's item key
    // (CBattleScreen::itemUseKey_): {1, 0, 0}. Returns this.
    LockKey* ConstructItemUseKey();

    std::uint32_t mask;
    std::uint32_t id;
    std::uint32_t level;
};
static_assert(sizeof(LockKey) == 0xC);
