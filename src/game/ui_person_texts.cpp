// The "pers <name>" texts of the battle and briefing screens (PersonTextMap, ui_camp.h): one line
// per person, by the text key of the person's name. Read by the battle screen's setup (inline
// there) and the briefing screen.
#include "ui_camp.h"

#include "text_manager.h"
#include "object_tables.h"

#include "console.h"
#include <string>
#include "lib/str.h"
#include "game_singletons.h"
#include "world.h"

namespace {

const char* PersonPattern() { return "pers *"; }
constexpr int kPersonPrefixLength = 5;                                               // "pers "

} // namespace

void LoadPersonTexts(std::map<int, std::string>* map) {
    TextManager* texts = g_clientWorld.texts;
    std::string line;
    for (const std::string& textKey : texts->ListKeys(PersonPattern())) {
        if (!texts->TextManager::Open(textKey)) continue;
        line = texts->ReadLine(true);
        int key;
        {
            std::string name = str::Mid(textKey, kPersonPrefixLength);
            key = TextKeyFromName(name);
        }
        (*map)[key] = line;
    }
}
