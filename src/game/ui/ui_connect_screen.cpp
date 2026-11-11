// The multiplayer connection screen (MpConnectScreen), its master server request box (MasterRequestBox) and the
// address list's vector members. See ui_screens.h and docs/notes/ui.md, section 3. Multiplayer only. Its Create
// reproduces addon's ExtendedServerName sites, as the list draw (ui_connect_list.cpp) does its patches; EI_Plugin's
// NPlayers and Pause are the new game's player slider and pause mode.
#include "game/app/game_flow.h"
#include "game/net/net_session.h"
#include "game/net/mod_identity.h"
#include "game/app/options.h"
#include "engine/core/asset_text.h"
#include "game/ui/ui.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_scrollbar.h"
#include "game/ui/ui_edit_field.h"
#include "game/ui/ui_message_box.h"
#include "game/ui/ui_screens.h"

#include "engine/data/chunk_archive.h"
#include "engine/core/core_transform.h"
#include "engine/core/coder.h"
#include "game/net/net_link.h"
#include "game/net/net_second_master.h"
#include "game/players/party.h"
#include "engine/core/random.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "engine/data/text_manager.h"

#include "lib/crt.h"
#include "platform/platform.h"
#include "lib/intro_sort.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/str.h"
#include "lib/vecmath.h"
#include "game/app/singletons.h"

#include <memory>
#include <new>

namespace net {
net::MasterLink::State MasterLinkTick(net::MasterLink*);
}
namespace {

constexpr float kMenuTilt = /*007C7F40*/ 1.5707964f;   // a static initialiser: pi * 0.5

constexpr const char* kMessageSuffix = "_msg";
constexpr const char* kWrongProtocolKey = "lmp_wrong_protocol";
constexpr const char* kWrongProtocolTextKey = "lmp_wrong_protocol_msg";
constexpr const char* kNewGameText = "??";    // the new game entry's name and description
constexpr const char* kDefaultGameName = "?";
constexpr const char* kAddressBookFile = "config\\addrbook.ini";
const char* RefusalKey(net::RefusalReason refusal) {
    switch (refusal) {
    case net::RefusalReason::Full: return "lmp_server_full";
    case net::RefusalReason::Banned: return "lmp_you_are_banned";
    case net::RefusalReason::WrongPassword: return "lmp_wrong_password";
    case net::RefusalReason::ModMismatch: return "lmp_mod_mismatch";
    default: return "lmp_cannot_connect";
    }
}
constexpr int kMasterPort = 28004;

// The switches' sprite frames: off (0.0), on (-0.08984375); the selected game
// type tab's frame is an immediate in the code.
constexpr float kFrameOff = 0.0f;
constexpr float kFrameOn = -0.08984375f;
constexpr float kRefreshPeriod = 0.5f;   // 0.5 s
constexpr float kMillisecondsPerSecond = 1000.0f;
constexpr float kBarWidth = 355.0f;
constexpr float kBarLeft = 225.0f;
constexpr float kTabSelected = 0.08984375f;               // 0x3DB80000

constexpr int kSurfaceConnecting = 1;
constexpr int kTextPassword = 18, kTextAddress = 19, kTextMaster = 20;
constexpr std::uint32_t kLeaveScreen = 0x1000001;
constexpr int kNoPingMillis = 9999;
constexpr int kNewGameType = 4;
constexpr int kStateAsked = 1;   // net::ServerEntryBase::State::Asked
constexpr std::uint32_t kBarColour = 0xFFA06800;

void CopyText(char* to, std::string_view from) {
    std::memcpy(to, from.data(), from.size());
    to[from.size()] = 0;
}

void OpenBox(std::string_view title, std::string_view text, MessageQuestion question, MessageButtons buttons,
             char* editBuffer = nullptr) {
    g_interface.PushWindow(std::make_unique<MessageWindow>(title, text, question, buttons, editBuffer), WindowBackdrop::FirstWindow);
}


constexpr const char* kSaveLoadTexture = "saveload";
constexpr const char* kScrollbarTexture = "scrollbar";
constexpr const char* kMenuModel = "unmoco2";
constexpr const char* kMenuTexture = "MainMenu00";
constexpr const char* kMenuLabels = "MainMenu00labels";
constexpr const char* kMenuButtonPart = "but03";
constexpr const char* kMenuButtonLabel = "button03_multiplayer";
constexpr const char* kAllodNameFormat = "lmp_allod_name_%d";
constexpr const char* kConnectionTextFormat = "lmp_connection_%d";
constexpr const char* kNameEnd = "|";
float MenuTurn() { return kMenuTilt * 1.2f; }
constexpr std::uint32_t kCaretColour = 0x31B3FF;
// ExtendedServerName's values in Create (addon.dll: operands).
constexpr float kExtendedSelectionRight = 381.5f;
constexpr int kExtendedFieldRight = 680;
constexpr int kExtendedNameLength = 50;
constexpr std::uint32_t kSelectionColour = 0xC8A06800;
constexpr std::uint32_t kBackColour = 0xA0000000;
constexpr int kTextCount = 22;
constexpr int kTextConnecting = 8;
constexpr int kNameLength = 10;
constexpr int kScrollbarObject = 1;   // objects: 0 "saveload", 1 "scrollbar"
constexpr int kDefaultPlayers = 6;    // the player slider's start
constexpr int kPauseModes = static_cast<int>(net::PauseMode::Off) + 1;

} // namespace

void MpConnectScreen::Update() {
    net::ClientSession* client = net::g_gameClient;
    bool asking = client->joinState == net::ClientBase::JoinState::Asking;
    if (asking) {
        surfaces[kSurfaceConnecting]->Show();
    } else {
        surfaces[kSurfaceConnecting]->Hide();
        if (joining) {
            joining = false;
            std::string key = RefusalKey(client->refusal);
            std::string text = key + kMessageSuffix;
            auto box = std::make_unique<MessageWindow>(key, text, MpConnectScreen::kBoxNotice, MessageButtons::Ok);
            if (client->refusal == net::RefusalReason::ModMismatch) {
                // Ours: the two mods named (modidentity::RefusalBox).
                const modidentity::Box shown = modidentity::RefusalBox(client->hostMod);
                box->title = shown.title;
                box->text = shown.text;
            }
            g_interface.PushWindow(std::move(box), WindowBackdrop::FirstWindow);
            return;
        }
    }
    joining = asking;
    UiContainer::Update();

    refreshTimer += FrameTime();
    float period = kRefreshPeriod;
    if (refreshTimer > period) {
        float timer = refreshTimer;
        do {
            timer -= period;
        } while (timer > period);
        refreshTimer = timer;
        net::NetAddress none{};
        const net::NetAddress* ask = &none;
        if (selected >= 1 && static_cast<unsigned>(selected) < entries.size()) ask = &entries[selected].address;
        net::g_gameClient->SetAlwaysAsk(ask);
        net::g_gameClient->Refresh();
        RebuildList();
        DrawListVanilla();
    }

    if (scroll->TakeMoved()) {
        scrollTop = scroll->Position();
        DrawListVanilla();
    }
    if (playersSlider->TakeMoved()) {
        maxPlayers = playersSlider->Position() + 1;
        DrawListVanilla();
    }
    bool renamed = nameEdit->changed;
    nameEdit->changed = false;
    if (renamed) DrawListVanilla();
}

void MpConnectScreen::MessageResult(std::uint32_t result) {
    if (result == BoxResult(kBoxMaster, MessageAnswer::Cancel)) {
        DrawListVanilla();
    } else if (result == BoxResult(kBoxAddress, MessageAnswer::Ok)) {
        net::NetAddress address{};
        if (address.Parse(boxText, net::g_gamePort)) net::g_gameClient->Ask(&address);
        SaveAddressBook();
        DrawListVanilla();
    } else if (result == BoxResult(kBoxPassword, MessageAnswer::Ok) || result == BoxResult(kBoxPassword, MessageAnswer::Cancel)) {
        hostPassword = boxText;
        passwordBox->textureOffsetU = hostPassword.size() == 0 ? kFrameOff : kFrameOn;
    } else if (result == BoxResult(kBoxJoinPassword, MessageAnswer::Ok)) {
        g_gameFlow.JoinServer(&entries[selected].address, boxText);
        net::g_gameClient->gameType = entries[selected].gameType;
        net::g_gameClient->maxPlayers = entries[selected].maxPlayers;
    }
}

// --- input ---------------------------------------------------------------------------------------

void MpConnectScreen::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::LeftDouble: LeftDouble(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    default: UiContainer::OnEvent(event); break;
    }
}

void MpConnectScreen::LeftDown(UiEvent& event) {
    UiRegion* region = RegionUnder(event.at);
    const int column = RegionIndex(sortColumns, region);
    const int tab = RegionIndex(tabAreas, region);
    const int shownRow = RegionIndex(rows, region);
    if (region == connectButton) {
        Connect();
    } else if (region == backButton) {
        g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
        SaveAddressBook();
        g_gameFlow.DropSessions();
        g_interface.SetPendingValue(kLeaveScreen);
    } else if (region == masterButton) {
        MasterRequest();
    } else if (region == addressButton) {
        EditAddress();
    } else if (region == unlistedSwitch) {
        unlisted = !unlisted;
        unlistedBox->textureOffsetU = unlisted ? kFrameOn : kFrameOff;
    } else if (region == passwordSwitch) {
        if (hostPassword.size() == 0) {
            CopyText(boxText, hostPassword);
            OpenBox(texts[kTextPassword], "", kBoxPassword, MessageButtons::OkCancelEdit, boxText);
        } else {
            hostPassword.clear();
            passwordBox->textureOffsetU = 0.0f;
        }
    } else if (column >= 0) {
        // Column c sorts by key 2c + 1, a second click by 2c + 2.
        int ascending = 2 * column + 1;
        sortKey = sortKey == ascending ? ascending + 1 : ascending;
        RebuildList();
        DrawListVanilla();
    } else if (tab >= 0) {
        if (selected == 0) {
            gameType = tab;
            for (UiMesh* box : tabBoxes) box->textureOffsetU = 0.0f;
            tabBoxes[gameType]->textureOffsetU = kTabSelected;
        }
    } else if (region == pauseSwitch) {
        if (selected == 0) pauseMode = static_cast<net::PauseMode>((static_cast<int>(pauseMode) + 1) % kPauseModes);
    } else if (region == companionSwitch) {
        if (selected == 0) companions = companions == 0 ? net::kMostCompanions : 0;
    } else if (shownRow >= 0) {
        unsigned row = static_cast<unsigned>(scrollTop + shownRow);
        if (row < entries.size()) {
            g_interface.PlayInterfaceSound(kSaveSelectSound);
            selected = static_cast<int>(row);
            selectedAddress = entries[row].address;
            DrawListVanilla();
        }
    }
    UiContainer::OnEvent(event);
}

void MpConnectScreen::LeftDouble(UiEvent& event) {
    const int shownRow = RegionIndex(rows, RegionUnder(event.at));
    if (shownRow >= 0) {
        unsigned row = static_cast<unsigned>(scrollTop + shownRow);
        if (row < entries.size()) {
            selected = static_cast<int>(row);
            selectedAddress = entries[row].address;
            Connect();
        }
    }
    UiContainer::OnEvent(event);
}

void MpConnectScreen::KeyDown(UiEvent& event) {
    UiContainer::OnEvent(event);
    int step;
    switch (event.key) {
    case platform::Key::Return:
        Connect();
        event.handled = true;
        return;
    case platform::Key::Escape:
        g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
        g_gameFlow.DropSessions();
        g_interface.SetPendingValue(kLeaveScreen);
        event.handled = true;
        return;
    case platform::Key::Up: step = -1; break;
    case platform::Key::Down: step = 1; break;
    case platform::Key::PageUp: step = -kRows; break;
    case platform::Key::PageDown: step = kRows; break;
    default: return;
    }
    g_interface.PlayInterfaceSound(kSaveSelectSound);
    selected += step;
    SelectAndScroll();
    event.handled = true;
}

void MpConnectScreen::SelectAndScroll() {
    if (selected < 0) selected = 0;
    if (static_cast<unsigned>(selected) >= entries.size()) selected = static_cast<int>(entries.size()) - 1;
    int row = selected;
    selectedAddress = entries[row].address;
    if (!(scrollTop + kRows > row)) scrollTop = row - (kRows - 1);
    if (scrollTop > row) scrollTop = row;
    scroll->SetPosition(scrollTop);
    scrollTop = scroll->TakePosition();
    DrawListVanilla();
}

void MpConnectScreen::Connect() {
    g_interface.PlayInterfaceSound(kMessageBoxOkSound);
    int row = selected;
    if (row == 0) {
        const int players = playersSlider->TakePosition() + 1;
        std::string name = nameEdit->text;
        if (name.size() == 0) name = kDefaultGameName;
        SaveAddressBook();
        g_gameFlow.StartServer(name, players, pauseMode, companions, gameType, hostPassword, unlisted,
                               static_cast<int>(net::g_gameClient->queries.extra));
        return;
    }
    if (row < 0 || static_cast<unsigned>(row) >= entries.size()) return;
    if (entries[row].foreign) {
        OpenBox(kWrongProtocolKey, kWrongProtocolTextKey, kBoxNotice, MessageButtons::Ok);
        return;
    }
    SaveAddressBook();
    if (entries[selected].password) {
        boxText[0] = '\0';
        OpenBox(texts[kTextPassword], "", kBoxJoinPassword, MessageButtons::OkCancelEdit, boxText);
    }
    g_gameFlow.JoinServer(&entries[selected].address, "");
    net::g_gameClient->gameType = entries[selected].gameType;
    net::g_gameClient->maxPlayers = entries[selected].maxPlayers;
}

void MpConnectScreen::EditAddress() {
    int row = selected;
    if (row < 0 || static_cast<unsigned>(row) >= entries.size()) return;
    MpAddressEntry& entry = entries[row];
    if (entry.state == kStateAsked) {
        net::g_gameClient->Forget(&entry.address);
        SaveAddressBook();
        return;
    }
    boxText[0] = '\0';
    if (row > 0 && static_cast<unsigned>(row) < entries.size()) {
        std::string text = entry.address.ToString();
        CopyText(boxText, text.data());
    }
    OpenBox(texts[kTextAddress], "", kBoxAddress, MessageButtons::OkCancelEdit, boxText);
}

void MpConnectScreen::MasterRequest() {
    CopyText(boxText, "");
    net::NetAddress master{};
    // EI_Plugin's NewMaster: with a second master set, the master is tried first.
    if (net::second_master::Configured())
        net::second_master::ParseMaster(&master, options::g_options.masterServerName, kMasterPort);
    else
        master.Parse(options::g_options.masterServerName, kMasterPort);
    net::ClientSession* client = net::g_gameClient;
    std::uint32_t extra = client ? client->queries.extra : 0;
    g_interface.PushWindow(std::make_unique<MasterRequestBox>(texts[kTextMaster], std::string_view(boxText), kBoxMaster, &master, extra),
                   WindowBackdrop::FirstWindow);
}

void MpConnectScreen::SaveAddressBook() {
    ChunkArchive archive;
    FileStream file;
    if (!file.Open(g_dataFolder + kAddressBookFile, platform::FileMode::Create)) return;
    archive.Open(&file, 0, 1);
    net::ClientQueries* queries = &net::g_gameClient->queries;
    if (archive.EnterChunk(1)) {
        queries->Serialize(&archive);
        archive.LeaveChunk();
    }
    archive.Close(&file);
    file.Close();
}

void MpConnectScreen::RebuildList() {
    entries.clear();
    for (const auto& listed : net::g_gameClient->servers) {
        const net::ServerEntry* server = listed.Get();
        MpAddressEntry& entry = entries.emplace_back();
        entry.address = server->address;
        entry.gameType = static_cast<int>(server->kind);
        if (entry.gameType < 0 || entry.gameType > 3) entry.gameType = 0;
        entry.name = server->name.data();
        entry.description = server->description.data();
        entry.players = static_cast<signed char>(server->players);
        entry.maxPlayers = static_cast<signed char>(server->maxPlayers);
        entry.ping = std::lround(server->ping * kMillisecondsPerSecond);
        entry.foreign = server->foreign != 0;
        entry.state = static_cast<int>(server->state);
        entry.password = server->password != 0;
        entry.modRead = server->modRead;
        entry.mod = server->mod;
        entry.joinable = server->ModAgrees();
    }

    // By the sort key: 1 / 2 the name ascending / descending (_mbscmp), 3 / 4 the players, 5 / 6 the game type,
    // 7 / 8 the ping, anything else the state, ascending. Servers with the same value keep the game's sort's order.
    const int key = sortKey;
    lib::IntroSort(entries.data(), entries.data() + entries.size(),
                   [key](const MpAddressEntry& a, const MpAddressEntry& b) {
        switch (key) {
        case 1: return str::Compare(a.name, b.name) < 0;
        case 2: return str::Compare(a.name, b.name) > 0;
        case 3: return a.players < b.players;
        case 4: return b.players < a.players;
        case 5: return a.gameType < b.gameType;
        case 6: return b.gameType < a.gameType;
        case 7: return a.ping < b.ping;
        case 8: return b.ping < a.ping;
        default: return a.state < b.state;
        }
    });

    MpAddressEntry& newGame = *entries.emplace(entries.begin());
    newGame.address = net::NetAddress{};
    newGame.gameType = kNewGameType;
    newGame.name = kNewGameText;
    newGame.description = kNewGameText;
    newGame.ping = kNoPingMillis;
    newGame.players = 0;
    newGame.maxPlayers = 0;
    newGame.foreign = false;
    newGame.state = 0;
    selected = 0;
    for (unsigned row = 1; row < entries.size(); ++row)
        if (std::memcmp(&entries[row].address, &selectedAddress, sizeof(net::NetAddress)) == 0)
            selected = static_cast<int>(row);
    if (selected == 0) selectedAddress = net::NetAddress{};

    unsigned count = static_cast<unsigned>(entries.size());
    if (count > static_cast<unsigned>(kRows)) {
        const int position = scroll->TakePosition();
        scroll->SetMaximum(static_cast<int>(entries.size()) - kRows);
        scroll->SetPosition(position);
    } else {
        scroll->SetMaximum(0);
    }
    scrollTop = 0;
}

void MpConnectScreen::CreateScreen() {
    rect = Rect{0, 0, 800, 600};
    LoadTexture(kSaveLoadTexture, TextureKind::Interface);
    LoadTexture(kScrollbarTexture, TextureKind::Interface);
    UiModel* model = AddModel();
    model->Load(kMenuModel, kMenuTexture, kMenuLabels);
    model->SetScreenPosition(400.0f, 350.0f, 8.0f);
    {
        const Vector3 axis = {1.0f, 0.0f, 0.0f};
        model->object->localRotation = Quaternion::AxisAngle(axis, MenuTurn());
    }
    model->ShowAllParts(0);
    model->ShowPart(kMenuButtonPart, 1);
    model->ShowPart(kMenuButtonLabel, 1);

    auto region = [this](int left, int top, int right, int bottom, int tip) {
        UiRegion* added = AddRegion();
        added->SetRect(left, top, right, bottom);
        if (tip) added->SetTip(tip);
        return added;
    };
    connectButton = region(163, 526, 237, 574, 0x9E98);
    backButton = region(563, 526, 637, 574, 0x9E99);
    masterButton = region(490, 441, 620, 465, 0x9EA2);
    addressButton = region(490, 465, 620, 489, 0x9EA3);
    unlistedSwitch = region(440, 346, 670, 367, 0);
    passwordSwitch = region(440, 370, 670, 391, 0);
    sortColumns[0] = region(106, 106, 236, 127, 0);
    sortColumns[1] = region(236, 106, 264, 127, 0);
    sortColumns[2] = region(264, 106, 334, 127, 0);
    sortColumns[3] = region(334, 106, 375, 127, 0);

    auto sprite = [this](float depth, float left, float top, float right, float bottom, int u0, int v0, int u1, int v1) {
        UiMesh* added = AddBlankSprite();
        added->SetRectangle(depth, kSpriteScale, left, top, right, bottom, u0, v0, u1, v1);
        return added;
    };
    auto plain = [](UiMesh* mesh, std::uint32_t colour) {
        mesh->texture = nullptr;
        mesh->flags |= FigureMeshFlag::Flat;
        mesh->flatColour = colour;
    };
    sprite(3.0f, 163.0f, 526.0f, 237.0f, 574.0f, 0x51, 2, 0x9B, 0x32);
    sprite(3.0f, 563.0f, 526.0f, 637.0f, 574.0f, 0xA0, 2, 0xEA, 0x32);
    // The selection's right edge (Rebirth's ExtendedServerName; the original's 370).
    selectionMarker = sprite(3.0f, 108.0f, 108.0f, kExtendedSelectionRight, 132.0f, 0, 0, kUiTextureSize, kUiTextureSize);
    plain(selectionMarker, kSelectionColour);
    sortArrow = AddBlankSprite();
    sortArrow->texture = objects[kScrollbarObject];
    sortArrow->SetRectangle(4.0f, kSpriteScale, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0x80, 0x50, 0xB0);
    sortArrow->visible = false;
    unlistedBox = sprite(4.0f, 440.0f, 346.0f, 461.0f, 367.0f, 0xD1, 0x37, 0xE6, 0x4C);
    unlistedBox->textureOffsetU = -kTabSelected;
    passwordBox = sprite(4.0f, 440.0f, 370.0f, 461.0f, 391.0f, 0xD1, 0x37, 0xE6, 0x4C);

    TextManager* clientWorldTexts = g_clientWorld.texts.get();
    std::string text;
    for (int i = 0, top = 238; top < 334; top += 24, ++i) {
        text = str::Format(kAllodNameFormat, i);
        text = clientWorldTexts->TextManager::GetString(text);
        allodNames.push_back(text);
        const int bottom = top + 21;
        tabBoxes[i] = sprite(4.0f, 460.0f, static_cast<float>(top), 481.0f, static_cast<float>(bottom), 0xD1, 0x37, 0xE6, 0x4C);
        tabAreas[i] = region(460, top, 670, bottom, 0);
    }
    tabBoxes[0]->textureOffsetU = kTabSelected;
    gameType = 0;
    unlisted = true;
    hostPassword.clear();
    for (int top = 444; top < 492; top += 24)
        sprite(3.0f, 490.0f, static_cast<float>(top), 620.0f, static_cast<float>(top + 21), 0x7A, 0x6C, 0xFC, 0x7F);
    AddBlankSprite()->SetFrame(2.0f, 100.0f, 95.0f, 95.0f, 395.0f, 505.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    // The details' panel reaches 10 lower than the original's (to 420) for the pause mode's row, the panel of the
    // master and address buttons starts 4 lower (at 434).
    AddBlankSprite()->SetFrame(2.0f, 100.0f, 405.0f, 95.0f, 705.0f, 425.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    AddBlankSprite()->SetFrame(2.0f, 100.0f, 405.0f, 429.0f, 705.0f, 505.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    plain(sprite(5.0f, 100.0f, 100.0f, 390.0f, 500.0f, 0, 0, kUiTextureSize, kUiTextureSize), kBackColour);
    plain(sprite(5.0f, 410.0f, 100.0f, 700.0f, 420.0f, 0, 0, kUiTextureSize, kUiTextureSize), kBackColour);
    plain(sprite(5.0f, 410.0f, 434.0f, 700.0f, 500.0f, 0, 0, kUiTextureSize, kUiTextureSize), kBackColour);
    for (int row = 0, top = 132; row < kRows; ++row, top += 24) rows[row] = region(108, top, 370, top + 24, 0);
    pauseSwitch = region(440, 396, 690, 418, 0);
    pauseMode = net::PauseMode::Original;
    // Ours, region 30: on the game type's title row, right of the title (the tabs below reach x 670).
    companionSwitch = region(560, 214, 700, 237, 0);
    companions = 0;

    AddSurface(NewSurface(Rect{100, 100, 700, 500}))->PaintWith([this](UiSurface& out) { PaintList(out); });
    {
        scroll = AddChild(std::make_unique<UiScrollbar>(Rect{108, 132, 384, 492}, 3.0f, 0, 1.0f));
    }
    {
        // EI_Plugin's NPlayers: the slider runs to the most players the game takes (net::kMostPlayers), the original's
        // to 6; it starts at the original's 6. addon's ExtendedServerName makes the right edge of the layout rectangle,
        // set after the construction, and of the name's field 680.
        const int right = kExtendedFieldRight;
        playersSlider =
            AddChild(std::make_unique<UiSlider>(Rect{450, 196, 650, 206}, 3.0f, net::kMostPlayers - 1, 1.0f));
        Rect area{450, 196, right, 206};
        playersSlider->SetRect(area);
        playersSlider->SetPosition(kDefaultPlayers - 1);
        maxPlayers = kDefaultPlayers;
    }
    {
        nameEdit = AddChild(std::make_unique<UiEditField>(Rect{450, 144, kExtendedFieldRight, 160}));
        // The classes 15, font 0, at most 50 characters (Rebirth's ExtendedServerName; the original's 3, 1, 10). The
        // name taken from the hero below still stops at 10.
        nameEdit->SetLook(15, 0, 0, kExtendedNameLength, g_connectTextColour, kCaretColour);
    }
    // The new game's name: the first hero's, to its '|', at most 10 characters.
    std::string name = LocalPlayerData()->members[0].scriptName;
    {
        const int end = str::Find(name, kNameEnd);
        if (end >= 0) name = str::Left(name, end);
        if (text::CharacterCount(name) > kNameLength) name = str::Left(name, text::CharacterBytes(name, kNameLength));
        str::TrimLeft(name);
        str::TrimRight(name);
        nameEdit->SetText(name);
        nameEdit->SetEnabled(false);
        nameEdit->SetFocused(false);
    }
    for (int i = 0; i < kTextCount; ++i) {
        text = str::Format(kConnectionTextFormat, i);
        text = clientWorldTexts->TextManager::GetString(text);
        texts.push_back(text);
    }

    g_gameFlow.StartClient();
    net::ClientQueries queries;
    ChunkArchive archive;
    FileStream file;
    if (file.Open(g_dataFolder + kAddressBookFile, platform::FileMode::Read)) {
        archive.Open(&file, 1, 1);
        if (archive.EnterChunk(1)) {
            queries.Serialize(&archive);
            archive.LeaveChunk();
        }
        archive.Close(&file);
        file.Close();
        if (queries.extra == 0) {
            const std::uint32_t random = g_clientRandom.Next();
            queries.extra = platform::CoarseClock() ^ random;
        }
        net::g_gameClient->SetQueries(&queries);
    }

    sortKey = 0;
    joining = false;
    selected = 0;
    newGameTips = false;
    selectedAddress = net::NetAddress{};
    RebuildList();
    DrawListVanilla();
    g_interface.OpenHelp(false);
    refreshTimer = 0.0f;
    UiSurface* note = AddSurface(NewSurface(Rect{0, 0, 300, 20}));
    note->PaintWith([this](UiSurface& out) {
        Rect area{0, 0, 300, 20};
        out.DrawTextIn(texts[kTextConnecting], area, TextStyle{2, g_connectTextColour});
    });
}

// --- the master request box ----------------------------------------------------------------------

MasterRequestBox::MasterRequestBox(std::string_view titleKey, std::string_view textKey, MessageQuestion question,
                                     const net::NetAddress* address, std::uint32_t extra)
    : MessageWindow(titleKey, textKey, question, MessageButtons::Cancel), link(address) {
    bar = AddBlankSprite();
    bar->texture = nullptr;
    bar->flags |= FigureMeshFlag::Flat;
    bar->flatColour = kBarColour;
    link.out.Put(extra);
}

void MasterRequestBox::Update() {
    float right = net::g_gameClient->BufferRatio(&link.in) * kBarWidth + kBarLeft;
    bar->SetRectangle(2.5f, kSpriteScale, 220.0f, 280.0f, right, 300.0f, 0, 0, kUiTextureSize, kUiTextureSize);
    if (net::MasterLinkTick(&link) == net::MasterLink::State::Done) net::g_gameClient->ReadServerList(&link.in);
    if (net::MasterLinkTick(&link) == net::MasterLink::State::Done || net::MasterLinkTick(&link) == net::MasterLink::State::Closed)
        g_interface.SetPendingValue(BoxResult(MpConnectScreen::kBoxMaster, MessageAnswer::Cancel));
}
