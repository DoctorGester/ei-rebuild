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

constexpr int kModeList = 9;
constexpr int kModeNewHero = 10;
constexpr float kDefaultAttribute = 25.0f;
constexpr float kDefaultHeight = 0.7f;
constexpr int kVoiceRows = 11;

// The index of `name` in a vector of CStrings (case-insensitive), or -1.
int IndexOf(const std::string* begin, const std::string* end, std::string_view name) {
    int index = 0;
    for (const std::string* entry = begin; entry != end; ++entry, ++index) {
        if (text::CompareIgnoringCase(*entry, name) == 0)
            return index;
    }
    return -1;
}

// The first entry of a record's item array, or null when it is empty.
CItem* FirstOf(const std::vector<CItem*>& list) { return !list.empty() ? list[0] : nullptr; }

} // namespace

void CMpHeroScreen::ChooseFigure(const char* prototype) {
    std::vector<PartyUnitRecord*> records = {};
    auto* player = LocalPlayerData();
    // The attributes and height to keep (copied as dwords).
    float strength = kDefaultAttribute;
    float dexterity = kDefaultAttribute;
    float intelligence = kDefaultAttribute;
    float height = kDefaultHeight;
    if (std::ssize(player->members_) > 0) {
        PartyUnitRecord* first = player->members_.data();
        const auto* stats = &first->stats;
        strength = stats->stat10;
        dexterity = stats->stat18;
        height = first->height;
        intelligence = stats->stat20;
        int size = std::ssize(records);
        records.resize(size + 1);
        records[size] = first;
        GameRemoveUnitsOf(&records);
    }
    player->Clear(1, 0);
    player->AddMember(kHeroName, prototype, prototype);
    player->EquipFromPrototype(player->members_.data());
    PartyUnitRecord* hero = player->members_.data();
    auto* stats = &hero->stats;
    if (mode_ == kModeList) {
        stats->stat10Base = stats->stat10 = kDefaultAttribute;
        stats->stat18Base = stats->stat18 = kDefaultAttribute;
        stats->stat20Base = stats->stat20 = kDefaultAttribute;
        hero->height = kDefaultHeight;
    } else {
        stats->stat10Base = stats->stat10 = strength;
        stats->stat18Base = stats->stat18 = dexterity;
        stats->stat20Base = stats->stat20 = intelligence;
        hero->height = height;
    }
    // Each strip selects the figure when its list has it; the second list decides the voices.
    figureStrips_[1]->selected_ = -1;
    figureStrips_[0]->selected_ = -1;
    int index = IndexOf(defaults_.data(), defaults_.data() + defaults_.size(), prototype);
    if (index >= 0) figureStrips_[0]->selected_ = index;
    index = IndexOf(names_.data(), names_.data() + names_.size(), prototype);
    bool firstVoices = index < 0;
    if (index >= 0) figureStrips_[1]->selected_ = index;

    weapon_ = FirstOf(hero->weapons);
    weaponPanel_->ShowWeapon(weapon_);
    armor_ = FirstOf(hero->quickItems);
    weaponPanel_->ShowArmour(armor_);
    spellContainer_ = FirstOf(hero->spells);
    spellPanel_->ShowEntry(spellContainer_);
    MakePreview(0);
    statsPanel_->SetHero(0, nullptr);
    tradePanel_->ResetRecords();
    tradePanel_->SelectHero(0);
    text_ = prototype;
    voiceIndex_ = 0;
    voiceTop_ = 0;
    voices_ = firstVoices ? &voiceLists_[0] : &voiceLists_[1];
    std::vector<MpVoiceEntry>& voices = *voices_;
    // The first entry's file (the original read it even from an empty list; ours leaves the text then).
    if (voices.begin() != voices.end()) hero->texts[6] = voices.begin()->file;
    // The entry count compared unsigned.
    std::uint32_t count = static_cast<std::uint32_t>(voices.end() - voices.begin());
    voiceScroll_->SetMaximum(count > kVoiceRows ? static_cast<int>(count) - kVoiceRows : 0);
    voiceScroll_->SetPosition(0);
    if (mode_ == kModeNewHero) DrawVoices();
}
