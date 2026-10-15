// The battle screen's Create (slot 25 of 00740764, 00635250): the HUD panels as children, the
// screen's saved state, the "pers" text table. See docs/notes/ui.md ("Battle screen").
//
// Several panel builders are Rebirth-patched (widescreen); every panel constructor and builder is
// called through its original address.
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

using console::CStringArray;

void __fastcall UiDwordsSetSize(UiDwordArray* a, void*, int n, int g) { DwordArraySetSizeOriginal(a, nullptr, n, g); }

namespace {

// --- Not rewritten -------------------------------------------------------------------------------

// The panel constructors and builders, called on the panel (all thiscall).
struct RawPanel : WindowConstructOriginal {
    // Constructors (owner = the battle screen); each returns the panel.
    CUiWindow* ConstructWeaponBar(CInterface3D* ui, CUiWindow* owner);      // 0063BAB0 (Rebirth)
    CUiWindow* ConstructQuickItems(CInterface3D* ui, CUiWindow* owner);     // 0063C800 (Rebirth)
    CUiWindow* ConstructSpellColumn(CInterface3D* ui, CUiWindow* owner);    // 0063E2A0 (Rebirth)
    CUiWindow* ConstructActionColumn(CInterface3D* ui, CUiWindow* owner);   // 0063F300 (Rebirth)
    CUiWindow* ConstructPortraits(CInterface3D* ui, CUiWindow* owner);      // 0063FE70
    // 0065CC00: the message log (a scrolling text over `rect`, by value).
    CUiWindow* ConstructTextLog(CInterface3D* ui, void* font, RECT rect, float speed);
    // Builders of the inline-constructed panels.
    void BuildUnitPanel(int page, std::uint8_t shown);   // 006456E0 (Rebirth)
    void BuildMap(float zoom, std::uint8_t view);        // 00641F20 (Rebirth)
    void BuildClock(int fast);                           // 00648520 (Rebirth)
    void BuildTopPanel(int page, std::uint8_t shown);    // 00649AF0
    void ShowTopPanelPage();                             // 0064A7A0
    void AddLogText(const char* text);                   // 0065CF30 (the message log)
    // 005EACB0: the battle view's camera and pick input (0x2C bytes).
    void* ConstructViewInput(CInterface3D* ui);
};
__declspec(naked) CUiWindow* RawPanel::ConstructWeaponBar(CInterface3D*, CUiWindow*) EI_ORIGINAL(0x0063BAB0)
__declspec(naked) CUiWindow* RawPanel::ConstructQuickItems(CInterface3D*, CUiWindow*) EI_ORIGINAL(0x0063C800)
__declspec(naked) CUiWindow* RawPanel::ConstructSpellColumn(CInterface3D*, CUiWindow*) EI_ORIGINAL(0x0063E2A0)
__declspec(naked) CUiWindow* RawPanel::ConstructActionColumn(CInterface3D*, CUiWindow*) EI_ORIGINAL(0x0063F300)
__declspec(naked) CUiWindow* RawPanel::ConstructPortraits(CInterface3D*, CUiWindow*) EI_ORIGINAL(0x0063FE70)
CUiWindow* RawPanel::ConstructTextLog(CInterface3D* ui, void* font, RECT rect, float speed) {
    return static_cast<CUiWindow*>(ScrollTextConstructOriginal(this, nullptr, ui, static_cast<const char*>(font), rect.left,
                                                               rect.top, rect.right, rect.bottom, speed));
}
__declspec(naked) void RawPanel::BuildUnitPanel(int, std::uint8_t) EI_ORIGINAL(0x006456E0)
__declspec(naked) void RawPanel::BuildMap(float, std::uint8_t) EI_ORIGINAL(0x00641F20)
__declspec(naked) void RawPanel::BuildClock(int) EI_ORIGINAL(0x00648520)
__declspec(naked) void RawPanel::BuildTopPanel(int, std::uint8_t) EI_ORIGINAL(0x00649AF0)
void RawPanel::ShowTopPanelPage() { TopPanelShowPageOriginal(this); }
void RawPanel::AddLogText(const char* a0) { LogAddTextOriginal((void*)this, nullptr, a0); }
void* RawPanel::ConstructViewInput(CInterface3D* ui) { return ViewInputConstructOriginal(this, nullptr, ui); }

RawPanel* Raw(void* memory) { return static_cast<RawPanel*>(memory); }

// The screens' extra input handlers (CUiContainer +0x158 vector).
struct ContainerInputs {
    void AddInput(void* input);   // 00600CC0
};
void ContainerInputs::AddInput(void* input) { ContainerAddInputOriginal(this, nullptr, input); }

// The game's save stream's version (at 007AFE00, its object GameSaveStream is at 007AFDF8) and a
// game being loaded (at 007AFE1C).
int SaveVersion() { return *reinterpret_cast<const int*>(0x007AFE00); }
bool LoadingGame() { return *reinterpret_cast<const int*>(0x007AFE1C) != 0; }

// The world server (007AFF60) through the game object's helpers.
struct GameObjects {
    void SetServerFlag(std::uint8_t flag);      // 00578D10 (on 007AFEA4)
    void MarkInterfaceChanged();                // 00578950 (on 007AFEA5)
};
void GameObjects::SetServerFlag(std::uint8_t f) { SetServerFlagOriginal(f); }
void GameObjects::MarkInterfaceChanged() { GameMarkInterfaceChangedOriginal(this); }
GameObjects* GameAt(std::uintptr_t address) { return reinterpret_cast<GameObjects*>(address); }

// A panel constructed inline by Create: the window base, the owner (the battle screen, declared
// apart as CBattleScreen), the two vtables.
template <typename Panel>
Panel* NewPlainPanel(CInterface3D* ui, std::size_t size, CBattleScreenSetup* owner, std::uintptr_t vtable,
                     std::uintptr_t inputVtable) {
    void* memory = gamecrt::operator_new(size);
    if (!memory) return nullptr;
    auto* panel = static_cast<Panel*>(Raw(memory)->Construct(ui));
    panel->owner_ = reinterpret_cast<CBattleScreen*>(owner);
    panel->vtable_ = reinterpret_cast<void**>(vtable);
    panel->inputVtable_ = reinterpret_cast<void**>(inputVtable);
    return panel;
}

// A CString member made empty, as MFC's inline constructor does (afxEmptyString's data).
char* EmptyStringData() { return *reinterpret_cast<char* const*>(0x00796E48); }

} // namespace

// --- Create (00635250) ---------------------------------------------------------------------------

void CBattleScreenSetup::Create() {
    float mapZoom = 1.0f;
    portraitMode_ = 0;
    mfc::CString savedText;
    hasHero_ = false;
    savedSelection_ = -1;
    savedFlag_ = 0;

    if (LoadingGame()) {
        // The screen's part of the saved game.
        CMemoryStream* stream = GameSaveStream();
        stream->Seek(0);
        int count;
        stream->Read(&count, 4);
        UiDwordsSetSize(&heroes_, nullptr, count, -1);
        for (int i = 0; i < heroes_.size; ++i) stream->Read(&heroes_.data[i], 4);
        stream->Read(&mapZoom, 4);
        std::uint8_t serverFlag;
        stream->Read(&serverFlag, 1);
        GameAt(0x007AFEA4)->SetServerFlag(serverFlag);
        if (SaveVersion() >= 0x47) {
            int unused;
            stream->Read(&unused, 4);
            alignas(4) std::uint8_t raw[sizeof(mfc::CString)];
            mfc::CString* text = StreamReadString(stream, nullptr, raw);
            savedText = *text;
            text->~CString();
        }
        if (SaveVersion() >= 0x62) {
            stream->Read(&savedFlag_, 1);
            stream->Read(&savedSelection_, 4);
        }
        stream->Resize(0);
    } else {
        // A new game: the local player's first hero.
        if (*reinterpret_cast<const int*>(LocalPlayerData() + 0x34) > 0) {
            std::uint8_t* player = LocalPlayerData();
            int index = heroes_.size;
            std::uint32_t hero;
            std::memcpy(&hero, *reinterpret_cast<std::uint8_t**>(player + 0x30) + 0x1C, 4);
            UiDwordsSetSize(&heroes_, nullptr, index + 1, -1);
            heroes_.data[index] = hero;
            hasHero_ = true;
        }
    }

    // The screen's own input handler (vtable 00741154, 0x0C bytes).
    void** input = static_cast<void**>(gamecrt::operator_new(0x0C));
    if (input) {
        input[0] = reinterpret_cast<void*>(0x00741154);
        input[1] = interface_;
    }
    reinterpret_cast<ContainerInputs*>(this)->AddInput(input);

    CreateTexture(0x100, 0x100, 0x20);
    CreateTexture(0x100, 0x100, 0x20);
    LoadTexture(reinterpret_cast<const char*>(0x007942DC), 3, 0);   // "battle00"

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
    portraits_ = memory ? Raw(memory)->ConstructPortraits(interface_, this) : nullptr;
    AddChild(portraits_);
    portraits_->AddObject(objects_.data[2]);

    // The unit panel (two CStrings and two CStringArrays of its own).
    {
        CHudUnitPanel* panel = nullptr;
        if (void* p = gamecrt::operator_new(0x1C4)) {
            panel = static_cast<CHudUnitPanel*>(Raw(p)->Construct(interface_));
            panel->owner_ = reinterpret_cast<CBattleScreen*>(this);
            panel->strings_[0] = EmptyStringData();
            panel->strings_[1] = EmptyStringData();
            panel->texts_.Construct();
            panel->partLabels_.Construct();
            panel->vtable_ = reinterpret_cast<void**>(0x007426F8);
            panel->inputVtable_ = reinterpret_cast<void**>(0x007426C4);
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
            panel = static_cast<CHudMap*>(Raw(p)->Construct(interface_));
            panel->owner_ = reinterpret_cast<CBattleScreen*>(this);
            for (UiPointArray* points : {&panel->viewPolygon_, &panel->visibleArea_}) {
                points->data = nullptr;
                points->growBy = 0;
                points->capacity = 0;
                points->size = 0;
            }
            panel->vtable_ = reinterpret_cast<void**>(0x00742660);
            panel->inputVtable_ = reinterpret_cast<void**>(0x0074262C);
        }
        map_ = panel;
    }
    AddChild(map_);
    map_->AddObject(objects_.data[2]);
    Raw(map_)->BuildMap(mapZoom, interface_->hudPanelsIn_[2]);

    clock_ = NewPlainPanel<CHudClock>(interface_, 0x15C, this, 0x007425C8, 0x00742594);
    AddChild(clock_);
    clock_->AddObject(objects_.data[2]);
    Raw(clock_)->BuildClock(interface_->fastGame_);

    movePanel_ = NewPlainPanel<CHudMovePanel>(interface_, 0x154, this, 0x00742530, 0x007424FC);
    AddChild(movePanel_);
    movePanel_->AddObject(objects_.data[2]);
    using CreateFn = void(__thiscall*)(CUiWindow*);
    reinterpret_cast<CreateFn>(movePanel_->vtable_[18])(movePanel_);   // slot 18 (+0x48)

    memory = gamecrt::operator_new(0x1C8);
    messageLog_ = memory ? Raw(memory)->ConstructTextLog(interface_, const_cast<char*>(game::EmptyText()),
                                                         RECT{0xBE, 0x19, 0x262, 0x59}, 3.0f)
                         : nullptr;
    AddChild(messageLog_);
    static_cast<CUiScrollText*>(messageLog_)->align_ = 1;

    {
        CHudTopPanel* panel = nullptr;
        if (void* p = gamecrt::operator_new(0x16C)) {
            panel = static_cast<CHudTopPanel*>(Raw(p)->Construct(interface_));
            panel->owner_ = reinterpret_cast<CBattleScreen*>(this);
            panel->pageTexts_[0] = EmptyStringData();
            panel->pageTexts_[1] = EmptyStringData();
            panel->vtable_ = reinterpret_cast<void**>(0x00742498);
            panel->inputVtable_ = reinterpret_cast<void**>(0x00742464);
        }
        topPanel_ = panel;
    }
    AddChild(topPanel_);
    topPanel_->AddObject(objects_.data[2]);
    Raw(topPanel_)->BuildTopPanel(interface_->hudTopPage_, interface_->hudPanelsIn_[1]);
    {
        // The saved message text goes back to the top panel and, when it shows page 0, the log.
        auto* top = static_cast<CHudTopPanel*>(topPanel_);
        mfc::CString& panelText = *reinterpret_cast<mfc::CString*>(&top->pageTexts_[0]);
        const char* text = savedText;
        panelText += text;
        if (top->page_ == 0) {
            // The owner is this screen (a container; CBattleScreen declares it as a window).
            auto* owner = reinterpret_cast<CUiContainer*>(top->owner_);
            Raw(owner->children_.data[9])->AddLogText(text);
        }
    }
    Raw(topPanel_)->ShowTopPanelPage();

    memory = gamecrt::operator_new(0x2C);
    reinterpret_cast<ContainerInputs*>(this)->AddInput(memory ? Raw(memory)->ConstructViewInput(interface_) : nullptr);

    *reinterpret_cast<std::uint32_t*>(0x0079B2B0) &= ~0x40u;
    pendingAction_ = 0;
    pendingSlot_ = -1;
    GameSetWeatherOriginal(GameAt(0x007AFEA6), nullptr, 0, 0.0f);   // clear weather
    GameAt(0x007AFEA5)->MarkInterfaceChanged();
    PartyArraySetSizeOriginal(partyArray_, nullptr, 0, -1);
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
    partyCamera_ = -1;
}

// --- Drawing (slot 20, 00636020) ---------------------------------------------------------------

namespace {

// 00578910 on 007AFEA5 (the menu object), every frame before the battle screen draws.

constexpr DWORD kClearTarget = 2;         // D3DCLEAR_ZBUFFER
constexpr DWORD kClearStencil = 6;        // D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL

} // namespace

BOOL CBattleScreenSetup::Draw(void* target) {
    MenuFrame578910(reinterpret_cast<void*>(0x007AFEA5));
    if (boxSelecting_) {   // the selection box, a dark rectangle with an outline (renderer slot 19)
        static_cast<CSceneRenderer*>(target)->DrawRectangle(selectionBox_.left, selectionBox_.top, selectionBox_.right,
                                                            selectionBox_.bottom, 0x40, 1);
    }
    InputMouseMove(0, *reinterpret_cast<int*>(0x007C7C50), *reinterpret_cast<int*>(0x007C7C54));
    // The depth buffer (and stencil, when the device has one) is cleared before the panels draw.
    UiRenderer* renderer = interface_->renderer_;
    DWORD flags = kClearTarget;
    if ((renderer->caps_ & 0x4000) && renderer->stencil_) flags = kClearStencil;
    auto* lock = reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78);
    gameapi::EnterCriticalSection(lock);
    renderer->result_ = renderer->device_->Clear(0, nullptr, flags, 0, 1.0f, 0);
    gameapi::LeaveCriticalSection(lock);
    render::ClearDepth(1.0f, nullptr, 0);
    // Rebirth's AddNewConsoleCommands (call redirected at 006360DF): "nogui" shows or hides the
    // panels.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AddNewConsoleCommands)) console::ApplyAddonGuiVisibility(this);
    ContainerDrawOriginal(this, nullptr, reinterpret_cast<int>(target));   // 00600EF0: CUiContainer::DrawContainer
    return 1;
}

namespace {
void __fastcall BattleScreenCreate(CBattleScreenSetup* self, void*) { self->Create(); }
} // namespace
EI_REPLACE(0x00635250, BattleScreenCreate);
EI_REPLACE_METHOD(0x00636020, CBattleScreenSetup::Draw);
EI_REBIRTH_IMPLEMENTS(0x00636020, ei::rebirth::Patch::AddNewConsoleCommands);
