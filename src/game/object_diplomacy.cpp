#include "object_diplomacy.h"

#include "core_streams.h"
#include "object.h"
#include "player_messages.h"   // CPlayer::units_
#include "unit.h"

#include "world.h"

namespace {

// SHL r32,CL: the count is masked to 5 bits.
std::uint32_t Bit(int n) { return 1u << (static_cast<unsigned>(n) & 31); }

} // namespace

void CDiplomacy::Reset() {
    for (int i = 0; i < 32; ++i) hostile_[i] = 0;
    for (int i = 0; i < 32; ++i) allied_[i] = 1u << i;
}

void CDiplomacy::SetRelation(int player, int other, int relation, BOOL applyToUnits) {
    std::uint32_t bit = Bit(other);
    switch (relation) {
    case kRelationAllied:
        hostile_[player] &= ~bit;
        allied_[player] |= bit;
        break;
    case kRelationNeutral:
        hostile_[player] &= ~bit;
        allied_[player] &= ~bit;
        break;
    case kRelationHostile:
        hostile_[player] |= bit;
        allied_[player] &= ~bit;
        break;
    default:
        break;
    }
    if (!applyToUnits) return;

    ObjectListIterator it;
    it.manager = nullptr;
    for (CEngineObject* object = it.First(g_serverWorld->objectManager_); object; object = it.Next()) {
        CUnit* unit = UnitOrNull(object);
        if (!unit) continue;
        if (unit->side != player) continue;
        std::uint32_t mask = unit->relationsA;
        mask = relation == kRelationHostile ? mask | Bit(other) : mask & ~Bit(other);
        unit->SetHostileMask(mask);
    }
    if (it.manager) --it.manager->refCount_;
}

int CDiplomacy::GetRelation(int player, int other) const {
    if (IsAllied(player, other)) return kRelationAllied;
    return (hostile_[player] & Bit(other)) != 0 ? kRelationHostile : kRelationNeutral;
}

int CDiplomacy::GetHostileMask(int player) const {
    if (player >= 0 && player < 32) return static_cast<int>(hostile_[player]);
    return -1;
}

void CDiplomacy::ReadFrom(CStream* stream) {
    stream->Read(allied_, sizeof(allied_));
    stream->Read(hostile_, sizeof(hostile_));
}

void CDiplomacy::WriteTo(CStream* stream) {
    stream->Write(allied_, sizeof(allied_));
    stream->Write(hostile_, sizeof(hostile_));
}

void CDiplomacy::OnAttack(CUnit* attacker, CUnit* victim) {
    if (!attacker || !victim) return;
    if (IsAllied(victim->side, attacker->side)) return;
    victim->AddHostileMask(victim->relationsA | Bit(attacker->side));
    victim->flags_ |= MapObjectFlag::RelationsChanged;
    // The victim's party: the player's units, walked in iterator order.
    const CPlayer* party = victim->player;
    if (!party) return;
    const CPlayer::UnitMap* members = &party->units_;
    for (auto node = members->begin(); node != members->end() && node->second; ++node) {
        CUnit* unit = node->second;
        unit->AddHostileMask(unit->relationsA | Bit(attacker->side));
    }
}
