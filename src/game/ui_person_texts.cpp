// The "pers <name>" texts of the battle and briefing screens (PersonTextMap, ui_camp.h): one line
// per person, by the text key of the person's name. Read by the battle screen's setup (inline
// there) and the briefing screen.
#include "game/ui_camp.h"

#include "game/text_manager.h"
#include "game/object_tables.h"

#include "game/console.h"
#include <string>
#include "lib/str.h"
#include "game/game_singletons.h"
#include "game/world.h"

namespace {

constexpr const char* kPersonPattern = "pers *";
constexpr int kPersonPrefixLength = 5;                                               // "pers "

} // namespace

void LoadPersonTexts(std::map<int, std::string>* map) {
    TextManager* texts = g_clientWorld.texts.get();
    std::string line;
    for (const std::string& textKey : texts->ListKeys(kPersonPattern)) {
        if (!texts->TextManager::Open(textKey)) continue;
        line = texts->ReadLine(true);
        (*map)[TextKeyFromName(str::Mid(textKey, kPersonPrefixLength))] = line;
    }
}
