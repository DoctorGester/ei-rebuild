// The battle screen (BattleScreen): its creation, saved state and party events, and its every-frame update. See
// docs/notes/ui.md ("Battle screen"). The input side: ui_battle_input.cpp.
#include "camera.h"

#include "console.h"
#include "core_streams.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "item.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "maths.h"
#include "model.h"
#include "net_classes.h"
#include "net_messages.h"
#include "options.h"
#include "party.h"
#include "player_client.h"
#include "player_messages.h"
#include "save_versions.h"
#include "text_manager.h"
#include "text_split.h"
#include "texture.h"
#include "ui.h"
#include "ui_camera_keys.h"
#include "ui_camp.h"
#include "ui_cursor.h"
#include "ui_widescreen.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"
#include "world_data.h"
#include "world_glue.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "render/render.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
#include <string>
#include <vector>

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
            Party* player = LocalPlayerData();
            int index = lib::Count(heroes);
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
    messageLog = new UiScrollText(ui, game::kEmptyText, Rect{0xBE, 0x19, 0x262, 0x59}, 3.0f);
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
    g_serverWorld.SetWeather(0, 0.0f);   // clear weather
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
    g_clientWorld.Render();
    if (boxSelecting) {    // the selection box, a dark rectangle with an outline
        pass->DrawRectangle(selectionBox.left, selectionBox.top, selectionBox.right,
                                                            selectionBox.bottom, 0x40, 1);
    }
    Input()->MouseMove(0, g_cursor.position.x, g_cursor.position.y);
    // The depth buffer is cleared before the panels draw.
    ClearUiDepth(1.0f);
    // "nogui" shows or hides the panels (Rebirth's AddNewConsoleCommands).
    console::ApplyAddonGuiVisibility(this);
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

void BattleScreen::ItemsPicked(const std::vector<Item*>& items, int money, bool crippled) {
    for (Item* item : items) topPanel->AddPickedUp(item, crippled);
    topPanel->AddMoney(money);
    topPanel->ShowItems();
    RefreshSelection();
}

void BattleScreen::UnitLeft(Unit* unit) {
    if (IsMultiplayerClient()) {
        topPanel->pageTexts[0].clear();
        if (topPanel->page == 0) messageLog->SetText(game::kEmptyText);
        return;
    }
    auto& currentParty = party;
    int index = 0;
    while (index < std::ssize(currentParty) && currentParty[index] != unit) ++index;
    if (index >= std::ssize(currentParty)) return;
    int after = lib::Count(currentParty) - index - 1;
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

void BattleScreen::UnitJoined(Unit* unit) {
    if (IsMultiplayerClient()) {
        topPanel->pageTexts[0].clear();
        if (topPanel->page == 0) messageLog->SetText(game::kEmptyText);
        return;
    }
    Party* player = LocalPlayerData();
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
        int count = lib::Count(currentParty);
        if (place >= count) {
            currentParty.resize(place + 1);
        } else {
            currentParty.resize(count + 1);
            std::memmove(&currentParty[place + 1], &currentParty[place], (count - place) * sizeof(currentParty[0]));
        }
        currentParty[place] = unit;
        selected = player->members[record].selected != 0;
    } else {
        int count = lib::Count(currentParty);
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
        int after = lib::Count(heroes) - i - 1;
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
        ui->PlayInterfaceSound("buttons\\gameover.wav");
        box = new MessageWindow(ui, "game_over", "game_over_msg", 0x20000, 3, 1);
    } else {
        box = new MessageWindow(ui, "end_of_game", "end_of_game_msg", 0xFFFF0000u, 3, 1);
    }
    ui->PushWindow(box, true, true);
}

// --- The every-frame update --------------------------------------------------------------------------

// The battle screen's every-frame update, its left button release,
// its quest variable notice, the selection's refresh and the quick
// item panel's unit. See docs/notes/ui.md ("Battle screen").

// --- the quick item panel's unit -----------------------------------------------------------------

void HudQuickItems::SetUnit(Unit* newUnit) {
    std::string tip;
    unit = newUnit;
    ClearSceneObjects();
    shownCount = 0;
    selected = -1;
    for (int i = 0; i < 8; ++i) {
        regions[i]->SetTip(0);
        regionItem[i] = -1;
    }
    entries.clear();    // (zeroed new entries)
    if (!unit) return;

    // The entry of the sprite just added.
    auto finish = [&](Item* item, bool own) {
        entries.resize(shownCount);
        Entry& entry = entries[shownCount - 1];
        entry.own = own;
        entry.id = item->id;
        entry.item = item;
    };
    // The sprite just added: placed in its row (x from the panels' right column, 695; Rebirth moves it), its
    // region pointing at it, scaled, opaque.
    auto place = [&](int slot) {
        EngineObject* sprite = sceneObjects[shownCount - 1];
        const float right = widescreen::g_panelRight;
        if (slot < 4)
            AsUiMesh(sprite)->Place(right - (slot * 50), 575.0f, 20.0f);
        else
            AsUiMesh(sprite)->Place(right - (slot * 50 - 200), 525.0f, 20.0f);
        int last = shownCount - 1;
        regionItem[slot] = last;
        if (last >= 0 && last < shownCount) {
            EngineObject* shown = sceneObjects[last];
            AsUiMesh(shown)->SetScale(slot < 4 ? 0.55f : 0.85f);
            SceneMesh(last)->flatSpecular = 0xFF000000;
        }
    };

    // The unit's four quick items in the bottom row.
    for (unsigned slot = 0; slot < 4; ++slot) {
        Item* item = unit->GetQuickItem(slot);
        if (!item) continue;
        {
            std::string image = item->FigureName();
            std::string model = item->PictureName();
            AddItem(model, image);
        }
        finish(item, true);
        regions[slot]->SetTipString(ItemTip(slot), static_cast<int>(slot) + 0x2A);
        place(static_cast<int>(slot));
    }

    // The local player's own unit also shows up to four of the player's ItemKind::Quest items above, from
    // the last of the player's items backwards (node: +4 previous, +8 item).
    Player* player = g_clientWorld.LocalView()->player;
    if (!player->IsLeader(unit)) return;
    player = g_clientWorld.LocalView()->player;
    auto node = player->items.rbegin();
    if (node == player->items.rend()) return;
    int slot = 4;
    while (node != player->items.rend()) {
        if (slot - 4 >= 4) break;
        Item* item = *node;
        ++node;
        if (!item || item->Kind() != ItemKind::Quest)
            continue;
        {
            std::string image = item->FigureName();
            std::string model = item->PictureName();
            AddItem(model, image);
        }
        finish(item, false);
        // The same string serves as the second and third texts.
        tip = g_clientWorld.texts->GetItemTexts(item).name;
        regions[slot]->SetTipString(tip);
        place(slot);
        ++slot;
    }
}

// --- the battle screen's update ------------------------------------------------------------------

namespace {

// The unit panel's model view for the unit it now shows, copied from the prototype bit for bit (the size
// from `first`'s, the height from the shown unit's: the same unit), and its name.
void ShowPanelView(HudUnitPanel* panel, Unit* first) {
    panel->viewSize[0] = first->GetPrototype()->panelViewSize;
    panel->viewHeight[0] = std::bit_cast<float>(panel->shownUnit->GetPrototype()->height);
    panel->strings[0] = panel->owner->UnitName(panel->shownUnit);
}

// A multiplayer client: the party is the session's players' units, the camera's player first.
void RebuildClientParty(BattleScreen* screen) {
    net::ClientSession* session = net::g_gameClient;
    const auto& players = session->players;
    BattleScreen::PartyIds ids;
    ids.camera = -1;
    for (auto& node : players) {
        net::PlayerMessage* player = node.Get();
        const int unitId = player->id;
        const int playerId = player->serial;
        if (player->state == 0 && unitId >= 0) {
            if (!g_clientWorld.FindUnit(unitId)) {
                ids.without.push_back(playerId);
                continue;
            }
            ids.withUnit.push_back(unitId);
            net::BindingMessage* camera = session->binding;
            if (camera && !(camera->flags7 & RefCounted::kDetached) &&
                camera->first.Get() == player)
                ids.camera = unitId;
        } else {
            ids.without.push_back(playerId);
        }
    }
    std::sort(ids.withUnit.begin(), ids.withUnit.end());
    std::sort(ids.without.begin(), ids.without.end());
    if (!ids.Equals(screen->clientParty)) {
        auto* party = &screen->party;
        party->clear();
        bool cameraFirst = false;
        for (auto& id : ids.withUnit) {
            int unitId = id;
            Unit* unit = g_clientWorld.FindUnit(unitId);
            if (!unit) continue;
            if (unitId == ids.camera) {
                g_clientWorld.Select(unit);
                // Insert at the front.
                int n = lib::Count(*party);
                if (n <= 0) {
                    party->resize(1);
                } else {
                    party->resize(n + 1);
                    std::memmove(party->data() + 1, party->data(), static_cast<std::size_t>(n) * sizeof((*party)[0]));

                }
                (*party)[0] = unit;
                cameraFirst = true;
            } else {
                lib::SetAtGrow(*party, lib::Count(*party), unit);
            }
        }
        screen->portraits->Rebuild(cameraFirst);
        if (screen->clientParty.camera != ids.camera) {
            g_camera.SetTargetId(ids.camera);
            screen->RefreshSelection();
        }
        screen->clientParty.withUnit = ids.withUnit;
        screen->clientParty.camera = ids.camera;
        screen->clientParty.without = ids.without;
    }
}

bool SameIds(const std::vector<int>& a, const std::vector<int>& b) {
    if (a.end() - a.begin() != b.end() - b.begin()) return false;
    for (const int *x = a.data(), *y = b.data(); x != a.data() + a.size(); ++x, ++y)
        if (*x != *y) return false;
    return true;
}

// Equal.
bool Equal(float a, float b) {
    EI_NAN_AUDIT(a, b);
    return a == b;
}

} // namespace

void BattleScreen::Update() {
    // The one selected party unit is dead (IsDead): the panels lose their unit and the
    // unit panel shows it.
    auto* currentParty = &party;
    Unit* first = nullptr;
    int selected = 0;
    for (int i = 0; i < std::ssize(*currentParty); ++i) {
        auto* unit = (*currentParty)[i];
        if (unit && (unit->flags & MapObjectFlag::Selected)) {
            if (!first) first = unit;
            ++selected;
        }
    }
    if (first) {
        if (first->IsDead() && selected == 1) {
            weaponBar->SetUnit(nullptr);
            quickItems->SetUnit(nullptr);
            spellColumn->SetUnit(nullptr);
            actionColumn->SetUnit(nullptr, false);
            HudUnitPanel* panel = unitPanel;
            if (first != panel->shownUnit) {
                panel->shownUnit = first;
                ShowPanelView(panel, first);
            }
        }
    }

    if (IsMultiplayerClient()) RebuildClientParty(this);

    UiContainer::Update();
    ui->ShowMessage(false);

    // A zone exit saved with the game: once it can be taken, the "leave_zone" question.
    if (exitPending) {
        // The local player's unit records (0x784 bytes each) and the ones in the exit.
        std::vector<PartyUnitRecord*> records = {};
        std::vector<PartyUnitRecord*> second = {};
        Party* data = LocalPlayerData();
        for (int i = 0; i < std::ssize(data->members); ++i)
            lib::SetAtGrow(records, lib::Count(records), &data->members[i]);
        if (g_clientWorld.PartyInExit(&records, pendingExit, &second)) {
            MessageWindow* box = new MessageWindow(ui, "leave_zone", "leave_zone_msg", 0x10000, 3, 1);
            ui->PushWindow(box, true, true);
            exitPending = false;
        }
    }

    // The selection box: party units inside it are marked (MapObjectFlag::BoxMarked and their figure's select virtual
    // with 1), the others unmarked (with 0); selected ones are left alone.
    if (boxSelecting) {
        std::vector<MapObject*> units = g_clientWorld.UnitsInBox(&selectionBox);
        std::vector<std::uintptr_t> inside;
        for (MapObject* unit : units) {
            for (int i = 0; i < std::ssize(*currentParty); ++i) {
                if ((*currentParty)[i] != unit) continue;
                inside.push_back(static_cast<std::uintptr_t>(i));
            }
        }
        auto mark = [](Unit* unit, bool on) { unit->AsObject3D()->attached->SetLit(on); };
        for (int i = 0; i < std::ssize(*currentParty); ++i) {
            auto* unit = (*currentParty)[i];
            std::uint32_t unitFlags = unit->flags;
            if (!(unitFlags & MapObjectFlag::Selected) && (unitFlags & MapObjectFlag::BoxMarked)) {
                unit->flags = unitFlags & ~MapObjectFlag::BoxMarked;
                mark(unit, 0);
            }
        }
        for (auto& node : inside) {
            auto* unit = (*currentParty)[node];
            std::uint32_t unitFlags = unit->flags;
            if (!(unitFlags & MapObjectFlag::Selected) && !(unitFlags & MapObjectFlag::BoxMarked)) {
                unit->flags = unitFlags | MapObjectFlag::BoxMarked;
                mark(unit, 1);
            }
        }
    }

    // The quest sound asked for.
    if (questSound != 0) {
        const char* sound = nullptr;
        switch (questSound) {
        case 1: sound = "buttons\\quest.wav"; break;
        case 2: sound = "buttons\\subcomplete.wav"; break;
        case 3: sound = "buttons\\complete.wav"; break;
        }
        if (sound) ui->PlayInterfaceSound(sound);
        questSound = 0;
    }
}

// --- the party ids compared ----------------------------------------------------------------------

bool BattleScreen::PartyIds::Equals(const BattleScreen::PartyIds& other) const {
    return SameIds(withUnit, other.withUnit) && SameIds(without, other.without);
}

// --- the battle screen's left button release -----------------------------------------------------

void BattleScreen::LeftUp(int buttons, int x, int y) {
    if (portraitMode != 0) return;
    if (!viewPressed) {
        UiContainer::LeftUp(buttons, x, y);
        return;
    }
    viewPressed = false;
    WorldClient* world = &g_clientWorld;
    if (!boxSelecting || IsMultiplayerClient()) {
        // The (x, y) pair is read back through its address.
        Point point = {x, y};
        Unit* unit = world->UnitAt(&point);
        if (unit && !(unit->flags & MapObjectFlag::InParty)) unit = nullptr;
        SelectUnit(unit, point.x, point.y);
        return;
    }

    auto* currentParty = &party;
    auto unitAt = [&](int i) { return (*currentParty)[i]; };
    int first = -1;
    std::vector<MapObject*> units = world->UnitsInBox(&selectionBox);
    std::vector<std::uintptr_t> inside;
    if (!units.empty()) {
        for (MapObject* unit : units) {
            for (int i = 0; i < std::ssize(*currentParty); ++i) {
                if ((*currentParty)[i] != unit) continue;
                inside.push_back(static_cast<std::uintptr_t>(i));
                if (first < 0) first = i;
            }
        }
        if (first >= 0) {
            world->Deselect();
            // Portrait i's selection marker (scene object 5 i + 4) and face state.
            auto marker = [&](int i) { return portraits->SceneMesh(i * 5 + 4); };
            auto slot = [&](int i) -> HudPortraitSlot& { return portraits->slots[i]; };
            for (int i = 0; i < std::ssize(*currentParty); ++i) {
                Unit* unit = unitAt(i);
                std::uint32_t unitFlags = unit->flags;
                if (!(unitFlags & MapObjectFlag::Selected) && (unitFlags & MapObjectFlag::BoxMarked)) {
                    unit->flags = unitFlags & ~MapObjectFlag::BoxMarked;
                    unit->AsObject3D()->attached->SetLit(false);
                }
                marker(i)->visible = false;
                slot(i).restB = kHudRestRotation;
            }
            for (auto& node : inside) {
                int i = static_cast<int>(node);
                marker(i)->visible = true;
                const Vector3 axis = {-1.0f, 0.0f, 0.0f};
                const float angle = kHudQuarterTurn *
                                    0.14285715f;   // 1/7
                Quaternion turn;
                turn.SetAxisAngle(axis, angle);
                slot(i).restB = turn;
                world->Select(unitAt(static_cast<int>(node)));
            }
            int unitId = unitAt(first)->id;
            Speech(unitId, AckKind::Selected, nullptr);
        }
    }
    RefreshSelection();
    boxSelecting = false;
}

// --- a quest variable's notice -------------------------------------------------------------------

void BattleScreen::QuestVariable(std::string_view name, float value) {
    TextManager* texts = g_clientWorld.texts;
    std::vector<std::string> parts;
    std::string text;
    std::string title;
    parts = text::Split(name, '.');
    const float complete = 2.0f;
    const float failed = 3.0f;
    if (parts.size() == 3) {
        const char* key;
        if (Equal(value, complete)) {
            if (questSound <= 3) questSound = 3;
            key = "combat_complete_quest";
        } else if (Equal(value, failed)) {
            value = 1.0f;
            key = "combat_failed_quest";
        } else {
            key = "combat_get_quest";
        }
        title = texts->TextManager::GetString(key);
        text.clear();
        if (texts->Open(std::string("quest ") + parts[2])) text = texts->ReadLine(true);
        std::string colour("\007C\356\343" "1 ");   // colour code 0x31E3EE43 and a space
        text = colour + title + " " + text;
    } else if (parts.size() == 4) {
        if (!IsMultiplayerClient()) {
            const char* dot = ".";
            std::string variable = parts[0] + dot + parts[1] + dot + parts[2];
            float current = LocalPlayerVariable(variable);
            EI_NAN_AUDIT(current);
            if (current == 0.0) return;
        }
        const char* key;
        if (Equal(value, complete)) {
            key = "combat_complete_subobj";
        } else if (Equal(value, failed)) {
            value = 1.0f;
            key = "combat_failed_subobj";
        } else {
            key = "combat_get_subobj";
        }
        title = texts->TextManager::GetString(key);
        text.clear();
        bool found = false;
        {
            std::string quest("quest ");
            std::string textKey = quest + parts[2];
            if (texts->Open(textKey)) {
                std::string sub("#subobj ");
                std::string lineKey = sub + parts[3];
                found = texts->FindLine(lineKey) != 0;
            }
        }
        if (found) {
            texts->NextLine();
            text = texts->ReadLine(true);
        }
        std::string colour("\007C\377\263" "1 ");   // colour code 0x31B3FF43 and a space
        text = colour + title + " " + text;
    }
    text = text + "\007C\344\327\247\n";   // colour code and a line end

    topPanel->pageTexts[0] += text;
    if (topPanel->page == 0) messageLog->AddText(text);
    clock->highlight = true;
    int sound = static_cast<int>(value);
    if (questSound > sound) sound = questSound;
    questSound = sound;
}

// --- the selection's refresh ---------------------------------------------------------------------

void BattleScreen::RefreshSelection() {
    auto* currentParty = &party;
    Unit* first = nullptr;
    int selected = 0;
    for (int i = 0; i < std::ssize(*currentParty); ++i) {
        auto* unit = (*currentParty)[i];
        if (!unit || !(unit->flags & MapObjectFlag::Selected)) continue;
        if (!first) first = unit;
        ++selected;
    }
    // Exactly one living unit selected: the panels show it; otherwise they are emptied. The action
    // column's extra button stays while anyone is selected (the flag is pushed with
    // upper bytes left over; the callee reads the low byte only).
    Unit* shown = (first && first->IsDead() == 0 && selected == 1) ? first : nullptr;
    weaponBar->SetUnit(shown);
    quickItems->SetUnit(shown);
    spellColumn->SetUnit(shown);
    actionColumn->SetUnit(shown, selected > 0);

    // The unit panel shows the first selected unit (also a dead one, or the first of several).
    HudUnitPanel* panel = unitPanel;
    if (first != panel->shownUnit) {
        panel->shownUnit = first;
        if (first) ShowPanelView(panel, first);
    }

    // No pending action; every highlight cleared.
    SetPendingAction(kPendingNone, -1, -1);
}
