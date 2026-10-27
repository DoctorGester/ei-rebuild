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

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "console.h"
#include "item.h"
#include "party.h"
#include "unit.h"
#include "map_object_figures.h"
#include "world.h"
#include "world_glue.h"
#include "library_containers.h"
#include "game_data.h"
#include "lib/nan_audit.h"
#include "lib/mfc_slots.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace {

// The hero screen's own methods (on the window): the mode (Rebirth-patched), create the hero, the
// statistics page's next, continue to the connection screen, rename, the voice's sound.
void SetMode(CMpHeroScreen* screen, void*, int mode) { screen->SetModeVanilla(mode); }
void __fastcall MpHeroCreate(CMpHeroScreen*);
void __fastcall MpHeroAddNew(CMpHeroScreen*);
void __fastcall MpHeroContinue(CMpHeroScreen*);
void __fastcall MpHeroRename(CMpHeroScreen*);
void __fastcall MpHeroPlayVoice(CMpHeroScreen*);


int HeroCount() {
    return SavedHeroCount();
}

// The first entry of one of a hero record's item arrays (data, count), or null when it is empty.
CItem* FirstOf(CItem** const& data, const std::uint32_t& count) {
    return static_cast<int>(count) > 0 ? *data : nullptr;
}

// Back to the main menu: the "reserved_04" status text, the session ended, the screen closed.
void BackToMainMenu(CInterface3D* ui) {
    mfc::CString text;
    GameClientTexts()->CTextManager::GetString("reserved_04", &text);
    TheInterface()->ShowStatusText(text);
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
    return hero->Party()->members_.data->texts[0];
}

} // namespace

void CMpHeroScreen::CloseRenameEdit() {
    static_cast<CUiSurface*>(surfaces_.data[3])->Freeze(0, 0);
    renameEdit_->SetEnabled(0, 1);
    renameEdit_->SetFocused(false);
}

void CMpHeroScreen::BackToNewHero() {
    uiorig::PlayUiSound(interface_, game::CancelSoundText(), 0);
    statsPanel_->SetHero(0, const_cast<char*>(game::EmptyText()));
    surfaces_.data[0]->GetDC();
    surfaces_.data[0]->Clear();
    surfaces_.data[0]->ReleaseDC();
    tradePanel_->Settle(0, 0);
    SetMode(this, nullptr, kModeNewHero);
    nameEdit_->SetText(HeroRecord()->texts[0].c_str());
}

void CMpHeroScreen::FinishRename() {
    std::string typed = renameEdit_->text_;
    str::TrimLeft(typed);
    str::TrimRight(typed);
    if (typed.size() != 0) typed = mfc::CString(RenameSeparator()) + typed;
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
            ChooseFigure(defaults_[0]);
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
        const mfc::CString& file = voices.begin()[index].file;
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
            weapon_ = FirstOf(hero->weapons, hero->weaponCount);
            weaponPanel_->ShowWeapon(weapon_);
            armor_ = FirstOf(hero->quickItems, hero->quickItemCount);
            weaponPanel_->ShowArmour(armor_);
            spellContainer_ = FirstOf(hero->spells, hero->spellCount);
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
        ChooseFigure(defaults_[0]);
        statsPanel_->SetHero(0, const_cast<char*>(game::EmptyText()));
        surfaces_.data[0]->GetDC();
        surfaces_.data[0]->Clear();
        surfaces_.data[0]->ReleaseDC();
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

EI_REWRITTEN(0x00617050, CMpHeroScreen::OnLButtonDown);

EI_REWRITTEN(0x00618A10, CMpHeroScreen::OnKeyDown);

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
        std::string typed = console::Ops(name).Mid(bar + 1).GetString();
        str::TrimLeft(typed);
        renameEdit_->SetText(typed.c_str());
        name = str::Left(name, bar);
        str::TrimRight(name);
    }
    auto* surface = surfaces_.data[3];
    static_cast<CUiSurface*>(surface)->Freeze(1, 0);
    surface->GetDC();
    surface->Clear();
    RECT rect = kRenameTitle;
    surface->DrawTextIn((name + RenameSeparator()).c_str(), &rect, 1, kBright, 2, 1);
    surface->ReleaseDC();
}
namespace {
void __fastcall MpHeroRename(CMpHeroScreen* self) { self->Rename(); }
} // namespace
EI_REWRITTEN(0x00618060, MpHeroRename);

// --- The voice's sound ----------------------------------------------------------------

namespace {
constexpr std::uint32_t kVoiceFlags = 0x2282;

bool IsVoice(const UnitSoundEntry& entry) { return (entry.flags & kVoiceFlags) == kVoiceFlags && entry.name; }
} // namespace

void CMpHeroScreen::PlayVoice() {
    interface_->StopSound(voiceSound_);
    voiceSound_ = -1;
    auto* hero = HeroRecord();
    UnitPrototypeRecord* prototype;
    {
        stlp::string key(hero->texts[6].c_str());
        prototype = unitdb::PrototypeTable().byName.Subscript(key);
    }
    if (!prototype) return;
    UnitSoundBank* set;
    {
        stlp::string key(prototype->name.c_str());
        // The sound sets (the units' acks) by name: operator[] inserts a null entry for an unknown name.
        set = reinterpret_cast<UnitSoundBank*>(acks::Database().units.byName.Subscript(key));
    }
    if (!set) return;
    // The voice entries are counted and found again by their rank (no allocation; the original gathers
    // them in a temporary vector).
    int count = 0;
    for (const UnitSoundEntry* e = set->entries.begin(); e != set->entries.end(); ++e)
        if (IsVoice(*e)) ++count;
    if (count == 0) return;
    auto nth = [&](int rank) -> const UnitSoundEntry* {
        for (const UnitSoundEntry* e = set->entries.begin(); e != set->entries.end(); ++e)
            if (IsVoice(*e) && rank-- == 0) return e;
        return nullptr;
    };
    int index = gamecrt::rand() * count / 0x7FFF % count;
    for (int tries = 0; tries < count && voiceSound_ == -1; ++tries) {
        voiceSound_ = interface_->PlaySound(nth(index)->sound, 0);
        index = (index + 1) % count;
    }
}
namespace {
void __fastcall MpHeroPlayVoice(CMpHeroScreen* self) { self->PlayVoice(); }
} // namespace
EI_REWRITTEN(0x00619150, MpHeroPlayVoice);

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
void __fastcall MpHeroContinue(CMpHeroScreen* self) { self->Continue(); }
} // namespace
EI_REWRITTEN(0x006183B0, MpHeroContinue);

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
        stats->GetFactors(&hero->scaleX, &hero->scaleY, static_cast<int>(reinterpret_cast<std::intptr_t>(&hero->height)));
        previewModel_->SetFigureScale(&hero->scaleX);
        CUnit* unit = previewModel_;
        unit->SetFromStats(&HeroRecord()->stats);

        unit->Controller()->RefreshWounds();
    }
    statsPanel_->DrawStats();
}
namespace {
void __fastcall MpHeroRefresh(CMpHeroScreen* self) { self->RefreshHero(); }
} // namespace
EI_REWRITTEN(0x00618850, MpHeroRefresh);

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
    weapon_ = static_cast<int>(hero->weaponCount) > 0 ? hero->weapons[0] : nullptr;
    weaponPanel_->ShowWeapon(weapon_);
    armor_ = static_cast<int>(hero->quickItemCount) > 0 ? hero->quickItems[0] : nullptr;
    weaponPanel_->ShowArmour(armor_);
    spellContainer_ = static_cast<int>(hero->spellCount) > 0 ? hero->spells[0] : nullptr;
    spellPanel_->ShowEntry(spellContainer_);
    hero->texts[0] = name;
    statsPanel_->SetHero(0, const_cast<char*>(name.c_str()));
    RefreshStats();
    SetMode(this, nullptr, kModeStats);
}
namespace {
void __fastcall MpHeroCreate(CMpHeroScreen* self) { self->CreateHero(); }
} // namespace
EI_REWRITTEN(0x006186F0, MpHeroCreate);
// --- A hero deleted, the message box's answer ------------------------------------------

MpHeroRecord* MpHeroRecord::Assign(const MpHeroRecord* other) {
    Party()->Assign(const_cast<MpHeroRecord*>(other)->Party());   // (reads the source non-const)
    file = other->file;
    return this;
}
EI_REWRITTEN(0x0061A350, MpHeroRecord::Assign);

void SavedParties::Select(const MpHeroRecord* hero) { outsideGame_.Assign(hero); }
EI_REWRITTEN(0x00662F80, SavedParties::Select);

MpHeroRecord* MpHeroRecord::Construct() {
    new (Party()) CParty();
    std::construct_at(&file);
    return this;
}
EI_REWRITTEN(0x00619540, MpHeroRecord::Construct);

void MpHeroRecord::Destruct() {
    std::destroy_at(&file);
    Party()->CParty::~CParty();
}
EI_REWRITTEN(0x00619560, MpHeroRecord::Destruct);

MpHeroRecord* MpHeroRecord::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x00664AE0, MpHeroRecord::DeletingDestruct);

MpHeroRecord* __fastcall MpHeroRecord::CopyRange(const MpHeroRecord* first, const MpHeroRecord* last,
                                                 MpHeroRecord* dest) {
    for (std::ptrdiff_t n = last - first; n > 0; --n, ++first, ++dest) dest->Assign(first);
    return dest;
}
EI_REWRITTEN(0x00664410, MpHeroRecord::CopyRange);

namespace {
// The heroes' folder (a CString; the records follow it) and the records as a vector.
const std::string& MpHeroesFolder() { return TheSavedParties().folder_; }
MpHeroRecord*& MpHeroesBegin() { return TheSavedParties().entries_.Start(); }
MpHeroRecord*& MpHeroesEnd() { return TheSavedParties().entries_.Finish(); }
// The count as the original computes it: a signed division, compared unsigned.
std::uint32_t MpHeroCount() { return static_cast<std::uint32_t>(static_cast<int>(MpHeroesEnd() - MpHeroesBegin())); }
constexpr std::uint32_t kListRows = 10;
constexpr int kAnswerOk = 0x10001;
} // namespace

void CMpHeroScreen::DeleteHero() {
    if (heroIndex_ < 0 || static_cast<std::uint32_t>(heroIndex_) >= MpHeroCount()) return;
    std::string path = MpHeroesFolder() + MpHeroesBegin()[heroIndex_].file;
    gameapi::DeleteFileA(path.c_str());
    MpHeroRecord* at = MpHeroesBegin() + heroIndex_;
    for (MpHeroRecord* next = at + 1; next < MpHeroesEnd(); ++at, ++next) at->Assign(next);
    (--MpHeroesEnd())->Destruct();
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
void __fastcall MpHeroDelete(CMpHeroScreen* self) { self->DeleteHero(); }
} // namespace
EI_REWRITTEN(0x00617E40, MpHeroDelete);

void CMpHeroScreen::OnMessageResult(int result) {
    if (result == kAnswerOk) MpHeroDelete(this);
    if (heroIndex_ >= 0 && static_cast<std::uint32_t>(heroIndex_) < MpHeroCount()) {
        MpHeroRecord* hero = MpHeroesBegin() + heroIndex_;
        hero->Party()->Assign(LocalPlayerData());
        hero->WriteFile(HeroesFolder());
    }
    SetMode(this, nullptr, kModeList);
}
EI_REWRITTEN(0x00615940, CMpHeroScreen::OnMessageResult);

// --- The new hero added to the list --------------------------------------------------

namespace {
MpHeroRecord*& MpHeroesCapacity() { return TheSavedParties().entries_.EndOfStorage(); }
const char* HeroFilePattern() { return "%s*.mp"; }
const char* HeroFileFormat() { return "%d.mp"; }

// A record built in place for the vector's insert (constructed and destroyed by hand, as the
// original does with its stack copy).
union BlankHeroRecord {
    BlankHeroRecord() {}
    ~BlankHeroRecord() {}
    MpHeroRecord record;
};

// The highest number among the titles of the files matching `pattern` (atoi; 0 without any).
int HighestFileNumber(const char* pattern) {
    int highest = 0;
    mfc::CFileFind find;
    if (find.FindFile(pattern, 0)) {
        int more;
        do {
            more = find.FindNext();
            if (!find.IsDots()) {
                std::string title = find.GetTitle().GetString();
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
    for (int i = static_cast<int>(hero->weaponCount) - 1; i >= 0; --i)
        if (hero->weapons[i] != weapon_)
            hero->RemoveWeapon(hero->weapons[i]);
    for (int i = static_cast<int>(hero->quickItemCount) - 1; i >= 0; --i)
        if (hero->quickItems[i] != armor_)
            hero->RemoveQuickItem(hero->quickItems[i]);
    for (int i = static_cast<int>(hero->spellCount) - 1; i >= 0; --i)
        if (hero->spells[i] != spellContainer_)
            hero->RemoveSpell(hero->spells[i]);
    hero->weaponIndex = static_cast<int>(hero->weaponCount) > 0 ? 0 : 0xFFFFFFFFu;
    if (static_cast<int>(hero->spellCount) > 0) {
        auto* container = hero->spells[0];
        CSpell* spell = container->spell_;
        if (spell && !HeroCanUseSpell(&hero->stats, spell)) {
            LocalPlayerData()->AddItem(container, 0, 1);
            hero->RemoveSpell(container);
        }
    }

    if (MpHeroesEnd() == MpHeroesCapacity()) {
        BlankHeroRecord blank;
        blank.record.Construct();
        TheSavedParties().entries_.InsertOverflow<libcontainers::HeroRecordElement>(MpHeroesEnd(), blank.record, 1);
        blank.record.Destruct();
    } else {
        if (MpHeroesEnd()) MpHeroesEnd()->Construct();
        ++MpHeroesEnd();
    }
    heroIndex_ = static_cast<int>(MpHeroesEnd() - MpHeroesBegin()) - 1;
    MpHeroRecord* record = MpHeroesBegin() + heroIndex_;

    std::string pattern;
    pattern = str::Format(HeroFilePattern(), MpHeroesFolder().c_str());
    int highest = HighestFileNumber(pattern.c_str());
    record->file = str::Format(HeroFileFormat(), highest + 1);
    record->Party()->Assign(LocalPlayerData());
    record->WriteFile(HeroesFolder());
    RefreshHeroScroll();
    DrawHeroList();
    SetMode(this, nullptr, kModeList);
}
namespace {
void __fastcall MpHeroAddNew(CMpHeroScreen* self) { self->AddNewHero(); }
} // namespace
EI_REWRITTEN(0x00617A50, MpHeroAddNew);
