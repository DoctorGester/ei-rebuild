#pragma once

// A key and the lock it is tested against (levers and traps keep their lock; a zone
// file's lever record holds one, map_spec.h's LeverSetup): {mask, id, level}. Names provisional.


#include <cstdint>

class ByteStream;

struct LockKey {
    // Do the masks overlap, and either (bit 3 in common) the ids match, or (bits 0-2 in
    // common) this level is at least the lock's, compared signed; a lock of level 0 gives this level + 1
    // (Rebirth's FixLeversActivate; the original compared unsigned).
    int Opens(const LockKey* lock) const;
    // Always 1: placed traps take any key.
    bool AcceptedByTrap() const;
    // Reads / writes the three dwords.
    void ReadFrom(ByteStream* stream);
    void WriteTo(ByteStream* stream) const;
    std::uint32_t mask;
    std::uint32_t id;
    std::uint32_t level;
};
static_assert(sizeof(LockKey) == 0xC);
