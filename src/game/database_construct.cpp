// The compiler-generated constructors of the resource databases. Only the static initialisers call them,
// before the hooks are installed (the spell and item databases' also the resource switch and reload of a
// network game), so the hook build checks them at start-up against what the originals built; the standalone
// image runs them. Each is the members' constructors inlined: an STLport vector empty, an empty index.
#include "acks_db.h"
#include "item_db.h"
#include "model.h"
#include "quest_records.h"
#include "unit_db.h"

#include <initializer_list>
#include <memory>
#include <new>

SkillDatabase* SkillDatabase::Construct() {
    new (&skills) stlp::vector<SkillRecord>();
    new (&levels) stlp::vector<SkillLevelRecord>();
    return this;
}

namespace itemdb {

template <typename Record>
void CodeIndexedTable<Record>::Construct() {
    new (&records) stlp::vector<Record>();
    std::construct_at(&byCode);
}
template void CodeIndexedTable<SpellRecord>::Construct();
template void CodeIndexedTable<SpellModifierRecord>::Construct();

SpellDatabase* SpellDatabase::Construct() {
    spells.Construct();
    modifiers.Construct();
    for (stlp::vector<SpellListRecord>& list : lists) new (&list) stlp::vector<SpellListRecord>();
    return this;
}

template <typename Record>
void NamedRecordTable<Record>::Construct() {
    records.ConstructEmpty();
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
    new (&quests) stlp::vector<QuestRecord>();
    std::construct_at(&questsByName);
    new (&briefings.records) stlp::vector<QuestBriefingRecord>();
    std::construct_at(&briefings.index);
    return this;
}

PrintsDatabase* PrintsDatabase::Construct() {
    new (&blood) stlp::vector<PrintRecord>();
    std::construct_at(&bloodByName);
    new (&footprints) stlp::vector<PrintRecord2>();
    std::construct_at(&footprintsByName);
    new (&scorch) stlp::vector<PrintRecord>();
    std::construct_at(&scorchByName);
    return this;
}

namespace acks {

AckDatabase* AckDatabase::Construct() {
    new (&units.records) stlp::vector<UnitAcks>();
    std::construct_at(&units.byName);
    new (&models.records) stlp::vector<ModelAcks>();
    std::construct_at(&models.byName);
    new (&extras.records) stlp::vector<ExtraAcks>();
    std::construct_at(&extras.byName);
    return this;
}

} // namespace acks
