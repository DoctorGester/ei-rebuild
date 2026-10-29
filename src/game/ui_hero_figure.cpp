// The multiplayer hero screen's figure choice. See docs/notes/ui.md, section 5.
#include "ui.h"
#include "world_glue.h"
#include "ui_camp.h"
#include "ui_screens.h"

#include "party.h"
#include "unit.h"
#include "world.h"

#include "lib/crt.h"
#include <cstdint>
#include <string>
#include "lib/text_case.h"

#include <vector>

namespace {

const char* const kHeroName = "Hero";

constexpr float kDefaultAttribute = 25.0f;
constexpr float kDefaultHeight = 0.7f;
constexpr int kVoiceRows = 11;

// The index of `name` in `names` (case-insensitive), or -1.
int IndexOf(const std::vector<std::string>& names, std::string_view name) {
    for (int i = 0; i < std::ssize(names); ++i)
        if (text::CompareIgnoringCase(names[i], name) == 0) return i;
    return -1;
}

} // namespace

void MpHeroScreen::ChooseFigure(std::string_view prototype) {
    auto* player = LocalPlayerData();
    // The attributes and height to keep (copied bit for bit).
    float strength = kDefaultAttribute;
    float dexterity = kDefaultAttribute;
    float intelligence = kDefaultAttribute;
    float height = kDefaultHeight;
    if (!player->members.empty()) {
        PartyUnitRecord* first = player->members.data();
        const auto* stats = &first->stats;
        strength = stats->strength;
        dexterity = stats->dexterity;
        height = first->height;
        intelligence = stats->intelligence;
        std::vector<PartyUnitRecord*> records{first};
        GameRemoveUnitsOf(&records);
    }
    player->Clear(1, 0);
    player->AddMember(kHeroName, prototype, prototype);
    player->EquipFromPrototype(player->members.data());
    PartyUnitRecord* hero = player->members.data();
    auto* stats = &hero->stats;
    if (mode == kModeList) {
        stats->strengthBase = stats->strength = kDefaultAttribute;
        stats->dexterityBase = stats->dexterity = kDefaultAttribute;
        stats->intelligenceBase = stats->intelligence = kDefaultAttribute;
        hero->height = kDefaultHeight;
    } else {
        stats->strengthBase = stats->strength = strength;
        stats->dexterityBase = stats->dexterity = dexterity;
        stats->intelligenceBase = stats->intelligence = intelligence;
        hero->height = height;
    }
    // Each strip selects the figure when its list has it; the second list decides the voices.
    figureStrips[1]->selected = -1;
    figureStrips[0]->selected = -1;
    int index = IndexOf(defaults, prototype);
    if (index >= 0) figureStrips[0]->selected = index;
    index = IndexOf(names, prototype);
    bool firstVoices = index < 0;
    if (index >= 0) figureStrips[1]->selected = index;

    ShowFirstEquipment();
    MakePreview(false);
    statsPanel->SetHero(0, std::nullopt);
    tradePanel->ResetRecords();
    tradePanel->SelectHero(0);
    text = prototype;
    voiceIndex = 0;
    voiceTop = 0;
    voices = firstVoices ? &voiceLists[0] : &voiceLists[1];
    const std::vector<MpVoiceEntry>& currentVoices = *voices;
    // The first entry's file (the original read it even from an empty list; ours leaves the text then).
    if (!currentVoices.empty()) hero->texts[6] = currentVoices.front().file;
    const int count = static_cast<int>(currentVoices.size());
    voiceScroll->SetMaximum(count > kVoiceRows ? count - kVoiceRows : 0);
    voiceScroll->SetPosition(0);
    if (mode == kModeNewHero) DrawVoices();
}
