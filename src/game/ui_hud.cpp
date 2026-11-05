// The battle screen's HUD panels: mouse handlers, per-frame updates and the small drawing
// wrappers. See ui.h ("Battle screen and its HUD panels") and docs/notes/ui.md.
#include "app.h"
#include "camera.h"
#include "console.h"
#include "game_singletons.h"
#include "item.h"
#include "map_object.h"
#include "net_classes.h"
#include "net_messages.h"
#include "player_client.h"
#include "render/render_state.h"
#include "screen_layout.h"
#include "texture.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_cursor.h"
#include "ui_screens.h"
#include "unit.h"
#include "world.h"

#include "lib/containers.h"
#include "lib/maths.h"
#include "lib/text_case.h"
#include "lib/vecmath.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {
constexpr const char* kSoundClick = "buttons\\battle\\click.wav";
constexpr const char* kSoundWeapon = "buttons\\battle\\weapon.wav";

// Scene objects of the panels (sprites, UiMesh) are tinted through their three tint floats (the
// original stores the constants as dwords).
void SetTint(UiMesh* object, std::uint32_t bits) {
    float* tint = object->tint;
    tint[0] = std::bit_cast<float>(bits);
    tint[1] = std::bit_cast<float>(bits);
    tint[2] = std::bit_cast<float>(bits);
}
constexpr std::uint32_t kTintOn = 0x3F800000;    // 1.0f
constexpr std::uint32_t kTintOff = 0x3F000000;   // 0.5f
constexpr std::uint32_t kTintDim = 0x3F333333;   // 0.7f: the movement panel's other modes

constexpr const char* kSoundClock = "buttons\\battle\\clock.wav";
// The world's update period in milliseconds: the game's speed.
constexpr int kNormalPeriod = 55;
constexpr int kFastPeriod = 27;

// The constants the movement panel's marker is placed with, read where the game reads them.
constexpr float kSixteenth = 0.0625f;
constexpr float kMarkerRadius = 73.0f;
constexpr float kMarkerCentreY = 600.0f;
constexpr float kMarkerDepth = 8.0f;

// The map's centre in the layout, and the camera flag a click on the map clears.
constexpr int kMapCentreX = 700;
constexpr int kMapCentreY = 80;
constexpr std::uint32_t kCameraFollows = 8;

void DrawWithSpecular(UiWindow* window, SceneRenderer* pass) {
    render::g_renderState.specular = true;
    window->DrawBase(pass);
    render::g_renderState.specular = false;
}

constexpr int kWobbleNod = 1;   // HudPortraitSlot::wobbleKind

// Whether `x` is zero (either sign).
bool IsZero(float value) {
    return value == 0.0f;
}

} // namespace

// --- Weapon bar --------------------------------------------------------------------------------

void HudWeaponBar::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: PressRegion(event, count); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::MouseMove: HitRegion(event, count); break;
    default: break;
    }
}

void HudWeaponBar::LeftUp(UiEvent& event) {
    if (const int slot = RegionIndex(regions, PressedRegion(event, count)); slot >= 0) Choose(slot);
}

void HudWeaponBar::Choose(int slot) {
    const int previous = selected;
    if (slot >= count || slot == previous) return;
    if (previous >= 0 && previous < count) Highlight(previous, false);
    Highlight(slot, true);
    selected = slot;
    ui->PlayInterfaceSound(kSoundWeapon);
    // No check for a missing local view here (like the original).
    if (unit) g_clientWorld.LocalView()->SetWeapon(unit, slot);
}

void HudWeaponBar::Update() {
    Unit* currentUnit = unit;
    if (!currentUnit || selected == currentUnit->pendingWeapon) return;
    Highlight(selected, false);
    selected = unit->pendingWeapon;
    Highlight(selected, true);
}

void HudWeaponBar::DrawSprites(SceneRenderer* pass) {
    render::RenderState& state = render::g_renderState;
    const std::uint8_t alphaRef = state.alphaReference;
    const bool alphaTest = state.alphaTest;
    state.alphaReference = 1;
    state.alphaTest = true;
    state.specular = true;
    DrawBase(pass);
    state.alphaReference = alphaRef;
    state.alphaTest = alphaTest;
    state.specular = false;
}

// The three other panels draw the same way, with specular on (the draw virtual).

void HudQuickItems::DrawSprites(SceneRenderer* pass) { DrawWithSpecular(this, pass); }
void HudSpellColumn::DrawSprites(SceneRenderer* pass) { DrawWithSpecular(this, pass); }
void HudActionColumn::DrawSprites(SceneRenderer* pass) { DrawWithSpecular(this, pass); }

// --- Quick items -------------------------------------------------------------------------------

void HudQuickItems::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: LeftDown(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::MouseMove: MouseMove(event); break;
    default: break;
    }
}

void HudQuickItems::LeftDown(UiEvent& event) {
    if (RegionItem(RegionIndex(regions, RegionUnder(event.at))) < 0) return;
    event.handled = true;
    CaptureMouse();
}

void HudQuickItems::MouseMove(UiEvent& event) {
    if (RegionItem(RegionIndex(regions, RegionUnder(event.at))) >= 0) event.handled = true;
}

void HudQuickItems::Update() {
    if (!unit) return;
    for (unsigned i = 0; i < 4; ++i) {
        if (!unit->GetQuickItem(i)) continue;
        if (!regions[i]->HasActiveTip()) continue;
        regions[i]->SetTipText(ItemTip(i));
    }
}

// --- Spell column ------------------------------------------------------------------------------

void HudSpellColumn::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: PressRegion(event, count); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::MouseMove: HitRegion(event, count); break;
    default: break;
    }
}

void HudSpellColumn::LeftUp(UiEvent& event) {
    const int index = RegionIndex(regions, PressedRegion(event, count));
    if (index < 0) return;
    if (ui->doubleClick)
        ClickAlternate(index);
    else
        Click(index);
}

// --- Action column -----------------------------------------------------------------------------

void HudActionColumn::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: PressRegion(event, count); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::MouseMove: HitRegion(event, count); break;
    default: break;
    }
}

void HudActionColumn::LeftUp(UiEvent& event) {
    if (const int index = RegionIndex(regions, PressedRegion(event, count)); index >= 0) Select(index);
}

// --- Portraits ---------------------------------------------------------------------------------

// The portraits read the portrait mode of the battle screen they belong to (the game reads the interface's active
// screen: the battle screen while they get input).

void HudPortraits::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: LeftDown(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::MouseMove: MouseMove(event); break;
    default: break;
    }
}

void HudPortraits::LeftDown(UiEvent& event) {
    const int mode = owner->portraitMode;
    if (mode == 0) {
        // Any portrait region takes the event; only a party member's takes the mouse.
        if (const int region = RegionIndex(regions, HitRegion(event)); region >= 0 && region < std::ssize(owner->party))
            CaptureMouse();
        return;
    }
    const int region = RegionIndex(regions, RegionUnder(event.at));
    if (region < 0 || region >= count) return;
    // Picking a player for a server command: the player id of the slot, or of the session
    // player whose unit is the portrait's unit.
    int playerId = -1;
    if (slots[region].hasPlayer) {
        playerId = slots[region].playerId;
    } else {
        int unitId = owner->party[region]->id;
        // The session's player records: id the player's unit, serial its id.
        for (auto node = net::g_gameClient->players.begin(); node != net::g_gameClient->players.end(); ++node) {
            auto* player = node->Get();
            if (player->id == unitId) {
                playerId = player->serial;
                break;
            }
        }
    }
    char text[128];
    std::snprintf(text, sizeof(text), "%d", playerId);
    // ClientSession::SendCommand: kick / ban by serial.
    if (mode == 1)
        net::g_gameClient->SendCommand(net::CommandKind::KickBySerial, text);
    else if (mode == 2)
        net::g_gameClient->SendCommand(net::CommandKind::BanBySerial, text);
    owner->portraitMode = 0;
}

void HudPortraits::LeftUp(UiEvent& event) {
    if (owner->portraitMode != 0) return;
    const int region = RegionIndex(regions, PressedRegion(event));
    // Rebirth's FixSpellSelfCasting patches SelectUnit.
    if (region >= 0 && region < std::ssize(owner->party)) owner->SelectUnit(owner->party[region], {});
}

void HudPortraits::MouseMove(UiEvent& event) {
    if (owner->portraitMode == 0) {
        const int region = RegionIndex(regions, HitRegion(event));
        if (region >= 0 && region < std::ssize(owner->party)) owner->HoverUnit(owner->party[region], {});
        return;
    }
    const int region = RegionIndex(regions, RegionUnder(event.at));
    if (region >= 0 && region < count) g_cursor.SetShape(CursorShape::Default);
}

// --- Map ---------------------------------------------------------------------------------------

void HudMap::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: LeftDown(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::MouseMove: HitRegion(event); break;
    case UiEventKind::Wheel: Wheel(event); break;
    default: break;
    }
}

void HudMap::LeftDown(UiEvent& event) {
    const UiRegion* region = PressRegion(event);
    if (region != viewButtons[0] && region != viewButtons[1]) return;
    ui->PlayInterfaceSound(kSoundClick);
    view = region == viewButtons[0] ? 1 : 2;
    ShowView();
}

void HudMap::ShowView() {
    for (int i = 0; i < 2; ++i) SetTint(viewIcons[i], view == i + 1 ? kTintOn : kTintOff);
}

void HudMap::LeftUp(UiEvent& event) {
    if (!ReleaseMouse()) return;
    if (view != 0) {
        // A view button was held: the zoom stops and both buttons go dim (the event is not marked used).
        view = 0;
        ShowView();
        return;
    }
    const UiRegion* region = HitRegion(event);
    if (region == slingButton) {
        slide.Toggle(ui);
    } else if (region == roundMap || region == mapSquare) {
        // The world point under the click.
        Point at = event.at;
        ui->ScalePointFromScreen(&at);
        // WideScreen: the centre moved right by the shift.
        float right = static_cast<float>(at.x - (kMapCentreX + screen_layout::EdgeShift())) / halfSide;
        float down = static_cast<float>(at.y - kMapCentreY) / halfSide;
        if (region == roundMap) {
            right /= zoom;
            down /= zoom;
        }
        const float worldX = right / scale + centre.x;
        const float worldY = centre.y - down / scale;
        if (worldX > 0.0f && worldX < worldWidth && worldY > 0.0f && worldY < worldHeight) {
            // The camera stops following its unit and looks there.
            Camera* camera = &g_camera;
            camera->flags &= ~kCameraFollows;
            camera->SetTargetId(-1);
            camera->SetFocus(WithZ(Vector2{worldX, worldY}, camera->focus.z));
        }
    } else if (region == northButton) {
        FaceNorth();
    }
}

void HudMap::FaceNorth() {
    ui->PlayInterfaceSound(kSoundClick);
    g_camera.SetHeading(0.0f);
    northTime = 0.5f;
}

// --- Unit panel --------------------------------------------------------------------------------

void HudUnitPanel::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: PressRegion(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::MouseMove: HitRegion(event); break;
    case UiEventKind::Wheel: HitRegion(event); break;
    default: break;
    }
}

void HudUnitPanel::LeftUp(UiEvent& event) {
    const UiRegion* region = PressedRegion(event);
    if (region == slingButton) {
        slide.Toggle(ui);
        return;
    }
    if (const int part = RegionIndex(partButtons, region); part >= 0) {
        ui->PlayInterfaceSound(kSoundClick);
        bodyPart = part;
        ShowBodyPart();
    }
}

// --- Clock -------------------------------------------------------------------------------------

void HudClock::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: PressRegion(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::MouseMove: HitRegion(event); break;
    default: break;
    }
}

void HudClock::LeftUp(UiEvent& event) {
    const UiRegion* region = PressedRegion(event);
    if (region == pauseButton) {
        TogglePause();
    } else if (region == normalButton || region == fastButton) {
        // A network client too (Rebirth's MPSpeedControl; the original ignored the buttons there): the host's sets
        // the server's speed, everyone's game.
        if (g_clientWorld.paused) g_clientWorld.SendPause(0);
        SetSpeed(region == fastButton);
    } else if (region == objectivesButton) {
        OpenObjectives();
    }
}

void HudClock::TogglePause() {
    ui->PlayInterfaceSound(kSoundClock);
    const bool paused = g_clientWorld.paused;
    g_clientWorld.SendPause(paused ? 0 : 1);
}

void HudClock::SetSpeed(bool newFast) {
    fast = newFast;
    app::g_updatePeriod = newFast ? kFastPeriod : kNormalPeriod;
    ui->fastGame = newFast;
    ui->PlayInterfaceSound(kSoundClock);
}

void HudClock::OpenObjectives() {
    highlight = false;
    ui->PlayInterfaceSound(kSoundClick);
    auto window = std::make_unique<ObjectivesWindow>(ui);
    window->Setup(LocalPlayerZone(), nullptr, false, false);
    ui->PushWindow(std::move(window), WindowBackdrop::FirstWindow);
}

// --- Movement panel ----------------------------------------------------------------------------

void HudMovePanel::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: PressRegion(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::MouseMove: HitRegion(event); break;
    default: break;
    }
}

void HudMovePanel::LeftUp(UiEvent& event) {
    const UiRegion* region = PressedRegion(event);
    if (region == flagButton)
        ToggleFlag();
    else if (const int moveMode = RegionIndex(modeButtons, region); moveMode >= 0)
        SetMode(moveMode);
}

void HudMovePanel::SetMode(int newMode) {
    PlayerClient* view = g_clientWorld.LocalView();
    if (!view) return;
    if (newMode >= 0 && newMode < 4) {
        ui->PlayInterfaceSound(kSoundClick);
        for (Unit* unit : owner->party) {
            if (unit->flags & MapObjectFlag::Selected) view->SetMoveMode(unit, newMode);
        }
    }
    ShowMode(newMode);
}

void HudMovePanel::ShowMode(int newMode) {
    screen_layout::EnterLeftPanel();    // addon's handler; another at the end
    mode = newMode;
    marker->visible = newMode >= 0;
    for (int i = 0; i < 4; ++i) {
        UiMesh* button = modeIcons[i];
        if (mode != i) {
            SetTint(button, kTintDim);
            continue;
        }
        // The marker points at the mode's button: turned about -z and put on its circle.
        const float angle = static_cast<float>(2 * i + 1) * kPi * kSixteenth;
        const Vector3 axis = {0.0f, 0.0f, -1.0f};
        marker->localRotation = Quaternion::AxisAngle(axis, angle);
        const float y = kMarkerCentreY - (lib::Sin(angle) * kMarkerRadius);
        const float x = lib::Cos(angle) * kMarkerRadius;
        marker->Place(x, y, kMarkerDepth);
        SetTint(button, kTintOn);
    }
    screen_layout::LeavePanel();
}

void HudMovePanel::Update() {
    if (!g_clientWorld.LocalView()) return;
    // The common value over the selected units: -1 none selected, -2 mixed.
    int newMode = -1;
    int newFlag = -1;
    int count = lib::Count(owner->party);
    Unit** units = owner->party.data();
    for (int i = 0; i < count; ++i) {
        Unit* unit = units[i];
        if (!(unit->flags & MapObjectFlag::Selected)) continue;
        if (newMode == -1)
            newMode = static_cast<int>(unit->moveMode2);
        else if (newMode != static_cast<int>(unit->moveMode2))
            newMode = -2;
        if (newFlag == -1)
            newFlag = unit->fightAtWill;
        else if (newFlag != unit->fightAtWill)
            newFlag = -2;
    }
    ShowMode(newMode);
    flag = newFlag;
    std::uint32_t bits = newFlag == 0 ? 0x3EAC0000u : newFlag == 1 ? 0u : 0x3EF60000u;
    flagIcon->textureOffsetU = std::bit_cast<float>(bits);
}

// --- Top panel ---------------------------------------------------------------------------------

void HudTopPanel::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: PressRegion(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::MouseMove: HitRegion(event); break;
    case UiEventKind::Wheel: HitRegion(event); break;
    default: break;
    }
}

void HudTopPanel::LeftUp(UiEvent& event) {
    const UiRegion* region = PressedRegion(event);
    if (region == slingButton) {
        slide.Toggle(ui);
    } else if (const int newPage = RegionIndex(pageButtons, region); newPage >= 0) {
        page = newPage;
        ShowPage();
        ui->PlayInterfaceSound(kSoundClick);
    }
}

// --- BattleScreen::SelectUnit --------------------------------------------------------

void BattleScreen::SelectUnit(Unit* unit, Point mouse) {
    const bool ctrl = ui->doubleClick;
    auto unitDead = [](Unit* u) { return u->IsDead(); };
    auto pick = [mouse]() { return g_clientWorld.PickSurface(mouse.x, mouse.y); };
    auto unitPosition = [](Unit* u) { return u->localPosition; };
    auto objectAt = [mouse]() { return g_clientWorld.Pick(mouse, HitTest::Parts); };
    // The portrait of member `index` nods after a short random delay.
    auto flash = [this](int index) { portraits->Wobble(index, kWobbleNod); };
    auto order = [this, ctrl](Unit* first, MapObject* target, int kind, int value) {
        g_clientWorld.LocalView()->SendUseOrder(first, target, kind, value, ctrl);
    };

    Unit* living = (unit && !unitDead(unit)) ? unit : nullptr;
    int count = lib::Count(party);
    int firstIndex = -1;
    Unit* first = nullptr;
    int selected = 0;
    for (int i = 0; i < count; ++i) {
        Unit* member = party[i];
        if (!member || !(member->flags & MapObjectFlag::Selected)) continue;
        if (!first) {
            first = member;
            firstIndex = i;
        }
        ++selected;
    }

    if (count <= 0 || selected == 0) {
        // Nobody selected: a click on a portrait's unit selects it (Ctrl: the camera follows it).
        if (count <= 0) return;
        for (int i = 0; i < std::ssize(party); ++i) {
            if (unit != party[i]) continue;
            ui->PlayInterfaceSound("buttons\\battle\\on_off.wav");
            if (ui->doubleClick) {
                if (i < std::ssize(party)) g_camera.SetTarget(party[i]);
            } else {
                ToggleMember(i);
            }
        }
        return;
    }

    if (ui->altDown) {
        OrderAt(unit ? unitPosition(unit) : pick(), false, ctrl);
        return;
    }
    bool anyFlag = false;
    for (int i = 0; i < 6 && !anyFlag; ++i) anyFlag = orderKeys[i] != 0;
    if (anyFlag || ui->ctrlDown) {
        if (unit && !unitDead(unit)) {
            AttackUnit(unit, ctrl);
            return;
        }
        OrderAt(pick(), true, ctrl);
        return;
    }

    switch (pendingAction) {
    case kPendingNone:
        if (unit) {
            if (unitDead(unit)) {
                OrderDeadUnit(unit, ctrl);
                return;
            }
            int members = lib::Count(party);
            for (int i = 0; i < members; ++i) {
                if (unit != party[i]) continue;
                ui->PlayInterfaceSound("buttons\\battle\\on_off.wav");
                if (ui->doubleClick) {
                    if (i < std::ssize(party)) g_camera.SetTarget(party[i]);
                } else {
                    ToggleMember(i);
                }
                return;
            }
            if (members != 0 && party[0] && party[0]->IsHostileTo(unit)) {
                AttackUnit(unit, ctrl);
                return;
            }
            OrderAt(unitPosition(unit), false, ctrl);
            return;
        }
        if (MapObject* object = objectAt()) {
            if (object->IsUsable() != 0 && object->IsDirectUseBlocked() == 0 && selected == 1) {
                order(first, object, 0, -1);
                flash(firstIndex);
                return;
            }
        }
        {
            Vector3 at = pick();
            OrderAt(at, false, ctrl);
            // "Terrain Coords x y z" on the console: each float through the inline operator<<
            // (x_floatused set, printed as a double), then '\n' and flush.
            console::EndLine(console::g_graphicsLogOut << "Terrain Coords " << at.x << " " << at.y
                                                      << " " << at.z);
        }
        return;
    case kPendingSpellOnUnit:
        if (!unit || unitDead(unit) || !first) return;
        // `quick` false (Rebirth's FixSpellSelfCasting; the original passed the Ctrl flag).
        UseOnUnit(unit, false, false);
        return;
    case kPendingSpellAtPoint:
    case kPendingQuickAtPoint: {
        Vector3 at = (unit && !unitDead(unit)) ? unitPosition(unit) : pick();
        if (IsZero(at.x) && IsZero(at.y)) return;
        if (!first) return;
        UseAtPoint(at, living, pendingAction == kPendingQuickAtPoint);
        return;
    }
    case kPendingQuickOnUnit: {
        if (!unit || unitDead(unit) || !first) return;
        Item* item = first->GetQuickItem(pendingSlot);
        if (item && item->GetCategory() == QuickItemCategory::Potion && unit != first) return;
        UseOnUnit(unit, true, false);
        return;
    }
    case kPendingItemOnObject:
    case kPendingKeyOnObject: {
        MapObject* target = unit ? unit : objectAt();
        if (!target || !first) return;
        if (target->IsUsable() == 0) return;
        if (pendingAction == kPendingKeyOnObject && target->AcceptsKey(&itemUseKey) == 0) return;
        order(first, target, 0, pendingItem);
        flash(firstIndex);
        SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    case kPendingFollow: {   // every selected member follows the unit
        if (!unit || unitDead(unit)) return;
        for (int i = 0; i < std::ssize(party); ++i) {
            Unit* member = party[i];
            if (!member || !(member->flags & MapObjectFlag::Selected)) continue;
            g_clientWorld.LocalView()->SendFollow(member, unit);
            Speech(party[i]->id, AckKind::Follow, nullptr);
            portraits->Wobble(i, kWobbleNod);
        }
        SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    case kPendingOrder: {   // a unit outside the party, or the object under the mouse
        MapObject* target = unit ? unit : objectAt();
        if (!target || !first) return;
        if (unit) {
            for (int i = 0; i < std::ssize(party); ++i)
                if (unit == party[i]) return;
            order(first, unit, 1, -1);
        } else {
            if (target->IsUsable() == 0) return;
            order(first, target, 0, -1);
        }
        flash(firstIndex);
        SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    default:
        return;
    }
}

// --- The quick item panel's button release -------------------------------------------

void HudQuickItems::LeftUp(UiEvent& event) {
    if (!ReleaseMouse()) return;
    const int slot = RegionIndex(regions, RegionUnder(event.at));
    const int entry = RegionItem(slot);
    if (entry < 0) return;
    event.handled = true;
    if (slot < 4 && entries[entry].own) {
        if (ui->doubleClick)
            UseNow(slot);
        else
            Pick(slot);
        return;
    }
    ui->PlayInterfaceSound(kSoundClick);
    if (selected != slot) {
        // One of the player's items (top row): used on an object, with the item's key in the screen's
        // itemUseKey and its id in pendingItem.
        owner->itemUseKey = entries[entry].item->GetUse();
        owner->SetPendingAction(kPendingKeyOnObject, slot, entries[entry].id);
        return;
    }
    // The highlighted one again: back to plain selection.
    owner->SetPendingAction(kPendingNone, -1, -1);
}

// --- The selected units ordered to a point -------------------------------------------

void BattleScreen::OrderAt(Vector3 point, bool goTo, bool run) {
    int selected = 0;
    for (int i = 0; i < std::ssize(party); ++i) {
        Unit* member = party[i];
        if (member && (member->flags & MapObjectFlag::Selected)) ++selected;
    }

    // A point on a zone exit that is open ("z.<zone>" not 1; "none" is no zone): remembered for the
    // update to ask about leaving (exitPending and the exit).
    exitPending = false;
    if (std::optional<int> exit = g_clientWorld.FindExitAt(point); exit && selected > 0) {
        std::string name = g_clientWorld.ExitTarget(*exit);
        if (text::CompareIgnoringCase(name,
                            "none") != 0) {
            name = std::string("z.") + name;
            float value = LocalPlayerVariable(name);
            const float one = 1.0f;
            // Equal skips.
            if (value != one) {
                exitPending = true;
                pendingExit = *exit;
            }
        }
    }

    // The selected members go; each one's portrait nods after a short random delay.
    std::vector<Unit*> units;
    for (int i = 0; i < std::ssize(party); ++i) {
        Unit* member = party[i];
        if (!member || !(member->flags & MapObjectFlag::Selected)) continue;
        units.push_back(member);
        portraits->Wobble(i, kWobbleNod);
    }
    if (!units.empty()) {
        PlayerClient* view = g_clientWorld.LocalView();
        if (goTo)
            view->SendGoTo(&units, point, run);
        else
            view->SendWalk(&units, point, run);
    }
}
