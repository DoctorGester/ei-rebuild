// The resource databases' resets (S13a): the compiler-generated code that destroys a database and
// constructs it again in place, which the resource switch 0057CB50 runs before it loads the
// databases of another resource folder (the skill database's is SkillDatabase::Reset, unit_db.cpp).
// Each table's members go last to first: the name index (hash_map::Destroy: every chain from its
// head, each node's key string then the node, then the buckets), then the records through their
// vector's element policy (record_vectors.h: each record in order, then the storage). The
// constructors are database_construct.cpp's. Checked in game by verify_record_lifetime.cpp until S21d.
#include "item_db.h"
#include "record_vectors.h"
#include "unit_db.h"

#include "ei/hook.h"

namespace {

constexpr std::size_t kBuckets = 100;   // each index asks for 100 buckets (STLport: 193)

// A table's records: the STLport vector its first three words are.
template <typename R>
stlp::vector<R>& RecordsOf(void* table) {
    return *static_cast<stlp::vector<R>*>(table);
}

// ~vector<R>() as the element policy `Abi` destroys it.
template <typename Abi>
void DestroyRecords(void* table) {
    Abi::Destruct(&RecordsOf<typename Abi::Vector::value_type>(table));
}

// A unit table: the index, then the records.
template <typename Abi, typename Table>
void DestroyUnitTable(Table& table) {
    table.byName.Destroy();
    DestroyRecords<Abi>(&table);
}

// A unit table as the database's constructor builds it: no records, an empty index.
template <typename Table>
void ConstructUnitTable(Table& table) {
    table.begin = nullptr;
    table.end = nullptr;
    table.endOfStorage = nullptr;
    table.byName.InitBuckets(kBuckets);
}

// An item table: the index, then the records.
template <typename Abi>
void DestroyItemTable(itemdb::IndexedRecordTable& table) {
    table.ByName().Destroy();
    DestroyRecords<Abi>(&table);
}

} // namespace

void UnitDatabase::Reset() {
    DestroyUnitTable<recordvec::UnitLevelVector>(levels);
    DestroyUnitTable<recordvec::UnitModelVector>(models);
    DestroyUnitTable<recordvec::UnitNpcVector>(npcs);
    DestroyUnitTable<recordvec::UnitPrototypeVector>(prototypes);
    ConstructUnitTable(prototypes);
    ConstructUnitTable(npcs);
    ConstructUnitTable(models);
    ConstructUnitTable(levels);
}

namespace itemdb {

void SpellDatabase::Reset() {
    Destruct();   // 00684390 (database_destruct.cpp)
    Construct();
}

void ItemDatabase::Reset() {
    DestroyItemTable<recordvec::LootVector>(loot);
    DestroyItemTable<recordvec::QuestItemVector>(questItems);
    DestroyItemTable<recordvec::QuickItemVector>(quickItems);
    DestroyItemTable<recordvec::ArmorVector>(armors);
    DestroyItemTable<recordvec::WeaponVector>(weapons);
    DestroyItemTable<recordvec::MaterialVector>(materials);
    Construct();
}

} // namespace itemdb

