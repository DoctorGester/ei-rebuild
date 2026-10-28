// The "pers <name>" texts of the battle and briefing screens (PersonTextMap, ui_camp.h): one line
// per person, by the text key of the person's name. Read by the battle screen's setup (inline
// there) and the briefing screen.
#include "ui_camp.h"

#include "item_db_text.h"
#include "object_tables.h"

#include "console.h"
#include "lib/mfc.h"
#include "lib/str.h"
#include "library_trees.h"
#include "game_singletons.h"

#include <string>

namespace {

const char* PersonPattern() { return "pers *"; }
constexpr int kPersonPrefixLength = 5;                                               // "pers "

} // namespace

void LoadPersonTexts(PersonTextMap* map) {
    CTextManager* texts = TheTextManager();
    std::vector<std::string> keys;
    std::string line;
    texts->ListKeys(&keys, PersonPattern());
    for (const std::string& textKey : keys) {
        if (!texts->CTextManager::Open(textKey.c_str())) continue;
        texts->ReadLine(&line, 1);
        int key;
        {
            std::string name = str::Mid(textKey, kPersonPrefixLength);
            key = TextKeyFromName(name.c_str());
        }
        PersonTextNode* node = map->lower_bound(key);
        if (node == map->end() || key < node->first) {
            std::string empty;
            const PersonTextMap::Value pair{key, empty};   // the key and the text
            static_cast<lib_trees::PersonTexts*>(map)->InsertHint(&node, node, pair);
        }
        node->second = line;
    }
}
