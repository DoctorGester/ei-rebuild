// Small functions of the unit statistics and databases: the packed vitals' and parameters' network fields,
// the attack parameters' difficulty scale and the databases' record lookups. The stat ratios are in
// unit_stats.cpp and the school's price in unit_skill_price.cpp (ei_vectors links those). Plain C++ float
// maths (rule 7).
#include "combat.h"
#include "options.h"
#include "net_delta.h"
#include "unit.h"
#include "unit_create.h"
#include "unit_db.h"

#include "game_data.h"
#include "logic_settings.h"

namespace {
// The game's settings (globals read at the time of the call).
int Difficulty() { return options::TheOptions()->difficulty_; }
} // namespace

void AttackParams::ScaleForDifficulty(int keyed) {
    if (keyed != 0) return;
    attack = static_cast<int>(attack * GameLogicSettings().attack[Difficulty()]);
    defence =
        static_cast<int>(static_cast<float>(defence) * GameLogicSettings().defence[Difficulty()]);
}

// --- Database records ---------------------------------------------------------------------------------

UnitModelRecord* UnitModelTable::At(std::uint32_t index) { return unitdb::RecordAt(records, index); }

// Prototype `index`, or null.
UnitPrototypeRecord* UnitPrototypeTable::At(std::uint32_t index) {
    return unitdb::RecordAt(records, index);
}
