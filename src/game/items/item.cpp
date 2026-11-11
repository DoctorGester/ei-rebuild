// The items: Item, its six classes (construction, the recharge, the values a record gives, the
// comparison, the copy, Load / Save), the link record and the weapon stats by packed id. The name builders are
// item_names.cpp; the item constructor and manager item_manager.cpp. See item.h and docs/notes/items_spells.md.
#include "game/items/item.h"

#include "engine/core/base_manager.h"
#include "engine/core/core_streams.h"
#include "engine/core/random.h"
#include "game/spells/spell.h"
#include "game/spells/spell_db.h"
#include "game/world/world.h"

#include "lib/float_select.h"

#include <array>
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

void Item::Create() {
    count = 1;
    spell.Reset();
}

void Item::Destroy() {
    spell.Reset();
    serverLink.Reset();
    turn.Reset();
}

bool Item::RunTick() {
    if (charge == chargeMax) return false;
    if (chargeRate <= 0.0f) {
        charge = chargeMax;
        return true;
    }
    charge = chargeRate * chargeMax + charge;
    charge = MinKeepA(charge, chargeMax);
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
    if (condition != other->condition || conditionMax != other->conditionMax ||
        charge != other->charge || chargeMax != other->chargeMax || weight != other->weight ||
        price != other->price || unreadValue != other->unreadValue)
        return false;
    if (magicLevel != other->magicLevel) return false;
    const Spell* currentSpell = spell;
    const Spell* otherSpell = other->spell;
    if (currentSpell == nullptr || otherSpell == nullptr) return currentSpell == otherSpell;
    if (currentSpell->prototypeId != otherSpell->prototypeId) return false;
    return currentSpell->modifiers == otherSpell->modifiers;
}

bool Item::CopyTo(Item* target, SpellManager* spells) {
    if (target->type != type) return false;
    target->Destroy();
    target->prototypeId = prototypeId;
    // The floats bit for bit (a loaded item's values may be any bit pattern).
    target->condition = condition;
    target->conditionMax = conditionMax;
    target->charge = charge;
    target->chargeMax = chargeMax;
    target->chargeRate = chargeRate;
    target->weight = weight;
    target->price = price;
    target->unreadValue = unreadValue;
    target->magicLevel = magicLevel;
    target->spell.Assign(spell ? spell->Clone(spells) : nullptr);
    target->count = 1;
    return true;
}

Item* Item::Clone(SpellManager* spells) {
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
    SpellManager* spells = GetManagerContext()->GetSpellManager();
    const int spellId = stream->ReadId();
    Spell* newSpell = spellId == kNoObjectId ? nullptr : spells->FindObjectAs<Spell>(static_cast<unsigned>(spellId));
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
    stream->WriteId(spell);
    stream->WriteValue(count);
    stream->WriteValue(magicLevel);
}

ItemRecordHead* Item::GetPrototype() {
    if (Kind() == ItemKind::Scroll) return nullptr;
    return PrototypeAt(Kind(), PrototypeIndex(prototypeId));
}

bool Item::HasPrototype() { return PrototypeAt(Kind(), PrototypeIndex(prototypeId)) != nullptr; }

void Item::RequirePrototype() {
    if (HasPrototype()) return;
    game::FatalExit("Item %d of kind 0x%X names prototype %u, which the item database does not have: the save or "
                    "the other player uses another database.",
                    id, static_cast<unsigned>(type), PrototypeIndex(prototypeId));
}

QuickItemCategory Item::GetCategory() {
    const ItemRecordHead* prototype = GetPrototype();
    return prototype ? static_cast<QuickItemCategory>(prototype->category) : QuickItemCategory::NoPrototype;
}

void Item::AttachSpell(Spell* newSpell) {
    if (Spell* previous = spell) price = price - previous->price;
    spell.Assign(newSpell);
    if (spell) price = spell->price + price;
}

LockKey Item::GetUse() {
    if (const ItemQuest* quest = AsQuest()) return LockKey{8, quest->questItemId, 0};
    return LockKey{0, 0, 0};
}

// --- ItemLink -----------------------------------------------------------------------------------

ItemLink* ItemLink::Clone() {
    auto* copy = new ItemLink();
    copy->value = value;
    return copy;
}

// --- ItemWeapon ---------------------------------------------------------------------------------

const char* ItemWeapon::ClassName() { return "CItemWeapon"; }

ClassId ItemWeapon::GetClassId() { return ClassId::ItemWeapon; }

WeaponType ItemWeapon::GetType() {
    return static_cast<WeaponType>(WeaponOf(prototypeId)->type);
}

std::array<float, kDamageTypeCount> ItemWeapon::DamageTypes() {
    if (const WeaponRecord* prototype = WeaponOf(prototypeId)) return prototype->damageTypes;
    return {};
}

// A damage in [damage, damage + damageSpread); no draw without a spread.
float ItemWeapon::RollDamage() {
    if (damageSpread == 0.0f) return damage;
    return g_random.Range(damage, damageSpread + damage);
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
    ItemWeapon* other = item->AsWeapon();   // of this one's type (the base's test)
    if (other->actionValue != actionValue) return false;
    if (other->damage != damage) return false;
    if (other->damageSpread != damageSpread) return false;
    if (other->reach != reach) return false;
    return true;
}

bool ItemWeapon::CopyTo(Item* target, SpellManager* spells) {
    if (!Item::CopyTo(target, spells)) return false;
    ItemWeapon* weapon = target->AsWeapon();   // a copy of this weapon
    weapon->actionValue = actionValue;
    weapon->damage = damage;
    weapon->damageSpread = damageSpread;
    weapon->reach = reach;
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

ArmorType ItemArmor::GetType() {
    return static_cast<ArmorType>(ArmorAt(PrototypeIndex(prototypeId))->type);
}

void ItemArmor::InitFromRecord(const ItemRecordHead* record) {
    Item::InitFromRecord(record);
    auto* r = RecordAs<ArmorRecord>(record);
    absorb[0] = r->absorbFirst;
    absorb[1] = r->absorbOthers;
}

bool ItemArmor::IsSameAs(Item* item) {
    if (!Item::IsSameAs(item)) return false;
    auto* other = item->AsArmor();
    // Both absorption rows and the layer masks, compared by their bits.
    return std::memcmp(&absorb, &other->absorb, sizeof(absorb)) == 0 &&
           std::memcmp(layerMask, other->layerMask, sizeof(layerMask)) == 0;
}

bool ItemArmor::CopyTo(Item* target, SpellManager* spells) {
    if (!Item::CopyTo(target, spells)) return false;
    auto* armor = target->AsArmor();
    armor->absorb = absorb;
    armor->layerMask[0] = layerMask[0];
    armor->layerMask[1] = layerMask[1];
    return true;
}

void ItemArmor::Load(ByteStream* stream) {
    Item::Load(stream);
    stream->ReadValue(absorb);
}

void ItemArmor::Save(ByteStream* stream) {
    Item::Save(stream);
    stream->WriteValue(absorb);
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
    return other->AsQuick()->actionValue == actionValue;
}

bool ItemQuick::CopyTo(Item* target, SpellManager* spells) {
    if (!Item::CopyTo(target, spells)) return false;
    target->AsQuick()->actionValue = actionValue;
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
    return other->AsQuest()->questItemId == questItemId;
}

bool ItemQuest::CopyTo(Item* target, SpellManager* spells) {
    if (!Item::CopyTo(target, spells)) return false;
    target->AsQuest()->questItemId = questItemId;
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

// --- ItemLoot -----------------------------------------------------------------------------------

const char* ItemLoot::ClassName() { return "CItemLoot"; }

ClassId ItemLoot::GetClassId() { return ClassId::ItemLoot; }

bool ItemLoot::HasRecord() const {
    switch (kind) {
    case LootKind::Self: return true;
    case LootKind::Material: return MaterialAt(index) != nullptr;
    case LootKind::Record: break;
    default: return false;
    }
    switch (recordType) {
    case LootRecordType::Weapon: return WeaponAt(index) != nullptr;
    case LootRecordType::Armor: return ArmorAt(index) != nullptr;
    case LootRecordType::Quick: return QuickItemAt(index) != nullptr;
    case LootRecordType::Spell: return SpellAt(index) != nullptr;
    case LootRecordType::Modifier: return SpellModifierAt(index) != nullptr;
    default: return false;
    }
}

namespace {

// A record's price; -1 without the record, as for a kind without a price.
template <typename Record>
float PriceOf(const Record* record) {
    return record ? record->price : -1.0f;
}

} // namespace

float ItemLoot::GetPrice() {
    switch (kind) {
    case LootKind::Self: return price;
    case LootKind::Material: {
        const MaterialRecordHead* material = MaterialAt(index);
        return material ? material->unitPrice : -1.0f;
    }
    case LootKind::Record: break;
    default: return -1.0f;
    }
    switch (recordType) {
    case LootRecordType::Weapon: return PriceOf(WeaponAt(index));
    case LootRecordType::Armor: return PriceOf(ArmorAt(index));
    case LootRecordType::Quick: return PriceOf(QuickItemAt(index));
    case LootRecordType::Spell: return PriceOf(SpellAt(index));
    case LootRecordType::Modifier: return PriceOf(SpellModifierAt(index));
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
    auto* other = item->AsLoot();
    if (other->kind != kind || other->recordType != recordType) return false;
    return other->index == index;
}

bool ItemLoot::CopyTo(Item* target, SpellManager* spells) {
    if (!Item::CopyTo(target, spells)) return false;
    auto* loot = target->AsLoot();
    loot->kind = kind;
    loot->recordType = recordType;
    loot->index = index;
    return true;
}

void ItemLoot::Load(ByteStream* stream) {
    Item::Load(stream);
    stream->ReadValue(kind);
    stream->ReadValue(recordType);
    stream->ReadValue(index);
}

void ItemLoot::Save(ByteStream* stream) {
    Item::Save(stream);
    stream->WriteValue(kind);
    stream->WriteValue(recordType);
    stream->WriteValue(index);
}

// --- Weapon and armour stats by packed id / index ------------------------------------------------

WeaponType WeaponTypeOf(std::uint32_t packedId) {
    const WeaponRecord* weapon = WeaponOf(packedId);
    return weapon ? static_cast<WeaponType>(weapon->type) : WeaponType::NoWeapon;
}

float WeaponMinDamage(std::uint32_t packedId) {
    const WeaponRecord* weapon = WeaponOf(packedId);
    const MaterialRecordHead* material = MaterialOf(packedId);
    return weapon && material ? weapon->damage * material->damageFactor : 0.0f;
}

float WeaponMaxDamage(std::uint32_t packedId) {
    const WeaponRecord* weapon = WeaponOf(packedId);
    const MaterialRecordHead* material = MaterialOf(packedId);
    return weapon && material ? (weapon->damageSpread + weapon->damage) * material->damageFactor : 0.0f;
}

float WeaponWeight(std::uint32_t packedId) {
    const WeaponRecord* weapon = WeaponOf(packedId);
    const MaterialRecordHead* material = MaterialOf(packedId);
    return weapon && material ? weapon->weight * material->weightFactor : 0.0f;
}

int WeaponDamageType(std::uint32_t packedId) {
    if (const WeaponRecord* weapon = WeaponOf(packedId)) {
        for (int i = 0; i < kDamageTypeCount; ++i) {
            if (weapon->damageTypes[i] != 0.0f) return i;
        }
    }
    return kDamageTypeCount - 1;
}

float WeaponAttackBonus(std::uint32_t packedId) {
    const WeaponRecord* weapon = WeaponOf(packedId);
    return weapon ? weapon->attackBonus : 0.0f;
}

float WeaponDefenceBonus(std::uint32_t packedId) {
    const WeaponRecord* weapon = WeaponOf(packedId);
    return weapon ? weapon->defenceBonus : 0.0f;
}

int ArmorLayer(std::uint32_t index) {
    ArmorRecord* armor = ArmorAt(index);
    return armor ? static_cast<int>(armor->layer) : 0;
}
