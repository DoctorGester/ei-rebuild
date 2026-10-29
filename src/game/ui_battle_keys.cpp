// The battle screen's key-down handler (input side): the chat line's keys on a
// network client, the panels' own keys, Esc (the pending action dropped, else the escape menu) and
// the key actions of the key table (g_keyActions): pause and game speed, the objectives window, the
// camera, the order keys, movement modes, the action, spell, weapon and quick item buttons, the
// unit panel's body parts, the top panel's pages, the map's slide, quick save / load, the party
// members and the message window. Rebirth's MPSpeedControl (the two JNEs
// aimed at the next instruction) lets the speed keys work on a network client too. See ui_camp.h
// (CBattleScreen) and docs/notes/ui.md, section 6.
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
#include "lib/gameapi.h"
#include <cstdint>
#include <cstring>
#include "game_data.h"
#include "game_singletons.h"
#include "world_data.h"


namespace {

constexpr const char* kSoundClock = "buttons\\battle\\clock.wav";
constexpr const char* kSoundClick = "buttons\\battle\\click.wav";
constexpr const char* kSoundWeapon = "buttons\\battle\\weapon.wav";
constexpr const char* kSoundSling = "buttons\\battle\\sling.wav";
constexpr const char* kSoundOnOff = "buttons\\battle\\on_off.wav";
constexpr const char* kQuickSaveName = "saveq";

// The world's update period in milliseconds: the game's speed.
constexpr int kNormalPeriod = 55;
constexpr int kFastPeriod = 27;

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
// Which orderKeys_ entry each order key action holds (as the key-up handler releases).
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
    if (panel->slideDirection_ == 1.0f) {
        shown = choice;
        panel->slideDirection_ = -1.0f;
        panel->sliding_ = true;
    } else if (shown != choice) {
        shown = choice;
        show(panel);
        return;
    } else {
        panel->sliding_ = true;
        panel->slideDirection_ = 1.0f;
    }
    panel->interface_->PlaySound(kSoundSling);
    show(panel);
}

} // namespace

void CBattleScreen::KeyDown(int key, int repeat, int flags) {
    if (portraitMode_ != 0) {
        if (key == VK_ESCAPE) portraitMode_ = 0;
        return;
    }
    if (key == VK_RETURN) {
        if (IsMultiplayerClient()) {
            interface_->ChatKeyEnter();
            handled_ = true;
            return;
        }
    } else if (key == VK_BACK) {
        if (IsMultiplayerClient()) {
            interface_->ChatKeyBackspace();
            handled_ = true;
            return;
        }
    }
    CUiContainer::KeyDown(key, repeat, flags);
    if (handled_) return;
    if (flags & kKeyRepeated) return;

    if (key == VK_ESCAPE) {
        if (pendingAction_ != kPendingNone) {
            // The pending action is dropped with every panel's highlight.
            SetPendingAction(kPendingNone, -1, -1);
            return;
        }
        interface_->PushWindow(new CEscMenu(interface_), 1, 1);
        handled_ = true;
        return;
    }

    const int action = g_keyActions[flags & kScanCodeMask];
    CCamera* camera = &g_camera.Get();
    switch (action) {
    case kActionPause: {
        interface_->PlaySound(kSoundClock);
        const bool paused = g_clientWorld.paused_;
        GameSendFlag10(paused ? 0 : 1);
        handled_ = true;
        return;
    }
    case kActionNormalSpeed:
    case kActionFastSpeed: {
        if (g_clientWorld.paused_) return;
        if (IsMultiplayerClient() && !ei::rebirth::Enabled(ei::rebirth::Patch::MPSpeedControl)) return;
        interface_->PlaySound(kSoundClock);
        const bool fast = action == kActionFastSpeed;
        clock_->fast_ = fast;
        worlddata::g_updatePeriod = fast ? kFastPeriod : kNormalPeriod;
        interface_->fastGame_ = fast;
        return;
    }
    case kActionObjectives:
        clock_->OpenObjectives();
        handled_ = true;
        return;
    case kActionCameraToUnit:
        // The camera follows the first selected member, or nobody.
        for (int i = 0; i < std::ssize(party_); ++i) {
            if (party_[i]->flags_ & MapObjectFlag::Selected) {
                camera->SetTarget(party_[i]);
                return;
            }
        }
        camera->SetTargetId(-1);
        return;
    case kActionCameraNorth:
        interface_->PlaySound(kSoundClick);
        camera->SetHeading(0.0f);
        map_->northTime_ = 0.5f;
        return;
    case kActionOrderKey:
    case kActionOrderKey + 1:
    case kActionOrderKey + 2:
    case kActionOrderKey + 3:
    case kActionOrderKey + 4:
    case kActionOrderKey + 5:
        orderKeys_[kOrderKeyOfAction[action - kActionOrderKey]] = 1;
        return;
    case kActionMoveMode:
    case kActionMoveMode + 1:
    case kActionMoveMode + 2:
    case kActionMoveMode + 3: {
        const int mode = action - kActionMoveMode;
        if (CPlayerClient* view = g_clientWorld.LocalView()) {
            interface_->PlaySound(kSoundClick);
            for (CUnit* unit : party_) {
                if (unit->flags_ & MapObjectFlag::Selected) view->SetMoveMode(unit, mode);
            }
            movePanel_->ShowMode(mode);
        }
        handled_ = true;
        return;
    }
    case kActionMoveFlag:
        movePanel_->ToggleFlag();
        handled_ = true;
        return;
    case kActionFollow:
        actionColumn_->Select(kFollowButton);
        return;
    case kActionScience:
        actionColumn_->Select(kScienceButton);
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
        if (!interface_->ctrlDown_ && !interface_->altDown_)
            spellColumn_->Click(action - kActionSpell);
        else
            spellColumn_->ClickAlternate(action - kActionSpell);
        return;
    case kActionWeapon:
    case kActionWeapon + 1:
    case kActionWeapon + 2:
    case kActionWeapon + 3: {
        CHudWeaponBar* bar = weaponBar_;
        const int slot = action - kActionWeapon;
        const int count = bar->count_;
        if (count <= slot) return;
        const int current = bar->selected_;
        if (current == slot) return;
        if (current >= 0 && current < count) bar->Highlight(current, false);
        bar->Highlight(slot, true);
        bar->selected_ = slot;
        interface_->PlaySound(kSoundWeapon);
        CUnit* unit = bar->unit_;
        if (!unit) return;
        g_clientWorld.LocalView()->SetWeapon(unit, slot);
        return;
    }
    case kActionQuickItem:
    case kActionQuickItem + 1:
    case kActionQuickItem + 2:
    case kActionQuickItem + 3:
        // With Ctrl or Alt the item is used at once.
        if (!interface_->ctrlDown_ && !interface_->altDown_)
            quickItems_->Pick(action - kActionQuickItem);
        else
            quickItems_->UseNow(action - kActionQuickItem);
        return;
    case kActionBodyPart:
    case kActionBodyPart + 1:
    case kActionBodyPart + 2:
    case kActionBodyPart + 3: {
        auto* panel = unitPanel_;
        SlideKey(panel, panel->bodyPart_, action - kActionBodyPart,
                 [](CHudUnitPanel* shown) { shown->ShowBodyPart(); });
        return;
    }
    case kActionPage:
    case kActionPage + 1: {
        auto* panel = topPanel_;
        SlideKey(panel, panel->page_, action - kActionPage, [](CHudTopPanel* shown) { shown->ShowPage(); });
        return;
    }
    case kActionMapSlide:
        interface_->PlaySound(kSoundSling);
        map_->slideDirection_ = -map_->slideDirection_;
        map_->sliding_ = true;
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
        interface_->PlaySound(kSoundOnOff);
        ToggleMember(member);
        // With Ctrl or Alt the camera follows the member.
        if ((interface_->ctrlDown_ || interface_->altDown_) && std::ssize(party_) > member)
            camera->SetTarget(party_[member]);
        return;
    }
    case kActionSelectAll:
        if (IsMultiplayerClient()) return;
        interface_->PlaySound(kSoundOnOff);
        for (int i = 0; i < std::ssize(party_); ++i) {
            MarkPortrait(i);
            if (CUnit* unit = party_[i]) g_clientWorld.Select(unit);
        }
        if (!party_.empty() && party_[0]) Speech(party_[0]->id_, AckKind::Selected, nullptr);
        RefreshSelection();
        return;
    case kActionMessage:
        interface_->ShowMessage(1);
        handled_ = true;
        return;
    default:
        return;
    }
}
