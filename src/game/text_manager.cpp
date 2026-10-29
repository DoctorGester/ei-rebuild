// The text manager's string lookup and the item texts (names, descriptions, kinds). The line reader is
// text_reader.cpp, the archive list text_sources.cpp. See text_manager.h.
#include "text_manager.h"

#include "item.h"
#include "resource.h"
#include "spell.h"
#include "text_split.h"

#include "lib/str.h"
#include "lib/text_case.h"

#include <cstdint>
#include <string>

using namespace itemdb;

namespace {

// The record's name as used in keys: spaces become '_', lower case. `name` is a string field of a database record
// that may be null (reading it then faults).
std::string KeyName(const std::string& name) {
    std::string key = name;
    text::ReplaceChar(&key, ' ', '_');
    str::MakeLower(key);
    return key;
}

// Looks up `key`; when found, reads the name line and the description.
void ReadItemText(CTextManager* texts, const std::string& key, ItemTexts* out) {
    if (texts->Open(key)) {
        texts->ReadLine(&out->name, 1);
        texts->ReadToHash(&out->description);
    }
}

// The text key and kind key of a loot item standing for a record.
void RecordKeys(const CItemLoot* loot, std::string* key, const char** kindKey) {
    switch (loot->recordType_) {
    case LootRecordType::Weapon:
        *key = "instr " + KeyName(WeaponAt(loot->index_)->name);
        *kindKey = "item_weapon_shape";
        break;
    case LootRecordType::Armor: {
        const ArmorRecord* armor = ArmorAt(loot->index_);
        *key = "instr " + KeyName(armor->name);
        *kindKey = IsUpperBodyArmor(armor->type) ? "item_armor_shape_upper" : "item_armor_shape_lower";
        break;
    }
    case LootRecordType::Quick:
        *key = "instr " + KeyName(QuickItemAt(loot->index_)->name);
        *kindKey = "item_wand_shape";
        break;
    case LootRecordType::Spell:
        *key = std::string("spell ") + SpellAt(loot->index_)->code;
        *kindKey = "item_prototype";
        break;
    case LootRecordType::Modifier:
        *key = std::string("modifier ") + SpellModifierAt(loot->index_)->code;
        *kindKey = "item_modifier";
        break;
    default: break;
    }
}

} // namespace

bool CTextManager::IsPastEnd(const char* p) {
    int offset = static_cast<int>(p - text_);
    return offset >= static_cast<int>(length_);
}

bool CTextManager::GetString(std::string_view name, std::string* out) {
    if (cursor_) {
        text_ = nullptr;
        cursor_ = nullptr;
    }
    bool found = Open("string " + std::string(name));
    if (found) {
        ReadToHash(out);
        str::TrimRight(*out, "\r\n");
    } else if (out->data() != name.data()) {
        // `name` may be out's own text (callers pass a string and its address).
        *out = name;
    }
    text_ = nullptr;
    cursor_ = nullptr;
    return found;
}

ItemTexts CTextManager::GetItemTexts(CItem* item) {
    std::uint32_t materialIndex = MaterialIndex(item->prototypeId_);
    std::uint32_t prototypeIndex = PrototypeIndex(item->prototypeId_);
    std::string key;                 // the item's text; none: no name and description
    const char* kindKey = nullptr;   // the kind's string; none: no kind
    bool wand = false;
    switch (item->Kind()) {
    case ItemKind::Weapon:
        key = "weapon " + KeyName(WeaponAt(prototypeIndex)->name) + " " + KeyName(MaterialAt(materialIndex)->key);
        kindKey = "item_weapon";
        break;
    case ItemKind::Armor: {
        const ArmorRecord* armor = ArmorAt(prototypeIndex);
        key = "armor " + KeyName(armor->name) + " " + KeyName(MaterialAt(materialIndex)->key);
        kindKey = IsUpperBodyArmor(armor->type) ? "item_armor_upper" : "item_armor_lower";
        break;
    }
    case ItemKind::Quick: {
        key = "qitem " + KeyName(QuickItemAt(prototypeIndex)->name);
        // A quick item whose prototype has no material is a potion.
        const ItemRecordHead* prototype = item->GetPrototype();
        if (prototype && text::CompareIgnoringCase(prototype->materialClass, "none") == 0) {
            kindKey = "item_potion";
            break;
        }
        key += " " + KeyName(MaterialAt(materialIndex)->key);
        // Only wands get a kind; their name is followed by the spell's name.
        wand = item->GetCategory() == QuickItemCategory::Wand;
        if (wand) kindKey = "item_wand";
        break;
    }
    case ItemKind::Loot: {
        auto* loot = AsLoot(item);
        if (loot->kind_ == LootKind::Self) {
            key = "litem " + KeyName(LootAt(prototypeIndex)->name);
            kindKey = "item_loot";
        } else if (loot->kind_ == LootKind::Material) {
            key = "material " + KeyName(MaterialAt(loot->index_)->key);
            kindKey = "item_material";
        } else if (loot->kind_ == LootKind::Record) {
            RecordKeys(loot, &key, &kindKey);
        }
        break;
    }
    case ItemKind::Spell:
        if (CSpell* spell = item->spell_) {
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

    ItemTexts texts;
    if (!key.empty()) ReadItemText(this, key, &texts);
    if (wand) {
        if (CSpell* spell = item->spell_) {
            std::string spellName = "Unknown";
            if (Open(std::string("spell ") + spell->GetPrototype()->code)) ReadLine(&spellName, 1);
            texts.name += ": " + spellName;
        }
    }
    if (kindKey) GetString(kindKey, &texts.kind);
    return texts;
}

std::string CTextManager::FormatItemName(CItem* item) {
    ItemTexts texts = GetItemTexts(item);
    std::string format;
    if (static_cast<int>(item->count_) > 1) {
        GetString("format_item2", &format);
        return str::Format(format, texts.kind, texts.name, item->count_);
    }
    GetString("format_item1", &format);
    return str::Format(format, texts.kind, texts.name);
}
