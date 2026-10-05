// The multiplayer hero screen's left button (00617050, input slot 2). See docs/notes/ui.md
// ("Ninth pass").
#include "ui_screens.h"
#include "game_strings.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_engine.h"
#include "originals_world.h"

#include <cstdint>
#include <cstring>

namespace {

const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
constexpr std::uintptr_t kSoundCancel = 0x007937E4;   // buttons\messbox_cancel.wav
constexpr std::uintptr_t kSoundTurn = 0x00793764;     // the rotate arrows
constexpr std::uintptr_t kSoundSelect = 0x00793A60;   // buttons\save_select.wav
// An empty CString object of the game (the new hero's name).

// The hero screen's own methods (thiscall on the window): 00615D30 the mode (RET 4;
// Rebirth-patched), 006186F0 create the hero, 00617A50 the statistics page's next, 006183B0
// continue to the connection screen, 00618060 rename, 00619150 the voice's sound.
__declspec(naked) void __fastcall SetMode(CMpHeroScreen*, void*, int) EI_ORIGINAL(0x00615D30)
__declspec(naked) void __fastcall CreateHero(CMpHeroScreen*) EI_ORIGINAL(0x006186F0)
__declspec(naked) void __fastcall StatsNext(CMpHeroScreen*) EI_ORIGINAL(0x00617A50)
__declspec(naked) void __fastcall Continue(CMpHeroScreen*) EI_ORIGINAL(0x006183B0)
__declspec(naked) void __fastcall Rename(CMpHeroScreen*) EI_ORIGINAL(0x00618060)
__declspec(naked) void __fastcall PlayVoice(CMpHeroScreen*) EI_ORIGINAL(0x00619150)
// The panels: 006232D0 the statistics' hero (index, name; RET 8), 006255A0 the trade panel's
// (RET 8), 006229E0 / 00622A30 / 00622BC0 the equipment panels' weapon, armour and spell
// container (RET 4); 0065C290 an edit window's text (RET 4), 0065C2D0 its editing (RET 4).
__declspec(naked) void __fastcall StatsSetHero(void*, void*, int, void*) EI_ORIGINAL(0x006232D0)
__declspec(naked) void __fastcall TradeSetHero(void*, void*, int, int) EI_ORIGINAL(0x006255A0)
__declspec(naked) void __fastcall EditSetText(void*, void*, const void*) EI_ORIGINAL(0x0065C290)
__declspec(naked) void __fastcall EditActivate(void*, void*, int) EI_ORIGINAL(0x0065C2D0)
// Surfaces: 005FDC00 / 005FE2F0 / 005FDC60 (GetDC, clear, ReleaseDC), 005FD760 (RET 8) Freeze.
struct SurfaceDraw {
    void* Begin() { return SurfaceGetDC(this); }   // 005FDC00
    void Clear();   // 005FE2F0
    void End() { SurfaceReleaseDC(this); }     // 005FDC60
};
__declspec(naked) void SurfaceDraw::Clear() EI_ORIGINAL(0x005FE2F0)
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
__declspec(naked) void __fastcall PushWindow(CInterface3D*, void*, void*, int, int) EI_ORIGINAL(0x005F7350)

// The camp screen as the hero screen builds it inline (the same fields as its constructor
// 005F6240, with the texture cache's header node made by the tree's own allocator): container
// base 00600B20 (RET 4), the model cache's map 005FA420 (RET 4) / 005FA400, the texture cache's
// tree base 00619750 (RET 8), node allocation 00619770 (RET 8) and 005FA610, CStringArray 006FE925.
__declspec(naked) void __fastcall TreeBaseConstruct(void*, void*, const void*, const void*) EI_ORIGINAL(0x00619750)
__declspec(naked) void* __fastcall TreeAllocateNodes(void*, void*, int, const void*) EI_ORIGINAL(0x00619770)

template <typename Fn>
Fn VirtualAt(void* object, int slot) {
    return reinterpret_cast<Fn>((*static_cast<void***>(object))[slot]);
}

template <typename T>
T& At(void* p, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + offset);
}

int HeroCount() {
    const RecordTable& heroes = MpHeroes();
    return static_cast<int>(heroes.end - heroes.begin) / kMpHeroSize;
}

// The first entry of a hero record's array (data, size), or null when it is empty.
char* FirstOf(char* hero, int dataOffset) {
    return At<int>(hero, dataOffset + 4) > 0 ? *At<char**>(hero, dataOffset) : nullptr;
}

// `flags` is the handler's first argument: the original hands its stack slot to the model cache
// as the allocator and keeps its low byte as the trees' (empty) compare objects.
void* BuildCamp(CInterface3D* ui, const int* flags) {
    auto* camp = static_cast<std::uint8_t*>(gamecrt::operator_new(0x260));
    if (!camp) return nullptr;
    ConstructContainerAt(camp, nullptr, ui);
    At<int>(camp, 0x168) = 0;
    std::uint8_t compare = *reinterpret_cast<const std::uint8_t*>(flags);
    std::uint8_t* models = camp + 0x170;
    ConstructModelMap(models, nullptr, flags);
    At<int>(models, 4) = 0;
    models[8] = compare;
    EmptyModelMap(models);
    At<int>(models, 0xC) = 50;
    std::uint8_t* textures = camp + 0x180;
    void* header = nullptr;
    TreeBaseConstruct(textures, nullptr, &models, &header);
    At<void*>(textures, 0) = TreeAllocateNodes(textures, nullptr, 1, nullptr);
    At<int>(textures, 4) = 0;
    textures[8] = compare;
    EmptyTextureMap(textures);
    At<int>(textures, 0xC) = 50;
    At<std::uint32_t>(camp, 0) = 0x00740A2C;
    At<std::uint32_t>(camp, 0x94) = 0x007409F8;
    camp[0x194] = 1;   // opened from the hero screen
    At<std::uint32_t>(camp, 0x19C) = 0x007402AC;
    At<int>(camp, 0x1A0) = 0;
    At<int>(camp, 0x1AC) = 0;
    At<int>(camp, 0x1A8) = 0;
    At<int>(camp, 0x1A4) = 0;
    StringArrayConstruct(camp + 0x240);
    At<std::uint32_t>(camp, 0) = 0x0074091C;
    At<std::uint32_t>(camp, 0x94) = 0x007408E8;
    return camp;
}

} // namespace

void CMpHeroScreen::OnLButtonDown(int flags, int x, int y) {
    if (renameEdit_[0x179]) return;
    InputOf(this)->BaseLButtonDown(flags, x, y);
    if (UiHandled(this)) return;
    CInterface3D* ui = window.interface_;
    int region = window.RegionAt(x, y);
    using StoreFn = void(__thiscall*)(CMpHeroScreen*, void*);
    auto rotate = [&](int direction) {
        uiorig::PlayUiSound(ui, GameText(kSoundTurn), 0);
        rotating_ = direction;
        UiCapture(ui) = CaptureValue(InputOf(this));
    };

    if (mode_ == 10) {
        // The new hero.
        switch (region) {
        case 0:
            uiorig::PlayUiSound(ui, GameText(kSoundCancel), 0);
            SetMode(this, nullptr, 9);
            return;
        case 1:
            CreateHero(this);
            return;
        case 2:
            rotate(1);
            return;
        case 3:
            rotate(2);
            return;
        case 4: {
            // Back to the defaults: the attributes 25, +0x72C 0.7.
            uiorig::PlayUiSound(ui, GameText(kSoundCancel), 0);
            VirtualAt<StoreFn>(this, 52)(this, *defaults_);
            char* hero = Hero();
            for (int offset = 0x2C; offset <= 0x40; offset += 4) At<float>(hero, offset) = 25.0f;
            At<float>(hero, 0x72C) = 0.7f;
            EditSetText(nameEdit_, nullptr, game::EmptyText());
            StatsSetHero(statsPanel_, nullptr, 0, const_cast<char*>(game::EmptyText()));
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
        auto** voices = reinterpret_cast<std::uint8_t**>(voices_);
        if (static_cast<unsigned>(index) >= static_cast<unsigned>((voices[1] - voices[0]) >> 3)) return;
        uiorig::PlayUiSound(ui, GameText(kSoundSelect), 0);
        voiceIndex_ = index;
        const mfc::CString& file = *reinterpret_cast<const mfc::CString*>(voices[0] + index * 8);
        *reinterpret_cast<mfc::CString*>(Hero() + 0x18) = file;
        PlayVoice(this);
        DrawVoices();
        return;
    }

    if (mode_ == 11) {
        // The new hero's statistics page.
        if (region == 0) {
            uiorig::PlayUiSound(window.interface_, GameText(kSoundCancel), 0);
            StatsSetHero(statsPanel_, nullptr, 0, const_cast<char*>(game::EmptyText()));
            auto* surface = reinterpret_cast<SurfaceDraw*>(window.surfaces_.data_[0]);
            surface->Begin();
            reinterpret_cast<SurfaceDraw*>(window.surfaces_.data_[0])->Clear();
            reinterpret_cast<SurfaceDraw*>(window.surfaces_.data_[0])->End();
            TradeSetHero(tradePanel_, nullptr, 0, 0);
            SetMode(this, nullptr, 10);
            EditSetText(nameEdit_, nullptr, *reinterpret_cast<void**>(Hero()));
        } else if (region == 1) {
            StatsNext(this);
        } else if (region == 4) {
            // Take the equipment back: the hero's first weapon, armour and spell container.
            uiorig::PlayUiSound(ui, GameText(kSoundCancel), 0);
            TradeSetHero(tradePanel_, nullptr, 0, 0);
            char* hero = Hero();
            weapon_ = FirstOf(hero, 0x730);
            ShowWeapon(weaponPanel_, nullptr, weapon_);
            armor_ = FirstOf(hero, 0x740);
            ShowArmour(weaponPanel_, nullptr, armor_);
            spellContainer_ = FirstOf(hero, 0x760);
            ShowContainer(spellPanel_, nullptr, spellContainer_);
        }
        return;
    }

    if (mode_ != 9) return;
    // The hero list. A click while a hero is being renamed ends the renaming first (only reached
    // when the base handler started it: the handler returns at once while renaming).
    if (region >= 0 && renameEdit_[0x179]) {
        SurfaceFreeze(window.surfaces_.data_[3], nullptr, 0, 0);
        WindowSetEnabled(renameEdit_, nullptr, 0, 1);
        EditActivate(renameEdit_, nullptr, 0);
    }
    switch (region) {
    case 0: {
        // Back to the main menu.
        mfc::CString text;
        TextsGetString(reinterpret_cast<TextGlue*>(0x007AFEA4)->Table(), nullptr, GameText(0x00793A54), &text);   // "reserved_04"
        ShowStatusText(reinterpret_cast<void*>(0x007C7D50), nullptr, text);
        uiorig::PlayUiSound(ui, GameText(kSoundCancel), 0);
        EndSession(reinterpret_cast<void*>(0x007AFE20));
        ui->SetPendingValue(0);
        return;
    }
    case 1:
        Continue(this);
        return;
    case 2:
        rotate(1);
        return;
    case 3:
        rotate(2);
        return;
    case 5:
        // A new hero.
        uiorig::PlayUiSound(ui, GameText(kSoundSelect), 0);
        VirtualAt<StoreFn>(this, 52)(this, *defaults_);
        StatsSetHero(statsPanel_, nullptr, 0, const_cast<char*>(game::EmptyText()));
        reinterpret_cast<SurfaceDraw*>(window.surfaces_.data_[0])->Begin();
        reinterpret_cast<SurfaceDraw*>(window.surfaces_.data_[0])->Clear();
        reinterpret_cast<SurfaceDraw*>(window.surfaces_.data_[0])->End();
        turn_ = 0.0f;
        SetMode(this, nullptr, 10);
        EditSetText(nameEdit_, nullptr, game::EmptyText());
        return;
    case 6:
        uiorig::PlayUiSound(ui, GameText(kSoundSelect), 0);
        Rename(this);
        return;
    case 7: {
        // Delete: asks first ("lmp_deletepers").
        uiorig::PlayUiSound(ui, GameText(kSoundSelect), 0);
        if (heroIndex_ < 0 || static_cast<unsigned>(heroIndex_) >= static_cast<unsigned>(HeroCount())) return;
        void* memory = gamecrt::operator_new(0x180);
        void* box = memory ? MessageBoxConstruct(memory, nullptr, ui, GameText(0x00793A30), GameText(0x00793A40),
                                                 0x10000, 3, 1)
                           : nullptr;
        PushWindow(ui, nullptr, box, 1, 1);
        return;
    }
    case 8: {
        // The hero's inventory: the camp screen.
        uiorig::PlayUiSound(ui, GameText(kSoundSelect), 0);
        if (heroIndex_ < 0 || static_cast<unsigned>(heroIndex_) >= static_cast<unsigned>(HeroCount())) return;
        void* camp = BuildCamp(window.interface_, &flags);
        PushWindow(window.interface_, nullptr, camp, 0, 1);
        return;
    }
    default:
        break;
    }
    if (region < 9 || region >= 19) return;
    int index = heroTop_ + region - 9;
    if (index < 0 || static_cast<unsigned>(index) >= static_cast<unsigned>(HeroCount())) return;
    uiorig::PlayUiSound(ui, GameText(kSoundSelect), 0);
    ShowHero(index);
}

namespace {
void __fastcall MpHeroLButtonDown(void* input, void*, int flags, int x, int y) {
    WindowFromInput<CMpHeroScreen>(input)->OnLButtonDown(flags, x, y);
}
} // namespace
EI_REPLACE(0x00617050, MpHeroLButtonDown);
