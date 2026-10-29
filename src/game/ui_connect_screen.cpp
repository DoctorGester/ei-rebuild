// The multiplayer connection screen (MpConnectScreen), its master server request box (MasterRequestBox) and the
// address list's vector members. See ui_screens.h and docs/notes/ui.md, section 3. Multiplayer only. Its Create
// reproduces addon's ExtendedServerName sites and EI_Plugin's NPlayers, as the list draw (ui_connect_list.cpp) does
// its patches.
#include "net_classes.h"
#include "options.h"
#include "asset_text.h"
#include "ui.h"
#include "ui_screens.h"
#include "game_strings.h"

#include "chunk_archive.h"
#include "core_transform.h"
#include "net_coder.h"
#include "net_link.h"
#include "net_second_master.h"
#include "party.h"
#include "random.h"
#include "unit.h"
#include "world.h"
#include "text_manager.h"

#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/intro_sort.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/str.h"
#include "lib/vecmath.h"
#include "world_glue.h"
#include "game_data.h"
#include "game_singletons.h"

#include <memory>
#include <new>

namespace net {
int MasterLinkTick(net::MasterLink*);
}
namespace {

constexpr float kMenuTilt = /*007C7F40*/ 1.5707964f;   // a static initialiser: pi * 0.5

constexpr const char* kCancelSound = "buttons\\messbox\\cancel.wav";
constexpr const char* kSelectSound = "buttons\\save\\select.wav";
constexpr const char* kOkSound = "buttons\\messbox\\ok.wav";
constexpr const char* kMessageSuffix = "_msg";
constexpr const char* kWrongProtocolKey = "lmp_wrong_protocol";
constexpr const char* kWrongProtocolTextKey = "lmp_wrong_protocol_msg";
constexpr const char* kNewGameText = "??";    // the new game entry's name and description
constexpr const char* kDefaultGameName = "?";
constexpr const char* kEmptyText = "";
constexpr const char* kAddressBookFile = "config\\addrbook.ini";
const char* RefusalKey(int refusal) {
    switch (refusal) {
    case 1: return "lmp_server_full";
    case 2: return "lmp_you_are_banned";
    case 3: return "lmp_wrong_password";
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

constexpr int kRegionConnect = 0, kRegionBack = 1, kRegionMaster = 2, kRegionAddress = 3, kRegionUnlisted = 4,
              kRegionPassword = 5, kRegionSortFirst = 6, kRegionTabFirst = 10, kRegionRowFirst = 14;
constexpr int kSpriteUnlisted = 4, kSpritePassword = 5, kSpriteTabFirst = 6, kTabs = 4;
constexpr int kSurfaceConnecting = 1;
constexpr int kTextPassword = 18, kTextAddress = 19, kTextMaster = 20;
constexpr std::uint32_t kLeaveScreen = 0x1000001;
constexpr std::uint32_t kAnswerOk = 1, kAnswerCancel = 2;
constexpr int kNoPing = 9999;
constexpr int kNewGameType = 4;
constexpr int kStateAsked = 1;   // net::ServerEntryBase::kAsked
constexpr int kBoxFlagsOk = 1, kBoxFlagsEdit = 7, kBoxFlagsCancel = 2, kBoxFormat = 1;
constexpr std::uint32_t kBarColour = 0xFFA06800;

void CopyText(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }

void OpenBox(Interface3D* ui, std::string_view title, const char* text, std::uint32_t result, int flags) {
    MessageWindow* box = new MessageWindow(ui, title, text, result, flags, kBoxFormat);
    ui->PushWindow(box, true, true);
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

UiSurface* NewSurface(Interface3D* ui, int left, int top, int right, int bottom) {
    UiSurface* surface = new UiSurface(ui, left, top, right, bottom, SurfaceBlend::Keyed, true);
    return surface;
}

// A write the way the box's constructor inlines it: in place (marking the buffer written) when it
// fits before `end`, otherwise through the buffer's virtual call. Not inlined, so it is called through the
// buffer's own vtable.
__declspec(noinline) void BufferPut(GameBuffer* b, const void* from, unsigned size) {
    if (b->cur + size > b->end) {
        b->WriteMore(from, size);
        return;
    }
    b->flags |= 1;
    std::memcpy(b->cur, from, size);
    b->cur += size;
}

} // namespace

void MpConnectScreen::Update() {
    net::ClientSession* client = net::g_gameClient;
    bool asking = client->joinState == net::ClientBase::kAsking;
    if (asking) {
        surfaces[kSurfaceConnecting]->Show();
    } else {
        surfaces[kSurfaceConnecting]->Hide(false);
        if (joining) {
            joining = false;
            std::string key = RefusalKey(client->refusal);
            std::string text = key + kMessageSuffix;
            ui->PushWindow(new MessageWindow(ui, key, text.c_str(), kBoxNotice, kBoxFlagsOk, kBoxFormat), true, true);
            return;
        }
    }
    joining = asking;
    UiContainer::Update();

    double elapsed = FrameTime() + refreshTimer;
    refreshTimer = static_cast<float>(elapsed);
    float period = kRefreshPeriod;
    if (elapsed > period) {
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

void MpConnectScreen::MessageResult(int answer) {
    const auto result = static_cast<std::uint32_t>(answer);
    if (result == (kBoxMaster | kAnswerCancel)) {
        DrawListVanilla();
    } else if (result == (kBoxAddress | kAnswerOk)) {
        net::NetAddress address{};
        if (address.Parse(boxText, net::g_gamePort)) net::g_gameClient->Ask(&address);
        SaveAddressBook();
        DrawListVanilla();
    } else if (result == (kBoxPassword | kAnswerOk) || result == (kBoxPassword | kAnswerCancel)) {
        hostPassword = boxText;
        auto* sprite = SceneMesh(kSpritePassword);
        sprite->textureOffsetU = hostPassword.size() == 0 ? kFrameOff : kFrameOn;
    } else if (result == (kBoxJoinPassword | kAnswerOk)) {
        g_gameFlow.JoinServer(&entries[selected].address, boxText);
        net::g_gameClient->gameType = entries[selected].gameType;
    }
}

// --- input ---------------------------------------------------------------------------------------

void MpConnectScreen::LeftDown(int buttons, int x, int y) {
    int region = RegionAt(x, y);
    switch (region) {
    case kRegionConnect:
        Connect();
        break;
    case kRegionBack:
        ui->PlaySound(kCancelSound);
        SaveAddressBook();
        g_gameFlow.DropSessions();
        ui->SetPendingValue(kLeaveScreen);
        break;
    case kRegionMaster:
        MasterRequest();
        break;
    case kRegionAddress:
        EditAddress();
        break;
    case kRegionUnlisted: {
        unlisted = !unlisted;
        auto* sprite = SceneMesh(kSpriteUnlisted);
        sprite->textureOffsetU = unlisted ? kFrameOn : kFrameOff;
        break;
    }
    case kRegionPassword:
        if (hostPassword.size() == 0) {
            CopyText(boxText, hostPassword.c_str());
            OpenBox(ui, texts[kTextPassword], boxText, kBoxPassword, kBoxFlagsEdit);
        } else {
            hostPassword.clear();
            SceneMesh(kSpritePassword)->textureOffsetU = 0.0f;
        }
        break;
    case kRegionSortFirst:
    case kRegionSortFirst + 1:
    case kRegionSortFirst + 2:
    case kRegionSortFirst + 3: {
        // Column c sorts by key 2c + 1, a second click by 2c + 2.
        int ascending = 2 * (region - kRegionSortFirst) + 1;
        sortKey = sortKey == ascending ? ascending + 1 : ascending;
        RebuildList();
        DrawListVanilla();
        break;
    }
    default:
        if (region >= kRegionTabFirst && region < kRegionRowFirst) {
            if (selected == 0) {
                gameType = region - kRegionTabFirst;
                for (int i = 0; i < kTabs; ++i)
                    SceneMesh(kSpriteTabFirst + i)->textureOffsetU = 0.0f;
                SceneMesh(kSpriteTabFirst + gameType)->textureOffsetU = kTabSelected;
            }
        } else if (region >= kRegionRowFirst) {
            unsigned row = static_cast<unsigned>(scrollTop + region - kRegionRowFirst);
            if (row < entries.size()) {
                ui->PlaySound(kSelectSound);
                selected = static_cast<int>(row);
                selectedAddress = entries[row].address;
                DrawListVanilla();
            }
        }
        break;
    }
    UiContainer::LeftDown(buttons, x, y);
}

void MpConnectScreen::LeftDouble(int buttons, int x, int y) {
    int region = RegionAt(x, y);
    if (region >= kRegionRowFirst) {
        unsigned row = static_cast<unsigned>(scrollTop + region - kRegionRowFirst);
        if (row < entries.size()) {
            selected = static_cast<int>(row);
            selectedAddress = entries[row].address;
            Connect();
        }
    }
    UiContainer::LeftDouble(buttons, x, y);
}

void MpConnectScreen::KeyDown(int key, int repeat, int modifiers) {
    UiContainer::KeyDown(key, repeat, modifiers);
    int step;
    switch (key) {
    case VK_RETURN:
        Connect();
        handled = true;
        return;
    case VK_ESCAPE:
        ui->PlaySound(kCancelSound);
        g_gameFlow.DropSessions();
        ui->SetPendingValue(kLeaveScreen);
        handled = true;
        return;
    case VK_UP: step = -1; break;
    case VK_DOWN: step = 1; break;
    case VK_PRIOR: step = -kRows; break;
    case VK_NEXT: step = kRows; break;
    default: return;
    }
    ui->PlaySound(kSelectSound);
    selected += step;
    SelectAndScroll();
    handled = true;
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
    ui->PlaySound(kOkSound);
    int row = selected;
    if (row == 0) {
        const int players = playersSlider->TakePosition() + 1;
        std::string name = nameEdit->text;
        if (name.size() == 0) name = kDefaultGameName;
        SaveAddressBook();
        g_gameFlow.StartServer(name.c_str(), players, gameType, hostPassword.c_str(), unlisted,
                                   static_cast<int>(net::g_gameClient->queries.extra));
        return;
    }
    if (row < 0 || static_cast<unsigned>(row) >= entries.size()) return;
    if (entries[row].foreign) {
        OpenBox(ui, kWrongProtocolKey, kWrongProtocolTextKey, kBoxNotice, kBoxFlagsOk);
        return;
    }
    SaveAddressBook();
    if (entries[selected].password) {
        boxText[0] = '\0';
        OpenBox(ui, texts[kTextPassword], boxText, kBoxJoinPassword, kBoxFlagsEdit);
    }
    g_gameFlow.JoinServer(&entries[selected].address, kEmptyText);
    net::g_gameClient->gameType = entries[selected].gameType;
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
    OpenBox(ui, texts[kTextAddress], boxText, kBoxAddress, kBoxFlagsEdit);
}

void MpConnectScreen::MasterRequest() {
    CopyText(boxText, kEmptyText);
    net::NetAddress master{};
    // EI_Plugin's NewMaster: with a second master set, the master is tried first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster) && net::second_master::Configured())
        net::second_master::ParseMaster(&master, options::g_options.masterServerName.c_str(), kMasterPort);
    else
        master.Parse(options::g_options.masterServerName.c_str(), kMasterPort);
    net::ClientSession* client = net::g_gameClient;
    std::uint32_t extra = client ? client->queries.extra : 0;
    ui->PushWindow(new MasterRequestBox(ui, texts[kTextMaster], boxText, kBoxMaster, &master, extra), true,
                           true);
}

void MpConnectScreen::SaveAddressBook() {
    ChunkArchive archive;
    netlink::FileStream file;
    if (!file.Open(g_dataFolder + kAddressBookFile, netlink::FileStream::kOpenWrite, 0)) return;
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
    unsigned index = 0;
    for (net::ServerEntry* server = net::g_gameClient->ServerAt(0); server;
         server = net::g_gameClient->ServerAt(static_cast<int>(++index))) {
        MpAddressEntry& entry = entries.emplace_back();
        entry.address = server->address;
        entry.gameType = static_cast<int>(server->kind);
        if (entry.gameType < 0 || entry.gameType > 3) entry.gameType = 0;
        entry.name = server->name.data();
        entry.description = server->description.data();
        entry.players = static_cast<signed char>(server->players);
        entry.maxPlayers = static_cast<signed char>(server->maxPlayers);
        entry.ping = x87::fistp(server->ping * kMillisecondsPerSecond);
        entry.foreign = server->foreign != 0;
        entry.state = static_cast<int>(server->state);
        entry.password = server->password != 0;
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
    newGame.ping = kNoPing;
    newGame.players = 0;
    newGame.maxPlayers = 0;
    newGame.foreign = false;
    newGame.state = 0;
    selected = 0;
    for (unsigned row = 1; row < entries.size(); ++row)
        if (std::memcmp(&entries[row].address, &selectedAddress, sizeof(net::NetAddress)) == 0)
            selected = static_cast<int>(row);
    if (selected == 0) selectedAddress = net::NetAddress{};

    unsigned rows = static_cast<unsigned>(entries.size());
    if (rows > static_cast<unsigned>(kRows)) {
        const int position = scroll->TakePosition();
        scroll->SetMaximum(static_cast<int>(entries.size()) - kRows);
        scroll->SetPosition(position);
    } else {
        scroll->SetMaximum(0);
    }
    scrollTop = 0;
}

void MpConnectScreen::CreateScreen() {
    rect = RECT{0, 0, 800, 600};
    LoadTexture(kSaveLoadTexture, TextureKind::Interface);
    LoadTexture(kScrollbarTexture, TextureKind::Interface);
    UiModel* model = AddModel();
    model->Load(kMenuModel, kMenuTexture, kMenuLabels);
    model->SetScreenPosition(400.0f, 350.0f, 8.0f);
    {
        const Vector3 axis = {1.0f, 0.0f, 0.0f};
        Quaternion turn;
        model->object->localRotation = *turn.SetAxisAngle(axis, MenuTurn());
    }
    model->ShowAllParts(0);
    model->ShowPart(kMenuButtonPart, 1);
    model->ShowPart(kMenuButtonLabel, 1);

    auto region = [this](int left, int top, int right, int bottom, int tip) {
        UiRegion* added = AddRegion();
        added->SetRect(left, top, right, bottom);
        if (tip) added->SetTip(tip);
    };
    region(163, 526, 237, 574, 0x9E98);   // connect
    region(563, 526, 637, 574, 0x9E99);   // back
    region(490, 441, 620, 465, 0x9EA2);   // the master server
    region(490, 465, 620, 489, 0x9EA3);   // an address
    region(440, 346, 670, 367, 0);        // unlisted
    region(440, 370, 670, 391, 0);        // the password
    region(106, 106, 236, 127, 0);        // the four columns
    region(236, 106, 264, 127, 0);
    region(264, 106, 334, 127, 0);
    region(334, 106, 375, 127, 0);

    auto sprite = [this](float depth, float left, float top, float right, float bottom, int u0, int v0, int u1, int v1) {
        UiMesh* added = AddBlankSprite();
        added->SetRectangle(depth, 100.0f, left, top, right, bottom, u0, v0, u1, v1);
        return added;
    };
    auto plain = [](UiMesh* mesh, std::uint32_t colour) {
        mesh->texture = nullptr;
        mesh->flags |= FigureMeshFlag::Flat;
        mesh->flatColour = colour;
    };
    sprite(3.0f, 163.0f, 526.0f, 237.0f, 574.0f, 0x51, 2, 0x9B, 0x32);
    sprite(3.0f, 563.0f, 526.0f, 637.0f, 574.0f, 0xA0, 2, 0xEA, 0x32);
    // ExtendedServerName: the selection's right edge 370 -> 381.5.
    const bool extended = ei::rebirth::Enabled(ei::rebirth::Patch::ExtendedServerName);
    plain(sprite(3.0f, 108.0f, 108.0f, extended ? kExtendedSelectionRight : 370.0f, 132.0f, 0, 0, 0x100, 0x100),
          kSelectionColour);
    {
        UiMesh* arrow = AddBlankSprite();
        arrow->texture = objects[kScrollbarObject];
        arrow->SetRectangle(4.0f, 100.0f, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0x80, 0x50, 0xB0);
        arrow->visible = false;
    }
    sprite(4.0f, 440.0f, 346.0f, 461.0f, 367.0f, 0xD1, 0x37, 0xE6, 0x4C)->textureOffsetU = -kTabSelected;
    sprite(4.0f, 440.0f, 370.0f, 461.0f, 391.0f, 0xD1, 0x37, 0xE6, 0x4C);

    TextManager* clientWorldTexts = g_clientWorld.texts;
    std::string text;
    for (int i = 0, top = 238; top < 334; top += 24, ++i) {
        text = str::Format(kAllodNameFormat, i);
        text = clientWorldTexts->TextManager::GetString(text);
        allodNames.push_back(text);
        const int bottom = top + 21;
        sprite(4.0f, 460.0f, static_cast<float>(top), 481.0f, static_cast<float>(bottom), 0xD1, 0x37, 0xE6, 0x4C);
        region(460, top, 670, bottom, 0);
    }
    SceneMesh(kSpriteTabFirst)->textureOffsetU = kTabSelected;
    gameType = 0;
    unlisted = true;
    hostPassword.clear();
    for (int top = 444; top < 492; top += 24)
        sprite(3.0f, 490.0f, static_cast<float>(top), 620.0f, static_cast<float>(top + 21), 0x7A, 0x6C, 0xFC, 0x7F);
    AddBlankSprite()->SetFrame(2.0f, 100.0f, 95.0f, 95.0f, 395.0f, 505.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    AddBlankSprite()->SetFrame(2.0f, 100.0f, 405.0f, 95.0f, 705.0f, 415.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    AddBlankSprite()->SetFrame(2.0f, 100.0f, 405.0f, 425.0f, 705.0f, 505.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    plain(sprite(5.0f, 100.0f, 100.0f, 390.0f, 500.0f, 0, 0, 0x100, 0x100), kBackColour);
    plain(sprite(5.0f, 410.0f, 100.0f, 700.0f, 410.0f, 0, 0, 0x100, 0x100), kBackColour);
    plain(sprite(5.0f, 410.0f, 430.0f, 700.0f, 500.0f, 0, 0, 0x100, 0x100), kBackColour);
    for (int top = 132; top < 492; top += 24) region(108, top, 370, top + 24, 0);   // the rows

    AddSurface(NewSurface(ui, 100, 100, 700, 500));
    {
        scroll = new UiScrollbar(ui, RECT{108, 132, 384, 492}, 3.0f, 0, 1.0f);
        AddChild(scroll);
    }
    {
        // EI_Plugin's NPlayers: the slider's maximum is max(players, 7) - 1 and its
        // position max(players, 1) - 1 instead of 5 and 5. addon's ExtendedServerName makes the
        // right edge of the layout rectangle, set after the construction, and of the name's field 680.
        const bool plugin = ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNPlayers);
        const int players = ei::rebirth::PluginNPlayers();
        const int maximum = plugin ? (players < 7 ? 7 : players) - 1 : 5;
        const int position = plugin ? (players < 1 ? 1 : players) - 1 : 5;
        const int right = extended ? kExtendedFieldRight : 650;
        playersSlider = new UiSlider(ui, RECT{450, 196, 650, 206}, 3.0f, maximum, 1.0f);
        AddChild(playersSlider);
        RECT area{450, 196, right, 206};
        playersSlider->SetRect(&area);
        playersSlider->SetPosition(position);
        maxPlayers = 6;
    }
    {
        nameEdit = new UiEditField(ui, RECT{450, 144, extended ? kExtendedFieldRight : 650, 160});
        AddChild(nameEdit);
        // ExtendedServerName: the classes 15, font 0, at most 50 characters (the name taken from the hero
        // below still stops at 10).
        if (extended)
            nameEdit->SetLook(15, 0, 0, kExtendedNameLength, g_connectTextColour, kCaretColour);
        else
            nameEdit->SetLook(3, 0, 1, kNameLength, g_connectTextColour, kCaretColour);
    }
    // The new game's name: the first hero's, to its '|', at most 10 characters.
    std::string name = LocalPlayerData()->members[0].texts[0];
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
    netlink::FileStream file;
    if (file.Open(g_dataFolder + kAddressBookFile, netlink::FileStream::kOpenRead, 1)) {
        archive.Open(&file, 1, 1);
        if (archive.EnterChunk(1)) {
            queries.Serialize(&archive);
            archive.LeaveChunk();
        }
        archive.Close(&file);
        file.Close();
        if (queries.extra == 0) {
            const std::uint32_t random = g_random.Next();
            queries.extra = gameapi::GetTickCount() ^ random;
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
    ui->OpenHelp(false);
    refreshTimer = 0.0f;
    UiSurface* note = AddSurface(NewSurface(ui, 0, 0, 300, 20));
    note->Clear();
    RECT area{0, 0, 300, 20};
    note->DrawTextIn(texts[kTextConnecting], &area, 2, g_connectTextColour, 0, 1);
}

// --- the master request box ----------------------------------------------------------------------

MasterRequestBox::MasterRequestBox(Interface3D* ui, std::string_view titleKey, const char* textKey, std::uint32_t result,
                                     const net::NetAddress* address, std::uint32_t extra)
    : MessageWindow(ui, titleKey, textKey, result, kBoxFlagsCancel, kBoxFormat), link(address) {
    bar = AddBlankSprite();
    bar->texture = nullptr;
    bar->flags |= FigureMeshFlag::Flat;
    bar->flatColour = kBarColour;
    BufferPut(&link.out, &extra, sizeof(extra));
}

void MasterRequestBox::Update() {
    double share = net::g_gameClient->BufferRatio(&link.in);
    float right = static_cast<float>(share * kBarWidth + kBarLeft);
    bar->SetRectangle(2.5f, 100.0f, 220.0f, 280.0f, right, 300.0f, 0, 0, 0x100, 0x100);
    if (net::MasterLinkTick(&link) == net::MasterLink::kDone) net::g_gameClient->ReadServerList(&link.in);
    if (net::MasterLinkTick(&link) == net::MasterLink::kDone || net::MasterLinkTick(&link) == net::MasterLink::kClosed)
        ui->SetPendingValue(MpConnectScreen::kBoxMaster | kAnswerCancel);
}
