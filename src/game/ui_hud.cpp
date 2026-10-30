// The battle screen's HUD panels: mouse handlers, per-frame updates and the small drawing
// wrappers. See ui.h ("Battle screen and its HUD panels") and docs/notes/ui.md.
#include "item.h"
#include "world_glue.h"
#include "texture.h"
#include "ui.h"


#include "camera.h"
#include "console.h"
#include "game_strings.h"
#include "lib/containers.h"
#include "lib/text_case.h"
#include "map_object.h"
#include "net_classes.h"
#include "net_messages.h"
#include "player_client.h"
#include "ui_briefing.h"
#include "ui_cursor.h"
#include "ui_screens.h"
#include "ui_widescreen.h"
#include "lib/vecmath.h"
#include "render/render_state.h"
#include "ei/rebirth.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include "unit.h"
#include "world.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>
#include "game_singletons.h"
#include "world_data.h"

namespace {
constexpr const char* kSoundClick = "buttons\\battle\\click.wav";
constexpr const char* kSoundSling = "buttons\\battle\\sling.wav";
constexpr const char* kSoundWeapon = "buttons\\battle\\weapon.wav";

// Scene objects of the panels (sprites, UiMesh) are tinted through their three tint floats (the
// original stores the constants as dwords).
void SetTint(EngineObject* object, std::uint32_t bits) {
    float* tint = AsUiMesh(object)->tint;
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

// Whether `x` is zero (either sign), as the FPU compare gives it.
bool IsZero(float value) {
    EI_NAN_AUDIT(value);
    return value == 0.0f;
}

} // namespace

// --- Weapon bar --------------------------------------------------------------------------------

void HudWeaponBar::LeftDown(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region < 0 || region >= count) return;
    handled = true;
    CaptureMouse();
}

void HudWeaponBar::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region < 0 || region >= count) return;
    handled = true;
    int previous = selected;
    if (previous == region) return;
    if (previous >= 0 && previous < count) Highlight(previous, false);
    Highlight(region, true);
    selected = region;
    ui->PlayInterfaceSound(kSoundWeapon);
    // No check for a missing local view here (like the original).
    if (Unit* currentUnit = unit) g_clientWorld.LocalView()->SetWeapon(currentUnit, region);
}

void HudWeaponBar::MouseMove(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region >= 0 && region < count) handled = true;
}

void HudWeaponBar::Update() {
    Unit* currentUnit = unit;
    if (!currentUnit || selected == currentUnit->pendingWeapon) return;
    Highlight(selected, false);
    selected = unit->pendingWeapon;
    Highlight(selected, true);
}

void HudWeaponBar::Draw(SceneRenderer* pass) {
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

void HudQuickItems::Draw(SceneRenderer* pass) { DrawWithSpecular(this, pass); }
void HudSpellColumn::Draw(SceneRenderer* pass) { DrawWithSpecular(this, pass); }
void HudActionColumn::Draw(SceneRenderer* pass) { DrawWithSpecular(this, pass); }

// --- Quick items -------------------------------------------------------------------------------

void HudQuickItems::LeftDown(int, int x, int y) {
    if (RegionItem(RegionAt(x, y)) < 0) return;
    handled = true;
    CaptureMouse();
}

void HudQuickItems::LeftDouble(int, int x, int y) {
    if (RegionItem(RegionAt(x, y)) < 0) return;
    handled = true;
    CaptureMouse();
}

void HudQuickItems::MouseMove(int, int x, int y) {
    if (RegionItem(RegionAt(x, y)) >= 0) handled = true;
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

void HudSpellColumn::LeftDown(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region < 0 || region >= count) return;
    handled = true;
    CaptureMouse();
}

void HudSpellColumn::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region < 0 || region >= count) return;
    handled = true;
    if (ui->doubleClick)
        ClickAlternate(region);
    else
        Click(region);
}

void HudSpellColumn::MouseMove(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region >= 0 && region < count) handled = true;
}

// --- Action column -----------------------------------------------------------------------------

void HudActionColumn::LeftDown(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region < 0 || region >= count) return;
    handled = true;
    CaptureMouse();
}

void HudActionColumn::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region < 0 || region >= count) return;
    handled = true;
    Select(region);
}

void HudActionColumn::MouseMove(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region >= 0 && region < count) handled = true;
}

// --- Portraits ---------------------------------------------------------------------------------

// The portraits read the portrait mode of the interface's active screen (the battle screen while they get
// input).

void HudPortraits::LeftDown(int, int x, int y) {
    int region = RegionAt(x, y);
    const int mode = ui->ActiveWindowAs<BattleScreen>()->portraitMode;
    if (mode == 0) {
        if (region < 0) return;
        handled = true;
        if (region < std::ssize(owner->party)) CaptureMouse();
        return;
    }
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
    // ClientSession::SendCommand: the server commands 4 and 5.
    if (mode == 1)
        net::g_gameClient->SendCommand(4, text);
    else if (mode == 2)
        net::g_gameClient->SendCommand(5, text);
    ui->ActiveWindowAs<BattleScreen>()->portraitMode = 0;
}

void HudPortraits::LeftUp(int, int x, int y) {
    if (ui->ActiveWindowAs<BattleScreen>()->portraitMode != 0) return;
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region < 0) return;
    handled = true;
    BattleScreen* currentOwner = owner;
    // Rebirth's FixSpellSelfCasting patches SelectUnit.
    if (region < std::ssize(currentOwner->party)) currentOwner->SelectUnit(currentOwner->party[region], 0, 0);
}

void HudPortraits::MouseMove(int, int x, int y) {
    int region = RegionAt(x, y);
    if (ui->ActiveWindowAs<BattleScreen>()->portraitMode == 0) {
        if (region < 0) return;
        handled = true;
        BattleScreen* currentOwner = owner;
        if (region < std::ssize(currentOwner->party)) currentOwner->HoverUnit(currentOwner->party[region], 0, 0);
        return;
    }
    if (region >= 0 && region < count) g_cursor.SetShape(0);
}

// --- Map ---------------------------------------------------------------------------------------

void HudMap::LeftDown(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region >= 0) {
        handled = true;
        CaptureMouse();
    }
    if (region != 3 && region != 4) return;
    ui->PlayInterfaceSound(kSoundClick);
    view = region == 3 ? 1 : 2;
    // Scene objects 6 and 7 are the buttons of views 1 and 2.
    for (int i = 0; i < 2; ++i)
        SetTint(sceneObjects[6 + i], view == i + 1 ? kTintOn : kTintOff);
}

void HudMap::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    if (view != 0) {
        // A view button was held: the zoom stops and both buttons go dim.
        view = 0;
        for (int i = 0; i < 2; ++i) SetTint(sceneObjects[6 + i], view == i + 1 ? kTintOn : kTintOff);
        return;
    }
    const int region = RegionAt(x, y);
    switch (region) {
    case 0:     // the sling button
        ui->PlayInterfaceSound(kSoundSling);
        slideDirection = -slideDirection;
        sliding = true;
        handled = true;
        return;
    case 1:     // the round map (zoomed)
    case 2: {   // the map area
        // The world point under the click.
        Point point = {x, y};
        ui->ScalePointFromScreenEntry(&point);
        // WideScreen: the centre moved right by the shift.
        float right = static_cast<float>(point.x - (kMapCentreX + widescreen::EdgeShift())) / halfSide;
        float down = static_cast<float>(point.y - kMapCentreY) / halfSide;
        if (region == 1) {
            right /= zoom;
            down /= zoom;
        }
        const float worldX = static_cast<float>(right / scale + centreX);
        const float worldY = static_cast<float>(centreY - down / scale);
        if (worldX > 0.0f && worldX < worldWidth && worldY > 0.0f && worldY < worldHeight) {
            // The camera stops following its unit and looks there.
            Camera* camera = &g_camera;
            camera->flags &= ~kCameraFollows;
            camera->SetTargetId(-1);
            Vector3 focus = camera->focus;
            focus.x = worldX;
            focus.y = worldY;
            camera->SetFocus(focus);
        }
        handled = true;
        return;
    }
    case 5:     // "camera north"
        ui->PlayInterfaceSound(kSoundClick);
        g_camera.SetHeading(0.0f);
        northTime = 0.5f;
        handled = true;
        return;
    default:
        if (region >= 0) handled = true;
        return;
    }
}

void HudMap::MouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) handled = true;
}

// --- Unit panel --------------------------------------------------------------------------------

void HudUnitPanel::LeftDown(int, int x, int y) {
    if (RegionAt(x, y) < 0) return;
    handled = true;
    CaptureMouse();
}

void HudUnitPanel::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region == 0) {
        ui->PlayInterfaceSound(kSoundSling);
        sliding = true;
        slideDirection = -slideDirection;
        handled = true;
        return;
    }
    if (region > 0 && region <= 4) {
        ui->PlayInterfaceSound(kSoundClick);
        bodyPart = region - 1;
        // Scene objects 3-6 are the body parts.
        for (int i = 0; i < 4; ++i) SetTint(sceneObjects[3 + i], bodyPart == i ? kTintOn : kTintOff);
    }
    if (region >= 0) handled = true;
}

void HudUnitPanel::MouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) handled = true;
}

void HudUnitPanel::Wheel(int, int, int x, int y) {
    if (RegionAt(x, y) >= 0) handled = true;
}

// --- Clock -------------------------------------------------------------------------------------

void HudClock::LeftDown(int, int x, int y) {
    if (RegionAt(x, y) < 0) return;
    handled = true;
    CaptureMouse();
}

void HudClock::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    const int region = RegionAt(x, y);
    switch (region) {
    case 0: {   // the pause button
        handled = true;
        TogglePause();
        return;
    }
    case 1:     // the normal speed
    case 2:     // the fast speed
        handled = true;
        // Rebirth's MPSpeedControl (the JNEs aimed at the next instruction).
        if (IsMultiplayerClient() && !ei::rebirth::Enabled(ei::rebirth::Patch::MPSpeedControl)) return;
        if (g_clientWorld.paused) GameSendFlag10(0);
        SetSpeed(region == 2);
        return;
    case 3:     // the objectives
        handled = true;
        OpenObjectives();
        return;
    default:
        return;
    }
}

void HudClock::MouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) handled = true;
}

void HudClock::TogglePause() {
    ui->PlayInterfaceSound(kSoundClock);
    const bool paused = g_clientWorld.paused;
    GameSendFlag10(paused ? 0 : 1);
}

void HudClock::SetSpeed(bool newFast) {
    fast = newFast;
    worlddata::g_updatePeriod = newFast ? kFastPeriod : kNormalPeriod;
    ui->fastGame = newFast;
    ui->PlayInterfaceSound(kSoundClock);
}

void HudClock::OpenObjectives() {
    highlight = false;
    ui->PlayInterfaceSound(kSoundClick);
    auto* window = new ObjectivesWindow(ui);
    window->Setup(LocalPlayerZone(), nullptr, false, false);
    ui->PushWindow(window, true, true);
}

// --- Movement panel ----------------------------------------------------------------------------

void HudMovePanel::LeftDown(int, int x, int y) {
    if (RegionAt(x, y) < 0) return;
    handled = true;
    CaptureMouse();
}

void HudMovePanel::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region == -1) return;
    handled = true;
    if (region == 0) {
        ToggleFlag();
        return;
    }
    SetMode(region - 1);
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

void HudMovePanel::MouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) handled = true;
}

void HudMovePanel::ShowMode(int newMode) {
    widescreen::EnterLeftPanel();    // addon's handler; another at the end
    mode = newMode;
    auto* marker = SceneMesh(0);
    marker->visible = newMode >= 0;
    for (int i = 0; i < 4; ++i) {
        EngineObject* button = sceneObjects[1 + i];
        if (mode != i) {
            SetTint(button, kTintDim);
            continue;
        }
        // The marker points at the mode's button: turned about -z and put on its circle.
        const float angle = static_cast<float>(2 * i + 1) * kPi * kSixteenth;
        const Vector3 axis = {0.0f, 0.0f, -1.0f};
        Quaternion turn;
        marker->localRotation = *turn.SetAxisAngle(axis, angle);
        const float y = kMarkerCentreY - pc24::Mul(pc24::Sin(angle), kMarkerRadius);
        const float x = pc24::Mul(pc24::Cos(angle), kMarkerRadius);
        SceneMesh(0)->Place(x, y, kMarkerDepth);
        SetTint(button, kTintOn);
    }
    widescreen::LeavePanel();
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
            newMode = unit->moveMode2;
        else if (newMode != unit->moveMode2)
            newMode = -2;
        if (newFlag == -1)
            newFlag = unit->fightAtWill;
        else if (newFlag != unit->fightAtWill)
            newFlag = -2;
    }
    ShowMode(newMode);
    flag = newFlag;
    // Scene object 5 shows the flag through its texture offset (textureOffsetU).
    std::uint32_t bits = newFlag == 0 ? 0x3EAC0000u : newFlag == 1 ? 0u : 0x3EF60000u;
    SceneMesh(5)->textureOffsetU = std::bit_cast<float>(bits);
}

// --- Top panel ---------------------------------------------------------------------------------

void HudTopPanel::LeftDown(int, int x, int y) {
    if (RegionAt(x, y) < 0) return;
    handled = true;
    CaptureMouse();
}

void HudTopPanel::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    switch (region) {
    case 0:
        ui->PlayInterfaceSound(kSoundSling);
        sliding = true;
        slideDirection = -slideDirection;
        handled = true;
        return;
    case 1:
    case 2:
        page = region - 1;
        ShowPage();
        ui->PlayInterfaceSound(kSoundClick);
        handled = true;
        return;
    default:
        if (region >= 0) handled = true;
        return;
    }
}

void HudTopPanel::MouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) handled = true;
}

void HudTopPanel::Wheel(int, int, int x, int y) {
    if (RegionAt(x, y) >= 0) handled = true;
}

// --- BattleScreen::SelectUnit --------------------------------------------------------

void BattleScreen::SelectUnit(Unit* unit, int x, int y) {
    const bool ctrl = ui->doubleClick;
    auto unitDead = [](Unit* u) { return u->IsDead(); };
    auto pick = [x, y]() {
        Vector3 out;
        Vector3* p = g_clientWorld.PickSurface(&out, x, y);
        return Vector3{p->x, p->y, p->z};
    };
    auto unitPosition = [](Unit* u) { return u->localPosition; };
    auto objectAt = [x, y]() {
        Point point = {x, y};
        return static_cast<Unit*>(g_clientWorld.PickAt2(&point));
    };
    // The portrait of member `index` nods after a short random delay.
    auto flash = [this](int index) { portraits->Wobble(index, kWobbleNod); };
    auto order = [this, ctrl](Unit* first, Unit* target, int kind, int value) {
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
            ui->PlayInterfaceSound(game::SoundOnOffText());
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
                ui->PlayInterfaceSound(game::SoundOnOffText());
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
        if (Unit* object = objectAt()) {
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
            console::EndLine(console::g_graphicsLogOut << game::TerrainCoordsText() << at.x << game::SpaceText() << at.y
                                                      << game::SpaceText() << at.z);
        }
        return;
    case kPendingSpellOnUnit:
        if (!unit || unitDead(unit) || !first) return;
        // The original passes the Ctrl flag as UseOnUnit's `quick`; Rebirth's FixSpellSelfCasting #1 (MOV CL,0)
        // passes false.
        UseOnUnit(unit, ei::rebirth::Enabled(ei::rebirth::Patch::FixSpellSelfCasting) ? false : ctrl, false);
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
        Unit* target = unit ? unit : objectAt();
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
        Unit* target = unit ? unit : objectAt();
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

void HudQuickItems::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    // Unchecked, like the original (region -1 reads a neighbouring field).
    int entry = RegionItem(region);
    if (entry < 0) return;
    handled = true;
    if (region < 4 && entries[entry].own) {
        if (ui->doubleClick)
            UseNow(region);
        else
            Pick(region);
        return;
    }
    ui->PlayInterfaceSound(kSoundClick);
    if (selected != region) {
        // One of the player's items (top row): used on an object, with the item's key in the screen's
        // itemUseKey and its id in pendingItem.
        owner->itemUseKey = entries[entry].item->GetUse();
        owner->SetPendingAction(kPendingKeyOnObject, region, entries[entry].id);
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
    int exit;
    if (GameFindExitAt(&point.x, &exit) && selected > 0) {
        std::string name = GameExitTarget(exit);
        if (text::CompareIgnoringCase(name,
                            game::NoneText()) != 0) {
            name = std::string(game::ZoneVariablePrefixText()) + name;   // "z."
            float value = LocalPlayerVariable(name);
            const float one = 1.0f;
            // Equal skips.
            EI_NAN_AUDIT(value);
            if (value != one) {
                exitPending = true;
                pendingExit = exit;
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
