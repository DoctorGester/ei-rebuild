// The battle screen's Create (slot 25 of 00740764, 00635250): the HUD panels as children, the
// screen's saved state, the "pers" text table. See docs/notes/ui.md ("Camp, global map and battle
// screen setup").
//
// Several panel builders are Rebirth-patched (widescreen); every panel constructor and builder is
// called through its original address.
#include "ui_camp.h"
#include "game_strings.h"

#include "console.h"
#include "item_db_text.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/vcall.h"
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

// The game's save stream (global object at 007AFDF8, a CMemoryStream; version at 007AFE00, a
// game being loaded at 007AFE1C).
struct SaveStream {
    void** vtable_;
    void Read(void* buffer, int size) {
        using ReadFn = void(__thiscall*)(SaveStream*, void*, int);
        reinterpret_cast<ReadFn>(vtable_[1])(this, buffer, size);
    }
    void Seek(int position);                    // 004A5490
    void Resize(int size);                      // 004A54D0
};
void SaveStream::Seek(int a0) { StreamSeekOriginal((void*)this, nullptr, (std::uint32_t)a0); }
void SaveStream::Resize(int a2) { StreamResizeOriginal((void*)this, nullptr, (std::uint32_t)a2); }
SaveStream* GameSaveStream() { return reinterpret_cast<SaveStream*>(0x007AFDF8); }
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

// Interface manager fields the panels are built from (set by the options and saved games).
std::uint8_t UiByte(CInterface3D* ui, int offset) { return reinterpret_cast<const std::uint8_t*>(ui)[offset]; }
int UiInt(CInterface3D* ui, int offset) {
    int v;
    std::memcpy(&v, reinterpret_cast<const std::uint8_t*>(ui) + offset, 4);
    return v;
}

// A panel constructed inline by Create: the window base, the owner at +0x148, the two vtables.
CUiWindow* NewPlainPanel(CInterface3D* ui, std::size_t size, CUiWindow* owner, std::uintptr_t vtable,
                         std::uintptr_t inputVtable) {
    void* memory = gamecrt::operator_new(size);
    if (!memory) return nullptr;
    CUiWindow* panel = Raw(memory)->Construct(ui);
    std::uint8_t* p = reinterpret_cast<std::uint8_t*>(panel);
    std::memcpy(p + 0x148, &owner, 4);
    panel->vtable_ = reinterpret_cast<void**>(vtable);
    panel->inputVtable_ = reinterpret_cast<void**>(inputVtable);
    return panel;
}

void SetRawPointer(CUiWindow* window, int offset, const void* value) {
    std::memcpy(reinterpret_cast<std::uint8_t*>(window) + offset, &value, 4);
}

void SetEmptyString(CUiWindow* window, int offset) {
    SetRawPointer(window, offset, *reinterpret_cast<char* const*>(0x00796E48));
}

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
        SaveStream* stream = GameSaveStream();
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
            mfc::CString* text = StreamReadString(reinterpret_cast<CStream*>(stream), nullptr, raw);
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
        CUiWindow* panel = nullptr;
        if (void* p = gamecrt::operator_new(0x1C4)) {
            panel = Raw(p)->Construct(interface_);
            SetRawPointer(panel, 0x148, this);
            SetEmptyString(panel, 0x164);
            SetEmptyString(panel, 0x168);
            new (reinterpret_cast<std::uint8_t*>(panel) + 0x19C) CStringArray();
            new (reinterpret_cast<std::uint8_t*>(panel) + 0x1B0) CStringArray();
            panel->vtable_ = reinterpret_cast<void**>(0x007426F8);
            panel->inputVtable_ = reinterpret_cast<void**>(0x007426C4);
        }
        unitPanel_ = panel;
    }
    AddChild(unitPanel_);
    unitPanel_->AddObject(objects_.data[2]);
    unitPanel_->AddObject(objects_.data[1]);
    Raw(unitPanel_)->BuildUnitPanel(UiInt(interface_, 0x8C), UiByte(interface_, 0x89));

    // The map (its point arrays start empty).
    {
        CUiWindow* panel = nullptr;
        if (void* p = gamecrt::operator_new(0x1B4)) {
            panel = Raw(p)->Construct(interface_);
            SetRawPointer(panel, 0x148, this);
            for (int offset : {0x178, 0x184, 0x180, 0x17C, 0x188, 0x194, 0x190, 0x18C})
                SetRawPointer(panel, offset, nullptr);
            panel->vtable_ = reinterpret_cast<void**>(0x00742660);
            panel->inputVtable_ = reinterpret_cast<void**>(0x0074262C);
        }
        map_ = panel;
    }
    AddChild(map_);
    map_->AddObject(objects_.data[2]);
    Raw(map_)->BuildMap(mapZoom, UiByte(interface_, 0x8B));

    clock_ = NewPlainPanel(interface_, 0x15C, this, 0x007425C8, 0x00742594);
    AddChild(clock_);
    clock_->AddObject(objects_.data[2]);
    Raw(clock_)->BuildClock(UiInt(interface_, 0x94));

    movePanel_ = NewPlainPanel(interface_, 0x154, this, 0x00742530, 0x007424FC);
    AddChild(movePanel_);
    movePanel_->AddObject(objects_.data[2]);
    using CreateFn = void(__thiscall*)(CUiWindow*);
    reinterpret_cast<CreateFn>(movePanel_->vtable_[18])(movePanel_);   // slot 18 (+0x48)

    memory = gamecrt::operator_new(0x1C8);
    messageLog_ = memory ? Raw(memory)->ConstructTextLog(interface_, const_cast<char*>(game::EmptyText()),
                                                         RECT{0xBE, 0x19, 0x262, 0x59}, 3.0f)
                         : nullptr;
    AddChild(messageLog_);
    SetRawPointer(messageLog_, 0x1C4, reinterpret_cast<void*>(1));

    {
        CUiWindow* panel = nullptr;
        if (void* p = gamecrt::operator_new(0x16C)) {
            panel = Raw(p)->Construct(interface_);
            SetRawPointer(panel, 0x148, this);
            SetEmptyString(panel, 0x164);
            SetEmptyString(panel, 0x168);
            panel->vtable_ = reinterpret_cast<void**>(0x00742498);
            panel->inputVtable_ = reinterpret_cast<void**>(0x00742464);
        }
        topPanel_ = panel;
    }
    AddChild(topPanel_);
    topPanel_->AddObject(objects_.data[2]);
    Raw(topPanel_)->BuildTopPanel(UiInt(interface_, 0x90), UiByte(interface_, 0x8A));
    {
        // The saved message text goes back to the top panel and, when it shows page 0, the log.
        mfc::CString& panelText = *reinterpret_cast<mfc::CString*>(reinterpret_cast<std::uint8_t*>(topPanel_) + 0x164);
        const char* text = savedText;
        panelText += text;
        if (*reinterpret_cast<const int*>(reinterpret_cast<std::uint8_t*>(topPanel_) + 0x160) == 0) {
            CUiContainer* owner = *reinterpret_cast<CUiContainer**>(reinterpret_cast<std::uint8_t*>(topPanel_) + 0x148);
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
    unknown_1C0 = false;
    unknown_1C1 = false;
    std::memset(orderKeys_, 0, 6);   // +0x18D dword and +0x191 word
    unknown_1FC = 0;

    // The "pers <name>" texts: one line per person, by the person's text key.
    personTexts_.Load();
    unknown_164 = 0;
    if (IsMultiplayerClient()) {
        using SlotFn = void(__thiscall*)(CUiWindow*, int);
        reinterpret_cast<SlotFn>(vtable_[36])(this, 1);   // +0x90
    }
    unknown_210 = -1;
}

// --- Drawing (slot 20, 00636020) ---------------------------------------------------------------

namespace {

// 00578910 on 007AFEA5 (the menu object), every frame before the battle screen draws.

constexpr int kTargetSlotViewport = 19;   // the draw target (+0x4C): (x, y, w, h, 0x40, 1)
constexpr int kInputSlotMouseMove = 4;    // the input side (+0x10): (buttons, x, y)
constexpr DWORD kClearTarget = 2;         // D3DCLEAR_ZBUFFER
constexpr DWORD kClearStencil = 6;        // D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL
using ClearFn = HRESULT(__stdcall*)(void* device, DWORD count, void* rects, DWORD flags, DWORD colour, float z,
                                    DWORD stencil);

} // namespace

BOOL CBattleScreenSetup::Draw(void* target) {
    MenuFrame578910(reinterpret_cast<void*>(0x007AFEA5));
    auto* self = reinterpret_cast<std::uint8_t*>(this);
    if (unknown_1C1) {
        ei::VCall<void>(target, kTargetSlotViewport, *reinterpret_cast<int*>(self + 0x1CC), *reinterpret_cast<int*>(self + 0x1D0),
                        *reinterpret_cast<int*>(self + 0x1D4), *reinterpret_cast<int*>(self + 0x1D8), 0x40, 1);
    }
    ei::VCall<void>(InputSide(), kInputSlotMouseMove, 0, *reinterpret_cast<int*>(0x007C7C50), *reinterpret_cast<int*>(0x007C7C54));
    // The depth buffer (and stencil, when the device has one) is cleared before the panels draw.
    auto* renderer = *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(interface_) + 8);
    DWORD flags = kClearTarget;
    if ((*reinterpret_cast<std::uint32_t*>(renderer + 0x90) & 0x4000) && *reinterpret_cast<int*>(renderer + 0x9C))
        flags = kClearStencil;
    auto* lock = reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78);
    gameapi::EnterCriticalSection(lock);
    void* device = *reinterpret_cast<void**>(renderer + 0x54);
    *reinterpret_cast<HRESULT*>(renderer + 8) =
        reinterpret_cast<ClearFn>((*static_cast<void***>(device))[10])(device, 0, nullptr, flags, 0, 1.0f, 0);
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
