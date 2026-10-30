// The battle screen's key-down handler (input side): the chat line's keys on a
// network client, the panels' own keys, Esc (the pending action dropped, else the escape menu) and
// the key actions of the key table (g_keyActions): pause and game speed, the objectives window, the
// camera, the order keys, movement modes, the action, spell, weapon and quick item buttons, the
// unit panel's body parts, the top panel's pages, the map's slide, quick save / load, the party
// members and the message window. Rebirth's MPSpeedControl (the two JNEs
// aimed at the next instruction) lets the speed keys work on a network client too. See ui_camp.h
// (BattleScreen) and docs/notes/ui.md, section 6.
#include "ui.h"
#include "world_glue.h"
#include "ui_camp.h"
#include "game_strings.h"

#include "camera.h"
#include "player_client.h"
#include "ui_screens.h"
#include "unit.h"
#include "world.h"

#include "ei/rebirth.h"
#include "lib/crt.h"
#include <cstdint>
#include <cstring>
#include "game_data.h"
#include "game_singletons.h"
#include "lib/win_user.h"

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
// Which orderKeys entry each order key action holds (as the key-up handler releases).
constexpr int kOrderKeyOfAction[6] = {0, 1, 5, 4, 3, 2};

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
        if (key == VK_ESCAPE) portraitMode = 0;
        return;
    }
    if (key == VK_RETURN) {
        if (IsMultiplayerClient()) {
            ui->ChatKeyEnter();
            handled = true;
            return;
        }
    } else if (key == VK_BACK) {
        if (IsMultiplayerClient()) {
            ui->ChatKeyBackspace();
            handled = true;
            return;
        }
    }
    UiContainer::KeyDown(key, repeat, modifiers);
    if (handled) return;
    if (modifiers & kKeyRepeated) return;

    if (key == VK_ESCAPE) {
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
        if (IsMultiplayerClient() && !ei::rebirth::Enabled(ei::rebirth::Patch::MPSpeedControl)) return;
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
        CUnit* unit = bar->unit;
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
            if (CUnit* unit = party[i]) g_clientWorld.Select(unit);
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
