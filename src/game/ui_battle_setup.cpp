// The battle screen's Create: the HUD panels as children, the
// screen's saved state, the "pers" text table. See docs/notes/ui.md ("Battle screen").
// Several panel builders are Rebirth-patched (widescreen); every panel constructor and builder is
// called through its original address.
#include "core_streams.h"
#include "ui.h"
#include "texture.h"
#include "ui_camera_keys.h"
#include "ui_camp.h"
#include "game_strings.h"

#include "console.h"
#include "text_manager.h"
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
int SaveVersion() { return static_cast<int>(g_saveStream.version); }
bool LoadingGame() { return g_saveStream.size != 0; }


} // namespace

// The panels Create builds inline (they have no constructors of their own).
HudUnitPanel::HudUnitPanel(Interface3D* ui, BattleScreen* newOwner) : UiWindow(ui), owner(newOwner) {}

HudMap::HudMap(Interface3D* ui, BattleScreen* newOwner) : UiWindow(ui), owner(newOwner) {}

void BattleScreen::CreateScreen() {
    float mapZoom = 1.0f;
    portraitMode = 0;
    std::string savedText;
    hasHero = false;
    pendingExit = -1;
    exitPending = false;

    if (LoadingGame()) {
        // The screen's part of the saved game.
        MemoryStream* stream = &g_saveStream;
        stream->Seek(0);
        int count;
        stream->ReadValue(count);
        heroes.resize(count);
        for (int i = 0; i < std::ssize(heroes); ++i) stream->ReadValue(heroes[i]);
        stream->ReadValue(mapZoom);
        std::uint8_t serverFlag;
        stream->ReadValue(serverFlag);
        GameSetServerPaused(serverFlag);
        if (SaveVersion() >= kVersionBattleText) {
            int money;   // the party's money (SaveState), not used
            stream->ReadValue(money);
            savedText = stream->ReadString();
        }
        if (SaveVersion() >= kVersionPendingExit) {
            std::uint8_t newExitPending;   // a byte
            stream->ReadValue(newExitPending);
            exitPending = newExitPending != 0;
            stream->ReadValue(pendingExit);
        }
        stream->Resize(0);
    } else {
        // A new game: the local player's first hero.
        if (std::ssize(LocalPlayerData()->members) > 0) {
            CParty* player = LocalPlayerData();
            int index = std::ssize(heroes);
            std::uint32_t hero = player->members[0].unitId;
            heroes.resize(index + 1);
            heroes[index] = hero;
            hasHero = true;
        }
    }

    // The screen's own input handler.
    UiInputSide* input = new UiInertInput(ui);
    AddInput(input);

    CreateTexture(0x100, 0x100, TextureKind::Blank1555);
    CreateTexture(0x100, 0x100, TextureKind::Blank1555);
    LoadTexture("battle00", TextureKind::Interface);

    // Panels built by their own constructors (Rebirth-patched: reproduced behind its flags, rule 9).
    weaponBar = new HudWeaponBar(ui, this);
    AddChild(weaponBar);
    weaponBar->AddObject(objects[0]);

    quickItems = new HudQuickItems(ui, this);
    AddChild(quickItems);
    quickItems->AddObject(objects[0]);

    spellColumn = new HudSpellColumn(ui, this);
    AddChild(spellColumn);
    spellColumn->AddObject(objects[1]);

    actionColumn = new HudActionColumn(ui, this);
    AddChild(actionColumn);
    actionColumn->AddObject(objects[1]);

    portraits = new HudPortraits(ui, this);
    AddChild(portraits);
    portraits->AddObject(objects[2]);

    unitPanel = new HudUnitPanel(ui, this);
    AddChild(unitPanel);
    unitPanel->AddObject(objects[2]);
    unitPanel->AddObject(objects[1]);
    unitPanel->Setup(ui->hudBodyPart, ui->hudPanelsIn[0] != 0);

    map = new HudMap(ui, this);
    AddChild(map);
    map->AddObject(objects[2]);
    map->Setup(mapZoom, ui->hudPanelsIn[2] != 0);

    clock = new HudClock(ui, this);
    AddChild(clock);
    clock->AddObject(objects[2]);
    clock->Setup(ui->fastGame != 0);

    movePanel = new HudMovePanel(ui, this);
    AddChild(movePanel);
    movePanel->AddObject(objects[2]);
    movePanel->Build();

    // The message log.
    messageLog = new UiScrollText(ui, game::kEmptyText, RECT{0xBE, 0x19, 0x262, 0x59}, 3.0f);
    AddChild(messageLog);
    messageLog->align = 1;

    topPanel = new HudTopPanel(ui, this);
    AddChild(topPanel);
    topPanel->AddObject(objects[2]);
    topPanel->Setup(ui->hudTopPage, ui->hudPanelsIn[1] != 0);
    // The saved message text goes back to the top panel and, when it shows page 0, the log.
    topPanel->pageTexts[0] += savedText;
    if (topPanel->page == 0) messageLog->AddText(savedText);
    topPanel->ShowItems();

    AddInput(new CameraInput(ui));

    g_camera.flags &= ~0x40u;
    pendingAction = kPendingNone;
    pendingSlot = -1;
    GameSetWeather(0, 0.0f);   // clear weather
    GameStartAmbience();
    party.clear();
    viewPressed = false;
    boxSelecting = false;
    std::memset(orderKeys, 0, 6);    // dword and +0x191 word
    questSound = 0;

    // The "pers <name>" texts: one line per person, by the person's text key.
    LoadPersonTexts(&personTexts);
    spare = 0;
    if (IsMultiplayerClient()) {
        PlayersChanged(1);
    }
    clientParty.camera = -1;
}

void BattleScreen::Draw(SceneRenderer* pass) {
    GameRender();
    if (boxSelecting) {    // the selection box, a dark rectangle with an outline
        pass->DrawRectangle(selectionBox.left, selectionBox.top, selectionBox.right,
                                                            selectionBox.bottom, 0x40, 1);
    }
    Input()->MouseMove(0, g_cursor.position.x, g_cursor.position.y);
    // The depth buffer is cleared before the panels draw.
    ClearUiDepth(1.0f);
    // Rebirth's AddNewConsoleCommands (call redirected): "nogui" shows or hides the
    // panels.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::AddNewConsoleCommands)) console::ApplyAddonGuiVisibility(this);
    UiContainer::Draw(pass);
}
