// The battle screen's Create: the HUD panels as children, the
// screen's saved state, the "pers" text table. See docs/notes/ui.md ("Battle screen").
// Several panel builders are Rebirth-patched (widescreen); every panel constructor and builder is
// called through its original address.
#include "camera.h"
#include "console.h"
#include "core_streams.h"
#include "ei/rebirth.h"
#include "game_data.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "lib/crt.h"
#include "maths.h"
#include "model.h"
#include "options.h"
#include "party.h"
#include "render/render.h"
#include "save_versions.h"
#include "text_manager.h"
#include "texture.h"
#include "ui.h"
#include "ui_camera_keys.h"
#include "ui_camp.h"
#include "ui_cursor.h"
#include "unit.h"
#include "world.h"
#include "world_data.h"
#include "world_glue.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <list>
#include <memory>
#include <new>
#include <string>
#include <windows.h>

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

// --- Closing, the first poll, picked-up items, a unit leaving the party and the saved state ------------

namespace {

// The world's update period in milliseconds (the harness's `speed`; the game's default 55).
constexpr int kDefaultUpdatePeriod = 55;
// The autosave option (nonzero: save "savea" on entering a zone).
bool AutoSaveEnabled() { return options::g_options.autosave != 0; }
const char* AutoSaveName() { return "savea"; }


// The portraits' selected-face turn: pi / 2 times (1/7).
float SelectedFaceAngle() {
    return kHudQuarterTurn * 0.14285715f;
}

constexpr std::uint32_t kLeaveZoneYes = 0x10001;
constexpr std::uint32_t kGameOverLoad = 0x20001;
constexpr std::uint32_t kGameOverQuit = 0x20002;
constexpr std::uint32_t kLeaveAnswer = 0x90001;
constexpr std::uint32_t kLoadWindowClosed = 0x100001;
constexpr int kPortraitSprites = 5;   // sprites per portrait; the fifth (4) is the selection marker

} // namespace

void BattleScreen::CloseScreen() {
    g_camera.flags |= Camera::kFree;
    worlddata::g_updatePeriod = kDefaultUpdatePeriod;
    g_cursor.SetHeld(0);
    GameStopSounds(1);
    GameStopAmbience();
    party.clear();
}

bool BattleScreen::ReadyToStopPolling() {
    if (hasHero) {
        auto& currentParty = party;
        if (!currentParty.empty()) g_camera.SetTarget(currentParty[0]);
        if (AutoSaveEnabled()) ::QuickSave(AutoSaveName(), true);
        hasHero = false;
        ui->OpenHelp(false);
    }
    return true;
}

void BattleScreen::ItemsPicked(std::list<Item*>* items, int money, bool crippled) {
    for (Item* item : *items) topPanel->AddPickedUp(item, crippled);
    topPanel->AddMoney(money);
    topPanel->ShowItems();
    RefreshSelection();
}

void BattleScreen::UnitLeft(CUnit* unit) {
    if (IsMultiplayerClient()) {
        topPanel->pageTexts[0].clear();
        if (topPanel->page == 0) messageLog->SetText(game::kEmptyText);
        return;
    }
    auto& currentParty = party;
    int index = 0;
    while (index < std::ssize(currentParty) && currentParty[index] != unit) ++index;
    if (index >= std::ssize(currentParty)) return;
    int after = std::ssize(currentParty) - index - 1;
    if (after != 0) std::memmove(&currentParty[index], &currentParty[index + 1], after * sizeof(currentParty[0]));
    currentParty.pop_back();
    portraits->Rebuild(false);
    RefreshSelection();
}

void BattleScreen::SaveState() {
    ByteStream* stream = &g_saveStream;
    auto& currentParty = party;
    // The ids go through an array first; the bytes written are the same.
    int count = 0;
    for (int i = 0; i < std::ssize(currentParty); ++i)
        if (currentParty[i]->flags & MapObjectFlag::Selected) ++count;
    stream->WriteValue(count);
    for (int i = 0; i < std::ssize(currentParty); ++i) {
        auto* unit = currentParty[i];
        if (unit->flags & MapObjectFlag::Selected) stream->WriteValue(unit->id);
    }
    stream->WriteValue(map->zoom);
    stream->WriteValue(ui->serverFlag);
    stream->WriteValue(LocalPlayerData()->money);    // the party's +0x50 (Create reads it back and drops it)
    stream->WriteString(topPanel->pageTexts[0]);
    stream->WriteValue(exitPending);
    stream->WriteValue(pendingExit);
}

void BattleScreen::MarkPortrait(int index) {
    auto* currentPortraits = portraits;
    currentPortraits->SceneMesh(index * kPortraitSprites + 4)->visible = true;
    const Vector3 axis = {-1.0f, 0.0f, 0.0f};
    Quaternion turn;
    currentPortraits->slots[index].restB = *turn.SetAxisAngle(axis, SelectedFaceAngle());
}

void BattleScreen::UnitJoined(CUnit* unit) {
    if (IsMultiplayerClient()) {
        topPanel->pageTexts[0].clear();
        if (topPanel->page == 0) messageLog->SetText(game::kEmptyText);
        return;
    }
    CParty* player = LocalPlayerData();
    auto& currentParty = party;
    // The unit's place: after the party units whose records come before its record.
    int place = 0;
    int record = 0;
    bool found = false;
    for (; record < std::ssize(player->members); ++record) {
        int unitId = static_cast<int>(player->members[record].unitId);
        if (unit->id == unitId) {
            found = true;
            break;
        }
        if (place < std::ssize(currentParty) && currentParty[place]->id == unitId) ++place;
    }
    bool selected = false;
    if (found) {
        int count = std::ssize(currentParty);
        if (place >= count) {
            currentParty.resize(place + 1);
        } else {
            currentParty.resize(count + 1);
            std::memmove(&currentParty[place + 1], &currentParty[place], (count - place) * sizeof(currentParty[0]));
        }
        currentParty[place] = unit;
        selected = player->members[record].selected != 0;
    } else {
        int count = std::ssize(currentParty);
        currentParty.resize(count + 1);
        currentParty[count] = unit;
    }
    portraits->Rebuild(false);
    if (selected) {
        g_clientWorld.Select(unit);
        MarkPortrait(place);
    }
    // A hero the save had selected.
    for (int i = 0; i < std::ssize(heroes); ++i) {
        if (static_cast<int>(heroes[i]) != unit->id) continue;
        int after = std::ssize(heroes) - i - 1;
        if (after != 0) std::memmove(&heroes[i], &heroes[i + 1], after * sizeof(heroes[0]));
        heroes.pop_back();
        g_clientWorld.Select(unit);
        MarkPortrait(place);
        break;
    }
    RefreshSelection();
}

void BattleScreen::MessageResult(int result) {
    switch (static_cast<std::uint32_t>(result)) {
    case kLeaveZoneYes:
        GameLeaveThroughExit(pendingExit);
        break;
    case kGameOverLoad: {
        ui->PushWindow(new SaveLoadWindow(ui, false, false), true, true);
        break;
    }
    case kGameOverQuit:
    case kLeaveAnswer:
    case kLoadWindowClosed:
        ui->closeAll = true;
        g_gameFlow.DropSessions();
        g_gameFlow.CloseSessions();
        break;
    default:
        break;
    }
}

void BattleScreen::GameOver(bool won) {
    GameStopSounds(1);
    GameStopAmbience();
    MessageWindow* box;
    if (!won) {
        ui->PlaySound("buttons\\gameover.wav");
        box = new MessageWindow(ui, "game_over", "game_over_msg", 0x20000, 3, 1);
    } else {
        box = new MessageWindow(ui, "end_of_game", "end_of_game_msg", 0xFFFF0000u, 3, 1);
    }
    ui->PushWindow(box, true, true);
}
