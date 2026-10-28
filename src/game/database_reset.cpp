// The resource databases' resets: the compiler-generated code that destroys a database and constructs it
// again in place, which the resource switch runs before it loads the databases of another resource folder
// (the skill database's is SkillDatabase::Reset, unit_db.cpp). Each table's members go last to first: the
// name index, then the records through their vector's element policy (record_vectors.h: each record in order,
// then the storage). The constructors are database_construct.cpp's.
#include "item_db.h"
#include "record_vectors.h"
#include "unit_db.h"

namespace {

template <typename R>
stlp::vector<R>& RecordsOf(void* table) {
    return *static_cast<stlp::vector<R>*>(table);
}

// ~vector<R>() as the element policy `Abi` destroys it.
template <typename Abi>
void DestroyRecords(void* table) {
    Abi::Destruct(&RecordsOf<typename Abi::Vector::value_type>(table));
}

template <typename Abi, typename Table>
void DestroyUnitTable(Table& table) {
    std::destroy_at(&table.byName);
    DestroyRecords<Abi>(&table);
}

// A unit table as the database's constructor builds it: no records, an empty index.
template <typename Table>
void ConstructUnitTable(Table& table) {
    table.begin = nullptr;
    table.end = nullptr;
    table.endOfStorage = nullptr;
    std::construct_at(&table.byName);
}

template <typename Abi, typename Record>
void DestroyItemTable(itemdb::NamedRecordTable<Record>& table) {
    std::destroy_at(&table.names);
    Abi::Destruct(&table.records);
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
    Destruct();
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
