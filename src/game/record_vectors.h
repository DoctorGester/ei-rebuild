#pragma once

// Element policies (lib/stlp_vector.h) of the game's vector<T> instantiations whose elements are game records
// with their own copy constructor, operator= and destructor, and the VectorAbi types record_vectors.cpp
// registers at the instantiations' addresses (docs/notes/standalone.md).

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

#include "lib/stlp_hash.h"
#include "lib/stlp_vector.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <type_traits>

// A unit model's body slot as its record's copies copy it, found by argument-dependent lookup from
// records::Memberwise (the slot constructor leaves the level name empty).
using UnitModelSlotMembers = records::Memberwise<UnitModelSlot, &UnitModelSlot::levelName, &UnitModelSlot::levelIndex,
                                                 &UnitModelSlot::share, &UnitModelSlot::factor>;
inline void CopyMember(UnitModelSlot& to, const UnitModelSlot& from) { UnitModelSlotMembers::Construct(&to, from); }
inline void AssignMember(UnitModelSlot& to, const UnitModelSlot& from) { UnitModelSlotMembers::Assign(to, from); }
inline void DefaultMember(UnitModelSlot& to) { UnitModelSlotMembers::Default(&to); }

namespace recordvec {

// resize(n) of instantiation `Abi` (its default record appended) for a caller that holds the vector
// untyped: the record tables' serialisers take it as a function of (vector, count).
template <typename Abi>
void ResizeUntyped(void* vector, unsigned count) {
    static_cast<typename Abi::Vector*>(vector)->template ResizeDefault<typename Abi::Element>(count);
}

// The weapon, armour, quick item, quest item and loot records: the kind's copy constructor, its
// operator= (the head's Assign, then its own fields), its destructor; resize(n) appends copies of
// the kind's default record.
template <typename R>
struct ItemElement {
    static void Construct(R* to, const R& from) { new (to) R(from); }
    static void Assign(R& to, const R& from) {
        to.Assign(&from);
        to.CopyOwn(&from);
    }
    static void Destroy(R* p) { p->~R(); }
    static void Default(R* p) { new (p) R(); }
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

// The copies of the unit prototype records: every member, the bytes +0x38..+0x47 too.
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

// The copies of the unit NPC records: +0x23 and +0x5D..+0x5F are not copied.
struct UnitNpcElement
    : records::Memberwise<UnitNpcRecord, &UnitNpcRecord::name, &UnitNpcRecord::field1, &UnitNpcRecord::experience,
                          &UnitNpcRecord::stats, &UnitNpcRecord::attributes, &UnitNpcRecord::skills, &UnitNpcRecord::skillNames,
                          &UnitNpcRecord::startWeapons, &UnitNpcRecord::startQuickItems, &UnitNpcRecord::startSpells,
                          &UnitNpcRecord::experienceFloor, &UnitNpcRecord::startMoney, &UnitNpcRecord::voiced> {
    static void Destroy(UnitNpcRecord* p) { unitdb::DestroyNpcRecord(p); }
};

// The copies of the unit model records: the members up to the sound names, the sound counts (a
// vector<int>), the rest; +0x11D..+0x11F are not copied.
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
    static Counts& CountsOf(R& r) { return r.soundCounts; }
    static const Counts& CountsOf(const R& r) { return r.soundCounts; }

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

// The copies of the unit level records; the destructor is the name's.
struct UnitLevelElement : records::Memberwise<UnitLevelRecord, &UnitLevelRecord::name, &UnitLevelRecord::slotType,
                                              &UnitLevelRecord::valuesA, &UnitLevelRecord::valuesB,
                                              &UnitLevelRecord::vitalMask> {
    static void Destroy(UnitLevelRecord* p) { std::destroy_at(&p->name); }
};

// The skill database: the copies of the skill and skill level records (+0x41..+0x43 are not
// copied); the destructors are the records' own.
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

// ---- A zone's unit looks (unit_looks.h; world_unit_set.cpp) ----------------------------

// The figured units' records (+0x02 and +0x1D..+0x1F are not copied): the head,
// then the texture names, CStrings declared as their data pointers. The destructor:
// the names and their storage, then the weapon and armour ids' storage.
struct FiguredUnitElement {
    using R = FiguredUnitRecord;
    using Head = records::Memberwise<R, &R::prototype, &R::items, &R::extraWeapons, &R::extraWeapon, &R::modelNumber,
                                     &R::morphS, &R::morphT, &R::morphU>;

    static void Construct(R* to, const R& from) {
        Head::Construct(to, from);
        records::CopyMember(to->names, from.names);
    }
    static void Assign(R& to, const R& from) {
        Head::Assign(to, from);
        records::AssignMember(to.names, from.names);
    }
    static void Default(R* p) {
        Head::Default(p);
        records::DefaultMember(p->names);
    }
    static void Destroy(R* p) {
        for (mfc::CString* name = p->names.Start(); name != p->names.Finish(); ++name) std::destroy_at(name);
        p->names.FreeStorage();
        p->extraWeapons.FreeStorage();
        p->items.FreeStorage();
    }
};

// The dressed units' records; the destructor: both id lists' storage, the last first.
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

// ---- prints.db (model.h; model_db.cpp) -------------------------------------------------

// The prints' records (model.h): their copy and default constructors and destructor; operator=
// assigns the name, then copies the value triples.
template <typename R>
struct PrintElement {
    static void Construct(R* to, const R& from) { new (to) R(from); }
    static void Assign(R& to, const R& from) {
        to.name.AssignFrom(from.name);
        std::memcpy(Values(to), Values(from), sizeof(R) - offsetof(R, look2));
    }
    static void Default(R* p) { new (p) R(); }
    static void Destroy(R* p) { p->~R(); }

private:
    static void* Values(R& r) { return &r.look2; }
    static const void* Values(const R& r) { return &r.look2; }
};

using PrintVector = stlp::VectorAbi<PrintRecord, PrintElement<PrintRecord>>;
using Print2Vector = stlp::VectorAbi<PrintRecord2, PrintElement<PrintRecord2>>;

// ---- acks.db's speech lists (acks_db.h; acks_db.cpp) ------------------------------------

// A speech: memberwise (the chance left as it is by default); the destructor the strings from the last.
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

// The records' head as their compiler-written members copy it: the name, then the map by ack kind (STLport's
// copy constructor and operator=, over _M_copy_from; the copy's map still points into the source record's
// lists, as in the game). By default an empty name with room for 8 bytes and an empty map of 193 buckets; its
// functor bytes are an uninitialised stack byte in the game, 0 here).
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

// ---- acks.db's table records (acks_db.h; loaded at startup) ------------------------------

// The compiler-written members (and the copy loops' inlined operator=): the
// head, then the speech lists in order; the destructors are the records' own.
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

// ---- The client's lobby queries (net_classes.h; net_lobby.cpp) -----------------------------

// A query: the copy constructor, the copy loop's assignment (the address, then the reply buffer's), the
// default (a zero address, an empty growing buffer) and the destructor (the reply buffer's).
struct QueryElement {
    using R = net::Query;
    static void Construct(R* to, const R& from) { new (to) R(from); }
    static void Assign(R& to, const R& from) { to = from; }
    static void Default(R* p) { new (p) R(); }
    static void Destroy(R* p) { p->~R(); }
};

using QueryVector = stlp::VectorAbi<net::Query, QueryElement>;

// ---- The party manager's entries (party_manager.h; party_manager.cpp) -------------------

// An entry: memberwise (empty texts and ids by default).
struct PartyEntryElement : records::Memberwise<PartyManagerEntry, &PartyManagerEntry::name, &PartyManagerEntry::text,
                                               &PartyManagerEntry::ids> {
    static void Destroy(PartyManagerEntry* p) { p->Destruct(); }
};

using PartyEntryVector = stlp::VectorAbi<PartyManagerEntry, PartyEntryElement>;

// ---- The lever table's records (lever.h; app_device_list.cpp) --------------------------

// A lever record: memberwise; resize(n)'s default has empty texts and no spots (the other fields
// left as they are); the destructor (the texts from the last).
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

// ---- The session's replicated messages (net_classes.h; net_session_base.cpp) -------------

// {message, priority}: plain bytes; push_back(T()) builds nothing (the constructor is
// empty), the caller stores into the new element.
struct ReplicatedElement : stlp::PlainElement<net::Replicated> {
    static void Default(net::Replicated*) {}
};

using ReplicatedVector = stlp::VectorAbi<net::Replicated, ReplicatedElement>;

// ---- The client party's engine objects (net_client_party.cpp) -----------------------------

// Counted engine object pointers: a copy adds a reference, the destructor releases a
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

// ---- The HUD's portrait slots (ui.h; ui_hud_portrait_setup.cpp) ---------------------------

// A slot: member by member (the padding after hasPlayer_ is not copied), the three texts as
// CStrings; push_back(T())'s default has identity orientations and empty texts (the other fields
// left as they are); the destructor: the texts from the last.
struct HudPortraitSlotElement {
    using R = HudPortraitSlot;
    static void Construct(R* to, const R& from) {
        Plain(*to, from);
        std::construct_at(&to->faceName_, from.faceName_);
        std::construct_at(&to->modelPrefix_, from.modelPrefix_);
        std::construct_at(&to->modelSuffix_, from.modelSuffix_);
    }
    static void Assign(R& to, const R& from) {
        Plain(to, from);
        to.faceName_ = from.faceName_;
        to.modelPrefix_ = from.modelPrefix_;
        to.modelSuffix_ = from.modelSuffix_;
    }
    static void Default(R* p) {
        p->restA_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
        p->current_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
        p->restB_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
        std::construct_at(&p->faceName_);
        std::construct_at(&p->modelPrefix_);
        std::construct_at(&p->modelSuffix_);
        p->wobbleTarget_.SetIdentity();
    }
    static void Destroy(R* p) {
        std::destroy_at(&p->modelSuffix_);
        std::destroy_at(&p->modelPrefix_);
        std::destroy_at(&p->faceName_);
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

// The save list's entries (ui_saveload.cpp): the compiler's copy constructor, operator= and destructor.
struct SaveEntryElement {
    static void Construct(SaveEntry* to, const SaveEntry& from) { to->CopyConstruct(&from); }
    static void Assign(SaveEntry& to, const SaveEntry& from) { to.Assign(&from); }
    static void Destroy(SaveEntry* p) { p->Destruct(); }
};

// The multiplayer address list (ui_connect_screen.cpp).
struct MpAddressElement {
    static void Construct(MpAddressEntry* to, const MpAddressEntry& from) { to->CopyConstruct(&from); }
    static void Assign(MpAddressEntry& to, const MpAddressEntry& from) { to.AssignFrom(from); }
    static void Destroy(MpAddressEntry* p) { p->Destroy(); }
};

// The camp's trade rows (ui_trade_panel.cpp): the copy constructor (the texts,
// then the numbers and the flag; the padding is not copied), the destructor.
struct TradeRowElement {
    static void Construct(TradeRow* to, const TradeRow& from) { to->CopyConstruct(&from); }
    static void Destroy(TradeRow* p) { p->Destroy(); }
};

// The options window's key bindings (ui_options.cpp): memberwise,
// the destructor inline (OptionBinding::Destroy).
struct OptionBindingElement : records::Memberwise<OptionBinding, &OptionBinding::action_, &OptionBinding::keys_,
                                                  &OptionBinding::name_, &OptionBinding::keysText_> {
    static void Destroy(OptionBinding* p) { p->Destroy(); }
};

using SaveEntryVector = stlp::VectorAbi<SaveEntry, SaveEntryElement>;
using MpAddressVector = stlp::VectorAbi<MpAddressEntry, MpAddressElement>;
using TradeRowVector = stlp::VectorAbi<TradeRow, TradeRowElement>;
using TradeRecordVector = stlp::VectorAbi<TradeRecord>;   // plain 0x3C-byte records
using OptionBindingVector = stlp::VectorAbi<OptionBinding, OptionBindingElement>;

// ---- The game flow's zone table (world_game.cpp) -------------------------------------

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
