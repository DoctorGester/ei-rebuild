// The "pers <name>" texts of the battle and briefing screens (PersonTextMap, ui_camp.h): one line
// per person, by the text key of the person's name. Read by the battle screen's setup (inline
// there) and the briefing screen.
#include "ui_camp.h"

#include "item_db_text.h"
#include "object_tables.h"

#include "console.h"
#include <string>
#include "lib/str.h"
#include "game_singletons.h"


namespace {

const char* PersonPattern() { return "pers *"; }
constexpr int kPersonPrefixLength = 5;                                               // "pers "

} // namespace

void LoadPersonTexts(std::map<int, std::string>* map) {
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
            key = TextKeyFromName(name);
        }
        (*map)[key] = line;
    }
}
