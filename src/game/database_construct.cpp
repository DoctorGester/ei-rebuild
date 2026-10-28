// The compiler-generated constructors of the resource databases. Only the static initialisers call them,
// before the hooks are installed (the spell and item databases' also the resource switch and reload of a
// network game), so the hook build checks them at start-up against what the originals built; the standalone
// image runs them. Each is the members' constructors inlined: a vector empty, a hash_map with 100 buckets
// (STLport rounds it up to 193).
#include "acks_db.h"
#include "item_db.h"
#include "model.h"
#include "quest_records.h"
#include "unit_db.h"

#include <memory>
#include <vector>

namespace {
constexpr std::size_t kBuckets = 100;
} // namespace

SkillDatabase* SkillDatabase::Construct() {
    std::construct_at(&skills);
    std::construct_at(&levels);
    return this;
}

namespace itemdb {

template <typename Record>
void CodeIndexedTable<Record>::Construct() {
    std::construct_at(&records);
    byCode.InitBuckets(kBuckets);
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
    names.InitBuckets(kBuckets);
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
    questsByName.InitBuckets(kBuckets);
    std::construct_at(&briefings.records);
    briefings.index.InitBuckets(kBuckets);
    return this;
}

PrintsDatabase* PrintsDatabase::Construct() {
    std::construct_at(&blood);
    bloodByName.InitBuckets(kBuckets);
    std::construct_at(&footprints);
    footprintsByName.InitBuckets(kBuckets);
    std::construct_at(&scorch);
    scorchByName.InitBuckets(kBuckets);
    return this;
}

namespace acks {

AckDatabase* AckDatabase::Construct() {
    std::construct_at(&units.records);
    units.byName.InitBuckets(kBuckets);
    std::construct_at(&models.records);
    models.byName.InitBuckets(kBuckets);
    std::construct_at(&extras.records);
    extras.byName.InitBuckets(kBuckets);
    return this;
}

} // namespace acks
