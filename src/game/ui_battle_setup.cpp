// The battle screen's Create (slot 25 of 00740764, 00635250): the HUD panels as children, the
// screen's saved state, the "pers" text table. See docs/notes/ui.md ("Battle screen").
//
// Several panel builders are Rebirth-patched (widescreen); every panel constructor and builder is
// called through its original address.
#include "core_streams.h"
#include "ui.h"
#include "ui_camera_keys.h"
#include "ui_camp.h"
#include "game_strings.h"

#include "console.h"
#include "item_db_text.h"
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

using console::CStringArray;

namespace {

// --- Not rewritten -------------------------------------------------------------------------------

// The Rebirth-patched panel constructors and builders, called on the panel (all thiscall; kept as
// forwarders so Rebirth's patches run in a Rebirth launch).
struct RawPanel {
    // Constructors (owner = the battle screen); each returns the panel.
    CHudWeaponBar* ConstructWeaponBar(CInterface3D* ui, CUiWindow* owner);      // 0063BAB0
    CHudQuickItems* ConstructQuickItems(CInterface3D* ui, CUiWindow* owner);     // 0063C800
    CHudSpellColumn* ConstructSpellColumn(CInterface3D* ui, CUiWindow* owner);    // 0063E2A0
    CHudActionColumn* ConstructActionColumn(CInterface3D* ui, CUiWindow* owner);   // 0063F300
    // Builders of the inline-constructed panels.
    void BuildUnitPanel(int page, std::uint8_t shown);   // 006456E0
    void BuildMap(float zoom, std::uint8_t view);        // 00641F20
    void BuildClock(int fast);                           // 00648520
};
__declspec(naked) CHudWeaponBar* RawPanel::ConstructWeaponBar(CInterface3D*, CUiWindow*) EI_ORIGINAL(0x0063BAB0)
__declspec(naked) CHudQuickItems* RawPanel::ConstructQuickItems(CInterface3D*, CUiWindow*) EI_ORIGINAL(0x0063C800)
__declspec(naked) CHudSpellColumn* RawPanel::ConstructSpellColumn(CInterface3D*, CUiWindow*) EI_ORIGINAL(0x0063E2A0)
__declspec(naked) CHudActionColumn* RawPanel::ConstructActionColumn(CInterface3D*, CUiWindow*) EI_ORIGINAL(0x0063F300)
__declspec(naked) void RawPanel::BuildUnitPanel(int, std::uint8_t) EI_ORIGINAL(0x006456E0)
__declspec(naked) void RawPanel::BuildMap(float, std::uint8_t) EI_ORIGINAL(0x00641F20)
__declspec(naked) void RawPanel::BuildClock(int) EI_ORIGINAL(0x00648520)

RawPanel* Raw(void* memory) { return static_cast<RawPanel*>(memory); }
// The window base constructed in fresh memory (005FE700).
CUiWindow* NewWindowBase(void* memory, CInterface3D* ui) { return static_cast<CUiWindow*>(memory)->Construct(ui); }

// The screens' extra input handlers (CUiContainer +0x158 vector).
struct ContainerInputs {
    void AddInput(void* input);   // 00600CC0
};
void ContainerInputs::AddInput(void* input) { reinterpret_cast<CUiContainer*>(this)->AddInput(static_cast<UiInputSide*>(input)); }

// The game's save stream's version (at 007AFE00, its object GameSaveStream is at 007AFDF8) and a
// game being loaded (at 007AFE1C).
int SaveVersion() { return ei::GameData<const int>(0x007AFE00); }
bool LoadingGame() { return ei::GameData<const int>(0x007AFE1C) != 0; }

// The vtables (main, input) of the panels Create builds inline.
constexpr std::uintptr_t kClockVtable = std::uintptr_t(0x007425C8);            // CHudClock
constexpr std::uintptr_t kClockInputVtable = std::uintptr_t(0x00742594);
constexpr std::uintptr_t kMovePanelVtable = std::uintptr_t(0x00742530);        // CHudMovePanel
constexpr std::uintptr_t kMovePanelInputVtable = std::uintptr_t(0x007424FC);

// A panel constructed inline by Create: the window base, the owner (the battle screen, declared
// apart as CBattleScreen), the two vtables.
template <typename Panel>
Panel* NewPlainPanel(CInterface3D* ui, std::size_t size, CBattleScreen* owner, std::uintptr_t vtable,
                     std::uintptr_t inputVtable) {
    void* memory = gamecrt::operator_new(size);
    if (!memory) return nullptr;
    auto* panel = static_cast<Panel*>(NewWindowBase(memory, ui));
    panel->owner_ = owner;
    panel->vtable_ = ei::GamePointer<void*>(vtable);
    panel->inputVtable_ = ei::GamePointer<void*>(inputVtable);
    return panel;
}

// A CString member made empty, as MFC's inline constructor does (afxEmptyString's data).

} // namespace

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
        if (*reinterpret_cast<const int*>(LocalPlayerData() + 0x34) > 0) {
            std::uint8_t* player = LocalPlayerData();
            int index = heroes_.size;
            std::uint32_t hero;
            std::memcpy(&hero, *reinterpret_cast<std::uint8_t**>(player + 0x30) + 0x1C, 4);
            heroes_.SetSize(index + 1, -1);
            heroes_.data[index] = hero;
            hasHero_ = true;
        }
    }

    // The screen's own input handler (vtable 00741154, 0x0C bytes).
    void** input = static_cast<void**>(gamecrt::operator_new(0x0C));
    if (input) {
        input[0] = ei::GamePointer<void>(0x00741154);
        input[1] = interface_;
    }
    reinterpret_cast<ContainerInputs*>(this)->AddInput(input);

    CreateTexture(0x100, 0x100, 0x20);
    CreateTexture(0x100, 0x100, 0x20);
    LoadTexture("battle00", 3, 0);

    // Panels built by their own constructors.
    void* memory = gamecrt::operator_new(0x16C);
    weaponBar_ = memory ? Raw(memory)->ConstructWeaponBar(interface_, this) : nullptr;
    AddChild(weaponBar_);
    weaponBar_->AddObject(objects_.data[0]);

    memory = gamecrt::operator_new(0x1A0);
    quickItems_ = memory ? Raw(memory)->ConstructQuickItems(interface_, this) : nullptr;
    AddChild(quickItems_);
    quickItems_->AddObject(objects_.data[0]);

    memory = gamecrt::operator_new(0x16C);
    spellColumn_ = memory ? Raw(memory)->ConstructSpellColumn(interface_, this) : nullptr;
    AddChild(spellColumn_);
    spellColumn_->AddObject(objects_.data[1]);

    memory = gamecrt::operator_new(0x180);
    actionColumn_ = memory ? Raw(memory)->ConstructActionColumn(interface_, this) : nullptr;
    AddChild(actionColumn_);
    actionColumn_->AddObject(objects_.data[1]);

    memory = gamecrt::operator_new(0x164);
    portraits_ = memory ? static_cast<CHudPortraits*>(memory)->Construct(interface_, this) : nullptr;
    AddChild(portraits_);
    portraits_->AddObject(objects_.data[2]);

    // The unit panel (two CStrings and two CStringArrays of its own).
    {
        CHudUnitPanel* panel = nullptr;
        if (void* p = gamecrt::operator_new(0x1C4)) {
            panel = static_cast<CHudUnitPanel*>(NewWindowBase(p, interface_));
            panel->owner_ = this;
            ::new (&panel->strings_[0]) mfc::CString;
            ::new (&panel->strings_[1]) mfc::CString;
            panel->texts_.Construct();
            panel->partLabels_.Construct();
            panel->vtable_ = ei::GamePointer<void*>(0x007426F8);
            panel->inputVtable_ = ei::GamePointer<void*>(0x007426C4);
        }
        unitPanel_ = panel;
    }
    AddChild(unitPanel_);
    unitPanel_->AddObject(objects_.data[2]);
    unitPanel_->AddObject(objects_.data[1]);
    Raw(unitPanel_)->BuildUnitPanel(interface_->hudBodyPart_, interface_->hudPanelsIn_[0]);

    // The map (its point arrays start empty).
    {
        CHudMap* panel = nullptr;
        if (void* p = gamecrt::operator_new(0x1B4)) {
            panel = static_cast<CHudMap*>(NewWindowBase(p, interface_));
            panel->owner_ = this;
            for (UiPointArray* points : {&panel->viewPolygon_, &panel->visibleArea_}) {
                points->data = nullptr;
                points->growBy = 0;
                points->capacity = 0;
                points->size = 0;
            }
            panel->vtable_ = ei::GamePointer<void*>(0x00742660);
            panel->inputVtable_ = ei::GamePointer<void*>(0x0074262C);
        }
        map_ = panel;
    }
    AddChild(map_);
    map_->AddObject(objects_.data[2]);
    Raw(map_)->BuildMap(mapZoom, interface_->hudPanelsIn_[2]);

    clock_ = NewPlainPanel<CHudClock>(interface_, 0x15C, this, kClockVtable, kClockInputVtable);
    AddChild(clock_);
    clock_->AddObject(objects_.data[2]);
    Raw(clock_)->BuildClock(interface_->fastGame_);

    movePanel_ = NewPlainPanel<CHudMovePanel>(interface_, 0x154, this, kMovePanelVtable, kMovePanelInputVtable);
    AddChild(movePanel_);
    movePanel_->AddObject(objects_.data[2]);
    using CreateFn = void(__thiscall*)(CUiWindow*);
    reinterpret_cast<CreateFn>(movePanel_->vtable_[18])(movePanel_);   // slot 18 (+0x48)

    memory = gamecrt::operator_new(0x1C8);
    messageLog_ = memory ? static_cast<CUiScrollText*>(memory)->Construct(interface_, game::EmptyText(),
                                                                          RECT{0xBE, 0x19, 0x262, 0x59}, 3.0f)
                         : nullptr;
    AddChild(messageLog_);
    static_cast<CUiScrollText*>(messageLog_)->align_ = 1;

    {
        CHudTopPanel* panel = nullptr;
        if (void* p = gamecrt::operator_new(0x16C)) {
            panel = static_cast<CHudTopPanel*>(NewWindowBase(p, interface_));
            panel->owner_ = this;
            ::new (&panel->pageTexts_[0]) mfc::CString;
            ::new (&panel->pageTexts_[1]) mfc::CString;
            panel->vtable_ = ei::GamePointer<void*>(0x00742498);
            panel->inputVtable_ = ei::GamePointer<void*>(0x00742464);
        }
        topPanel_ = panel;
    }
    AddChild(topPanel_);
    topPanel_->AddObject(objects_.data[2]);
    topPanel_->Build(interface_->hudTopPage_, interface_->hudPanelsIn_[1]);
    {
        // The saved message text goes back to the top panel and, when it shows page 0, the log.
        auto* top = static_cast<CHudTopPanel*>(topPanel_);
        mfc::CString& panelText = top->pageTexts_[0];
        const char* text = savedText;
        panelText += text;
        if (top->page_ == 0) {
            // The owner is this screen (a container; CBattleScreen declares it as a window).
            auto* owner = top->owner_;
            static_cast<CUiScrollText*>(owner->children_.data[9])->AddText(text);
        }
    }
    TopPanelShowPageOriginal(topPanel_);

    memory = gamecrt::operator_new(0x2C);
    reinterpret_cast<ContainerInputs*>(this)->AddInput(memory ? static_cast<CCameraInput*>(memory)->Construct(interface_) : nullptr);

    ei::GameData<std::uint32_t>(0x0079B2B0) &= ~0x40u;
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
        using SlotFn = void(__thiscall*)(CUiWindow*, int);
        reinterpret_cast<SlotFn>(vtable_[36])(this, 1);   // +0x90
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
    InputMouseMove(0, ei::GameData<int>(0x007C7C50), ei::GameData<int>(0x007C7C54));
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
