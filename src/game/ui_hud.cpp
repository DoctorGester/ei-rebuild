// The battle screen's HUD panels: mouse handlers, per-frame updates and the small drawing
// wrappers. See ui.h ("Battle screen and its HUD panels") and docs/notes/ui.md.
#include "ui.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "unit.h"
#include "world.h"

#include <cstdint>
#include <cstring>

namespace {
constexpr const char* kSoundClick = "buttons\\battle\\click.wav";     // 00794460
constexpr const char* kSoundSling = "buttons\\battle\\sling.wav";     // 00794428
constexpr const char* kSoundWeapon = "buttons\\battle\\weapon.wav";   // 00794444

// The local view of the world client (CWorldClient::LocalView): sends the player's orders.
struct UiLocalView {
    // 0066AE70 (not rewritten). Orders `unit` to use weapon slot `slot`; stores it at +0x270.
    BOOL SetWeapon(CUnit* unit, int slot);
    // 0066AED0 (not rewritten). Orders movement mode `mode`; stores it at +0x268.
    BOOL SetMoveMode(CUnit* unit, int mode);
};
__declspec(naked) BOOL UiLocalView::SetWeapon(CUnit*, int) EI_ORIGINAL(0x0066AE70)
__declspec(naked) BOOL UiLocalView::SetMoveMode(CUnit*, int) EI_ORIGINAL(0x0066AED0)

UiLocalView* LocalView() { return reinterpret_cast<UiLocalView*>(WorldClient()->LocalView()); }

// The mouse cursor (global at 007C7BD8).
struct UiCursor {
    // 005E5200 (not rewritten). Selects cursor shape `shape`; returns the previous one (-1 when
    // the cursor is not set up).
    int SetShape(int shape);
};
__declspec(naked) int UiCursor::SetShape(int) EI_ORIGINAL(0x005E5200)
UiCursor* Cursor() { return reinterpret_cast<UiCursor*>(0x007C7BD8); }

// A network player record as the multiplayer session lists them.
struct UiSessionPlayer {
    std::uint8_t unknown_00[0x2C];
    int unitId_;                     // +0x2C id of the player's unit
    int playerId_;                   // +0x30
};
// A node of the session's player list (STLport list: next, prev, value).
struct UiSessionNode {
    UiSessionNode* next_;
    UiSessionNode* prev_;
    UiSessionPlayer* player_;
};
// The multiplayer session (global pointer at 0079B5D8).
struct UiSession {
    // 0041D260 (not rewritten). Runs a server command (4 and 5 are sent from the portraits).
    void Command(int kind, const char* argument);

    std::uint8_t unknown_0000[0x25D8];
    UiSessionNode* players_;         // +0x25D8 list sentinel
};
__declspec(naked) void UiSession::Command(int, const char*) EI_ORIGINAL(0x0041D260)
UiSession* Session() { return *reinterpret_cast<UiSession**>(0x0079B5D8); }

// The panel window whose input side is `input` (handlers get window + 0x94 as `this`).
template <typename W>
W* FromInput(void* input) {
    return reinterpret_cast<W*>(static_cast<std::uint8_t*>(input) - 0x94);
}

// IDirect3DDevice7::SetRenderState / GetRenderState through the device's vtable.
long SetRenderState(UiD3DDevice* device, int state, std::uint32_t value) {
    using Fn = long(__stdcall*)(UiD3DDevice*, int, std::uint32_t);
    return reinterpret_cast<Fn>(device->vtable_[20])(device, state, value);
}
long GetRenderState(UiD3DDevice* device, int state, std::uint32_t* value) {
    using Fn = long(__stdcall*)(UiD3DDevice*, int, std::uint32_t*);
    return reinterpret_cast<Fn>(device->vtable_[21])(device, state, value);
}

// Scene objects of the panels are tinted through three floats at +0x224 (the original stores
// the constants as dwords).
void SetTint(CEngineObject* object, std::uint32_t bits) {
    auto* p = reinterpret_cast<std::uint8_t*>(object) + 0x224;
    std::memcpy(p, &bits, 4);
    std::memcpy(p + 4, &bits, 4);
    std::memcpy(p + 8, &bits, 4);
}
constexpr std::uint32_t kTintOn = 0x3F800000;    // 1.0f
constexpr std::uint32_t kTintOff = 0x3F000000;   // 0.5f

// Shim from the input-side `this` to a panel member taking (buttons, x, y).
#define UI_INPUT_HANDLER(addr, Class, Method)                                                     \
    static void __fastcall Class##_##Method(void* input, void*, int buttons, int x, int y) {      \
        FromInput<Class>(input)->Method(buttons, x, y);                                           \
    }                                                                                             \
    EI_REPLACE(addr, Class##_##Method)

// Same for the wheel handlers (buttons, delta, x, y), which return BOOL.
#define UI_WHEEL_HANDLER(addr, Class, Method)                                                     \
    static BOOL __fastcall Class##_##Method(void* input, void*, int buttons, int delta, int x,     \
                                            int y) {                                              \
        return FromInput<Class>(input)->Method(buttons, delta, x, y);                             \
    }                                                                                             \
    EI_REPLACE(addr, Class##_##Method)
} // namespace

// --- Forwarders --------------------------------------------------------------------------------

__declspec(naked) int CInterface3D::PlaySound(const char*, int) EI_ORIGINAL(0x005F7E60)
__declspec(naked) BOOL CUiWindow::DrawBase(int) EI_ORIGINAL(0x005FEB90)
__declspec(naked) void UiRenderer::FlushBatchA() EI_ORIGINAL(0x004E7840)
__declspec(naked) void UiRenderer::FlushBatchB() EI_ORIGINAL(0x004E7A40)
__declspec(naked) void CBattleScreen::SelectUnit(CUnit*, int, int) EI_ORIGINAL(0x00638EF0)
__declspec(naked) void CBattleScreen::HoverUnit(CUnit*, int, int) EI_ORIGINAL(0x00639EF0)
__declspec(naked) void CHudWeaponBar::Highlight(int, int) EI_ORIGINAL(0x0063C050)
__declspec(naked) void CHudQuickItems::ItemTip(unsigned, void*) EI_ORIGINAL(0x0063CEE0)
__declspec(naked) void CUiRegion::SetTipText(const char*) EI_ORIGINAL(0x005FC690)
__declspec(naked) void CHudSpellColumn::Click(int) EI_ORIGINAL(0x0063EC90)
__declspec(naked) void CHudSpellColumn::ClickAlternate(int) EI_ORIGINAL(0x0063EF60)
__declspec(naked) void CHudActionColumn::Select(int) EI_ORIGINAL(0x0063FB50)
__declspec(naked) void CHudMovePanel::ToggleFlag() EI_ORIGINAL(0x00649810)
__declspec(naked) void CHudMovePanel::ShowMode(int) EI_ORIGINAL(0x00649910)
__declspec(naked) void CHudTopPanel::ShowPage() EI_ORIGINAL(0x0064A320)

// --- Weapon bar --------------------------------------------------------------------------------

void CHudWeaponBar::OnMouseDown(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region < 0 || region >= count_) return;
    inputHandled_ = true;
    CaptureMouse();
}
UI_INPUT_HANDLER(0x0063C6D0, CHudWeaponBar, OnMouseDown);

void CHudWeaponBar::OnMouseUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region < 0 || region >= count_) return;
    inputHandled_ = true;
    int previous = selected_;
    if (previous == region) return;
    if (previous >= 0 && previous < count_) Highlight(previous, 0);
    Highlight(region, 1);
    selected_ = region;
    interface_->PlaySound(kSoundWeapon, 0);
    // The original does not check for a missing local view here.
    if (CUnit* unit = unit_) LocalView()->SetWeapon(unit, region);
}
UI_INPUT_HANDLER(0x0063C710, CHudWeaponBar, OnMouseUp);

void CHudWeaponBar::OnMouseMove(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region >= 0 && region < count_) inputHandled_ = true;
}
UI_INPUT_HANDLER(0x0063C7D0, CHudWeaponBar, OnMouseMove);

void CHudWeaponBar::Update() {
    CUnit* unit = unit_;
    if (!unit || selected_ == unit->unknown_270) return;
    Highlight(selected_, 0);
    selected_ = unit_->unknown_270;
    Highlight(selected_, 1);
}
EI_REPLACE_METHOD(0x0063C600, CHudWeaponBar::Update);

BOOL CHudWeaponBar::Draw(int pass) {
    std::uint32_t alphaRef, alphaTest;
    UiRenderer* renderer = interface_->renderer_;
    renderer->FlushBatchA();
    renderer->FlushBatchB();
    GetRenderState(interface_->renderer_->device_, kRenderStateAlphaRef, &alphaRef);
    SetRenderState(interface_->renderer_->device_, kRenderStateAlphaRef, 1);
    GetRenderState(interface_->renderer_->device_, kRenderStateAlphaTest, &alphaTest);
    SetRenderState(interface_->renderer_->device_, kRenderStateAlphaTest, 1);
    SetRenderState(interface_->renderer_->device_, kRenderStateSpecular, 1);
    DrawBase(pass);
    renderer = interface_->renderer_;
    renderer->FlushBatchA();
    renderer->FlushBatchB();
    SetRenderState(interface_->renderer_->device_, kRenderStateAlphaRef, alphaRef);
    SetRenderState(interface_->renderer_->device_, kRenderStateAlphaTest, alphaTest);
    SetRenderState(interface_->renderer_->device_, kRenderStateSpecular, 0);
    return 1;
}
static BOOL __fastcall WeaponBarDraw(CHudWeaponBar* self, void*, int pass) { return self->CHudWeaponBar::Draw(pass); }
EI_REPLACE(0x0063C4F0, WeaponBarDraw);

// The three other panels draw the same way, with specular on (slot 20).
namespace {
BOOL DrawWithSpecular(CUiWindow* window, int pass) {
    SetRenderState(window->interface_->renderer_->device_, kRenderStateSpecular, 1);
    window->DrawBase(pass);
    SetRenderState(window->interface_->renderer_->device_, kRenderStateSpecular, 0);
    return 1;
}
} // namespace

BOOL CHudQuickItems::Draw(int pass) { return DrawWithSpecular(this, pass); }
BOOL CHudSpellColumn::Draw(int pass) { return DrawWithSpecular(this, pass); }
BOOL CHudActionColumn::Draw(int pass) { return DrawWithSpecular(this, pass); }
static BOOL __fastcall QuickItemsDraw(CHudQuickItems* self, void*, int pass) { return self->CHudQuickItems::Draw(pass); }
static BOOL __fastcall SpellColumnDraw(CHudSpellColumn* self, void*, int pass) { return self->CHudSpellColumn::Draw(pass); }
static BOOL __fastcall ActionColumnDraw(CHudActionColumn* self, void*, int pass) { return self->CHudActionColumn::Draw(pass); }
EI_REPLACE(0x0063CDC0, QuickItemsDraw);
EI_REPLACE(0x0063F190, SpellColumnDraw);
EI_REPLACE(0x0063FD50, ActionColumnDraw);

// --- Quick items -------------------------------------------------------------------------------

void CHudQuickItems::OnMouseDown(int, int x, int y) {
    if (RegionItem(RegionAt(x, y)) < 0) return;
    inputHandled_ = true;
    CaptureMouse();
}
UI_INPUT_HANDLER(0x0063DAA0, CHudQuickItems, OnMouseDown);

void CHudQuickItems::OnDoubleClick(int, int x, int y) {
    if (RegionItem(RegionAt(x, y)) < 0) return;
    inputHandled_ = true;
    CaptureMouse();
}
UI_INPUT_HANDLER(0x0063DAE0, CHudQuickItems, OnDoubleClick);

void CHudQuickItems::OnMouseMove(int, int x, int y) {
    if (RegionItem(RegionAt(x, y)) >= 0) inputHandled_ = true;
}
UI_INPUT_HANDLER(0x0063E270, CHudQuickItems, OnMouseMove);

void CHudQuickItems::Update() {
    mfc::CString text;
    if (!unit_) return;
    for (unsigned i = 0; i < 4; ++i) {
        if (!unit_->GetQuickItem(i)) continue;
        if (!regions_.data_[i]->HasActiveTip()) continue;
        ItemTip(i, &text);
        regions_.data_[i]->SetTipText(text);
    }
}
EI_REPLACE_METHOD(0x0063D9D0, CHudQuickItems::Update);

// --- Spell column ------------------------------------------------------------------------------

void CHudSpellColumn::OnMouseDown(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region < 0 || region >= count_) return;
    inputHandled_ = true;
    CaptureMouse();
}
UI_INPUT_HANDLER(0x0063F1E0, CHudSpellColumn, OnMouseDown);

void CHudSpellColumn::OnDoubleClick(int buttons, int x, int y) { OnMouseDown(buttons, x, y); }
UI_INPUT_HANDLER(0x0063F220, CHudSpellColumn, OnDoubleClick);

void CHudSpellColumn::OnMouseUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region < 0 || region >= count_) return;
    inputHandled_ = true;
    if (interface_->keyCtrl_)
        ClickAlternate(region);
    else
        Click(region);
}
UI_INPUT_HANDLER(0x0063F260, CHudSpellColumn, OnMouseUp);

void CHudSpellColumn::OnMouseMove(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region >= 0 && region < count_) inputHandled_ = true;
}
UI_INPUT_HANDLER(0x0063F2D0, CHudSpellColumn, OnMouseMove);

// --- Action column -----------------------------------------------------------------------------

void CHudActionColumn::OnMouseDown(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region < 0 || region >= count_) return;
    inputHandled_ = true;
    CaptureMouse();
}
UI_INPUT_HANDLER(0x0063FDA0, CHudActionColumn, OnMouseDown);

void CHudActionColumn::OnMouseUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region < 0 || region >= count_) return;
    inputHandled_ = true;
    Select(region);
}
UI_INPUT_HANDLER(0x0063FDE0, CHudActionColumn, OnMouseUp);

void CHudActionColumn::OnMouseMove(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region >= 0 && region < count_) inputHandled_ = true;
}
UI_INPUT_HANDLER(0x0063FE40, CHudActionColumn, OnMouseMove);

// --- Portraits ---------------------------------------------------------------------------------

namespace {
int PortraitMode(CInterface3D* ui) {
    return static_cast<CBattleScreen*>(ui->GetActiveWindow())->portraitMode_;
}
} // namespace

void CHudPortraits::OnMouseDown(int, int x, int y) {
    int region = RegionAt(x, y);
    int mode = PortraitMode(interface_);
    if (mode == 0) {
        if (region < 0) return;
        inputHandled_ = true;
        if (region < owner_->partyCount_) CaptureMouse();
        return;
    }
    if (region < 0 || region >= count_) return;
    // Picking a player for a server command: the player id of the slot, or of the session
    // player whose unit is the portrait's unit.
    int playerId = -1;
    if (slots_[region].hasPlayer_) {
        playerId = slots_[region].playerId_;
    } else {
        int unitId = owner_->party_[region]->id_;
        UiSessionNode* end = Session()->players_;
        for (UiSessionNode* node = end->next_; node != end; node = node->next_) {
            if (node->player_->unitId_ == unitId) {
                playerId = node->player_->playerId_;
                break;
            }
        }
    }
    char text[128];
    gamecrt::sprintf(text, "%d", playerId);
    if (mode == 1)
        Session()->Command(4, text);
    else if (mode == 2)
        Session()->Command(5, text);
    static_cast<CBattleScreen*>(interface_->GetActiveWindow())->portraitMode_ = 0;
}
UI_INPUT_HANDLER(0x00641CE0, CHudPortraits, OnMouseDown);

void CHudPortraits::OnMouseUp(int, int x, int y) {
    if (PortraitMode(interface_) != 0) return;
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region < 0) return;
    inputHandled_ = true;
    CBattleScreen* owner = owner_;
    if (region < owner->partyCount_) owner->SelectUnit(owner->party_[region], 0, 0);
}
UI_INPUT_HANDLER(0x00641E20, CHudPortraits, OnMouseUp);

void CHudPortraits::OnMouseMove(int, int x, int y) {
    int region = RegionAt(x, y);
    if (PortraitMode(interface_) == 0) {
        if (region < 0) return;
        inputHandled_ = true;
        CBattleScreen* owner = owner_;
        if (region < owner->partyCount_) owner->HoverUnit(owner->party_[region], 0, 0);
        return;
    }
    if (region >= 0 && region < count_) Cursor()->SetShape(0);
}
UI_INPUT_HANDLER(0x00641EA0, CHudPortraits, OnMouseMove);

// --- Map ---------------------------------------------------------------------------------------

void CHudMap::OnMouseDown(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region >= 0) {
        inputHandled_ = true;
        CaptureMouse();
    }
    if (region != 3 && region != 4) return;
    interface_->PlaySound(kSoundClick, 0);
    view_ = region == 3 ? 1 : 2;
    // Scene objects 6 and 7 are the buttons of views 1 and 2.
    for (int i = 0; i < 2; ++i)
        SetTint(sceneObjects_.data_[6 + i], view_ == i + 1 ? kTintOn : kTintOff);
}
UI_INPUT_HANDLER(0x00645060, CHudMap, OnMouseDown);

void CHudMap::OnMouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) inputHandled_ = true;
}
UI_INPUT_HANDLER(0x00645610, CHudMap, OnMouseMove);

// --- Unit panel --------------------------------------------------------------------------------

void CHudUnitPanel::OnMouseDown(int, int x, int y) {
    if (RegionAt(x, y) < 0) return;
    inputHandled_ = true;
    CaptureMouse();
}
UI_INPUT_HANDLER(0x00648330, CHudUnitPanel, OnMouseDown);

void CHudUnitPanel::OnMouseUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region == 0) {
        interface_->PlaySound(kSoundSling, 0);
        sliding_ = 1;
        slideDirection_ = -slideDirection_;
        inputHandled_ = true;
        return;
    }
    if (region > 0 && region <= 4) {
        interface_->PlaySound(kSoundClick, 0);
        bodyPart_ = region - 1;
        // Scene objects 3-6 are the body parts.
        for (int i = 0; i < 4; ++i) SetTint(sceneObjects_.data_[3 + i], bodyPart_ == i ? kTintOn : kTintOff);
    }
    if (region >= 0) inputHandled_ = true;
}
UI_INPUT_HANDLER(0x00648370, CHudUnitPanel, OnMouseUp);

void CHudUnitPanel::OnMouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) inputHandled_ = true;
}
UI_INPUT_HANDLER(0x006484C0, CHudUnitPanel, OnMouseMove);

BOOL CHudUnitPanel::OnWheel(int, int, int x, int y) {
    if (RegionAt(x, y) >= 0) inputHandled_ = true;
    return 1;
}
UI_WHEEL_HANDLER(0x006484F0, CHudUnitPanel, OnWheel);

// --- Clock -------------------------------------------------------------------------------------

void CHudClock::OnMouseDown(int, int x, int y) {
    if (RegionAt(x, y) < 0) return;
    inputHandled_ = true;
    CaptureMouse();
}
UI_INPUT_HANDLER(0x00648D30, CHudClock, OnMouseDown);

void CHudClock::OnMouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) inputHandled_ = true;
}
UI_INPUT_HANDLER(0x00649160, CHudClock, OnMouseMove);

// --- Movement panel ----------------------------------------------------------------------------

void CHudMovePanel::OnMouseDown(int, int x, int y) {
    if (RegionAt(x, y) < 0) return;
    inputHandled_ = true;
    CaptureMouse();
}
UI_INPUT_HANDLER(0x00649630, CHudMovePanel, OnMouseDown);

void CHudMovePanel::OnMouseUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region == -1) return;
    inputHandled_ = true;
    if (region == 0) {
        ToggleFlag();
        return;
    }
    int mode = region - 1;
    if (!WorldClient()->LocalView()) return;
    if (mode >= 0 && mode < 4) {
        interface_->PlaySound(kSoundClick, 0);
        for (int i = 0; i < owner_->partyCount_; ++i) {
            CUnit* unit = owner_->party_[i];
            if (unit->flags_ & 0x10) LocalView()->SetMoveMode(unit, mode);
        }
    }
    ShowMode(mode);
}
UI_INPUT_HANDLER(0x00649670, CHudMovePanel, OnMouseUp);

void CHudMovePanel::OnMouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) inputHandled_ = true;
}
UI_INPUT_HANDLER(0x00649750, CHudMovePanel, OnMouseMove);

void CHudMovePanel::Update() {
    if (!WorldClient()->LocalView()) return;
    // The common value over the selected units: -1 none selected, -2 mixed.
    int mode = -1;
    int flag = -1;
    int count = owner_->partyCount_;
    CUnit** units = owner_->party_;
    for (int i = 0; i < count; ++i) {
        CUnit* unit = units[i];
        if (!(unit->flags_ & 0x10)) continue;
        if (mode == -1)
            mode = unit->moveMode2;
        else if (mode != unit->moveMode2)
            mode = -2;
        if (flag == -1)
            flag = unit->unknown_438;
        else if (flag != unit->unknown_438)
            flag = -2;
    }
    ShowMode(mode);
    flag_ = flag;
    // Scene object 5 shows the flag through its +0xCC float.
    std::uint32_t bits = flag == 0 ? 0x3EAC0000u : flag == 1 ? 0u : 0x3EF60000u;
    std::memcpy(reinterpret_cast<std::uint8_t*>(sceneObjects_.data_[5]) + 0xCC, &bits, 4);
}
EI_REPLACE_METHOD(0x00649550, CHudMovePanel::Update);

// --- Top panel ---------------------------------------------------------------------------------

void CHudTopPanel::OnMouseDown(int, int x, int y) {
    if (RegionAt(x, y) < 0) return;
    inputHandled_ = true;
    CaptureMouse();
}
UI_INPUT_HANDLER(0x0064A1C0, CHudTopPanel, OnMouseDown);

void CHudTopPanel::OnMouseUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    switch (region) {
    case 0:
        interface_->PlaySound(kSoundSling, 0);
        sliding_ = 1;
        slideDirection_ = -slideDirection_;
        inputHandled_ = true;
        return;
    case 1:
    case 2:
        page_ = region - 1;
        ShowPage();
        interface_->PlaySound(kSoundClick, 0);
        inputHandled_ = true;
        return;
    default:
        if (region >= 0) inputHandled_ = true;
        return;
    }
}
UI_INPUT_HANDLER(0x0064A200, CHudTopPanel, OnMouseUp);

void CHudTopPanel::OnMouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) inputHandled_ = true;
}
UI_INPUT_HANDLER(0x0064A2C0, CHudTopPanel, OnMouseMove);

BOOL CHudTopPanel::OnWheel(int, int, int x, int y) {
    if (RegionAt(x, y) >= 0) inputHandled_ = true;
    return 1;
}
UI_WHEEL_HANDLER(0x0064A2F0, CHudTopPanel, OnWheel);
