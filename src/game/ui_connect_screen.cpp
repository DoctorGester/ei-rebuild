// The multiplayer connection screen (CMpConnectScreen), its master server request box (CMasterRequestBox) and the
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
#include "item_db_text.h"

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

const char* CancelSound() { return "buttons\\messbox\\cancel.wav"; }
const char* SelectSound() { return "buttons\\save\\select.wav"; }
const char* OkSound() { return "buttons\\messbox\\ok.wav"; }
const char* MessageSuffix() { return "_msg"; }
const char* WrongProtocolKey() { return "lmp_wrong_protocol"; }
const char* WrongProtocolTextKey() { return "lmp_wrong_protocol_msg"; }
const char* NewGameText() { return "??"; }    // the new game entry's name and description
const char* DefaultGameName() { return "?"; }
const char* EmptyText() { return ""; }
const char* AddressBookFile() { return "config\\addrbook.ini"; }
const char* RefusalKey(int refusal) {
    switch (refusal) {
    case 1: return "lmp_server_full";
    case 2: return "lmp_you_are_banned";
    case 3: return "lmp_wrong_password";
    default: return "lmp_cannot_connect";
    }
}
const char* MasterHost() { return options::TheOptions()->masterServerName_.c_str(); }
constexpr int kMasterPort = 28004;

// The switches' sprite frames: off (0.0), on (-0.08984375); the selected game
// type tab's frame is an immediate in the code.
float FrameOff() { return 0.0f /*0073B858*/; }
float FrameOn() { return (-0.08984375f /*00741DA0*/); }
float RefreshPeriod() { return 0.5f /*0073B86C*/; }   // 0.5 s
float MillisecondsPerSecond() { return 1000.0f /*0073C34C*/; }
float BarWidth() { return 355.0f /*00741E84*/; }
float BarLeft() { return 225.0f /*00741E80*/; }
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
constexpr std::uint32_t kPlainSprite = 0x100;

void CopyText(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }

void OpenBox(CInterface3D* ui, const char* title, const char* text, std::uint32_t result, int flags) {
    CMessageBox* box = new CMessageBox(ui, title, text, result, flags, kBoxFormat);
    ui->PushWindow(box, 1, 1);
}


const char* SaveLoadTexture() { return "saveload"; }
const char* ScrollbarTexture() { return "scrollbar"; }
const char* MenuModel() { return "unmoco2"; }
const char* MenuTexture() { return "MainMenu00"; }
const char* MenuLabels() { return "MainMenu00labels"; }
const char* MenuButtonPart() { return "but03"; }
const char* MenuButtonLabel() { return "button03_multiplayer"; }
const char* AllodNameFormat() { return "lmp_allod_name_%d"; }
const char* ConnectionTextFormat() { return "lmp_connection_%d"; }
const char* NameEnd() { return "|"; }
float MenuTurn() { return kMenuTilt * 1.2f /*0073CDF4*/; }
std::uint32_t TextColour() { return g_connectTextColour; }
constexpr std::uint32_t kCaretColour = 0x31B3FF;
// ExtendedServerName's values in Create (addon.dll: operands).
constexpr float kExtendedSelectionRight = 381.5f;
constexpr int kExtendedFieldRight = 680;
constexpr int kExtendedNameLength = 50;
constexpr std::uint32_t kSelectionColour = 0xC8A06800;
constexpr std::uint32_t kBackColour = 0xA0000000;
constexpr int kSurfaceFormat = 5;
constexpr int kTextCount = 22;
constexpr int kTextConnecting = 8;
constexpr int kNameLength = 10;
constexpr int kTextureKind = 3;
constexpr int kScrollbarTexture = 1;   // objects_: 0 "saveload", 1 "scrollbar"

CUiSurface* NewSurface(CInterface3D* ui, int left, int top, int right, int bottom) {
    return new CUiSurface(ui, left, top, right, bottom, kSurfaceFormat, 1);
}

// A write the way the box's constructor inlines it: in place (marking the buffer written) when it
// fits before `end`, otherwise through slot 4. Not inlined, so the slot is called through the
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

void CMpConnectScreen::Update() {
    net::ClientSession* client = net::GameClient();
    bool asking = client->joinState_ == net::ClientBase::kAsking;
    if (asking) {
        surfaces_[kSurfaceConnecting]->Freeze(1, 0);
    } else {
        surfaces_[kSurfaceConnecting]->Freeze(0, 0);
        if (joining_) {
            joining_ = false;
            std::string key;
            key = RefusalKey(client->refusal_);
            std::string text = key + MessageSuffix();
            interface_->PushWindow(new CMessageBox(interface_, key.c_str(), text.c_str(), kBoxNotice, kBoxFlagsOk, kBoxFormat), 1, 1);
            return;
        }
    }
    joining_ = asking;
    UpdateBase();

    double elapsed = FrameTime() + refreshTimer_;
    refreshTimer_ = static_cast<float>(elapsed);
    float period = RefreshPeriod();
    if (elapsed > period) {
        float timer = refreshTimer_;
        do {
            timer -= period;
        } while (timer > period);
        refreshTimer_ = timer;
        net::NetAddress none{};
        const net::NetAddress* ask = &none;
        if (selected_ >= 1 && static_cast<unsigned>(selected_) < entries_.size()) ask = &entries_[selected_].address;
        net::GameClient()->SetAlwaysAsk(ask);
        net::GameClient()->Refresh();
        RebuildList();
        DrawListVanilla();
    }

    int position;
    if (scroll_->TakePosition(&position)) {
        scrollTop_ = position;
        DrawListVanilla();
    }
    if (playersSlider_->TakePosition(&position)) {
        maxPlayers_ = position + 1;
        DrawListVanilla();
    }
    bool renamed = nameEdit_->changed_;
    nameEdit_->changed_ = false;
    if (renamed) DrawListVanilla();
}

void CMpConnectScreen::OnBoxResult(std::uint32_t result) {
    if (result == (kBoxMaster | kAnswerCancel)) {
        DrawListVanilla();
    } else if (result == (kBoxAddress | kAnswerOk)) {
        net::NetAddress address{};
        if (address.Parse(boxText_, net::g_gamePort)) net::GameClient()->Ask(&address);
        SaveAddressBook();
        DrawListVanilla();
    } else if (result == (kBoxPassword | kAnswerOk) || result == (kBoxPassword | kAnswerCancel)) {
        hostPassword_ = boxText_;
        auto* sprite = SceneMesh(kSpritePassword);
        sprite->textureOffsetU_ = hostPassword_.size() == 0 ? FrameOff() : FrameOn();
    } else if (result == (kBoxJoinPassword | kAnswerOk)) {
        TheGameFlow()->JoinServer(&entries_[selected_].address, boxText_);
        net::GameClient()->gameType_ = entries_[selected_].gameType;
    }
}

// --- input ---------------------------------------------------------------------------------------

void CMpConnectScreen::OnLButtonDown(int buttons, int x, int y) {
    int region = RegionAt(x, y);
    switch (region) {
    case kRegionConnect:
        Connect();
        break;
    case kRegionBack:
        interface_->PlaySound(CancelSound(), 0);
        SaveAddressBook();
        TheGameFlow()->DropSessions();
        interface_->SetPendingValue(kLeaveScreen);
        break;
    case kRegionMaster:
        MasterRequest();
        break;
    case kRegionAddress:
        EditAddress();
        break;
    case kRegionUnlisted: {
        unlisted_ = !unlisted_;
        auto* sprite = SceneMesh(kSpriteUnlisted);
        sprite->textureOffsetU_ = unlisted_ ? FrameOn() : FrameOff();
        break;
    }
    case kRegionPassword:
        if (hostPassword_.size() == 0) {
            CopyText(boxText_, hostPassword_.c_str());
            OpenBox(interface_, texts_[kTextPassword].c_str(), boxText_, kBoxPassword, kBoxFlagsEdit);
        } else {
            hostPassword_.clear();
            SceneMesh(kSpritePassword)->textureOffsetU_ = 0.0f;
        }
        break;
    case kRegionSortFirst:
    case kRegionSortFirst + 1:
    case kRegionSortFirst + 2:
    case kRegionSortFirst + 3: {
        // Column c sorts by key 2c + 1, a second click by 2c + 2.
        int ascending = 2 * (region - kRegionSortFirst) + 1;
        sortKey_ = sortKey_ == ascending ? ascending + 1 : ascending;
        RebuildList();
        DrawListVanilla();
        break;
    }
    default:
        if (region >= kRegionTabFirst && region < kRegionRowFirst) {
            if (selected_ == 0) {
                gameType_ = region - kRegionTabFirst;
                for (int i = 0; i < kTabs; ++i)
                    SceneMesh(kSpriteTabFirst + i)->textureOffsetU_ = 0.0f;
                SceneMesh(kSpriteTabFirst + gameType_)->textureOffsetU_ = kTabSelected;
            }
        } else if (region >= kRegionRowFirst) {
            unsigned row = static_cast<unsigned>(scrollTop_ + region - kRegionRowFirst);
            if (row < entries_.size()) {
                interface_->PlaySound(SelectSound(), 0);
                selected_ = static_cast<int>(row);
                selectedAddress_ = entries_[row].address;
                DrawListVanilla();
            }
        }
        break;
    }
    CUiContainer::LeftDown(buttons, x, y);
}

void CMpConnectScreen::OnLButtonDouble(int buttons, int x, int y) {
    int region = RegionAt(x, y);
    if (region >= kRegionRowFirst) {
        unsigned row = static_cast<unsigned>(scrollTop_ + region - kRegionRowFirst);
        if (row < entries_.size()) {
            selected_ = static_cast<int>(row);
            selectedAddress_ = entries_[row].address;
            Connect();
        }
    }
    CUiContainer::LeftDouble(buttons, x, y);
}

void CMpConnectScreen::OnKeyDown(int key, int repeat, int flags) {
    CUiContainer::KeyDown(key, repeat, flags);
    int step;
    switch (key) {
    case VK_RETURN:
        Connect();
        handled_ = true;
        return;
    case VK_ESCAPE:
        interface_->PlaySound(CancelSound(), 0);
        TheGameFlow()->DropSessions();
        interface_->SetPendingValue(kLeaveScreen);
        handled_ = true;
        return;
    case VK_UP: step = -1; break;
    case VK_DOWN: step = 1; break;
    case VK_PRIOR: step = -kRows; break;
    case VK_NEXT: step = kRows; break;
    default: return;
    }
    interface_->PlaySound(SelectSound(), 0);
    selected_ += step;
    SelectAndScroll();
    handled_ = true;
}

void CMpConnectScreen::SelectAndScroll() {
    if (selected_ < 0) selected_ = 0;
    if (static_cast<unsigned>(selected_) >= entries_.size()) selected_ = static_cast<int>(entries_.size()) - 1;
    int row = selected_;
    selectedAddress_ = entries_[row].address;
    if (!(scrollTop_ + kRows > row)) scrollTop_ = row - (kRows - 1);
    if (scrollTop_ > row) scrollTop_ = row;
    scroll_->SetPosition(scrollTop_);
    scroll_->TakePosition(&scrollTop_);
    DrawListVanilla();
}

void CMpConnectScreen::Connect() {
    interface_->PlaySound(OkSound(), 0);
    int row = selected_;
    if (row == 0) {
        int players;
        playersSlider_->TakePosition(&players);
        ++players;
        std::string name = nameEdit_->text_;
        if (name.size() == 0) name = DefaultGameName();
        SaveAddressBook();
        TheGameFlow()->StartServer(name.c_str(), players, gameType_, hostPassword_.c_str(), static_cast<char>(unlisted_),
                                   static_cast<int>(net::GameClient()->queries_.extra));
        return;
    }
    if (row < 0 || static_cast<unsigned>(row) >= entries_.size()) return;
    if (entries_[row].foreign) {
        OpenBox(interface_, WrongProtocolKey(), WrongProtocolTextKey(), kBoxNotice, kBoxFlagsOk);
        return;
    }
    SaveAddressBook();
    if (entries_[selected_].password) {
        boxText_[0] = '\0';
        OpenBox(interface_, texts_[kTextPassword].c_str(), boxText_, kBoxJoinPassword, kBoxFlagsEdit);
    }
    TheGameFlow()->JoinServer(&entries_[selected_].address, EmptyText());
    net::GameClient()->gameType_ = entries_[selected_].gameType;
}

void CMpConnectScreen::EditAddress() {
    int row = selected_;
    if (row < 0 || static_cast<unsigned>(row) >= entries_.size()) return;
    MpAddressEntry& entry = entries_[row];
    if (entry.state == kStateAsked) {
        net::GameClient()->Forget(&entry.address);
        SaveAddressBook();
        return;
    }
    boxText_[0] = '\0';
    if (row > 0 && static_cast<unsigned>(row) < entries_.size()) {
        std::string text = entry.address.ToString();
        CopyText(boxText_, text.data());
    }
    OpenBox(interface_, texts_[kTextAddress].c_str(), boxText_, kBoxAddress, kBoxFlagsEdit);
}

void CMpConnectScreen::MasterRequest() {
    CopyText(boxText_, EmptyText());
    net::NetAddress master{};
    // EI_Plugin's NewMaster: with a second master set, the master is tried first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster) && net::second_master::Configured())
        net::second_master::ParseMaster(&master, MasterHost(), kMasterPort);
    else
        master.Parse(MasterHost(), kMasterPort);
    net::ClientSession* client = net::GameClient();
    std::uint32_t extra = client ? client->queries_.extra : 0;
    interface_->PushWindow(new CMasterRequestBox(interface_, texts_[kTextMaster].c_str(), boxText_, kBoxMaster, &master, extra), 1,
                           1);
}

void CMpConnectScreen::SaveAddressBook() {
    ChunkArchive archive;
    netlink::FileStream file;
    if (!file.Open(TheDataFolder() + AddressBookFile(), netlink::FileStream::kOpenWrite, 0)) return;
    archive.Open(&file, 0, 1);
    net::ClientQueries* queries = &net::GameClient()->queries_;
    if (archive.EnterChunk(1)) {
        queries->Serialize(&archive);
        archive.LeaveChunk();
    }
    archive.Close(&file);
    file.Close();
}

void CMpConnectScreen::RebuildList() {
    entries_.clear();
    unsigned index = 0;
    for (net::ServerEntry* server = net::GameClient()->ServerAt(0); server;
         server = net::GameClient()->ServerAt(static_cast<int>(++index))) {
        MpAddressEntry& entry = entries_.emplace_back();
        entry.address = server->address_;
        entry.gameType = static_cast<int>(server->kind_);
        if (entry.gameType < 0 || entry.gameType > 3) entry.gameType = 0;
        entry.name = server->name_.data();
        entry.description = server->description_.data();
        entry.players = static_cast<signed char>(server->players_);
        entry.maxPlayers = static_cast<signed char>(server->maxPlayers_);
        entry.ping = x87::fistp(server->ping_ * MillisecondsPerSecond());
        entry.foreign = server->foreign_ != 0;
        entry.state = static_cast<int>(server->state_);
        entry.password = server->password_ != 0;
    }

    // By the sort key: 1 / 2 the name ascending / descending (_mbscmp), 3 / 4 the players, 5 / 6 the game type,
    // 7 / 8 the ping, anything else the state, ascending. Servers with the same value keep the game's sort's order.
    const int key = sortKey_;
    lib::IntroSort(entries_.data(), entries_.data() + entries_.size(),
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

    MpAddressEntry& newGame = *entries_.emplace(entries_.begin());
    newGame.address = net::NetAddress{};
    newGame.gameType = kNewGameType;
    newGame.name = NewGameText();
    newGame.description = NewGameText();
    newGame.ping = kNoPing;
    newGame.players = 0;
    newGame.maxPlayers = 0;
    newGame.foreign = false;
    newGame.state = 0;
    selected_ = 0;
    for (unsigned row = 1; row < entries_.size(); ++row)
        if (std::memcmp(&entries_[row].address, &selectedAddress_, sizeof(net::NetAddress)) == 0)
            selected_ = static_cast<int>(row);
    if (selected_ == 0) selectedAddress_ = net::NetAddress{};

    unsigned rows = static_cast<unsigned>(entries_.size());
    if (rows > static_cast<unsigned>(kRows)) {
        int position;
        scroll_->TakePosition(&position);
        scroll_->SetMaximum(static_cast<int>(entries_.size()) - kRows);
        scroll_->SetPosition(position);
    } else {
        scroll_->SetMaximum(0);
    }
    scrollTop_ = 0;
}

void CMpConnectScreen::CreateScreen() {
    rect_ = RECT{0, 0, 800, 600};
    LoadTexture(SaveLoadTexture(), kTextureKind, nullptr);
    LoadTexture(ScrollbarTexture(), kTextureKind, nullptr);
    CUiModel* model = AddModel();
    model->Load(MenuModel(), MenuTexture(), MenuLabels());
    model->SetScreenPosition(400.0f, 350.0f, 8.0f);
    {
        const Vector3 axis = {1.0f, 0.0f, 0.0f};
        Quaternion turn;
        model->object_->localRotation_ = *turn.SetAxisAngle(axis, MenuTurn());
    }
    model->ShowAllParts(0);
    model->ShowPart(MenuButtonPart(), 1);
    model->ShowPart(MenuButtonLabel(), 1);

    auto region = [this](int left, int top, int right, int bottom, int tip) {
        CUiRegion* added = AddRegion();
        added->SetRect(left, top, right, bottom);
        if (tip) added->SetTip(tip, 1, 0);
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
        CUiMesh* added = AddSprite(nullptr);
        added->SetRectangle(depth, 100.0f, left, top, right, bottom, u0, v0, u1, v1);
        return added;
    };
    auto plain = [](CUiMesh* mesh, std::uint32_t colour) {
        mesh->texture_ = nullptr;
        mesh->flags_ |= kPlainSprite;
        mesh->flatColour_ = colour;
    };
    sprite(3.0f, 163.0f, 526.0f, 237.0f, 574.0f, 0x51, 2, 0x9B, 0x32);
    sprite(3.0f, 563.0f, 526.0f, 637.0f, 574.0f, 0xA0, 2, 0xEA, 0x32);
    // ExtendedServerName: the selection's right edge 370 -> 381.5.
    const bool extended = ei::rebirth::Enabled(ei::rebirth::Patch::ExtendedServerName);
    plain(sprite(3.0f, 108.0f, 108.0f, extended ? kExtendedSelectionRight : 370.0f, 132.0f, 0, 0, 0x100, 0x100),
          kSelectionColour);
    {
        CUiMesh* arrow = AddSprite(nullptr);
        arrow->texture_ = objects_[kScrollbarTexture];
        arrow->SetRectangle(4.0f, 100.0f, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0x80, 0x50, 0xB0);
        arrow->visible_ = 0;
    }
    sprite(4.0f, 440.0f, 346.0f, 461.0f, 367.0f, 0xD1, 0x37, 0xE6, 0x4C)->textureOffsetU_ = -kTabSelected;
    sprite(4.0f, 440.0f, 370.0f, 461.0f, 391.0f, 0xD1, 0x37, 0xE6, 0x4C);

    CTextManager* texts = GameClientTexts();
    std::string text;
    for (int i = 0, top = 238; top < 334; top += 24, ++i) {
        text = str::Format(AllodNameFormat(), i);
        texts->CTextManager::GetString(text.c_str(), &text);
        allodNames_.push_back(text);
        const int bottom = top + 21;
        sprite(4.0f, 460.0f, static_cast<float>(top), 481.0f, static_cast<float>(bottom), 0xD1, 0x37, 0xE6, 0x4C);
        region(460, top, 670, bottom, 0);
    }
    SceneMesh(kSpriteTabFirst)->textureOffsetU_ = kTabSelected;
    gameType_ = 0;
    unlisted_ = true;
    hostPassword_.clear();
    for (int top = 444; top < 492; top += 24)
        sprite(3.0f, 490.0f, static_cast<float>(top), 620.0f, static_cast<float>(top + 21), 0x7A, 0x6C, 0xFC, 0x7F);
    AddSprite(nullptr)->SetFrame(2.0f, 100.0f, 95.0f, 95.0f, 395.0f, 505.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    AddSprite(nullptr)->SetFrame(2.0f, 100.0f, 405.0f, 95.0f, 705.0f, 415.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    AddSprite(nullptr)->SetFrame(2.0f, 100.0f, 405.0f, 425.0f, 705.0f, 505.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    plain(sprite(5.0f, 100.0f, 100.0f, 390.0f, 500.0f, 0, 0, 0x100, 0x100), kBackColour);
    plain(sprite(5.0f, 410.0f, 100.0f, 700.0f, 410.0f, 0, 0, 0x100, 0x100), kBackColour);
    plain(sprite(5.0f, 410.0f, 430.0f, 700.0f, 500.0f, 0, 0, 0x100, 0x100), kBackColour);
    for (int top = 132; top < 492; top += 24) region(108, top, 370, top + 24, 0);   // the rows

    AddSurface(NewSurface(interface_, 100, 100, 700, 500));
    {
        scroll_ = new CUiScrollbar(interface_, RECT{108, 132, 384, 492}, 3.0f, 0, 1.0f);
        AddChild(scroll_);
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
        playersSlider_ = new CUiSlider(interface_, RECT{450, 196, 650, 206}, 3.0f, maximum, 1.0f);
        AddChild(playersSlider_);
        RECT rect{450, 196, right, 206};
        playersSlider_->SetRect(&rect);
        playersSlider_->SetPosition(position);
        maxPlayers_ = 6;
    }
    {
        nameEdit_ = new CUiEditField(interface_, RECT{450, 144, extended ? kExtendedFieldRight : 650, 160});
        AddChild(nameEdit_);
        // ExtendedServerName: the classes 15, font 0, at most 50 characters (the name taken from the hero
        // below still stops at 10).
        if (extended)
            nameEdit_->SetLook(15, 0, 0, kExtendedNameLength, TextColour(), kCaretColour);
        else
            nameEdit_->SetLook(3, 0, 1, kNameLength, TextColour(), kCaretColour);
    }
    // The new game's name: the first hero's, to its '|', at most 10 characters.
    std::string name = LocalPlayerData()->members_[0].texts[0];
    {
        const int end = str::Find(name, NameEnd());
        if (end >= 0) name = str::Left(name, end);
        if (text::CharacterCount(name) > kNameLength) name = str::Left(name, text::CharacterBytes(name, kNameLength));
        str::TrimLeft(name);
        str::TrimRight(name);
        nameEdit_->SetText(name.c_str());
        nameEdit_->SetEnabled(0, 1);
        nameEdit_->SetFocused(false);
    }
    for (int i = 0; i < kTextCount; ++i) {
        text = str::Format(ConnectionTextFormat(), i);
        texts->CTextManager::GetString(text.c_str(), &text);
        texts_.push_back(text);
    }

    TheGameFlow()->StartClient();
    spare_ = 0;
    net::ClientQueries queries;
    ChunkArchive archive;
    netlink::FileStream file;
    if (file.Open(TheDataFolder() + AddressBookFile(), netlink::FileStream::kOpenRead, 1)) {
        archive.Open(&file, 1, 1);
        if (archive.EnterChunk(1)) {
            queries.Serialize(&archive);
            archive.LeaveChunk();
        }
        archive.Close(&file);
        file.Close();
        if (queries.extra == 0) {
            const std::uint32_t random = IsaacRandom::Global().Next();
            queries.extra = gameapi::GetTickCount() ^ random;
        }
        net::GameClient()->SetQueries(&queries);
    }

    sortKey_ = 0;
    joining_ = false;
    selected_ = 0;
    newGameTips_ = false;
    selectedAddress_ = net::NetAddress{};
    RebuildList();
    DrawListVanilla();
    interface_->OpenHelp(0);
    refreshTimer_ = 0.0f;
    CUiSurface* note = AddSurface(NewSurface(interface_, 0, 0, 300, 20));
    UiSurfaceGetDC(note);
    note->Clear();
    RECT rect{0, 0, 300, 20};
    note->DrawTextIn(texts_[kTextConnecting], &rect, 2, TextColour(), 0, 1);
    UiSurfaceReleaseDC(note);
}

// --- the master request box ----------------------------------------------------------------------

CMasterRequestBox::CMasterRequestBox(CInterface3D* ui, const char* titleKey, const char* textKey, std::uint32_t result,
                                     const net::NetAddress* address, std::uint32_t extra)
    : CMessageBox(ui, titleKey, textKey, result, kBoxFlagsCancel, kBoxFormat), link_(address) {
    bar_ = AddSprite(nullptr);
    bar_->texture_ = nullptr;
    bar_->flags_ |= kPlainSprite;
    bar_->flatColour_ = kBarColour;
    BufferPut(&link_.out, &extra, sizeof(extra));
}

void CMasterRequestBox::Update() {
    double share = net::GameClient()->BufferRatio(&link_.in);
    float right = static_cast<float>(share * BarWidth() + BarLeft());
    bar_->SetRectangle(2.5f, 100.0f, 220.0f, 280.0f, right, 300.0f, 0, 0, 0x100, 0x100);
    if (net::MasterLinkTick(&link_) == net::MasterLink::kDone) net::GameClient()->ReadServerList(&link_.in);
    if (net::MasterLinkTick(&link_) == net::MasterLink::kDone || net::MasterLinkTick(&link_) == net::MasterLink::kClosed)
        interface_->SetPendingValue(CMpConnectScreen::kBoxMaster | kAnswerCancel);
}
