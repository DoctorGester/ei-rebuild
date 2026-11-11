// The multiplayer hero screen's left button and keys.
// See docs/notes/ui.md, section 5.
#include "game/units/acks_db.h"
#include "game/console/console.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/players/saved_parties.h"
#include "engine/data/text_manager.h"
#include "game/objects/object3d.h"
#include "game/players/party.h"
#include "game/ui/ui.h"
#include "game/ui/ui_scrollbar.h"
#include "game/ui/ui_message_box.h"
#include "game/ui/ui_camp.h"
#include "game/ui/ui_screens.h"
#include "game/units/unit.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"
#include "engine/core/random.h"

#include "lib/containers.h"
#include "platform/platform.h"
#include "lib/str.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include <format>

namespace {

const char* const kRenameSeparator = " | ";
const char* const kRenameBar = "|";

// Whether `index` is a saved hero's.
bool IsHero(int index) { return index >= 0 && index < g_savedParties.Count(); }

// The first entry of one of a hero record's item arrays, or null when it is empty.
Item* FirstOf(const std::vector<Item*>& list) { return !list.empty() ? list[0] : nullptr; }

// Back to the main menu: the "reserved_04" status text, the session ended, the screen closed.
void BackToMainMenu() {
    std::string text = g_clientWorld.texts->TextManager::GetString("reserved_04");
    g_interface.ShowStatusText(text);
    g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
    g_gameFlow.EndSession();
    g_interface.SetPendingValue(0);
}

// A multiplayer hero's name: the scriptName of its party's first member record (read through the members'
// data pointer).
std::string& MpHeroName(int index) {
    auto* hero = g_savedParties.Hero(index);
    return hero->GetParty()->members.data()->scriptName;
}

// The rename's title on surface 3: 130 x 16 pixels.
constexpr Rect kRenameTitle = {0, 0, 0x82, 0x10};

constexpr std::uint32_t kVoiceFlags = 0x2282;

// A voice: an environment speech whose flags include kVoiceFlags, with a chance.
bool IsVoice(const acks::EnvironmentSpeech& entry) {
    return (entry.environment & kVoiceFlags) == kVoiceFlags && entry.speech.chance != 0;
}

constexpr int kListRows = 10;
constexpr std::uint32_t kDeleteAnswerOk = BoxResult(MessageQuestion::First, MessageAnswer::Ok);

// The highest number among the titles of the files matching `pattern` (atoi; 0 without any).
int HighestFileNumber(std::string_view pattern) {
    int highest = 0;
    for (const platform::FolderEntry& entry : platform::FolderEntries(pattern)) {
        if (entry.IsDots()) continue;
        const int number = str::ToInt(entry.Title());
        if (!(highest > number)) highest = number;
    }
    return highest;
}

} // namespace

void MpHeroScreen::CloseRenameEdit() {
    surfaces[3]->Hide();
    renameEdit->SetEnabled(false);
    renameEdit->SetFocused(false);
}

void MpHeroScreen::BackToNewHero() {
    g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
    statsPanel->SetHero(0, "");
    labels[0].clear();
    tradePanel->Settle(false, false);
    SetMode(kModeNewHero);
    nameEdit->SetText(HeroRecord()->scriptName);
}

void MpHeroScreen::FinishRename() {
    std::string typed = renameEdit->text;
    str::TrimLeft(typed);
    str::TrimRight(typed);
    if (!typed.empty()) typed = kRenameSeparator + typed;
    std::string& name = MpHeroName(heroIndex);
    std::string title = name;
    std::string base;
    int bar = str::Find(title, kRenameBar);
    if (bar >= 0)
        base = str::Left(title, bar);
    else
        base = title;
    str::TrimLeft(base);
    str::TrimRight(base);
    typed = base + typed;
    name = typed;
    g_savedParties.Hero(heroIndex)->WriteFile(g_savedParties.folder);
    CloseRenameEdit();
    ShowHero(heroIndex);
}

void MpHeroScreen::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    default: ItemScreen::OnEvent(event); break;
    }
}

void MpHeroScreen::KeyDown(UiEvent& event) {
    UiContainer::OnEvent(event);
    if (event.handled) return;
    bool renaming = renameEdit->focused;
    int heroStep = 0;
    int voiceStep = 0;
    switch (event.key) {
    case platform::Key::Return:
        if (renaming)
            FinishRename();
        else if (mode == kModeList)
            Continue();
        else if (mode == kModeStats)
            AddNewHero();
        else if (mode == kModeNewHero)
            CreateHero();
        event.handled = true;
        return;
    case platform::Key::Escape:
        if (renaming) {
            CloseRenameEdit();
            DrawHeroList();
        } else if (mode == kModeList) {
            BackToMainMenu();
        } else if (mode == kModeStats) {
            BackToNewHero();
        } else if (mode == kModeNewHero) {
            g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
            SetMode(kModeList);
        }
        event.handled = true;
        return;
    case platform::Key::PageUp:
        heroStep = -10;
        voiceStep = -11;
        break;
    case platform::Key::PageDown:
        heroStep = 10;
        voiceStep = 11;
        break;
    case platform::Key::Up:
        heroStep = -1;
        voiceStep = -1;
        break;
    case platform::Key::Down:
        heroStep = 1;
        voiceStep = 1;
        break;
    default:
        return;
    }
    if (mode == kModeList) {
        g_interface.PlayInterfaceSound(kSaveSelectSound);
        heroIndex += heroStep;
        ClampHeroSelection();
    } else if (mode == kModeNewHero) {
        g_interface.PlayInterfaceSound(kSaveSelectSound);
        voiceIndex += voiceStep;
        ClampVoiceSelection();
        PlayVoice();
    } else {
        return;
    }
    event.handled = true;
}

void MpHeroScreen::LeftDown(UiEvent& event) {
    if (renameEdit->focused) return;
    UiContainer::OnEvent(event);
    if (event.handled) return;
    const UiRegion* region = RegionUnder(event.at);
    auto rotate = [&](int direction) {
        g_interface.PlayInterfaceSound("buttons\\camp\\move.wav");
        rotating = direction;
        CaptureMouse();
    };

    if (mode == kModeNewHero) {
        // The new hero.
        if (region == backButton) {
            g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
            SetMode(kModeList);
            return;
        }
        if (region == forwardButton) {
            CreateHero();
            return;
        }
        if (region == turnButtons[0]) {
            rotate(1);
            return;
        }
        if (region == turnButtons[1]) {
            rotate(2);
            return;
        }
        if (region == resetButton) {
            // Back to the defaults: the attributes 25, the speed factor 0.7.
            g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
            ChooseFigure(defaults[0]);
            PartyUnitRecord* hero = HeroRecord();
            auto* stats = &hero->stats;
            stats->strengthBase = 25.0f;
            stats->strength = 25.0f;
            stats->dexterityBase = 25.0f;
            stats->dexterity = 25.0f;
            stats->intelligenceBase = 25.0f;
            stats->intelligence = 25.0f;
            hero->height = 0.7f;
            nameEdit->SetText("");
            statsPanel->SetHero(0, "");
            RefreshStats();
            return;
        }
        const int row = ListRow(region);
        if (row < 0 || row >= kVoiceRows) return;
        // A voice row: the hero takes its file.
        int index = voiceTop + row;
        if (index < 0) return;
        std::vector<MpVoiceEntry>& currentVoices = *voices;
        if (static_cast<unsigned>(index) >= static_cast<unsigned>(currentVoices.end() - currentVoices.begin())) return;
        g_interface.PlayInterfaceSound(kSaveSelectSound);
        voiceIndex = index;
        const std::string& file = currentVoices.begin()[index].file;
        HeroRecord()->secondPrototype = file;
        PlayVoice();
        DrawVoices();
        return;
    }

    if (mode == kModeStats) {
        // The new hero's statistics page.
        if (region == backButton) {
            BackToNewHero();
        } else if (region == forwardButton) {
            AddNewHero();
        } else if (region == resetButton) {
            // Take the equipment back.
            g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
            tradePanel->Settle(false, false);
            ShowFirstEquipment();
        }
        return;
    }

    if (mode != kModeList) return;
    // The hero list. A click while a hero is being renamed ends the renaming first (only reached
    // when the base handler started it: the handler returns at once while renaming).
    if (region && renameEdit->focused) CloseRenameEdit();
    if (region == backButton) {
        BackToMainMenu();
        return;
    }
    if (region == forwardButton) {
        Continue();
        return;
    }
    if (region == turnButtons[0]) {
        rotate(1);
        return;
    }
    if (region == turnButtons[1]) {
        rotate(2);
        return;
    }
    if (region == newHeroButton) {
        g_interface.PlayInterfaceSound(kSaveSelectSound);
        ChooseFigure(defaults[0]);
        statsPanel->SetHero(0, "");
        labels[0].clear();
        turn = 0.0f;
        SetMode(kModeNewHero);
        nameEdit->SetText("");
        return;
    }
    if (region == renameButton) {
        g_interface.PlayInterfaceSound(kSaveSelectSound);
        Rename();
        return;
    }
    if (region == deleteButton) {
        // Delete: asks first ("lmp_deletepers").
        g_interface.PlayInterfaceSound(kSaveSelectSound);
        if (!IsHero(heroIndex)) return;
        g_interface.PushWindow(std::make_unique<MessageWindow>("lmp_deletepers", "lmp_deletepers_msg", MessageQuestion::First, MessageButtons::OkCancel), WindowBackdrop::FirstWindow);
        return;
    }
    if (region == inventoryButton) {
        // The hero's inventory: the camp screen.
        g_interface.PlayInterfaceSound(kSaveSelectSound);
        if (!IsHero(heroIndex)) return;
        g_interface.PushWindow(std::make_unique<CampPages>(true), WindowBackdrop::None);
        return;
    }
    const int row = ListRow(region);
    if (row < 0 || row >= kHeroRows) return;
    int index = heroTop + row;
    if (!IsHero(index)) return;
    g_interface.PlayInterfaceSound(kSaveSelectSound);
    ShowHero(index);
}

// --- Rename ---------------------------------------------------------------------------

void MpHeroScreen::Rename() {
    if (!IsHero(heroIndex)) return;
    renameEdit->SetEnabled(true);
    renameEdit->SetFocused(true);
    std::string name = MpHeroName(heroIndex);
    int bar = str::Find(name, kRenameBar);
    if (bar < 0) {
        renameEdit->SetText("");
    } else {
        std::string typed = str::Mid(name, bar + 1);
        str::TrimLeft(typed);
        renameEdit->SetText(typed);
        name = str::Left(name, bar);
        str::TrimRight(name);
    }
    surfaces[3]->Show();
    labels[3] = {{name + kRenameSeparator, kRenameTitle, {1, kUiTextColour, render::text::Format::Right}}};
}

// --- The voice's sound ----------------------------------------------------------------

void MpHeroScreen::PlayVoice() {
    g_interface.StopSound(voiceSound);
    voiceSound = -1;
    auto* hero = HeroRecord();
    UnitPrototypeRecord* prototype;
    {
        std::string key(hero->secondPrototype);
        prototype = g_unitDatabase.prototypes.FindByName(key);
    }
    if (!prototype) return;
    acks::UnitAcks* set;
    {
        std::string key(prototype->name);
        set = g_ackDatabase.units.Find(key);
    }
    if (!set) return;
    // The voice entries are counted and found again by their rank (no allocation; the original gathers
    // them in a temporary vector).
    int count = 0;
    const std::vector<acks::EnvironmentSpeech>& entries = set->environmentSpeeches.entries;
    for (const acks::EnvironmentSpeech& e : entries)
        if (IsVoice(e)) ++count;
    if (count == 0) return;
    auto nth = [&](int rank) -> const acks::EnvironmentSpeech* {
        for (const acks::EnvironmentSpeech& e : entries)
            if (IsVoice(e) && rank-- == 0) return &e;
        return nullptr;
    };
    int index = ClientRandBelow(count);
    for (int tries = 0; tries < count && voiceSound == -1; ++tries) {
        voiceSound = g_interface.PlayInterfaceSound(nth(index)->speech.sound);
        index = (index + 1) % count;
    }
}

// --- Continue -------------------------------------------------------------------------

void MpHeroScreen::Continue() {
    g_interface.PlayInterfaceSound(kMessageBoxOkSound);
    if (IsHero(heroIndex)) {
        g_savedParties.SaveAll();
        g_interface.PushWindow(std::make_unique<MpConnectScreen>(), WindowBackdrop::FirstWindow);
        return;
    }
    g_interface.PushWindow(std::make_unique<MessageWindow>("lmp_no_pers", "lmp_no_pers_msg", MessageQuestion::Second, MessageButtons::Ok), WindowBackdrop::FirstWindow);
}

// --- The new hero's statistics and its creation --------------------------------------

void MpHeroScreen::RefreshStats() {
    if (previewModel) {
        auto* hero = HeroRecord();
        auto* stats = &hero->stats;
        stats->RestoreSlots();
        // The third argument (the height's address) is not read.
        stats->GetFactors(&hero->scaleX, &hero->scaleY, 0);
        previewModel->SetFigureScale({hero->scaleX, hero->scaleY, hero->height});
        Unit* unit = previewModel;
        unit->SetFromStats(&HeroRecord()->stats);

        unit->controller->RefreshWounds();
    }
    statsPanel->DrawStats();
}

void MpHeroScreen::CreateHero() {
    std::string name = nameEdit->text;
    auto* hero = HeroRecord();
    auto* stats = &hero->stats;
    float sum = stats->intelligenceBase + stats->dexterityBase + stats->strengthBase;
    float points = kAttributePoints;
    if (sum != points || static_cast<int>(name.size()) == 0) {
        g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
        return;
    }
    g_interface.PlayInterfaceSound(kMessageBoxOkSound);
    ShowFirstEquipment();
    hero->scriptName = name;
    statsPanel->SetHero(0, name);
    RefreshStats();
    SetMode(kModeStats);
}

void MpHeroScreen::ShowFirstEquipment() {
    PartyUnitRecord* hero = HeroRecord();
    weapon = FirstOf(hero->weapons);
    weaponPanel->ShowWeapon(weapon);
    quickItem = FirstOf(hero->quickItems);
    weaponPanel->ShowQuickItem(quickItem);
    spellContainer = FirstOf(hero->spells);
    spellPanel->ShowEntry(spellContainer);
}
// --- A hero deleted, the message box's answer ------------------------------------------

MpHeroRecord::MpHeroRecord() {}
MpHeroRecord::MpHeroRecord(const MpHeroRecord& other) : party(const_cast<MpHeroRecord&>(other).party), file(other.file) {}
MpHeroRecord& MpHeroRecord::operator=(const MpHeroRecord& other) {
    GetParty()->Assign(const_cast<MpHeroRecord&>(other).GetParty());
    file = other.file;
    return *this;
}
MpHeroRecord::~MpHeroRecord() {}

void SavedParties::Select(const MpHeroRecord* hero) { outsideGame = *hero; }

void MpHeroScreen::DeleteHero() {
    if (!IsHero(heroIndex)) return;
    std::string path = g_savedParties.folder + g_savedParties.entries[heroIndex].file;
    platform::RemoveFile(path);
    g_savedParties.entries.erase(g_savedParties.entries.begin() + heroIndex);
    if (heroIndex >= g_savedParties.Count()) heroIndex = g_savedParties.Count() - 1;
    RefreshHeroScroll();
    DrawHeroList();
    ShowHero(heroIndex);
}

void MpHeroScreen::RefreshHeroScroll() {
    UiScrollbar* bar = heroScroll;
    if (g_savedParties.Count() > kListRows) {
        const int position = bar->TakePosition();
        bar->SetMaximum(g_savedParties.Count() - kListRows);
        bar->SetPosition(position);
        heroTop = bar->TakePosition();
    } else {
        bar->SetMaximum(0);
        heroTop = 0;
    }
}

void MpHeroScreen::MessageResult(std::uint32_t result) {
    if (result == kDeleteAnswerOk) DeleteHero();
    if (IsHero(heroIndex)) {
        MpHeroRecord* hero = g_savedParties.Hero(heroIndex);
        hero->GetParty()->Assign(LocalPlayerData());
        hero->WriteFile(g_savedParties.folder);
    }
    SetMode(kModeList);
}

// --- The new hero added to the list --------------------------------------------------

void MpHeroScreen::AddNewHero() {
    g_interface.PlayInterfaceSound(kMessageBoxOkSound);
    auto* hero = HeroRecord();
    // Only the chosen equipment stays (each list walked from its end; the count read once).
    for (int i = lib::Count(hero->weapons) - 1; i >= 0; --i)
        if (hero->weapons[i] != weapon)
            hero->RemoveWeapon(hero->weapons[i]);
    for (int i = lib::Count(hero->quickItems) - 1; i >= 0; --i)
        if (hero->quickItems[i] != quickItem)
            hero->RemoveQuickItem(hero->quickItems[i]);
    for (int i = lib::Count(hero->spells) - 1; i >= 0; --i)
        if (hero->spells[i] != spellContainer)
            hero->RemoveSpell(hero->spells[i]);
    hero->weaponIndex = !hero->weapons.empty() ? 0 : kNoWeaponSlot;
    if (!hero->spells.empty()) {
        auto* container = hero->spells[0];
        Spell* spell = container->spell;
        if (spell && !HeroCanUseSpell(&hero->stats, spell)) {
            LocalPlayerData()->AddItem(container, false, true);
            hero->RemoveSpell(container);
        }
    }

    // A new record; at capacity the original copies a blank one into the grown storage (a party copy draws ids).
    std::vector<MpHeroRecord>& heroes = g_savedParties.entries;
    if (heroes.size() == heroes.capacity()) {
        MpHeroRecord blank;
        g_savedParties.ReserveOne();
        heroes.push_back(blank);
    } else {
        heroes.emplace_back();
    }
    MpHeroRecord* record = &heroes.back();
    heroIndex = static_cast<int>(heroes.size()) - 1;

    const int highest = HighestFileNumber(std::format("{}*.mp", g_savedParties.folder));
    record->file = std::format("{}.mp", highest + 1);
    record->GetParty()->Assign(LocalPlayerData());
    record->WriteFile(g_savedParties.folder);
    RefreshHeroScroll();
    DrawHeroList();
    SetMode(kModeList);
}
