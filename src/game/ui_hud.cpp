// The battle screen's HUD panels: mouse handlers, per-frame updates and the small drawing
// wrappers. See ui.h ("Battle screen and its HUD panels") and docs/notes/ui.md.
#include "item.h"
#include "world_glue.h"
#include "texture.h"
#include "ui.h"


#include "camera.h"
#include "console.h"
#include "game_strings.h"
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
#include "ei/x87.h"
#include "lib/nan_audit.h"
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
#include "game_data.h"
#include "game_singletons.h"
#include "world_data.h"

namespace {
constexpr const char* kSoundClick = "buttons\\battle\\click.wav";
constexpr const char* kSoundSling = "buttons\\battle\\sling.wav";
constexpr const char* kSoundWeapon = "buttons\\battle\\weapon.wav";

// Scene objects of the panels (sprites, CUiMesh) are tinted through their three tint_ floats (the
// original stores the constants as dwords).
void SetTint(CEngineObject* object, std::uint32_t bits) {
    float* tint = AsUiMesh(object)->tint_;
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

void DrawWithSpecular(CUiWindow* window, CSceneRenderer* pass) {
    render::g_renderState.specular = true;
    window->DrawBase(pass);
    render::g_renderState.specular = false;
}

constexpr int kWobbleNod = 1;   // HudPortraitSlot::wobbleKind_

// Whether `x` is zero (either sign), as the FPU compare gives it.
bool IsZero(float value) {
    EI_NAN_AUDIT(value);
    return value == 0.0f;
}

} // namespace

// --- Weapon bar --------------------------------------------------------------------------------

void CHudWeaponBar::LeftDown(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region < 0 || region >= count_) return;
    handled_ = true;
    CaptureMouse();
}

void CHudWeaponBar::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region < 0 || region >= count_) return;
    handled_ = true;
    int previous = selected_;
    if (previous == region) return;
    if (previous >= 0 && previous < count_) Highlight(previous, false);
    Highlight(region, true);
    selected_ = region;
    interface_->PlaySound(kSoundWeapon);
    // No check for a missing local view here (like the original).
    if (CUnit* unit = unit_) g_clientWorld.LocalView()->SetWeapon(unit, region);
}

void CHudWeaponBar::MouseMove(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region >= 0 && region < count_) handled_ = true;
}

void CHudWeaponBar::Update() {
    CUnit* unit = unit_;
    if (!unit || selected_ == unit->pendingWeapon) return;
    Highlight(selected_, false);
    selected_ = unit_->pendingWeapon;
    Highlight(selected_, true);
}

void CHudWeaponBar::Draw(CSceneRenderer* pass) {
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

void CHudQuickItems::Draw(CSceneRenderer* pass) { DrawWithSpecular(this, pass); }
void CHudSpellColumn::Draw(CSceneRenderer* pass) { DrawWithSpecular(this, pass); }
void CHudActionColumn::Draw(CSceneRenderer* pass) { DrawWithSpecular(this, pass); }

// --- Quick items -------------------------------------------------------------------------------

void CHudQuickItems::LeftDown(int, int x, int y) {
    if (RegionItem(RegionAt(x, y)) < 0) return;
    handled_ = true;
    CaptureMouse();
}

void CHudQuickItems::LeftDouble(int, int x, int y) {
    if (RegionItem(RegionAt(x, y)) < 0) return;
    handled_ = true;
    CaptureMouse();
}

void CHudQuickItems::MouseMove(int, int x, int y) {
    if (RegionItem(RegionAt(x, y)) >= 0) handled_ = true;
}

void CHudQuickItems::Update() {
    if (!unit_) return;
    for (unsigned i = 0; i < 4; ++i) {
        if (!unit_->GetQuickItem(i)) continue;
        if (!regions_[i]->HasActiveTip()) continue;
        regions_[i]->SetTipText(ItemTip(i));
    }
}

// --- Spell column ------------------------------------------------------------------------------

void CHudSpellColumn::LeftDown(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region < 0 || region >= count_) return;
    handled_ = true;
    CaptureMouse();
}

void CHudSpellColumn::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region < 0 || region >= count_) return;
    handled_ = true;
    if (interface_->doubleClick_)
        ClickAlternate(region);
    else
        Click(region);
}

void CHudSpellColumn::MouseMove(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region >= 0 && region < count_) handled_ = true;
}

// --- Action column -----------------------------------------------------------------------------

void CHudActionColumn::LeftDown(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region < 0 || region >= count_) return;
    handled_ = true;
    CaptureMouse();
}

void CHudActionColumn::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region < 0 || region >= count_) return;
    handled_ = true;
    Select(region);
}

void CHudActionColumn::MouseMove(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region >= 0 && region < count_) handled_ = true;
}

// --- Portraits ---------------------------------------------------------------------------------

// The portraits read the portrait mode of the interface's active screen (the battle screen while they get
// input).

void CHudPortraits::LeftDown(int, int x, int y) {
    int region = RegionAt(x, y);
    const int mode = interface_->ActiveWindowAs<CBattleScreen>()->portraitMode_;
    if (mode == 0) {
        if (region < 0) return;
        handled_ = true;
        if (region < std::ssize(owner_->party_)) CaptureMouse();
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
        // The session's player records: id_ the player's unit, serial_ its id.
        for (auto node = net::g_gameClient->players_.begin(); node != net::g_gameClient->players_.end(); ++node) {
            auto* player = node->Get();
            if (player->id_ == unitId) {
                playerId = player->serial_;
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
    interface_->ActiveWindowAs<CBattleScreen>()->portraitMode_ = 0;
}

void CHudPortraits::LeftUp(int, int x, int y) {
    if (interface_->ActiveWindowAs<CBattleScreen>()->portraitMode_ != 0) return;
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region < 0) return;
    handled_ = true;
    CBattleScreen* owner = owner_;
    // Rebirth's FixSpellSelfCasting patches SelectUnit.
    if (region < std::ssize(owner->party_)) owner->SelectUnit(owner->party_[region], 0, 0);
}

void CHudPortraits::MouseMove(int, int x, int y) {
    int region = RegionAt(x, y);
    if (interface_->ActiveWindowAs<CBattleScreen>()->portraitMode_ == 0) {
        if (region < 0) return;
        handled_ = true;
        CBattleScreen* owner = owner_;
        if (region < std::ssize(owner->party_)) owner->HoverUnit(owner->party_[region], 0, 0);
        return;
    }
    if (region >= 0 && region < count_) g_cursor.SetShape(0);
}

// --- Map ---------------------------------------------------------------------------------------

void CHudMap::LeftDown(int, int x, int y) {
    int region = RegionAt(x, y);
    if (region >= 0) {
        handled_ = true;
        CaptureMouse();
    }
    if (region != 3 && region != 4) return;
    interface_->PlaySound(kSoundClick);
    view_ = region == 3 ? 1 : 2;
    // Scene objects 6 and 7 are the buttons of views 1 and 2.
    for (int i = 0; i < 2; ++i)
        SetTint(sceneObjects_[6 + i], view_ == i + 1 ? kTintOn : kTintOff);
}

void CHudMap::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    if (view_ != 0) {
        // A view button was held: the zoom stops and both buttons go dim.
        view_ = 0;
        for (int i = 0; i < 2; ++i) SetTint(sceneObjects_[6 + i], view_ == i + 1 ? kTintOn : kTintOff);
        return;
    }
    const int region = RegionAt(x, y);
    switch (region) {
    case 0:     // the sling button
        interface_->PlaySound(kSoundSling);
        slideDirection_ = -slideDirection_;
        sliding_ = true;
        handled_ = true;
        return;
    case 1:     // the round map (zoomed)
    case 2: {   // the map area
        // The world point under the click.
        POINT point = {x, y};
        interface_->ScalePointFromScreenEntry(&point);
        // WideScreen: the centre moved right by the shift.
        float right = static_cast<float>(point.x - (kMapCentreX + widescreen::EdgeShift())) / halfSide_;
        float down = static_cast<float>(point.y - kMapCentreY) / halfSide_;
        if (region == 1) {
            right /= zoom_;
            down /= zoom_;
        }
        const float worldX = static_cast<float>(right / scale_ + centreX_);
        const float worldY = static_cast<float>(centreY_ - down / scale_);
        if (worldX > 0.0f && worldX < worldWidth_ && worldY > 0.0f && worldY < worldHeight_) {
            // The camera stops following its unit and looks there.
            CCamera* view = &g_camera;
            view->flags_ &= ~kCameraFollows;
            view->SetTargetId(-1);
            Vector3 focus = view->focus_;
            focus.x = worldX;
            focus.y = worldY;
            view->SetFocus(focus);
        }
        handled_ = true;
        return;
    }
    case 5:     // "camera north"
        interface_->PlaySound(kSoundClick);
        g_camera.SetHeading(0.0f);
        northTime_ = 0.5f;
        handled_ = true;
        return;
    default:
        if (region >= 0) handled_ = true;
        return;
    }
}

void CHudMap::MouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) handled_ = true;
}

// --- Unit panel --------------------------------------------------------------------------------

void CHudUnitPanel::LeftDown(int, int x, int y) {
    if (RegionAt(x, y) < 0) return;
    handled_ = true;
    CaptureMouse();
}

void CHudUnitPanel::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region == 0) {
        interface_->PlaySound(kSoundSling);
        sliding_ = true;
        slideDirection_ = -slideDirection_;
        handled_ = true;
        return;
    }
    if (region > 0 && region <= 4) {
        interface_->PlaySound(kSoundClick);
        bodyPart_ = region - 1;
        // Scene objects 3-6 are the body parts.
        for (int i = 0; i < 4; ++i) SetTint(sceneObjects_[3 + i], bodyPart_ == i ? kTintOn : kTintOff);
    }
    if (region >= 0) handled_ = true;
}

void CHudUnitPanel::MouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) handled_ = true;
}

void CHudUnitPanel::Wheel(int, int, int x, int y) {
    if (RegionAt(x, y) >= 0) handled_ = true;
}

// --- Clock -------------------------------------------------------------------------------------

void CHudClock::LeftDown(int, int x, int y) {
    if (RegionAt(x, y) < 0) return;
    handled_ = true;
    CaptureMouse();
}

void CHudClock::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    const int region = RegionAt(x, y);
    switch (region) {
    case 0: {   // the pause button
        handled_ = true;
        TogglePause();
        return;
    }
    case 1:     // the normal speed
    case 2:     // the fast speed
        handled_ = true;
        // Rebirth's MPSpeedControl (the JNEs aimed at the next instruction).
        if (IsMultiplayerClient() && !ei::rebirth::Enabled(ei::rebirth::Patch::MPSpeedControl)) return;
        if (g_clientWorld.paused_) GameSendFlag10(0);
        SetSpeed(region == 2);
        return;
    case 3:     // the objectives
        handled_ = true;
        OpenObjectives();
        return;
    default:
        return;
    }
}

void CHudClock::MouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) handled_ = true;
}

void CHudClock::TogglePause() {
    interface_->PlaySound(kSoundClock);
    const bool paused = g_clientWorld.paused_;
    GameSendFlag10(paused ? 0 : 1);
}

void CHudClock::SetSpeed(bool fast) {
    fast_ = fast;
    worlddata::g_updatePeriod = fast ? kFastPeriod : kNormalPeriod;
    interface_->fastGame_ = fast;
    interface_->PlaySound(kSoundClock);
}

void CHudClock::OpenObjectives() {
    highlight_ = false;
    interface_->PlaySound(kSoundClick);
    auto* window = new CObjectivesWindow(interface_);
    window->Setup(LocalPlayerZone(), nullptr, false, false);
    interface_->PushWindow(window, true, true);
}

// --- Movement panel ----------------------------------------------------------------------------

void CHudMovePanel::LeftDown(int, int x, int y) {
    if (RegionAt(x, y) < 0) return;
    handled_ = true;
    CaptureMouse();
}

void CHudMovePanel::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    if (region == -1) return;
    handled_ = true;
    if (region == 0) {
        ToggleFlag();
        return;
    }
    SetMode(region - 1);
}

void CHudMovePanel::SetMode(int mode) {
    CPlayerClient* view = g_clientWorld.LocalView();
    if (!view) return;
    if (mode >= 0 && mode < 4) {
        interface_->PlaySound(kSoundClick);
        for (CUnit* unit : owner_->party_) {
            if (unit->flags_ & MapObjectFlag::Selected) view->SetMoveMode(unit, mode);
        }
    }
    ShowMode(mode);
}

void CHudMovePanel::MouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) handled_ = true;
}

void CHudMovePanel::ShowMode(int mode) {
    widescreen::EnterLeftPanel();    // addon's handler; another at the end
    mode_ = mode;
    auto* marker = SceneMesh(0);
    marker->visible_ = mode >= 0;
    for (int i = 0; i < 4; ++i) {
        CEngineObject* button = sceneObjects_[1 + i];
        if (mode_ != i) {
            SetTint(button, kTintDim);
            continue;
        }
        // The marker points at the mode's button: turned about -z and put on its circle.
        const float angle = static_cast<float>(static_cast<float>(2 * i + 1) * kPi * kSixteenth);
        const Vector3 axis = {0.0f, 0.0f, -1.0f};
        Quaternion turn;
        marker->localRotation_ = *turn.SetAxisAngle(axis, angle);
        const float y = static_cast<float>(kMarkerCentreY - x87::fsin(angle) * kMarkerRadius);
        const float x = static_cast<float>(x87::fcos(angle) * kMarkerRadius);
        SceneMesh(0)->Place(x, y, kMarkerDepth);
        SetTint(button, kTintOn);
    }
    widescreen::LeavePanel();
}

void CHudMovePanel::Update() {
    if (!g_clientWorld.LocalView()) return;
    // The common value over the selected units: -1 none selected, -2 mixed.
    int mode = -1;
    int flag = -1;
    int count = std::ssize(owner_->party_);
    CUnit** units = owner_->party_.data();
    for (int i = 0; i < count; ++i) {
        CUnit* unit = units[i];
        if (!(unit->flags_ & MapObjectFlag::Selected)) continue;
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
    // Scene object 5 shows the flag through its texture offset (textureOffsetU_).
    std::uint32_t bits = flag == 0 ? 0x3EAC0000u : flag == 1 ? 0u : 0x3EF60000u;
    SceneMesh(5)->textureOffsetU_ = std::bit_cast<float>(bits);
}

// --- Top panel ---------------------------------------------------------------------------------

void CHudTopPanel::LeftDown(int, int x, int y) {
    if (RegionAt(x, y) < 0) return;
    handled_ = true;
    CaptureMouse();
}

void CHudTopPanel::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    switch (region) {
    case 0:
        interface_->PlaySound(kSoundSling);
        sliding_ = true;
        slideDirection_ = -slideDirection_;
        handled_ = true;
        return;
    case 1:
    case 2:
        page_ = region - 1;
        ShowPage();
        interface_->PlaySound(kSoundClick);
        handled_ = true;
        return;
    default:
        if (region >= 0) handled_ = true;
        return;
    }
}

void CHudTopPanel::MouseMove(int, int x, int y) {
    if (RegionAt(x, y) >= 0) handled_ = true;
}

void CHudTopPanel::Wheel(int, int, int x, int y) {
    if (RegionAt(x, y) >= 0) handled_ = true;
}

// --- CBattleScreen::SelectUnit --------------------------------------------------------

void CBattleScreen::SelectUnit(CUnit* unit, int x, int y) {
    const bool ctrl = interface_->doubleClick_;
    auto unitDead = [](CUnit* u) { return u->IsDead(); };
    auto pick = [x, y]() {
        Vector3 out;
        Vector3* p = g_clientWorld.PickSurface(&out, x, y);
        return Vector3{p->x, p->y, p->z};
    };
    auto unitPosition = [](CUnit* u) { return u->localPosition_; };
    auto objectAt = [x, y]() {
        POINT point = {x, y};
        return static_cast<CUnit*>(g_clientWorld.PickAt2(&point));
    };
    // The portrait of member `index` nods after a short random delay.
    auto flash = [this](int index) { portraits_->Wobble(index, kWobbleNod); };
    auto order = [this, ctrl](CUnit* first, CUnit* target, int kind, int value) {
        g_clientWorld.LocalView()->SendUseOrder(first, target, kind, value, ctrl);
    };

    CUnit* living = (unit && !unitDead(unit)) ? unit : nullptr;
    int count = std::ssize(party_);
    int firstIndex = -1;
    CUnit* first = nullptr;
    int selected = 0;
    for (int i = 0; i < count; ++i) {
        CUnit* member = party_[i];
        if (!member || !(member->flags_ & MapObjectFlag::Selected)) continue;
        if (!first) {
            first = member;
            firstIndex = i;
        }
        ++selected;
    }

    if (count <= 0 || selected == 0) {
        // Nobody selected: a click on a portrait's unit selects it (Ctrl: the camera follows it).
        if (count <= 0) return;
        for (int i = 0; i < std::ssize(party_); ++i) {
            if (unit != party_[i]) continue;
            interface_->PlaySound(game::SoundOnOffText());
            if (interface_->doubleClick_) {
                if (i < std::ssize(party_)) g_camera.SetTarget(party_[i]);
            } else {
                ToggleMember(i);
            }
        }
        return;
    }

    if (interface_->altDown_) {
        OrderAt(unit ? unitPosition(unit) : pick(), false, ctrl);
        return;
    }
    bool anyFlag = false;
    for (int i = 0; i < 6 && !anyFlag; ++i) anyFlag = orderKeys_[i] != 0;
    if (anyFlag || interface_->ctrlDown_) {
        if (unit && !unitDead(unit)) {
            AttackUnit(unit, ctrl);
            return;
        }
        OrderAt(pick(), true, ctrl);
        return;
    }

    switch (pendingAction_) {
    case kPendingNone:
        if (unit) {
            if (unitDead(unit)) {
                OrderDeadUnit(unit, ctrl);
                return;
            }
            int members = std::ssize(party_);
            for (int i = 0; i < members; ++i) {
                if (unit != party_[i]) continue;
                interface_->PlaySound(game::SoundOnOffText());
                if (interface_->doubleClick_) {
                    if (i < std::ssize(party_)) g_camera.SetTarget(party_[i]);
                } else {
                    ToggleMember(i);
                }
                return;
            }
            if (members != 0 && party_[0] && party_[0]->IsHostileTo(unit)) {
                AttackUnit(unit, ctrl);
                return;
            }
            OrderAt(unitPosition(unit), false, ctrl);
            return;
        }
        if (CUnit* object = objectAt()) {
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
        UseAtPoint(at, living, pendingAction_ == kPendingQuickAtPoint);
        return;
    }
    case kPendingQuickOnUnit: {
        if (!unit || unitDead(unit) || !first) return;
        CItem* item = first->GetQuickItem(pendingSlot_);
        if (item && item->GetCategory() == QuickItemCategory::Potion && unit != first) return;
        UseOnUnit(unit, true, false);
        return;
    }
    case kPendingItemOnObject:
    case kPendingKeyOnObject: {
        CUnit* target = unit ? unit : objectAt();
        if (!target || !first) return;
        if (target->IsUsable() == 0) return;
        if (pendingAction_ == kPendingKeyOnObject && target->AcceptsKey(&itemUseKey_) == 0) return;
        order(first, target, 0, pendingItem_);
        flash(firstIndex);
        SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    case kPendingFollow: {   // every selected member follows the unit
        if (!unit || unitDead(unit)) return;
        for (int i = 0; i < std::ssize(party_); ++i) {
            CUnit* member = party_[i];
            if (!member || !(member->flags_ & MapObjectFlag::Selected)) continue;
            g_clientWorld.LocalView()->SendFollow(member, unit);
            Speech(party_[i]->id_, AckKind::Follow, nullptr);
            portraits_->Wobble(i, kWobbleNod);
        }
        SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    case kPendingOrder: {   // a unit outside the party, or the object under the mouse
        CUnit* target = unit ? unit : objectAt();
        if (!target || !first) return;
        if (unit) {
            for (int i = 0; i < std::ssize(party_); ++i)
                if (unit == party_[i]) return;
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

void CHudQuickItems::LeftUp(int, int x, int y) {
    if (!ReleaseMouse()) return;
    int region = RegionAt(x, y);
    // Unchecked, like the original (region -1 reads a neighbouring field).
    int entry = RegionItem(region);
    if (entry < 0) return;
    handled_ = true;
    if (region < 4 && entries_[entry].own) {
        if (interface_->doubleClick_)
            UseNow(region);
        else
            Pick(region);
        return;
    }
    interface_->PlaySound(kSoundClick);
    if (selected_ != region) {
        // One of the player's items (top row): used on an object, with the item's key in the screen's
        // itemUseKey_ and its id in pendingItem_.
        owner_->itemUseKey_ = entries_[entry].item->GetUse();
        owner_->SetPendingAction(kPendingKeyOnObject, region, entries_[entry].id);
        return;
    }
    // The highlighted one again: back to plain selection.
    owner_->SetPendingAction(kPendingNone, -1, -1);
}

// --- The selected units ordered to a point -------------------------------------------

void CBattleScreen::OrderAt(Vector3 point, bool goTo, bool run) {
    int selected = 0;
    for (int i = 0; i < std::ssize(party_); ++i) {
        CUnit* member = party_[i];
        if (member && (member->flags_ & MapObjectFlag::Selected)) ++selected;
    }

    // A point on a zone exit that is open ("z.<zone>" not 1; "none" is no zone): remembered for the
    // update to ask about leaving (exitPending_ and the exit).
    exitPending_ = false;
    int exit;
    if (GameFindExitAt(&point.x, &exit) && selected > 0) {
        std::string name = GameExitTarget(exit);
        if (text::CompareIgnoringCase(name,
                            game::NoneText()) != 0) {
            name = std::string(game::ZoneVariablePrefixText()) + name;   // "z."
            double value = LocalPlayerVariable(name);
            const float one = 1.0f;
            // Equal skips.
            EI_NAN_AUDIT(value);
            if (value != one) {
                exitPending_ = true;
                pendingExit_ = exit;
            }
        }
    }

    // The selected members go; each one's portrait nods after a short random delay.
    std::vector<CUnit*> units;
    for (int i = 0; i < std::ssize(party_); ++i) {
        CUnit* member = party_[i];
        if (!member || !(member->flags_ & MapObjectFlag::Selected)) continue;
        units.push_back(member);
        portraits_->Wobble(i, kWobbleNod);
    }
    if (!units.empty()) {
        CPlayerClient* view = g_clientWorld.LocalView();
        if (goTo)
            view->SendGoTo(&units, point, run);
        else
            view->SendWalk(&units, point, run);
    }
}
