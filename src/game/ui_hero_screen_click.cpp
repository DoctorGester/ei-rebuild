// The multiplayer hero screen's left button and keys.
// See docs/notes/ui.md, section 5.
#include "acks_db.h"
#include "console.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "item.h"
#include "item_db_text.h"
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
CItem* FirstOf(const std::vector<CItem*>& list) { return !list.empty() ? list[0] : nullptr; }

// Back to the main menu: the "reserved_04" status text, the session ended, the screen closed.
void BackToMainMenu(CInterface3D* ui) {
    std::string text;
    g_clientWorld.texts_->CTextManager::GetString("reserved_04", &text);
    g_interface.ShowStatusText(text);
    ui->PlaySound(game::CancelSoundText());
    g_gameFlow.EndSession();
    ui->SetPendingValue(0);
}

// A multiplayer hero's name: the text 0 of its party's first member record (read through the members'
// data pointer).
std::string& MpHeroName(int index) {
    auto* hero = g_savedParties.Hero(index);
    return hero->Party()->members_.data()->texts[0];
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

void CMpHeroScreen::CloseRenameEdit() {
    surfaces_[3]->Hide(false);
    renameEdit_->SetEnabled(false);
    renameEdit_->SetFocused(false);
}

void CMpHeroScreen::BackToNewHero() {
    interface_->PlaySound(game::CancelSoundText());
    statsPanel_->SetHero(0, game::kEmptyText);
    surfaces_[0]->Clear();
    tradePanel_->Settle(false, false);
    SetMode(kModeNewHero);
    nameEdit_->SetText(HeroRecord()->texts[0]);
}

void CMpHeroScreen::FinishRename() {
    std::string typed = renameEdit_->text_;
    str::TrimLeft(typed);
    str::TrimRight(typed);
    if (!typed.empty()) typed = kRenameSeparator + typed;
    std::string& name = MpHeroName(heroIndex_);
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
    g_savedParties.Hero(heroIndex_)->WriteFile(g_savedParties.folder_);
    CloseRenameEdit();
    ShowHero(heroIndex_);
}

void CMpHeroScreen::KeyDown(int key, int repeat, int flags) {
    CUiContainer::KeyDown(key, repeat, flags);
    if (handled_) return;
    CInterface3D* ui = interface_;
    bool renaming = renameEdit_->focused_;
    int heroStep = 0;
    int voiceStep = 0;
    switch (key) {
    case VK_RETURN:
        if (renaming)
            FinishRename();
        else if (mode_ == kModeList)
            Continue();
        else if (mode_ == kModeStats)
            AddNewHero();
        else if (mode_ == kModeNewHero)
            CreateHero();
        handled_ = true;
        return;
    case VK_ESCAPE:
        if (renaming) {
            CloseRenameEdit();
            DrawHeroList();
        } else if (mode_ == kModeList) {
            BackToMainMenu(ui);
        } else if (mode_ == kModeStats) {
            BackToNewHero();
        } else if (mode_ == kModeNewHero) {
            ui->PlaySound(game::CancelSoundText());
            SetMode(kModeList);
        }
        handled_ = true;
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
    if (mode_ == kModeList) {
        ui->PlaySound(game::SelectSoundText());
        heroIndex_ += heroStep;
        ClampHeroSelection();
    } else if (mode_ == kModeNewHero) {
        ui->PlaySound(game::SelectSoundText());
        voiceIndex_ += voiceStep;
        ClampVoiceSelection();
        PlayVoice();
    } else {
        return;
    }
    handled_ = true;
}

void CMpHeroScreen::LeftDown(int flags, int x, int y) {
    if (renameEdit_->focused_) return;
    CUiContainer::LeftDown(flags, x, y);
    if (handled_) return;
    CInterface3D* ui = interface_;
    int region = RegionAt(x, y);
    auto rotate = [&](int direction) {
        ui->PlaySound(game::RotateSoundText());
        rotating_ = direction;
        CaptureMouse();
    };

    if (mode_ == kModeNewHero) {
        // The new hero.
        switch (region) {
        case 0:
            ui->PlaySound(game::CancelSoundText());
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
            ui->PlaySound(game::CancelSoundText());
            ChooseFigure(defaults_[0]);
            PartyUnitRecord* hero = HeroRecord();
            auto* stats = &hero->stats;
            stats->strengthBase = 25.0f;
            stats->strength = 25.0f;
            stats->dexterityBase = 25.0f;
            stats->dexterity = 25.0f;
            stats->intelligenceBase = 25.0f;
            stats->intelligence = 25.0f;
            hero->height = 0.7f;
            nameEdit_->SetText(game::kEmptyText);
            statsPanel_->SetHero(0, game::kEmptyText);
            RefreshStats();
            return;
        }
        default:
            break;
        }
        if (region < 9 || region >= 20) return;
        // A voice row: the hero takes its file.
        int index = voiceTop_ + region - 9;
        if (index < 0) return;
        std::vector<MpVoiceEntry>& voices = *voices_;
        if (static_cast<unsigned>(index) >= static_cast<unsigned>(voices.end() - voices.begin())) return;
        ui->PlaySound(game::SelectSoundText());
        voiceIndex_ = index;
        const std::string& file = voices.begin()[index].file;
        HeroRecord()->texts[6] = file;
        PlayVoice();
        DrawVoices();
        return;
    }

    if (mode_ == kModeStats) {
        // The new hero's statistics page.
        if (region == 0) {
            BackToNewHero();
        } else if (region == 1) {
            AddNewHero();
        } else if (region == 4) {
            // Take the equipment back.
            ui->PlaySound(game::CancelSoundText());
            tradePanel_->Settle(false, false);
            ShowFirstEquipment();
        }
        return;
    }

    if (mode_ != kModeList) return;
    // The hero list. A click while a hero is being renamed ends the renaming first (only reached
    // when the base handler started it: the handler returns at once while renaming).
    if (region >= 0 && renameEdit_->focused_) CloseRenameEdit();
    switch (region) {
    case 0:
        BackToMainMenu(ui);
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
        ui->PlaySound(game::SelectSoundText());
        ChooseFigure(defaults_[0]);
        statsPanel_->SetHero(0, game::kEmptyText);
        surfaces_[0]->Clear();
        turn_ = 0.0f;
        SetMode(kModeNewHero);
        nameEdit_->SetText(game::kEmptyText);
        return;
    case 6:
        ui->PlaySound(game::SelectSoundText());
        Rename();
        return;
    case 7: {
        // Delete: asks first ("lmp_deletepers").
        ui->PlaySound(game::SelectSoundText());
        if (!IsHero(heroIndex_)) return;
        CUiWindow* box = new CMessageBox(ui, "lmp_deletepers", "lmp_deletepers_msg", 0x10000, 3, 1);
        ui->PushWindow(box, true, true);
        return;
    }
    case 8: {
        // The hero's inventory: the camp screen.
        ui->PlaySound(game::SelectSoundText());
        if (!IsHero(heroIndex_)) return;
        interface_->PushWindow(new CCampPages(interface_, true), false, true);
        return;
    }
    default:
        break;
    }
    if (region < 9 || region >= 19) return;
    int index = heroTop_ + region - 9;
    if (!IsHero(index)) return;
    ui->PlaySound(game::SelectSoundText());
    ShowHero(index);
}

// --- Rename ---------------------------------------------------------------------------

void CMpHeroScreen::Rename() {
    if (!IsHero(heroIndex_)) return;
    renameEdit_->SetEnabled(true);
    renameEdit_->SetFocused(true);
    std::string name = MpHeroName(heroIndex_);
    int bar = str::Find(name, kRenameBar);
    if (bar < 0) {
        renameEdit_->SetText(game::kEmptyText);
    } else {
        std::string typed = str::Mid(name, bar + 1);
        str::TrimLeft(typed);
        renameEdit_->SetText(typed);
        name = str::Left(name, bar);
        str::TrimRight(name);
    }
    auto* surface = surfaces_[3];
    surface->Show();
    surface->Clear();
    RECT rect = kRenameTitle;
    surface->DrawTextIn(name + kRenameSeparator, &rect, 1, kBright, 2, 1);
}

// --- The voice's sound ----------------------------------------------------------------

void CMpHeroScreen::PlayVoice() {
    interface_->StopSound(voiceSound_);
    voiceSound_ = -1;
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
    const acks::EnvironmentSpeeches& entries = set->environmentSpeeches;
    for (const acks::EnvironmentSpeech& e : entries)
        if (IsVoice(e)) ++count;
    if (count == 0) return;
    auto nth = [&](int rank) -> const acks::EnvironmentSpeech* {
        for (const acks::EnvironmentSpeech& e : entries)
            if (IsVoice(e) && rank-- == 0) return &e;
        return nullptr;
    };
    int index = ClientRandBelow(count);
    for (int tries = 0; tries < count && voiceSound_ == -1; ++tries) {
        voiceSound_ = interface_->PlaySound(nth(index)->speech.sound.c_str());
        index = (index + 1) % count;
    }
}

// --- Continue -------------------------------------------------------------------------

void CMpHeroScreen::Continue() {
    CInterface3D* ui = interface_;
    ui->PlaySound(kSoundOk);
    if (IsHero(heroIndex_)) {
        g_savedParties.SaveAll();
        ui->PushWindow(new CMpConnectScreen(ui), true, true);
        return;
    }
    CUiWindow* box = new CMessageBox(ui, "lmp_no_pers", "lmp_no_pers_msg", 0x20000, 1, 1);
    ui->PushWindow(box, true, true);
}

// --- The new hero's statistics and its creation --------------------------------------

void CMpHeroScreen::RefreshStats() {
    if (previewModel_) {
        auto* hero = HeroRecord();
        auto* stats = &hero->stats;
        stats->RestoreSlots();
        // The third argument (the height's address) is not read.
        stats->GetFactors(&hero->scaleX, &hero->scaleY, 0);
        previewModel_->SetFigureScale(&hero->scaleX);
        CUnit* unit = previewModel_;
        unit->SetFromStats(&HeroRecord()->stats);

        unit->Controller()->RefreshWounds();
    }
    statsPanel_->DrawStats();
}

void CMpHeroScreen::CreateHero() {
    std::string name = nameEdit_->text_;
    auto* hero = HeroRecord();
    auto* stats = &hero->stats;
    double sum = static_cast<double>(stats->intelligenceBase) + stats->dexterityBase + stats->strengthBase;
    float points = kAttributePoints;
    EI_NAN_AUDIT(sum, points);
    if (sum != points || static_cast<int>(name.size()) == 0) {
        interface_->PlaySound(game::CancelSoundText());
        return;
    }
    interface_->PlaySound(kSoundOk);
    ShowFirstEquipment();
    hero->texts[0] = name;
    statsPanel_->SetHero(0, name);
    RefreshStats();
    SetMode(kModeStats);
}

void CMpHeroScreen::ShowFirstEquipment() {
    PartyUnitRecord* hero = HeroRecord();
    weapon_ = FirstOf(hero->weapons);
    weaponPanel_->ShowWeapon(weapon_);
    quickItem_ = FirstOf(hero->quickItems);
    weaponPanel_->ShowQuickItem(quickItem_);
    spellContainer_ = FirstOf(hero->spells);
    spellPanel_->ShowEntry(spellContainer_);
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

void SavedParties::Select(const MpHeroRecord* hero) { outsideGame_ = *hero; }

void CMpHeroScreen::DeleteHero() {
    if (!IsHero(heroIndex_)) return;
    std::string path = g_savedParties.folder_ + g_savedParties.entries_[heroIndex_].file;
    gameapi::DeleteFileA(path.c_str());
    g_savedParties.entries_.erase(g_savedParties.entries_.begin() + heroIndex_);
    if (heroIndex_ >= g_savedParties.Count()) heroIndex_ = g_savedParties.Count() - 1;
    RefreshHeroScroll();
    DrawHeroList();
    ShowHero(heroIndex_);
}

void CMpHeroScreen::RefreshHeroScroll() {
    CUiScrollbar* bar = heroScroll_;
    if (g_savedParties.Count() > kListRows) {
        const int position = bar->TakePosition();
        bar->SetMaximum(g_savedParties.Count() - kListRows);
        bar->SetPosition(position);
        heroTop_ = bar->TakePosition();
    } else {
        bar->SetMaximum(0);
        heroTop_ = 0;
    }
}

void CMpHeroScreen::MessageResult(int result) {
    if (result == kAnswerOk) DeleteHero();
    if (IsHero(heroIndex_)) {
        MpHeroRecord* hero = g_savedParties.Hero(heroIndex_);
        hero->Party()->Assign(LocalPlayerData());
        hero->WriteFile(g_savedParties.folder_);
    }
    SetMode(kModeList);
}

// --- The new hero added to the list --------------------------------------------------

void CMpHeroScreen::AddNewHero() {
    interface_->PlaySound(kSoundOk);
    auto* hero = HeroRecord();
    // Only the chosen equipment stays (each list walked from its end; the count read once).
    for (int i = std::ssize(hero->weapons) - 1; i >= 0; --i)
        if (hero->weapons[i] != weapon_)
            hero->RemoveWeapon(hero->weapons[i]);
    for (int i = std::ssize(hero->quickItems) - 1; i >= 0; --i)
        if (hero->quickItems[i] != quickItem_)
            hero->RemoveQuickItem(hero->quickItems[i]);
    for (int i = std::ssize(hero->spells) - 1; i >= 0; --i)
        if (hero->spells[i] != spellContainer_)
            hero->RemoveSpell(hero->spells[i]);
    hero->weaponIndex = !hero->weapons.empty() ? 0 : kNoWeaponSlot;
    if (!hero->spells.empty()) {
        auto* container = hero->spells[0];
        CSpell* spell = container->spell_;
        if (spell && !HeroCanUseSpell(&hero->stats, spell)) {
            LocalPlayerData()->AddItem(container, 0, 1);
            hero->RemoveSpell(container);
        }
    }

    // A new record; at capacity the original copies a blank one into the grown storage (a party copy draws ids).
    std::vector<MpHeroRecord>& heroes = g_savedParties.entries_;
    if (heroes.size() == heroes.capacity()) {
        MpHeroRecord blank;
        g_savedParties.ReserveOne();
        heroes.push_back(blank);
    } else {
        heroes.emplace_back();
    }
    MpHeroRecord* record = &heroes.back();
    heroIndex_ = static_cast<int>(heroes.size()) - 1;

    const int highest = HighestFileNumber(std::format("{}*.mp", g_savedParties.folder_));
    record->file = std::format("{}.mp", highest + 1);
    record->Party()->Assign(LocalPlayerData());
    record->WriteFile(g_savedParties.folder_);
    RefreshHeroScroll();
    DrawHeroList();
    SetMode(kModeList);
}
