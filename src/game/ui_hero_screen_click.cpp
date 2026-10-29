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

#include "lib/crt.h"
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

namespace {

// The hero screen's own methods (on the window): the mode (Rebirth-patched), create the hero, the
// statistics page's next, continue to the connection screen, rename, the voice's sound.
void SetMode(CMpHeroScreen* screen, int mode) { screen->SetModeVanilla(mode); }
void MpHeroCreate(CMpHeroScreen*);
void MpHeroAddNew(CMpHeroScreen*);
void MpHeroContinue(CMpHeroScreen*);
void MpHeroRename(CMpHeroScreen*);
void MpHeroPlayVoice(CMpHeroScreen*);


int HeroCount() {
    return SavedHeroCount();
}

// The first entry of one of a hero record's item arrays, or null when it is empty.
CItem* FirstOf(const std::vector<CItem*>& list) { return !list.empty() ? list[0] : nullptr; }

// Back to the main menu: the "reserved_04" status text, the session ended, the screen closed.
void BackToMainMenu(CInterface3D* ui) {
    std::string text;
    g_clientWorld->texts_->CTextManager::GetString("reserved_04", &text);
    g_interface->ShowStatusText(text);
    ui->PlaySound(game::CancelSoundText());
    g_gameFlow->EndSession();
    ui->SetPendingValue(0);
}

const char* HeroesFolder() { return g_savedParties->folder_.c_str(); }
const char* RenameSeparator() { return " | "; }
const char* RenameBar() { return "|"; }

constexpr int kModeList = 9;
constexpr int kModeNewHero = 10;
constexpr int kModeStats = 11;

// A multiplayer hero's name: the text 0 of its party's first member record (read through the members'
// data pointer).
std::string& MpHeroName(int index) {
    auto* hero = SavedHero(index);
    return hero->Party()->members_.data()->texts[0];
}

// The rename's title on surface 3: 130 x 16 pixels.
constexpr RECT kRenameTitle = {0, 0, 0x82, 0x10};
constexpr std::uint32_t kBright = 0xA7D7E4;

void MpHeroRename(CMpHeroScreen* self) { self->Rename(); }

constexpr std::uint32_t kVoiceFlags = 0x2282;

// A voice: an environment speech whose flags include kVoiceFlags, with a chance.
bool IsVoice(const acks::EnvironmentSpeech& entry) {
    return (entry.environment & kVoiceFlags) == kVoiceFlags && entry.speech.chance != 0;
}

void MpHeroPlayVoice(CMpHeroScreen* self) { self->PlayVoice(); }

const char* SoundOk() { return "buttons\\messbox\\ok.wav"; }

void MpHeroContinue(CMpHeroScreen* self) { self->Continue(); }

const char* SoundCancel() { return game::CancelSoundText(); }
// The points the three attributes must add up to (a float in the exe's data).
constexpr float kAttributePoints = 75.0f;

void MpHeroCreate(CMpHeroScreen* self) { self->CreateHero(); }

// The heroes' folder and the records.
const std::string& MpHeroesFolder() { return g_savedParties->folder_; }
std::uint32_t MpHeroCount() { return static_cast<std::uint32_t>(g_savedParties->entries_.size()); }
constexpr std::uint32_t kListRows = 10;
constexpr int kAnswerOk = 0x10001;

void MpHeroDelete(CMpHeroScreen* self) { self->DeleteHero(); }

const char* HeroFilePattern() { return "%s*.mp"; }
const char* HeroFileFormat() { return "%d.mp"; }

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

void MpHeroAddNew(CMpHeroScreen* self) { self->AddNewHero(); }

} // namespace

void CMpHeroScreen::CloseRenameEdit() {
    surfaces_[3]->Freeze(0, 0);
    renameEdit_->SetEnabled(false);
    renameEdit_->SetFocused(false);
}

void CMpHeroScreen::BackToNewHero() {
    interface_->PlaySound(game::CancelSoundText());
    statsPanel_->SetHero(0, game::kEmptyText);
    surfaces_[0]->Clear();
    tradePanel_->Settle(false, false);
    SetMode(this, kModeNewHero);
    nameEdit_->SetText(HeroRecord()->texts[0]);
}

void CMpHeroScreen::FinishRename() {
    std::string typed = renameEdit_->text_;
    str::TrimLeft(typed);
    str::TrimRight(typed);
    if (typed.size() != 0) typed = std::string(RenameSeparator()) + typed;
    std::string& name = MpHeroName(heroIndex_);
    std::string title = name;
    std::string base;
    int bar = str::Find(title, RenameBar());
    if (bar >= 0)
        base = str::Left(title, bar);
    else
        base = title;
    str::TrimLeft(base);
    str::TrimRight(base);
    typed = base + typed;
    name = typed;
    SavedHero(heroIndex_)->WriteFile(HeroesFolder());
    CloseRenameEdit();
    CallShowHero(heroIndex_);
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
            MpHeroContinue(this);
        else if (mode_ == kModeStats)
            MpHeroAddNew(this);
        else if (mode_ == kModeNewHero)
            MpHeroCreate(this);
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
            SetMode(this, kModeList);
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
        MpHeroPlayVoice(this);
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
        UiCapture(ui) = Input();
    };

    if (mode_ == 10) {
        // The new hero.
        switch (region) {
        case 0:
            ui->PlaySound(game::CancelSoundText());
            SetMode(this, 9);
            return;
        case 1:
            MpHeroCreate(this);
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
        MpHeroPlayVoice(this);
        DrawVoices();
        return;
    }

    if (mode_ == 11) {
        // The new hero's statistics page.
        if (region == 0) {
            BackToNewHero();
        } else if (region == 1) {
            MpHeroAddNew(this);
        } else if (region == 4) {
            // Take the equipment back: the hero's first weapon, armour and spell container.
            ui->PlaySound(game::CancelSoundText());
            tradePanel_->Settle(false, false);
            PartyUnitRecord* hero = HeroRecord();
            weapon_ = FirstOf(hero->weapons);
            weaponPanel_->ShowWeapon(weapon_);
            armor_ = FirstOf(hero->quickItems);
            weaponPanel_->ShowArmour(armor_);
            spellContainer_ = FirstOf(hero->spells);
            spellPanel_->ShowEntry(spellContainer_);
        }
        return;
    }

    if (mode_ != 9) return;
    // The hero list. A click while a hero is being renamed ends the renaming first (only reached
    // when the base handler started it: the handler returns at once while renaming).
    if (region >= 0 && renameEdit_->focused_) CloseRenameEdit();
    switch (region) {
    case 0:
        BackToMainMenu(ui);
        return;
    case 1:
        MpHeroContinue(this);
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
        SetMode(this, 10);
        nameEdit_->SetText(game::kEmptyText);
        return;
    case 6:
        ui->PlaySound(game::SelectSoundText());
        MpHeroRename(this);
        return;
    case 7: {
        // Delete: asks first ("lmp_deletepers").
        ui->PlaySound(game::SelectSoundText());
        if (heroIndex_ < 0 || static_cast<unsigned>(heroIndex_) >= static_cast<unsigned>(HeroCount())) return;
        CUiWindow* box = new CMessageBox(ui, "lmp_deletepers", "lmp_deletepers_msg", 0x10000, 3, 1);
        ui->PushWindow(box, 1, 1);
        return;
    }
    case 8: {
        // The hero's inventory: the camp screen.
        ui->PlaySound(game::SelectSoundText());
        if (heroIndex_ < 0 || static_cast<unsigned>(heroIndex_) >= static_cast<unsigned>(HeroCount())) return;
        interface_->PushWindow(new CCampPages(interface_, true), 0, 1);
        return;
    }
    default:
        break;
    }
    if (region < 9 || region >= 19) return;
    int index = heroTop_ + region - 9;
    if (index < 0 || static_cast<unsigned>(index) >= static_cast<unsigned>(HeroCount())) return;
    ui->PlaySound(game::SelectSoundText());
    CallShowHero(index);
}

// --- Rename ---------------------------------------------------------------------------

void CMpHeroScreen::Rename() {
    if (heroIndex_ < 0 || static_cast<unsigned>(heroIndex_) >= static_cast<unsigned>(HeroCount())) return;
    renameEdit_->SetEnabled(true);
    renameEdit_->SetFocused(true);
    std::string name = MpHeroName(heroIndex_);
    int bar = str::Find(name, RenameBar());
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
    surface->Freeze(1, 0);
    surface->Clear();
    RECT rect = kRenameTitle;
    surface->DrawTextIn((name + RenameSeparator()), &rect, 1, kBright, 2, 1);
}

// --- The voice's sound ----------------------------------------------------------------

void CMpHeroScreen::PlayVoice() {
    interface_->StopSound(voiceSound_);
    voiceSound_ = -1;
    auto* hero = HeroRecord();
    UnitPrototypeRecord* prototype;
    {
        std::string key(hero->texts[6]);
        prototype = g_unitDatabase->prototypes.byName[key];
    }
    if (!prototype) return;
    acks::UnitAcks* set;
    {
        std::string key(prototype->name);
        // The sound sets (the units' acks) by name: operator[] inserts a null entry for an unknown name.
        set = g_ackDatabase->units.byName[key];
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
    int index = libcrt::RandBelow(count);
    for (int tries = 0; tries < count && voiceSound_ == -1; ++tries) {
        voiceSound_ = interface_->PlaySound(nth(index)->speech.sound.c_str());
        index = (index + 1) % count;
    }
}

// --- Continue -------------------------------------------------------------------------

void CMpHeroScreen::Continue() {
    CInterface3D* ui = interface_;
    ui->PlaySound(SoundOk());
    if (heroIndex_ >= 0 && static_cast<unsigned>(heroIndex_) < static_cast<unsigned>(HeroCount())) {
        g_savedParties->SaveAll();
        ui->PushWindow(new CMpConnectScreen(ui), 1, 1);
        return;
    }
    CUiWindow* box = new CMessageBox(ui, "lmp_no_pers", "lmp_no_pers_msg", 0x20000, 1, 1);
    ui->PushWindow(box, 1, 1);
}

// --- The new hero's statistics and its creation --------------------------------------

void CMpHeroScreen::RefreshHero() {
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
        interface_->PlaySound(SoundCancel());
        return;
    }
    interface_->PlaySound("buttons\\messbox\\ok.wav");
    weapon_ = !hero->weapons.empty() ? hero->weapons[0] : nullptr;
    weaponPanel_->ShowWeapon(weapon_);
    armor_ = !hero->quickItems.empty() ? hero->quickItems[0] : nullptr;
    weaponPanel_->ShowArmour(armor_);
    spellContainer_ = !hero->spells.empty() ? hero->spells[0] : nullptr;
    spellPanel_->ShowEntry(spellContainer_);
    hero->texts[0] = name;
    statsPanel_->SetHero(0, name);
    RefreshStats();
    SetMode(this, kModeStats);
}
// --- A hero deleted, the message box's answer ------------------------------------------

MpHeroRecord::MpHeroRecord() { new (Party()) CParty(); }
MpHeroRecord::MpHeroRecord(const MpHeroRecord& other) : file(other.file) {
    new (Party()) CParty(*const_cast<MpHeroRecord&>(other).Party());
}
MpHeroRecord& MpHeroRecord::operator=(const MpHeroRecord& other) {
    Party()->Assign(const_cast<MpHeroRecord&>(other).Party());
    file = other.file;
    return *this;
}
MpHeroRecord::~MpHeroRecord() { Party()->~CParty(); }

void SavedParties::Select(const MpHeroRecord* hero) { outsideGame_ = *hero; }

void CMpHeroScreen::DeleteHero() {
    if (heroIndex_ < 0 || static_cast<std::uint32_t>(heroIndex_) >= MpHeroCount()) return;
    std::string path = MpHeroesFolder() + g_savedParties->entries_[heroIndex_].file;
    gameapi::DeleteFileA(path.c_str());
    g_savedParties->entries_.erase(g_savedParties->entries_.begin() + heroIndex_);
    std::uint32_t count = MpHeroCount();
    if (static_cast<std::uint32_t>(heroIndex_) >= count) heroIndex_ = static_cast<int>(count) - 1;
    RefreshHeroScroll();
    DrawHeroList();
    CallShowHero(heroIndex_);
}

void CMpHeroScreen::RefreshHeroScroll() {
    CUiScrollbar* bar = heroScroll_;
    if (MpHeroCount() > kListRows) {
        int position;
        bar->TakePosition(&position);
        bar->SetMaximum(static_cast<int>(MpHeroCount() - kListRows));
        bar->SetPosition(position);
        bar->TakePosition(&heroTop_);
    } else {
        bar->SetMaximum(0);
        heroTop_ = 0;
    }
}

void CMpHeroScreen::MessageResult(int result) {
    if (result == kAnswerOk) MpHeroDelete(this);
    if (heroIndex_ >= 0 && static_cast<std::uint32_t>(heroIndex_) < MpHeroCount()) {
        MpHeroRecord* hero = &g_savedParties->entries_[heroIndex_];
        hero->Party()->Assign(LocalPlayerData());
        hero->WriteFile(HeroesFolder());
    }
    SetMode(this, kModeList);
}

// --- The new hero added to the list --------------------------------------------------

void CMpHeroScreen::AddNewHero() {
    interface_->PlaySound("buttons\\messbox\\ok.wav");
    auto* hero = HeroRecord();
    // Only the chosen equipment stays (each list walked from its end; the count read once).
    for (int i = std::ssize(hero->weapons) - 1; i >= 0; --i)
        if (hero->weapons[i] != weapon_)
            hero->RemoveWeapon(hero->weapons[i]);
    for (int i = std::ssize(hero->quickItems) - 1; i >= 0; --i)
        if (hero->quickItems[i] != armor_)
            hero->RemoveQuickItem(hero->quickItems[i]);
    for (int i = std::ssize(hero->spells) - 1; i >= 0; --i)
        if (hero->spells[i] != spellContainer_)
            hero->RemoveSpell(hero->spells[i]);
    hero->weaponIndex = !hero->weapons.empty() ? 0 : 0xFFFFFFFFu;
    if (!hero->spells.empty()) {
        auto* container = hero->spells[0];
        CSpell* spell = container->spell_;
        if (spell && !HeroCanUseSpell(&hero->stats, spell)) {
            LocalPlayerData()->AddItem(container, 0, 1);
            hero->RemoveSpell(container);
        }
    }

    // A new record; at capacity the original copies a blank one into the grown storage (a party copy draws ids).
    std::vector<MpHeroRecord>& heroes = g_savedParties->entries_;
    if (heroes.size() == heroes.capacity()) {
        MpHeroRecord blank;
        g_savedParties->ReserveOne();
        heroes.push_back(blank);
    } else {
        heroes.emplace_back();
    }
    MpHeroRecord* record = &heroes.back();
    heroIndex_ = static_cast<int>(heroes.size()) - 1;

    std::string pattern;
    pattern = str::Format(HeroFilePattern(), MpHeroesFolder());
    int highest = HighestFileNumber(pattern);
    record->file = str::Format(HeroFileFormat(), highest + 1);
    record->Party()->Assign(LocalPlayerData());
    record->WriteFile(HeroesFolder());
    RefreshHeroScroll();
    DrawHeroList();
    SetMode(this, kModeList);
}
