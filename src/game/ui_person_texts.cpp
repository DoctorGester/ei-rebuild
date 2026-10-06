// The "pers <name>" texts of the battle and briefing screens (PersonTextMap, ui_camp.h): one line
// per person, by the text key of the person's name. Read by the battle screen's setup (inline
// there) and the briefing screen (00607720).
#include "ui_camp.h"

#include "item_db_text.h"
#include "object_tables.h"

#include "console.h"
#include "lib/mfc.h"
#include "originals_engine.h"
#include "originals_ui.h"

namespace {

const char* PersonPattern() { return reinterpret_cast<const char*>(0x00793504); }   // "pers *"
constexpr int kPersonPrefixLength = 5;                                               // "pers "

// What 00609E70 inserts: the key and the text.
struct PersonTextPair {
    int key;
    mfc::CString text;
};

PersonTextNode* LowerBound(const PersonTextMap* map, int key) {
    PersonTextNode* bound = map->header;
    for (PersonTextNode* node = map->header->parent; node;) {
        if (!(node->key < key)) {
            bound = node;
            node = node->left;
        } else {
            node = node->right;
        }
    }
    return bound;
}

} // namespace

void PersonTextMap::Load() {
    CTextManager* texts = GameTexts();
    console::CStringArray keys;
    mfc::CString line;
    TextsFindKeysOriginal(texts, nullptr, &keys, PersonPattern());
    for (int i = 0; i < keys.size_; ++i) {
        if (!TextsOpenSection(texts, nullptr, keys[i])) continue;
        TextsReadLineOriginal(texts, nullptr, &line, 1);
        int key;
        {
            mfc::CString name = console::Ops(keys[i]).Mid(kPersonPrefixLength);
            key = TextKeyFromName(name);
        }
        PersonTextNode* node = LowerBound(this, key);
        if (node == header || key < node->key) {
            mfc::CString empty;
            PersonTextPair pair{key, empty};
            PersonTextNode* inserted;
            node = *PersonMapInsertHintOriginal(this, nullptr, &inserted, node, &pair);
        }
        node->text = line;
    }
}
