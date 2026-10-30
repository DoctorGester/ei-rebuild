// The battle screen's input side (BattleScreen): the close, double click, key release and wheel, the key-down
// handler, the mouse, the mouse-over and the party commands they issue. See ui.h, ui_camp.h and docs/notes/ui.md
// ("Battle screen").
#include "ui.h"

#include "acks_db.h"
#include "base_manager.h"
#include "camera.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "item.h"
#include "maths.h"
#include "object_tables.h"
#include "options.h"
#include "party.h"
#include "player_client.h"
#include "text_manager.h"
#include "text_split.h"
#include "ui_camp.h"
#include "ui_cursor.h"
#include "ui_screens.h"
#include "unit.h"
#include "unit_client_models.h"
#include "unit_db.h"
#include "world.h"
#include "world_glue.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "platform/platform.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
// Key actions 0x11..0x16 hold the order keys; each releases its flag in orderKeys.
constexpr int kFirstOrderKeyAction = 0x11;
// Which orderKeys entry each of them releases.
constexpr int kOrderKeyOfAction[6] = {0, 1, 5, 4, 3, 2};
} // namespace

// --- the battle screen loses the top -------------------------------------------------------------

void BattleScreen::Deactivate() {
    for (std::uint8_t& key : orderKeys) key = 0;
    viewPressed = false;
    boxSelecting = false;
    UiContainer::Deactivate();
}

// --- a double click on the battle screen ---------------------------------------------------------

void BattleScreen::LeftDouble(int buttons, int x, int y) {
    if (portraitMode != 0) return;
    if (!viewPressed) {
        UiContainer::LeftDouble(buttons, x, y);
        if (handled) return;
    }
    viewPressed = true;
    pressX = x;
    pressY = y;
    boxSelecting = false;
}

// --- a key released on the battle screen ---------------------------------------------------------

void BattleScreen::KeyUp(int key, int repeat, int modifiers) {
    if (portraitMode != 0) return;
    UiContainer::KeyUp(key, repeat, modifiers);
    int action = g_keyActions[modifiers & 0x1FF] - kFirstOrderKeyAction;
    if (action >= 0 && action < 6) orderKeys[kOrderKeyOfAction[action]] = 0;
}

// --- the wheel on the battle screen --------------------------------------------------------------

void BattleScreen::Wheel(int modifiers, int delta, int x, int y) {
    if (portraitMode != 0) return;
    UiWindow* const panels[4] = {map, unitPanel, messageLog, topPanel};
    for (UiWindow* panel : panels) {
        panel->handled = false;
        panel->Input()->Wheel(modifiers, delta, x, y);
        if (panel->handled) return;
    }
    for (UiWindow* panel : panels) panel->SetEnabled(false, false);
    UiContainer::Wheel(modifiers, delta, x, y);
    for (UiWindow* panel : panels) panel->SetEnabled(true, false);
}

// --- Key down ----------------------------------------------------------------------------------------

// The battle screen's key-down handler (input side): the chat line's keys on a
// network client, the panels' own keys, Esc (the pending action dropped, else the escape menu) and
// the key actions of the key table (g_keyActions): pause and game speed, the objectives window, the
// camera, the order keys, movement modes, the action, spell, weapon and quick item buttons, the
// unit panel's body parts, the top panel's pages, the map's slide, quick save / load, the party
// members and the message window. Rebirth's MPSpeedControl (the two JNEs
// aimed at the next instruction) lets the speed keys work on a network client too. See ui_camp.h
// (BattleScreen) and docs/notes/ui.md, section 6.

namespace {

constexpr const char* kSoundClick = "buttons\\battle\\click.wav";
constexpr const char* kSoundWeapon = "buttons\\battle\\weapon.wav";
constexpr const char* kSoundSling = "buttons\\battle\\sling.wav";
constexpr const char* kSoundOnOff = "buttons\\battle\\on_off.wav";
constexpr const char* kQuickSaveName = "saveq";

constexpr int kKeyRepeated = 0x4000;            // the key was already down
constexpr int kScanCodeMask = 0x1FF;

// The key actions (the key table's values).
enum KeyAction {
    kActionPause = 1,
    kActionNormalSpeed = 2,
    kActionFastSpeed = 3,
    kActionObjectives = 4,
    kActionCameraToUnit = 0xB,
    kActionCameraNorth = 0xC,
    kActionOrderKey = 0x11,      // .. 0x16
    kActionMoveMode = 0x17,      // .. 0x1A: modes 0..3
    kActionMoveFlag = 0x1B,
    kActionFollow = 0x1C,
    kActionScience = 0x1D,
    kActionSpell = 0x1E,         // .. 0x25: spells 0..7
    kActionWeapon = 0x26,        // .. 0x29: weapons 0..3
    kActionQuickItem = 0x2A,     // .. 0x2D: quick items 0..3
    kActionBodyPart = 0x2E,      // .. 0x31: body parts 0..3
    kActionPage = 0x32,          // 0x33: the top panel's pages 0, 1
    kActionMapSlide = 0x34,
    kActionQuickSave = 0x35,
    kActionQuickLoad = 0x36,
    kActionMember = 0x37,        // .. 0x39: party members 0..2
    kActionSelectAll = 0x3A,
    kActionMessage = 0x3B,
};

// The action column's buttons the keys select (the original compares the names "follow" and "science"
// with "follow").
constexpr int kFollowButton = 0;
constexpr int kScienceButton = 1;

// A panel's slide key with a choice (the unit panel's body part, the top panel's page): a panel
// that is slid away comes back showing the choice; a new choice is shown without a sound; the
// choice already shown slides the panel away.
template <typename Panel, typename Show>
void SlideKey(Panel* panel, int& shown, int choice, Show show) {
    if (panel->slideDirection == 1.0f) {
        shown = choice;
        panel->slideDirection = -1.0f;
        panel->sliding = true;
    } else if (shown != choice) {
        shown = choice;
        show(panel);
        return;
    } else {
        panel->sliding = true;
        panel->slideDirection = 1.0f;
    }
    panel->ui->PlayInterfaceSound(kSoundSling);
    show(panel);
}

} // namespace

void BattleScreen::KeyDown(int key, int repeat, int modifiers) {
    if (portraitMode != 0) {
        if (key == platform::kKeyEscape) portraitMode = 0;
        return;
    }
    if (key == platform::kKeyReturn) {
        if (IsMultiplayerClient()) {
            ui->ChatKeyEnter();
            handled = true;
            return;
        }
    } else if (key == platform::kKeyBack) {
        if (IsMultiplayerClient()) {
            ui->ChatKeyBackspace();
            handled = true;
            return;
        }
    }
    UiContainer::KeyDown(key, repeat, modifiers);
    if (handled) return;
    if (modifiers & kKeyRepeated) return;

    if (key == platform::kKeyEscape) {
        if (pendingAction != kPendingNone) {
            // The pending action is dropped with every panel's highlight.
            SetPendingAction(kPendingNone, -1, -1);
            return;
        }
        ui->PushWindow(new EscMenu(ui), true, true);
        handled = true;
        return;
    }

    const int action = g_keyActions[modifiers & kScanCodeMask];
    Camera* camera = &g_camera;
    switch (action) {
    case kActionPause: {
        clock->TogglePause();
        handled = true;
        return;
    }
    case kActionNormalSpeed:
    case kActionFastSpeed: {
        if (g_clientWorld.paused) return;
        clock->SetSpeed(action == kActionFastSpeed);
        return;
    }
    case kActionObjectives:
        clock->OpenObjectives();
        handled = true;
        return;
    case kActionCameraToUnit:
        // The camera follows the first selected member, or nobody.
        for (int i = 0; i < std::ssize(party); ++i) {
            if (party[i]->flags & MapObjectFlag::Selected) {
                camera->SetTarget(party[i]);
                return;
            }
        }
        camera->SetTargetId(-1);
        return;
    case kActionCameraNorth:
        ui->PlayInterfaceSound(kSoundClick);
        camera->SetHeading(0.0f);
        map->northTime = 0.5f;
        return;
    case kActionOrderKey:
    case kActionOrderKey + 1:
    case kActionOrderKey + 2:
    case kActionOrderKey + 3:
    case kActionOrderKey + 4:
    case kActionOrderKey + 5:
        orderKeys[kOrderKeyOfAction[action - kActionOrderKey]] = 1;
        return;
    case kActionMoveMode:
    case kActionMoveMode + 1:
    case kActionMoveMode + 2:
    case kActionMoveMode + 3: {
        movePanel->SetMode(action - kActionMoveMode);
        handled = true;
        return;
    }
    case kActionMoveFlag:
        movePanel->ToggleFlag();
        handled = true;
        return;
    case kActionFollow:
        actionColumn->Select(kFollowButton);
        return;
    case kActionScience:
        actionColumn->Select(kScienceButton);
        return;
    case kActionSpell:
    case kActionSpell + 1:
    case kActionSpell + 2:
    case kActionSpell + 3:
    case kActionSpell + 4:
    case kActionSpell + 5:
    case kActionSpell + 6:
    case kActionSpell + 7:
        // With Ctrl or Alt the spell is cast at once.
        if (!ui->ctrlDown && !ui->altDown)
            spellColumn->Click(action - kActionSpell);
        else
            spellColumn->ClickAlternate(action - kActionSpell);
        return;
    case kActionWeapon:
    case kActionWeapon + 1:
    case kActionWeapon + 2:
    case kActionWeapon + 3: {
        HudWeaponBar* bar = weaponBar;
        const int slot = action - kActionWeapon;
        const int count = bar->count;
        if (count <= slot) return;
        const int current = bar->selected;
        if (current == slot) return;
        if (current >= 0 && current < count) bar->Highlight(current, false);
        bar->Highlight(slot, true);
        bar->selected = slot;
        ui->PlayInterfaceSound(kSoundWeapon);
        Unit* unit = bar->unit;
        if (!unit) return;
        g_clientWorld.LocalView()->SetWeapon(unit, slot);
        return;
    }
    case kActionQuickItem:
    case kActionQuickItem + 1:
    case kActionQuickItem + 2:
    case kActionQuickItem + 3:
        // With Ctrl or Alt the item is used at once.
        if (!ui->ctrlDown && !ui->altDown)
            quickItems->Pick(action - kActionQuickItem);
        else
            quickItems->UseNow(action - kActionQuickItem);
        return;
    case kActionBodyPart:
    case kActionBodyPart + 1:
    case kActionBodyPart + 2:
    case kActionBodyPart + 3: {
        auto* panel = unitPanel;
        SlideKey(panel, panel->bodyPart, action - kActionBodyPart,
                 [](HudUnitPanel* shown) { shown->ShowBodyPart(); });
        return;
    }
    case kActionPage:
    case kActionPage + 1: {
        auto* panel = topPanel;
        SlideKey(panel, panel->page, action - kActionPage, [](HudTopPanel* shown) { shown->ShowPage(); });
        return;
    }
    case kActionMapSlide:
        ui->PlayInterfaceSound(kSoundSling);
        map->slideDirection = -map->slideDirection;
        map->sliding = true;
        return;
    case kActionQuickSave:
        ::QuickSave(kQuickSaveName, false);
        return;
    case kActionQuickLoad:
        QuickLoad(kQuickSaveName);
        return;
    case kActionMember:
    case kActionMember + 1:
    case kActionMember + 2: {
        const int member = action - kActionMember;
        ui->PlayInterfaceSound(kSoundOnOff);
        ToggleMember(member);
        // With Ctrl or Alt the camera follows the member.
        if ((ui->ctrlDown || ui->altDown) && std::ssize(party) > member)
            camera->SetTarget(party[member]);
        return;
    }
    case kActionSelectAll:
        if (IsMultiplayerClient()) return;
        ui->PlayInterfaceSound(kSoundOnOff);
        for (int i = 0; i < std::ssize(party); ++i) {
            MarkPortrait(i);
            if (Unit* unit = party[i]) g_clientWorld.Select(unit);
        }
        if (!party.empty() && party[0]) Speech(party[0]->id, AckKind::Selected, nullptr);
        RefreshSelection();
        return;
    case kActionMessage:
        ui->ShowMessage(true);
        handled = true;
        return;
    default:
        return;
    }
}

// --- Mouse move and left button ----------------------------------------------------------------------

// The battle screen's mouse move, left button down and a unit's name line. See ui.h (BattleScreen) and
// docs/notes/ui.md ("Battle screen").

namespace {
constexpr int kCursorArrow = 0;
constexpr int kCursorPickPlayer = 0xD;   // a portrait click picks a player
constexpr const char* kUnitTextPrefix = "unit ";

int Distance(int a, int b) { return a < b ? b - a : a - b; }
} // namespace

// --- the mouse moved over the battle screen ------------------------------------------------------

void BattleScreen::MouseMove(int buttons, int x, int y) {
    if (portraitMode != 0) {
        g_cursor.SetShape(kCursorPickPlayer);
        if (portraits) portraits->Input()->MouseMove(buttons, x, y);
        return;
    }
    g_cursor.SetShape(kCursorArrow);
    if (viewPressed && options::g_options.cameraFrameSelectionArea > 0 && !IsMultiplayerClient()) {
        int threshold = options::g_options.cameraFrameSelectionArea;
        if (Distance(x, pressX) > threshold || Distance(y, pressY) > threshold) boxSelecting = true;
        if (boxSelecting) {
            HudUnitPanel* panel = unitPanel;
            if (panel->hoverUnit) panel->hoverUnit = nullptr;
            selectionBox.left = x < pressX ? x : pressX;
            selectionBox.top = y < pressY ? y : pressY;
            selectionBox.right = x > pressX ? x : pressX;
            selectionBox.bottom = y > pressY ? y : pressY;
            return;
        }
    } else {
        UiContainer::MouseMove(buttons, x, y);
        if (handled) return;
    }
    // The (x, y) pair is read back through its address.
    Point point = {x, y};
    Unit* unit = g_clientWorld.UnitAt(&point);
    HoverUnit(unit, point.x, point.y);
}

// --- the left button went down on the battle screen ----------------------------------------------

void BattleScreen::LeftDown(int buttons, int x, int y) {
    if (portraitMode != 0) {
        if (portraits) portraits->Input()->LeftDown(buttons, x, y);
        return;
    }
    if (!viewPressed) {
        UiContainer::LeftDown(buttons, x, y);
        if (handled) return;
    }
    viewPressed = true;
    pressX = x;
    pressY = y;
    boxSelecting = false;
}

// --- a unit's name line --------------------------------------------------------------------------

// Each text is copied up to its first NUL.
std::string BattleScreen::UnitName(Unit* unit) {
    TextManager* texts = g_clientWorld.texts;
    std::string name = unit->GetName();
    if (name.size() != 0) return name.c_str();
    if (IsTextKey(unit->id)) {
        auto node = personTexts.find(unit->id);
        return node == personTexts.end() ? std::string() : std::string(node->second.c_str());
    }
    name = unit->GetPrototype()->name;
    text::ReplaceChar(&name, ' ', '_');
    if (!texts->Open(std::string(kUnitTextPrefix) + name)) return {};
    std::string line = texts->ReadLine(true);
    return line.c_str();
}

// --- Mouse-over --------------------------------------------------------------------------------------

// The battle screen's mouse-over: the unit under the mouse in the unit panel and the
// cursor shape for what a click would do. See ui.h (BattleScreen::HoverUnit) and docs/notes/ui.md
// ("Battle screen").

namespace {

// Cursor shapes ('s index).
constexpr int kCursorAttack = 1;
constexpr int kCursorCast = 2;
constexpr int kCursorUse = 4;
constexpr int kCursorNo = 0xD;
constexpr int kCursorExit = 0xF;
constexpr int kCursorFlagBase = 0x10;   // + the first set flag of +0x18D..+0x192
constexpr int kCursorCantCast = 0x17;
constexpr int kCursorOrder = 0x18;

// fcomp with 0.0 and: true for zero (either sign).
bool IsZero(float value) {
    EI_NAN_AUDIT(value);
    return value == 0.0f;
}

} // namespace

void BattleScreen::HoverUnit(Unit* unit, int x, int y) {
    auto unitDead = [](Unit* u) { return u->IsDead(); };
    auto pick = [x, y]() { return g_clientWorld.PickSurface(x, y); };
    auto objectAt = [x, y]() {
        Point point = {x, y};
        return static_cast<Unit*>(g_clientWorld.PickAt2(&point));
    };
    auto unitPosition = [](Unit* u) {
        Vector3 at;
        at = u->localPosition;
        return at;
    };

    // The unit panel's hover block (hoverUnit, viewSize[1] / viewHeight[1] copied bit for bit from
    // its prototype's +0x20 / +0x24, strings[1]). A unit without MapObjectFlag::InParty counts as none
    // below; a dead one is kept.
    HudUnitPanel* panel = unitPanel;
    if (unit && !(unit->flags & MapObjectFlag::InParty)) unit = nullptr;
    if (unit && !unitDead(unit)) {
        if (unit != panel->hoverUnit) {
            panel->hoverUnit = unit;
            panel->viewSize[1] = unit->GetPrototype()->panelViewSize;
            panel->viewHeight[1] = std::bit_cast<float>(panel->hoverUnit->GetPrototype()->height);
            panel->strings[1] = UnitName(panel->hoverUnit);
        }
    } else if (panel->hoverUnit) {
        panel->hoverUnit = nullptr;
    }

    int count = lib::Count(party);
    if (count <= 0) return;
    Unit* first = nullptr;
    int selected = 0;
    for (int i = 0; i < count; ++i) {
        Unit* member = party[i];
        if (!member || !(member->flags & MapObjectFlag::Selected)) continue;
        if (!first) first = member;
        ++selected;
    }
    if (selected == 0) return;

    int shape = 0;
    if (ui->altDown) {
        Vector3 at = pick();
        int exit;
        if (!g_clientWorld.FindExitAt(&at.x, &exit)) return;
        g_cursor.SetShape(kCursorExit);
        return;
    }
    for (int i = 0; i < 6; ++i) {
        if (orderKeys[i]) {
            g_cursor.SetShape(kCursorFlagBase + i);
            return;
        }
    }
    if (ui->ctrlDown) {
        g_cursor.SetShape(kCursorAttack);
        return;
    }

    switch (pendingAction) {
    case kPendingNone:
        if (unit) {
            if (unitDead(unit)) {
                shape = kCursorUse;
                break;
            }
            if (party.empty() || !party[0]) return;
            if (!party[0]->IsHostileTo(unit)) return;
            shape = kCursorAttack;
            break;
        }
        {
            Unit* object = objectAt();
            if (object && object->IsUsable() != 0 && object->IsDirectUseBlocked() == 0 &&
                selected == 1) {
                shape = kCursorUse;
                break;
            }
            // Over a zone exit: the exit cursor unless the zone's "z.<name>" variable is 1 ("none"
            // is asked as is).
            Vector3 at = pick();
            int exit;
            if (!g_clientWorld.FindExitAt(&at.x, &exit)) return;
            std::string name = g_clientWorld.ExitTarget(exit);
            if (text::CompareIgnoringCase(name,
                                game::NoneText()) != 0) {
                name = std::string("z.") + name;
                float value = LocalPlayerVariable(name);
                const float one = 1.0f;
                // equal skips.
                EI_NAN_AUDIT(value);
                if (value != one) g_cursor.SetShape(kCursorExit);
            }
            return;
        }
    case kPendingSpellOnUnit: {
        if (!unit || unitDead(unit)) {
            shape = kCursorCantCast;
            break;
        }
        if (!first || !first->CanCastSpell(pendingSlot)) {
            shape = kCursorCantCast;
            break;
        }
        shape = kCursorCast;
        break;
    }
    case kPendingSpellAtPoint:
    case kPendingQuickAtPoint: {
        Vector3 at = (unit && !unitDead(unit)) ? unitPosition(unit) : pick();
        if ((IsZero(at.x) && IsZero(at.y)) || !first) {
            shape = kCursorCantCast;
            break;
        }
        const bool able = pendingAction == kPendingSpellAtPoint ? first->CanCastSpell(pendingSlot)
                                                                 : first->CanUseQuickItem(pendingSlot);
        shape = able ? kCursorCast : kCursorCantCast;
        break;
    }
    case kPendingQuickOnUnit: {   // a potion only on the user itself
        shape = kCursorCantCast;
        if (!unit || unitDead(unit)) break;
        if (!first || !first->CanUseQuickItem(pendingSlot)) break;
        Item* item = first->GetQuickItem(pendingSlot);
        if (item && item->GetCategory() == QuickItemCategory::Potion && unit != first) break;
        shape = kCursorCast;
        break;
    }
    case kPendingItemOnObject: {
        Unit* target = unit ? unit : objectAt();
        shape = (target && first && target->IsUsable() != 0) ? kCursorUse : kCursorNo;
        break;
    }
    case kPendingKeyOnObject: {
        Unit* target = unit ? unit : objectAt();
        shape = (target && first && target->IsUsable() != 0 && target->AcceptsKey(&itemUseKey) != 0)
                    ? kCursorUse
                    : kCursorNo;
        break;
    }
    case kPendingFollow:
        shape = (first && unit && !unitDead(unit)) ? kCursorExit : kCursorNo;
        break;
    case kPendingOrder: {
        if (unit) {
            shape = kCursorNo;
            if (!first) break;
            bool member = false;
            for (int i = 0; i < count; ++i) member |= unit == party[i];
            if (!member) shape = kCursorOrder;
            break;
        }
        Unit* target = objectAt();
        shape = (target && first && target->IsUsable() != 0) ? kCursorUse : kCursorNo;
        break;
    }
    default:
        return;
    }
    g_cursor.SetShape(shape);
}

// --- Party commands ----------------------------------------------------------------------------------

// The battle screen's party commands: the member toggle, the attack on a unit
// the order on a dead unit, a unit's speech event and
// the pending spell or quick item used on a unit or a point.
// See docs/notes/ui.md ("Battle screen").

namespace {

// HudPortraitSlot::wobbleKind.
constexpr int kWobbleNod = 1;
constexpr int kWobbleShake = 2;

} // namespace

// --- a party member toggled ----------------------------------------------------------------------

void BattleScreen::ToggleMember(int index) {
    auto* currentParty = &party;
    auto unitAt = [currentParty](int i) { return (*currentParty)[i]; };
    if (IsMultiplayerClient()) return;
    if (index >= std::ssize(*currentParty)) return;

    std::vector<std::uint32_t> chosen{};
    for (int i = 0; i < std::ssize(*currentParty); ++i) {
        Unit* member = unitAt(i);
        lib::SetAtGrow(chosen, lib::Count(chosen), member ? (member->flags & MapObjectFlag::Selected) : 0);
    }
    if (ui->shiftDown) {
        chosen[index] = chosen[index] == 0 ? 1u : 0u;
    } else {
        for (int i = 0; i < std::ssize(chosen); ++i)
            if (chosen[i] != 0) chosen[i] = 0;
        chosen[index] = 1;
    }
    g_clientWorld.Deselect();

    for (int i = 0; i < std::ssize(*currentParty); ++i) {
        auto* currentPortraits = portraits;
        std::uint32_t value = chosen[i];
        currentPortraits->SceneMesh(i * 5 + 4)->visible = value != 0;
        Quaternion turn;
        const Quaternion* rest = &kHudRestRotation;
        if (value != 0) {
            const Vector3 axis = {-1.0f, 0.0f, 0.0f};
            const float angle = kHudQuarterTurn *
                                0.14285715f;   // 1/7
            rest = turn.SetAxisAngle(axis, angle);
        }
        currentPortraits->slots[i].restB = *rest;
        Unit* member = unitAt(i);
        if (member && chosen[i] != 0) {
            g_clientWorld.Select(member);
            if (index == i) Speech(unitAt(i)->id, AckKind::Selected, nullptr);
        }
    }

    // Single player: the local player's unit records (0x784 bytes, +0x1C the unit id) keep the
    // members' selection, record i against member i.
    if (g_serverWorld.gameResult == 1) {
        if (auto* player = g_serverWorld.GetPlayer(0)) {
            for (int i = 0; i < std::ssize(player->GetParty()->members); ++i) {
                if (i >= std::ssize(*currentParty)) continue;
                PartyUnitRecord* record = &player->GetParty()->members[i];
                Unit* member = unitAt(i);
                if (member->id == static_cast<int>(record->unitId))    // the record's unit id
                    record->selected = static_cast<std::uint8_t>((member->flags >> 4) & 1);
            }
        }
    }
    RefreshSelection();
}

// --- the selected members ordered at a unit ------------------------------------------------------

std::vector<int> BattleScreen::SelectedIndexes() const {
    std::vector<int> indexes;
    for (int i = 0; i < std::ssize(party); ++i) {
        const Unit* member = party[i];
        if (member && (member->flags & MapObjectFlag::Selected)) indexes.push_back(i);
    }
    return indexes;
}

void BattleScreen::AttackUnit(Unit* target, bool ctrl) {
    int kind = 0;
    while (kind < 6 && orderKeys[kind] == 0) ++kind;

    std::vector<Unit*> units;
    for (int i : SelectedIndexes()) {
        Unit* member = party[i];
        units.push_back(member);
        portraits->Wobble(i, kWobbleNod);
        if (target->Level() - member->Level() > 5) Speech(member->id, AckKind::BigAttack, nullptr);
    }
    if (!units.empty()) g_clientWorld.LocalView()->SendAttack(&units, target, kind, ctrl);
}

void BattleScreen::OrderDeadUnit(Unit* target, bool ctrl) {
    std::vector<Unit*> units;
    for (int i : SelectedIndexes()) {
        units.push_back(party[i]);
        portraits->Wobble(i, kWobbleNod);
    }
    if (!units.empty()) g_clientWorld.LocalView()->SendUseDead(&units, target, ctrl);
}

// --- the pending spell or quick item used --------------------------------------------------------

void BattleScreen::UseOnUnit(Unit* target, bool quick, bool ctrl) {
    for (int i : SelectedIndexes()) {
        Unit* member = party[i];
        bool able;
        if (!quick) {
            g_clientWorld.LocalView()->SendCastOnUnit(pendingSlot, member, target, ctrl);
            able = member->CanCastSpell(pendingSlot);
        } else {
            g_clientWorld.LocalView()->SendItemOnUnit(pendingSlot, member, target, ctrl);
            quickItems->SetUnit(member);
            able = member->CanUseQuickItem(pendingSlot);
        }
        if (able) portraits->Wobble(i, kWobbleNod);
    }
    SetPendingAction(kPendingNone, -1, -1);
}

// The shift below stays for the later members.
void BattleScreen::UseAtPoint(Vector3 point, Unit* unit, bool quick) {
    for (int i : SelectedIndexes()) {
        Unit* member = party[i];
        if (member == unit) {
            Vector3 axis = unit->localRotation.AxisY();
            const float k = 0.1f;
            point.x = point.x + -axis.x * k;
            point.y = point.y + -axis.y * k;
            point.z = point.z + -axis.z * k;
        }
        bool able;
        if (quick) {
            g_clientWorld.LocalView()->SendItemAtPoint(pendingSlot, member, point, 0);
            quickItems->SetUnit(member);
            able = member->CanUseQuickItem(pendingSlot);
        } else {
            g_clientWorld.LocalView()->SendCastAtPoint(pendingSlot, member, point, 0);
            able = member->CanCastSpell(pendingSlot);
        }
        if (able) portraits->Wobble(i, kWobbleNod);
    }
    SetPendingAction(kPendingNone, -1, -1);
}

// --- a unit's speech event -----------------------------------------------------------------------

void BattleScreen::Speech(int unitId, AckKind kind, char* extra) {
    Unit* unit = UnitWithId(g_clientWorld.objectManager, unitId);
    if (!unit) return;
    const SpeechRecord* phrase = (kind == AckKind::Scenario || kind == AckKind::ScenarioTalking)
                                     ? acks::SayNamedAck(unit, extra, kind == AckKind::ScenarioTalking)
                                     : acks::SayAck(unit, kind);
    if (phrase) {
        // The phrase's text.
        std::string text;
        std::string line = UnitName(unit) + ":\n";
        TextManager* texts = g_clientWorld.texts;
        if (texts->Open(std::string("say ") + phrase->name.data())) {
            text = texts->ReadToHash();
            str::TrimRight(text, "\r\n");
            text += "\n";
            // Both go to the top panel's page 0 and, while it shows that page, the message log.
            auto post = [this](const char* part) {
                topPanel->pageTexts[0] += part;
                if (topPanel->page == 0) messageLog->AddText(part);
            };
            post(line.c_str());
            post(text.c_str());
        }
    }

    auto* currentParty = &party;
    auto unitAt = [currentParty](int i) { return (*currentParty)[i]; };
    if (kind == AckKind::Kill) {
        // The member's timed face (no null test).
        for (int i = 0; i < std::ssize(*currentParty); ++i) {
            if (unitAt(i)->id != unitId) continue;
            HudPortraitSlot* slots = portraits->slots.data();
            slots[i].faceTimer = 0.0f;
            portraits->slots[i].faceMode = 1;
        }
    } else if (kind >= AckKind::NoWayToAttack && kind <= AckKind::Injured) {
        for (int i = 0; i < std::ssize(*currentParty); ++i) {
            if (unitAt(i)->id == unitId) portraits->Wobble(i, kWobbleShake);
        }
    }
}
