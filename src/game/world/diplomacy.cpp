#include "game/world/diplomacy.h"

#include "engine/core/core_streams.h"
#include "game/world/hoover.h"
#include "game/players/player.h"   // Player::units
#include "game/units/unit.h"

#include "game/world/world.h"

namespace {

// SHL r32,CL: the count is masked to 5 bits.
std::uint32_t Bit(int n) { return 1u << (static_cast<unsigned>(n) & 31); }

} // namespace

void Diplomacy::Reset() {
    for (int i = 0; i < 32; ++i) hostile[i] = 0;
    for (int i = 0; i < 32; ++i) allied[i] = 1u << i;
}

void Diplomacy::SetRelation(int player, int other, DiplomacyRelation relation) {
    std::uint32_t bit = Bit(other);
    switch (relation) {
    case DiplomacyRelation::Allied:
        hostile[player] &= ~bit;
        allied[player] |= bit;
        break;
    case DiplomacyRelation::Neutral:
        hostile[player] &= ~bit;
        allied[player] &= ~bit;
        break;
    case DiplomacyRelation::Hostile:
        hostile[player] |= bit;
        allied[player] &= ~bit;
        break;
    default:
        break;
    }
    ObjectListIterator it;
    for (EngineObject* object = it.First(g_serverWorld.objectManager.get()); object; object = it.Next()) {
        Unit* unit = UnitOrNull(object);
        if (!unit) continue;
        if (unit->side != player) continue;
        std::uint32_t mask = unit->relationsA;
        mask = relation == DiplomacyRelation::Hostile ? mask | Bit(other) : mask & ~Bit(other);
        unit->SetHostileMask(mask);
    }
}

DiplomacyRelation Diplomacy::GetRelation(int player, int other) const {
    if (IsAllied(player, other)) return DiplomacyRelation::Allied;
    return (hostile[player] & Bit(other)) != 0 ? DiplomacyRelation::Hostile : DiplomacyRelation::Neutral;
}

int Diplomacy::GetHostileMask(int player) const {
    if (player >= 0 && player < 32) return static_cast<int>(hostile[player]);
    return -1;
}

void Diplomacy::Load(ByteStream* stream) {
    stream->Read(allied, sizeof(allied));
    stream->Read(hostile, sizeof(hostile));
}

void Diplomacy::Save(ByteStream* stream) const {
    stream->Write(allied, sizeof(allied));
    stream->Write(hostile, sizeof(hostile));
}

void Diplomacy::OnAttack(Unit* attacker, Unit* victim) {
    if (!attacker || !victim) return;
    if (IsAllied(victim->side, attacker->side)) return;
    victim->AddHostileMask(victim->relationsA | Bit(attacker->side));
    victim->flags |= MapObjectFlag::RelationsChanged;
    // The victim's party: the player's units, walked in iterator order.
    const Player* party = victim->player;
    if (!party) return;
    const Player::UnitMap* members = &party->units;
    for (auto node = members->begin(); node != members->end() && node->second; ++node) {
        Unit* unit = node->second;
        unit->AddHostileMask(unit->relationsA | Bit(attacker->side));
    }
}
