// Small functions of the unit statistics and databases (A-U12): the packed vitals' and parameters'
// network fields, the attack parameters' difficulty scale and the databases' record lookups. The
// stat ratios are in unit_stats.cpp and the school's price in unit_skill_price.cpp (ei_vectors
// links those). Plain C++ float maths (rule 7).
#include "combat.h"
#include "options.h"
#include "net_delta.h"
#include "unit.h"
#include "unit_create.h"
#include "unit_db.h"

#include "ei/hook.h"
#include "game_data.h"
#include "logic_settings.h"

namespace {
// The game's settings (globals read at the time of the call).
int Difficulty() { return options::TheOptions()->difficulty_; }

// Element `index` of the records between `begin` and `end`, or null past the end (the count is a
// signed division, the comparison unsigned, as the original's).
template <typename T>
T* RecordAt(T* begin, T* end, std::uint32_t index) {
    return index < static_cast<std::uint32_t>(end - begin) ? begin + index : nullptr;
}
} // namespace

void AttackParams::ScaleForDifficulty(int keyed) {
    if (keyed != 0) return;
    attack = static_cast<int>(attack * GameLogicSettings().attack[Difficulty()]);
    defence =
        static_cast<int>(static_cast<float>(defence) * GameLogicSettings().defence[Difficulty()]);
}
EI_REWRITTEN(0x0052E0D0, AttackParams::ScaleForDifficulty);

// --- Database records ---------------------------------------------------------------------------------

SkillRecord* SkillDatabase::SkillAt(std::uint32_t index) { return RecordAt(skills.begin(), skills.end(), index); }
EI_REWRITTEN(0x00526FF0, SkillDatabase::SkillAt);

SkillLevelRecord* SkillDatabase::LevelAt(std::uint32_t index) { return RecordAt(levels.begin(), levels.end(), index); }
EI_REWRITTEN(0x00524FA0, SkillDatabase::LevelAt);

// 00539280 (thiscall on the unit database's model vector, RET 4): model `index`, or null.
UnitModelRecord* __fastcall UnitModelAt(UnitModelRecord** models, void*, std::uint32_t index) {
    return RecordAt(models[0], models[1], index);
}
EI_REWRITTEN(0x00539280, UnitModelAt);

// 0053AD30 (thiscall on the prototype table, RET 4): prototype `index`, or null.
UnitPrototypeRecord* UnitPrototypeTable::At(std::uint32_t index) {
    return RecordAt(reinterpret_cast<UnitPrototypeRecord*>(begin),
                    reinterpret_cast<UnitPrototypeRecord*>(end), index);
}
EI_REWRITTEN(0x0053AD30, UnitPrototypeTable::At);
