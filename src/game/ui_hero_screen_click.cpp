// The multiplayer hero screen's left button (00617050, input slot 2) and keys (00618A10, slot 9).
// See docs/notes/ui.md, section 5.
#include "ui_screens.h"
#include "game_strings.h"
#include "ui_camp.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "originals_world.h"
#include "originals_units.h"
#include "console.h"
#include "item.h"
#include "party.h"
#include "unit.h"
#include "map_object_figures.h"
#include "world.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

// An empty CString object of the game (the new hero's name).

// The hero screen's own methods (thiscall on the window): 00615D30 the mode (RET 4;
// Rebirth-patched), 006186F0 create the hero, 00617A50 the statistics page's next, 006183B0
// continue to the connection screen, 00618060 rename, 00619150 the voice's sound.
void SetMode(CMpHeroScreen* screen, void*, int mode) { HeroScreenSetModeOriginal(screen, nullptr, mode); }
__declspec(naked) void __fastcall CreateHeroAt(CMpHeroScreen*) EI_ORIGINAL(0x006186F0)
__declspec(naked) void __fastcall StatsNext(CMpHeroScreen*) EI_ORIGINAL(0x00617A50)
__declspec(naked) void __fastcall ContinueAt(CMpHeroScreen*) EI_ORIGINAL(0x006183B0)
__declspec(naked) void __fastcall RenameAt(CMpHeroScreen*) EI_ORIGINAL(0x00618060)
__declspec(naked) void __fastcall PlayVoiceAt(CMpHeroScreen*) EI_ORIGINAL(0x00619150)
// 006167C0: the hero list drawn again (plain RET).
// 00618FF0 / 00619090 (rewritten as ClampHeroSelection / ClampVoiceSelection; through their address so the
// wrappers run; plain RET).
__declspec(naked) void __fastcall ClampHeroAt(CMpHeroScreen*) EI_ORIGINAL(0x00618FF0)
__declspec(naked) void __fastcall ClampVoiceAt(CMpHeroScreen*) EI_ORIGINAL(0x00619090)
// The panels: 006232D0 (StatsPanelSetHeroOriginal), 006255A0 the trade panel's settling
// (TradePanelSettleOriginal, RET 8), 006229E0 / 00622A30 / 00622BC0 the equipment panels' weapon,
// armour and spell container (RET 4); 0065C290 an edit window's text (RET 4), 0065C2D0 its
// editing (RET 4).
void EditSetText(void* edit, void*, const void* text) { EditFieldSetTextOriginal(edit, nullptr, text); }
void EditActivate(void* edit, void*, int on) { EditFieldActivateOriginal(edit, nullptr, on); }
// Surfaces: 005FDC00 / 005FE2F0 / 005FDC60 (GetDC, clear, ReleaseDC), 005FD760 (RET 8) Freeze.
struct SurfaceDraw {
    void* Begin() { return SurfaceGetDC(this); }   // 005FDC00
    void Clear() { SurfaceClearOriginal(this); }   // 005FE2F0
    void End() { SurfaceReleaseDC(this); }     // 005FDC60
};
__declspec(naked) void __fastcall SurfaceFreeze(void*, void*, int, int) EI_ORIGINAL(0x005FD760)
// 006002A0 (RET 8): a window's enabled flag (CUiWindow::SetEnabled, through its address).

// Back to the main menu: the text "reserved_04" (00578E20 / 004E0020) to 005F75B0 (thiscall on
// 007C7D50, RET 4), 0057ED00 (thiscall on the session 007AFE20) ends it.
struct TextGlue {
    void* Table();   // 00578E20
};
void* TextGlue::Table() { return GameFlowOriginal(); }
__declspec(naked) void __fastcall EndSession(void*) EI_ORIGINAL(0x0057ED00)

// Windows: 00634650 the message box (RET 0x18), 005F7350 (RET 0xC) opens one on top.
void* __fastcall MessageBoxConstruct(void* a0, void*, CInterface3D* a2, const char* a3, const char* a4, int a5, int a6, int a7) { return MessageBoxConstructOriginal(a0, nullptr, a2, a3, a4, a5, a6, a7); }

// The camp screen as the hero screen builds it inline (the same fields as its constructor
// 005F6240, with the texture cache's header node made by the tree's own allocator): container
// base 00600B20 (RET 4), the model cache's map 005FA420 (RET 4) / 005FA400, the texture cache's
// tree base 00619750 (RET 8), node allocation 00619770 (RET 8) and 005FA610, CStringArray 006FE925.
__declspec(naked) void __fastcall TreeBaseConstruct(void*, void*, const void*, const void*) EI_ORIGINAL(0x00619750)
__declspec(naked) void* __fastcall TreeAllocateNodes(void*, void*, int, const void*) EI_ORIGINAL(0x00619770)


int HeroCount() {
    const RecordTable& heroes = MpHeroes();
    return static_cast<int>(heroes.end - heroes.begin) / kMpHeroSize;
}

// The first entry of one of a hero record's item arrays (data, count), or null when it is empty.
char* FirstOf(CEngineObject** const& data, const std::uint32_t& count) {
    return static_cast<int>(count) > 0 ? reinterpret_cast<char*>(*data) : nullptr;
}

// `flags` is the handler's first argument: the original hands its stack slot to the model cache
// as the allocator and keeps its low byte as the trees' (empty) compare objects.
CCampPages* BuildCamp(CInterface3D* ui, const int* flags) {
    auto* camp = static_cast<CCampPages*>(gamecrt::operator_new(sizeof(CCampPages)));
    if (!camp) return nullptr;
    ConstructContainerAt(camp, nullptr, ui);
    camp->hoveredItem_ = nullptr;
    std::uint8_t compare = *reinterpret_cast<const std::uint8_t*>(flags);
    ItemObjectCache* models = &camp->models_;
    ConstructModelMap(models, nullptr, flags);
    models->count = 0;
    models->compare = compare;
    EmptyModelMap(models);
    models->limit = 50;
    ItemObjectCache* textures = &camp->textures_;
    void* header = nullptr;
    TreeBaseConstruct(textures, nullptr, &models, &header);
    textures->header = static_cast<ItemObjectCache::Node*>(TreeAllocateNodes(textures, nullptr, 1, nullptr));
    textures->count = 0;
    textures->compare = compare;
    EmptyTextureMap(textures);
    textures->limit = 50;
    camp->vtable_ = reinterpret_cast<void**>(0x00740A2C);
    camp->inputVtable_ = reinterpret_cast<void**>(0x007409F8);
    camp->fromHeroScreen_ = true;   // opened from the hero screen
    camp->heroes_.Construct(0x007402AC);
    StringArrayConstruct(&camp->pageTitles_);
    camp->vtable_ = reinterpret_cast<void**>(0x0074091C);
    camp->inputVtable_ = reinterpret_cast<void**>(0x007408E8);
    return camp;
}

// Back to the main menu: the "reserved_04" status text, the session ended, the screen closed.
void BackToMainMenu(CInterface3D* ui) {
    mfc::CString text;
    TextsGetString(reinterpret_cast<TextGlue*>(0x007AFEA4)->Table(), nullptr, game::Text(0x00793A54), &text);   // "reserved_04"
    ShowStatusText(reinterpret_cast<void*>(0x007C7D50), nullptr, text);
    uiorig::PlayUiSound(ui, game::CancelSoundText(), 0);
    EndSession(reinterpret_cast<void*>(0x007AFE20));
    ui->SetPendingValue(0);
}

// The heroes' map transfer (sent after a rename).
int HeroesTransfer() { return *reinterpret_cast<const int*>(0x007C82A0); }
const char* RenameSeparator() { return game::Text(0x00793AA0); }   // " | "
const char* RenameBar() { return game::Text(0x00793AA4); }         // "|"

constexpr int kModeList = 9;
constexpr int kModeNewHero = 10;
constexpr int kModeStats = 11;
// The hero record the screen is making (slot 51).
PartyUnitRecord* HeroRecordOf(CMpHeroScreen* screen) { return reinterpret_cast<PartyUnitRecord*>(screen->Hero()); }

// A multiplayer hero's name: the text 0 of its party's first member record (the original reads
// the members' data pointer at +0x30 and takes the CString there).
mfc::CString& MpHeroName(int index) {
    auto* hero = reinterpret_cast<MpHeroRecord*>(MpHeroes().begin + index * kMpHeroSize);
    return hero->Party()->members_.data->texts[0];
}

} // namespace

void CMpHeroScreen::CloseRenameEdit() {
    SurfaceFreeze(window.surfaces_.data[3], nullptr, 0, 0);
    WindowSetEnabled(renameEdit_, nullptr, 0, 1);
    EditActivate(renameEdit_, nullptr, 0);
}

void CMpHeroScreen::BackToNewHero() {
    uiorig::PlayUiSound(window.interface_, game::CancelSoundText(), 0);
    StatsPanelSetHeroOriginal(statsPanel_, nullptr, 0, const_cast<char*>(game::EmptyText()));
    reinterpret_cast<SurfaceDraw*>(window.surfaces_.data[0])->Begin();
    reinterpret_cast<SurfaceDraw*>(window.surfaces_.data[0])->Clear();
    reinterpret_cast<SurfaceDraw*>(window.surfaces_.data[0])->End();
    TradePanelSettleOriginal(tradePanel_, nullptr, 0, 0);
    SetMode(this, nullptr, kModeNewHero);
    EditSetText(nameEdit_, nullptr, static_cast<const char*>(HeroRecordOf(this)->texts[0]));
}

void CMpHeroScreen::FinishRename() {
    mfc::CString typed(renameEdit_->text_);
    CStringTrimLeftOriginal(&typed);
    CStringTrimRightOriginal(&typed);
    if (typed.GetLength() != 0) typed = mfc::CString(RenameSeparator()) + typed;
    mfc::CString& name = MpHeroName(heroIndex_);
    mfc::CString title(name);
    mfc::CString base;
    int bar = CStringFindTextOriginal(&title, nullptr, RenameBar());
    if (bar >= 0)
        base = title.Left(bar);
    else
        base = title;
    CStringTrimLeftOriginal(&base);
    CStringTrimRightOriginal(&base);
    typed = base + typed;
    name = typed;
    PartySendTransferOriginal(MpHeroes().begin + heroIndex_ * kMpHeroSize, nullptr, HeroesTransfer());
    CloseRenameEdit();
    CallShowHero(heroIndex_);
}

void CMpHeroScreen::OnKeyDown(int key, int repeat, int flags) {
    InputOf(this)->BaseKeyDown(key, repeat, flags);
    if (UiHandled(this)) return;
    CInterface3D* ui = window.interface_;
    bool renaming = renameEdit_->focused_;
    int heroStep = 0;
    int voiceStep = 0;
    switch (key) {
    case VK_RETURN:
        if (renaming)
            FinishRename();
        else if (mode_ == kModeList)
            ContinueAt(this);
        else if (mode_ == kModeStats)
            StatsNext(this);
        else if (mode_ == kModeNewHero)
            CreateHeroAt(this);
        UiHandled(this) = true;
        return;
    case VK_ESCAPE:
        if (renaming) {
            CloseRenameEdit();
            CallDrawHeroList();
        } else if (mode_ == kModeList) {
            BackToMainMenu(ui);
        } else if (mode_ == kModeStats) {
            BackToNewHero();
        } else if (mode_ == kModeNewHero) {
            uiorig::PlayUiSound(ui, game::CancelSoundText(), 0);
            SetMode(this, nullptr, kModeList);
        }
        UiHandled(this) = true;
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
        ClampHeroAt(this);
    } else if (mode_ == kModeNewHero) {
        uiorig::PlayUiSound(ui, game::SelectSoundText(), 0);
        voiceIndex_ += voiceStep;
        ClampVoiceAt(this);
        PlayVoiceAt(this);
    } else {
        return;
    }
    UiHandled(this) = true;
}

void CMpHeroScreen::OnLButtonDown(int flags, int x, int y) {
    if (renameEdit_->focused_) return;
    InputOf(this)->BaseLButtonDown(flags, x, y);
    if (UiHandled(this)) return;
    CInterface3D* ui = window.interface_;
    int region = window.RegionAt(x, y);
    auto rotate = [&](int direction) {
        uiorig::PlayUiSound(ui, game::RotateSoundText(), 0);
        rotating_ = direction;
        UiCapture(ui) = CaptureValue(InputOf(this));
    };

    if (mode_ == 10) {
        // The new hero.
        switch (region) {
        case 0:
            uiorig::PlayUiSound(ui, game::CancelSoundText(), 0);
            SetMode(this, nullptr, 9);
            return;
        case 1:
            CreateHeroAt(this);
            return;
        case 2:
            rotate(1);
            return;
        case 3:
            rotate(2);
            return;
        case 4: {
            // Back to the defaults: the attributes 25, +0x72C 0.7.
            uiorig::PlayUiSound(ui, game::CancelSoundText(), 0);
            VirtualChooseFigure(static_cast<const char*>(*defaults_));
            PartyUnitRecord* hero = HeroRecordOf(this);
            auto* stats = reinterpret_cast<UnitStats*>(hero->stats);
            stats->stat10Base = 25.0f;
            stats->stat10 = 25.0f;
            stats->stat18Base = 25.0f;
            stats->stat18 = 25.0f;
            stats->stat20Base = 25.0f;
            stats->stat20 = 25.0f;
            hero->height = 0.7f;
            EditSetText(nameEdit_, nullptr, game::EmptyText());
            StatsPanelSetHeroOriginal(statsPanel_, nullptr, 0, const_cast<char*>(game::EmptyText()));
            Refresh();
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
        const mfc::CString& file = *reinterpret_cast<const mfc::CString*>(&voices.begin()[index].file);
        HeroRecordOf(this)->texts[6] = file;
        PlayVoiceAt(this);
        CallDrawVoices();
        return;
    }

    if (mode_ == 11) {
        // The new hero's statistics page.
        if (region == 0) {
            BackToNewHero();
        } else if (region == 1) {
            StatsNext(this);
        } else if (region == 4) {
            // Take the equipment back: the hero's first weapon, armour and spell container.
            uiorig::PlayUiSound(ui, game::CancelSoundText(), 0);
            TradePanelSettleOriginal(tradePanel_, nullptr, 0, 0);
            PartyUnitRecord* hero = HeroRecordOf(this);
            weapon_ = FirstOf(hero->weapons, hero->weaponCount);
            ShowWeapon(weaponPanel_, nullptr, weapon_);
            armor_ = FirstOf(hero->quickItems, hero->quickItemCount);
            ShowArmour(weaponPanel_, nullptr, armor_);
            spellContainer_ = FirstOf(hero->spells, hero->spellCount);
            ShowContainer(spellPanel_, nullptr, spellContainer_);
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
        ContinueAt(this);
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
        VirtualChooseFigure(static_cast<const char*>(*defaults_));
        StatsPanelSetHeroOriginal(statsPanel_, nullptr, 0, const_cast<char*>(game::EmptyText()));
        reinterpret_cast<SurfaceDraw*>(window.surfaces_.data[0])->Begin();
        reinterpret_cast<SurfaceDraw*>(window.surfaces_.data[0])->Clear();
        reinterpret_cast<SurfaceDraw*>(window.surfaces_.data[0])->End();
        turn_ = 0.0f;
        SetMode(this, nullptr, 10);
        EditSetText(nameEdit_, nullptr, game::EmptyText());
        return;
    case 6:
        uiorig::PlayUiSound(ui, game::SelectSoundText(), 0);
        RenameAt(this);
        return;
    case 7: {
        // Delete: asks first ("lmp_deletepers").
        uiorig::PlayUiSound(ui, game::SelectSoundText(), 0);
        if (heroIndex_ < 0 || static_cast<unsigned>(heroIndex_) >= static_cast<unsigned>(HeroCount())) return;
        void* memory = gamecrt::operator_new(0x180);
        void* box = memory ? MessageBoxConstruct(memory, nullptr, ui, game::Text(0x00793A30), game::Text(0x00793A40),
                                                 0x10000, 3, 1)
                           : nullptr;
        InterfacePushWindowOriginal(ui, nullptr, box, 1, 1);
        return;
    }
    case 8: {
        // The hero's inventory: the camp screen.
        uiorig::PlayUiSound(ui, game::SelectSoundText(), 0);
        if (heroIndex_ < 0 || static_cast<unsigned>(heroIndex_) >= static_cast<unsigned>(HeroCount())) return;
        CCampPages* camp = BuildCamp(window.interface_, &flags);
        InterfacePushWindowOriginal(window.interface_, nullptr, camp, 0, 1);
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

namespace {
void __fastcall MpHeroLButtonDown(void* input, void*, int flags, int x, int y) {
    WindowFromInput<CMpHeroScreen>(input)->OnLButtonDown(flags, x, y);
}
} // namespace
EI_REPLACE(0x00617050, MpHeroLButtonDown);

namespace {
void __fastcall MpHeroKeyDown(void* input, void*, int key, int repeat, int flags) {
    WindowFromInput<CMpHeroScreen>(input)->OnKeyDown(key, repeat, flags);
}
} // namespace
EI_REPLACE(0x00618A10, MpHeroKeyDown);

// --- 00618060: rename ---------------------------------------------------------------------------

namespace {
// The rename's title on surface 3: 130 x 16 pixels.
constexpr RECT kRenameTitle = {0, 0, 0x82, 0x10};
constexpr std::uint32_t kBright = 0xA7D7E4;
} // namespace

void CMpHeroScreen::Rename() {
    if (heroIndex_ < 0 || static_cast<unsigned>(heroIndex_) >= static_cast<unsigned>(HeroCount())) return;
    WindowSetEnabled(renameEdit_, nullptr, 1, 1);
    EditActivate(renameEdit_, nullptr, 1);
    mfc::CString name(MpHeroName(heroIndex_));
    int bar = CStringFindTextOriginal(&name, nullptr, RenameBar());
    if (bar < 0) {
        EditSetText(renameEdit_, nullptr, game::EmptyText());
    } else {
        mfc::CString typed = console::Ops(name).Mid(bar + 1);
        CStringTrimLeftOriginal(&typed);
        EditSetText(renameEdit_, nullptr, static_cast<const char*>(typed));
        name = name.Left(bar);
        CStringTrimRightOriginal(&name);
    }
    auto* surface = reinterpret_cast<SurfaceDraw*>(window.surfaces_.data[3]);
    SurfaceFreeze(surface, nullptr, 1, 0);
    surface->Begin();
    surface->Clear();
    RECT rect = kRenameTitle;
    SurfaceDrawText(surface, nullptr, name + RenameSeparator(), &rect, 1, kBright, 2, 1);
    surface->End();
}
namespace {
void __fastcall MpHeroRename(CMpHeroScreen* self) { self->Rename(); }
} // namespace
EI_REPLACE(0x00618060, MpHeroRename);

// --- 00619150: the voice's sound ----------------------------------------------------------------

namespace {
// 00619FB0 (RET 4): the sound sets' hash_map<string, UnitSoundBank*>::operator[] (inserts a null
// entry for an unknown name); EAX = the value slot.
__declspec(naked) UnitSoundBank** __fastcall SoundSetAt(void* /*map*/, void*, const stlp::string* /*key*/)
    EI_ORIGINAL(0x00619FB0)
void* UnitPrototypes() { return reinterpret_cast<void*>(0x007AF364); }
void* SoundSets() { return reinterpret_cast<void*>(0x007C8A6C); }
constexpr std::uint32_t kVoiceFlags = 0x2282;

bool IsVoice(const UnitSoundEntry& entry) { return (entry.flags & kVoiceFlags) == kVoiceFlags && entry.name; }
} // namespace

void CMpHeroScreen::PlayVoice() {
    InterfaceStopSoundOriginal(window.interface_, nullptr, voiceSound_);
    voiceSound_ = -1;
    auto* hero = reinterpret_cast<PartyUnitRecord*>(Hero());
    char* prototype;
    {
        stlp::string key(static_cast<const char*>(hero->texts[6]));
        prototype = static_cast<char*>(*PrototypeMapAtOriginal(UnitPrototypes(), nullptr, &key));
    }
    if (!prototype) return;
    UnitSoundBank* set;
    {
        stlp::string key(*reinterpret_cast<const char* const*>(prototype));
        set = *SoundSetAt(SoundSets(), nullptr, &key);
    }
    if (!set) return;
    // The original gathers the voice entries in a temporary vector; here they are counted and
    // found again by their rank (no allocation).
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
        voiceSound_ = window.interface_->PlaySound(nth(index)->sound, 0);
        index = (index + 1) % count;
    }
}
namespace {
void __fastcall MpHeroPlayVoice(CMpHeroScreen* self) { self->PlayVoice(); }
} // namespace
EI_REPLACE(0x00619150, MpHeroPlayVoice);

// --- 006183B0: continue -------------------------------------------------------------------------

namespace {
const char* SoundOk() { return game::Text(0x00793A88); }   // "buttons\messbox\ok.wav"
void* MpHeroesObject() { return reinterpret_cast<void*>(0x007C82A0); }

CMpConnectScreen* BuildConnectScreen(CInterface3D* ui) {
    auto* screen = static_cast<CMpConnectScreen*>(gamecrt::operator_new(sizeof(CMpConnectScreen)));
    if (!screen) return nullptr;
    ConstructContainerAt(screen, nullptr, ui);
    screen->entries_.Start() = nullptr;
    screen->entries_.Finish() = nullptr;
    screen->entries_.EndOfStorage() = nullptr;
    screen->selectedAddress_ = net::NetAddress{};
    new (&screen->hostPassword_) mfc::CString();
    StringArrayConstruct(&screen->allodNames_);
    StringArrayConstruct(&screen->texts_);
    screen->vtable_ = CMpConnectScreen::Vtable();
    screen->inputVtable_ = CMpConnectScreen::InputVtable();
    return screen;
}
} // namespace

void CMpHeroScreen::Continue() {
    CInterface3D* ui = window.interface_;
    ui->PlaySound(SoundOk(), 0);
    if (heroIndex_ >= 0 && static_cast<unsigned>(heroIndex_) < static_cast<unsigned>(HeroCount())) {
        MpHeroesSaveOriginal(MpHeroesObject());
        InterfacePushWindowOriginal(ui, nullptr, BuildConnectScreen(ui), 1, 1);
        return;
    }
    void* memory = gamecrt::operator_new(0x180);
    void* box = memory ? MessageBoxConstruct(memory, nullptr, ui, game::Text(0x00793AA8), game::Text(0x00793AB4), 0x20000, 1, 1)
                       : nullptr;
    InterfacePushWindowOriginal(ui, nullptr, box, 1, 1);
}
namespace {
void __fastcall MpHeroContinue(CMpHeroScreen* self) { self->Continue(); }
} // namespace
EI_REPLACE(0x006183B0, MpHeroContinue);

// --- 00618850 / 006186F0: the new hero's statistics and its creation --------------------------------

namespace {
// 00623880 (rewritten; through its address so the wrapper runs) is StatsPanelDrawStatsOriginal.
const char* SoundCancel() { return game::CancelSoundText(); }
// The points the three attributes must add up to (a float in the exe's data).
float AttributePoints() { return *reinterpret_cast<const float*>(0x00741C98); }
} // namespace

void CMpHeroScreen::RefreshHero() {
    if (previewModel_) {
        auto* hero = reinterpret_cast<PartyUnitRecord*>(Hero());
        auto* stats = reinterpret_cast<UnitStats*>(hero->stats);
        stats->RestoreSlots();
        // The third argument (the height's address) is not read.
        stats->GetFactors(&hero->scaleX, &hero->scaleY, static_cast<int>(reinterpret_cast<std::intptr_t>(&hero->height)));
        reinterpret_cast<CObject3D*>(previewModel_)->SetFigureScale(&hero->scaleX);
        auto* unit = reinterpret_cast<CUnit*>(previewModel_);
        unit->SetFromStats(reinterpret_cast<UnitStats*>(reinterpret_cast<PartyUnitRecord*>(Hero())->stats));
        static_cast<CUnitController*>(unit->controller)->RefreshWounds();
    }
    StatsPanelDrawStatsOriginal(statsPanel_);
}
namespace {
void __fastcall MpHeroRefresh(CMpHeroScreen* self) { self->RefreshHero(); }
} // namespace
EI_REPLACE(0x00618850, MpHeroRefresh);

void CMpHeroScreen::CreateHero() {
    mfc::CString name(reinterpret_cast<CUiEditField*>(nameEdit_)->text_);
    auto* hero = reinterpret_cast<PartyUnitRecord*>(Hero());
    auto* stats = reinterpret_cast<UnitStats*>(hero->stats);
    double sum = static_cast<double>(stats->stat20Base) + stats->stat18Base + stats->stat10Base;
    float points = AttributePoints();
    // FCOMP; TEST AH,0x40: equal or unordered.
    if (sum < points || sum > points || name.GetLength() == 0) {
        window.interface_->PlaySound(SoundCancel(), 0);
        return;
    }
    window.interface_->PlaySound(game::Text(0x00793A88), 0);   // "buttons\messbox\ok.wav"
    weapon_ = static_cast<int>(hero->weaponCount) > 0 ? reinterpret_cast<char*>(hero->weapons[0]) : nullptr;
    ShowWeapon(weaponPanel_, nullptr, weapon_);
    armor_ = static_cast<int>(hero->quickItemCount) > 0 ? reinterpret_cast<char*>(hero->quickItems[0]) : nullptr;
    ShowArmour(weaponPanel_, nullptr, armor_);
    spellContainer_ = static_cast<int>(hero->spellCount) > 0 ? reinterpret_cast<char*>(hero->spells[0]) : nullptr;
    ShowContainer(spellPanel_, nullptr, spellContainer_);
    hero->texts[0] = name;
    StatsPanelSetHeroOriginal(statsPanel_, nullptr, 0, const_cast<char*>(static_cast<const char*>(name)));
    Refresh();
    SetMode(this, nullptr, kModeStats);
}
namespace {
void __fastcall MpHeroCreate(CMpHeroScreen* self) { self->CreateHero(); }
} // namespace
EI_REPLACE(0x006186F0, MpHeroCreate);
// --- 0061A350 / 00617E40 / 00615940: a hero deleted, the message box's answer ------------------------

MpHeroRecord* MpHeroRecord::Assign(const MpHeroRecord* other) {
    PartyCopyFrom(party, nullptr, other->party);   // 00661300 by address: its wrapper runs
    file = other->file;
    return this;
}
EI_REPLACE_METHOD(0x0061A350, MpHeroRecord::Assign);

void MpHeroRoster::Select(const MpHeroRecord* hero) { current.Assign(hero); }
EI_REPLACE_METHOD(0x00662F80, MpHeroRoster::Select);

MpHeroRecord* MpHeroRecord::Construct() {
    PartyRecordsConstructOriginal(party);   // 00661050 by address: its wrapper runs
    new (&file) mfc::CString();
    return this;
}
EI_REPLACE_METHOD(0x00619540, MpHeroRecord::Construct);

void MpHeroRecord::Destruct() {
    file.~CString();
    PartyRecordsDestructOriginal(party);   // 006611D0 by address: its wrapper runs
}
EI_REPLACE_METHOD(0x00619560, MpHeroRecord::Destruct);

MpHeroRecord* MpHeroRecord::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00664AE0, MpHeroRecord::DeletingDestruct);

MpHeroRecord* __fastcall MpHeroRecord::CopyRange(const MpHeroRecord* first, const MpHeroRecord* last,
                                                 MpHeroRecord* dest) {
    for (std::ptrdiff_t n = last - first; n > 0; --n, ++first, ++dest) dest->Assign(first);
    return dest;
}
EI_REPLACE(0x00664410, MpHeroRecord::CopyRange);

namespace {
// The heroes' folder (007C82A0, a CString; the records follow it) and the records as a vector.
const mfc::CString& MpHeroesFolder() { return *reinterpret_cast<const mfc::CString*>(0x007C82A0); }
MpHeroRecord*& MpHeroesBegin() { return *reinterpret_cast<MpHeroRecord**>(0x007C82A4); }
MpHeroRecord*& MpHeroesEnd() { return *reinterpret_cast<MpHeroRecord**>(0x007C82A8); }
// The count as the original computes it: a signed division, compared unsigned.
std::uint32_t MpHeroCount() { return static_cast<std::uint32_t>(static_cast<int>(MpHeroesEnd() - MpHeroesBegin())); }
constexpr std::uint32_t kListRows = 10;
constexpr int kAnswerOk = 0x10001;
// 00617E40 (rewritten as DeleteHero below) through its address so its wrapper runs.
__declspec(naked) void __fastcall DeleteHeroAt(CMpHeroScreen*) EI_ORIGINAL(0x00617E40)
} // namespace

void CMpHeroScreen::DeleteHero() {
    if (heroIndex_ < 0 || static_cast<std::uint32_t>(heroIndex_) >= MpHeroCount()) return;
    mfc::CString path = MpHeroesFolder() + MpHeroesBegin()[heroIndex_].file;
    gameapi::DeleteFileA(path);
    MpHeroRecord* at = MpHeroesBegin() + heroIndex_;
    for (MpHeroRecord* next = at + 1; next < MpHeroesEnd(); ++at, ++next) at->Assign(next);
    (--MpHeroesEnd())->Destruct();   // inlined in the original
    std::uint32_t count = MpHeroCount();
    if (static_cast<std::uint32_t>(heroIndex_) >= count) heroIndex_ = static_cast<int>(count) - 1;
    RefreshHeroScroll();
    CallDrawHeroList();
    CallShowHero(heroIndex_);
}

void CMpHeroScreen::RefreshHeroScroll() {
    auto* bar = static_cast<CUiScrollbar*>(heroScroll_);
    if (MpHeroCount() > kListRows) {
        int position;
        ScrollbarTakePositionOriginal(bar, nullptr, &position);
        ScrollbarSetRangeOriginal(bar, nullptr, static_cast<int>(MpHeroCount() - kListRows));
        bar->CallSetPosition(position);
        ScrollbarTakePositionOriginal(bar, nullptr, &heroTop_);
    } else {
        ScrollbarSetRangeOriginal(bar, nullptr, 0);
        heroTop_ = 0;
    }
}
namespace {
void __fastcall MpHeroDelete(CMpHeroScreen* self) { self->DeleteHero(); }
} // namespace
EI_REPLACE(0x00617E40, MpHeroDelete);

void CMpHeroScreen::OnMessageResult(int result) {
    if (result == kAnswerOk) DeleteHeroAt(this);
    if (heroIndex_ >= 0 && static_cast<std::uint32_t>(heroIndex_) < MpHeroCount()) {
        MpHeroRecord* hero = MpHeroesBegin() + heroIndex_;
        PartyCopyFrom(hero->party, nullptr, LocalPlayerData());
        PartySendTransferOriginal(MpHeroesBegin()[heroIndex_].party, nullptr, HeroesTransfer());
    }
    SetMode(this, nullptr, kModeList);
}
EI_REPLACE_METHOD(0x00615940, CMpHeroScreen::OnMessageResult);

// --- 00617A50: the new hero added to the list ----------------------------------------------------

namespace {
MpHeroRecord*& MpHeroesCapacity() { return *reinterpret_cast<MpHeroRecord**>(0x007C82AC); }
const char* HeroFilePattern() { return game::Text(0x00793A80); }   // "%s*.mp"
const char* HeroFileFormat() { return game::Text(0x00793A78); }    // "%d.mp"

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
            more = CFileFindNext(&find);
            if (!CFileFindIsDotsOriginal(&find)) {
                mfc::CString title;
                CFileFindTitle(&find, nullptr, &title);
                int number = GameAtoi(title);
                if (!(highest > number)) highest = number;
            }
        } while (more);
    }
    CFileFindCloseOriginal(&find);
    return highest;
}
} // namespace

void CMpHeroScreen::AddNewHero() {
    window.interface_->PlaySound(game::Text(0x00793A88), 0);   // "buttons\messbox\ok.wav"
    auto* hero = reinterpret_cast<PartyUnitRecord*>(Hero());
    // Only the chosen equipment stays (each list walked from its end; the count read once).
    for (int i = static_cast<int>(hero->weaponCount) - 1; i >= 0; --i)
        if (static_cast<void*>(hero->weapons[i]) != weapon_)
            RecordRemoveQuickEntryOriginal(hero, nullptr, hero->weapons[i]);
    for (int i = static_cast<int>(hero->quickItemCount) - 1; i >= 0; --i)
        if (static_cast<void*>(hero->quickItems[i]) != armor_)
            RecordRemoveBeltEntryOriginal(hero, nullptr, hero->quickItems[i]);
    for (int i = static_cast<int>(hero->spellCount) - 1; i >= 0; --i)
        if (static_cast<void*>(hero->spells[i]) != spellContainer_)
            RecordRemoveSpellEntryOriginal(hero, nullptr, hero->spells[i]);
    hero->weaponIndex = static_cast<int>(hero->weaponCount) > 0 ? 0 : 0xFFFFFFFFu;
    if (static_cast<int>(hero->spellCount) > 0) {
        auto* container = static_cast<CItem*>(hero->spells[0]);
        auto* spell = reinterpret_cast<char*>(container->spell_);
        if (spell && !HeroCanUseSpell(hero->stats, spell)) {
            PartyAddItemOriginal(LocalPlayerData(), nullptr, container, 0, 1);
            RecordRemoveSpellEntryOriginal(hero, nullptr, container);
        }
    }

    if (MpHeroesEnd() == MpHeroesCapacity()) {
        BlankHeroRecord blank;
        blank.record.Construct();
        PartyEntriesInsertOverflowOriginal(&MpHeroesBegin(), nullptr, MpHeroesEnd(), &blank.record, 1);
        blank.record.Destruct();
    } else {
        if (MpHeroesEnd()) MpHeroesEnd()->Construct();
        ++MpHeroesEnd();
    }
    heroIndex_ = static_cast<int>(MpHeroesEnd() - MpHeroesBegin()) - 1;
    MpHeroRecord* record = MpHeroesBegin() + heroIndex_;

    mfc::CString pattern;
    pattern.Format(HeroFilePattern(), static_cast<const char*>(MpHeroesFolder()));
    int highest = HighestFileNumber(pattern);
    record->file.Format(HeroFileFormat(), highest + 1);
    PartyCopyFrom(record->party, nullptr, LocalPlayerData());
    PartySendTransferOriginal(record->party, nullptr, HeroesTransfer());
    RefreshHeroScroll();
    CallDrawHeroList();
    SetMode(this, nullptr, kModeList);
}
namespace {
void __fastcall MpHeroAddNew(CMpHeroScreen* self) { self->AddNewHero(); }
} // namespace
EI_REPLACE(0x00617A50, MpHeroAddNew);
