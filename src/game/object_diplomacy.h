#pragma once

// The diplomacy table between the 32 players (a global). Name provisional.

#include <cstddef>
#include <cstdint>


class CStream;
class CUnit;

enum DiplomacyRelation : int {
    kRelationAllied = 0,
    kRelationNeutral = 1,
    kRelationHostile = 2,
};

struct CDiplomacy {
    // Every player is allied with itself only; nobody is hostile.
    void Reset();

    // Sets how `player` treats `other` (row `player`, bit `other`): 0 allied, 1 neutral,
    // 2 hostile; other values leave the table alone. With `applyToUnits`, every unit of `player`
    // gets bit `other` of its hostile mask set (relation 2) or cleared (anything else).
    void SetRelation(int player, int other, int relation);

    // 0 if `player` is allied with `other`, else 2 if hostile, else 1.
    int GetRelation(int player, int other) const;

    // Row `player` of the hostile table; -1 outside 0..31.
    int GetHostileMask(int player) const;

    // Both tables read from / written to a stream, 0x80 bytes each.
    void ReadFrom(CStream* stream);
    void WriteTo(CStream* stream);

    // `attacker` attacked `victim`: unless the victim's player is allied with the
    // attacker's, the victim, then the members of the victim's party up to the first empty entry,
    // become hostile to the attacker's player.
    void OnAttack(CUnit* attacker, CUnit* victim);

    // Whether `player` is allied with `other` (bit other & 31 of row `player`; a player is allied with itself).
    bool IsAllied(int player, int other) const { return (allied[player] & (1u << (other & 31))) != 0; }

    std::uint32_t allied[32];     // bit q of row p: p is allied with q
    std::uint32_t hostile[32];    // bit q of row p: p is hostile to q
};
