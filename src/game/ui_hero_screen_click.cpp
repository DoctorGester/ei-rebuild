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
#include "world_glue.h"
#include "random.h"

#include "lib/file_io.h"
#include "lib/gameapi.h"
#include "lib/nan_audit.h"
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

const char* const kSoundOk = "buttons\\messbox\\ok.wav";
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
    ui->PlaySound(game::CancelSoundText());
    g_gameFlow.EndSession();
    ui->SetPendingValue(0);
}

// A multiplayer hero's name: the text 0 of its party's first member record (read through the members'
// data pointer).
std::string& MpHeroName(int index) {
    auto* hero = g_savedParties.Hero(index);
    return hero->Party()->members.data()->texts[0];
}

// The rename's title on surface 3: 130 x 16 pixels.
constexpr RECT kRenameTitle = {0, 0, 0x82, 0x10};
constexpr std::uint32_t kBright = 0xA7D7E4;

constexpr std::uint32_t kVoiceFlags = 0x2282;

// A voice: an environment speech whose flags include kVoiceFlags, with a chance.
bool IsVoice(const acks::EnvironmentSpeech& entry) {
    return (entry.environment & kVoiceFlags) == kVoiceFlags && entry.speech.chance != 0;
}

constexpr int kListRows = 10;
constexpr int kAnswerOk = 0x10001;

// The highest number among the titles of the files matching `pattern` (atoi; 0 without any).
int HighestFileNumber(std::string_view pattern) {
    int highest = 0;
    lib::FileFind find;
    if (find.FindFile(pattern)) {
        int more;
        do {
            more = find.FindNext();
            if (!find.IsDots()) {
                std::string title = find.GetTitle();
                int number = str::ToInt(title);
                if (!(highest > number)) highest = number;
            }
        } while (more);
    }
    find.Close();
    return highest;
}

} // namespace

void MpHeroScreen::CloseRenameEdit() {
    surfaces[3]->Hide(false);
    renameEdit->SetEnabled(false);
    renameEdit->SetFocused(false);
}

void MpHeroScreen::BackToNewHero() {
    ui->PlaySound(game::CancelSoundText());
    statsPanel->SetHero(0, game::kEmptyText);
    surfaces[0]->Clear();
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

void MpHeroScreen::KeyDown(int key, int repeat, int modifiers) {
    UiContainer::KeyDown(key, repeat, modifiers);
    if (handled) return;
    Interface3D* currentUi = ui;
    bool renaming = renameEdit->focused;
    int heroStep = 0;
    int voiceStep = 0;
    switch (key) {
    case VK_RETURN:
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
    case VK_ESCAPE:
        if (renaming) {
            CloseRenameEdit();
            DrawHeroList();
        } else if (mode == kModeList) {
            BackToMainMenu(currentUi);
        } else if (mode == kModeStats) {
            BackToNewHero();
        } else if (mode == kModeNewHero) {
            currentUi->PlaySound(game::CancelSoundText());
            SetMode(kModeList);
        }
        handled = true;
        return;
    case VK_PRIOR:
        heroStep = -10;
        voiceStep = -11;
        break;
    case VK_NEXT:
        heroStep = 10;
        voiceStep = 11;
        break;
    case VK_UP:
        heroStep = -1;
        voiceStep = -1;
        break;
    case VK_DOWN:
        heroStep = 1;
        voiceStep = 1;
        break;
    default:
        return;
    }
    if (mode == kModeList) {
        currentUi->PlaySound(game::SelectSoundText());
        heroIndex += heroStep;
        ClampHeroSelection();
    } else if (mode == kModeNewHero) {
        currentUi->PlaySound(game::SelectSoundText());
        voiceIndex += voiceStep;
        ClampVoiceSelection();
        PlayVoice();
    } else {
        return;
    }
    handled = true;
}

void MpHeroScreen::LeftDown(int modifiers, int x, int y) {
    if (renameEdit->focused) return;
    UiContainer::LeftDown(modifiers, x, y);
    if (handled) return;
    Interface3D* currentUi = ui;
    int region = RegionAt(x, y);
    auto rotate = [&](int direction) {
        currentUi->PlaySound(game::RotateSoundText());
        rotating = direction;
        CaptureMouse();
    };

    if (mode == kModeNewHero) {
        // The new hero.
        switch (region) {
        case 0:
            currentUi->PlaySound(game::CancelSoundText());
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
            currentUi->PlaySound(game::CancelSoundText());
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
        currentUi->PlaySound(game::SelectSoundText());
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
            currentUi->PlaySound(game::CancelSoundText());
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
        currentUi->PlaySound(game::SelectSoundText());
        ChooseFigure(defaults[0]);
        statsPanel->SetHero(0, game::kEmptyText);
        surfaces[0]->Clear();
        turn = 0.0f;
        SetMode(kModeNewHero);
        nameEdit->SetText(game::kEmptyText);
        return;
    case 6:
        currentUi->PlaySound(game::SelectSoundText());
        Rename();
        return;
    case 7: {
        // Delete: asks first ("lmp_deletepers").
        currentUi->PlaySound(game::SelectSoundText());
        if (!IsHero(heroIndex)) return;
        UiWindow* box = new MessageWindow(currentUi, "lmp_deletepers", "lmp_deletepers_msg", 0x10000, 3, 1);
        currentUi->PushWindow(box, true, true);
        return;
    }
    case 8: {
        // The hero's inventory: the camp screen.
        currentUi->PlaySound(game::SelectSoundText());
        if (!IsHero(heroIndex)) return;
        ui->PushWindow(new CampPages(ui, true), false, true);
        return;
    }
    default:
        break;
    }
    if (region < 9 || region >= 19) return;
    int index = heroTop + region - 9;
    if (!IsHero(index)) return;
    currentUi->PlaySound(game::SelectSoundText());
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
    auto* surface = surfaces[3];
    surface->Show();
    surface->Clear();
    RECT area = kRenameTitle;
    surface->DrawTextIn(name + kRenameSeparator, &area, 1, kBright, 2, 1);
}

// --- The voice's sound ----------------------------------------------------------------

void MpHeroScreen::PlayVoice() {
    ui->StopSound(voiceSound);
    voiceSound = -1;
    auto* hero = HeroRecord();
    UnitPrototypeRecord* prototype;
    {
        std::string key(hero->texts[6]);
        prototype = g_unitDatabase.prototypes.byName[key];
    }
    if (!prototype) return;
    acks::UnitAcks* set;
    {
        std::string key(prototype->name);
        // The sound sets (the units' acks) by name: operator[] inserts a null entry for an unknown name.
        set = g_ackDatabase.units.byName[key];
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
        voiceSound = ui->PlaySound(nth(index)->speech.sound.c_str());
        index = (index + 1) % count;
    }
}

// --- Continue -------------------------------------------------------------------------

void MpHeroScreen::Continue() {
    Interface3D* currentUi = ui;
    currentUi->PlaySound(kSoundOk);
    if (IsHero(heroIndex)) {
        g_savedParties.SaveAll();
        currentUi->PushWindow(new MpConnectScreen(currentUi), true, true);
        return;
    }
    UiWindow* box = new MessageWindow(currentUi, "lmp_no_pers", "lmp_no_pers_msg", 0x20000, 1, 1);
    currentUi->PushWindow(box, true, true);
}

// --- The new hero's statistics and its creation --------------------------------------

void MpHeroScreen::RefreshStats() {
    if (previewModel) {
        auto* hero = HeroRecord();
        auto* stats = &hero->stats;
        stats->RestoreSlots();
        // The third argument (the height's address) is not read.
        stats->GetFactors(&hero->scaleX, &hero->scaleY, 0);
        previewModel->SetFigureScale(&hero->scaleX);
        CUnit* unit = previewModel;
        unit->SetFromStats(&HeroRecord()->stats);

        unit->Controller()->RefreshWounds();
    }
    statsPanel->DrawStats();
}

void MpHeroScreen::CreateHero() {
    std::string name = nameEdit->text;
    auto* hero = HeroRecord();
    auto* stats = &hero->stats;
    float sum = stats->intelligenceBase + stats->dexterityBase + stats->strengthBase;
    float points = kAttributePoints;
    EI_NAN_AUDIT(sum, points);
    if (sum != points || static_cast<int>(name.size()) == 0) {
        ui->PlaySound(game::CancelSoundText());
        return;
    }
    ui->PlaySound(kSoundOk);
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
    Party()->Assign(const_cast<MpHeroRecord&>(other).Party());
    file = other.file;
    return *this;
}
MpHeroRecord::~MpHeroRecord() {}

void SavedParties::Select(const MpHeroRecord* hero) { outsideGame = *hero; }

void MpHeroScreen::DeleteHero() {
    if (!IsHero(heroIndex)) return;
    std::string path = g_savedParties.folder + g_savedParties.entries[heroIndex].file;
    gameapi::DeleteFileA(path.c_str());
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

void MpHeroScreen::MessageResult(int result) {
    if (result == kAnswerOk) DeleteHero();
    if (IsHero(heroIndex)) {
        MpHeroRecord* hero = g_savedParties.Hero(heroIndex);
        hero->Party()->Assign(LocalPlayerData());
        hero->WriteFile(g_savedParties.folder);
    }
    SetMode(kModeList);
}

// --- The new hero added to the list --------------------------------------------------

void MpHeroScreen::AddNewHero() {
    ui->PlaySound(kSoundOk);
    auto* hero = HeroRecord();
    // Only the chosen equipment stays (each list walked from its end; the count read once).
    for (int i = std::ssize(hero->weapons) - 1; i >= 0; --i)
        if (hero->weapons[i] != weapon)
            hero->RemoveWeapon(hero->weapons[i]);
    for (int i = std::ssize(hero->quickItems) - 1; i >= 0; --i)
        if (hero->quickItems[i] != quickItem)
            hero->RemoveQuickItem(hero->quickItems[i]);
    for (int i = std::ssize(hero->spells) - 1; i >= 0; --i)
        if (hero->spells[i] != spellContainer)
            hero->RemoveSpell(hero->spells[i]);
    hero->weaponIndex = !hero->weapons.empty() ? 0 : kNoWeaponSlot;
    if (!hero->spells.empty()) {
        auto* container = hero->spells[0];
        CSpell* spell = container->spell;
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
    record->Party()->Assign(LocalPlayerData());
    record->WriteFile(g_savedParties.folder);
    RefreshHeroScroll();
    DrawHeroList();
    SetMode(kModeList);
}
