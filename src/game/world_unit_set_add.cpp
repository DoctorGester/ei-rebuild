// The unit set's builder (005593D0, UnitSet::Add; world.h), called for each unit by the server's
// SendZone (world_sync.cpp), and the STLport helpers it was compiled with (S16a). The set's maps are
// game_hashes.h's tables: the figured groups (hash_map<prototype, FiguredTable>), each group a
// hash_multimap of figured records by their scale key, and the dressed records, a hash_multimap by
// prototype. The temporaries are made, copied and destroyed in the original's order: the game reads
// the heap's address order (docs/notes/standalone-verification.md, SV2f).
#include "world.h"

#include "game_hashes.h"
#include "item_db.h"
#include "record_vectors.h"
#include "unit.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/stlp_hash.h"
#include "lib/stlp_vector.h"

#include <cstdint>

namespace {

using gamehash::DressedTable;
using gamehash::FiguredGroupsTable;
using gamehash::FiguredTable;
using recordvec::DressedUnitElement;
using recordvec::FiguredUnitElement;
using IdElement = stlp::PlainElement<std::uint32_t>;
using NameElement = stlp::ObjectElement<mfc::CString>;

constexpr std::uint32_t kNoItem = 0xFFFFFFFFu;
constexpr float kScaleSteps = 127.0f;   // 0073D06C

static_assert(sizeof(FiguredGroupsTable) == sizeof(UnitSet::figured));
static_assert(sizeof(DressedTable) == sizeof(UnitSet::dressed));
// A figured record starts with a dressed record's fields (the dressed copy reads that head).
static_assert(offsetof(FiguredUnitRecord, armourIds) == offsetof(DressedUnitRecord, items));
static_assert(offsetof(FiguredUnitRecord, weaponIds) == offsetof(DressedUnitRecord, extraWeapons));
static_assert(offsetof(FiguredUnitRecord, extraWeapon) == offsetof(DressedUnitRecord, extraWeapon));
static_assert(offsetof(FiguredUnitRecord, modelNumber) == offsetof(DressedUnitRecord, modelNumber));

FiguredGroupsTable& GroupsOf(UnitSet& set) { return reinterpret_cast<FiguredGroupsTable&>(set.figured); }
DressedTable& DressedOf(UnitSet& set) { return reinterpret_cast<DressedTable&>(set.dressed); }
const DressedUnitRecord& HeadOf(const FiguredUnitRecord& record) { return reinterpret_cast<const DressedUnitRecord&>(record); }

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

// The group of `prototype` (made when missing) as the original reaches it: an empty table
// (hash_map(), 0055AC40), a pair of the key and a copy of it (0055ABB0 / 0055AEB0), find_or_insert
// of the pair (0055BA10: a new node copies the pair again), then the pair's table and the empty one
// destroyed (0055AD10).
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
    FiguredUnitRecord record;   // its vectors empty (00559DB0, 0055A3E0)
    std::uint32_t prototype = unit->prototypeId_ & 0xFFFF;
    const Vector3 scale = unit->scale;
    record.prototype = static_cast<std::uint16_t>(unit->prototypeId_);
    std::uint32_t scaleKey = ScaleKey(scale);
    record.morphS = scale.x;
    record.morphT = scale.y;
    record.morphU = scale.z;

    if (unit->type_ == kUnitTypeHero) {
        for (int slot = 0; slot < 7; ++slot)
            record.armourIds.PushBack<IdElement>(ItemId(unit->WornItem(slot), KnownArmour));
        for (std::uint32_t slot = 0; slot < 4; ++slot)
            record.weaponIds.PushBack<IdElement>(ItemId(unit->GetWeapon(slot), KnownWeapon));
        record.extraWeapon = static_cast<std::uint8_t>(unit->activeWeapon);
    } else {
        record.extraWeapon = 0xFF;
    }

    // The figured records: one of the same scale key and ids in the prototype's group is marked
    // (modelNumber -1); else the record goes into the group.
    FiguredTable* group = GroupFor(GroupsOf(*this), prototype);
    FiguredTable::Node* first;
    FiguredTable::Node* last;
    group->EqualRange(scaleKey, first, last);
    bool marked = false;
    for (FiguredTable::Node* node = first; node != last; node = group->Next(node)) {
        FiguredUnitRecord& other = node->value.mapped;
        if (SameIds(other.armourIds, record.armourIds) && SameIds(other.weaponIds, record.weaponIds)) {
            other.modelNumber = kNoItem;
            marked = true;
            break;
        }
    }
    if (!marked) {
        if (unit->type_ != kUnitTypeHero) {
            // The texts the server companion holds (CMapObjectServer's string array).
            const CMapObjectServer* server = static_cast<const CMapObjectServer*>(unit->companion);
            for (int i = 0; i < server->strings.size; ++i) {
                mfc::CString name(static_cast<const char*>(server->strings[i]));
                FiguredUnitElement::NamesOf(record).PushBack<NameElement>(name);
            }
        }
        record.modelNumber = static_cast<std::uint32_t>(unit->id_);
        FiguredTable::Value entry;
        entry.key = scaleKey;
        FiguredUnitElement::Construct(&entry.mapped, record);    // 0055A860
        group = GroupFor(GroupsOf(*this), prototype);
        group->Resize(group->Count() + 1);                       // 0055B670
        group->InsertEqualNoresize(entry);                       // 0055B540
        FiguredUnitElement::Destroy(&entry.mapped);              // 0053F020, 00559DE0
    }

    // The dressed records: one of the same prototype and ids is marked, else a copy of the
    // record's head is added.
    DressedTable& dressedRecords = DressedOf(*this);
    DressedTable::Node* from;
    DressedTable::Node* to;
    dressedRecords.EqualRange(prototype, from, to);
    for (DressedTable::Node* node = from; node != to; node = dressedRecords.Next(node)) {
        DressedUnitRecord& other = node->value.mapped;
        if (SameIds(other.items, record.armourIds) && SameIds(other.extraWeapons, record.weaponIds)) {
            other.modelNumber = static_cast<int>(kNoItem);
            FiguredUnitElement::Destroy(&record);
            return;
        }
    }
    record.modelNumber = static_cast<std::uint32_t>(unit->id_);
    DressedTable::Value entry;
    entry.key = prototype;
    DressedUnitElement::Construct(&entry.mapped, HeadOf(record));   // 0055A9F0
    dressedRecords.Resize(dressedRecords.Count() + 1);                             // 0055BBF0
    dressedRecords.InsertEqualNoresize(entry);                              // 0055BAC0
    DressedUnitElement::Destroy(&entry.mapped);                      // 004FE420 twice
    FiguredUnitElement::Destroy(&record);
}
EI_REWRITTEN(0x005593D0, UnitSet::Add);

// ---- The helpers 005593D0 was compiled with -------------------------------------------------------

namespace unitset {

DressedUnitRecord* __fastcall ConstructHead(DressedUnitRecord* self) {
    self->items.ConstructEmpty();
    self->extraWeapons.ConstructEmpty();
    return self;
}

void __fastcall DestroyHead(DressedUnitRecord* self) { DressedUnitElement::Destroy(self); }

} // namespace unitset

FiguredUnitRecord* FiguredUnitRecord::CopyConstruct(const FiguredUnitRecord* from) {
    FiguredUnitElement::Construct(this, *from);
    return this;
}

DressedUnitRecord* DressedUnitRecord::CopyConstruct(const DressedUnitRecord* from) {
    DressedUnitElement::Construct(this, *from);
    return this;
}

namespace unitset {

bool __fastcall EqualIds(const std::uint32_t* first, const std::uint32_t* last, const std::uint32_t* other) {
    for (; first != last; ++first, ++other)
        if (*first != *other) return false;
    return true;
}

mfc::CString* __fastcall DeleteName(mfc::CString* self, void*, unsigned flags) {
    self->~CString();
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

} // namespace unitset

// The records: the head's constructor and destructor, the copies, equal() of two id ranges.
EI_REWRITTEN(0x00559DE0, unitset::DestroyHead);
EI_REWRITTEN(0x0055A860, FiguredUnitRecord::CopyConstruct);

// The tables: a group's pair, hash_map(), its copy and destructor; the groups' find_or_insert and
// resize; the multimaps' equal_range, resize, insert_equal_noresize and the iterator's step.
EI_REWRITTEN(0x0055ABB0, gamehash::ConstructGroupPair);

// The id and name vectors: _M_insert_overflow, ~_Vector_base (2), _Construct of an id and of a
// CString, and the CString's scalar deleting destructor (the names' element loop calls it with 0).
EI_REWRITTEN(0x0051FAD0, unitset::DeleteName);
