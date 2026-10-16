// The battle screen's key-down handler (0063A560, input side slot 9): the chat line's keys on a
// network client, the panels' own keys, Esc (the pending action dropped, else the escape menu) and
// the key actions of the key table (KeyActions): pause and game speed, the objectives window, the
// camera, the order keys, movement modes, the action, spell, weapon and quick item buttons, the
// unit panel's body parts, the top panel's pages, the map's slide, quick save / load, the party
// members and the message window. Rebirth's MPSpeedControl (the two JNEs at 0063A6D4 / 0063A73A
// aimed at the next instruction) lets the speed keys work on a network client too. See ui_camp.h
// (CBattleScreen) and
// docs/notes/ui.md, section 6.
#include "ui.h"
#include "ui_camp.h"
#include "game_strings.h"

#include "camera.h"
#include "player_client.h"
#include "ui_screens.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_ui.h"
#include "originals_world.h"

#include <cstring>

namespace {

const char* ClockSound() { return game::Text(0x0079447C); }     // "buttons\\battle\\clock.wav"
const char* ClickSound() { return game::Text(0x00794460); }     // "buttons\\battle\\click.wav"
const char* WeaponSound() { return game::Text(0x00794444); }    // "buttons\\battle\\weapon.wav"
const char* SlingSound() { return game::Text(0x00794428); }     // "buttons\\battle\\sling.wav"
const char* OnOffSound() { return game::Text(0x007935BC); }     // "buttons\\battle\\on_off.wav"
const char* QuickSaveName() { return game::Text(0x007933AC); }  // "saveq"
const char* FollowName() { return game::Text(0x00787928); }     // "follow"
const char* ScienceName() { return game::Text(0x0078B8FC); }    // "science"

// The world's update period in milliseconds: the game's speed.
int& UpdatePeriod() { return *reinterpret_cast<int*>(0x0078FF34); }
constexpr int kNormalPeriod = 55;
constexpr int kFastPeriod = 27;

constexpr std::uint32_t kUnitSelected = 0x10;   // CEngineObject::flags_ of a selected party unit
constexpr int kKeyRepeated = 0x4000;            // the key was already down
constexpr int kScanCodeMask = 0x1FF;
constexpr int kQuickSlots = 8;
constexpr std::size_t kObjectivesSize = 0x1F0;
// The escape menu (0x168 bytes, built inline).
constexpr std::size_t kEscMenuSize = 0x168;
void** EscMenuVtable() { return reinterpret_cast<void**>(0x007412A4); }
void** EscMenuInputVtable() { return reinterpret_cast<void**>(0x00741270); }

// The key actions (the key table's values; the original's jump table at 0063B4DC).
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
// Which orderKeys_ entry each order key action holds (as the key-up handler 0063B6D0 releases).
constexpr int kOrderKeyOfAction[6] = {0, 1, 5, 4, 3, 2};

// The screen as the panels know it (the same object; ui.h).
CBattleScreen* Hud(CBattleScreen* screen) { return screen; }
CPlayerClient* LocalView() { return reinterpret_cast<CPlayerClient*>(WorldClient()->LocalView()); }

// The action column's button of an action name: "follow" is button 0, any other name button 1
// (the original compares the two constant names inline).
int ActionButton(const char* name) { return std::strcmp(name, FollowName()) == 0 ? 0 : 1; }

// A panel's slide key with a choice (the unit panel's body part, the top panel's page): a panel
// that is slid away comes back showing the choice; a new choice is shown without a sound; the
// choice already shown slides the panel away.
template <typename Panel, typename Show>
void SlideKey(Panel* panel, int& shown, int choice, Show show) {
    if (panel->slideDirection_ == 1.0f) {
        shown = choice;
        panel->slideDirection_ = -1.0f;
        panel->sliding_ = 1;
    } else if (shown != choice) {
        shown = choice;
        show(panel);
        return;
    } else {
        panel->sliding_ = 1;
        panel->slideDirection_ = 1.0f;
    }
    panel->interface_->PlaySound(SlingSound(), 0);
    show(panel);
}

} // namespace

void CBattleScreen::OnKeyDown(int key, int repeat, int flags) {
    if (portraitMode_ != 0) {
        if (key == VK_ESCAPE) portraitMode_ = 0;
        return;
    }
    if (key == VK_RETURN) {
        if (IsNetworkClientOriginal()) {
            interface_->ChatKeyEnter();
            inputHandled_ = true;
            return;
        }
    } else if (key == VK_BACK) {
        if (IsNetworkClientOriginal()) {
            interface_->ChatKeyBackspace();
            inputHandled_ = true;
            return;
        }
    }
    InputOf(this)->BaseKeyDown(key, repeat, flags);
    if (inputHandled_) return;
    if (flags & kKeyRepeated) return;

    CBattleScreen* hud = Hud(this);
    if (key == VK_ESCAPE) {
        if (hud->pendingAction_ != 0) {
            // The pending action is dropped with every panel's highlight.
            CHudQuickItems* quick = hud->quickItems_;
            hud->pendingSlot_ = -1;
            hud->pendingAction_ = 0;
            hud->pendingItem_ = -1;
            for (int i = 0; i < kQuickSlots; ++i) static_cast<CHudQuickItems*>(quick)->Highlight(i, 0);
            quick->selected_ = -1;
            hud->spellColumn_->ClearHighlights();
            static_cast<CHudActionColumn*>(hud->actionColumn_)->ClearHighlights();
            return;
        }
        void* memory = gamecrt::operator_new(kEscMenuSize);
        CUiWindow* menu = nullptr;
        if (memory) {
            ConstructContainerAt(memory, nullptr, interface_);
            menu = static_cast<CUiWindow*>(memory);
            menu->vtable_ = EscMenuVtable();
            menu->inputVtable_ = EscMenuInputVtable();
        }
        InterfacePushWindowOriginal(interface_, nullptr, menu, 1, 1);
        inputHandled_ = true;
        return;
    }

    const int action = KeyActions()[flags & kScanCodeMask];
    CCamera* camera = camera::TheCamera();
    switch (action) {
    case kActionPause: {
        auto* clock = static_cast<CHudClock*>(clock_);
        clock->interface_->PlaySound(ClockSound(), 0);
        const bool paused = GameIsPausedOriginal(GameGlueObject());
        GameSetPausedOriginal(GameGlueObject(), nullptr, paused ? 0 : 1);
        inputHandled_ = true;
        return;
    }
    case kActionNormalSpeed:
    case kActionFastSpeed: {
        auto* clock = static_cast<CHudClock*>(clock_);
        if (GameIsPausedOriginal(GameGlueObject())) return;
        if (IsNetworkClientOriginal() && !ei::rebirth::Enabled(ei::rebirth::Patch::MPSpeedControl)) return;
        clock->interface_->PlaySound(ClockSound(), 0);
        const bool fast = action == kActionFastSpeed;
        clock->fast_ = fast ? 1 : 0;
        UpdatePeriod() = fast ? kFastPeriod : kNormalPeriod;
        clock->interface_->fastGame_ = clock->fast_;
        return;
    }
    case kActionObjectives: {
        // The objectives of the zone the local player is in, without buttons or a zone list.
        auto* clock = static_cast<CHudClock*>(clock_);
        clock->highlight_ = false;
        clock->interface_->PlaySound(ClickSound(), 0);
        void* memory = gamecrt::operator_new(kObjectivesSize);
        void* window = memory ? ObjectivesConstructOriginal(memory, nullptr, clock->interface_) : nullptr;
        {
            alignas(mfc::CString) unsigned char raw[sizeof(mfc::CString)];
            mfc::CString* zone = LocalPlayerName(reinterpret_cast<mfc::CString*>(raw));
            static_cast<CObjectivesWindow*>(window)->Setup(static_cast<const char*>(*zone), nullptr, 0, 0);
            zone->~CString();
        }
        InterfacePushWindowOriginal(clock->interface_, nullptr, window, 1, 1);
        inputHandled_ = true;
        return;
    }
    case kActionCameraToUnit:
        // The camera follows the first selected member, or nobody.
        for (int i = 0; i < hud->party_.size; ++i) {
            if (hud->party_[i]->flags_ & kUnitSelected) {
                CameraSetTargetOriginal(camera, nullptr, hud->party_[i]);
                return;
            }
        }
        camera->SetTargetId(-1);
        return;
    case kActionCameraNorth: {
        auto* map = static_cast<CHudMap*>(map_);
        map->interface_->PlaySound(ClickSound(), 0);
        camera->SetHeading(0.0f);
        map->northTime_ = 0.5f;
        return;
    }
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
        auto* panel = static_cast<CHudMovePanel*>(movePanel_);
        const int mode = action - kActionMoveMode;
        if (WorldClient()->LocalView()) {
            panel->interface_->PlaySound(ClickSound(), 0);
            for (int i = 0; i < panel->owner_->party_.size; ++i) {
                CUnit* unit = panel->owner_->party_[i];
                if (unit->flags_ & kUnitSelected) LocalView()->SetMoveMode(unit, mode);
            }
            panel->ShowMode(mode);
        }
        inputHandled_ = true;
        return;
    }
    case kActionMoveFlag:
        static_cast<CHudMovePanel*>(movePanel_)->ToggleFlag();
        inputHandled_ = true;
        return;
    case kActionFollow:
        hud->actionColumn_->Select(ActionButton(FollowName()));
        return;
    case kActionScience:
        hud->actionColumn_->Select(ActionButton(ScienceName()));
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
            hud->spellColumn_->Click(action - kActionSpell);
        else
            hud->spellColumn_->ClickAlternate(action - kActionSpell);
        return;
    case kActionWeapon:
    case kActionWeapon + 1:
    case kActionWeapon + 2:
    case kActionWeapon + 3: {
        CHudWeaponBar* bar = hud->weaponBar_;
        const int slot = action - kActionWeapon;
        const int count = bar->count_;
        if (count <= slot) return;
        const int current = bar->selected_;
        if (current == slot) return;
        if (current >= 0 && current < count) bar->Highlight(current, 0);
        bar->Highlight(slot, 1);
        bar->selected_ = slot;
        bar->interface_->PlaySound(WeaponSound(), 0);
        CUnit* unit = bar->unit_;
        if (!unit) return;
        LocalView()->SetWeapon(unit, slot);
        return;
    }
    case kActionQuickItem:
    case kActionQuickItem + 1:
    case kActionQuickItem + 2:
    case kActionQuickItem + 3:
        // With Ctrl or Alt the item is used at once.
        if (!interface_->ctrlDown_ && !interface_->altDown_)
            hud->quickItems_->Pick(action - kActionQuickItem);
        else
            hud->quickItems_->UseNow(action - kActionQuickItem);
        return;
    case kActionBodyPart:
    case kActionBodyPart + 1:
    case kActionBodyPart + 2:
    case kActionBodyPart + 3: {
        auto* panel = static_cast<CHudUnitPanel*>(unitPanel_);
        SlideKey(panel, panel->bodyPart_, action - kActionBodyPart,
                 [](CHudUnitPanel* shown) { shown->ShowBodyPart(); });
        return;
    }
    case kActionPage:
    case kActionPage + 1: {
        auto* panel = static_cast<CHudTopPanel*>(topPanel_);
        SlideKey(panel, panel->page_, action - kActionPage, [](CHudTopPanel* shown) { shown->ShowPage(); });
        return;
    }
    case kActionMapSlide: {
        auto* map = static_cast<CHudMap*>(map_);
        map->interface_->PlaySound(SlingSound(), 0);
        map->slideDirection_ = -map->slideDirection_;
        map->sliding_ = 1;
        return;
    }
    case kActionQuickSave:
        AutoSaveOriginal(QuickSaveName(), 0);
        return;
    case kActionQuickLoad:
        LoadSavedGameOriginal(QuickSaveName());
        return;
    case kActionMember:
    case kActionMember + 1:
    case kActionMember + 2: {
        const int member = action - kActionMember;
        interface_->PlaySound(OnOffSound(), 0);
        ToggleMember(member);
        // With Ctrl or Alt the camera follows the member.
        if ((interface_->ctrlDown_ || interface_->altDown_) && hud->party_.size > member)
            CameraSetTargetOriginal(camera, nullptr, hud->party_[member]);
        return;
    }
    case kActionSelectAll:
        if (IsNetworkClientOriginal()) return;
        interface_->PlaySound(OnOffSound(), 0);
        for (int i = 0; i < hud->party_.size; ++i) {
            MarkPortrait(i);
            if (CUnit* unit = hud->party_[i]) ClientWorldControlUnitOriginal(WorldClient(), nullptr, unit);
        }
        if (hud->party_.size != 0 && hud->party_[0]) hud->VirtualUnitSpeech(hud->party_[0]->id_, 0, nullptr);
        RefreshSelection();
        return;
    case kActionMessage:
        interface_->ShowMessage(1);
        inputHandled_ = true;
        return;
    default:
        return;
    }
}

namespace {
// Rebirth-patched (the shim stays a named function so check_patched_calls sees it).
void __fastcall BattleScreenKeyDown(void* input, void*, int key, int repeat, int flags) {
    WindowFromInput<CBattleScreen>(input)->OnKeyDown(key, repeat, flags);
}
} // namespace
EI_REPLACE(0x0063A560, BattleScreenKeyDown);
EI_REBIRTH_IMPLEMENTS(0x0063A560, ei::rebirth::Patch::MPSpeedControl);
