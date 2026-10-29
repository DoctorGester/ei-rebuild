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
    condition = -1.0f;
    conditionMax = -1.0f;
    charge = -1.0f;
    chargeMax = -1.0f;
    chargeRate = -1.0f;
    weight = -1.0f;
    price = -1.0f;
    unreadValue = -1.0f;
    count = 1;
}

CItem::~CItem() {
    CItem::Destroy();
}

void CItem::Create() {
    count = 1;
    spell.Reset();
}

void CItem::Destroy() {
    spell.Reset();
    link.Reset();
}

bool CItem::RunTick() {
    EI_NAN_AUDIT(charge, chargeMax);
    if (charge == chargeMax) return false;
    EI_NAN_AUDIT(chargeRate);
    if (chargeRate <= 0.0f) {
        charge = chargeMax;
        return true;
    }
    charge = chargeRate * chargeMax + charge;
    if (charge >= chargeMax) charge = chargeMax;
    return true;
}

void CItem::InitFromRecord(const ItemRecordHead* head) {
    conditionMax = head->condition;
    condition = head->condition;
    chargeRate = kDefaultChargeRate;
    chargeMax = head->charge;
    charge = head->charge;
    weight = head->weight;
    price = head->price;
    unreadValue = head->unreadValue;
    magicLevel = head->magicLevel;
}

bool CItem::IsSameAs(CItem* other) {
    if (other == this) return true;
    if (type != other->type || prototypeId != other->prototypeId) return false;
    EI_NAN_AUDIT(condition, other->condition, conditionMax, other->conditionMax, charge,
                 other->charge, chargeMax, other->chargeMax, weight, other->weight, price, other->price,
                 unreadValue, other->unreadValue);
    if (condition != other->condition || conditionMax != other->conditionMax ||
        charge != other->charge || chargeMax != other->chargeMax || weight != other->weight ||
        price != other->price || unreadValue != other->unreadValue)
        return false;
    if (magicLevel != other->magicLevel) return false;
    const CSpell* currentSpell = spell;
    const CSpell* otherSpell = other->spell;
    if (currentSpell == nullptr || otherSpell == nullptr) return currentSpell == otherSpell;
    if (currentSpell->prototypeId != otherSpell->prototypeId) return false;
    return std::memcmp(currentSpell->modifiers, otherSpell->modifiers, sizeof(currentSpell->modifiers)) == 0;
}

bool CItem::CopyTo(CItem* target, CSpellManager* spells) {
    if (target->type != type) return false;
    target->Destroy();
    target->prototypeId = prototypeId;
    // The floats bit for bit (a loaded item's values may be any bit pattern).
    x87::CopyBits(target->condition, condition);
    x87::CopyBits(target->conditionMax, conditionMax);
    x87::CopyBits(target->charge, charge);
    x87::CopyBits(target->chargeMax, chargeMax);
    x87::CopyBits(target->chargeRate, chargeRate);
    x87::CopyBits(target->weight, weight);
    x87::CopyBits(target->price, price);
    x87::CopyBits(target->unreadValue, unreadValue);
    target->magicLevel = magicLevel;
    target->spell.Assign(spell ? spell->Clone(spells) : nullptr);
    target->count = 1;
    return true;
}

CItem* CItem::Clone(CSpellManager* spells) {
    auto* copy = owner->CreateObjectAs<CItem>(type, -1);
    if (CopyTo(copy, spells)) return copy;
    copy->AddRef();
    copy->Release();
    return nullptr;
}

void CItem::Load(CStream* stream) {
    stream->ReadValue(prototypeId);
    stream->ReadValue(condition);
    stream->ReadValue(conditionMax);
    stream->ReadValue(charge);
    stream->ReadValue(chargeMax);
    stream->ReadValue(chargeRate);
    stream->ReadValue(weight);
    stream->ReadValue(price);
    stream->ReadValue(unreadValue);
    CSpellManager* spells = WorldContext()->SpellManager();
    std::int32_t objectId = 0;
    stream->ReadValue(objectId);
    CSpell* newSpell = objectId == kNoObjectId ? nullptr : spells->FindObjectAs<CSpell>(static_cast<unsigned>(objectId));
    spell.Assign(newSpell);
    stream->ReadValue(count);
    stream->ReadValue(magicLevel);
}

void CItem::Save(CStream* stream) {
    stream->WriteValue(prototypeId);
    stream->WriteValue(condition);
    stream->WriteValue(conditionMax);
    stream->WriteValue(charge);
    stream->WriteValue(chargeMax);
    stream->WriteValue(chargeRate);
    stream->WriteValue(weight);
    stream->WriteValue(price);
    stream->WriteValue(unreadValue);
    std::int32_t spellId = spell != nullptr ? spell->id : kNoObjectId;
    stream->WriteValue(spellId);
    stream->WriteValue(count);
    stream->WriteValue(magicLevel);
}

ItemRecordHead* CItem::GetPrototype() {
    if (Kind() == ItemKind::Scroll) return nullptr;
    return PrototypeAt(Kind(), PrototypeIndex(prototypeId));
}

QuickItemCategory CItem::GetCategory() {
    const ItemRecordHead* prototype = GetPrototype();
    return prototype ? static_cast<QuickItemCategory>(prototype->category) : QuickItemCategory::NoPrototype;
}

void CItem::AttachSpell(CSpell* newSpell) {
    if (CSpell* previous = spell) price = price - std::bit_cast<float>(previous->price);
    spell.Assign(newSpell);
    if (spell) price = spell->price + price;
}

// No key (the quest item's has its id).
LockKey CItem::GetUse() { return LockKey{0, 0, 0}; }

// --- CItemLink -----------------------------------------------------------------------------------

CItemLink::CItemLink() {
    refs = 0;
    flags6 = 0;
    flags7 = 0;
}

void CItemLink::OnShutdown() {}

CItemLink* CItemLink::Clone() {
    auto* copy = new CItemLink();
    copy->value = value;
    return copy;
}

// --- CItemWeapon ---------------------------------------------------------------------------------

CItemWeapon::CItemWeapon() {
    damage = -1.0f;
    damageSpread = -1.0f;
    reach = -1.0f;
    actionValue = 0;
}

const char* CItemWeapon::ClassName() { return "CItemWeapon"; }

ClassId CItemWeapon::GetClassId() { return ClassId::ItemWeapon; }

int CItemWeapon::GetType() {
    return WeaponAt(PrototypeIndex(prototypeId))->type;
}

void CItemWeapon::GetDamageTypes(float (&out)[kDamageTypeCount]) {
    if (const WeaponRecord* prototype = WeaponAt(PrototypeIndex(prototypeId)))
        std::memcpy(out, prototype->damageTypes, sizeof(out));
    else
        std::memset(out, 0, sizeof(out));
}

double CItemWeapon::RollDamage() {
    EI_NAN_AUDIT(damageSpread);
    if (damageSpread == 0.0f) return damage;
    float high = damageSpread + damage;     // stored as float
    float low = damage;                      // read back from memory for the subtraction and the add
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
    reach = r->reach;
    damage = r->damage;
    damageSpread = r->damageSpread;
}

bool CItemWeapon::IsSameAs(CItem* item) {
    if (!CItem::IsSameAs(item)) return false;
    CItemWeapon* other = AsWeapon(item);   // of this one's type (the base's test)
    if (other->actionValue != actionValue) return false;
    EI_NAN_AUDIT(other->damage, damage, other->damageSpread, damageSpread, other->reach, reach);
    if (other->damage != damage) return false;
    if (other->damageSpread != damageSpread) return false;
    if (other->reach != reach) return false;
    return true;
}

bool CItemWeapon::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyTo(target, spells)) return false;
    CItemWeapon* weapon = AsWeapon(target);   // a copy of this weapon
    weapon->actionValue = actionValue;
    x87::CopyBits(weapon->damage, damage);
    x87::CopyBits(weapon->damageSpread, damageSpread);
    x87::CopyBits(weapon->reach, reach);
    return true;
}

void CItemWeapon::Load(CStream* stream) {
    CItem::Load(stream);
    stream->ReadValue(actionValue);
    stream->ReadValue(damage);
    stream->ReadValue(damageSpread);
    stream->ReadValue(reach);
}

void CItemWeapon::Save(CStream* stream) {
    CItem::Save(stream);
    stream->WriteValue(actionValue);
    stream->WriteValue(damage);
    stream->WriteValue(damageSpread);
    stream->WriteValue(reach);
}

// --- CItemArmor ----------------------------------------------------------------------------------

const char* CItemArmor::ClassName() { return "CItemArmor"; }

ClassId CItemArmor::GetClassId() { return ClassId::ItemArmor; }

int CItemArmor::GetType() {
    return ArmorAt(PrototypeIndex(prototypeId))->type;
}

void CItemArmor::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    auto* r = RecordAs<ArmorRecord>(record);
    std::memcpy(absorb[0], r->absorbFirst, sizeof(absorb[0]));
    std::memcpy(absorb[1], r->absorbOthers, sizeof(absorb[1]));
}

bool CItemArmor::IsSameAs(CItem* item) {
    if (!CItem::IsSameAs(item)) return false;
    auto* other = AsArmor(item);
    // Both absorption rows and the layer masks, compared by their bits.
    return std::memcmp(absorb, other->absorb, sizeof(absorb)) == 0 &&
           std::memcmp(layerMask, other->layerMask, sizeof(layerMask)) == 0;
}

bool CItemArmor::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyTo(target, spells)) return false;
    auto* armor = AsArmor(target);
    std::memcpy(armor->absorb, absorb, sizeof(absorb));
    armor->layerMask[0] = layerMask[0];
    armor->layerMask[1] = layerMask[1];
    return true;
}

void CItemArmor::Load(CStream* stream) {
    CItem::Load(stream);
    stream->Read(absorb[0], sizeof(absorb[0]));
    stream->Read(absorb[1], sizeof(absorb[1]));
}

void CItemArmor::Save(CStream* stream) {
    CItem::Save(stream);
    stream->Write(absorb[0], sizeof(absorb[0]));
    stream->Write(absorb[1], sizeof(absorb[1]));
}

// --- CItemQuick ----------------------------------------------------------------------------------

const char* CItemQuick::ClassName() { return "CItemQuick"; }

ClassId CItemQuick::GetClassId() { return ClassId::ItemQuick; }

void CItemQuick::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    actionValue = RecordAs<QuickItemRecord>(record)->actionValue;
}

bool CItemQuick::IsSameAs(CItem* other) {
    if (!CItem::IsSameAs(other)) return false;
    return AsQuick(other)->actionValue == actionValue;
}

bool CItemQuick::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyTo(target, spells)) return false;
    AsQuick(target)->actionValue = actionValue;
    return true;
}

void CItemQuick::Load(CStream* stream) {
    CItem::Load(stream);
    stream->ReadValue(actionValue);
}

void CItemQuick::Save(CStream* stream) {
    CItem::Save(stream);
    stream->WriteValue(actionValue);
}

// --- CItemScroll ----------------------------------------------------------------------------------

const char* CItemScroll::ClassName() { return "CItemScroll"; }

ClassId CItemScroll::GetClassId() { return ClassId::ItemScroll; }

// --- CItemQuest ----------------------------------------------------------------------------------

const char* CItemQuest::ClassName() { return "CItemQuest"; }

ClassId CItemQuest::GetClassId() { return ClassId::ItemQuest; }

void CItemQuest::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    questItemId = record->category;
}

bool CItemQuest::IsSameAs(CItem* other) {
    if (!CItem::IsSameAs(other)) return false;
    return AsQuest(other)->questItemId == questItemId;
}

bool CItemQuest::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyTo(target, spells)) return false;
    AsQuest(target)->questItemId = questItemId;
    return true;
}

void CItemQuest::Load(CStream* stream) {
    CItem::Load(stream);
    stream->ReadValue(questItemId);
}

void CItemQuest::Save(CStream* stream) {
    CItem::Save(stream);
    stream->WriteValue(questItemId);
}

LockKey CItemQuest::GetUse() { return LockKey{8, questItemId, 0}; }

// --- CItemLoot -----------------------------------------------------------------------------------

const char* CItemLoot::ClassName() { return "CItemLoot"; }

ClassId CItemLoot::GetClassId() { return ClassId::ItemLoot; }

float CItemLoot::GetPrice() {
    switch (kind) {
    case LootKind::Self: return price;
    case LootKind::Material: return MaterialAt(index)->unitPrice;
    case LootKind::Record: break;
    default: return -1.0f;
    }
    switch (recordType) {
    case LootRecordType::Weapon: return WeaponAt(index)->price;
    case LootRecordType::Armor: return ArmorAt(index)->price;
    case LootRecordType::Quick: return QuickItemAt(index)->price;
    case LootRecordType::Spell: return SpellAt(index)->price;
    case LootRecordType::Modifier: return SpellModifierAt(index)->price;
    default: return -1.0f;
    }
}

void CItemLoot::InitFromRecord(const ItemRecordHead* record) {
    CItem::InitFromRecord(record);
    auto* r = RecordAs<LootItemRecord>(record);
    kind = static_cast<LootKind>(r->category);
    recordType = static_cast<LootRecordType>(r->recordType);
    index = r->index;
}

bool CItemLoot::IsSameAs(CItem* item) {
    if (!CItem::IsSameAs(item)) return false;
    auto* other = AsLoot(item);
    if (other->kind != kind || other->recordType != recordType) return false;
    return other->index == index;
}

bool CItemLoot::CopyTo(CItem* target, CSpellManager* spells) {
    if (!CItem::CopyTo(target, spells)) return false;
    auto* loot = AsLoot(target);
    loot->kind = kind;
    loot->recordType = recordType;
    loot->index = index;
    return true;
}

void CItemLoot::Load(CStream* stream) {
    CItem::Load(stream);
    std::uint32_t value = 0;
    stream->ReadValue(value);
    kind = static_cast<LootKind>(value);
    stream->ReadValue(value);
    recordType = static_cast<LootRecordType>(value);
    stream->ReadValue(index);
}

void CItemLoot::Save(CStream* stream) {
    CItem::Save(stream);
    stream->WriteValue(kind);
    stream->WriteValue(recordType);
    stream->WriteValue(index);
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
