// The items: CItem, its six classes (construction, the recharge, the values a record gives, the
// comparison, the copy, Load / Save), the link record and the weapon stats by packed id. The name builders are
// item_names.cpp; the item constructor and manager item_manager.cpp. See item.h and docs/notes/items_spells.md.
#include "item.h"

#include "base_manager.h"
#include "core_streams.h"
#include "random.h"
#include "spell.h"
#include "world.h"

#include "lib/nan_audit.h"

#include "ei/x87.h"

#include <bit>
#include <cstdint>
#include <cstring>

using namespace itemdb;

namespace {

WeaponRecord* WeaponOf(std::uint32_t packedId) { return WeaponAt(PrototypeIndex(packedId)); }

MaterialRecordHead* MaterialOf(std::uint32_t packedId) { return MaterialAt(MaterialIndex(packedId)); }

// The charge rate every item starts with.
constexpr float kDefaultChargeRate = 0.001f;

} // namespace

ItemRecordHead* PrototypeAt(ItemKind kind, std::uint32_t index) {
    switch (kind) {
    case ItemKind::Weapon: return WeaponAt(index);
    case ItemKind::Armor: return ArmorAt(index);
    case ItemKind::Quick:
    case ItemKind::Scroll: return QuickItemAt(index);
    case ItemKind::Loot: return LootAt(index);
    case ItemKind::Quest: return QuestItemAt(index);
    default: return nullptr;
    }
}

// --- CItem ---------------------------------------------------------------------------------------

CItem::CItem() {
    condition_ = -1.0f;
    conditionMax_ = -1.0f;
    charge_ = -1.0f;
    chargeMax_ = -1.0f;
    chargeRate_ = -1.0f;
    weight_ = -1.0f;
    price_ = -1.0f;
    unreadValue_ = -1.0f;
    count_ = 1;
}

CItem::~CItem() {
    CItem::Destroy();
}

void CItem::Create() {
    count_ = 1;
    spell_.Reset();
}

void CItem::Destroy() {
    spell_.Reset();
    link_.Reset();
}

bool CItem::RunTick() {
    EI_NAN_AUDIT(charge_, chargeMax_);
    if (charge_ == chargeMax_) return false;
    EI_NAN_AUDIT(chargeRate_);
    if (chargeRate_ <= 0.0f) {
        charge_ = chargeMax_;
        return true;
    }
    charge_ = chargeRate_ * chargeMax_ + charge_;
    if (charge_ >= chargeMax_) charge_ = chargeMax_;
    return true;
}

void CItem::InitFromRecord(const ItemRecordHead* head) {
    conditionMax_ = head->condition;
    condition_ = head->condition;
    chargeRate_ = kDefaultChargeRate;
    chargeMax_ = head->charge;
    charge_ = head->charge;
    weight_ = head->weight;
    price_ = head->price;
    unreadValue_ = head->unreadValue;
    magicLevel_ = head->magicLevel;
}

bool CItem::IsSameAs(CItem* other) {
    if (other == this) return true;
    if (type_ != other->type_ || prototypeId_ != other->prototypeId_) return false;
    EI_NAN_AUDIT(condition_, other->condition_, conditionMax_, other->conditionMax_, charge_,
                 other->charge_, chargeMax_, other->chargeMax_, weight_, other->weight_, price_, other->price_,
                 unreadValue_, other->unreadValue_);
    if (condition_ != other->condition_ || conditionMax_ != other->conditionMax_ ||
        charge_ != other->charge_ || chargeMax_ != other->chargeMax_ || weight_ != other->weight_ ||
        price_ != other->price_ || unreadValue_ != other->unreadValue_)
        return false;
    if (magicLevel_ != other->magicLevel_) return false;
    const CSpell* spell = spell_;
    const CSpell* otherSpell = other->spell_;
    if (spell == nullptr || otherSpell == nullptr) return spell == otherSpell;
    if (spell->prototypeId_ != otherSpell->prototypeId_) return false;
    return std::memcmp(spell->modifiers_, otherSpell->modifiers_, sizeof(spell->modifiers_)) == 0;
}

bool CItem::CopyTo(CItem* target, CSpellManager* spells) {
    if (target->type_ != type_) return false;
    target->Destroy();
    target->prototypeId_ = prototypeId_;
    // The floats bit for bit (a loaded item's values may be any bit pattern).
    x87::CopyBits(target->condition_, condition_);
    x87::CopyBits(target->conditionMax_, conditionMax_);
    x87::CopyBits(target->charge_, charge_);
    x87::CopyBits(target->chargeMax_, chargeMax_);
    x87::CopyBits(target->chargeRate_, chargeRate_);
    x87::CopyBits(target->weight_, weight_);
    x87::CopyBits(target->price_, price_);
    x87::CopyBits(target->unreadValue_, unreadValue_);
    target->magicLevel_ = magicLevel_;
    target->spell_.Assign(spell_ ? spell_->Clone(spells) : nullptr);
    target->count_ = 1;
    return true;
}

CItem* CItem::Clone(CSpellManager* spells) {
    auto* copy = owner_->CreateObjectAs<CItem>(type_, -1);
    if (CopyTo(copy, spells)) return copy;
    copy->AddRef();
    copy->Release();
    return nullptr;
}

void CItem::Load(CStream* stream) {
    stream->ReadValue(prototypeId_);
    stream->ReadValue(condition_);
    stream->ReadValue(conditionMax_);
    stream->ReadValue(charge_);
    stream->ReadValue(chargeMax_);
    stream->ReadValue(chargeRate_);
    stream->ReadValue(weight_);
    stream->ReadValue(price_);
    stream->ReadValue(unreadValue_);
    CSpellManager* spells = WorldContext()->SpellManager();
    std::int32_t id = 0;
    stream->ReadValue(id);
    CSpell* spell = id == kNoObjectId ? nullptr : spells->FindObjectAs<CSpell>(static_cast<unsigned>(id));
    spell_.Assign(spell);
    stream->ReadValue(count_);
    stream->ReadValue(magicLevel_);
}

void CItem::Save(CStream* stream) {
    stream->WriteValue(prototypeId_);
    stream->WriteValue(condition_);
    stream->WriteValue(conditionMax_);
    stream->WriteValue(charge_);
    stream->WriteValue(chargeMax_);
    stream->WriteValue(chargeRate_);
    stream->WriteValue(weight_);
    stream->WriteValue(price_);
    stream->WriteValue(unreadValue_);
    std::int32_t spellId = spell_ != nullptr ? spell_->id_ : kNoObjectId;
    stream->WriteValue(spellId);
    stream->WriteValue(count_);
    stream->WriteValue(magicLevel_);
}

ItemRecordHead* CItem::GetPrototype() {
    if (Kind() == ItemKind::Scroll) return nullptr;
    return PrototypeAt(Kind(), PrototypeIndex(prototypeId_));
}

QuickItemCategory CItem::GetCategory() {
    const ItemRecordHead* prototype = GetPrototype();
    return prototype ? static_cast<QuickItemCategory>(prototype->category) : QuickItemCategory::NoPrototype;
}

void CItem::AttachSpell(CSpell* spell) {
    if (CSpell* previous = spell_) price_ = price_ - std::bit_cast<float>(previous->price_);
    spell_.Assign(spell);
    if (spell_) price_ = spell_->price_ + price_;
}

// No key (the quest item's has its id).
LockKey CItem::GetUse() { return LockKey{0, 0, 0}; }

// --- CItemLink -----------------------------------------------------------------------------------

CItemLink::CItemLink() {
    refs_ = 0;
    flags6_ = 0;
    flags7_ = 0;
}

void CItemLink::OnShutdown() {}

CItemLink* CItemLink::Clone() {
    auto* copy = new CItemLink();
    copy->value = value;
    return copy;
}

// --- CItemWeapon ---------------------------------------------------------------------------------

CItemWeapon::CItemWeapon() {
    damage_ = -1.0f;
    damageSpread_ = -1.0f;
    reach_ = -1.0f;
    actionValue_ = 0;
}

const char* CItemWeapon::ClassName() { return "CItemWeapon"; }

ClassId CItemWeapon::GetClassId() { return ClassId::ItemWeapon; }

int CItemWeapon::GetType() {
    return WeaponAt(PrototypeIndex(prototypeId_))->type;
}

void CItemWeapon::GetDamageTypes(float (&out)[kDamageTypeCount]) {
    if (const WeaponRecord* prototype = WeaponAt(PrototypeIndex(prototypeId_)))
        std::memcpy(out, prototype->damageTypes, sizeof(out));
    else
        std::memset(out, 0, sizeof(out));
}

double CItemWeapon::RollDamage() {
    EI_NAN_AUDIT(damageSpread_);
    if (damageSpread_ == 0.0f) return damage_;
    float high = damageSpread_ + damage_;   // stored as float
    float low = damage_;                     // read back from memory for the subtraction and the add
    std::uint32_t roll = g_random.Next();
    volatile std::int64_t roll64 = roll;
    // About 1 / (2^32 - 1).
    constexpr double kRollScale = 0x1.0000000100000p-32;
    // ((high - low) * roll) * scale + low, all in double.
    return (static_cast<double>(high) - static_cast<double>(low)) * static_cast<double>(roll64) * kRollScale +
           static_cast<double>(low);
}

void CItemWeapon::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    auto* r = RecordAs<WeaponRecord>(record);
    reach_ = r->reach;
    damage_ = r->damage;
    damageSpread_ = r->damageSpread;
}

bool CItemWeapon::IsSameAs(CItem* item) {
    if (!CItem::IsSameAs(item)) return false;
    CItemWeapon* other = AsWeapon(item);   // of this one's type (the base's test)
    if (other->actionValue_ != actionValue_) return false;
    EI_NAN_AUDIT(other->damage_, damage_, other->damageSpread_, damageSpread_, other->reach_, reach_);
    if (other->damage_ != damage_) return false;
    if (other->damageSpread_ != damageSpread_) return false;
    if (other->reach_ != reach_) return false;
    return true;
}

bool CItemWeapon::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyTo(target, spells)) return false;
    CItemWeapon* weapon = AsWeapon(target);   // a copy of this weapon
    weapon->actionValue_ = actionValue_;
    x87::CopyBits(weapon->damage_, damage_);
    x87::CopyBits(weapon->damageSpread_, damageSpread_);
    x87::CopyBits(weapon->reach_, reach_);
    return true;
}

void CItemWeapon::Load(CStream* stream) {
    CItem::Load(stream);
    stream->ReadValue(actionValue_);
    stream->ReadValue(damage_);
    stream->ReadValue(damageSpread_);
    stream->ReadValue(reach_);
}

void CItemWeapon::Save(CStream* stream) {
    CItem::Save(stream);
    stream->WriteValue(actionValue_);
    stream->WriteValue(damage_);
    stream->WriteValue(damageSpread_);
    stream->WriteValue(reach_);
}

// --- CItemArmor ----------------------------------------------------------------------------------

const char* CItemArmor::ClassName() { return "CItemArmor"; }

ClassId CItemArmor::GetClassId() { return ClassId::ItemArmor; }

int CItemArmor::GetType() {
    return ArmorAt(PrototypeIndex(prototypeId_))->type;
}

void CItemArmor::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    auto* r = RecordAs<ArmorRecord>(record);
    std::memcpy(absorb_[0], r->absorbFirst, sizeof(absorb_[0]));
    std::memcpy(absorb_[1], r->absorbOthers, sizeof(absorb_[1]));
}

bool CItemArmor::IsSameAs(CItem* item) {
    if (!CItem::IsSameAs(item)) return false;
    auto* other = AsArmor(item);
    // Both absorption rows and the layer masks, compared by their bits.
    return std::memcmp(absorb_, other->absorb_, sizeof(absorb_)) == 0 &&
           std::memcmp(layerMask_, other->layerMask_, sizeof(layerMask_)) == 0;
}

bool CItemArmor::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyTo(target, spells)) return false;
    auto* armor = AsArmor(target);
    std::memcpy(armor->absorb_, absorb_, sizeof(absorb_));
    armor->layerMask_[0] = layerMask_[0];
    armor->layerMask_[1] = layerMask_[1];
    return true;
}

void CItemArmor::Load(CStream* stream) {
    CItem::Load(stream);
    stream->Read(absorb_[0], sizeof(absorb_[0]));
    stream->Read(absorb_[1], sizeof(absorb_[1]));
}

void CItemArmor::Save(CStream* stream) {
    CItem::Save(stream);
    stream->Write(absorb_[0], sizeof(absorb_[0]));
    stream->Write(absorb_[1], sizeof(absorb_[1]));
}

// --- CItemQuick ----------------------------------------------------------------------------------

const char* CItemQuick::ClassName() { return "CItemQuick"; }

ClassId CItemQuick::GetClassId() { return ClassId::ItemQuick; }

void CItemQuick::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    actionValue_ = RecordAs<QuickItemRecord>(record)->actionValue;
}

bool CItemQuick::IsSameAs(CItem* other) {
    if (!CItem::IsSameAs(other)) return false;
    return AsQuick(other)->actionValue_ == actionValue_;
}

bool CItemQuick::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyTo(target, spells)) return false;
    AsQuick(target)->actionValue_ = actionValue_;
    return true;
}

void CItemQuick::Load(CStream* stream) {
    CItem::Load(stream);
    stream->ReadValue(actionValue_);
}

void CItemQuick::Save(CStream* stream) {
    CItem::Save(stream);
    stream->WriteValue(actionValue_);
}

// --- CItemScroll ----------------------------------------------------------------------------------

const char* CItemScroll::ClassName() { return "CItemScroll"; }

ClassId CItemScroll::GetClassId() { return ClassId::ItemScroll; }

// --- CItemQuest ----------------------------------------------------------------------------------

const char* CItemQuest::ClassName() { return "CItemQuest"; }

ClassId CItemQuest::GetClassId() { return ClassId::ItemQuest; }

void CItemQuest::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    questItemId_ = record->category;
}

bool CItemQuest::IsSameAs(CItem* other) {
    if (!CItem::IsSameAs(other)) return false;
    return AsQuest(other)->questItemId_ == questItemId_;
}

bool CItemQuest::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyTo(target, spells)) return false;
    AsQuest(target)->questItemId_ = questItemId_;
    return true;
}

void CItemQuest::Load(CStream* stream) {
    CItem::Load(stream);
    stream->ReadValue(questItemId_);
}

void CItemQuest::Save(CStream* stream) {
    CItem::Save(stream);
    stream->WriteValue(questItemId_);
}

LockKey CItemQuest::GetUse() { return LockKey{8, questItemId_, 0}; }

// --- CItemLoot -----------------------------------------------------------------------------------

const char* CItemLoot::ClassName() { return "CItemLoot"; }

ClassId CItemLoot::GetClassId() { return ClassId::ItemLoot; }

float CItemLoot::GetPrice() {
    switch (kind_) {
    case LootKind::Self: return price_;
    case LootKind::Material: return MaterialAt(index_)->unitPrice;
    case LootKind::Record: break;
    default: return -1.0f;
    }
    switch (recordType_) {
    case LootRecordType::Weapon: return WeaponAt(index_)->price;
    case LootRecordType::Armor: return ArmorAt(index_)->price;
    case LootRecordType::Quick: return QuickItemAt(index_)->price;
    case LootRecordType::Spell: return SpellAt(index_)->price;
    case LootRecordType::Modifier: return SpellModifierAt(index_)->price;
    default: return -1.0f;
    }
}

void CItemLoot::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    auto* r = RecordAs<LootItemRecord>(record);
    kind_ = static_cast<LootKind>(r->category);
    recordType_ = static_cast<LootRecordType>(r->recordType);
    index_ = r->index;
}

bool CItemLoot::IsSameAs(CItem* item) {
    if (!CItem::IsSameAs(item)) return false;
    auto* other = AsLoot(item);
    if (other->kind_ != kind_ || other->recordType_ != recordType_) return false;
    return other->index_ == index_;
}

bool CItemLoot::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyTo(target, spells)) return false;
    auto* loot = AsLoot(target);
    loot->kind_ = kind_;
    loot->recordType_ = recordType_;
    loot->index_ = index_;
    return true;
}

void CItemLoot::Load(CStream* stream) {
    CItem::Load(stream);
    std::uint32_t value = 0;
    stream->ReadValue(value);
    kind_ = static_cast<LootKind>(value);
    stream->ReadValue(value);
    recordType_ = static_cast<LootRecordType>(value);
    stream->ReadValue(index_);
}

void CItemLoot::Save(CStream* stream) {
    CItem::Save(stream);
    stream->WriteValue(kind_);
    stream->WriteValue(recordType_);
    stream->WriteValue(index_);
}

// --- Weapon and armour stats by packed id / index ------------------------------------------------

int WeaponType(std::uint32_t packedId) {
    return WeaponOf(packedId)->type;
}

float WeaponMinDamage(std::uint32_t packedId) {
    WeaponRecord* weapon = WeaponOf(packedId);
    return weapon->damage * MaterialOf(packedId)->damageFactor;
}

float WeaponMaxDamage(std::uint32_t packedId) {
    WeaponRecord* weapon = WeaponOf(packedId);
    return (weapon->damageSpread + weapon->damage) * MaterialOf(packedId)->damageFactor;
}

float WeaponWeight(std::uint32_t packedId) {
    WeaponRecord* weapon = WeaponOf(packedId);
    return weapon->weight * MaterialOf(packedId)->weightFactor;
}

int WeaponDamageType(std::uint32_t packedId) {
    WeaponRecord* weapon = WeaponOf(packedId);
    for (int i = 0; i < kDamageTypeCount; ++i) {
        EI_NAN_AUDIT(weapon->damageTypes[i]);
        if (weapon->damageTypes[i] != 0.0f) return i;
    }
    return kDamageTypeCount - 1;
}

float WeaponAttackBonus(std::uint32_t packedId) {
    return WeaponOf(packedId)->attackBonus;
}

float WeaponDefenceBonus(std::uint32_t packedId) {
    return WeaponOf(packedId)->defenceBonus;
}

int ArmorLayer(std::uint32_t index) {
    ArmorRecord* armor = ArmorAt(index);
    return armor ? static_cast<int>(armor->layer) : 0;
}
