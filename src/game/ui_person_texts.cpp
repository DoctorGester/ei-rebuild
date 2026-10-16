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

} // namespace

void LoadPersonTexts(PersonTextMap* map) {
    CTextManager* texts = GameTexts();
    console::CStringArray keys;
    mfc::CString line;
    TextsFindKeysOriginal(texts, nullptr, &keys, PersonPattern());
    for (int i = 0; i < keys.size; ++i) {
        if (!TextsOpenSection(texts, nullptr, keys[i])) continue;
        static_cast<CTextManager*>(texts)->ReadLine(&line, 1);
        int key;
        {
            mfc::CString name = console::Ops(keys[i]).Mid(kPersonPrefixLength);
            key = TextKeyFromName(name);
        }
        PersonTextNode* node = map->lower_bound(key);
        if (node == map->end() || key < node->first) {
            mfc::CString empty;
            PersonTextPair pair{key, empty};
            void* inserted;
            node = static_cast<PersonTextNode*>(*PersonMapInsertHintOriginal(map, nullptr, &inserted, node, &pair));
        }
        node->second = line;
    }
}
