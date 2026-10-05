// Checks for unit_steal.cpp: CUnit::TakeRandomItem (00531400) changes only the unit's carried
// items (count and order), its quick item slots, what MarkChanged writes (changedParts, flags_,
// changeStamp) and the game's random generator. Differential: those are saved, the original
// runs, its result and the state after it are taken, the state is put back and ours runs; the
// item and every saved field must match (the carried entries up to the old count). A unit with
// more than 64 carried items runs ours only. Returns the item. Reached when a thief steals from
// a unit (CUnitServer's actions, combat_actions.cpp kind 1); probably no scenario does that.
#include "verify.h"

#include "../../src/game/random.h"
#include "../../src/game/unit.h"

#include <cstring>

namespace {
IsaacRandom& Random() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }

constexpr int kMaxCarried = 64;

struct StealState {
    IsaacRandom random;
    CEngineObject* carried[kMaxCarried];
    int carriedCount;
    CEngineObject* quickItems[4];
    std::uint16_t changedParts;
    std::uint32_t flags;
    int changeStamp;

    void Take(CUnit* unit, int count) {
        std::memcpy(&random, &Random(), sizeof(random));
        std::memcpy(carried, unit->carried, count * sizeof(CEngineObject*));
        carriedCount = unit->carriedCount;
        std::memcpy(quickItems, unit->quickItems, sizeof(quickItems));
        changedParts = unit->changedParts;
        flags = unit->flags_;
        changeStamp = unit->changeStamp;
    }
    void Put(CUnit* unit, int count) const {
        std::memcpy(&Random(), &random, sizeof(random));
        std::memcpy(unit->carried, carried, count * sizeof(CEngineObject*));
        unit->carriedCount = carriedCount;
        std::memcpy(unit->quickItems, quickItems, sizeof(quickItems));
        unit->changedParts = changedParts;
        unit->flags_ = flags;
        unit->changeStamp = changeStamp;
    }
    bool Same(const StealState& other, int count) const {
        return std::memcmp(&random, &other.random, sizeof(random)) == 0 &&
               std::memcmp(carried, other.carried, count * sizeof(CEngineObject*)) == 0 &&
               carriedCount == other.carriedCount &&
               std::memcmp(quickItems, other.quickItems, sizeof(quickItems)) == 0 &&
               changedParts == other.changedParts && flags == other.flags && changeStamp == other.changeStamp;
    }
};
} // namespace

// 00531400: PUSH -1; PUSH 0x71E848 (7 bytes).
static CEngineObject*(__fastcall* s_takeRandomItem)(CUnit*);
static CEngineObject* __fastcall TakeRandomItemVerify(CUnit* unit) {
    int count = unit->carriedCount;
    if (count < 0 || count > kMaxCarried) return unit->TakeRandomItem();
    static StealState before, expected, actual;
    before.Take(unit, count);
    CEngineObject* original = s_takeRandomItem(unit);
    expected.Take(unit, count);
    before.Put(unit, count);
    CEngineObject* ours = unit->TakeRandomItem();
    actual.Take(unit, count);
    ei::verify::Check("CUnit::TakeRandomItem", original, ours, "unit %d (carried %d)", unit->id_, count);
    ei::verify::Check("CUnit::TakeRandomItem state", true, expected.Same(actual, count),
                      "unit %d: carried %d -> %d (%d expected), parts %04X (%04X)", unit->id_, count,
                      actual.carriedCount, expected.carriedCount, actual.changedParts, expected.changedParts);
    return ours;
}
EI_VERIFY(0x00531400, TakeRandomItemVerify, 7, s_takeRandomItem);
