#pragma once

// Element policies (lib/stlp_vector.h) of the game's vector<T> instantiations whose elements are
// game records with their own copy constructor, operator= and destructor (S10a), and the
// VectorAbi types record_vectors.cpp registers at the instantiations' addresses
// (tests/verify/verify_record_vectors.cpp checks them). docs/notes/standalone.md, S10a.

#include "item_db.h"
#include "quest_records.h"

#include "lib/stlp_vector.h"

namespace recordvec {

// The item records' destructors (item_db_records.cpp): the head's members, plus the quick item's
// string and the quest item's zone list.
inline void DestroyItem(itemdb::ItemRecordHead* record) { record->DestructHead(); }
inline void DestroyItem(itemdb::QuickItemRecord* record) { record->Destruct(); }
inline void DestroyItem(itemdb::QuestItemRecord* record) { record->Destruct(); }

// The weapon, armour, quick item, quest item and loot records: the kind's copy constructor, its
// operator= (the head's Assign, then its own fields), its destructor; resize(n) appends copies of
// the kind's default record.
template <typename R>
struct ItemElement {
    static void Construct(R* to, const R& from) { itemdb::CopyConstructRecord(to, &from); }
    static void Assign(R& to, const R& from) {
        to.Assign(&from);
        to.CopyOwn(&from);
    }
    static void Destroy(R* p) { DestroyItem(p); }
    static void Default(R* p) { itemdb::DefaultConstructRecord(p); }
};

// A record whose copy constructor, operator=, destructor and resize(n) temporary are its members.
template <typename R, auto CopyFn, auto AssignFn, auto DestroyFn, auto DefaultFn>
struct MemberElement {
    static void Construct(R* to, const R& from) { (to->*CopyFn)(&from); }
    static void Assign(R& to, const R& from) { (to.*AssignFn)(&from); }
    static void Destroy(R* p) { (p->*DestroyFn)(); }
    static void Default(R* p) { (p->*DefaultFn)(); }
};
template <typename R, auto AssignFn>
using RecordElement = MemberElement<R, &R::CopyConstruct, AssignFn, &R::Destruct, &R::ConstructDefault>;

using MaterialVector = stlp::VectorAbi<itemdb::MaterialRecordHead,
                                       RecordElement<itemdb::MaterialRecordHead, &itemdb::MaterialRecordHead::AssignMaterial>>;
using WeaponVector = stlp::VectorAbi<itemdb::WeaponRecord, ItemElement<itemdb::WeaponRecord>>;
using ArmorVector = stlp::VectorAbi<itemdb::ArmorRecord, ItemElement<itemdb::ArmorRecord>>;
using QuickItemVector = stlp::VectorAbi<itemdb::QuickItemRecord, ItemElement<itemdb::QuickItemRecord>>;
using QuestItemVector = stlp::VectorAbi<itemdb::QuestItemRecord, ItemElement<itemdb::QuestItemRecord>>;
using LootVector = stlp::VectorAbi<itemdb::LootItemRecord, ItemElement<itemdb::LootItemRecord>>;

using SpellVector = stlp::VectorAbi<itemdb::SpellRecord, RecordElement<itemdb::SpellRecord, &itemdb::SpellRecord::AssignSpell>>;
using SpellModifierVector =
    stlp::VectorAbi<itemdb::SpellModifierRecord,
                    RecordElement<itemdb::SpellModifierRecord, &itemdb::SpellModifierRecord::AssignModifier>>;
using SpellListVector =
    stlp::VectorAbi<itemdb::SpellListRecord, RecordElement<itemdb::SpellListRecord, &itemdb::SpellListRecord::AssignList>>;

using QuestVector = stlp::VectorAbi<QuestRecord, RecordElement<QuestRecord, &QuestRecord::AssignQuest>>;
using BriefingVector =
    stlp::VectorAbi<QuestBriefingRecord, RecordElement<QuestBriefingRecord, &QuestBriefingRecord::AssignBriefing>>;

} // namespace recordvec
