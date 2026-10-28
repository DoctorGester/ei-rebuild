// The resource databases' resets: the compiler-generated code that destroys a database and constructs it
// again in place, which the resource switch runs before it loads the databases of another resource folder
// (the skill database's is SkillDatabase::Reset, unit_db.cpp). Each table's members go last to first: the
// name index (hash_map::Destroy: every chain from its head, each node's key string then the node, then the
// buckets), then the records. The constructors are database_construct.cpp's.
#include "item_db.h"
#include "unit_db.h"

#include <memory>

namespace {

constexpr std::size_t kBuckets = 100;   // each index asks for 100 buckets (STLport: 193)

template <typename Table>
void DestroyUnitTable(Table& table) {
    table.byName.Destroy();
    std::destroy_at(&table.records);
}

// A unit table as the database's constructor builds it: no records, an empty index.
template <typename Table>
void ConstructUnitTable(Table& table) {
    std::construct_at(&table.records);
    table.byName.InitBuckets(kBuckets);
}

} // namespace

void UnitDatabase::Reset() {
    DestroyUnitTable(levels);
    DestroyUnitTable(models);
    DestroyUnitTable(npcs);
    DestroyUnitTable(prototypes);
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
    DestructBody();
    Construct();
}

} // namespace itemdb
