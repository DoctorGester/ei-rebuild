#pragma once

// Element policies (lib/stlp_vector.h) of the game's vector<T> instantiations whose elements are
// game records with their own copy constructor, operator= and destructor (S10a), and the
// VectorAbi types record_vectors.cpp registers at the instantiations' addresses
// (tests/verify/verify_record_vectors.cpp checks them). docs/notes/standalone.md, S10a.

#include "acks_db.h"
#include "engine_object.h"
#include "item_db.h"
#include "lever.h"
#include "model.h"
#include "net_classes.h"
#include "party_manager.h"
#include "quest_records.h"
#include "record_lifetime.h"
#include "record_members.h"
#include "ui.h"
#include "ui_camp.h"
#include "ui_screens.h"
#include "unit_db.h"
#include "unit_looks.h"

#include "ei/vcall.h"
#include "lib/stlp_hash.h"
#include "lib/stlp_vector.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <new>
#include <type_traits>

// A unit model's body slot as its record's copies copy it, found by argument-dependent lookup from
// records::Memberwise (the slot constructor 0053F000 leaves the level name empty).
using UnitModelSlotMembers = records::Memberwise<UnitModelSlot, &UnitModelSlot::levelName, &UnitModelSlot::levelIndex,
                                                 &UnitModelSlot::share, &UnitModelSlot::factor>;
inline void CopyMember(UnitModelSlot& to, const UnitModelSlot& from) { UnitModelSlotMembers::Construct(&to, from); }
inline void AssignMember(UnitModelSlot& to, const UnitModelSlot& from) { UnitModelSlotMembers::Assign(to, from); }
inline void DefaultMember(UnitModelSlot& to) { UnitModelSlotMembers::Default(&to); }

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

// ---- The unit and skill databases (unit_db.h) ---------------------------------------------------

// 0053E2A0's copies (e.g. 005400D0): every member, the bytes +0x38..+0x47 too.
struct UnitPrototypeElement
    : records::Memberwise<UnitPrototypeRecord, &UnitPrototypeRecord::name, &UnitPrototypeRecord::modelName,
                          &UnitPrototypeRecord::modelIndex, &UnitPrototypeRecord::skin, &UnitPrototypeRecord::hair,
                          &UnitPrototypeRecord::scale, &UnitPrototypeRecord::panelViewSize, &UnitPrototypeRecord::height,
                          &UnitPrototypeRecord::viewDistance, &UnitPrototypeRecord::viewHeight, &UnitPrototypeRecord::shake,
                          &UnitPrototypeRecord::soundFolder, &UnitPrototypeRecord::unused_38, &UnitPrototypeRecord::hitPoints,
                          &UnitPrototypeRecord::mana, &UnitPrototypeRecord::level, &UnitPrototypeRecord::experienceValue,
                          &UnitPrototypeRecord::protectionScale, &UnitPrototypeRecord::field12, &UnitPrototypeRecord::speed,
                          &UnitPrototypeRecord::combat, &UnitPrototypeRecord::naturalWeapon, &UnitPrototypeRecord::hitSoundKind,
                          &UnitPrototypeRecord::damage, &UnitPrototypeRecord::damageSpread, &UnitPrototypeRecord::skillLevel,
                          &UnitPrototypeRecord::attributes01, &UnitPrototypeRecord::attribute2,
                          &UnitPrototypeRecord::peripheralRange, &UnitPrototypeRecord::valuesB0, &UnitPrototypeRecord::valuesE0,
                          &UnitPrototypeRecord::gold, &UnitPrototypeRecord::secondGold, &UnitPrototypeRecord::lootItems,
                          &UnitPrototypeRecord::skills3to5, &UnitPrototypeRecord::spellNames, &UnitPrototypeRecord::armourNames,
                          &UnitPrototypeRecord::weaponName, &UnitPrototypeRecord::secondWeaponName> {
    static void Destroy(UnitPrototypeRecord* p) { unitdb::DestroyPrototypeRecord(p); }
};

// 0053E520's copies (e.g. 00540320): +0x23 and +0x5D..+0x5F are not copied.
struct UnitNpcElement
    : records::Memberwise<UnitNpcRecord, &UnitNpcRecord::name, &UnitNpcRecord::field1, &UnitNpcRecord::experience,
                          &UnitNpcRecord::stats, &UnitNpcRecord::attributes, &UnitNpcRecord::skills, &UnitNpcRecord::skillNames,
                          &UnitNpcRecord::startWeapons, &UnitNpcRecord::startQuickItems, &UnitNpcRecord::startSpells,
                          &UnitNpcRecord::experienceFloor, &UnitNpcRecord::startMoney, &UnitNpcRecord::voiced> {
    static void Destroy(UnitNpcRecord* p) { unitdb::DestroyNpcRecord(p); }
};

// 0053E730's copies (e.g. 00540510): the members up to the sound names, the sound counts (a
// vector<int> declared as its three pointers), the rest; +0x11D..+0x11F are not copied.
struct UnitModelElement {
    using R = UnitModelRecord;
    using Head = records::Memberwise<
        R, &R::name, &R::objectClass, &R::hitPointRegen, &R::manaRegen, &R::field4, &R::movementKind, &R::angle,
        &R::modeSpeeds, &R::actionSpeeds, &R::field8, &R::sizeClass, &R::sizeClassSmall, &R::damageWeights, &R::protection,
        &R::slots, &R::figureName, &R::field1F, &R::field20, &R::height, &R::bloodKind, &R::footprintKind, &R::footPart,
        &R::castEffect, &R::hitMaterial, &R::swapFeet, &R::sizeScale, &R::soundFolder, &R::unused_128, &R::soundNames>;
    using Tail = records::Memberwise<R, &R::idleSoundChance, &R::attackSoundChance, &R::eyeLeft, &R::eyeBottom,
                                     &R::eyeWidth, &R::eyeHeight>;
    using Counts = stlp::vector<std::uint32_t>;
    static Counts& CountsOf(R& r) { return *reinterpret_cast<Counts*>(&r.soundCounts); }
    static const Counts& CountsOf(const R& r) { return *reinterpret_cast<const Counts*>(&r.soundCounts); }

    static void Construct(R* to, const R& from) {
        Head::Construct(to, from);
        records::CopyMember(CountsOf(*to), CountsOf(from));
        Tail::Construct(to, from);
    }
    static void Assign(R& to, const R& from) {
        Head::Assign(to, from);
        records::AssignMember(CountsOf(to), CountsOf(from));
        Tail::Assign(to, from);
    }
    static void Default(R* p) {
        Head::Default(p);
        records::DefaultMember(CountsOf(*p));
    }
    static void Destroy(R* p) { unitdb::DestroyModelRecord(p); }
};

// 0053E9B0's copies (e.g. 005427B0); the destructor (00540840 with 0) is the name's.
struct UnitLevelElement : records::Memberwise<UnitLevelRecord, &UnitLevelRecord::name, &UnitLevelRecord::slotType,
                                              &UnitLevelRecord::valuesA, &UnitLevelRecord::valuesB,
                                              &UnitLevelRecord::vitalMask> {
    static void Destroy(UnitLevelRecord* p) { p->name.~CString(); }
};

// The skill database: 005209D0's copies (00521C70) and 00520B20's (00521E20; +0x41..+0x43 are not
// copied); the destructors are S13's.
struct SkillElement : records::Memberwise<SkillRecord, &SkillRecord::name, &SkillRecord::field1, &SkillRecord::field2,
                                          &SkillRecord::statNames> {
    static void Destroy(SkillRecord* p) { p->Destruct(); }
};
struct SkillLevelElement
    : records::Memberwise<SkillLevelRecord, &SkillLevelRecord::field0, &SkillLevelRecord::name, &SkillLevelRecord::field2,
                          &SkillLevelRecord::requiredLevelName, &SkillLevelRecord::exclusionGroup,
                          &SkillLevelRecord::requiredSkillName, &SkillLevelRecord::values, &SkillLevelRecord::learnable> {
    static void Destroy(SkillLevelRecord* p) { p->Destruct(); }
};

using UnitPrototypeVector = stlp::VectorAbi<UnitPrototypeRecord, UnitPrototypeElement>;
using UnitNpcVector = stlp::VectorAbi<UnitNpcRecord, UnitNpcElement>;
using UnitModelVector = stlp::VectorAbi<UnitModelRecord, UnitModelElement>;
using UnitLevelVector = stlp::VectorAbi<UnitLevelRecord, UnitLevelElement>;
using SkillVector = stlp::VectorAbi<SkillRecord, SkillElement>;
using SkillLevelVector = stlp::VectorAbi<SkillLevelRecord, SkillLevelElement>;

// ---- A zone's unit looks (unit_looks.h; world_unit_set.cpp's 0055A400 / 0055A580) --------------

// The figured units' records (0055D5A0's copies; +0x02 and +0x1D..+0x1F are not copied): the head,
// then the texture names, CStrings declared as their data pointers. The destructor (00559B30):
// the names and their storage, then the weapon and armour ids' storage.
struct FiguredUnitElement {
    using R = FiguredUnitRecord;
    using Head = records::Memberwise<R, &R::prototype, &R::armourIds, &R::weaponIds, &R::extraWeapon, &R::modelNumber,
                                     &R::morphS, &R::morphT, &R::morphU>;
    static CStringVector& NamesOf(R& r) { return *reinterpret_cast<CStringVector*>(&r.names); }
    static const CStringVector& NamesOf(const R& r) { return *reinterpret_cast<const CStringVector*>(&r.names); }

    static void Construct(R* to, const R& from) {
        Head::Construct(to, from);
        records::CopyMember(NamesOf(*to), NamesOf(from));
    }
    static void Assign(R& to, const R& from) {
        Head::Assign(to, from);
        records::AssignMember(NamesOf(to), NamesOf(from));
    }
    static void Default(R* p) {
        Head::Default(p);
        records::DefaultMember(NamesOf(*p));
    }
    static void Destroy(R* p) {
        CStringVector& names = NamesOf(*p);
        for (mfc::CString* name = names.Start(); name != names.Finish(); ++name) name->~CString();
        names.FreeStorage();
        p->weaponIds.FreeStorage();
        p->armourIds.FreeStorage();
    }
};

// The dressed units' records (0055D7F0's copies); the destructor (00559DE0): both id lists'
// storage, the last first.
struct DressedUnitElement : records::Memberwise<DressedUnitRecord, &DressedUnitRecord::prototype, &DressedUnitRecord::items,
                                                &DressedUnitRecord::extraWeapons, &DressedUnitRecord::extraWeapon,
                                                &DressedUnitRecord::modelNumber> {
    static void Destroy(DressedUnitRecord* p) {
        p->extraWeapons.FreeStorage();
        p->items.FreeStorage();
    }
};

using FiguredUnitVector = stlp::VectorAbi<FiguredUnitRecord, FiguredUnitElement>;
using DressedUnitVector = stlp::VectorAbi<DressedUnitRecord, DressedUnitElement>;

// ---- prints.db (model.h; model_db.cpp's 006D26E0 / 006D2830) ------------------------------------

// The prints' records: the copies and the defaults set the record's vtable; the name, then the
// value triples (copied as they are, zero by default). The destructor (006D29A0 / 006D29E0) only
// returns the name's storage; the vtable stays.
template <typename R>
struct PrintElement {
    static void Construct(R* to, const R& from) {
        ei::SetVtable(to, R::kVtableAddress);
        new (&to->name) stlp::string(from.name);
        std::memcpy(Values(*to), Values(from), sizeof(R) - offsetof(R, values2));
    }
    static void Assign(R& to, const R& from) {
        to.name.AssignFrom(from.name);
        std::memcpy(Values(to), Values(from), sizeof(R) - offsetof(R, values2));
    }
    static void Default(R* p) {
        stlp::StringElement::Default(&p->name);
        std::memset(Values(*p), 0, sizeof(R) - offsetof(R, values2));
        ei::SetVtable(p, R::kVtableAddress);
    }
    static void Destroy(R* p) { p->name.~string(); }

private:
    static void* Values(R& r) { return r.values2; }
    static const void* Values(const R& r) { return r.values2; }
};

using PrintVector = stlp::VectorAbi<PrintRecord, PrintElement<PrintRecord>>;
using Print2Vector = stlp::VectorAbi<PrintRecord2, PrintElement<PrintRecord2>>;

// ---- acks.db's speech lists (acks_db.h; acks_db.cpp's 006E4560 / 006E47F0) ----------------------

// A speech: memberwise (the chance left as it is by default); the destructor (006E3330) the
// strings from the last.
struct SpeechElement : records::Memberwise<SpeechRecord, &SpeechRecord::chance, &SpeechRecord::sound,
                                           &SpeechRecord::text, &SpeechRecord::name> {
    static void Destroy(SpeechRecord* p) { p->~SpeechRecord(); }
};
// The speech, then the environment flags.
struct EnvironmentSpeechElement {
    using R = acks::EnvironmentSpeech;
    static void Construct(R* to, const R& from) {
        SpeechElement::Construct(&to->speech, from.speech);
        to->environment = from.environment;
    }
    static void Assign(R& to, const R& from) {
        SpeechElement::Assign(to.speech, from.speech);
        to.environment = from.environment;
    }
    static void Default(R* p) { SpeechElement::Default(&p->speech); }
    static void Destroy(R* p) { SpeechElement::Destroy(&p->speech); }
};

using SpeechVector = stlp::VectorAbi<SpeechRecord, SpeechElement>;
using EnvironmentSpeechVector = stlp::VectorAbi<acks::EnvironmentSpeech, EnvironmentSpeechElement>;

} // namespace recordvec

// The speech lists' elements as members of the acks records (records::CopyMember of their vectors).
template <>
struct records::ElementOf<SpeechRecord> {
    using type = recordvec::SpeechElement;
};
template <>
struct records::ElementOf<acks::EnvironmentSpeech> {
    using type = recordvec::EnvironmentSpeechElement;
};

namespace acks {

// The records' head as their compiler-written members copy it: the name, then the map by ack kind
// (copy constructor 006DECC0, operator= 006E14E0: STLport's, over _M_copy_from 006E00F0; the
// copy's map still points into the source record's lists, as the original's does). By default an
// empty name with room for 8 bytes and an empty map of 193 buckets (006DCD90; its functor bytes
// are an uninitialised stack byte in the original, 0 here).
using SpeechTable = stlp::hashtable<stlp::DwordKeyTraits<8>>;
inline SpeechTable& SpeechTableOf(AckRecordHead& head) { return *reinterpret_cast<SpeechTable*>(&head.speeches); }
inline const SpeechTable& SpeechTableOf(const AckRecordHead& head) {
    return *reinterpret_cast<const SpeechTable*>(&head.speeches);
}
static_assert(sizeof(SpeechTable) == sizeof(AckRecordHead::speeches));
inline void CopyMember(AckRecordHead& to, const AckRecordHead& from) {
    records::CopyMember(to.name, from.name);
    SpeechTableOf(to).CopyConstruct(SpeechTableOf(from));
}
inline void AssignMember(AckRecordHead& to, const AckRecordHead& from) {
    records::AssignMember(to.name, from.name);
    SpeechTableOf(to).Assign(SpeechTableOf(from));
}
inline void DefaultMember(AckRecordHead& to) {
    records::DefaultMember(to.name);
    SpeechTableOf(to).Construct(100, 0, 0, 0);
}

} // namespace acks

namespace recordvec {

// ---- acks.db's table records (acks_db.h; 006DB9C0 / 006DBBD0 / 006DBE20, loaded at startup) ----

// The compiler-written members (006DE370 / 006DC000 and the copy loops' inlined operator=): the
// head, then the speech lists in order; the destructors are the S13 ports (006DC390, 006DC920,
// 006DCAF0).
template <typename R>
struct AcksElement {
    static constexpr bool kUnit = std::is_same_v<R, acks::UnitAcks>;
    static void Construct(R* to, const R& from) {
        acks::CopyMember(to->head, from.head);
        records::CopyMember(to->lists, from.lists);
        if constexpr (kUnit) {
            records::CopyMember(to->environmentSpeeches, from.environmentSpeeches);
            records::CopyMember(to->namedSpeeches, from.namedSpeeches);
        }
    }
    static void Assign(R& to, const R& from) {
        acks::AssignMember(to.head, from.head);
        records::AssignMember(to.lists, from.lists);
        if constexpr (kUnit) {
            records::AssignMember(to.environmentSpeeches, from.environmentSpeeches);
            records::AssignMember(to.namedSpeeches, from.namedSpeeches);
        }
    }
    static void Default(R* p) {
        acks::DefaultMember(p->head);
        records::DefaultMember(p->lists);
        if constexpr (kUnit) {
            records::DefaultMember(p->environmentSpeeches);
            records::DefaultMember(p->namedSpeeches);
        }
    }
    static void Destroy(R* p) { p->Destruct(); }
};
using UnitAcksElement = AcksElement<acks::UnitAcks>;
using ModelAcksElement = AcksElement<acks::ModelAcks>;
using ExtraAcksElement = AcksElement<acks::ExtraAcks>;

using UnitAcksVector = stlp::VectorAbi<acks::UnitAcks, UnitAcksElement>;
using ModelAcksVector = stlp::VectorAbi<acks::ModelAcks, ModelAcksElement>;
using ExtraAcksVector = stlp::VectorAbi<acks::ExtraAcks, ExtraAcksElement>;

// ---- The client's lobby queries (net_classes.h; net_lobby.cpp's 004439C0 00443BF0 004442B0 00444090)

// A query: the copy constructor 00444270, the copy loop's assignment (the address, then the
// reply buffer's 004DAA80), the default of 00443770 (a zero address, an empty growing buffer) and
// the destructor (the reply buffer's 004DA910).
struct QueryElement {
    using R = net::Query;
    static void Construct(R* to, const R& from) { to->QueryCopy(&from); }
    static void Assign(R& to, const R& from) {
        to.address = from.address;
        to.reply.AssignFrom(&from.reply);
    }
    static void Default(R* p) {
        std::memset(&p->address, 0, sizeof(p->address));
        p->reply.ConstructGrowing();
    }
    static void Destroy(R* p) { p->reply.DestructGrowing(); }
};

using QueryVector = stlp::VectorAbi<net::Query, QueryElement>;

// ---- The party manager's entries (party_manager.h; party_manager.cpp's 004ADAA0) ---------------

// An entry: memberwise (empty texts and ids by default); the destructor 004AE2D0.
struct PartyEntryElement : records::Memberwise<PartyManagerEntry, &PartyManagerEntry::name, &PartyManagerEntry::text,
                                               &PartyManagerEntry::ids> {
    static void Destroy(PartyManagerEntry* p) { p->Destruct(); }
};

using PartyEntryVector = stlp::VectorAbi<PartyManagerEntry, PartyEntryElement>;

// ---- The lever table's records (lever.h; app_device_list.cpp's 00465180) ------------------------

// A lever record: memberwise; resize(n)'s default has empty texts and no spots (the other fields
// left as they are); the destructor 004652A0 (the texts from the last).
struct LeverElement : records::Memberwise<LeverRecord, &LeverRecord::name, &LeverRecord::spots,
                                          &LeverRecord::spotCount, &LeverRecord::farScale,
                                          &LeverRecord::duration, &LeverRecord::text20,
                                          &LeverRecord::sound, &LeverRecord::text28> {
    static void Default(LeverRecord* p) {
        Memberwise::Default(p);
        p->spotCount = 0;
    }
    static void Destroy(LeverRecord* p) { p->~LeverRecord(); }
};

using LeverVector = stlp::VectorAbi<LeverRecord, LeverElement>;

// ---- The session's replicated messages (net_classes.h; net_session_base.cpp's 0041F790 / 00426B10)

// {message, priority}: plain bytes; push_back(T()) builds nothing (the constructor 00429030 is
// empty), the caller stores into the new element.
struct ReplicatedElement : stlp::PlainElement<net::Replicated> {
    static void Default(net::Replicated*) {}
};

using ReplicatedVector = stlp::VectorAbi<net::Replicated, ReplicatedElement>;

// ---- The client party's engine objects (net_client_party.cpp's 00428A20) -------------------------

// Counted engine object pointers: a copy adds a reference (004298F0), the destructor releases a
// held object through its slot 4 (CEngineObject::Release). Only _M_insert_overflow is out of line.
struct EngineObjectRefElement {
    static void Construct(CEngineObject** to, CEngineObject* const& from) {
        *to = from;
        if (from) ++from->refCount_;
    }
    static void Destroy(CEngineObject** p) {
        if (*p) (*p)->Release();
    }
};

using EngineObjectRefVector = stlp::VectorAbi<CEngineObject*, EngineObjectRefElement>;

// ---- The HUD's portrait slots (ui.h; ui_hud_portrait_setup.cpp's 0064AB20 / 0064AD40) -----------

// A slot: member by member (the padding after hasPlayer_ is not copied), the three texts as
// CStrings; push_back(T())'s default has identity orientations and empty texts (the other fields
// left as they are); the destructor 0064ACD0 the texts from the last.
struct HudPortraitSlotElement {
    using R = HudPortraitSlot;
    static void Construct(R* to, const R& from) {
        Plain(*to, from);
        new (&records::AsCString(to->faceName_)) mfc::CString(records::AsCString(from.faceName_));
        new (&records::AsCString(to->modelPrefix_)) mfc::CString(records::AsCString(from.modelPrefix_));
        new (&records::AsCString(to->modelSuffix_)) mfc::CString(records::AsCString(from.modelSuffix_));
    }
    static void Assign(R& to, const R& from) {
        Plain(to, from);
        records::AsCString(to.faceName_) = records::AsCString(from.faceName_);
        records::AsCString(to.modelPrefix_) = records::AsCString(from.modelPrefix_);
        records::AsCString(to.modelSuffix_) = records::AsCString(from.modelSuffix_);
    }
    static void Default(R* p) {
        p->restA_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
        p->current_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
        p->restB_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
        new (&records::AsCString(p->faceName_)) mfc::CString();
        new (&records::AsCString(p->modelPrefix_)) mfc::CString();
        new (&records::AsCString(p->modelSuffix_)) mfc::CString();
        p->wobbleTarget_.SetIdentity();
    }
    static void Destroy(R* p) {
        records::AsCString(p->modelSuffix_).~CString();
        records::AsCString(p->modelPrefix_).~CString();
        records::AsCString(p->faceName_).~CString();
    }

private:
    static void Plain(R& to, const R& from) {
        to.lastHitPoints_ = from.lastHitPoints_;
        to.flash_ = from.flash_;
        to.restA_ = from.restA_;
        to.current_ = from.current_;
        to.restB_ = from.restB_;
        to.wobbleKind_ = from.wobbleKind_;
        to.wobbleTime_ = from.wobbleTime_;
        to.faceState_ = from.faceState_;
        to.faceMode_ = from.faceMode_;
        to.faceTimer_ = from.faceTimer_;
        to.build_ = from.build_;
        to.wobbleTarget_ = from.wobbleTarget_;
        to.wobbleSpeed_ = from.wobbleSpeed_;
        to.hasPlayer_ = from.hasPlayer_;
        to.playerId_ = from.playerId_;
    }
};

using HudPortraitSlotVector = stlp::VectorAbi<HudPortraitSlot, HudPortraitSlotElement>;

// ---- The UI's lists (ui.h, ui_camp.h, ui_screens.h) ---------------------------------------------

// The save list's entries (ui_saveload.cpp's 006578F0 / 006579C0): the compiler's copy constructor
// 00656640, operator= 006538E0 and destructor 00656720 (S13).
struct SaveEntryElement {
    static void Construct(SaveEntry* to, const SaveEntry& from) { to->CopyConstruct(&from); }
    static void Assign(SaveEntry& to, const SaveEntry& from) { to.Assign(&from); }
    static void Destroy(SaveEntry* p) { p->Destruct(); }
};

// The multiplayer address list (ui_connect_screen.cpp's 0061EC40): 0061EE80, 0061E8F0, 0061E8A0.
struct MpAddressElement {
    static void Construct(MpAddressEntry* to, const MpAddressEntry& from) { to->CopyConstruct(&from); }
    static void Assign(MpAddressEntry& to, const MpAddressEntry& from) { to.AssignFrom(from); }
    static void Destroy(MpAddressEntry* p) { p->Destroy(); }
};

// The camp's trade rows (ui_trade_panel.cpp's 00629E60): the copy constructor 0062A310 (the texts,
// then the numbers and the flag; the padding is not copied), the destructor 00613CF0.
struct TradeRowElement {
    static void Construct(TradeRow* to, const TradeRow& from) { to->CopyConstruct(&from); }
    static void Destroy(TradeRow* p) { p->Destroy(); }
};

// The options window's key bindings (ui_options.cpp's 00652930): memberwise (the copy constructor
// 00652FF0), the destructor inline (OptionBinding::Destroy).
struct OptionBindingElement : records::Memberwise<OptionBinding, &OptionBinding::action_, &OptionBinding::keys_,
                                                  &OptionBinding::name_, &OptionBinding::keysText_> {
    static void Destroy(OptionBinding* p) { p->Destroy(); }
};

using SaveEntryVector = stlp::VectorAbi<SaveEntry, SaveEntryElement>;
using MpAddressVector = stlp::VectorAbi<MpAddressEntry, MpAddressElement>;
using TradeRowVector = stlp::VectorAbi<TradeRow, TradeRowElement>;
using TradeRecordVector = stlp::VectorAbi<TradeRecord>;   // plain 0x3C-byte records (0062A0B0)
using OptionBindingVector = stlp::VectorAbi<OptionBinding, OptionBindingElement>;

// ---- The game flow's zone table (world_game.cpp's 00587480) ------------------------------------

// world_game.cpp's ZoneEntry with its texts typed: two CStrings and an id, copied memberwise and
// destroyed from the last (the compiler's pair).
struct ZoneTableEntry {
    mfc::CString first;
    mfc::CString second;
    int id;
};
static_assert(sizeof(ZoneTableEntry) == 12);

using ZoneTableVector = stlp::VectorAbi<ZoneTableEntry, stlp::ObjectElement<ZoneTableEntry>>;

// ---- The quest database (quest_records.h) ------------------------------------------------------

using QuestVector =stlp::VectorAbi<QuestRecord, RecordElement<QuestRecord, &QuestRecord::AssignQuest>>;
using BriefingVector =
    stlp::VectorAbi<QuestBriefingRecord, RecordElement<QuestBriefingRecord, &QuestBriefingRecord::AssignBriefing>>;

} // namespace recordvec
