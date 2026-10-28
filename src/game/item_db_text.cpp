#include "item_db_text.h"

#include "ei/hook.h"
#include "resource.h"
#include "spell.h"

#include "lib/crt.h"
#include "text_split.h"
#include "lib/str.h"
#include "game_data.h"

#include <cstdint>
#include <string>

using namespace itemdb;


namespace {

constexpr const char kString[] = "string ";
constexpr const char kWeapon[] = "weapon ";
constexpr const char kArmor[] = "armor ";
constexpr const char kQuickItem[] = "qitem ";
constexpr const char kLootItem[] = "litem ";
constexpr const char kQuestItem[] = "questitem ";
constexpr const char kMaterial[] = "material ";
constexpr const char kInstruction[] = "instr ";
constexpr const char kSpell[] = "spell ";
constexpr const char kModifier[] = "modifier ";
constexpr const char kSpace[] = " ";
constexpr const char kNoMaterial[] = "none";
constexpr const char kUnknown[] = "Unknown";
constexpr const char kColon[] = ": ";
constexpr const char kLineEnd[] = "\r\n";

// Clears the current text, looks up `key` and makes it current. True when it was found and is not empty
// (CTextManager::Open inlined).
bool OpenText(CTextManager* self, const char* key) { return self->Open(key) != 0; }

// The record's name as used in keys: spaces become '_', lower case. `name` is a string field of a database record
// that may be null (reading it then faults).
void KeyName(std::string& out, const std::string& name) {
    out = name;
    text::ReplaceChar(&out, ' ', '_');
    str::MakeLower(out);
}

// Looks up `key`; when found, reads the name line and the description.
void ReadItemText(CTextManager* self, const std::string& key, std::string* name, std::string* description) {
    if (OpenText(self, key.c_str())) {
        self->ReadLine(name, 1);
        self->ReadToHash(description, 1);
    }
}

// Armor types 0-2 are worn on the upper body.
bool IsUpperArmor(const ArmorRecord* armor) {
    int type = armor->type;
    return type == 0 || type == 1 || type == 2;
}

} // namespace

BOOL CTextManager::IsPastEnd(const char* p) {
    // SUB; CMP; SETGE: the distance from the start compared signed with the length.
    int offset = static_cast<int>(p - text_);
    return offset >= static_cast<int>(length_);
}
EI_REWRITTEN(0x004E0000, CTextManager::IsPastEnd);

BOOL CTextManager::GetString(const char* name, std::string* out) {
    if (cursor_) {
        text_ = nullptr;
        cursor_ = nullptr;
    }
    bool found;
    {
        std::string key = std::string(kString) + name;
        found = OpenText(this, key.c_str());
    }
    if (!found) {
        // `name` may be out's own buffer (callers pass a string's text and the string).
        if (out->c_str() != name) *out = name;
        text_ = nullptr;
        cursor_ = nullptr;
        return 0;
    }
    ReadToHash(out, 1);
    str::TrimRight(*out, kLineEnd);
    text_ = nullptr;
    cursor_ = nullptr;
    return 1;
}
EI_REWRITTEN(0x004E0020, CTextManager::GetString);

BOOL CTextManager::GetItemTexts(CItem* item, std::string* name, std::string* description, std::string* kind) {
    std::uint32_t materialIndex = MaterialIndex(item->prototypeId_);
    std::uint32_t prototypeIndex = PrototypeIndex(item->prototypeId_);
    std::string recordName;
    std::string materialName;
    name->clear();
    description->clear();
    kind->clear();

    const char* kindKey = nullptr;
    switch (item->type_) {
    case kItemWeapon: {
        KeyName(materialName, MaterialAt(materialIndex)->key);
        KeyName(recordName, WeaponAt(prototypeIndex)->text04);
        ReadItemText(this, std::string(kWeapon) + recordName + kSpace + materialName, name, description);
        kindKey = "item_weapon";
        break;
    }
    case kItemArmor: {
        KeyName(materialName, MaterialAt(materialIndex)->key);
        const ArmorRecord* armor = ArmorAt(prototypeIndex);
        KeyName(recordName, armor->text04);
        ReadItemText(this, std::string(kArmor) + recordName + kSpace + materialName, name, description);
        kindKey = IsUpperArmor(armor) ? "item_armor_upper" : "item_armor_lower";
        break;
    }
    case kItemQuick: {
        KeyName(recordName, QuickItemAt(prototypeIndex)->text04);
        // A quick item whose prototype has no material (+0x10 "none") is a potion.
        const ItemRecordHead* prototype = item->GetPrototype();
        if (prototype && gamecrt::mbsicmp(prototype->materialClass,
                                         kNoMaterial) == 0) {
            ReadItemText(this, std::string(kQuickItem) + recordName, name, description);
            kindKey = "item_potion";
            break;
        }
        KeyName(materialName, MaterialAt(materialIndex)->key);
        ReadItemText(this, std::string(kQuickItem) + recordName + kSpace + materialName, name, description);
        // Only wands (category 5) get a kind; their name is followed by the spell's name.
        if (item->GetCategory() != 5) break;
        if (CSpell* spell = item->spell_) {
            std::string spellName(kUnknown);
            bool found;
            {
                std::string key = std::string(kSpell) + spell->GetPrototype()->code;
                found = OpenText(this, key.c_str());
            }
            if (found) ReadLine(&spellName, 1);
            *name += kColon + spellName;
        }
        kindKey = "item_wand";
        break;
    }
    case kItemLoot: {
        auto* loot = AsLoot(item);
        if (loot->kind_ == 0) {
            KeyName(recordName, LootAt(prototypeIndex)->text04);
            ReadItemText(this, std::string(kLootItem) + recordName, name, description);
            kindKey = "item_loot";
        } else if (loot->kind_ == 1) {
            KeyName(materialName, MaterialAt(loot->index_)->key);
            ReadItemText(this, std::string(kMaterial) + materialName, name, description);
            kindKey = "item_material";
        } else if (loot->kind_ == 2) {
            switch (loot->recordType_) {
            case 0:
                KeyName(recordName, WeaponAt(loot->index_)->text04);
                ReadItemText(this, std::string(kInstruction) + recordName, name, description);
                kindKey = "item_weapon_shape";
                break;
            case 1: {
                const ArmorRecord* armor = ArmorAt(loot->index_);
                KeyName(recordName, armor->text04);
                ReadItemText(this, std::string(kInstruction) + recordName, name, description);
                kindKey = IsUpperArmor(armor) ? "item_armor_shape_upper" : "item_armor_shape_lower";
                break;
            }
            case 2:
                KeyName(recordName, QuickItemAt(loot->index_)->text04);
                ReadItemText(this, std::string(kInstruction) + recordName, name, description);
                kindKey = "item_wand_shape";
                break;
            case 3:
                ReadItemText(this, std::string(kSpell) + SpellAt(loot->index_)->code, name,
                             description);
                kindKey = "item_prototype";
                break;
            case 4: {
                // The modifier's two-letter code is inline in the record (+0x04).
                ReadItemText(this, std::string(kModifier) + SpellModifierAt(loot->index_)->code, name, description);
                kindKey = "item_modifier";
                break;
            }
            default: break;
            }
        }
        break;
    }
    case kItemSpell: {
        CSpell* spell = item->spell_;
        if (!spell) break;
        ReadItemText(this, std::string(kSpell) + spell->GetPrototype()->code, name, description);
        kindKey = "item_spell";
        break;
    }
    case kItemQuest: {
        // 0x3009 is the quest item ("questitem"); its table is Loot in item_db.h.
        KeyName(recordName, QuestItemAt(prototypeIndex)->text04);
        ReadItemText(this, std::string(kQuestItem) + recordName, name, description);
        kindKey = "item_quest";
        break;
    }
    default: break;
    }
    if (kindKey) GetString(kindKey, kind);
    return 1;
}
EI_REWRITTEN(0x004E0150, CTextManager::GetItemTexts);

BOOL CTextManager::FormatItemName(CItem* item, std::string* out) {
    std::string name;
    std::string format;   // first the description, then the format text
    std::string kind;
    if (!GetItemTexts(item, &name, &format, &kind)) return 0;
    if (static_cast<int>(item->count_) > 1) {
        GetString("format_item2", &format);
        *out = str::Format(format.c_str(), kind.c_str(), name.c_str(), item->count_);
    } else {
        GetString("format_item1", &format);
        *out = str::Format(format.c_str(), kind.c_str(), name.c_str());
    }
    return 1;
}
EI_REWRITTEN(0x004E13E0, CTextManager::FormatItemName);

void TextSources::Begin(Node** out) { *out = head->next; }
EI_REWRITTEN(0x004E1530, TextSources::Begin);
EI_REWRITTEN(0x00560030, TextSources::Begin);
EI_REWRITTEN(0x00560040, TextSources::Begin);
EI_REWRITTEN(0x0056DF90, TextSources::Begin);
EI_REWRITTEN(0x0056DFE0, TextSources::Begin);

TextSources::Node** TextSources::Erase(Node** out, Node* pos) {
    Node* next = pos->next;
    Node* prev = pos->prev;
    prev->next = next;
    next->prev = prev;
    pos->data.name.~string();   // the storage freed
    if (pos) libcrt::NodeDeallocate(pos, 0x18);   // checked only here, after using the node (always true)
    *out = next;
    return out;
}
EI_REWRITTEN(0x004E1550, TextSources::Erase);

TextSources::Node* TextSources::CreateNode(const TextSource* value) {
    return stlp::list<TextSource>::CreateNode(*value);
}
EI_REWRITTEN(0x004E15C0, TextSources::CreateNode);

TextSources::Node** TextSources::Insert(Node** out, Node* pos, const TextSource* value) {
    Node* node = CreateNode(value);
    node->next = pos;
    node->prev = pos->prev;
    pos->prev->next = node;
    pos->prev = node;
    *out = node;
    return out;
}
EI_REWRITTEN(0x004E1590, TextSources::Insert);

CTextManager::SourceNode** TextSourceIterator::CopyTo(CTextManager::SourceNode** out) {
    *out = node;
    return out;
}
EI_REWRITTEN(0x004E1540, TextSourceIterator::CopyTo);
// Byte-identical copies (other list iterators' copy in the interface; A-I15).
EI_REWRITTEN(0x005E2470, TextSourceIterator::CopyTo);
EI_REWRITTEN(0x005F97D0, TextSourceIterator::CopyTo);
EI_REWRITTEN(0x00609A30, TextSourceIterator::CopyTo);
EI_REWRITTEN(0x00609A70, TextSourceIterator::CopyTo);
EI_REWRITTEN(0x0062C080, TextSourceIterator::CopyTo);
EI_REWRITTEN(0x00539210, TextSourceIterator::CopyTo);
EI_REWRITTEN(0x00560050, TextSourceIterator::CopyTo);   // A-W12
EI_REWRITTEN(0x0056DEC0, TextSourceIterator::CopyTo);
EI_REWRITTEN(0x0056DFA0, TextSourceIterator::CopyTo);
EI_REWRITTEN(0x0056DFF0, TextSourceIterator::CopyTo);
