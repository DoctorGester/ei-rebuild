// The battle screen's Create: the HUD panels as children, the
// screen's saved state, the "pers" text table. See docs/notes/ui.md ("Battle screen").
// Several panel builders are Rebirth-patched (widescreen); every panel constructor and builder is
// called through its original address.
#include "core_streams.h"
#include "ui.h"
#include "ui_native.h"
#include "texture.h"
#include "ui_camera_keys.h"
#include "ui_camp.h"
#include "game_strings.h"

#include "console.h"
#include "item_db_text.h"
#include "party.h"
#include "world.h"
#include "model.h"

#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "render/render.h"

#include <initializer_list>
#include <memory>
#include <new>
#include "world_glue.h"
#include "game_data.h"
#include "game_singletons.h"
#include "camera.h"
#include "ui_cursor.h"
#include "save_versions.h"

namespace {
// The game's save stream's version and a game being loaded (the save stream holds its data).
int SaveVersion() { return static_cast<int>(g_saveStream->version_); }
bool LoadingGame() { return g_saveStream->size_ != 0; }


} // namespace

// The panels Create builds inline (they have no constructors of their own).
CHudUnitPanel::CHudUnitPanel(CInterface3D* ui, CBattleScreen* owner) : CUiWindow(ui), owner_(owner) {}

CHudMap::CHudMap(CInterface3D* ui, CBattleScreen* owner) : CUiWindow(ui), owner_(owner) {}

void CBattleScreen::CreateScreen() {
    float mapZoom = 1.0f;
    portraitMode_ = 0;
    std::string savedText;
    hasHero_ = false;
    pendingExit_ = -1;
    exitPending_ = 0;

    if (LoadingGame()) {
        // The screen's part of the saved game.
        CMemoryStream* stream = &g_saveStream.Get();
        stream->Seek(0);
        int count;
        stream->ReadValue(count);
        heroes_.resize(count);
        for (int i = 0; i < std::ssize(heroes_); ++i) stream->ReadValue(heroes_[i]);
        stream->ReadValue(mapZoom);
        std::uint8_t serverFlag;
        stream->ReadValue(serverFlag);
        GameSetServerPaused(serverFlag);
        if (SaveVersion() >= kVersionBattleText) {
            int unused;
            stream->ReadValue(unused);
            std::string raw;
            std::string* text = stream->ReadString(&raw);
            savedText = *text;
        }
        if (SaveVersion() >= kVersionPendingExit) {
            stream->ReadValue(exitPending_);
            stream->ReadValue(pendingExit_);
        }
        stream->Resize(0);
    } else {
        // A new game: the local player's first hero.
        if (std::ssize(LocalPlayerData()->members_) > 0) {
            CParty* player = LocalPlayerData();
            int index = std::ssize(heroes_);
            std::uint32_t hero = player->members_[0].unitId;
            heroes_.resize(index + 1);
            heroes_[index] = hero;
            hasHero_ = true;
        }
    }

    // The screen's own input handler.
    UiInputSide* input = new UiInertInput(interface_);
    AddInput(input);

    CreateTexture(0x100, 0x100, 0x20);
    CreateTexture(0x100, 0x100, 0x20);
    LoadTexture("battle00", 3);

    // Panels built by their own constructors (Rebirth-patched: reproduced behind its flags, rule 9).
    weaponBar_ = new CHudWeaponBar(interface_, this);
    AddChild(weaponBar_);
    weaponBar_->AddObject(objects_[0]);

    quickItems_ = new CHudQuickItems(interface_, this);
    AddChild(quickItems_);
    quickItems_->AddObject(objects_[0]);

    spellColumn_ = new CHudSpellColumn(interface_, this);
    AddChild(spellColumn_);
    spellColumn_->AddObject(objects_[1]);

    actionColumn_ = new CHudActionColumn(interface_, this);
    AddChild(actionColumn_);
    actionColumn_->AddObject(objects_[1]);

    portraits_ = new CHudPortraits(interface_, this);
    AddChild(portraits_);
    portraits_->AddObject(objects_[2]);

    unitPanel_ = new CHudUnitPanel(interface_, this);
    AddChild(unitPanel_);
    unitPanel_->AddObject(objects_[2]);
    unitPanel_->AddObject(objects_[1]);
    unitPanel_->SetupVanilla(interface_->hudBodyPart_, interface_->hudPanelsIn_[0]);

    map_ = new CHudMap(interface_, this);
    AddChild(map_);
    map_->AddObject(objects_[2]);
    map_->SetupVanilla(mapZoom, interface_->hudPanelsIn_[2]);

    clock_ = new CHudClock(interface_, this);
    AddChild(clock_);
    clock_->AddObject(objects_[2]);
    clock_->SetupVanilla(interface_->fastGame_);

    movePanel_ = new CHudMovePanel(interface_, this);
    AddChild(movePanel_);
    movePanel_->AddObject(objects_[2]);
    movePanel_->Build();

    // The message log, native once made (the scroll text draws nothing before its first Draw).
    messageLog_ = new CUiScrollText(interface_, game::kEmptyText, RECT{0xBE, 0x19, 0x262, 0x59}, 3.0f);
    ui_native::Adopt(messageLog_->surfaces_[0], "messagelog");
    AddChild(messageLog_);
    messageLog_->align_ = 1;

    topPanel_ = new CHudTopPanel(interface_, this);
    AddChild(topPanel_);
    topPanel_->AddObject(objects_[2]);
    topPanel_->Build(interface_->hudTopPage_, interface_->hudPanelsIn_[1]);
    {
        // The saved message text goes back to the top panel and, when it shows page 0, the log.
        auto* top = topPanel_;
        auto& panelText = top->pageTexts_[0];
        panelText += savedText;
        if (top->page_ == 0) {
            // The owner is this screen (a container; CBattleScreen declares it as a window).
            auto* owner = top->owner_;
            owner->MessageLog()->AddText(savedText);
        }
    }
    topPanel_->ShowItems();

    AddInput(new CCameraInput(interface_));

    g_camera->flags_ &= ~0x40u;
    pendingAction_ = 0;
    pendingSlot_ = -1;
    GameSetWeather(0, 0.0f);   // clear weather
    GameStartAmbience();
    party_.clear();
    viewPressed_ = false;
    boxSelecting_ = false;
    std::memset(orderKeys_, 0, 6);   // dword and +0x191 word
    questSound_ = 0;

    // The "pers <name>" texts: one line per person, by the person's text key.
    LoadPersonTexts(&personTexts_);
    spare_ = 0;
    if (IsMultiplayerClient()) {
        PlayersChanged(1);
    }
    clientParty_.camera = -1;
}

BOOL CBattleScreen::Draw(CSceneRenderer* pass) {
    GameRender();
    if (boxSelecting_) {   // the selection box, a dark rectangle with an outline
        pass->DrawRectangle(selectionBox_.left, selectionBox_.top, selectionBox_.right,
                                                            selectionBox_.bottom, 0x40, 1);
    }
    Input()->MouseMove(0, g_cursor->position_.x, g_cursor->position_.y);
    // The depth buffer is cleared before the panels draw.
    ClearUiDepth(1.0f);
    // Rebirth's AddNewConsoleCommands (call redirected): "nogui" shows or hides the
    // panels.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AddNewConsoleCommands)) console::ApplyAddonGuiVisibility(this);
    CUiContainer::Draw(pass);
    return 1;
}
