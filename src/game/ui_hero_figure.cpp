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

void CMpHeroScreen::ChooseFigure(std::string_view prototype) {
    auto* player = LocalPlayerData();
    // The attributes and height to keep (copied bit for bit).
    float strength = kDefaultAttribute;
    float dexterity = kDefaultAttribute;
    float intelligence = kDefaultAttribute;
    float height = kDefaultHeight;
    if (!player->members_.empty()) {
        PartyUnitRecord* first = player->members_.data();
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
    player->EquipFromPrototype(player->members_.data());
    PartyUnitRecord* hero = player->members_.data();
    auto* stats = &hero->stats;
    if (mode_ == kModeList) {
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
    figureStrips_[1]->selected_ = -1;
    figureStrips_[0]->selected_ = -1;
    int index = IndexOf(defaults_, prototype);
    if (index >= 0) figureStrips_[0]->selected_ = index;
    index = IndexOf(names_, prototype);
    bool firstVoices = index < 0;
    if (index >= 0) figureStrips_[1]->selected_ = index;

    ShowFirstEquipment();
    MakePreview(false);
    statsPanel_->SetHero(0, std::nullopt);
    tradePanel_->ResetRecords();
    tradePanel_->SelectHero(0);
    text_ = prototype;
    voiceIndex_ = 0;
    voiceTop_ = 0;
    voices_ = firstVoices ? &voiceLists_[0] : &voiceLists_[1];
    const std::vector<MpVoiceEntry>& voices = *voices_;
    // The first entry's file (the original read it even from an empty list; ours leaves the text then).
    if (!voices.empty()) hero->texts[6] = voices.front().file;
    const int count = static_cast<int>(voices.size());
    voiceScroll_->SetMaximum(count > kVoiceRows ? count - kVoiceRows : 0);
    voiceScroll_->SetPosition(0);
    if (mode_ == kModeNewHero) DrawVoices();
}
