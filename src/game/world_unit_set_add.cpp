// The unit set's builder (UnitSet::Add; world.h), called for each unit by the server's SendZone
// (world_sync.cpp), and the STLport helpers it was compiled with. The set's maps are game_hashes.h's tables:
// the figured groups (hash_map<prototype, FiguredTable>), each group a hash_multimap of figured records by
// their scale key, and the dressed records, a hash_multimap by prototype. The temporaries are made, copied and
// destroyed in the game's order: it reads the heap's address order (docs/notes/standalone-verification.md).
#include "world.h"

#include "game_hashes.h"
#include "item_db.h"
#include "record_vectors.h"
#include "unit.h"

#include "lib/mfc.h"
#include "lib/mfc_slots.h"
#include "lib/stlp_hash.h"
#include "lib/stlp_vector.h"

#include <cstdint>
#include <memory>
#include <string>

namespace {

using gamehash::DressedTable;
using gamehash::FiguredGroupsTable;
using gamehash::FiguredTable;
using recordvec::DressedUnitElement;
using recordvec::FiguredUnitElement;
using IdElement = stlp::PlainElement<std::uint32_t>;
using NameElement = stlp::ObjectElement<mfc::CString>;

constexpr std::uint32_t kNoItem = 0xFFFFFFFFu;
constexpr float kScaleSteps = 127.0f;

// The figure's scale as a key: each component times 127, truncated (_ftol), 10 bits each, x highest.
std::uint32_t ScaleKey(const Vector3& scale) {
    auto steps = [](float v) { return static_cast<std::uint32_t>(static_cast<int>(v * kScaleSteps)) & 0x3FF; };
    return (steps(scale.x) << 10 | steps(scale.y)) << 10 | steps(scale.z);
}

// An item's id (its prototype index in the high word, its material in the low word) when the item
// is there and both indices are inside their tables (`known` for the prototype), else kNoItem.
template <typename Known>
std::uint32_t ItemId(const CEngineObject* item, Known known) {
    if (!item) return kNoItem;
    std::uint32_t id = item->prototypeId_;
    if (!known(id >> 16) || !itemdb::MaterialAt(id & 0xFFFF)) return kNoItem;
    return id;
}
bool KnownArmour(std::uint32_t index) { return itemdb::ArmorAt(index) != nullptr; }
bool KnownWeapon(std::uint32_t index) { return itemdb::WeaponAt(index) != nullptr; }

// Two id lists alike: the same length and the same ids.
bool SameIds(const UnitIdList& a, const UnitIdList& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return false;
    return true;
}

// The group of `prototype` (made when missing) as the game reaches it: an empty table, a pair of the key and a
// copy of it, find_or_insert of the pair (a new node copies the pair again), then the pair's table and the
// empty one destroyed.
FiguredTable* GroupFor(FiguredGroupsTable& groups, std::uint32_t prototype) {
    FiguredTable empty;
    empty.Construct(100, 0, 0, 0);
    FiguredGroupsTable::Value pair;
    pair.key = prototype;
    pair.mapped.CopyConstruct(empty);
    FiguredTable* group = &groups.FindOrInsert(pair).mapped;
    pair.mapped.Destroy();
    empty.Destroy();
    return group;
}

} // namespace

void UnitSet::Add(CUnit* unit) {
    FiguredUnitRecord record;   // its vectors empty
    std::uint32_t prototype = unit->prototypeId_ & 0xFFFF;
    const Vector3 scale = unit->scale;
    record.prototype = static_cast<std::uint16_t>(unit->prototypeId_);
    std::uint32_t scaleKey = ScaleKey(scale);
    record.morphS = scale.x;
    record.morphT = scale.y;
    record.morphU = scale.z;

    if (unit->type_ == kUnitTypeHero) {
        for (int slot = 0; slot < 7; ++slot)
            record.items.push_back(ItemId(unit->WornItem(slot), KnownArmour));
        for (std::uint32_t slot = 0; slot < 4; ++slot)
            record.extraWeapons.push_back(ItemId(unit->GetWeapon(slot), KnownWeapon));
        record.extraWeapon = static_cast<std::int8_t>(unit->activeWeapon);
    } else {
        record.extraWeapon = -1;
    }

    // The figured records: one of the same scale key and ids in the prototype's group is marked
    // (modelNumber -1); else the record goes into the group.
    FiguredTable* group = GroupFor(figured, prototype);
    FiguredTable::Node* first;
    FiguredTable::Node* last;
    group->EqualRange(scaleKey, first, last);
    bool marked = false;
    for (FiguredTable::Node* node = first; node != last; node = group->Next(node)) {
        FiguredUnitRecord& other = node->value.mapped;
        if (SameIds(other.items, record.items) && SameIds(other.extraWeapons, record.extraWeapons)) {
            other.modelNumber = static_cast<int>(kNoItem);
            marked = true;
            break;
        }
    }
    if (!marked) {
        if (unit->type_ != kUnitTypeHero) {
            // The texts the server companion holds (CMapObjectServer's string array).
            const CMapObjectServer* server = unit->ServerCompanion();
            for (const std::string& name : server->strings) {
                record.names.push_back(name);
            }
        }
        record.modelNumber = static_cast<int>(unit->id_);
        FiguredTable::Value entry;
        entry.key = scaleKey;
        FiguredUnitElement::Construct(&entry.mapped, record);
        group = GroupFor(figured, prototype);
        group->Resize(group->Count() + 1);
        group->InsertEqualNoresize(entry);
        FiguredUnitElement::Destroy(&entry.mapped);
    }

    // The dressed records: one of the same prototype and ids is marked, else a copy of the
    // record's head is added.
    DressedTable& dressedRecords = dressed;
    DressedTable::Node* from;
    DressedTable::Node* to;
    dressedRecords.EqualRange(prototype, from, to);
    for (DressedTable::Node* node = from; node != to; node = dressedRecords.Next(node)) {
        DressedUnitRecord& other = node->value.mapped;
        if (SameIds(other.items, record.items) && SameIds(other.extraWeapons, record.extraWeapons)) {
            other.modelNumber = static_cast<int>(kNoItem);
            FiguredUnitElement::Destroy(&record);
            return;
        }
    }
    record.modelNumber = static_cast<std::uint32_t>(unit->id_);
    DressedTable::Value entry;
    entry.key = prototype;
    DressedUnitElement::Construct(&entry.mapped, record);
    dressedRecords.Resize(dressedRecords.Count() + 1);
    dressedRecords.InsertEqualNoresize(entry);
    DressedUnitElement::Destroy(&entry.mapped);                      // called twice
    FiguredUnitElement::Destroy(&record);
}

// ---- The helpers UnitSet::Add was compiled with ---------------------------------------------------

namespace unitset {

void __fastcall DestructName(mfc::CString* name) { std::destroy_at(name); }

} // namespace unitset
