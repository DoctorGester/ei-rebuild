#pragma once

// The party record lookup of player_messages.cpp. Kept apart from player_messages.h because
// unit.h declares PartyRecords too (as a view without fields).

#include <cstddef>
#include <cstdint>

// The records of a party's units (0x784 bytes each; the party is the world player's +0x70).
struct PartyRecords {
    // 006621B0. The record whose +0x1C is `unitId`, or null.
    std::uint8_t* Find(int unitId) const;

    std::uint8_t unknown_00[0x30];
    std::uint8_t* records;          // +0x30
    int count;                      // +0x34
};
static_assert(offsetof(PartyRecords, records) == 0x30);
