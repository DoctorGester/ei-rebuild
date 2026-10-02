#include "object_diplomacy.h"

#include "object.h"

#include "ei/hook.h"
#include "ei/vcall.h"

namespace {

// SHL r32,CL: the count is masked to 5 bits.
std::uint32_t Bit(int n) { return 1u << (static_cast<unsigned>(n) & 31); }

// A party's members: an STLport hash_map<key, CUnit*> at party +0x40 (buckets vector at +4).
struct PartyNode {
    PartyNode* next;
    std::uint32_t key;      // hashed by value (IntHash)
    CUnit* unit;
};
struct PartyMembers {
    std::uint32_t functors;
    PartyNode** buckets;
    PartyNode** finish;
};

std::uint32_t BucketCount(const PartyMembers* map) {
    // (finish - start) >> 2, signed shift then used unsigned, as STLport's vector size.
    return static_cast<std::uint32_t>(static_cast<int>(reinterpret_cast<std::uintptr_t>(map->finish) -
                                                       reinterpret_cast<std::uintptr_t>(map->buckets)) >> 2);
}

// 00455830 (hashtable::begin): the head of the first non-empty bucket.
PartyNode* FirstNode(const PartyMembers* map) {
    for (std::uint32_t i = 0; i < BucketCount(map); ++i)
        if (map->buckets[i]) return map->buckets[i];
    return nullptr;
}

// operator++ (004558B0 when the chain ends): the next node in the chain, else the head of the next
// non-empty bucket after the node's own.
PartyNode* NextNode(const PartyMembers* map, const PartyNode* node) {
    if (node->next) return node->next;
    std::uint32_t bucket = node->key % BucketCount(map);
    for (;;) {
        ++bucket;
        if (BucketCount(map) <= bucket) return nullptr;
        if (PartyNode* head = map->buckets[bucket]) return head;
    }
}

} // namespace

void CDiplomacy::Reset() {
    for (int i = 0; i < 32; ++i) hostile_[i] = 0;
    for (int i = 0; i < 32; ++i) allied_[i] = 1u << i;
}
EI_REPLACE_METHOD(0x004554B0, CDiplomacy::Reset);

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
        if (ei::VCall<int>(object, 12) != kClassUnit) continue;   // GetClassId
        CUnit* unit = static_cast<CUnit*>(object);
        if (FieldAt<int>(unit, unitfield::kPlayer) != player) continue;
        std::uint32_t mask = FieldAt<std::uint32_t>(unit, unitfield::kHostileMask);
        mask = relation == kRelationHostile ? mask | Bit(other) : mask & ~Bit(other);
        UnitSetHostileMask(unit, nullptr, mask);
    }
    if (it.manager) --it.manager->refCount_;
}
EI_REPLACE_METHOD(0x004554E0, CDiplomacy::SetRelation);

int CDiplomacy::GetRelation(int player, int other) const {
    std::uint32_t bit = Bit(other);
    if (allied_[player] & bit) return kRelationAllied;
    return (hostile_[player] & bit) != 0 ? kRelationHostile : kRelationNeutral;
}
EI_REPLACE_METHOD(0x00455630, CDiplomacy::GetRelation);

int CDiplomacy::GetHostileMask(int player) const {
    if (player >= 0 && player < 32) return static_cast<int>(hostile_[player]);
    return -1;
}
EI_REPLACE_METHOD(0x00455660, CDiplomacy::GetHostileMask);

void CDiplomacy::ReadFrom(void* stream) {
    ei::VCall<unsigned>(stream, 1, static_cast<void*>(allied_), 0x80u);
    ei::VCall<unsigned>(stream, 1, static_cast<void*>(hostile_), 0x80u);
}
EI_REPLACE_METHOD(0x004557C0, CDiplomacy::ReadFrom);

void CDiplomacy::WriteTo(void* stream) {
    ei::VCall<unsigned>(stream, 2, static_cast<void*>(allied_), 0x80u);
    ei::VCall<unsigned>(stream, 2, static_cast<void*>(hostile_), 0x80u);
}
EI_REPLACE_METHOD(0x004557F0, CDiplomacy::WriteTo);

void CDiplomacy::OnAttack(CUnit* attacker, CUnit* victim, int) {
    if (!attacker || !victim) return;
    if (allied_[FieldAt<int>(victim, unitfield::kPlayer)] & Bit(FieldAt<int>(attacker, unitfield::kPlayer))) return;
    UnitAddHostileMask(victim, nullptr,
                       FieldAt<std::uint32_t>(victim, unitfield::kHostileMask) |
                           Bit(FieldAt<int>(attacker, unitfield::kPlayer)));
    victim->flags_ |= 0x80000;
    char* party = FieldAt<char*>(victim, unitfield::kParty);
    if (!party) return;
    auto* members = reinterpret_cast<const PartyMembers*>(party + 0x40);
    for (PartyNode* node = FirstNode(members); node && node->unit; node = NextNode(members, node)) {
        CUnit* unit = node->unit;
        UnitAddHostileMask(unit, nullptr,
                           FieldAt<std::uint32_t>(unit, unitfield::kHostileMask) |
                               Bit(FieldAt<int>(attacker, unitfield::kPlayer)));
    }
}
EI_REPLACE_METHOD(0x00455680, CDiplomacy::OnAttack);
