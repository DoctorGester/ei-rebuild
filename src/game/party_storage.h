#pragma once

// An embedded CParty (party.h; constructor 00661050, destructor body 006611D0): the party's bytes.
// CParty is abstract in its C++ declaration (the engine object's slots are the game's), so a class
// that holds one by value (CPlayer, the party records, the class templates, the client's trade
// parties, the game flow's request, parties on the stack) holds this; the originals or Construct /
// DestructBody build and tear it down. Party() is the one view of the bytes as the party.

#include <cstdint>

class CParty;   // party.h

struct PartyStorage {
    alignas(4) std::uint8_t bytes[0x70];   // sizeof(CParty), checked in party.h

    CParty* Party() { return reinterpret_cast<CParty*>(bytes); }
    const CParty* Party() const { return reinterpret_cast<const CParty*>(bytes); }
};
static_assert(sizeof(PartyStorage) == 0x70);
