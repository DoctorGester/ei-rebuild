// The compiler-generated constructors of the resource databases. Only the static initialisers call them,
// before the hooks are installed (the spell and item databases' also the resource switch and reload of a
// network game), so the hook build checks them at start-up against what the originals built; the standalone
// image runs them. Each is the members' constructors inlined: a vector empty, an empty index.
#include "acks_db.h"
#include "item_db.h"
#include "model.h"
#include "quest_records.h"
#include "unit_db.h"

#include <memory>
#include <vector>

SkillDatabase* SkillDatabase::Construct() {
    std::construct_at(&skills);
    std::construct_at(&levels);
    return this;
}

namespace itemdb {

template <typename Record>
void CodeIndexedTable<Record>::Construct() {
    std::construct_at(&records);
    std::construct_at(&byCode);
}
template void CodeIndexedTable<SpellRecord>::Construct();
template void CodeIndexedTable<SpellModifierRecord>::Construct();

SpellDatabase* SpellDatabase::Construct() {
    spells.Construct();
    modifiers.Construct();
    for (std::vector<SpellListRecord>& list : lists) std::construct_at(&list);
    return this;
}

template <typename Record>
void NamedRecordTable<Record>::Construct() {
    std::construct_at(&records);
    std::construct_at(&names);
}

ItemDatabase* ItemDatabase::Construct() {
    materials.Construct();
    weapons.Construct();
    armors.Construct();
    quickItems.Construct();
    questItems.Construct();
    loot.Construct();
    return this;
}

} // namespace itemdb

QuestDatabase* QuestDatabase::Construct() {
    std::construct_at(&quests);
    std::construct_at(&questsByName);
    std::construct_at(&briefings.records);
    std::construct_at(&briefings.index);
    return this;
}

PrintsDatabase* PrintsDatabase::Construct() {
    std::construct_at(&blood);
    std::construct_at(&bloodByName);
    std::construct_at(&footprints);
    std::construct_at(&footprintsByName);
    std::construct_at(&scorch);
    std::construct_at(&scorchByName);
    return this;
}

namespace acks {

AckDatabase* AckDatabase::Construct() {
    std::construct_at(&units.records);
    std::construct_at(&units.byName);
    std::construct_at(&models.records);
    std::construct_at(&models.byName);
    std::construct_at(&extras.records);
    std::construct_at(&extras.byName);
    return this;
}

} // namespace acks
