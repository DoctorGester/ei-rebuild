// The items: Item, its six classes (construction, the recharge, the values a record gives, the
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

// --- Item ---------------------------------------------------------------------------------------

Item::Item() {
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

Item::~Item() {
    Item::Destroy();
}

void Item::Create() {
    count = 1;
    spell.Reset();
}

void Item::Destroy() {
    spell.Reset();
    link.Reset();
}

bool Item::RunTick() {
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

void Item::InitFromRecord(const ItemRecordHead* head) {
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

bool Item::IsSameAs(Item* other) {
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

bool Item::CopyTo(Item* target, CSpellManager* spells) {
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

Item* Item::Clone(CSpellManager* spells) {
    auto* copy = owner->CreateObjectAs<Item>(type, -1);
    if (CopyTo(copy, spells)) return copy;
    copy->AddRef();
    copy->Release();
    return nullptr;
}

void Item::Load(ByteStream* stream) {
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

void Item::Save(ByteStream* stream) {
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

ItemRecordHead* Item::GetPrototype() {
    if (Kind() == ItemKind::Scroll) return nullptr;
    return PrototypeAt(Kind(), PrototypeIndex(prototypeId));
}

QuickItemCategory Item::GetCategory() {
    const ItemRecordHead* prototype = GetPrototype();
    return prototype ? static_cast<QuickItemCategory>(prototype->category) : QuickItemCategory::NoPrototype;
}

void Item::AttachSpell(CSpell* newSpell) {
    if (CSpell* previous = spell) price = price - std::bit_cast<float>(previous->price);
    spell.Assign(newSpell);
    if (spell) price = spell->price + price;
}

// No key (the quest item's has its id).
LockKey Item::GetUse() { return LockKey{0, 0, 0}; }

// --- ItemLink -----------------------------------------------------------------------------------

ItemLink::ItemLink() {
    refs = 0;
    flags6 = 0;
    flags7 = 0;
}

void ItemLink::OnShutdown() {}

ItemLink* ItemLink::Clone() {
    auto* copy = new ItemLink();
    copy->value = value;
    return copy;
}

// --- ItemWeapon ---------------------------------------------------------------------------------

ItemWeapon::ItemWeapon() {
    damage = -1.0f;
    damageSpread = -1.0f;
    reach = -1.0f;
    actionValue = 0;
}

const char* ItemWeapon::ClassName() { return "CItemWeapon"; }

ClassId ItemWeapon::GetClassId() { return ClassId::ItemWeapon; }

int ItemWeapon::GetType() {
    return WeaponAt(PrototypeIndex(prototypeId))->type;
}

void ItemWeapon::GetDamageTypes(float (&out)[kDamageTypeCount]) {
    if (const WeaponRecord* prototype = WeaponAt(PrototypeIndex(prototypeId)))
        std::memcpy(out, prototype->damageTypes, sizeof(out));
    else
        std::memset(out, 0, sizeof(out));
}

double ItemWeapon::RollDamage() {
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

void ItemWeapon::InitFromRecord(const ItemRecordHead* record) {
    Item::InitFromRecord(record);
    auto* r = RecordAs<WeaponRecord>(record);
    reach = r->reach;
    damage = r->damage;
    damageSpread = r->damageSpread;
}

bool ItemWeapon::IsSameAs(Item* item) {
    if (!Item::IsSameAs(item)) return false;
    ItemWeapon* other = AsWeapon(item);   // of this one's type (the base's test)
    if (other->actionValue != actionValue) return false;
    EI_NAN_AUDIT(other->damage, damage, other->damageSpread, damageSpread, other->reach, reach);
    if (other->damage != damage) return false;
    if (other->damageSpread != damageSpread) return false;
    if (other->reach != reach) return false;
    return true;
}

bool ItemWeapon::CopyTo(Item* target, CSpellManager* spells) {
    if (!Item::CopyTo(target, spells)) return false;
    ItemWeapon* weapon = AsWeapon(target);   // a copy of this weapon
    weapon->actionValue = actionValue;
    x87::CopyBits(weapon->damage, damage);
    x87::CopyBits(weapon->damageSpread, damageSpread);
    x87::CopyBits(weapon->reach, reach);
    return true;
}

void ItemWeapon::Load(ByteStream* stream) {
    Item::Load(stream);
    stream->ReadValue(actionValue);
    stream->ReadValue(damage);
    stream->ReadValue(damageSpread);
    stream->ReadValue(reach);
}

void ItemWeapon::Save(ByteStream* stream) {
    Item::Save(stream);
    stream->WriteValue(actionValue);
    stream->WriteValue(damage);
    stream->WriteValue(damageSpread);
    stream->WriteValue(reach);
}

// --- ItemArmor ----------------------------------------------------------------------------------

const char* ItemArmor::ClassName() { return "CItemArmor"; }

ClassId ItemArmor::GetClassId() { return ClassId::ItemArmor; }

int ItemArmor::GetType() {
    return ArmorAt(PrototypeIndex(prototypeId))->type;
}

void ItemArmor::InitFromRecord(const ItemRecordHead* record) {
    Item::InitFromRecord(record);
    auto* r = RecordAs<ArmorRecord>(record);
    std::memcpy(absorb[0], r->absorbFirst, sizeof(absorb[0]));
    std::memcpy(absorb[1], r->absorbOthers, sizeof(absorb[1]));
}

bool ItemArmor::IsSameAs(Item* item) {
    if (!Item::IsSameAs(item)) return false;
    auto* other = AsArmor(item);
    // Both absorption rows and the layer masks, compared by their bits.
    return std::memcmp(absorb, other->absorb, sizeof(absorb)) == 0 &&
           std::memcmp(layerMask, other->layerMask, sizeof(layerMask)) == 0;
}

bool ItemArmor::CopyTo(Item* target, CSpellManager* spells) {
    if (!Item::CopyTo(target, spells)) return false;
    auto* armor = AsArmor(target);
    std::memcpy(armor->absorb, absorb, sizeof(absorb));
    armor->layerMask[0] = layerMask[0];
    armor->layerMask[1] = layerMask[1];
    return true;
}

void ItemArmor::Load(ByteStream* stream) {
    Item::Load(stream);
    stream->Read(absorb[0], sizeof(absorb[0]));
    stream->Read(absorb[1], sizeof(absorb[1]));
}

void ItemArmor::Save(ByteStream* stream) {
    Item::Save(stream);
    stream->Write(absorb[0], sizeof(absorb[0]));
    stream->Write(absorb[1], sizeof(absorb[1]));
}

// --- ItemQuick ----------------------------------------------------------------------------------

const char* ItemQuick::ClassName() { return "CItemQuick"; }

ClassId ItemQuick::GetClassId() { return ClassId::ItemQuick; }

void ItemQuick::InitFromRecord(const ItemRecordHead* record) {
    Item::InitFromRecord(record);
    actionValue = RecordAs<QuickItemRecord>(record)->actionValue;
}

bool ItemQuick::IsSameAs(Item* other) {
    if (!Item::IsSameAs(other)) return false;
    return AsQuick(other)->actionValue == actionValue;
}

bool ItemQuick::CopyTo(Item* target, CSpellManager* spells) {
    if (!Item::CopyTo(target, spells)) return false;
    AsQuick(target)->actionValue = actionValue;
    return true;
}

void ItemQuick::Load(ByteStream* stream) {
    Item::Load(stream);
    stream->ReadValue(actionValue);
}

void ItemQuick::Save(ByteStream* stream) {
    Item::Save(stream);
    stream->WriteValue(actionValue);
}

// --- ItemScroll ----------------------------------------------------------------------------------

const char* ItemScroll::ClassName() { return "CItemScroll"; }

ClassId ItemScroll::GetClassId() { return ClassId::ItemScroll; }

// --- ItemQuest ----------------------------------------------------------------------------------

const char* ItemQuest::ClassName() { return "CItemQuest"; }

ClassId ItemQuest::GetClassId() { return ClassId::ItemQuest; }

void ItemQuest::InitFromRecord(const ItemRecordHead* record) {
    Item::InitFromRecord(record);
    questItemId = record->category;
}

bool ItemQuest::IsSameAs(Item* other) {
    if (!Item::IsSameAs(other)) return false;
    return AsQuest(other)->questItemId == questItemId;
}

bool ItemQuest::CopyTo(Item* target, CSpellManager* spells) {
    if (!Item::CopyTo(target, spells)) return false;
    AsQuest(target)->questItemId = questItemId;
    return true;
}

void ItemQuest::Load(ByteStream* stream) {
    Item::Load(stream);
    stream->ReadValue(questItemId);
}

void ItemQuest::Save(ByteStream* stream) {
    Item::Save(stream);
    stream->WriteValue(questItemId);
}

LockKey ItemQuest::GetUse() { return LockKey{8, questItemId, 0}; }

// --- ItemLoot -----------------------------------------------------------------------------------

const char* ItemLoot::ClassName() { return "CItemLoot"; }

ClassId ItemLoot::GetClassId() { return ClassId::ItemLoot; }

float ItemLoot::GetPrice() {
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

void ItemLoot::InitFromRecord(const ItemRecordHead* record) {
    Item::InitFromRecord(record);
    auto* r = RecordAs<LootItemRecord>(record);
    kind = static_cast<LootKind>(r->category);
    recordType = static_cast<LootRecordType>(r->recordType);
    index = r->index;
}

bool ItemLoot::IsSameAs(Item* item) {
    if (!Item::IsSameAs(item)) return false;
    auto* other = AsLoot(item);
    if (other->kind != kind || other->recordType != recordType) return false;
    return other->index == index;
}

bool ItemLoot::CopyTo(Item* target, CSpellManager* spells) {
    if (!Item::CopyTo(target, spells)) return false;
    auto* loot = AsLoot(target);
    loot->kind = kind;
    loot->recordType = recordType;
    loot->index = index;
    return true;
}

void ItemLoot::Load(ByteStream* stream) {
    Item::Load(stream);
    std::uint32_t value = 0;
    stream->ReadValue(value);
    kind = static_cast<LootKind>(value);
    stream->ReadValue(value);
    recordType = static_cast<LootRecordType>(value);
    stream->ReadValue(index);
}

void ItemLoot::Save(ByteStream* stream) {
    Item::Save(stream);
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
