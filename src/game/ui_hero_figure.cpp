// The multiplayer hero screen's figure choice (00616AC0, slot 52). See docs/notes/ui.md, section 5.
#include "ui_screens.h"

#include "party.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "originals_ui.h"

#include <cstdint>

namespace {

const char* const kHeroName = reinterpret_cast<const char*>(0x0078B0F4);   // "Hero"
// The client glue the record hand-over is called on (not read).
void* ClientGlue() { return reinterpret_cast<void*>(0x007AFEA5); }
// CArray<record*> (the party's records handed to the client world).
void* RecordArrayVtable() { return reinterpret_cast<void*>(0x0073FAA4); }

struct RecordArray {
    void* vtable;
    std::uint8_t** data;
    int size;
    int capacity;
    int growBy;
};

constexpr int kModeList = 9;
constexpr int kModeNewHero = 10;
constexpr float kDefaultAttribute = 25.0f;
constexpr float kDefaultHeight = 0.7f;
constexpr int kVoiceRows = 11;

// The index of `name` in a vector of CStrings (case-insensitive), or -1.
int IndexOf(const char* const* begin, const char* const* end, const char* name) {
    int index = 0;
    for (const char* const* entry = begin; entry != end; ++entry, ++index) {
        if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(*entry), reinterpret_cast<const unsigned char*>(name)) == 0)
            return index;
    }
    return -1;
}

// The first entry of a record's item array, or null when it is empty.
CEngineObject* FirstOf(CEngineObject* const* data, int count) { return count > 0 ? data[0] : nullptr; }

} // namespace

void CMpHeroScreen::ChooseFigure(const char* prototype) {
    RecordArray records = {RecordArrayVtable(), nullptr, 0, 0, 0};
    auto* player = reinterpret_cast<CParty*>(LocalPlayerData());
    // The attributes and height to keep (copied as dwords).
    float strength = kDefaultAttribute;
    float dexterity = kDefaultAttribute;
    float intelligence = kDefaultAttribute;
    float height = kDefaultHeight;
    if (player->members_.size > 0) {
        PartyUnitRecord* first = player->members_.data;
        const auto* stats = reinterpret_cast<const UnitStats*>(first->stats);
        strength = stats->stat10;
        dexterity = stats->stat18;
        height = first->height;
        intelligence = stats->stat20;
        int size = records.size;
        RecordArraySetSizeOriginal(&records, nullptr, size + 1, -1);
        records.data[size] = reinterpret_cast<std::uint8_t*>(first);
        GameTakeUnitRecordsOriginal(ClientGlue(), nullptr, &records);
    }
    player->Clear(1, 0);
    player->AddMember(kHeroName, prototype, prototype);
    PartyEquipFromPrototypeOriginal(player, nullptr, player->members_.data);
    PartyUnitRecord* hero = player->members_.data;
    auto* stats = reinterpret_cast<UnitStats*>(hero->stats);
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
    const auto* first = reinterpret_cast<const char* const*>(defaults_);
    int index = IndexOf(first, reinterpret_cast<const char* const*>(defaultsEnd_), prototype);
    if (index >= 0) figureStrips_[0]->selected_ = index;
    index = IndexOf(names_.begin, names_.end, prototype);
    bool firstVoices = index < 0;
    if (index >= 0) figureStrips_[1]->selected_ = index;

    weapon_ = reinterpret_cast<char*>(FirstOf(hero->weapons, static_cast<int>(hero->weaponCount)));
    ShowWeapon(weaponPanel_, nullptr, weapon_);
    armor_ = reinterpret_cast<char*>(FirstOf(hero->quickItems, static_cast<int>(hero->quickItemCount)));
    ShowArmour(weaponPanel_, nullptr, armor_);
    spellContainer_ = reinterpret_cast<char*>(FirstOf(hero->spells, static_cast<int>(hero->spellCount)));
    ShowContainer(spellPanel_, nullptr, spellContainer_);
    HeroScreenMakePreviewOriginal(this, nullptr, 0);
    StatsPanelSetHeroOriginal(statsPanel_, nullptr, 0, nullptr);
    TradePanelResetRecordsOriginal(tradePanel_);
    TradePanelSelectHeroOriginal(tradePanel_, nullptr, 0);
    reinterpret_cast<mfc::CString&>(text_) = prototype;
    voiceIndex_ = 0;
    voiceTop_ = 0;
    voices_ = reinterpret_cast<char**>(firstVoices ? &voiceLists_[0] : &voiceLists_[1]);
    MpVoiceVector& voices = Voices();
    // The first entry's file (read even when the list is empty, as the original does).
    hero->texts[6] = reinterpret_cast<const mfc::CString&>(voices.begin->file);
    // The entry count compared unsigned (the subtraction shifted arithmetically).
    std::uint32_t count = static_cast<std::uint32_t>(
        (reinterpret_cast<char*>(voices.end) - reinterpret_cast<char*>(voices.begin)) >> 3);
    ScrollbarSetRangeOriginal(voiceScroll_, nullptr, count > kVoiceRows ? static_cast<int>(count) - kVoiceRows : 0);
    static_cast<CUiScrollbar*>(voiceScroll_)->CallSetPosition(0);
    if (mode_ == kModeNewHero) CallDrawVoices();
    if (records.data) gamecrt::operator_delete(records.data);
}
namespace {
void __fastcall HeroScreenChooseFigure(CMpHeroScreen* self, void*, const char* prototype) { self->ChooseFigure(prototype); }
} // namespace
EI_REPLACE(0x00616AC0, HeroScreenChooseFigure);
