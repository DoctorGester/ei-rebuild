#include "object_diplomacy.h"

#include "core_streams.h"
#include "object.h"
#include "unit.h"

#include "ei/hook.h"

namespace {

// SHL r32,CL: the count is masked to 5 bits.
std::uint32_t Bit(int n) { return 1u << (static_cast<unsigned>(n) & 31); }

// A party's members: an STLport hash_map<key, CUnit*> at party +0x40, walked in iterator order.
using PartyMembers = stlp::hash_map<std::uint32_t, CUnit*, stlp::IntHash>;

} // namespace

void CDiplomacy::Reset() {
    for (int i = 0; i < 32; ++i) hostile_[i] = 0;
    for (int i = 0; i < 32; ++i) allied_[i] = 1u << i;
}
EI_REWRITTEN(0x0058A370, CDiplomacy::Reset);
EI_REWRITTEN(0x004554B0, CDiplomacy::Reset);

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
    for (CEngineObject* object = it.First(WorldObjectManager()); object; object = it.Next()) {
        CUnit* unit = UnitOrNull(object);
        if (!unit) continue;
        if (FieldAt<int>(unit, unitfield::kPlayer) != player) continue;
        std::uint32_t mask = FieldAt<std::uint32_t>(unit, unitfield::kHostileMask);
        mask = relation == kRelationHostile ? mask | Bit(other) : mask & ~Bit(other);
        unit->SetHostileMask(mask);
    }
    if (it.manager) --it.manager->refCount_;
}
EI_REWRITTEN(0x004554E0, CDiplomacy::SetRelation);

int CDiplomacy::GetRelation(int player, int other) const {
    std::uint32_t bit = Bit(other);
    if (allied_[player] & bit) return kRelationAllied;
    return (hostile_[player] & bit) != 0 ? kRelationHostile : kRelationNeutral;
}
EI_REWRITTEN(0x00455630, CDiplomacy::GetRelation);

int CDiplomacy::GetHostileMask(int player) const {
    if (player >= 0 && player < 32) return static_cast<int>(hostile_[player]);
    return -1;
}
EI_REWRITTEN(0x00455660, CDiplomacy::GetHostileMask);

void CDiplomacy::ReadFrom(CStream* stream) {
    stream->Read(allied_, sizeof(allied_));
    stream->Read(hostile_, sizeof(hostile_));
}
EI_REWRITTEN(0x004557C0, CDiplomacy::ReadFrom);

void CDiplomacy::WriteTo(CStream* stream) {
    stream->Write(allied_, sizeof(allied_));
    stream->Write(hostile_, sizeof(hostile_));
}
EI_REWRITTEN(0x004557F0, CDiplomacy::WriteTo);

void CDiplomacy::OnAttack(CUnit* attacker, CUnit* victim, int) {
    if (!attacker || !victim) return;
    if (allied_[FieldAt<int>(victim, unitfield::kPlayer)] & Bit(FieldAt<int>(attacker, unitfield::kPlayer))) return;
    victim->AddHostileMask(FieldAt<std::uint32_t>(victim, unitfield::kHostileMask) |
                               Bit(FieldAt<int>(attacker, unitfield::kPlayer)));
    victim->flags_ |= 0x80000;
    char* party = FieldAt<char*>(victim, unitfield::kParty);
    if (!party) return;
    auto* members = reinterpret_cast<const PartyMembers*>(party + 0x40);
    for (const PartyMembers::Node* node = members->First(); node && node->value; node = members->After(node)) {
        CUnit* unit = node->value;
        unit->AddHostileMask(FieldAt<std::uint32_t>(unit, unitfield::kHostileMask) |
                                 Bit(FieldAt<int>(attacker, unitfield::kPlayer)));
    }
}
EI_REWRITTEN(0x00455680, CDiplomacy::OnAttack);
