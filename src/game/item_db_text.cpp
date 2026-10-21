#include "item_db_text.h"

#include "ei/hook.h"
#include "resource.h"
#include "spell.h"

#include "lib/crt.h"
#include "originals_engine.h"
#include "text_split.h"
#include "game_data.h"

#include <cstdint>

using namespace itemdb;

#pragma warning(disable : 4740) // naked forwarders


namespace {

// The keys' prefixes and the kind texts (0078DD04..0078DE70).
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
constexpr const char kNoMaterial[] = "none";   // 007846F0
constexpr const char kUnknown[] = "Unknown";
constexpr const char kColon[] = ": ";
constexpr const char kLineEnd[] = "\r\n";

// The text lookup the original inlines (or calls as 004DEBC0): the first archive that has `key`;
// the archive writes the text's size to length_.
const char* FindText(CTextManager* self, const char* key) {
    for (CTextManager::SourceNode* node = self->sources_.head->next; node != self->sources_.head; node = node->next) {
        // The file's data or null.
        if (const char* text = static_cast<const char*>(node->data.source->GetFile(key, &self->length_))) return text;
    }
    return nullptr;
}

// Clears the current text, looks up `key` and makes it current. True when it was found and is
// not empty (length compared unsigned).
bool OpenText(CTextManager* self, const char* key) {
    if (self->text_) {
        self->text_ = nullptr;
        self->cursor_ = nullptr;
    }
    const char* text = FindText(self, key);
    self->cursor_ = text;
    self->text_ = text;
    return text && self->length_ != 0;
}

// A CString field of a database record that may be null (the original then faults reading it).
const mfc::CString& StringAt(const char* record, int offset) {
    return *reinterpret_cast<const mfc::CString*>(reinterpret_cast<std::uintptr_t>(record) + offset);
}

// The record's name as used in keys: spaces become '_', lower case.
void KeyName(mfc::CString& out, const char* record, int offset) {
    out = StringAt(record, offset);
    text::ReplaceChar(&out, ' ', '_');
    out.MakeLower();
}

// Looks up `key`; when found, reads the name line and the description.
void ReadItemText(CTextManager* self, const mfc::CString& key, mfc::CString* name, mfc::CString* description) {
    if (OpenText(self, key)) {
        self->ReadLine(name, 1);
        self->ReadToHash(description, 1);
    }
}

// Armor types 0-2 are worn on the upper body.
bool IsUpperArmor(const char* armor) {
    int type = reinterpret_cast<const ItemRecordHead*>(armor)->type;
    return type == 0 || type == 1 || type == 2;
}

} // namespace

BOOL CTextManager::IsPastEnd(const char* p) {
    // SUB; CMP; SETGE: the distance from the start compared signed with the length.
    int offset = static_cast<int>(reinterpret_cast<std::uintptr_t>(p) - reinterpret_cast<std::uintptr_t>(text_));
    return offset >= static_cast<int>(length_);
}
EI_REPLACE_METHOD(0x004E0000, CTextManager::IsPastEnd);

BOOL CTextManager::GetString(const char* name, mfc::CString* out) {
    if (cursor_) {
        text_ = nullptr;
        cursor_ = nullptr;
    }
    bool found;
    {
        mfc::CString key = mfc::CString(kString) + name;
        found = OpenText(this, key);
    }
    if (!found) {
        // `name` may be out's own buffer (callers pass a CString's text and the CString).
        if (static_cast<const char*>(*out) != name) *out = name;
        text_ = nullptr;
        cursor_ = nullptr;
        return 0;
    }
    ReadToHash(out, 1);
    out->TrimRight(kLineEnd);
    text_ = nullptr;
    cursor_ = nullptr;
    return 1;
}
EI_REPLACE_METHOD(0x004E0020, CTextManager::GetString);

BOOL CTextManager::GetItemTexts(CItem* item, mfc::CString* name, mfc::CString* description, mfc::CString* kind) {
    std::uint32_t materialIndex = MaterialIndex(item->prototypeId_);
    std::uint32_t prototypeIndex = PrototypeIndex(item->prototypeId_);
    mfc::CString recordName;   // ESP+0x10 in the original
    mfc::CString materialName; // ESP+0x14
    name->Empty();
    description->Empty();
    kind->Empty();

    const char* kindKey = nullptr;
    switch (item->type_) {
    case kItemWeapon: {
        KeyName(materialName, Materials().RecordAt(materialIndex, kMaterialSize), 0);
        KeyName(recordName, Weapons().RecordAt(prototypeIndex, kWeaponSize), 4);
        ReadItemText(this, mfc::CString(kWeapon) + recordName + kSpace + materialName, name, description);
        kindKey = "item_weapon";
        break;
    }
    case kItemArmor: {
        KeyName(materialName, Materials().RecordAt(materialIndex, kMaterialSize), 0);
        const char* armor = Armors().RecordAt(prototypeIndex, kArmorSize);
        KeyName(recordName, armor, 4);
        ReadItemText(this, mfc::CString(kArmor) + recordName + kSpace + materialName, name, description);
        kindKey = IsUpperArmor(armor) ? "item_armor_upper" : "item_armor_lower";
        break;
    }
    case kItemQuick: {
        KeyName(recordName, QuickItems().RecordAt(prototypeIndex, kQuickItemSize), 4);
        // A quick item whose prototype has no material (+0x10 "none") is a potion.
        char* prototype = item->GetPrototype();
        if (prototype && gamecrt::mbsicmp(static_cast<const char*>(
                                             reinterpret_cast<const ItemRecordHead*>(item->GetPrototype())->materialClass),
                                         kNoMaterial) == 0) {
            ReadItemText(this, mfc::CString(kQuickItem) + recordName, name, description);
            kindKey = "item_potion";
            break;
        }
        KeyName(materialName, Materials().RecordAt(materialIndex, kMaterialSize), 0);
        ReadItemText(this, mfc::CString(kQuickItem) + recordName + kSpace + materialName, name, description);
        // Only wands (category 5) get a kind; their name is followed by the spell's name.
        if (item->GetCategory() != 5) break;
        if (CSpell* spell = item->spell_) {
            mfc::CString spellName(kUnknown);
            bool found;
            {
                mfc::CString key = mfc::CString(kSpell) + StringAt(spell->GetPrototype(), 4);
                found = OpenText(this, key);
            }
            if (found) ReadLine(&spellName, 1);
            *name += kColon + spellName;
        }
        kindKey = "item_wand";
        break;
    }
    case kItemLoot: {
        auto* loot = static_cast<CItemLoot*>(item);
        if (loot->kind_ == 0) {
            KeyName(recordName, Loot().RecordAt(prototypeIndex, kLootSize), 4);
            ReadItemText(this, mfc::CString(kLootItem) + recordName, name, description);
            kindKey = "item_loot";
        } else if (loot->kind_ == 1) {
            KeyName(materialName, Materials().RecordAt(loot->index_, kMaterialSize), 0);
            ReadItemText(this, mfc::CString(kMaterial) + materialName, name, description);
            kindKey = "item_material";
        } else if (loot->kind_ == 2) {
            switch (loot->recordType_) {
            case 0:
                KeyName(recordName, Weapons().RecordAt(loot->index_, kWeaponSize), 4);
                ReadItemText(this, mfc::CString(kInstruction) + recordName, name, description);
                kindKey = "item_weapon_shape";
                break;
            case 1: {
                const char* armor = Armors().RecordAt(loot->index_, kArmorSize);
                KeyName(recordName, armor, 4);
                ReadItemText(this, mfc::CString(kInstruction) + recordName, name, description);
                kindKey = IsUpperArmor(armor) ? "item_armor_shape_upper" : "item_armor_shape_lower";
                break;
            }
            case 2:
                KeyName(recordName, QuickItems().RecordAt(loot->index_, kQuickItemSize), 4);
                ReadItemText(this, mfc::CString(kInstruction) + recordName, name, description);
                kindKey = "item_wand_shape";
                break;
            case 3:
                ReadItemText(this, mfc::CString(kSpell) + StringAt(Spells().RecordAt(loot->index_, kSpellSize), 4), name,
                             description);
                kindKey = "item_prototype";
                break;
            case 4: {
                // The modifier's two-letter code is inline in the record (+0x04).
                const char* code = reinterpret_cast<const char*>(
                    reinterpret_cast<std::uintptr_t>(SpellModifiers().RecordAt(loot->index_, kSpellModifierSize)) + 4);
                ReadItemText(this, mfc::CString(kModifier) + code, name, description);
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
        ReadItemText(this, mfc::CString(kSpell) + StringAt(spell->GetPrototype(), 4), name, description);
        kindKey = "item_spell";
        break;
    }
    case kItemQuest: {
        // 0x3009 is the quest item ("questitem"); its table (007C8758) is Loot in item_db.h.
        KeyName(recordName, QuestItems().RecordAt(prototypeIndex, kQuestItemSize), 4);
        ReadItemText(this, mfc::CString(kQuestItem) + recordName, name, description);
        kindKey = "item_quest";
        break;
    }
    default: break;
    }
    if (kindKey) GetString(kindKey, kind);
    return 1;
}
EI_REWRITTEN(0x004E0150, CTextManager::GetItemTexts);

BOOL CTextManager::FormatItemName(CItem* item, mfc::CString* out) {
    mfc::CString name;
    mfc::CString format;   // first the description, then the format text
    mfc::CString kind;
    if (!GetItemTexts(item, &name, &format, &kind)) return 0;
    if (static_cast<int>(item->count_) > 1) {
        GetString("format_item2", &format);
        out->Format(format, static_cast<const char*>(kind), static_cast<const char*>(name), item->count_);
    } else {
        GetString("format_item1", &format);
        out->Format(format, static_cast<const char*>(kind), static_cast<const char*>(name));
    }
    return 1;
}
EI_REWRITTEN(0x004E13E0, CTextManager::FormatItemName);

void TextSources::Begin(Node** out) { *out = head->next; }
EI_REPLACE_METHOD(0x004E1530, TextSources::Begin);
// Byte-identical copies (other lists' begin in the world range; A-W12).
EI_REPLACE_METHOD(0x00560030, TextSources::Begin);
EI_REPLACE_METHOD(0x00560040, TextSources::Begin);
EI_REPLACE_METHOD(0x0056DF90, TextSources::Begin);
EI_REPLACE_METHOD(0x0056DFE0, TextSources::Begin);

TextSources::Node** TextSources::Erase(Node** out, Node* pos) {
    Node* next = pos->next;
    Node* prev = pos->prev;
    prev->next = next;
    next->prev = prev;
    pos->data.name.~string();   // 004DEAB0: the storage freed
    if (pos) {   // the original checks only here, after using the node (always true)
        // The inlined STLport node deallocation: push on the 0x18-byte free list.
        auto*& freeList = ei::GameData<Node*>(0x0079B22C);
        pos->next = freeList;
        freeList = pos;
    }
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
EI_REPLACE_METHOD(0x004E1540, TextSourceIterator::CopyTo);
// Byte-identical copies (other list iterators' copy in the interface; A-I15).
EI_REPLACE_METHOD(0x005E2470, TextSourceIterator::CopyTo);
EI_REPLACE_METHOD(0x005F97D0, TextSourceIterator::CopyTo);
EI_REPLACE_METHOD(0x00609A30, TextSourceIterator::CopyTo);
EI_REPLACE_METHOD(0x00609A70, TextSourceIterator::CopyTo);
EI_REPLACE_METHOD(0x0062C080, TextSourceIterator::CopyTo);
EI_REPLACE_METHOD(0x00539210, TextSourceIterator::CopyTo);   // A-U11
EI_REPLACE_METHOD(0x00560050, TextSourceIterator::CopyTo);   // A-W12
EI_REPLACE_METHOD(0x0056DEC0, TextSourceIterator::CopyTo);
EI_REPLACE_METHOD(0x0056DFA0, TextSourceIterator::CopyTo);
EI_REPLACE_METHOD(0x0056DFF0, TextSourceIterator::CopyTo);
