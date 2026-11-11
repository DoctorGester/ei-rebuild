// The battle screen (BattleScreen, ui_battle.h): its creation, saved state and party events, its every-frame update, the
// selection and orders, the pending action the HUD buttons start, and its input (the close, double click, key release and
// wheel, the key-down handler, the mouse, the mouse-over and the party commands they issue). The HUD panels it builds
// are in ui_hud*.cpp. See docs/notes/ui.md, section 6 ("Battle screen") and section 11.
#include "game/ui/ui_battle.h"
#include "game/units/acks_db.h"
#include "game/app/app.h"
#include "game/core/base_manager.h"
#include "game/scene/camera.h"
#include "game/console/console.h"
#include "game/core/core_streams.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "game/app/tool_hooks.h"
#include "game/items/item.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/scene/scene_renderer.h"
#include "game/models/texture.h"
#include "game/net/net_session.h"
#include "game/net/net_messages.h"
#include "game/app/key_bindings.h"
#include "game/app/options.h"
#include "game/players/party.h"
#include "game/players/player_client.h"
#include "game/players/player.h"
#include "game/saves/save_versions.h"
#include "game/ui/screen_layout.h"
#include "game/sound/sound.h"
#include "game/terrain/terrain.h"
#include "game/data/text_manager.h"
#include "game/core/text_split.h"
#include "game/ui/ui.h"
#include "game/ui/ui_camera_input.h"
#include "game/ui/ui_camp.h"
#include "game/ui/ui_cursor.h"
#include "game/ui/ui_hud.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_message_box.h"
#include "game/ui/ui_saveload.h"
#include "game/ui/ui_screens.h"
#include "game/ui/ui_scroll_text.h"
#include "game/units/unit.h"
#include "game/units/unit_client_models.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "lib/vecmath.h"
#include "platform/platform.h"
#include "render/render.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {
// A game being loaded (the save stream holds its data).
bool LoadingGame() { return g_saveStream.size != 0; }

// The world's update period in milliseconds (the harness's `speed`; the game's default 55).
constexpr int kDefaultUpdatePeriod = 55;
// The autosave option (nonzero: save "savea" on entering a zone). A tool's own game (app::g_toolScene) never saves:
// the effect viewer's new game must not overwrite the player's autosave.
bool AutoSaveEnabled() { return options::g_options.autosave != 0 && !app::g_toolScene.newGame; }
constexpr const char* kAutoSaveName = "savea";

// The portraits' selected-face turn: pi / 2 times (1/7).
float SelectedFaceAngle() {
    return kHudQuarterTurn * 0.14285715f;
}

constexpr std::uint32_t kLeaveAnswer = 0x90001;
constexpr std::uint32_t kLoadWindowClosed = 0x100001;

// The unit panel's model view for the unit it now shows, copied from the prototype bit for bit (the size
// from `first`'s, the height from the shown unit's: the same unit), and its name.
void ShowPanelView(HudUnitPanel* panel, Unit* first) {
    panel->viewSize[0] = first->GetPrototype()->panelViewSize;
    panel->viewHeight[0] = std::bit_cast<float>(panel->shownUnit->GetPrototype()->height);
    panel->strings[0] = panel->owner->UnitName(panel->shownUnit);
}

// The local hero's companions: the units of its party's records after the first.
std::vector<int> CompanionIds() {
    std::vector<int> ids;
    const Party* party = LocalPlayerData();
    for (int i = 1; i < std::ssize(party->members); ++i) {
        const int id = static_cast<int>(party->members[i].unitId);
        if (g_clientWorld.FindUnit(id)) ids.push_back(id);
    }
    return ids;
}

// A multiplayer client: the party is the session's players' units, the camera's player first and its companions
// next.
void RebuildClientParty(BattleScreen* screen) {
    net::ClientSession* session = net::g_gameClient;
    const auto& players = session->players;
    BattleScreen::PartyIds ids;
    ids.camera = -1;
    const std::vector<int> companions = CompanionIds();
    ids.withUnit = companions;
    for (auto& node : players) {
        net::PlayerMessage* player = node.Get();
        const int unitId = player->id;
        const int playerId = player->serial;
        if (player->state == net::PlayerStatus::InZone && unitId >= 0) {
            if (!g_clientWorld.FindUnit(unitId)) {
                ids.without.push_back(playerId);
                continue;
            }
            ids.withUnit.push_back(unitId);
            net::BindingMessage* camera = session->binding;
            if (IsLive(camera) && camera->player.Get() == player)
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
            if (!unit || lib::Contains(companions, unitId)) continue;
            if (unitId == ids.camera) {
                g_clientWorld.Select(unit);
                party->insert(party->begin(), unit);
                cameraFirst = true;
            } else {
                party->push_back(unit);
            }
        }
        for (int i = 0; i < std::ssize(companions); ++i)
            party->insert(party->begin() + (cameraFirst ? 1 : 0) + i, g_clientWorld.FindUnit(companions[i]));
        screen->companionPortraits = lib::Count(companions);
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

// Key actions 0x11..0x16 hold the order keys; each releases its flag in orderKeys.
constexpr int kFirstOrderKeyAction = 0x11;
// Which orderKeys entry each of them releases.
constexpr int kOrderKeyOfAction[6] = {0, 1, 5, 4, 3, 2};

constexpr const char* kSoundSling = "buttons\\battle\\sling.wav";
constexpr const char* kSoundOnOff = "buttons\\battle\\on_off.wav";
constexpr const char* kQuickSaveName = "saveq";

// The key actions (the key table's values). A family of actions (the spells, the weapons) is a run of consecutive
// numbers from its first, so they are numbers for the dispatch's `case Spell + 1:`, not an enum class.
namespace KeyAction {
constexpr int Pause = 1;
constexpr int NormalSpeed = 2;
constexpr int FastSpeed = 3;
constexpr int Objectives = 4;
constexpr int CameraToUnit = 0xB;
constexpr int CameraNorth = 0xC;
constexpr int OrderKey = 0x11;      // .. 0x16
constexpr int MoveMode = 0x17;      // .. 0x1A: modes 0..3
constexpr int MoveFlag = 0x1B;
constexpr int Follow = 0x1C;
constexpr int Science = 0x1D;
constexpr int Spell = 0x1E;         // .. 0x25: spells 0..7
constexpr int Weapon = 0x26;        // .. 0x29: weapons 0..3
constexpr int QuickItem = 0x2A;     // .. 0x2D: quick items 0..3
constexpr int BodyPart = 0x2E;      // .. 0x31: body parts 0..3
constexpr int Page = 0x32;          // 0x33: the top panel's pages 0, 1
constexpr int MapSlide = 0x34;
constexpr int QuickSave = 0x35;
constexpr int QuickLoad = 0x36;
constexpr int Member = 0x37;        // .. 0x39: party members 0..2
constexpr int SelectAll = 0x3A;
constexpr int Message = 0x3B;
} // namespace KeyAction

// The action column's buttons the keys select (the original compares the names "follow" and "science"
// with "follow").
constexpr int kFollowButton = 0;
constexpr int kScienceButton = 1;

// A panel's slide key with a choice (the unit panel's body part, the top panel's page): a panel
// that is slid away comes back showing the choice; a new choice is shown without a sound; the
// choice already shown slides the panel away.
template <typename Panel, typename Show>
void SlideKey(Panel* panel, int& shown, int choice, Show show) {
    if (panel->slide.direction == 1.0f) {
        shown = choice;
        panel->slide.direction = -1.0f;
        panel->slide.moving = true;
    } else if (shown != choice) {
        shown = choice;
        show(panel);
        return;
    } else {
        panel->slide.moving = true;
        panel->slide.direction = 1.0f;
    }
    g_interface.PlayInterfaceSound(kSoundSling);
    show(panel);
}
constexpr const char* kUnitTextPrefix = "unit ";

int Distance(int a, int b) { return a < b ? b - a : a - b; }

// HudPortraitSlot::wobbleKind.
constexpr int kWobbleNod = 1;
constexpr int kWobbleShake = 2;
} // namespace

void BattleScreen::CreateScreen() {
    g_interface.battle = this;
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
        heroes.resize(stream->ReadCount());
        for (std::uint32_t& heroId : heroes) stream->ReadValue(heroId);
        stream->ReadValue(mapZoom);
        std::uint8_t serverFlag;
        stream->ReadValue(serverFlag);
        g_serverWorld.SetServerPaused(serverFlag);
        if (g_saveStream.version >= kVersionBattleText) {
            int money;   // the party's money (SaveState), not used
            stream->ReadValue(money);
            savedText = stream->ReadString();
        }
        if (g_saveStream.version >= kVersionPendingExit) {
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

    CreateTexture(0x100, 0x100, TextureKind::Blank1555);
    CreateTexture(0x100, 0x100, TextureKind::Blank1555);
    LoadTexture("battle00", TextureKind::Interface);

    BuildPanels(mapZoom);
    // The saved message text goes back to the top panel and, when it shows page 0, the log.
    topPanel->pageTexts[0] += savedText;
    if (topPanel->page == 0) messageLog->AddText(savedText);
    topPanel->ShowItems();

    AddInput(std::make_unique<CameraInput>());

    g_camera.flags &= ~0x40u;
    pendingAction = PendingAction::None;
    pendingSlot = -1;
    g_clientWorld.terrain->landscapeFlags |= terrain::LandscapeFlag::Ambience;
    party.clear();
    viewPressed = false;
    boxSelecting = false;
    std::memset(orderKeys, 0, 6);    // dword and +0x191 word
    questSound = 0;

    // The "pers <name>" texts: one line per person, by the person's text key.
    LoadPersonTexts(&personTexts);
    clientParty.camera = -1;
}

void BattleScreen::BuildPanels(float mapZoom) {
    // Panels built by their own constructors (Rebirth-patched: reproduced behind its flags, rule 9).
    weaponBar = AddChild(std::make_unique<HudWeaponBar>(this));
    weaponBar->AddObject(objects[0]);

    quickItems = AddChild(std::make_unique<HudQuickItems>(this));
    quickItems->AddObject(objects[0]);

    spellColumn = AddChild(std::make_unique<HudSpellColumn>(this));
    spellColumn->AddObject(objects[1]);

    actionColumn = AddChild(std::make_unique<HudActionColumn>(this));
    actionColumn->AddObject(objects[1]);

    portraits = AddChild(std::make_unique<HudPortraits>(this));
    portraits->AddObject(objects[2]);

    unitPanel = AddChild(std::make_unique<HudUnitPanel>(this));
    unitPanel->AddObject(objects[2]);
    unitPanel->AddObject(objects[1]);
    unitPanel->Setup(g_interface.hudBodyPart, g_interface.hudPanelsIn[0] != 0);

    map = AddChild(std::make_unique<HudMap>(this));
    map->AddObject(objects[2]);
    map->Setup(mapZoom, g_interface.hudPanelsIn[2] != 0);

    clock = AddChild(std::make_unique<HudClock>());
    clock->AddObject(objects[2]);
    clock->Setup(g_interface.fastGame != 0);

    movePanel = AddChild(std::make_unique<HudMovePanel>(this));
    movePanel->AddObject(objects[2]);
    movePanel->Build();

    // The message log.
    messageLog = AddChild(std::make_unique<UiScrollText>("", Rect{0xBE, 0x19, 0x262, 0x59}, 3.0f));
    messageLog->align = 1;

    topPanel = AddChild(std::make_unique<HudTopPanel>(this));
    topPanel->AddObject(objects[2]);
    topPanel->Setup(g_interface.hudTopPage, g_interface.hudPanelsIn[1] != 0);
}

void BattleScreen::LayoutChanged() {
    // What the panels hold that outlives them: the map's zoom, the top panel's texts, the clock's blink, dial and
    // highlight, the pending action. The slides, the unit panel's body part, the top panel's page and the clock's
    // speed the interface keeps every frame. The clock and the movement panel show their state at once (a window
    // over the battle screen keeps its Update from running: the backdrop taken again shows them).
    const float mapZoom = map->zoom;
    const float blink = clock->blink, dial = clock->dial;
    const bool highlight = clock->highlight;
    const std::string pageTexts[2] = {topPanel->pageTexts[0], topPanel->pageTexts[1]};
    const PendingAction mode = pendingAction;
    const int slot = pendingSlot, item = pendingItem;
    for (const auto& child : children) child->Deactivate();
    children.clear();   // a child (or its part) that took the mouse lets it go (~UiWindow)
    BuildPanels(mapZoom);
    topPanel->pageTexts[0] = pageTexts[0];
    topPanel->pageTexts[1] = pageTexts[1];
    if (topPanel->page == 0) messageLog->AddText(pageTexts[0]);
    topPanel->ShowItems();
    if (!IsMultiplayerClient()) portraits->Rebuild(false);
    RefreshSelection();
    SetPendingAction(mode, slot, item);
    clock->blink = blink;
    clock->dial = dial;
    clock->highlight = highlight;
    clock->Show();
    movePanel->Update();
}

void BattleScreen::DrawSprites(SceneRenderer* pass) {
    g_clientWorld.Render();
    if (boxSelecting) {    // the selection box, a dark rectangle with an outline
        pass->DrawRectangle(selectionBox.left, selectionBox.top, selectionBox.right,
                                                            selectionBox.bottom, 0x40, 1);
    }
    UiEvent move{.kind = UiEventKind::MouseMove, .at = g_cursor.position};
    OnEvent(move);
    // The depth buffer is cleared before the panels draw.
    ClearUiDepth(1.0f);
    // "nogui" shows or hides the panels (Rebirth's AddNewConsoleCommands).
    console::ApplyAddonGuiVisibility(this);
    UiContainer::DrawSprites(pass);
}

// --- Closing, the first poll, picked-up items, a unit leaving the party and the saved state ------------

void BattleScreen::CloseScreen() {
    g_camera.flags |= Camera::kFree;
    app::g_updatePeriod = kDefaultUpdatePeriod;
    g_cursor.SetHeld(0);
    g_clientWorld.sounds->StopAll(true);
    g_clientWorld.terrain->StopAmbience();
    party.clear();
    g_interface.battle = nullptr;
}

bool BattleScreen::ReadyToStopPolling() {
    if (hasHero) {
        auto& currentParty = party;
        if (!currentParty.empty()) g_camera.SetTarget(currentParty[0]);
        if (AutoSaveEnabled()) ::QuickSave(kAutoSaveName, true);
        hasHero = false;
        g_interface.OpenHelp(false);
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
        if (topPanel->page == 0) messageLog->SetText("");
        return;
    }
    auto& currentParty = party;
    const auto member = std::find(currentParty.begin(), currentParty.end(), unit);
    if (member == currentParty.end()) return;
    currentParty.erase(member);
    portraits->Rebuild(false);
    RefreshSelection();
}

void BattleScreen::SaveState() {
    ByteStream* stream = &g_saveStream;
    auto& currentParty = party;
    // The ids go through an array first; the bytes written are the same.
    int count = 0;
    for (const Unit* member : currentParty)
        if (member->flags & MapObjectFlag::Selected) ++count;
    stream->WriteValue(count);
    for (auto* unit : currentParty) {
        if (unit->flags & MapObjectFlag::Selected) stream->WriteId(unit);
    }
    stream->WriteValue(map->zoom);
    stream->WriteValue(g_interface.serverFlag);
    stream->WriteValue(LocalPlayerData()->money);    // the party's +0x50 (Create reads it back and drops it)
    stream->WriteString(topPanel->pageTexts[0]);
    stream->WriteValue(exitPending);
    stream->WriteValue(pendingExit);
}

void BattleScreen::MarkPortrait(int index) {
    auto* currentPortraits = portraits;
    currentPortraits->slots[index].frame->visible = true;
    const Vector3 axis = {-1.0f, 0.0f, 0.0f};
    currentPortraits->slots[index].restB = Quaternion::AxisAngle(axis, SelectedFaceAngle());
}

void BattleScreen::UnitJoined(Unit* unit) {
    if (IsMultiplayerClient()) {
        topPanel->pageTexts[0].clear();
        if (topPanel->page == 0) messageLog->SetText("");
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
        currentParty.insert(currentParty.begin() + place, unit);
        selected = player->members[record].selected != 0;
    } else {
        currentParty.push_back(unit);
    }
    portraits->Rebuild(false);
    if (selected) {
        g_clientWorld.Select(unit);
        MarkPortrait(place);
    }
    // A hero the save had selected.
    for (int i = 0; i < std::ssize(heroes); ++i) {
        if (static_cast<int>(heroes[i]) != unit->id) continue;
        heroes.erase(heroes.begin() + i);
        g_clientWorld.Select(unit);
        MarkPortrait(place);
        break;
    }
    RefreshSelection();
}

void BattleScreen::MessageResult(std::uint32_t result) {
    switch (static_cast<std::uint32_t>(result)) {
    case kLeaveZoneYes:
        g_gameFlow.LeaveThroughExit(pendingExit);
        break;
    case kGameOverLoad: {
        g_interface.PushWindow(std::make_unique<SaveLoadWindow>(false, false), WindowBackdrop::FirstWindow);
        break;
    }
    case kGameOverQuit:
    case kLeaveAnswer:
    case kLoadWindowClosed:
        g_interface.closeAll = true;
        g_gameFlow.DropSessions();
        g_gameFlow.CloseSessions();
        break;
    default:
        break;
    }
}

void BattleScreen::GameOver(bool won) {
    g_clientWorld.sounds->StopAll(true);
    g_clientWorld.terrain->StopAmbience();
    std::unique_ptr<MessageWindow> box;
    if (!won) {
        g_interface.PlayInterfaceSound("buttons\\gameover.wav");
        box = std::make_unique<MessageWindow>("game_over", "game_over_msg", MessageQuestion::Second, MessageButtons::OkCancel);
    } else {
        box = std::make_unique<MessageWindow>("end_of_game", "end_of_game_msg", MessageQuestion::Exit, MessageButtons::OkCancel);
    }
    g_interface.PushWindow(std::move(box), WindowBackdrop::FirstWindow);
}

// --- the battle screen's update ------------------------------------------------------------------

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
    g_interface.ShowMessage(false);

    // A zone exit saved with the game: once it can be taken, the "leave_zone" question.
    if (exitPending) {
        // The local player's unit records (0x784 bytes each) and the ones in the exit.
        std::vector<PartyUnitRecord*> records = {};
        std::vector<PartyUnitRecord*> second = {};
        Party* data = LocalPlayerData();
        for (auto& member : data->members)
            records.push_back(&member);
        if (g_clientWorld.PartyInExit(records, pendingExit, &second)) {
            g_interface.PushWindow(std::make_unique<MessageWindow>("leave_zone", "leave_zone_msg", MessageQuestion::First, MessageButtons::OkCancel), WindowBackdrop::FirstWindow);
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
        if (sound) g_interface.PlayInterfaceSound(sound);
        questSound = 0;
    }
}

// --- the party ids compared ----------------------------------------------------------------------

bool BattleScreen::PartyIds::Equals(const BattleScreen::PartyIds& other) const {
    return withUnit == other.withUnit && without == other.without;
}

// --- the battle screen's left button release -----------------------------------------------------

void BattleScreen::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::MouseMove: MouseMove(event); break;
    case UiEventKind::LeftDouble: LeftDouble(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    case UiEventKind::KeyUp: KeyUp(event); break;
    case UiEventKind::Wheel: Wheel(event); break;
    default: UiContainer::OnEvent(event); break;
    }
}

void BattleScreen::LeftUp(UiEvent& event) {
    if (portraitMode != 0) return;
    if (!viewPressed) {
        UiContainer::OnEvent(event);
        return;
    }
    viewPressed = false;
    WorldClient* world = &g_clientWorld;
    if (!boxSelecting || IsMultiplayerClient()) {
        Unit* unit = world->UnitAt(event.at);
        if (unit && !(unit->flags & MapObjectFlag::InParty)) unit = nullptr;
        SelectUnit(unit, event.at);
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
            // Portrait i's frame (shown while selected) and face state.
            auto slot = [&](int i) -> HudPortraitSlot& { return portraits->slots[i]; };
            for (int i = 0; i < std::ssize(*currentParty); ++i) {
                Unit* unit = unitAt(i);
                std::uint32_t unitFlags = unit->flags;
                if (!(unitFlags & MapObjectFlag::Selected) && (unitFlags & MapObjectFlag::BoxMarked)) {
                    unit->flags = unitFlags & ~MapObjectFlag::BoxMarked;
                    unit->AsObject3D()->attached->SetLit(false);
                }
                slot(i).frame->visible = false;
                slot(i).restB = kHudRestRotation;
            }
            for (auto& node : inside) {
                int i = static_cast<int>(node);
                slot(i).frame->visible = true;
                const Vector3 axis = {-1.0f, 0.0f, 0.0f};
                const float angle = kHudQuarterTurn *
                                    0.14285715f;   // 1/7
                Quaternion turn = Quaternion::AxisAngle(axis, angle);
                slot(i).restB = turn;
                world->Select(unitAt(static_cast<int>(node)));
            }
            int unitId = unitAt(first)->id;
            Speech(unitId, AckKind::Selected, {});
        }
    }
    RefreshSelection();
    boxSelecting = false;
}

// --- a quest variable's notice -------------------------------------------------------------------

void BattleScreen::QuestVariable(std::string_view name, float value) {
    TextManager* texts = g_clientWorld.texts.get();
    std::vector<std::string> parts;
    std::string text;
    std::string title;
    parts = text::Split(name, '.');
    const float complete = 2.0f;
    const float failed = 3.0f;
    if (parts.size() == 3) {
        const char* key;
        if (value == complete) {
            if (questSound <= 3) questSound = 3;
            key = "combat_complete_quest";
        } else if (value == failed) {
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
            if (current == 0.0f) return;
        }
        const char* key;
        if (value == complete) {
            key = "combat_complete_subobj";
        } else if (value == failed) {
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
    text += "\007C\344\327\247\n";   // colour code and a line end

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
    SetPendingAction(PendingAction::None, -1, -1);
}

// --- the battle screen loses the top -------------------------------------------------------------

void BattleScreen::Deactivate() {
    for (std::uint8_t& key : orderKeys) key = 0;
    viewPressed = false;
    boxSelecting = false;
    UiContainer::Deactivate();
}

// --- a double click on the battle screen ---------------------------------------------------------

void BattleScreen::LeftDouble(UiEvent& event) {
    if (portraitMode != 0) return;
    if (!viewPressed) {
        UiContainer::OnEvent(event);
        if (event.handled) return;
    }
    viewPressed = true;
    press = event.at;
    boxSelecting = false;
}

// --- a key released on the battle screen ---------------------------------------------------------

void BattleScreen::KeyUp(UiEvent& event) {
    if (portraitMode != 0) return;
    UiContainer::OnEvent(event);
    int action = g_keyActions[event.scan] - kFirstOrderKeyAction;
    if (action >= 0 && action < 6) orderKeys[kOrderKeyOfAction[action]] = 0;
}

// --- the wheel on the battle screen --------------------------------------------------------------

void BattleScreen::Wheel(UiEvent& event) {
    if (portraitMode != 0) return;
    UiWindow* const panels[4] = {map, unitPanel, messageLog, topPanel};
    for (UiWindow* panel : panels) {
        panel->OnEvent(event);
        if (event.handled) return;
    }
    for (UiWindow* panel : panels) panel->SetEnabled(false);
    UiContainer::OnEvent(event);
    for (UiWindow* panel : panels) panel->SetEnabled(true);
}

// --- Key down ----------------------------------------------------------------------------------------

// The battle screen's key-down handler: the chat line's keys on a
// network client, the panels' own keys, Esc (the pending action dropped, else the escape menu) and
// the key actions of the key table (g_keyActions): pause and game speed, the objectives window, the
// camera, the order keys, movement modes, the action, spell, weapon and quick item buttons, the
// unit panel's body parts, the top panel's pages, the map's slide, quick save / load, the party
// members and the message window. Rebirth's MPSpeedControl (the two JNEs
// aimed at the next instruction) lets the speed keys work on a network client too. See ui_camp.h
// (BattleScreen) and docs/notes/ui.md, section 6.

void BattleScreen::KeyDown(UiEvent& event) {
    if (portraitMode != 0) {
        if (event.key == platform::Key::Escape) portraitMode = 0;
        return;
    }
    if (event.key == platform::Key::Return) {
        if (IsMultiplayerClient()) {
            g_interface.ChatKeyEnter();
            event.handled = true;
            return;
        }
    } else if (event.key == platform::Key::Backspace) {
        if (IsMultiplayerClient()) {
            g_interface.ChatKeyBackspace();
            event.handled = true;
            return;
        }
    }
    UiContainer::OnEvent(event);
    if (event.handled) return;
    if (event.repeat) return;

    if (event.key == platform::Key::Escape) {
        if (pendingAction != PendingAction::None) {
            // The pending action is dropped with every panel's highlight.
            SetPendingAction(PendingAction::None, -1, -1);
            return;
        }
        g_interface.PushWindow(std::make_unique<EscMenu>(), WindowBackdrop::FirstWindow);
        event.handled = true;
        return;
    }

    const int action = g_keyActions[event.scan];
    Camera* camera = &g_camera;
    switch (action) {
    case KeyAction::Pause: {
        clock->TogglePause();
        event.handled = true;
        return;
    }
    case KeyAction::NormalSpeed:
    case KeyAction::FastSpeed: {
        if (g_clientWorld.paused) return;
        clock->SetSpeed(action == KeyAction::FastSpeed);
        return;
    }
    case KeyAction::Objectives:
        clock->OpenObjectives();
        event.handled = true;
        return;
    case KeyAction::CameraToUnit:
        // The camera follows the first selected member, or nobody.
        for (Unit* member : party) {
            if (member->flags & MapObjectFlag::Selected) {
                camera->SetTarget(member);
                return;
            }
        }
        camera->SetTargetId(-1);
        return;
    case KeyAction::CameraNorth:
        map->FaceNorth();
        return;
    case KeyAction::OrderKey:
    case KeyAction::OrderKey + 1:
    case KeyAction::OrderKey + 2:
    case KeyAction::OrderKey + 3:
    case KeyAction::OrderKey + 4:
    case KeyAction::OrderKey + 5:
        orderKeys[kOrderKeyOfAction[action - KeyAction::OrderKey]] = 1;
        return;
    case KeyAction::MoveMode:
    case KeyAction::MoveMode + 1:
    case KeyAction::MoveMode + 2:
    case KeyAction::MoveMode + 3: {
        movePanel->SetMode(action - KeyAction::MoveMode);
        event.handled = true;
        return;
    }
    case KeyAction::MoveFlag:
        movePanel->ToggleFlag();
        event.handled = true;
        return;
    case KeyAction::Follow:
        actionColumn->Select(kFollowButton);
        return;
    case KeyAction::Science:
        actionColumn->Select(kScienceButton);
        return;
    case KeyAction::Spell:
    case KeyAction::Spell + 1:
    case KeyAction::Spell + 2:
    case KeyAction::Spell + 3:
    case KeyAction::Spell + 4:
    case KeyAction::Spell + 5:
    case KeyAction::Spell + 6:
    case KeyAction::Spell + 7:
        // With Ctrl or Alt the spell is cast at once.
        if (!g_interface.ctrlDown && !g_interface.altDown)
            spellColumn->Click(action - KeyAction::Spell);
        else
            spellColumn->ClickAlternate(action - KeyAction::Spell);
        return;
    case KeyAction::Weapon:
    case KeyAction::Weapon + 1:
    case KeyAction::Weapon + 2:
    case KeyAction::Weapon + 3:
        weaponBar->Choose(action - KeyAction::Weapon);
        return;
    case KeyAction::QuickItem:
    case KeyAction::QuickItem + 1:
    case KeyAction::QuickItem + 2:
    case KeyAction::QuickItem + 3:
        // With Ctrl or Alt the item is used at once.
        if (!g_interface.ctrlDown && !g_interface.altDown)
            quickItems->Pick(action - KeyAction::QuickItem);
        else
            quickItems->UseNow(action - KeyAction::QuickItem);
        return;
    case KeyAction::BodyPart:
    case KeyAction::BodyPart + 1:
    case KeyAction::BodyPart + 2:
    case KeyAction::BodyPart + 3: {
        auto* panel = unitPanel;
        SlideKey(panel, panel->bodyPart, action - KeyAction::BodyPart,
                 [](HudUnitPanel* shown) { shown->ShowBodyPart(); });
        return;
    }
    case KeyAction::Page:
    case KeyAction::Page + 1: {
        auto* panel = topPanel;
        SlideKey(panel, panel->page, action - KeyAction::Page, [](HudTopPanel* shown) { shown->ShowPage(); });
        return;
    }
    case KeyAction::MapSlide:
        map->slide.Toggle();
        return;
    case KeyAction::QuickSave:
        ::QuickSave(kQuickSaveName, false);
        return;
    case KeyAction::QuickLoad:
        QuickLoad(kQuickSaveName);
        return;
    case KeyAction::Member:
    case KeyAction::Member + 1:
    case KeyAction::Member + 2: {
        const int member = action - KeyAction::Member;
        g_interface.PlayInterfaceSound(kSoundOnOff);
        ToggleMember(member);
        // With Ctrl or Alt the camera follows the member.
        if ((g_interface.ctrlDown || g_interface.altDown) && std::ssize(party) > member)
            camera->SetTarget(party[member]);
        return;
    }
    case KeyAction::SelectAll:
        if (IsMultiplayerClient()) return;
        g_interface.PlayInterfaceSound(kSoundOnOff);
        for (int i = 0; i < std::ssize(party); ++i) {
            MarkPortrait(i);
            if (Unit* unit = party[i]) g_clientWorld.Select(unit);
        }
        if (!party.empty() && party[0]) Speech(party[0]->id, AckKind::Selected, {});
        RefreshSelection();
        return;
    case KeyAction::Message:
        g_interface.ShowMessage(true);
        event.handled = true;
        return;
    default:
        return;
    }
}

// --- Mouse move and left button ----------------------------------------------------------------------

// The battle screen's mouse move, left button down and a unit's name line. See ui_battle.h (BattleScreen) and
// docs/notes/ui.md ("Battle screen").

// --- the mouse moved over the battle screen ------------------------------------------------------

void BattleScreen::MouseMove(UiEvent& event) {
    if (portraitMode != 0) {
        g_cursor.SetShape(CursorShape::Cancel);
        if (portraits) portraits->OnEvent(event);
        return;
    }
    g_cursor.SetShape(CursorShape::Default);
    if (viewPressed && options::g_options.cameraFrameSelectionArea > 0 && !IsMultiplayerClient()) {
        int threshold = options::g_options.cameraFrameSelectionArea;
        if (Distance(event.at.x, press.x) > threshold || Distance(event.at.y, press.y) > threshold) boxSelecting = true;
        if (boxSelecting) {
            HudUnitPanel* panel = unitPanel;
            if (panel->hoverUnit) panel->hoverUnit = nullptr;
            selectionBox = Rect::FromCorners(Min(event.at, press), Max(event.at, press));
            return;
        }
    } else {
        UiContainer::OnEvent(event);
        if (event.handled) return;
    }
    Unit* unit = g_clientWorld.UnitAt(event.at);
    HoverUnit(unit, event.at);
}

// --- the left button went down on the battle screen ----------------------------------------------

void BattleScreen::LeftDown(UiEvent& event) {
    if (portraitMode != 0) {
        if (portraits) portraits->OnEvent(event);
        return;
    }
    if (!viewPressed) {
        UiContainer::OnEvent(event);
        if (event.handled) return;
    }
    viewPressed = true;
    press = event.at;
    boxSelecting = false;
}

// --- a unit's name line --------------------------------------------------------------------------

// Each text is copied up to its first NUL.
std::string BattleScreen::UnitName(Unit* unit) {
    TextManager* texts = g_clientWorld.texts.get();
    std::string name(unit->GetName());
    const auto upToNull = [](const std::string& text) { return text.substr(0, text.find('\0')); };
    if (name.size() != 0) return upToNull(name);
    if (IsTextKey(unit->id)) {
        auto node = personTexts.find(unit->id);
        return node == personTexts.end() ? std::string() : upToNull(node->second);
    }
    name = unit->GetPrototype()->name;
    text::ReplaceChar(&name, ' ', '_');
    if (!texts->Open(std::string(kUnitTextPrefix) + name)) return {};
    std::string line = texts->ReadLine(true);
    return upToNull(line);
}

// --- Mouse-over --------------------------------------------------------------------------------------

// The battle screen's mouse-over: the unit under the mouse in the unit panel and the
// cursor shape for what a click would do. See ui_battle.h (BattleScreen::HoverUnit) and docs/notes/ui.md
// ("Battle screen").

void BattleScreen::HoverUnit(Unit* unit, Point mouse) {
    auto unitDead = [](Unit* u) { return u->IsDead(); };
    auto pick = [mouse]() { return g_clientWorld.PickSurface(mouse.x, mouse.y); };
    auto objectAt = [mouse]() { return g_clientWorld.Pick(mouse, HitTest::Parts); };
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

    CursorShape shape = CursorShape::Default;
    if (g_interface.altDown) {
        Vector3 at = pick();
        if (!g_clientWorld.FindExitAt(at)) return;
        g_cursor.SetShape(CursorShape::Move);
        return;
    }
    for (int i = 0; i < 6; ++i) {
        if (orderKeys[i]) {
            g_cursor.SetShape(static_cast<CursorShape>(static_cast<int>(CursorShape::AttackHead) + i));
            return;
        }
    }
    if (g_interface.ctrlDown) {
        g_cursor.SetShape(CursorShape::Attack);
        return;
    }

    switch (pendingAction) {
    case PendingAction::None:
        if (unit) {
            if (unitDead(unit)) {
                shape = CursorShape::Use;
                break;
            }
            if (party.empty() || !party[0]) return;
            if (!party[0]->IsHostileTo(unit)) return;
            shape = CursorShape::Attack;
            break;
        }
        {
            MapObject* object = objectAt();
            if (object && object->IsUsable() != 0 && object->IsDirectUseBlocked() == 0 &&
                selected == 1) {
                shape = CursorShape::Use;
                break;
            }
            // Over a zone exit: the exit cursor unless the zone's "z.<name>" variable is 1 ("none"
            // is asked as is).
            Vector3 at = pick();
            std::optional<int> exit = g_clientWorld.FindExitAt(at);
            if (!exit) return;
            std::string name = g_clientWorld.ExitTarget(*exit);
            if (text::CompareIgnoringCase(name,
                                "none") != 0) {
                name = std::string("z.") + name;
                float value = LocalPlayerVariable(name);
                const float one = 1.0f;
                // equal skips.
                if (value != one) g_cursor.SetShape(CursorShape::Move);
            }
            return;
        }
    case PendingAction::SpellOnUnit: {
        if (!unit || unitDead(unit)) {
            shape = CursorShape::SpellCancel;
            break;
        }
        if (!first || !first->CanCastSpell(pendingSlot)) {
            shape = CursorShape::SpellCancel;
            break;
        }
        shape = CursorShape::Spell;
        break;
    }
    case PendingAction::SpellAtPoint:
    case PendingAction::QuickAtPoint: {
        Vector3 at = (unit && !unitDead(unit)) ? unitPosition(unit) : pick();
        if ((IsZero(at.x) && IsZero(at.y)) || !first) {
            shape = CursorShape::SpellCancel;
            break;
        }
        const bool able = pendingAction == PendingAction::SpellAtPoint ? first->CanCastSpell(pendingSlot)
                                                                 : first->CanUseQuickItem(pendingSlot);
        shape = able ? CursorShape::Spell : CursorShape::SpellCancel;
        break;
    }
    case PendingAction::QuickOnUnit: {   // a potion only on the user itself
        shape = CursorShape::SpellCancel;
        if (!unit || unitDead(unit)) break;
        if (!first || !first->CanUseQuickItem(pendingSlot)) break;
        Item* item = first->GetQuickItem(pendingSlot);
        if (item && item->GetCategory() == QuickItemCategory::Potion && unit != first) break;
        shape = CursorShape::Spell;
        break;
    }
    case PendingAction::ItemOnObject: {
        MapObject* target = unit ? unit : objectAt();
        shape = (target && first && target->IsUsable() != 0) ? CursorShape::Use : CursorShape::Cancel;
        break;
    }
    case PendingAction::KeyOnObject: {
        MapObject* target = unit ? unit : objectAt();
        shape = (target && first && target->IsUsable() != 0 && target->AcceptsKey(&itemUseKey) != 0)
                    ? CursorShape::Use
                    : CursorShape::Cancel;
        break;
    }
    case PendingAction::Follow:
        shape = (first && unit && !unitDead(unit)) ? CursorShape::Move : CursorShape::Cancel;
        break;
    case PendingAction::Order: {
        if (unit) {
            shape = CursorShape::Cancel;
            if (!first) break;
            bool member = false;
            for (int i = 0; i < count; ++i) member |= unit == party[i];
            if (!member) shape = CursorShape::Steal;
            break;
        }
        MapObject* target = objectAt();
        shape = (target && first && target->IsUsable() != 0) ? CursorShape::Use : CursorShape::Cancel;
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

// --- a party member toggled ----------------------------------------------------------------------

void BattleScreen::ToggleMember(int index) {
    auto* currentParty = &party;
    auto unitAt = [currentParty](int i) { return (*currentParty)[i]; };
    if (index >= std::ssize(*currentParty)) return;
    // A multiplayer client toggles only its hero and its companions, the first 1 + companionPortraits.
    if (IsMultiplayerClient() && (companionPortraits == 0 || index > companionPortraits)) return;

    std::vector<std::uint32_t> chosen{};
    for (int i = 0; i < std::ssize(*currentParty); ++i) {
        Unit* member = unitAt(i);
        chosen.push_back(static_cast<std::uint32_t>(member ? (member->flags & MapObjectFlag::Selected) : 0));
    }
    if (g_interface.shiftDown) {
        chosen[index] = chosen[index] == 0 ? 1u : 0u;
    } else {
        for (auto& flag : chosen)
            if (flag != 0) flag = 0;
        chosen[index] = 1;
    }
    g_clientWorld.Deselect();

    for (int i = 0; i < std::ssize(*currentParty); ++i) {
        auto* currentPortraits = portraits;
        std::uint32_t value = chosen[i];
        currentPortraits->slots[i].frame->visible = value != 0;
        Quaternion rest = kHudRestRotation;
        if (value != 0) {
            const Vector3 axis = {-1.0f, 0.0f, 0.0f};
            const float angle = kHudQuarterTurn *
                                0.14285715f;   // 1/7
            rest = Quaternion::AxisAngle(axis, angle);
        }
        currentPortraits->slots[i].restB = rest;
        Unit* member = unitAt(i);
        if (member && chosen[i] != 0) {
            g_clientWorld.Select(member);
            if (index == i) Speech(unitAt(i)->id, AckKind::Selected, {});
        }
    }

    // Single player: the local player's unit records (0x784 bytes, +0x1C the unit id) keep the
    // members' selection, record i against member i.
    if (g_serverWorld.gameState == GameState::Playing) {
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
        if (target->Level() - member->Level() > 5) Speech(member->id, AckKind::BigAttack, {});
    }
    if (!units.empty()) g_clientWorld.LocalView()->SendAttack(units, target, kind, ctrl);
}

void BattleScreen::OrderDeadUnit(Unit* target, bool ctrl) {
    std::vector<Unit*> units;
    for (int i : SelectedIndexes()) {
        units.push_back(party[i]);
        portraits->Wobble(i, kWobbleNod);
    }
    if (!units.empty()) g_clientWorld.LocalView()->SendUseDead(units, target, ctrl);
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
    SetPendingAction(PendingAction::None, -1, -1);
}

// The shift below stays for the later members.
void BattleScreen::UseAtPoint(Vector3 point, Unit* unit, bool quick) {
    for (int i : SelectedIndexes()) {
        Unit* member = party[i];
        if (member == unit) {
            Vector3 axis = unit->localRotation.AxisY();
            const float k = 0.1f;
            point += -axis * k;
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
    SetPendingAction(PendingAction::None, -1, -1);
}

// --- a unit's speech event -----------------------------------------------------------------------

void BattleScreen::Speech(int unitId, AckKind kind, std::string_view extra) {
    Unit* unit = UnitWithId(g_clientWorld.objectManager.get(), unitId);
    if (!unit) return;
    const SpeechRecord* phrase = (kind == AckKind::Scenario || kind == AckKind::ScenarioTalking)
                                     ? acks::SayNamedAck(unit, extra, kind == AckKind::ScenarioTalking)
                                     : acks::SayAck(unit, kind);
    if (phrase) {
        // The phrase's text.
        std::string text;
        std::string line = UnitName(unit) + ":\n";
        TextManager* texts = g_clientWorld.texts.get();
        if (texts->Open(std::string("say ") + phrase->name.data())) {
            text = texts->ReadToHash();
            str::TrimRight(text, "\r\n");
            text += "\n";
            // Both go to the top panel's page 0 and, while it shows that page, the message log.
            auto post = [this](std::string_view part) {
                topPanel->pageTexts[0] += part;
                if (topPanel->page == 0) messageLog->AddText(part);
            };
            post(line);
            post(text);
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

// --- BattleScreen::SelectUnit --------------------------------------------------------

void BattleScreen::SelectUnit(Unit* unit, Point mouse) {
    const bool ctrl = g_interface.doubleClick;
    auto unitDead = [](Unit* u) { return u->IsDead(); };
    auto pick = [mouse]() { return g_clientWorld.PickSurface(mouse.x, mouse.y); };
    auto unitPosition = [](Unit* u) { return u->localPosition; };
    auto objectAt = [mouse]() { return g_clientWorld.Pick(mouse, HitTest::Parts); };
    // The portrait of member `index` nods after a short random delay.
    auto flash = [this](int index) { portraits->Wobble(index, kWobbleNod); };
    auto order = [this, ctrl](Unit* first, MapObject* target, int kind, int value) {
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
            g_interface.PlayInterfaceSound("buttons\\battle\\on_off.wav");
            if (g_interface.doubleClick) {
                if (i < std::ssize(party)) g_camera.SetTarget(party[i]);
            } else {
                ToggleMember(i);
            }
        }
        return;
    }

    if (g_interface.altDown) {
        OrderAt(unit ? unitPosition(unit) : pick(), false, ctrl);
        return;
    }
    bool anyFlag = false;
    for (int i = 0; i < 6 && !anyFlag; ++i) anyFlag = orderKeys[i] != 0;
    if (anyFlag || g_interface.ctrlDown) {
        if (unit && !unitDead(unit)) {
            AttackUnit(unit, ctrl);
            return;
        }
        OrderAt(pick(), true, ctrl);
        return;
    }

    switch (pendingAction) {
    case PendingAction::None:
        if (unit) {
            if (unitDead(unit)) {
                OrderDeadUnit(unit, ctrl);
                return;
            }
            int members = lib::Count(party);
            for (int i = 0; i < members; ++i) {
                if (unit != party[i]) continue;
                g_interface.PlayInterfaceSound("buttons\\battle\\on_off.wav");
                if (g_interface.doubleClick) {
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
        if (MapObject* object = objectAt()) {
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
            console::EndLine(console::g_graphicsLogOut << "Terrain Coords " << at.x << " " << at.y
                                                      << " " << at.z);
        }
        return;
    case PendingAction::SpellOnUnit:
        if (!unit || unitDead(unit) || !first) return;
        // `quick` false (Rebirth's FixSpellSelfCasting; the original passed the Ctrl flag).
        UseOnUnit(unit, false, false);
        return;
    case PendingAction::SpellAtPoint:
    case PendingAction::QuickAtPoint: {
        Vector3 at = (unit && !unitDead(unit)) ? unitPosition(unit) : pick();
        if (IsZero(at.x) && IsZero(at.y)) return;
        if (!first) return;
        UseAtPoint(at, living, pendingAction == PendingAction::QuickAtPoint);
        return;
    }
    case PendingAction::QuickOnUnit: {
        if (!unit || unitDead(unit) || !first) return;
        Item* item = first->GetQuickItem(pendingSlot);
        if (item && item->GetCategory() == QuickItemCategory::Potion && unit != first) return;
        UseOnUnit(unit, true, false);
        return;
    }
    case PendingAction::ItemOnObject:
    case PendingAction::KeyOnObject: {
        MapObject* target = unit ? unit : objectAt();
        if (!target || !first) return;
        if (target->IsUsable() == 0) return;
        if (pendingAction == PendingAction::KeyOnObject && target->AcceptsKey(&itemUseKey) == 0) return;
        order(first, target, 0, pendingItem);
        flash(firstIndex);
        SetPendingAction(PendingAction::None, -1, -1);
        return;
    }
    case PendingAction::Follow: {   // every selected member follows the unit
        if (!unit || unitDead(unit)) return;
        for (int i = 0; i < std::ssize(party); ++i) {
            Unit* member = party[i];
            if (!member || !(member->flags & MapObjectFlag::Selected)) continue;
            g_clientWorld.LocalView()->SendFollow(member, unit);
            Speech(party[i]->id, AckKind::Follow, {});
            portraits->Wobble(i, kWobbleNod);
        }
        SetPendingAction(PendingAction::None, -1, -1);
        return;
    }
    case PendingAction::Order: {   // a unit outside the party, or the object under the mouse
        MapObject* target = unit ? unit : objectAt();
        if (!target || !first) return;
        if (unit) {
            for (const Unit* member : party)
                if (unit == member) return;
            order(first, unit, 1, -1);
        } else {
            if (target->IsUsable() == 0) return;
            order(first, target, 0, -1);
        }
        flash(firstIndex);
        SetPendingAction(PendingAction::None, -1, -1);
        return;
    }
    default:
        return;
    }
}

// --- The selected units ordered to a point -------------------------------------------

void BattleScreen::OrderAt(Vector3 point, bool goTo, bool run) {
    int selected = 0;
    for (Unit* member : party) {
        if (member && (member->flags & MapObjectFlag::Selected)) ++selected;
    }

    // A point on a zone exit that is open ("z.<zone>" not 1; "none" is no zone): remembered for the
    // update to ask about leaving (exitPending and the exit).
    exitPending = false;
    if (std::optional<int> exit = g_clientWorld.FindExitAt(point); exit && selected > 0) {
        std::string name = g_clientWorld.ExitTarget(*exit);
        if (text::CompareIgnoringCase(name,
                            "none") != 0) {
            name = std::string("z.") + name;
            float value = LocalPlayerVariable(name);
            const float one = 1.0f;
            // Equal skips.
            if (value != one) {
                exitPending = true;
                pendingExit = *exit;
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
            view->SendGoTo(units, point, run);
        else
            view->SendWalk(units, point, run);
    }
}

// --- The pending action ------------------------------------------------------------------------

void BattleScreen::SetPendingAction(PendingAction mode, int slot, int item) {
    pendingSlot = slot;
    pendingAction = mode;
    pendingItem = item;
    quickItems->ClearHighlights();
    spellColumn->ClearHighlights();
    actionColumn->ClearHighlights();
    switch (mode) {
    case PendingAction::SpellOnUnit:
    case PendingAction::SpellAtPoint:
        spellColumn->Highlight(slot, true);
        break;
    case PendingAction::QuickOnUnit:
    case PendingAction::QuickAtPoint:
    case PendingAction::ItemOnObject:
    case PendingAction::KeyOnObject:
        quickItems->Highlight(slot, true);
        break;
    case PendingAction::Follow:
    case PendingAction::Order:
        actionColumn->Highlight(slot, true);
        break;
    default:
        break;
    }
}
