#include "object_diplomacy.h"

#include "core_streams.h"
#include "object.h"
#include "player_messages.h"   // CPlayer::units
#include "unit.h"

#include "world.h"

namespace {

// SHL r32,CL: the count is masked to 5 bits.
std::uint32_t Bit(int n) { return 1u << (static_cast<unsigned>(n) & 31); }

} // namespace

void Diplomacy::Reset() {
    for (int i = 0; i < 32; ++i) hostile[i] = 0;
    for (int i = 0; i < 32; ++i) allied[i] = 1u << i;
}

void Diplomacy::SetRelation(int player, int other, int relation) {
    std::uint32_t bit = Bit(other);
    switch (relation) {
    case kRelationAllied:
        hostile[player] &= ~bit;
        allied[player] |= bit;
        break;
    case kRelationNeutral:
        hostile[player] &= ~bit;
        allied[player] &= ~bit;
        break;
    case kRelationHostile:
        hostile[player] |= bit;
        allied[player] &= ~bit;
        break;
    default:
        break;
    }
    ObjectListIterator it;
    it.manager = nullptr;
    for (EngineObject* object = it.First(g_serverWorld.objectManager); object; object = it.Next()) {
        CUnit* unit = UnitOrNull(object);
        if (!unit) continue;
        if (unit->side != player) continue;
        std::uint32_t mask = unit->relationsA;
        mask = relation == kRelationHostile ? mask | Bit(other) : mask & ~Bit(other);
        unit->SetHostileMask(mask);
    }
    if (it.manager) it.manager->ForgetReference();
}

int Diplomacy::GetRelation(int player, int other) const {
    if (IsAllied(player, other)) return kRelationAllied;
    return (hostile[player] & Bit(other)) != 0 ? kRelationHostile : kRelationNeutral;
}

int Diplomacy::GetHostileMask(int player) const {
    if (player >= 0 && player < 32) return static_cast<int>(hostile[player]);
    return -1;
}

void Diplomacy::ReadFrom(ByteStream* stream) {
    stream->Read(allied, sizeof(allied));
    stream->Read(hostile, sizeof(hostile));
}

void Diplomacy::WriteTo(ByteStream* stream) {
    stream->Write(allied, sizeof(allied));
    stream->Write(hostile, sizeof(hostile));
}

void Diplomacy::OnAttack(CUnit* attacker, CUnit* victim) {
    if (!attacker || !victim) return;
    if (IsAllied(victim->side, attacker->side)) return;
    victim->AddHostileMask(victim->relationsA | Bit(attacker->side));
    victim->flags |= MapObjectFlag::RelationsChanged;
    // The victim's party: the player's units, walked in iterator order.
    const CPlayer* party = victim->player;
    if (!party) return;
    const CPlayer::UnitMap* members = &party->units;
    for (auto node = members->begin(); node != members->end() && node->second; ++node) {
        CUnit* unit = node->second;
        unit->AddHostileMask(unit->relationsA | Bit(attacker->side));
    }
}
