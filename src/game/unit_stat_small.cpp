// Small functions of the unit statistics and databases (A-U12): the packed vitals' and parameters'
// network fields, the attack parameters' difficulty scale and the databases' record lookups. The
// stat ratios are in unit_stats.cpp and the school's price in unit_skill_price.cpp (ei_vectors
// links those). Plain C++ float maths (rule 7).
#include "combat.h"
#include "net_delta.h"
#include "unit.h"
#include "unit_create.h"
#include "unit_db.h"

#include "ei/hook.h"
#include "game_data.h"

namespace {
// The game's settings (globals read at the time of the call).
constexpr std::uintptr_t kDifficulty = std::uintptr_t(0x0079BEC0);
constexpr std::uintptr_t kAttackFactors = std::uintptr_t(0x007C54AC);    // a float per difficulty
constexpr std::uintptr_t kDefenceFactors = std::uintptr_t(0x007C54B8);

int Difficulty() { return ei::GameData<const int>(kDifficulty); }

// Element `index` of the records between `begin` and `end`, or null past the end (the count is a
// signed division, the comparison unsigned, as the original's).
template <typename T>
T* RecordAt(T* begin, T* end, std::uint32_t index) {
    return index < static_cast<std::uint32_t>(end - begin) ? begin + index : nullptr;
}
} // namespace

void AttackParams::ScaleForDifficulty(int keyed) {
    if (keyed != 0) return;
    attack = static_cast<int>(attack * ei::GamePointer<const float>(kAttackFactors)[Difficulty()]);
    defence =
        static_cast<int>(static_cast<float>(defence) * ei::GamePointer<const float>(kDefenceFactors)[Difficulty()]);
}

// --- Database records ---------------------------------------------------------------------------------

SkillRecord* SkillDatabase::SkillAt(std::uint32_t index) { return RecordAt(skills.begin(), skills.end(), index); }

SkillLevelRecord* SkillDatabase::LevelAt(std::uint32_t index) { return RecordAt(levels.begin(), levels.end(), index); }

// 00539280 (thiscall on the unit database's model vector, RET 4): model `index`, or null.
UnitModelRecord* __fastcall UnitModelAt(UnitModelRecord** models, void*, std::uint32_t index) {
    return RecordAt(models[0], models[1], index);
}

// 0053AD30 (thiscall on the prototype table, RET 4): prototype `index`, or null.
UnitPrototypeRecord* UnitPrototypeTable::At(std::uint32_t index) {
    return RecordAt(reinterpret_cast<UnitPrototypeRecord*>(begin),
                    reinterpret_cast<UnitPrototypeRecord*>(end), index);
}
