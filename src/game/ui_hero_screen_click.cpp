// The multiplayer hero screen's left button and keys.
// See docs/notes/ui.md, section 5.
#include "ui.h"
#include "ui_screens.h"
#include "game_strings.h"
#include "ui_camp.h"
#include "acks_db.h"
#include "unit_create.h"
#include "unit_db.h"
#include "item_db_text.h"

#include "lib/file_io.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstdint>
#include <new>
#include <string>
#include "console.h"
#include "item.h"
#include "party.h"
#include "unit.h"
#include "map_object_figures.h"
#include "world.h"
#include "world_glue.h"
#include "lib/nan_audit.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {

// The hero screen's own methods (on the window): the mode (Rebirth-patched), create the hero, the
// statistics page's next, continue to the connection screen, rename, the voice's sound.
void SetMode(CMpHeroScreen* screen, void*, int mode) { screen->SetModeVanilla(mode); }
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
    GameClientTexts()->CTextManager::GetString("reserved_04", &text);
    TheInterface()->ShowStatusText(text.c_str());
    uiorig::PlayUiSound(ui, game::CancelSoundText(), 0);
    TheGameFlow()->EndSession();
    ui->SetPendingValue(0);
}

const char* HeroesFolder() { return TheSavedParties().folder_.c_str(); }
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

} // namespace

void CMpHeroScreen::CloseRenameEdit() {
    surfaces_[3]->Freeze(0, 0);
    renameEdit_->SetEnabled(0, 1);
    renameEdit_->SetFocused(false);
}

void CMpHeroScreen::BackToNewHero() {
    uiorig::PlayUiSound(interface_, game::CancelSoundText(), 0);
    statsPanel_->SetHero(0, const_cast<char*>(game::EmptyText()));
    surfaces_[0]->GetDC();
    surfaces_[0]->Clear();
    surfaces_[0]->ReleaseDC();
    tradePanel_->Settle(0, 0);
    SetMode(this, nullptr, kModeNewHero);
    nameEdit_->SetText(HeroRecord()->texts[0].c_str());
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

void CMpHeroScreen::OnKeyDown(int key, int repeat, int flags) {
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
            uiorig::PlayUiSound(ui, game::CancelSoundText(), 0);
            SetMode(this, nullptr, kModeList);
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
        uiorig::PlayUiSound(ui, game::SelectSoundText(), 0);
        heroIndex_ += heroStep;
        ClampHeroSelection();
    } else if (mode_ == kModeNewHero) {
        uiorig::PlayUiSound(ui, game::SelectSoundText(), 0);
        voiceIndex_ += voiceStep;
        ClampVoiceSelection();
        MpHeroPlayVoice(this);
    } else {
        return;
    }
    handled_ = true;
}

void CMpHeroScreen::OnLButtonDown(int flags, int x, int y) {
    if (renameEdit_->focused_) return;
    CUiContainer::LeftDown(flags, x, y);
    if (handled_) return;
    CInterface3D* ui = interface_;
    int region = RegionAt(x, y);
    auto rotate = [&](int direction) {
        uiorig::PlayUiSound(ui, game::RotateSoundText(), 0);
        rotating_ = direction;
        UiCapture(ui) = Input();
    };

    if (mode_ == 10) {
        // The new hero.
        switch (region) {
        case 0:
            uiorig::PlayUiSound(ui, game::CancelSoundText(), 0);
            SetMode(this, nullptr, 9);
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
            uiorig::PlayUiSound(ui, game::CancelSoundText(), 0);
            ChooseFigure(defaults_[0].c_str());
            PartyUnitRecord* hero = HeroRecord();
            auto* stats = &hero->stats;
            stats->stat10Base = 25.0f;
            stats->stat10 = 25.0f;
            stats->stat18Base = 25.0f;
            stats->stat18 = 25.0f;
            stats->stat20Base = 25.0f;
            stats->stat20 = 25.0f;
            hero->height = 0.7f;
            nameEdit_->SetText(game::EmptyText());
            statsPanel_->SetHero(0, const_cast<char*>(game::EmptyText()));
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
        MpVoiceVector& voices = Voices();
        if (static_cast<unsigned>(index) >= static_cast<unsigned>(voices.end() - voices.begin())) return;
        uiorig::PlayUiSound(ui, game::SelectSoundText(), 0);
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
            uiorig::PlayUiSound(ui, game::CancelSoundText(), 0);
            tradePanel_->Settle(0, 0);
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
        uiorig::PlayUiSound(ui, game::SelectSoundText(), 0);
        ChooseFigure(defaults_[0].c_str());
        statsPanel_->SetHero(0, const_cast<char*>(game::EmptyText()));
        surfaces_[0]->GetDC();
        surfaces_[0]->Clear();
        surfaces_[0]->ReleaseDC();
        turn_ = 0.0f;
        SetMode(this, nullptr, 10);
        nameEdit_->SetText(game::EmptyText());
        return;
    case 6:
        uiorig::PlayUiSound(ui, game::SelectSoundText(), 0);
        MpHeroRename(this);
        return;
    case 7: {
        // Delete: asks first ("lmp_deletepers").
        uiorig::PlayUiSound(ui, game::SelectSoundText(), 0);
        if (heroIndex_ < 0 || static_cast<unsigned>(heroIndex_) >= static_cast<unsigned>(HeroCount())) return;
        CUiWindow* box = new CMessageBox(ui, "lmp_deletepers", "lmp_deletepers_msg", 0x10000, 3, 1);
        ui->PushWindow(box, 1, 1);
        return;
    }
    case 8: {
        // The hero's inventory: the camp screen.
        uiorig::PlayUiSound(ui, game::SelectSoundText(), 0);
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
    uiorig::PlayUiSound(ui, game::SelectSoundText(), 0);
    CallShowHero(index);
}

// --- Rename ---------------------------------------------------------------------------

namespace {
// The rename's title on surface 3: 130 x 16 pixels.
constexpr RECT kRenameTitle = {0, 0, 0x82, 0x10};
constexpr std::uint32_t kBright = 0xA7D7E4;
} // namespace

void CMpHeroScreen::Rename() {
    if (heroIndex_ < 0 || static_cast<unsigned>(heroIndex_) >= static_cast<unsigned>(HeroCount())) return;
    renameEdit_->SetEnabled(1, 1);
    renameEdit_->SetFocused(true);
    std::string name = MpHeroName(heroIndex_);
    int bar = str::Find(name, RenameBar());
    if (bar < 0) {
        renameEdit_->SetText(game::EmptyText());
    } else {
        std::string typed = str::Mid(name, bar + 1);
        str::TrimLeft(typed);
        renameEdit_->SetText(typed.c_str());
        name = str::Left(name, bar);
        str::TrimRight(name);
    }
    auto* surface = surfaces_[3];
    surface->Freeze(1, 0);
    surface->GetDC();
    surface->Clear();
    RECT rect = kRenameTitle;
    surface->DrawTextIn((name + RenameSeparator()), &rect, 1, kBright, 2, 1);
    surface->ReleaseDC();
}
namespace {
void MpHeroRename(CMpHeroScreen* self) { self->Rename(); }
} // namespace

// --- The voice's sound ----------------------------------------------------------------

namespace {
constexpr std::uint32_t kVoiceFlags = 0x2282;

// A voice: an environment speech whose flags include kVoiceFlags, with a chance.
bool IsVoice(const acks::EnvironmentSpeech& entry) {
    return (entry.environment & kVoiceFlags) == kVoiceFlags && entry.speech.chance != 0;
}
} // namespace

void CMpHeroScreen::PlayVoice() {
    interface_->StopSound(voiceSound_);
    voiceSound_ = -1;
    auto* hero = HeroRecord();
    UnitPrototypeRecord* prototype;
    {
        std::string key(hero->texts[6].c_str());
        prototype = unitdb::PrototypeTable().byName[key];
    }
    if (!prototype) return;
    acks::UnitAcks* set;
    {
        std::string key(prototype->name.c_str());
        // The sound sets (the units' acks) by name: operator[] inserts a null entry for an unknown name.
        set = acks::Database().units.byName[key];
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
    int index = libcrt::Rand() * count / 0x7FFF % count;
    for (int tries = 0; tries < count && voiceSound_ == -1; ++tries) {
        voiceSound_ = interface_->PlaySound(nth(index)->speech.sound.c_str(), 0);
        index = (index + 1) % count;
    }
}
namespace {
void MpHeroPlayVoice(CMpHeroScreen* self) { self->PlayVoice(); }
} // namespace

// --- Continue -------------------------------------------------------------------------

namespace {
const char* SoundOk() { return "buttons\\messbox\\ok.wav"; }
SavedParties* MpHeroesObject() { return &TheSavedParties(); }

} // namespace

void CMpHeroScreen::Continue() {
    CInterface3D* ui = interface_;
    ui->PlaySound(SoundOk(), 0);
    if (heroIndex_ >= 0 && static_cast<unsigned>(heroIndex_) < static_cast<unsigned>(HeroCount())) {
        MpHeroesObject()->SaveAll();
        ui->PushWindow(new CMpConnectScreen(ui), 1, 1);
        return;
    }
    CUiWindow* box = new CMessageBox(ui, "lmp_no_pers", "lmp_no_pers_msg", 0x20000, 1, 1);
    ui->PushWindow(box, 1, 1);
}
namespace {
void MpHeroContinue(CMpHeroScreen* self) { self->Continue(); }
} // namespace

// --- The new hero's statistics and its creation --------------------------------------

namespace {
const char* SoundCancel() { return game::CancelSoundText(); }
// The points the three attributes must add up to (a float in the exe's data).
float AttributePoints() { return 75.0f /*00741C98*/; }
} // namespace

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
    double sum = static_cast<double>(stats->stat20Base) + stats->stat18Base + stats->stat10Base;
    float points = AttributePoints();
    EI_NAN_AUDIT(sum, points);
    if (sum != points || static_cast<int>(name.size()) == 0) {
        interface_->PlaySound(SoundCancel(), 0);
        return;
    }
    interface_->PlaySound("buttons\\messbox\\ok.wav", 0);
    weapon_ = !hero->weapons.empty() ? hero->weapons[0] : nullptr;
    weaponPanel_->ShowWeapon(weapon_);
    armor_ = !hero->quickItems.empty() ? hero->quickItems[0] : nullptr;
    weaponPanel_->ShowArmour(armor_);
    spellContainer_ = !hero->spells.empty() ? hero->spells[0] : nullptr;
    spellPanel_->ShowEntry(spellContainer_);
    hero->texts[0] = name;
    statsPanel_->SetHero(0, const_cast<char*>(name.c_str()));
    RefreshStats();
    SetMode(this, nullptr, kModeStats);
}
namespace {
void MpHeroCreate(CMpHeroScreen* self) { self->CreateHero(); }
} // namespace
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

namespace {
// The heroes' folder and the records.
const std::string& MpHeroesFolder() { return TheSavedParties().folder_; }
std::vector<MpHeroRecord>& MpHeroes() { return TheSavedParties().entries_; }
std::uint32_t MpHeroCount() { return static_cast<std::uint32_t>(MpHeroes().size()); }
constexpr std::uint32_t kListRows = 10;
constexpr int kAnswerOk = 0x10001;
} // namespace

void CMpHeroScreen::DeleteHero() {
    if (heroIndex_ < 0 || static_cast<std::uint32_t>(heroIndex_) >= MpHeroCount()) return;
    std::string path = MpHeroesFolder() + MpHeroes()[heroIndex_].file;
    gameapi::DeleteFileA(path.c_str());
    MpHeroes().erase(MpHeroes().begin() + heroIndex_);
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
namespace {
void MpHeroDelete(CMpHeroScreen* self) { self->DeleteHero(); }
} // namespace

void CMpHeroScreen::OnMessageResult(int result) {
    if (result == kAnswerOk) MpHeroDelete(this);
    if (heroIndex_ >= 0 && static_cast<std::uint32_t>(heroIndex_) < MpHeroCount()) {
        MpHeroRecord* hero = &MpHeroes()[heroIndex_];
        hero->Party()->Assign(LocalPlayerData());
        hero->WriteFile(HeroesFolder());
    }
    SetMode(this, nullptr, kModeList);
}

// --- The new hero added to the list --------------------------------------------------

namespace {
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
                int number = std::atoi(title.c_str());
                if (!(highest > number)) highest = number;
            }
        } while (more);
    }
    find.Close();
    return highest;
}
} // namespace

void CMpHeroScreen::AddNewHero() {
    interface_->PlaySound("buttons\\messbox\\ok.wav", 0);
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
    std::vector<MpHeroRecord>& heroes = MpHeroes();
    if (heroes.size() == heroes.capacity()) {
        MpHeroRecord blank;
        TheSavedParties().ReserveOne();
        heroes.push_back(blank);
    } else {
        heroes.emplace_back();
    }
    MpHeroRecord* record = &heroes.back();
    heroIndex_ = static_cast<int>(heroes.size()) - 1;

    std::string pattern;
    pattern = str::Format(HeroFilePattern(), MpHeroesFolder().c_str());
    int highest = HighestFileNumber(pattern);
    record->file = str::Format(HeroFileFormat(), highest + 1);
    record->Party()->Assign(LocalPlayerData());
    record->WriteFile(HeroesFolder());
    RefreshHeroScroll();
    DrawHeroList();
    SetMode(this, nullptr, kModeList);
}
namespace {
void MpHeroAddNew(CMpHeroScreen* self) { self->AddNewHero(); }
} // namespace
