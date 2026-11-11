// An item's texts: its name, description and kind from the text resources, and its display name. See item_texts.h.
#include "game/items/item_texts.h"

#include "engine/core/text_split.h"
#include "engine/data/text_manager.h"
#include "game/items/item.h"
#include "game/spells/spell.h"
#include "game/spells/spell_db.h"
#include "lib/str.h"

#include <string>

using namespace itemdb;

namespace {

// The record's name as used in keys: spaces become '_', lower case.
std::string KeyName(const std::string& name) {
    std::string key = name;
    text::ReplaceChar(&key, ' ', '_');
    str::MakeLower(key);
    return key;
}

// Looks up `key`; when found, reads the name line and the description.
void ReadItemText(TextManager* texts, const std::string& key, ItemTexts* out) {
    if (texts->Open(key)) {
        out->name = texts->ReadLine(true);
        out->description = texts->ReadToHash();
    }
}

// The text key and kind key of a loot item standing for a record (one with the record: ItemLoot::HasRecord).
void RecordKeys(const ItemLoot* loot, std::string* key, const char** kindKey) {
    switch (loot->recordType) {
    case LootRecordType::Weapon:
        *key = "instr " + KeyName(WeaponAt(loot->index)->name);
        *kindKey = "item_weapon_shape";
        break;
    case LootRecordType::Armor: {
        const ArmorRecord* armor = ArmorAt(loot->index);
        *key = "instr " + KeyName(armor->name);
        *kindKey = IsUpperBodyArmor(static_cast<ArmorType>(armor->type)) ? "item_armor_shape_upper" : "item_armor_shape_lower";
        break;
    }
    case LootRecordType::Quick:
        *key = "instr " + KeyName(QuickItemAt(loot->index)->name);
        *kindKey = "item_wand_shape";
        break;
    case LootRecordType::Spell:
        *key = std::string("spell ") + SpellAt(loot->index)->code;
        *kindKey = "item_prototype";
        break;
    case LootRecordType::Modifier:
        *key = std::string("modifier ") + SpellModifierAt(loot->index)->code;
        *kindKey = "item_modifier";
        break;
    default: break;
    }
}

} // namespace

// The item's prototype and its spell's are there (Item::HasPrototype, Spell::HasRecords); the material and the record a loot item stands for are
// tested: without one the item has no text key (no name and description).
ItemTexts GetItemTexts(TextManager* texts, Item* item) {
    const MaterialRecordHead* material = MaterialAt(MaterialIndex(item->prototypeId));
    std::uint32_t prototypeIndex = PrototypeIndex(item->prototypeId);
    std::string key;                 // the item's text; none: no name and description
    const char* kindKey = nullptr;   // the kind's string; none: no kind
    bool wand = false;
    switch (item->Kind()) {
    case ItemKind::Weapon:
        if (material) key = "weapon " + KeyName(WeaponAt(prototypeIndex)->name) + " " + KeyName(material->key);
        kindKey = "item_weapon";
        break;
    case ItemKind::Armor: {
        const ArmorRecord* armor = ArmorAt(prototypeIndex);
        if (material) key = "armor " + KeyName(armor->name) + " " + KeyName(material->key);
        kindKey = IsUpperBodyArmor(static_cast<ArmorType>(armor->type)) ? "item_armor_upper" : "item_armor_lower";
        break;
    }
    case ItemKind::Quick: {
        // A quick item whose prototype has no material is a potion.
        const ItemRecordHead* prototype = item->GetPrototype();
        if (!prototype->HasMaterial()) {
            key = "qitem " + KeyName(prototype->name);
            kindKey = "item_potion";
            break;
        }
        if (material) key = "qitem " + KeyName(prototype->name) + " " + KeyName(material->key);
        // Only wands get a kind; their name is followed by the spell's name.
        wand = item->GetCategory() == QuickItemCategory::Wand;
        if (wand) kindKey = "item_wand";
        break;
    }
    case ItemKind::Loot: {
        auto* loot = item->AsLoot();
        if (!loot->HasRecord()) break;
        if (loot->kind == LootKind::Self) {
            key = "litem " + KeyName(LootAt(prototypeIndex)->name);
            kindKey = "item_loot";
        } else if (loot->kind == LootKind::Material) {
            key = "material " + KeyName(MaterialAt(loot->index)->key);
            kindKey = "item_material";
        } else if (loot->kind == LootKind::Record) {
            RecordKeys(loot, &key, &kindKey);
        }
        break;
    }
    case ItemKind::Scroll:
        if (Spell* spell = item->spell) {
            key = std::string("spell ") + spell->GetPrototype()->code;
            kindKey = "item_spell";
        }
        break;
    case ItemKind::Quest:
        key = "questitem " + KeyName(QuestItemAt(prototypeIndex)->name);
        kindKey = "item_quest";
        break;
    default: break;
    }

    ItemTexts out;
    if (!key.empty()) ReadItemText(texts, key, &out);
    if (wand) {
        if (Spell* spell = item->spell) {
            std::string spellName = "Unknown";
            if (texts->Open(std::string("spell ") + spell->GetPrototype()->code)) spellName = texts->ReadLine(true);
            out.name += ": " + spellName;
        }
    }
    if (kindKey) out.kind = texts->GetString(kindKey);
    return out;
}

std::string FormatItemName(TextManager* texts, Item* item) {
    ItemTexts itemTexts = GetItemTexts(texts, item);
    if (static_cast<int>(item->count) > 1)
        return str::Format(texts->GetString("format_item2"), itemTexts.kind, itemTexts.name, item->count);
    return str::Format(texts->GetString("format_item1"), itemTexts.kind, itemTexts.name);
}
