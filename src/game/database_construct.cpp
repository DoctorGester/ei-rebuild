// The compiler-generated constructors of the resource databases (S3b). Only the static
// initialisers call them, before the hooks are installed (the spell and item databases' also the
// resource switch 0057CB50 and reload 0057DEE0 of a network game), so the hook build checks them at
// start-up against what the originals built (verify_global_constructors.cpp until S21d); the
// standalone image runs them. Each is the members' constructors inlined: an STLport vector empty,
// a hash_map with 100 buckets (STLport rounds it up to 193).
#include "acks_db.h"
#include "item_db.h"
#include "model.h"
#include "quest_records.h"
#include "unit_db.h"

#include "ei/hook.h"

#include <initializer_list>
#include <new>

namespace {
constexpr std::size_t kBuckets = 100;
} // namespace

SkillDatabase* SkillDatabase::Construct() {
    new (&skills) stlp::vector<SkillRecord>();
    new (&levels) stlp::vector<SkillLevelRecord>();
    return this;
}
EI_REPLACE_METHOD(0x0051FB10, SkillDatabase::Construct);

namespace itemdb {

void CodeIndexedTable::Construct() {
    records.begin = nullptr;
    records.end = nullptr;
    capacityEnd = nullptr;
    byCode.InitBuckets(kBuckets);
}

SpellDatabase* SpellDatabase::Construct() {
    spells.Construct();
    modifiers.Construct();
    for (stlp::vector<SpellListRecord>& list : lists) new (&list) stlp::vector<SpellListRecord>();
    return this;
}

void IndexedRecordTable::Construct() {
    records.begin = nullptr;
    records.end = nullptr;
    capacityEnd = nullptr;
    ByName().InitBuckets(kBuckets);
}

ItemDatabase* ItemDatabase::Construct() {
    for (IndexedRecordTable* table : {&materials, &weapons, &armors, &quickItems, &questItems, &loot}) table->Construct();
    return this;
}

} // namespace itemdb

EI_REPLACE_METHOD(0x0057E2F0, itemdb::SpellDatabase::Construct);
EI_REPLACE_METHOD(0x0057E3D0, itemdb::ItemDatabase::Construct);

QuestDatabase* QuestDatabase::Construct() {
    new (&quests) stlp::vector<QuestRecord>();
    questsByName.InitBuckets(kBuckets);
    new (&briefings.records) stlp::vector<QuestBriefingRecord>();
    briefings.index.InitBuckets(kBuckets);
    return this;
}
EI_REPLACE_METHOD(0x006AE0F0, QuestDatabase::Construct);

PrintsDatabase* PrintsDatabase::Construct() {
    new (&blood) stlp::vector<PrintRecord>();
    bloodByName.InitBuckets(kBuckets);
    new (&footprints) stlp::vector<PrintRecord2>();
    footprintsByName.InitBuckets(kBuckets);
    new (&scorch) stlp::vector<PrintRecord>();
    scorchByName.InitBuckets(kBuckets);
    return this;
}
EI_REPLACE_METHOD(0x006D0250, PrintsDatabase::Construct);

namespace acks {

AckDatabase* AckDatabase::Construct() {
    new (&units.records) stlp::vector<UnitAcks>();
    units.byName.InitBuckets(kBuckets);
    new (&models.records) stlp::vector<ModelAcks>();
    models.byName.InitBuckets(kBuckets);
    new (&extras.records) stlp::vector<ExtraAcks>();
    extras.byName.InitBuckets(kBuckets);
    return this;
}

} // namespace acks

EI_REPLACE_METHOD(0x006D7220, acks::AckDatabase::Construct);
