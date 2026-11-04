// The multiplayer hero screen's left button and keys.
// See docs/notes/ui.md, section 5.
#include "acks_db.h"
#include "console.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "item.h"
#include "text_manager.h"
#include "map_object_figures.h"
#include "party.h"
#include "ui.h"
#include "ui_camp.h"
#include "ui_screens.h"
#include "unit.h"
#include "unit_create.h"
#include "unit_db.h"
#include "world.h"
#include "random.h"

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
void BackToMainMenu(Interface3D* ui) {
    std::string text = g_clientWorld.texts->TextManager::GetString("reserved_04");
    g_interface.ShowStatusText(text);
    ui->PlayInterfaceSound(game::CancelSoundText());
    g_gameFlow.EndSession();
    ui->SetPendingValue(0);
}

// A multiplayer hero's name: the text 0 of its party's first member record (read through the members'
// data pointer).
std::string& MpHeroName(int index) {
    auto* hero = g_savedParties.Hero(index);
    return hero->GetParty()->members.data()->texts[0];
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
    ui->PlayInterfaceSound(game::CancelSoundText());
    statsPanel->SetHero(0, game::kEmptyText);
    labels[0].clear();
    tradePanel->Settle(false, false);
    SetMode(kModeNewHero);
    nameEdit->SetText(HeroRecord()->texts[0]);
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

void MpHeroScreen::KeyDown(platform::Key key, int scan, bool repeat) {
    UiContainer::KeyDown(key, scan, repeat);
    if (handled) return;
    Interface3D* currentUi = ui;
    bool renaming = renameEdit->focused;
    int heroStep = 0;
    int voiceStep = 0;
    switch (key) {
    case platform::Key::Return:
        if (renaming)
            FinishRename();
        else if (mode == kModeList)
            Continue();
        else if (mode == kModeStats)
            AddNewHero();
        else if (mode == kModeNewHero)
            CreateHero();
        handled = true;
        return;
    case platform::Key::Escape:
        if (renaming) {
            CloseRenameEdit();
            DrawHeroList();
        } else if (mode == kModeList) {
            BackToMainMenu(currentUi);
        } else if (mode == kModeStats) {
            BackToNewHero();
        } else if (mode == kModeNewHero) {
            currentUi->PlayInterfaceSound(game::CancelSoundText());
            SetMode(kModeList);
        }
        handled = true;
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
        currentUi->PlayInterfaceSound(game::SelectSoundText());
        heroIndex += heroStep;
        ClampHeroSelection();
    } else if (mode == kModeNewHero) {
        currentUi->PlayInterfaceSound(game::SelectSoundText());
        voiceIndex += voiceStep;
        ClampVoiceSelection();
        PlayVoice();
    } else {
        return;
    }
    handled = true;
}

void MpHeroScreen::LeftDown(Point point) {
    if (renameEdit->focused) return;
    UiContainer::LeftDown(point);
    if (handled) return;
    Interface3D* currentUi = ui;
    int region = RegionAt(point);
    auto rotate = [&](int direction) {
        currentUi->PlayInterfaceSound(game::RotateSoundText());
        rotating = direction;
        CaptureMouse();
    };

    if (mode == kModeNewHero) {
        // The new hero.
        switch (region) {
        case 0:
            currentUi->PlayInterfaceSound(game::CancelSoundText());
            SetMode(kModeList);
            return;
        case 1:
            CreateHero();
            return;
        case 2:
            rotate(1);
            return;
        case 3:
            rotate(2);
            return;
        case 4: {
            // Back to the defaults: the attributes 25, the speed factor 0.7.
            currentUi->PlayInterfaceSound(game::CancelSoundText());
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
            nameEdit->SetText(game::kEmptyText);
            statsPanel->SetHero(0, game::kEmptyText);
            RefreshStats();
            return;
        }
        default:
            break;
        }
        if (region < 9 || region >= 20) return;
        // A voice row: the hero takes its file.
        int index = voiceTop + region - 9;
        if (index < 0) return;
        std::vector<MpVoiceEntry>& currentVoices = *voices;
        if (static_cast<unsigned>(index) >= static_cast<unsigned>(currentVoices.end() - currentVoices.begin())) return;
        currentUi->PlayInterfaceSound(game::SelectSoundText());
        voiceIndex = index;
        const std::string& file = currentVoices.begin()[index].file;
        HeroRecord()->texts[6] = file;
        PlayVoice();
        DrawVoices();
        return;
    }

    if (mode == kModeStats) {
        // The new hero's statistics page.
        if (region == 0) {
            BackToNewHero();
        } else if (region == 1) {
            AddNewHero();
        } else if (region == 4) {
            // Take the equipment back.
            currentUi->PlayInterfaceSound(game::CancelSoundText());
            tradePanel->Settle(false, false);
            ShowFirstEquipment();
        }
        return;
    }

    if (mode != kModeList) return;
    // The hero list. A click while a hero is being renamed ends the renaming first (only reached
    // when the base handler started it: the handler returns at once while renaming).
    if (region >= 0 && renameEdit->focused) CloseRenameEdit();
    switch (region) {
    case 0:
        BackToMainMenu(currentUi);
        return;
    case 1:
        Continue();
        return;
    case 2:
        rotate(1);
        return;
    case 3:
        rotate(2);
        return;
    case 5:
        // A new hero.
        currentUi->PlayInterfaceSound(game::SelectSoundText());
        ChooseFigure(defaults[0]);
        statsPanel->SetHero(0, game::kEmptyText);
        labels[0].clear();
        turn = 0.0f;
        SetMode(kModeNewHero);
        nameEdit->SetText(game::kEmptyText);
        return;
    case 6:
        currentUi->PlayInterfaceSound(game::SelectSoundText());
        Rename();
        return;
    case 7: {
        // Delete: asks first ("lmp_deletepers").
        currentUi->PlayInterfaceSound(game::SelectSoundText());
        if (!IsHero(heroIndex)) return;
        auto* box = new MessageWindow(currentUi, "lmp_deletepers", "lmp_deletepers_msg", MessageQuestion::First, MessageButtons::OkCancel);
        currentUi->PushWindow(box, WindowBackdrop::FirstWindow);
        return;
    }
    case 8: {
        // The hero's inventory: the camp screen.
        currentUi->PlayInterfaceSound(game::SelectSoundText());
        if (!IsHero(heroIndex)) return;
        ui->PushWindow(new CampPages(ui, true), WindowBackdrop::None);
        return;
    }
    default:
        break;
    }
    if (region < 9 || region >= 19) return;
    int index = heroTop + region - 9;
    if (!IsHero(index)) return;
    currentUi->PlayInterfaceSound(game::SelectSoundText());
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
        renameEdit->SetText(game::kEmptyText);
    } else {
        std::string typed = str::Mid(name, bar + 1);
        str::TrimLeft(typed);
        renameEdit->SetText(typed);
        name = str::Left(name, bar);
        str::TrimRight(name);
    }
    surfaces[3]->Show();
    labels[3] = {{name + kRenameSeparator, kRenameTitle, 1, kUiTextColour, 2}};
}

// --- The voice's sound ----------------------------------------------------------------

void MpHeroScreen::PlayVoice() {
    ui->StopSound(voiceSound);
    voiceSound = -1;
    auto* hero = HeroRecord();
    UnitPrototypeRecord* prototype;
    {
        std::string key(hero->texts[6]);
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
        voiceSound = ui->PlayInterfaceSound(nth(index)->speech.sound.c_str());
        index = (index + 1) % count;
    }
}

// --- Continue -------------------------------------------------------------------------

void MpHeroScreen::Continue() {
    Interface3D* currentUi = ui;
    currentUi->PlayInterfaceSound(game::OkSoundText());
    if (IsHero(heroIndex)) {
        g_savedParties.SaveAll();
        currentUi->PushWindow(new MpConnectScreen(currentUi), WindowBackdrop::FirstWindow);
        return;
    }
    auto* box = new MessageWindow(currentUi, "lmp_no_pers", "lmp_no_pers_msg", MessageQuestion::Second, MessageButtons::Ok);
    currentUi->PushWindow(box, WindowBackdrop::FirstWindow);
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
        ui->PlayInterfaceSound(game::CancelSoundText());
        return;
    }
    ui->PlayInterfaceSound(game::OkSoundText());
    ShowFirstEquipment();
    hero->texts[0] = name;
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
    platform::RemoveFile(path.c_str());
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
    ui->PlayInterfaceSound(game::OkSoundText());
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
            LocalPlayerData()->AddItem(container, 0, 1);
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
