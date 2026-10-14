// The battle screen's HUD panels: mouse handlers, per-frame updates and the small drawing
// wrappers. See ui.h ("Battle screen and its HUD panels") and docs/notes/ui.md.
#include "ui.h"

#include "random.h"

#include "camera.h"
#include "console.h"
#include "game_strings.h"
#include "map_object.h"
#include "net_classes.h"
#include "net_messages.h"
#include "player_client.h"
#include "ui_briefing.h"
#include "ui_cursor.h"
#include "ei/hook.h"
#include "render/render_state.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "unit.h"
#include "world.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_gameplay.h"
#include "originals_world.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "originals_ui.h"
#include "originals_render.h"

namespace {
constexpr const char* kSoundClick = "buttons\\battle\\click.wav";     // 00794460
constexpr const char* kSoundSling = "buttons\\battle\\sling.wav";     // 00794428
constexpr const char* kSoundWeapon = "buttons\\battle\\weapon.wav";   // 00794444

// The local view of the world client (CWorldClient::LocalView, a CPlayerClient): sends the
// player's orders. 0066AE70 / 0066AED0 (CPlayerClient::SetWeapon / SetMoveMode) are called
// through their addresses, as before the cleanup, so their verify wrappers see the calls.
__declspec(naked) BOOL __fastcall ViewSetWeapon(CPlayerClient*, void*, CUnit*, int) EI_ORIGINAL(0x0066AE70)
__declspec(naked) BOOL __fastcall ViewSetMoveMode(CPlayerClient*, void*, CUnit*, int) EI_ORIGINAL(0x0066AED0)

CPlayerClient* LocalView() { return reinterpret_cast<CPlayerClient*>(WorldClient()->LocalView()); }

// The multiplayer session's player records (STLport list of PlayerMessage pointers).
using SessionPlayerList = stlp::list<net::PlayerMessage*>;

// The panel window whose input side is `input` (handlers get window + 0x94 as `this`).
template <typename W>
W* FromInput(void* input) {
    return reinterpret_cast<W*>(static_cast<std::uint8_t*>(input) - offsetof(CUiWindow, inputVtable_));
}

// IDirect3DDevice7::SetRenderState / GetRenderState through the device's vtable.
long SetRenderState(UiD3DDevice* device, int state, std::uint32_t value) {
    using Fn = long(__stdcall*)(UiD3DDevice*, int, std::uint32_t);
    render::state::Render(static_cast<std::uint32_t>(state), value);
    return reinterpret_cast<Fn>(device->vtable_[20])(device, state, value);
}
long GetRenderState(UiD3DDevice* device, int state, std::uint32_t* value) {
    using Fn = long(__stdcall*)(UiD3DDevice*, int, std::uint32_t*);
    return reinterpret_cast<Fn>(device->vtable_[21])(device, state, value);
}

// Scene objects of the panels (sprites, CUiMesh) are tinted through their three tint_ floats (the
// original stores the constants as dwords).
void SetTint(CEngineObject* object, std::uint32_t bits) {
    float* tint = reinterpret_cast<CUiMesh*>(object)->tint_;
    std::memcpy(&tint[0], &bits, 4);
    std::memcpy(&tint[1], &bits, 4);
    std::memcpy(&tint[2], &bits, 4);
}
constexpr std::uint32_t kTintOn = 0x3F800000;    // 1.0f
constexpr std::uint32_t kTintOff = 0x3F000000;   // 0.5f
constexpr std::uint32_t kTintDim = 0x3F333333;   // 0.7f: the movement panel's other modes

constexpr const char* kSoundClock = "buttons\\battle\\clock.wav";     // 0079447C
// The world's update period in milliseconds: the game's speed.
int& UpdatePeriod() { return *reinterpret_cast<int*>(std::uintptr_t(0x0078FF34)); }
constexpr int kNormalPeriod = 55;
constexpr int kFastPeriod = 27;

// The constants the movement panel's marker is placed with, read where the original reads them.
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const float*>(address); }
float Pi() { return GameFloat(0x007423F8); }
float Sixteenth() { return GameFloat(0x0073D040); }      // 0.0625
float MarkerRadius() { return GameFloat(0x00742B10); }   // 73
float MarkerCentreY() { return GameFloat(0x00742B0C); }  // 600
constexpr float kMarkerDepth = 8.0f;

// The map's centre in the layout, and the camera flag a click on the map clears.
constexpr int kMapCentreX = 700;
constexpr int kMapCentreY = 80;
constexpr std::uint32_t kCameraFollows = 8;

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

void UiRenderer::FlushBatchA() { DeviceFlushBatchAOriginal((void*)this); }
void UiRenderer::FlushBatchB() { DeviceFlushBatchBOriginal((void*)this); }
__declspec(naked) void CHudWeaponBar::Highlight(int, int) EI_ORIGINAL(0x0063C050)
__declspec(naked) void CHudQuickItems::ItemTipAt(unsigned, void*) EI_ORIGINAL(0x0063CEE0)
__declspec(naked) void CHudSpellColumn::ClickAlternateAt(int) EI_ORIGINAL(0x0063EF60)
__declspec(naked) void CHudMovePanel::CallToggleFlag() EI_ORIGINAL(0x00649810)
__declspec(naked) void CHudMovePanel::ShowMode(int) EI_ORIGINAL(0x00649910)
__declspec(naked) void CHudClock::CallOpenObjectives() EI_ORIGINAL(0x00648F10)
__declspec(naked) void CHudTopPanel::CallShowPage() EI_ORIGINAL(0x0064A320)

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
    if (CUnit* unit = unit_) ViewSetWeapon(LocalView(), nullptr, unit, region);
}
UI_INPUT_HANDLER(0x0063C710, CHudWeaponBar, OnMouseUp);

void CHudWeaponBar::OnMouseMove(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region >= 0 && region < count_) inputHandled_ = true;
}
UI_INPUT_HANDLER(0x0063C7D0, CHudWeaponBar, OnMouseMove);

void CHudWeaponBar::Update() {
    CUnit* unit = unit_;
    if (!unit || selected_ == unit->pendingWeapon) return;
    Highlight(selected_, 0);
    selected_ = unit_->pendingWeapon;
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
        if (!regions_.data[i]->HasActiveTip()) continue;
        ItemTipAt(i, &text);
        regions_.data[i]->SetTipText(text);
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
    if (interface_->doubleClick_)
        ClickAlternateAt(region);
    else
        ClickAt(region);
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
    SelectAt(region);
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
        // The session's player records: id_ (+0x2C) the player's unit, serial_ (+0x30) its id.
        auto* end = static_cast<SessionPlayerList::Node*>(net::GameClient()->players_);
        for (SessionPlayerList::Node* node = end->next; node != end; node = node->next) {
            if (node->data->id_ == unitId) {
                playerId = node->data->serial_;
                break;
            }
        }
    }
    char text[128];
    gamecrt::sprintf(text, "%d", playerId);
    // 0041D260 (ClientSession::SendCommand, rewritten): the server commands 4 and 5.
    if (mode == 1)
        net::GameClient()->SendCommand(4, text);
    else if (mode == 2)
        net::GameClient()->SendCommand(5, text);
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
    if (region >= 0 && region < count_) GameCursor()->SetShape(0);
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
        SetTint(sceneObjects_.data[6 + i], view_ == i + 1 ? kTintOn : kTintOff);
}
UI_INPUT_HANDLER(0x00645060, CHudMap, OnMouseDown);

void CHudMap::OnMouseUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    if (view_ != 0) {
        // A view button was held: the zoom stops and both buttons go dim.
        view_ = 0;
        for (int i = 0; i < 2; ++i) SetTint(sceneObjects_.data[6 + i], view_ == i + 1 ? kTintOn : kTintOff);
        return;
    }
    const int region = RegionAt(x, y);
    switch (region) {
    case 0:     // the sling button
        interface_->PlaySound(kSoundSling, 0);
        slideDirection_ = -slideDirection_;
        sliding_ = 1;
        inputHandled_ = true;
        return;
    case 1:     // the round map (zoomed)
    case 2: {   // the map area
        // The world point under the click; ScalePointFromScreen (005F7E00) is Rebirth-patched:
        // through its address.
        POINT point = {x, y};
        interface_->CallScalePointFromScreen(&point);
        float right = static_cast<float>(point.x - kMapCentreX) / halfSide_;
        float down = static_cast<float>(point.y - kMapCentreY) / halfSide_;
        if (region == 1) {
            right /= zoom_;
            down /= zoom_;
        }
        const float worldX = RoundToFloat(right / scale_ + centreX_);
        const float worldY = RoundToFloat(centreY_ - down / scale_);
        if (worldX > 0.0f && worldX < worldWidth_ && worldY > 0.0f && worldY < worldHeight_) {
            // The camera stops following its unit and looks there.
            CCamera* view = camera::TheCamera();
            view->flags_ &= ~kCameraFollows;
            view->SetTargetId(-1);
            Vector3 focus = view->focus_;
            focus.x = worldX;
            focus.y = worldY;
            view->SetFocus(&focus);
        }
        inputHandled_ = true;
        return;
    }
    case 5:     // "camera north"
        interface_->PlaySound(kSoundClick, 0);
        camera::TheCamera()->SetHeading(0.0f);
        unknown_1B0 = 0.5f;
        inputHandled_ = true;
        return;
    default:
        if (region >= 0) inputHandled_ = true;
        return;
    }
}
UI_INPUT_HANDLER(0x00645210, CHudMap, OnMouseUp);

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
        for (int i = 0; i < 4; ++i) SetTint(sceneObjects_.data[3 + i], bodyPart_ == i ? kTintOn : kTintOff);
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

void CHudClock::OnMouseUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    const int region = RegionAt(x, y);
    switch (region) {
    case 0: {   // the pause button
        inputHandled_ = true;
        interface_->PlaySound(kSoundClock, 0);
        const bool paused = GameIsPausedOriginal(GameGlueObject());
        GameSetPausedOriginal(GameGlueObject(), nullptr, paused ? 0 : 1);
        return;
    }
    case 1:     // the normal speed
    case 2:     // the fast speed
        inputHandled_ = true;
        if (IsNetworkClientOriginal()) return;
        if (GameIsPausedOriginal(GameGlueObject())) GameSetPausedOriginal(GameGlueObject(), nullptr, 0);
        fast_ = region - 1;
        UpdatePeriod() = fast_ ? kFastPeriod : kNormalPeriod;
        interface_->fastGame_ = fast_;
        interface_->PlaySound(kSoundClock, 0);
        return;
    case 3:     // the objectives
        inputHandled_ = true;
        CallOpenObjectives();
        return;
    default:
        return;
    }
}
UI_INPUT_HANDLER(0x00648D70, CHudClock, OnMouseUp);

void CHudClock::OnMouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) inputHandled_ = true;
}
UI_INPUT_HANDLER(0x00649160, CHudClock, OnMouseMove);

void CHudClock::OpenObjectives() {
    highlight_ = false;
    interface_->PlaySound(kSoundClick, 0);
    void* memory = gamecrt::operator_new(sizeof(CObjectivesWindow));
    void* window = memory ? ObjectivesConstructOriginal(memory, nullptr, interface_) : nullptr;
    {
        BriefingPlayerName zone;
        ObjectivesSetupOriginal(window, nullptr, zone, nullptr, 0, 0);
    }
    InterfacePushWindowOriginal(interface_, nullptr, window, 1, 1);
}
EI_REPLACE_METHOD(0x00648F10, CHudClock::OpenObjectives);

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
        CallToggleFlag();
        return;
    }
    int mode = region - 1;
    if (!WorldClient()->LocalView()) return;
    if (mode >= 0 && mode < 4) {
        interface_->PlaySound(kSoundClick, 0);
        for (int i = 0; i < owner_->partyCount_; ++i) {
            CUnit* unit = owner_->party_[i];
            if (unit->flags_ & 0x10) ViewSetMoveMode(LocalView(), nullptr, unit, mode);
        }
    }
    ShowMode(mode);
}
UI_INPUT_HANDLER(0x00649670, CHudMovePanel, OnMouseUp);

void CHudMovePanel::OnMouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) inputHandled_ = true;
}
UI_INPUT_HANDLER(0x00649750, CHudMovePanel, OnMouseMove);

void CHudMovePanel::ShowModeVanilla(int mode) {
    mode_ = mode;
    auto* marker = reinterpret_cast<CUiMesh*>(sceneObjects_.data[0]);
    marker->visible_ = mode < 0 ? 0 : 1;
    for (int i = 0; i < 4; ++i) {
        CEngineObject* button = sceneObjects_.data[1 + i];
        if (mode_ != i) {
            SetTint(button, kTintDim);
            continue;
        }
        // The marker points at the mode's button: turned about -z and put on its circle. Place
        // (005E6FA0) is Rebirth-patched: through its address.
        const float angle = RoundToFloat(static_cast<float>(2 * i + 1) * Pi() * Sixteenth());
        const Vector3 axis = {0.0f, 0.0f, -1.0f};
        Quaternion turn;
        marker->orientation_ = *QuaternionSetAxisAngleOriginal(&turn, nullptr, &axis, angle);
        const float y = MarkerCentreY() - std::sin(angle) * MarkerRadius();
        const float x = std::cos(angle) * MarkerRadius();
        SpritePlaceOriginal(sceneObjects_.data[0], nullptr, x, y, kMarkerDepth);
        SetTint(button, kTintOn);
    }
}
EI_REPLACE_METHOD(0x00649910, CHudMovePanel::ShowModeVanilla);

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
            flag = unit->fightAtWill;
        else if (flag != unit->fightAtWill)
            flag = -2;
    }
    ShowMode(mode);
    flag_ = flag;
    // Scene object 5 shows the flag through its texture offset (uOffset_, +0xCC).
    std::uint32_t bits = flag == 0 ? 0x3EAC0000u : flag == 1 ? 0u : 0x3EF60000u;
    std::memcpy(&reinterpret_cast<CUiMesh*>(sceneObjects_.data[5])->uOffset_, &bits, 4);
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
        CallShowPage();
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

// --- CBattleScreen::SelectUnit (00638EF0) --------------------------------------------------------

namespace {

constexpr std::uint32_t kFlagSelected = 0x10;
constexpr int kQuickSlots = 8;
constexpr std::uint32_t kSpriteOpaque = 0xFF000000u;
constexpr std::uint32_t kScaleLarge = 0x3F59999A;   // 0.85: quick slots 4..7
constexpr std::uint32_t kScaleSmall = 0x3F0CCCCD;   // 0.55: quick slots 0..3
constexpr std::uint32_t kScaleOne = 0x3F800000;     // 1.0
constexpr int kOrderEventFollow = 7;                // the speech event of a follow order

// A unit's position (CUnit position), copied as the original copies it (three dwords).
Vector3 PositionOf(const CUnit* unit) {
    Vector3 v;
    std::memcpy(&v, &unit->position.x, sizeof(v));
    return v;
}

// The battle screen's own commands (thiscall; the flag argument is the interface's +0x7F byte).
__declspec(naked) void __fastcall ScreenOrderAt(CBattleScreen*, void*, Vector3, int, char) EI_ORIGINAL(0x00636F10)
__declspec(naked) void __fastcall ScreenOrderUnit(CBattleScreen*, void*, CUnit*, char) EI_ORIGINAL(0x00637190)
__declspec(naked) void __fastcall ScreenOrderDead(CBattleScreen*, void*, CUnit*, char) EI_ORIGINAL(0x006373A0)
void ScreenCastOn(CBattleScreen* screen, void*, CUnit* target, char quick, int ctrl) {
    BattleUseOnUnitOriginal(screen, nullptr, target, quick, ctrl);
}
void ScreenCastAt(CBattleScreen* screen, void*, Vector3 at, CUnit* unit, int quick) {
    BattleUseAtPointOriginal(screen, nullptr, at.x, at.y, at.z, unit, static_cast<char>(quick));
}
__declspec(naked) void __fastcall ScreenToggleMember(CBattleScreen*, void*, int) EI_ORIGINAL(0x006379E0)
// The HUD panels: quick items 0063CD20 (slot, 0), spells 0063E750 (index, 0), actions 0063F730
// (index, 0) and 0063F7A0 (all).
void __fastcall QuickPanelSet(void* a0, void*, int a2, int a3) { QuickPanelSetOriginal(a0, nullptr, a2, a3); }
void __fastcall SpellPanelSet(void* a0, void*, int a2, int a3) { SpellPanelSetOriginal(a0, nullptr, a2, a3); }
void __fastcall ActionPanelSet(void* a0, void*, int a2, int a3) { ActionPanelSetOriginal(a0, nullptr, a2, a3); }
void __fastcall ActionPanelReset(void* a0) { ActionPanelClearOriginal(a0); }
// The client world (WorldClient(), 007AF5B8): the object under a screen point (x, y pair), the
// ground point under it (CWorldClient::PickSurface, rewritten), the local view (LocalView()).
// The local view's orders: 0066AA20 (unit, target, a, b, flag), 0066AFE0 (unit, target;
// CPlayerClient::SendFollow, through its address).
__declspec(naked) void __fastcall ViewOrderTarget(CPlayerClient*, void*, CUnit*, CUnit*, int, int, int) EI_ORIGINAL(0x0066AA20)
__declspec(naked) void __fastcall ViewOrderFollow(CPlayerClient*, void*, CUnit*, CUnit*) EI_ORIGINAL(0x0066AFE0)
// CUnit::GetQuickItem, CItem::GetCategory and the sprite's SetScale, through their addresses
// (the camera's SetTarget too: CameraSetTargetOriginal).
void __fastcall SpriteSetScale(CEngineObject* sprite, void*, std::uint32_t scaleBits) {
    SpriteSetScaleOriginal(sprite, nullptr, scaleBits);
}

constexpr int kCategoryWand = 8;
IsaacRandom& UiRandom() { return IsaacRandom::Global(); }
// 00742760: the factor turning a random dword into the portraits' negative start delay.
double PortraitDelayScale() { return *reinterpret_cast<const volatile double*>(0x00742760); }
// A random value times ~-2^-34 * 0.3 (00742760), as the original stores it.
float PortraitDelay(std::uint32_t random) {
    return static_cast<float>(x87::FromUnsigned(random) * PortraitDelayScale());
}
// Portrait `index` nods (wobble kind 1) after a short random delay.
void NodPortrait(CBattleScreen* screen, int index) {
    HudPortraitSlot* slots = screen->portraits_->slots_;
    float delay = PortraitDelay(UiRandom().Next());
    slots[index].wobbleTime_ = delay;
    screen->portraits_->slots_[index].wobbleKind_ = 1;
}
// fcomp with 0.0 and C3: true for zero (either sign) and for NaN.
bool ZeroOrNaN(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return (bits & 0x7FFFFFFFu) == 0 || x87::IsNaN(value);
}

} // namespace

void CBattleScreen::SelectUnit(CUnit* unit, int x, int y) {
    auto ctrl = [this] { return static_cast<char>(interface_->doubleClick_); };
    // Slot 55 (+0xDC, IsDead): non-zero for a dead unit.
    auto unitDead = [](CUnit* u) { return ObjectSlots(u)->IsDead() != 0; };
    auto pick = [x, y]() {
        Vector3 out;
        Vector3* p = HoverPickSurface(WorldClient(), nullptr, &out, x, y);
        return Vector3{p->x, p->y, p->z};
    };
    auto unitPosition = [](CUnit* u) { return PositionOf(u); };
    auto objectAt = [x, y]() {
        int point[2] = {x, y};
        return HoverObjectAt(WorldClient(), nullptr, point);
    };
    // The portrait of member `index` flashes after a short random delay.
    auto flash = [this](int index) { NodPortrait(this, index); };
    // Back to plain selection: the pending action and its item and spell are dropped and the
    // panels' highlights cleared.
    auto resetMode = [this](bool actionLoop) {
        CHudQuickItems* quick = quickItems_;
        pendingAction_ = 0;
        pendingSlot_ = -1;
        pendingItem_ = -1;
        for (int i = 0; i < kQuickSlots; ++i) QuickPanelSet(quick, nullptr, i, 0);
        quick->selected_ = -1;
        CHudSpellColumn* spells = spellColumn_;
        for (int i = 0; i < spells->count_; ++i) SpellPanelSet(spells, nullptr, i, 0);
        spells->selected_ = -1;
        CHudActionColumn* actions = actionColumn_;
        if (!actionLoop) {
            ActionPanelReset(actions);
            return;
        }
        for (int i = 0; i < actions->count_; ++i) ActionPanelSet(actions, nullptr, i, 0);
        actions->selected_ = -1;
    };
    auto order = [this](CUnit* first, CUnit* target, int a, int b) {
        int flag = static_cast<std::uint8_t>(interface_->doubleClick_);
        ViewOrderTarget(LocalView(), nullptr, first, target, a, b, flag);
    };

    CUnit* living = (unit && !unitDead(unit)) ? unit : nullptr;
    int count = partyCount_;
    int firstIndex = -1;
    CUnit* first = nullptr;
    int selected = 0;
    for (int i = 0; i < count; ++i) {
        CUnit* member = party_[i];
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        if (!first) {
            first = member;
            firstIndex = i;
        }
        ++selected;
    }

    if (count <= 0 || selected == 0) {
        // Nobody selected: a click on a portrait's unit selects it (Ctrl: the camera follows it).
        if (count <= 0) return;
        for (int i = 0; i < partyCount_; ++i) {
            if (unit != party_[i]) continue;
            interface_->PlaySound(game::SoundOnOffText(), 0);
            if (interface_->doubleClick_) {
                if (i < partyCount_) CameraSetTargetOriginal(camera::TheCamera(), nullptr, party_[i]);
            } else {
                ScreenToggleMember(this, nullptr, i);
            }
        }
        return;
    }

    if (interface_->altDown_) {
        ScreenOrderAt(this, nullptr, unit ? unitPosition(unit) : pick(), 0, ctrl());
        return;
    }
    bool anyFlag = false;
    for (int i = 0; i < 6 && !anyFlag; ++i) anyFlag = orderKeys_[i] != 0;
    if (anyFlag) {
        if (unit && !unitDead(unit)) {
            ScreenOrderUnit(this, nullptr, unit, ctrl());
            return;
        }
        ScreenOrderAt(this, nullptr, pick(), 1, ctrl());
        return;
    }
    if (interface_->ctrlDown_) {
        if (unit && !unitDead(unit)) {
            ScreenOrderUnit(this, nullptr, unit, ctrl());
            return;
        }
        ScreenOrderAt(this, nullptr, pick(), 1, ctrl());
        return;
    }

    switch (pendingAction_) {
    case 0:   // plain: select, attack or move
        if (unit) {
            if (unitDead(unit)) {
                ScreenOrderDead(this, nullptr, unit, ctrl());
                return;
            }
            int members = partyCount_;
            for (int i = 0; i < members; ++i) {
                if (unit != party_[i]) continue;
                interface_->PlaySound(game::SoundOnOffText(), 0);
                if (interface_->doubleClick_) {
                    if (i < partyCount_) CameraSetTargetOriginal(camera::TheCamera(), nullptr, party_[i]);
                } else {
                    ScreenToggleMember(this, nullptr, i);
                }
                return;
            }
            if (members != 0 && party_[0] && HoverHostileTo(party_[0], nullptr, unit)) {
                ScreenOrderUnit(this, nullptr, unit, ctrl());
                return;
            }
            ScreenOrderAt(this, nullptr, unitPosition(unit), 0, ctrl());
            return;
        }
        if (CUnit* object = objectAt()) {
            if (ObjectSlots(object)->IsUsable() != 0 && ObjectSlots(object)->IsDirectUseBlocked() == 0 && selected == 1) {
                order(first, object, 0, -1);
                flash(firstIndex);
                return;
            }
        }
        {
            Vector3 at = pick();
            ScreenOrderAt(this, nullptr, at, 0, ctrl());
            // "Terrain Coords x y z" on the console: each float through the inline operator<<
            // (x_floatused set, printed as a double), then '\n' and flush.
            console::GameOStream& stream = (console::Out7C5760() << game::TerrainCoordsText())
                                               .PutFloat(at.x) << game::SpaceText();
            console::EndLine((stream.PutFloat(at.y) << game::SpaceText()).PutFloat(at.z));
        }
        return;
    case 1:   // a spell on a unit
        if (!unit || unitDead(unit) || !first) return;
        // Rebirth's FixSpellSelfCasting #1 (MOV CL,0 at 00639447): the cast no longer passes the
        // Ctrl flag.
        ScreenCastOn(this, nullptr, unit,
                     ei::rebirth::Enabled(ei::rebirth::Patch::FixSpellSelfCasting) ? '\0' : ctrl(), 0);
        return;
    case 2:   // a spell on a point
    case 4: {
        Vector3 at = (unit && !unitDead(unit)) ? unitPosition(unit) : pick();
        if (ZeroOrNaN(at.x) && ZeroOrNaN(at.y)) return;
        if (!first) return;
        ScreenCastAt(this, nullptr, at, living, pendingAction_ == 4 ? 1 : 0);
        return;
    }
    case 3: {   // a quick item on a unit
        if (!unit || unitDead(unit) || !first) return;
        void* item = UnitQuickItem(first, nullptr, pendingSlot_);
        if (item && ItemGetCategoryOriginal(reinterpret_cast<CItem*>(item)) == kCategoryWand && unit != first) return;
        ScreenCastOn(this, nullptr, unit, 1, 0);
        return;
    }
    case 5: {   // use an item on an object
        CUnit* target = unit ? unit : objectAt();
        if (!target || !first) return;
        if (ObjectSlots(target)->IsUsable() == 0) return;
        order(first, target, 0, pendingItem_);
        flash(firstIndex);
        resetMode(false);
        return;
    }
    case 6: {   // use a spell on an object
        CUnit* target = unit ? unit : objectAt();
        if (!target || !first) return;
        if (ObjectSlots(target)->IsUsable() == 0) return;
        if (ObjectSlots(target)->AcceptsKey(&itemUseKey_) == 0) return;
        order(first, target, 0, pendingItem_);
        flash(firstIndex);
        resetMode(true);
        return;
    }
    case 7: {   // follow: every selected member follows the unit
        if (!unit || unitDead(unit)) return;
        for (int i = 0; i < partyCount_; ++i) {
            CUnit* member = party_[i];
            if (!member || !(member->flags_ & kFlagSelected)) continue;
            ViewOrderFollow(LocalView(), nullptr, member, unit);
            VirtualUnitSpeech(party_[i]->id_, kOrderEventFollow, nullptr);
            NodPortrait(this, i);
        }
        CHudQuickItems* quick = quickItems_;
        pendingSlot_ = -1;
        pendingItem_ = -1;
        pendingAction_ = 0;
        for (int i = 0; i < kQuickSlots; ++i) {
            int slot = quick->regionItem_[i];
            if (slot < 0 || slot >= quick->unknown_14C) continue;
            CEngineObject* sprite = quick->sceneObjects_.data[slot];
            SpriteSetScale(sprite, nullptr, i < 4 ? kScaleSmall : kScaleLarge);
            reinterpret_cast<CUiMesh*>(sprite)->colour_ = kSpriteOpaque;
        }
        quick->selected_ = -1;
        // The spell and action columns (the same layout: count_, selected_, the sprites).
        auto unhighlight = [](auto* panel) {
            for (int i = 0; i < panel->count_; ++i) {
                CEngineObject* sprite = panel->sceneObjects_.data[i];
                SpriteSetScale(sprite, nullptr, kScaleOne);
                reinterpret_cast<CUiMesh*>(sprite)->colour_ = kSpriteOpaque;
                panel->selected_ = i;
            }
            panel->selected_ = -1;
        };
        unhighlight(spellColumn_);
        unhighlight(actionColumn_);
        return;
    }
    case 8: {   // an order on an object (a unit outside the party, or the object under the mouse)
        CUnit* target = unit ? unit : objectAt();
        if (!target || !first) return;
        if (unit) {
            for (int i = 0; i < partyCount_; ++i)
                if (unit == party_[i]) return;
            order(first, unit, 1, -1);
        } else {
            if (ObjectSlots(target)->IsUsable() == 0) return;
            order(first, target, 0, -1);
        }
        flash(firstIndex);
        resetMode(true);
        return;
    }
    default:
        return;
    }
}
EI_REPLACE_METHOD(0x00638EF0, CBattleScreen::SelectUnit);
EI_REBIRTH_IMPLEMENTS(0x00638EF0, ei::rebirth::Patch::FixSpellSelfCasting);

// --- 0063DB20: the quick item panel's button release -------------------------------------------

namespace {
// The quick item panel (thiscall, RET 4, slot): 0063DD70 picks the unit's quick item `slot` (a wand
// or scroll with a spell starts target mode 3 / 4), 0063E020 uses it at once (key +0x7F).
__declspec(naked) void __fastcall QuickPanelPick(CHudQuickItems*, void*, int) EI_ORIGINAL(0x0063DD70)
// Slot 21 (+0x54) of a player's loot item (CItemLoot, type 0x3009): its key (the one map object
// slot 42 AcceptsKey tests) into `buffer`, returns it. CItem declares only slots 13-15 (item.h),
// so the slot is reached through the vtable here until the item classes declare 16-21 (16, 19
// and 20 are known: GetPrice, FigureName, PictureName).
const LockKey* ItemUseKey(CEngineObject* item, LockKey* buffer) {
    using Fn = const LockKey*(__thiscall*)(CEngineObject*, LockKey*);
    constexpr int kSlot = 21;
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(item))[kSlot])(item, buffer);
}
} // namespace

void CHudQuickItems::OnMouseUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    // Unchecked, as in the original (region -1 reads +0x168).
    int entry = RegionItem(region);
    if (entry < 0) return;
    inputHandled_ = true;
    if (region < 4 && entries_[entry].own) {
        if (interface_->doubleClick_)
            UseNowAt(region);
        else
            QuickPanelPick(this, nullptr, region);
        return;
    }
    CBattleScreen* screen = owner_;
    auto clearPanels = [screen](int value) {
        CHudQuickItems* quick = screen->quickItems_;
        for (int i = 0; i < kQuickSlots; ++i) QuickPanelSet(quick, nullptr, i, 0);
        quick->selected_ = value;
        CHudSpellColumn* spells = screen->spellColumn_;
        for (int i = 0; i < spells->count_; ++i) SpellPanelSet(spells, nullptr, i, 0);
        spells->selected_ = value;
        CHudActionColumn* actions = screen->actionColumn_;
        for (int i = 0; i < actions->count_; ++i) ActionPanelSet(actions, nullptr, i, 0);
        actions->selected_ = value;
    };
    if (selected_ != region) {
        // One of the player's items (top row): "use on an object" (mode 6) with the item's
        // key (slot 21) in the screen's itemUseKey_ and its id in pendingItem_.
        interface_->PlaySound(kSoundClick, 0);
        LockKey buffer;
        screen->itemUseKey_ = *ItemUseKey(entries_[entry].item, &buffer);
        screen->pendingItem_ = entries_[entry].id;
        screen->pendingSlot_ = region;
        screen->pendingAction_ = 6;
        clearPanels(-1);
        QuickPanelSet(screen->quickItems_, nullptr, region, 1);
        return;
    }
    // The highlighted one again: back to plain selection.
    interface_->PlaySound(kSoundClick, 0);
    screen->pendingSlot_ = -1;
    screen->pendingAction_ = 0;
    screen->pendingItem_ = -1;
    clearPanels(-1);
}
UI_INPUT_HANDLER(0x0063DB20, CHudQuickItems, OnMouseUp);

// --- 00636F10: the selected units ordered to a point -------------------------------------------

namespace {

// The local player glue (ClientGlueObject(), 007AFEA5, thiscall): 00578A00 the zone exit at a
// point (RET 8; jumps to the client world's 00567440), 00578A40 an exit's zone name (RET 4;
// GameExitZoneOriginal).
__declspec(naked) int __fastcall GlueExitAt(void*, void*, const Vector3*, int*) EI_ORIGINAL(0x00578A00)
// The CArray<CUnit*> the order takes (vtable kUnitPtrArrayVtable; SetSize 005EBDB0,
// PartyArraySetSizeOriginal).
using OrderUnits = mfc::CArray<CUnit*>;
// The local view's orders (thiscall, RET 0xC; units, point, the Ctrl flag): 0066A7D0 move there
// (LocalViewSendWalkOriginal), 0066A720 the other order (mode non-zero: the modifier flags or Shift;
// CPlayerClient::SendGoTo, through its address).
__declspec(naked) void __fastcall ViewOrderMoveAlt(CPlayerClient*, void*, OrderUnits*, const Vector3*, int) EI_ORIGINAL(0x0066A720)
// 0073B84C: the constant 1.0f the zone variable is compared with.
float GameOne() { return *reinterpret_cast<const volatile float*>(0x0073B84C); }

} // namespace

void CBattleScreen::OrderAt(float x, float y, float z, int mode, int ctrl) {
    // The original passes the address of its own argument slots as the point.
    Vector3 point = {x, y, z};
    int selected = 0;
    for (int i = 0; i < partyCount_; ++i) {
        CUnit* member = party_[i];
        if (member && (member->flags_ & kFlagSelected)) ++selected;
    }

    // A point on a zone exit that is open ("z.<zone>" not 1; "none" is no zone): remembered for
    // the update to ask about leaving (+0x18C, +0x188 the exit).
    exitPending_ = 0;
    int exit;
    void* glue = ClientGlueObject();
    if (GlueExitAt(glue, nullptr, &point, &exit) && selected > 0) {
        mfc::CString name(GameExitZoneOriginal(glue, nullptr, exit));
        if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(name)),
                             reinterpret_cast<const unsigned char*>(game::NoneText())) != 0) {
            name = mfc::CString(game::ZoneVariablePrefixText()) + name;   // "z."
            double value = HoverPlayerVariable(glue, nullptr, name);
            const float one = GameOne();
            // FCOMP; TEST AH,0x40; JNZ: equal or NaN skips.
            if (!x87::IsNaN(static_cast<float>(value)) && value != one) {
                exitPending_ = 1;
                pendingExit_ = exit;
            }
        }
    }

    // The selected members go; each one's portrait nods after a short random delay.
    OrderUnits units{reinterpret_cast<void*>(kUnitPtrArrayVtable), nullptr, 0, 0, 0};
    for (int i = 0; i < partyCount_; ++i) {
        CUnit* member = party_[i];
        if (!member || !(member->flags_ & kFlagSelected)) continue;
        int n = units.size;
        PartyArraySetSizeOriginal(&units, nullptr, n + 1, -1);
        units.data[n] = member;
        NodPortrait(this, i);
    }
    if (units.size != 0) {
        CPlayerClient* view = LocalView();
        if (static_cast<std::uint8_t>(mode) != 0)
            ViewOrderMoveAlt(view, nullptr, &units, &point, ctrl & 0xFF);
        else
            LocalViewSendWalkOriginal(view, nullptr, &units, &point.x, ctrl & 0xFF);
    }
    units.vtable = reinterpret_cast<void*>(kUnitPtrArrayVtable);
    units.FreeData();
}
EI_REPLACE_METHOD(0x00636F10, CBattleScreen::OrderAt);
