#pragma once

// The diplomacy table between the 32 players (global at 007C2BC4). Name provisional.

#include <cstddef>
#include <cstdint>

using BOOL = int;

class CStream;
class CUnit;

enum DiplomacyRelation : int {
    kRelationAllied = 0,
    kRelationNeutral = 1,
    kRelationHostile = 2,
};

struct CDiplomacy {
    // 004554B0. Every player is allied with itself only; nobody is hostile.
    void Reset();

    // 004554E0. Sets how `player` treats `other` (row `player`, bit `other`): 0 allied, 1 neutral,
    // 2 hostile; other values leave the table alone. With `applyToUnits`, every unit of `player`
    // gets bit `other` of its hostile mask set (relation 2) or cleared (anything else).
    void SetRelation(int player, int other, int relation, BOOL applyToUnits);

    // 00455630. 0 if `player` is allied with `other`, else 2 if hostile, else 1.
    int GetRelation(int player, int other) const;

    // 00455660. Row `player` of the hostile table; -1 outside 0..31.
    int GetHostileMask(int player) const;

    // 004557C0 / 004557F0. Both tables read from / written to a stream, 0x80 bytes each.
    void ReadFrom(CStream* stream);
    void WriteTo(CStream* stream);

    // 00455680. `attacker` attacked `victim`: unless the victim's player is allied with the
    // attacker's, the victim, then the members of the victim's party up to the first empty entry,
    // become hostile to the attacker's player. The third argument is unused.
    void OnAttack(CUnit* attacker, CUnit* victim, int unused);

    std::uint32_t allied_[32];    // +0x00 bit q of row p: p is allied with q
    std::uint32_t hostile_[32];   // +0x80 bit q of row p: p is hostile to q
};
static_assert(offsetof(CDiplomacy, hostile_) == 0x80);
static_assert(sizeof(CDiplomacy) == 0x100);

// 007C2BC4: the game's diplomacy table.
inline CDiplomacy* GameDiplomacy() { return reinterpret_cast<CDiplomacy*>(0x007C2BC4); }
