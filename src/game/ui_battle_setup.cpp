// The battle screen's Create (slot 25 of 00740764, 00635250): the HUD panels as children, the
// screen's saved state, the "pers" text table. See docs/notes/ui.md ("Battle screen").
//
// Several panel builders are Rebirth-patched (widescreen); every panel constructor and builder is
// called through its original address.
#include "core_streams.h"
#include "ui.h"
#include "ui_input_class.h"
#include "ui_window_class.h"
#include "ui_camera_keys.h"
#include "ui_camp.h"
#include "game_strings.h"

#include "console.h"
#include "item_db_text.h"
#include "party.h"
#include "world.h"
#include "model.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "render/render.h"
#include "originals_world.h"
#include "originals_engine.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <new>
#include "originals_library.h"
#include "originals_ui.h"
#include "world_glue.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/mfc_string.h"
#include "camera.h"

using console::CStringArray;

namespace {
// The top panel's input side's compiled table (C19, ui_input_class.h; game.exe's 00742464 with its tail 0074247C):
// the panel's mouse and wheel handlers, the base's others.
struct TopPanelInputClass final : InputSideClass<TopPanelInputClass, &CHudTopPanel::HudTopPanelDelete> {
    int LeftDown(int b, int x, int y) override { return SideWindow()->OnMouseDown(b, x, y), 0; }     // 2
    int LeftUp(int b, int x, int y) override { return SideWindow()->OnMouseUp(b, x, y), 0; }         // 3
    int MouseMove(int b, int x, int y) override { return SideWindow()->OnMouseMove(b, x, y), 0; }    // 4
    int Wheel(int f, int d, int x, int y) override { return SideWindow()->OnWheel(f, d, x, y); }     // 12
};

// The other panels' compiled tables (C19, ui_window_class.h) in place of game.exe's: the top panel's 00742498 (its
// update, 24), the clock's 007425C8 / 00742594 (its update), the movement panel's 00742530 / 007424FC (its setup, 18,
// and update), the unit panel's 007426F8 / 007426C4 (its draw, 20, and update; the wheel, 12) and the map's 00742660 /
// 0074262C (its update; the wheel). The input sides: the left button and mouse moves.
struct TopPanelClass final : UiWindowClass<TopPanelClass, &CHudTopPanel::HudTopPanelDelete> {
    void Update() override { AsWindow()->Update(); }   // 24: 00649FF0
};
struct ClockClass final : PanelClass<ClockClass, &CUiWindow::UiWindowDelete, CHudClock> {
    void Update() override { AsPanel()->Update(); }   // 24: 006488E0
};
struct ClockInputClass final : PanelInputClass<ClockInputClass, &CUiWindow::UiWindowDelete, CHudClock> {};
struct MovePanelClass final : PanelClass<MovePanelClass, &CUiWindow::UiWindowDelete, CHudMovePanel> {
    void Build() override { AsPanel()->SetupVanilla(); }   // 18: 00649190 (WideScreen behind its flag, W3)
    void Update() override { AsPanel()->Update(); }        // 24: 00649550
};
struct MovePanelInputClass final : PanelInputClass<MovePanelInputClass, &CUiWindow::UiWindowDelete, CHudMovePanel> {};
struct UnitPanelClass final : UiWindowClass<UnitPanelClass, &CHudUnitPanel::HudUnitPanelDelete> {
    BOOL Draw(int pass) override { return AsWindow()->DrawVanilla(pass); }   // 20: 006462A0 (WideScreen, W3)
    void Update() override { AsWindow()->UpdateVanilla(); }                  // 24: 00647FB0 (WideScreen, W3)
};
struct UnitPanelInputClass final : PanelInputClass<UnitPanelInputClass, &CHudUnitPanel::HudUnitPanelDelete, CHudUnitPanel> {
    int Wheel(int f, int d, int x, int y) override { return AsPanel()->OnWheel(f, d, x, y); }   // 12: 006484F0
};
struct MapClass final : UiWindowClass<MapClass, &CHudMap::HudMapDelete> {
    void Update() override { AsWindow()->UpdateVanilla(); }   // 24: 00642B40 (WideScreen, W3)
};
struct MapInputClass final : PanelInputClass<MapInputClass, &CHudMap::HudMapDelete, CHudMap> {
    int Wheel(int f, int d, int x, int y) override { return AsPanel()->OnWheel(f, d, x, y); }   // 12: 00645640
};

// The game's save stream's version and a game being loaded (the save stream holds its data).
int SaveVersion() { return static_cast<int>(GameSaveStream()->version_); }
bool LoadingGame() { return GameSaveStream()->size_ != 0; }

// A panel constructed inline by Create: the window base (005FE700), the owner (the battle screen,
// declared apart as CBattleScreen), the panel's two tables.
template <typename Panel>
Panel* NewPlainPanel(CInterface3D* ui, CBattleScreen* owner) {
    auto* panel = gamecrt::operator_new_as<Panel>();
    if (!panel) return nullptr;
    panel->Construct(ui);
    panel->owner_ = owner;
    panel->vtable_ = Panel::Vtable();
    panel->inputVtable_ = Panel::InputVtable();
    return panel;
}

// A CString member made empty, as MFC's inline constructor does (afxEmptyString's data).

} // namespace

void** CHudTopPanel::Vtable() { return TopPanelClass::Slots(); }
void** CHudClock::Vtable() { return ClockClass::Slots(); }
void** CHudClock::InputVtable() { return ClockInputClass::Slots(); }
void** CHudMovePanel::Vtable() { return MovePanelClass::Slots(); }
void** CHudMovePanel::InputVtable() { return MovePanelInputClass::Slots(); }
void** CHudUnitPanel::Vtable() { return UnitPanelClass::Slots(); }
void** CHudUnitPanel::InputVtable() { return UnitPanelInputClass::Slots(); }
void** CHudMap::Vtable() { return MapClass::Slots(); }
void** CHudMap::InputVtable() { return MapInputClass::Slots(); }

// --- Create (00635250) ---------------------------------------------------------------------------

void CBattleScreen::Create() {
    float mapZoom = 1.0f;
    portraitMode_ = 0;
    mfc::CString savedText;
    hasHero_ = false;
    pendingExit_ = -1;
    exitPending_ = 0;

    if (LoadingGame()) {
        // The screen's part of the saved game.
        CMemoryStream* stream = GameSaveStream();
        stream->Seek(0);
        int count;
        stream->Read(&count, 4);
        heroes_.SetSize(count, -1);
        for (int i = 0; i < heroes_.size; ++i) stream->Read(&heroes_.data[i], 4);
        stream->Read(&mapZoom, 4);
        std::uint8_t serverFlag;
        stream->Read(&serverFlag, 1);
        GameSetServerPaused(serverFlag);
        if (SaveVersion() >= 0x47) {
            int unused;
            stream->Read(&unused, 4);
            alignas(4) std::uint8_t raw[sizeof(mfc::CString)];
            mfc::CString* text = stream->ReadString(raw);
            savedText = *text;
            text->~CString();
        }
        if (SaveVersion() >= 0x62) {
            stream->Read(&exitPending_, 1);
            stream->Read(&pendingExit_, 4);
        }
        stream->Resize(0);
    } else {
        // A new game: the local player's first hero.
        if (LocalPlayerData()->members_.size > 0) {
            CParty* player = LocalPlayerData();
            int index = heroes_.size;
            std::uint32_t hero = player->members_.data[0].unitId;
            heroes_.SetSize(index + 1, -1);
            heroes_.data[index] = hero;
            hasHero_ = true;
        }
    }

    // The screen's own input handler (InertInputVtable(), 0x0C bytes).
    auto* input = gamecrt::operator_new_as<UiInputSide>();
    if (input) {
        input->vtable_ = InertInputVtable();
        input->owner_ = interface_;
    }
    AddInput(input);   // 00600CC0

    CreateTexture(0x100, 0x100, 0x20);
    CreateTexture(0x100, 0x100, 0x20);
    LoadTexture("battle00", 3, 0);

    // Panels built by their own constructors (Rebirth-patched: reproduced behind its flags, rule 9).
    CHudWeaponBar* weaponBar = gamecrt::operator_new_as<CHudWeaponBar>();
    weaponBar_ = weaponBar ? weaponBar->Construct(interface_, this) : nullptr;
    AddChild(weaponBar_);
    weaponBar_->AddObject(objects_.data[0]);

    CHudQuickItems* quickItems = gamecrt::operator_new_as<CHudQuickItems>();
    quickItems_ = quickItems ? quickItems->Construct(interface_, this) : nullptr;
    AddChild(quickItems_);
    quickItems_->AddObject(objects_.data[0]);

    CHudSpellColumn* spellColumn = gamecrt::operator_new_as<CHudSpellColumn>();
    spellColumn_ = spellColumn ? spellColumn->Construct(interface_, this) : nullptr;
    AddChild(spellColumn_);
    spellColumn_->AddObject(objects_.data[1]);

    CHudActionColumn* actionColumn = gamecrt::operator_new_as<CHudActionColumn>();
    actionColumn_ = actionColumn ? actionColumn->Construct(interface_, this) : nullptr;
    AddChild(actionColumn_);
    actionColumn_->AddObject(objects_.data[1]);

    CHudPortraits* portraits = gamecrt::operator_new_as<CHudPortraits>();   // 0x164 bytes
    portraits_ = portraits ? portraits->Construct(interface_, this) : nullptr;
    AddChild(portraits_);
    portraits_->AddObject(objects_.data[2]);

    // The unit panel (two CStrings and two CStringArrays of its own).
    {
        CHudUnitPanel* panel = gamecrt::operator_new_as<CHudUnitPanel>();
        if (panel) {
            panel->Construct(interface_);
            panel->owner_ = this;
            ::new (&panel->strings_[0]) mfc::CString;
            ::new (&panel->strings_[1]) mfc::CString;
            panel->texts_.Construct();
            panel->partLabels_.Construct();
            panel->vtable_ = CHudUnitPanel::Vtable();
            panel->inputVtable_ = CHudUnitPanel::InputVtable();
        }
        unitPanel_ = panel;
    }
    AddChild(unitPanel_);
    unitPanel_->AddObject(objects_.data[2]);
    unitPanel_->AddObject(objects_.data[1]);
    unitPanel_->SetupVanilla(interface_->hudBodyPart_, interface_->hudPanelsIn_[0]);

    // The map (its point arrays start empty).
    {
        CHudMap* panel = gamecrt::operator_new_as<CHudMap>();
        if (panel) {
            panel->Construct(interface_);
            panel->owner_ = this;
            for (UiPointArray* points : {&panel->viewPolygon_, &panel->visibleArea_}) {
                points->data = nullptr;
                points->growBy = 0;
                points->capacity = 0;
                points->size = 0;
            }
            panel->vtable_ = CHudMap::Vtable();
            panel->inputVtable_ = CHudMap::InputVtable();
        }
        map_ = panel;
    }
    AddChild(map_);
    map_->AddObject(objects_.data[2]);
    map_->SetupVanilla(mapZoom, interface_->hudPanelsIn_[2]);

    clock_ = NewPlainPanel<CHudClock>(interface_, this);
    AddChild(clock_);
    clock_->AddObject(objects_.data[2]);
    clock_->SetupVanilla(interface_->fastGame_);

    movePanel_ = NewPlainPanel<CHudMovePanel>(interface_, this);
    AddChild(movePanel_);
    movePanel_->AddObject(objects_.data[2]);
    movePanel_->VirtualBuild();

    CUiScrollText* messageLog = gamecrt::operator_new_as<CUiScrollText>();   // 0x1C8 bytes
    messageLog_ = messageLog ? messageLog->Construct(interface_, game::EmptyText(), RECT{0xBE, 0x19, 0x262, 0x59}, 3.0f)
                             : nullptr;
    AddChild(messageLog_);
    messageLog_->align_ = 1;

    {
        CHudTopPanel* panel = gamecrt::operator_new_as<CHudTopPanel>();
        if (panel) {
            panel->Construct(interface_);
            panel->owner_ = this;
            ::new (&panel->pageTexts_[0]) mfc::CString;
            ::new (&panel->pageTexts_[1]) mfc::CString;
            panel->vtable_ = CHudTopPanel::Vtable();
            panel->inputVtable_ = TopPanelInputClass::Slots();
        }
        topPanel_ = panel;
    }
    AddChild(topPanel_);
    topPanel_->AddObject(objects_.data[2]);
    topPanel_->Build(interface_->hudTopPage_, interface_->hudPanelsIn_[1]);
    {
        // The saved message text goes back to the top panel and, when it shows page 0, the log.
        auto* top = topPanel_;
        mfc::CString& panelText = top->pageTexts_[0];
        const char* text = savedText;
        panelText += text;
        if (top->page_ == 0) {
            // The owner is this screen (a container; CBattleScreen declares it as a window).
            auto* owner = top->owner_;
            owner->MessageLog()->AddText(text);
        }
    }
    topPanel_->ShowItems();

    CCameraInput* camera = gamecrt::operator_new_as<CCameraInput>();
    AddInput(camera ? reinterpret_cast<UiInputSide*>(camera->Construct(interface_)) : nullptr);

    TheCamera()->flags_ &= ~0x40u;
    pendingAction_ = 0;
    pendingSlot_ = -1;
    GameSetWeather(0, 0.0f);   // clear weather
    GameStartAmbience();
    party_.SetSize(0, -1);
    viewPressed_ = false;
    boxSelecting_ = false;
    std::memset(orderKeys_, 0, 6);   // +0x18D dword and +0x191 word
    questSound_ = 0;

    // The "pers <name>" texts: one line per person, by the person's text key.
    LoadPersonTexts(&personTexts_);
    spare_ = 0;
    if (IsMultiplayerClient()) {
        VirtualPlayersChanged(1);
    }
    clientParty_.camera = -1;
}

// --- Drawing (slot 20, 00636020) ---------------------------------------------------------------

namespace {

// 00578910 on 007AFEA5 (the menu object), every frame before the battle screen draws.

constexpr DWORD kClearTarget = 2;         // D3DCLEAR_ZBUFFER
constexpr DWORD kClearStencil = 6;        // D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL

} // namespace

BOOL CBattleScreen::Draw(void* target) {
    GameRender();
    if (boxSelecting_) {   // the selection box, a dark rectangle with an outline (renderer slot 19)
        static_cast<CSceneRenderer*>(target)->DrawRectangle(selectionBox_.left, selectionBox_.top, selectionBox_.right,
                                                            selectionBox_.bottom, 0x40, 1);
    }
    InputMouseMove(0, MouseScreenX(), MouseScreenY());
    // The depth buffer (and stencil, when the device has one) is cleared before the panels draw.
    UiRenderer* renderer = interface_->renderer_;
    DWORD flags = kClearTarget;
    if ((renderer->caps_ & 0x4000) && renderer->stencil_) flags = kClearStencil;
    auto* lock = TheDeviceLock();
    gameapi::EnterCriticalSection(lock);
    renderer->result_ = renderer->device_->Clear(0, nullptr, flags, 0, 1.0f, 0);
    gameapi::LeaveCriticalSection(lock);
    render::ClearDepth(1.0f, nullptr, 0);
    // Rebirth's AddNewConsoleCommands (call redirected at 006360DF): "nogui" shows or hides the
    // panels.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AddNewConsoleCommands)) console::ApplyAddonGuiVisibility(this);
    DrawContainerImpl(reinterpret_cast<int>(target));   // 00600EF0: CUiContainer::DrawContainer
    return 1;
}

EI_REPLACE_METHOD(0x00635250, CBattleScreen::Create);
EI_REPLACE_METHOD(0x00636020, CBattleScreen::Draw);
