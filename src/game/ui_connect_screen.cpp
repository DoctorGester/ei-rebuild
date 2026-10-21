// The multiplayer connection screen (CMpConnectScreen, 0061C2E0-0061D470), its master server request
// box (CMasterRequestBox, 0061E2F0 / 0061E4E0) and the address list's vector members (0061E5A0-
// 0061EE80). See ui_screens.h and docs/notes/ui.md, section 3. Multiplayer only: no scenario
// reaches the screen but ui_mp_host. Its Create (0061B2C0) reproduces addon's ExtendedServerName
// sites and EI_Plugin's NPlayers (W2b), as the list draw 0061D780 (ui_connect_list.cpp) does its
// patches.
#include "net_classes.h"
#include "record_vectors.h"
#include "stl_sorts.h"
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

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/vecmath.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_world.h"
#include "world_glue.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

float GameFloat(std::uintptr_t address) { return ei::GameData<const volatile float>(address); }

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
// The texts of the refusals the client can be given (ClientBase::refusal_ 1..3), else the fourth.
const char* RefusalKey(int refusal) {
    switch (refusal) {
    case 1: return "lmp_server_full";
    case 2: return "lmp_you_are_banned";
    case 3: return "lmp_wrong_password";
    default: return "lmp_cannot_connect";
    }
}
// The port an address typed without one gets (8888), and the master server's host name.
int DefaultPort() { return ei::GameData<const int>(0x00783E30); }
const char* MasterHost() { return ei::GameData<const char* const>(0x0079BED8); }
constexpr int kMasterPort = 28004;

// The switches' sprite frames: off (0.0, 0073B858), on (-0.08984375, 00741DA0); the selected game
// type tab's frame is an immediate in the code.
float FrameOff() { return 0.0f /*0073B858*/; }
float FrameOn() { return (-0.08984375f /*00741DA0*/); }
float RefreshPeriod() { return 0.5f /*0073B86C*/; }   // 0.5 s
float MillisecondsPerSecond() { return 1000.0f /*0073C34C*/; }
float BarWidth() { return 355.0f /*00741E84*/; }        // 355
float BarLeft() { return 225.0f /*00741E80*/; }         // 225
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

// The connection screen's helpers through their addresses (rewritten here; the wrappers run).
__declspec(naked) void __fastcall DrawListAt(CMpConnectScreen*) EI_ORIGINAL(0x0061D780)   // Rebirth-patched
__declspec(naked) void __fastcall MasterRequestAt(CMpConnectScreen*) EI_ORIGINAL(0x0061D0D0)
__declspec(naked) int __fastcall MasterLinkTickAt(net::MasterLink*) EI_ORIGINAL(0x00440DE0)

// The original's inlined strcpy into a box's text buffer.
void CopyText(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }

// A message box of `ui` on top (allocation failure: null pushed, as the original).
void OpenBox(CInterface3D* ui, const char* title, const char* text, std::uint32_t result, int flags) {
    void* memory = gamecrt::operator_new(sizeof(CMessageBox));
    void* box = memory ? static_cast<CMessageBox*>(memory)->Construct(ui, title, text, static_cast<int>(result), flags, kBoxFormat)
                       : nullptr;
    ui->PushWindow(static_cast<CUiWindow*>(box), 1, 1);
}

// The file stream the address book is written with, built and torn down inline (the archive is
// LocalArchive, chunk_archive.h).
struct LocalFileStream {
    LocalFileStream() { stream.Construct(); }
    ~LocalFileStream() { stream.Destruct(); }
    LocalFileStream(const LocalFileStream&) = delete;
    LocalFileStream& operator=(const LocalFileStream&) = delete;
    netlink::FileStream stream;
};

// The queries Create reads from the address book: an empty vector, its replies and storage
// released at the end (inline in the original; the node allocator's deallocate, 00404D70).
struct LocalQueries {
    LocalQueries() { queries.extra = 0; }
    ~LocalQueries() {
        stlp::vector<net::Query>& list = queries.list;
        for (net::Query* query = list.begin(); query != list.end(); ++query) query->reply.DestructGrowing();
        if (list.begin())
            libcrt::NodeDeallocate(list.begin(), static_cast<std::size_t>(list.EndOfStorage() - list.begin()) * sizeof(net::Query));
    }
    LocalQueries(const LocalQueries&) = delete;
    LocalQueries& operator=(const LocalQueries&) = delete;
    net::ClientQueries queries;
};

// Create's layout (0061B2C0).
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
// The model's turn about x: 007C7F40 (an angle set at start) times 1.2 (0073CDF4).
float MenuTurn() { return GameFloat(0x007C7F40) * 1.2f /*0073CDF4*/; }
// The interface's text colour (the list draw changes it per row).
std::uint32_t TextColour() { return ei::GameData<const volatile std::uint32_t>(0x00793AD8); }
constexpr std::uint32_t kCaretColour = 0x31B3FF;
// ExtendedServerName's values in Create (addon.dll: operands at 0061B5CE, 0061BB51, 0061BC09).
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
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    return memory ? static_cast<CUiSurface*>(memory)->Construct(ui, left, top, right, bottom, kSurfaceFormat, 1) : nullptr;
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

// --- The list's vector members (0061E5A0-0061EE80) ------------------------------------------------

MpAddressEntry* MpAddressEntry::CopyConstruct(const MpAddressEntry* other) {
    new (&name) mfc::CString(other->name);
    new (&description) mfc::CString(other->description);
    players = other->players;
    maxPlayers = other->maxPlayers;
    ping = other->ping;
    address = other->address;
    gameType = other->gameType;
    foreign = other->foreign;
    state = other->state;
    password = other->password;
    return this;
}
EI_REWRITTEN(0x0061EE80, MpAddressEntry::CopyConstruct);

MpAddressEntry* MpAddressEntry::AssignFrom(const MpAddressEntry& other) {
    name = other.name;
    description = other.description;
    players = other.players;
    maxPlayers = other.maxPlayers;
    ping = other.ping;
    address = other.address;
    gameType = other.gameType;
    foreign = other.foreign;
    state = other.state;
    password = other.password;
    return this;
}
EI_REWRITTEN(0x0061E8F0, MpAddressEntry::AssignFrom);

void MpAddressEntry::Destroy() {
    description.~CString();
    name.~CString();
}
EI_REWRITTEN(0x0061E8A0, MpAddressEntry::Destroy);

void __fastcall MpAddressesPushDefault(MpAddressList* list) {
    MpAddressEntry*& finish = list->Finish();
    if (finish != list->EndOfStorage()) {
        if (finish) {
            new (&finish->name) mfc::CString();
            new (&finish->description) mfc::CString();
            finish->address = net::NetAddress{};
        }
        ++finish;
        return;
    }
    MpAddressEntry entry{};
    list->InsertOverflow<recordvec::MpAddressElement>(finish, entry, 1);
}
EI_REWRITTEN(0x0061E5A0, MpAddressesPushDefault);

MpAddressEntry* __fastcall MpAddressesInsertDefault(MpAddressList* list, void*, MpAddressEntry* pos) {
    MpAddressEntry* begin = list->begin();
    int index = static_cast<int>(pos - begin);
    MpAddressEntry*& finish = list->Finish();
    if (finish == list->EndOfStorage()) {
        MpAddressEntry entry{};
        list->InsertOverflow<recordvec::MpAddressElement>(pos, entry, 1);
    } else if (pos == finish) {
        if (finish) {
            new (&finish->name) mfc::CString();
            new (&finish->description) mfc::CString();
            finish->address = net::NetAddress{};
        }
        ++finish;
    } else {
        // The last entry copied one up, the rest moved up by assignment, then the default assigned.
        if (finish) finish->CopyConstruct(finish - 1);
        ++finish;
        MpAddressEntry* to = finish - 1;
        for (int n = static_cast<int>((finish - 2) - pos); n > 0; --n) {
            --to;
            to->AssignFrom(*(to - 1));
        }
        MpAddressEntry entry{};
        pos->AssignFrom(entry);
    }
    return list->begin() + index;
}
EI_REWRITTEN(0x0061E660, MpAddressesInsertDefault);

MpAddressEntry* __fastcall MpAddressesErase(MpAddressList* list, void*, MpAddressEntry* first, MpAddressEntry* last) {
    MpAddressEntry* to = first;
    for (int n = static_cast<int>(list->end() - last); n > 0; --n, ++to, ++last) to->AssignFrom(*last);
    for (MpAddressEntry* entry = to; entry != list->end(); ++entry) entry->Destroy();
    list->Finish() = to;
    return first;
}
EI_REWRITTEN(0x0061EA90, MpAddressesErase);

// --- 0061C430: Update (slot 24) -------------------------------------------------------------------

void CMpConnectScreen::Update() {
    net::ClientSession* client = net::GameClient();
    bool asking = client->joinState_ == net::ClientBase::kAsking;
    if (asking) {
        surfaces_.data[kSurfaceConnecting]->Freeze(1, 0);
    } else {
        surfaces_.data[kSurfaceConnecting]->Freeze(0, 0);
        if (joining_) {
            joining_ = false;
            mfc::CString key;
            key = RefusalKey(client->refusal_);
            void* memory = gamecrt::operator_new(sizeof(CMessageBox));
            void* box = nullptr;
            if (memory) {
                mfc::CString text = key + MessageSuffix();
                box = static_cast<CMessageBox*>(memory)->Construct(interface_, key, text, kBoxNotice, kBoxFlagsOk, kBoxFormat);
            }
            interface_->PushWindow(static_cast<CUiWindow*>(box), 1, 1);
            return;
        }
    }
    joining_ = asking;
    UiUpdateChildrenOriginal(this);

    double elapsed = WindowFrameTimeOriginal(this) + refreshTimer_;
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
        DrawListAt(this);
    }

    int position;
    if (scroll_->TakePosition(&position)) {
        scrollTop_ = position;
        DrawListAt(this);
    }
    if (playersSlider_->TakePosition(&position)) {
        maxPlayers_ = position + 1;
        DrawListAt(this);
    }
    bool renamed = nameEdit_->changed_;
    nameEdit_->changed_ = false;
    if (renamed) DrawListAt(this);
}
EI_REPLACE_METHOD(0x0061C430, CMpConnectScreen::Update);

// --- 0061C2E0: a box answered (slot 27) -----------------------------------------------------------

void CMpConnectScreen::OnBoxResult(std::uint32_t result) {
    if (result == (kBoxMaster | kAnswerCancel)) {
        DrawListAt(this);
    } else if (result == (kBoxAddress | kAnswerOk)) {
        net::NetAddress address{};
        if (address.Parse(boxText_, DefaultPort())) net::GameClient()->Ask(&address);
        SaveAddressBook();
        DrawListAt(this);
    } else if (result == (kBoxPassword | kAnswerOk) || result == (kBoxPassword | kAnswerCancel)) {
        hostPassword_ = boxText_;
        auto* sprite = SceneMesh(kSpritePassword);
        sprite->textureOffsetU_ = hostPassword_.GetLength() == 0 ? FrameOff() : FrameOn();
    } else if (result == (kBoxJoinPassword | kAnswerOk)) {
        TheGameFlow()->JoinServer(&entries_[selected_].address, boxText_);
        net::GameClient()->gameType_ = entries_[selected_].gameType;
    }
}
EI_REPLACE_METHOD(0x0061C2E0, CMpConnectScreen::OnBoxResult);

// --- 0061C700 / 0061CA30 / 0061CAD0: input --------------------------------------------------------

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
        MasterRequestAt(this);
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
        if (hostPassword_.GetLength() == 0) {
            CopyText(boxText_, hostPassword_);
            OpenBox(interface_, texts_[kTextPassword], boxText_, kBoxPassword, kBoxFlagsEdit);
        } else {
            hostPassword_.Empty();
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
        DrawListAt(this);
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
                DrawListAt(this);
            }
        }
        break;
    }
    reinterpret_cast<UiContainerInput*>(InputSide())->OnLeftDown(buttons, x, y);
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
    static_cast<UiContainerInput*>(InputSide())->OnLeftDouble(buttons, x, y);
}

void CMpConnectScreen::OnKeyDown(int key, int repeat, int flags) {
    InputOf(this)->OnKeyDown(key, repeat, flags);
    int step;
    switch (key) {
    case VK_RETURN:
        Connect();
        inputHandled_ = true;
        return;
    case VK_ESCAPE:
        interface_->PlaySound(CancelSound(), 0);
        TheGameFlow()->DropSessions();
        interface_->SetPendingValue(kLeaveScreen);
        inputHandled_ = true;
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
    inputHandled_ = true;
}

EI_REPLACE_METHOD(0x0061C700, InputForward<&CMpConnectScreen::OnLButtonDown>::Call);
EI_REPLACE_METHOD(0x0061CA30, InputForward<&CMpConnectScreen::OnLButtonDouble>::Call);
EI_REPLACE_METHOD(0x0061CAD0, InputForward<&CMpConnectScreen::OnKeyDown>::Call);

// --- 0061CC00: the selection kept in the list and in view -----------------------------------------

void CMpConnectScreen::SelectAndScroll() {
    if (selected_ < 0) selected_ = 0;
    if (static_cast<unsigned>(selected_) >= entries_.size()) selected_ = static_cast<int>(entries_.size()) - 1;
    int row = selected_;
    selectedAddress_ = entries_.begin()[row].address;
    if (!(scrollTop_ + kRows > row)) scrollTop_ = row - (kRows - 1);
    if (scrollTop_ > row) scrollTop_ = row;
    scroll_->CallSetPosition(scrollTop_);
    scroll_->TakePosition(&scrollTop_);
    DrawListAt(this);
}
EI_REWRITTEN(0x0061CC00, CMpConnectScreen::SelectAndScroll);

// --- 0061CCE0: connect or host --------------------------------------------------------------------

void CMpConnectScreen::Connect() {
    interface_->PlaySound(OkSound(), 0);
    int row = selected_;
    if (row == 0) {
        int players;
        playersSlider_->TakePosition(&players);
        ++players;
        mfc::CString name(nameEdit_->text_);
        if (name.GetLength() == 0) name = DefaultGameName();
        SaveAddressBook();
        TheGameFlow()->StartServer(name, players, gameType_, hostPassword_, static_cast<char>(unlisted_),
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
        OpenBox(interface_, texts_[kTextPassword], boxText_, kBoxJoinPassword, kBoxFlagsEdit);
    }
    TheGameFlow()->JoinServer(&entries_[selected_].address, EmptyText());
    net::GameClient()->gameType_ = entries_[selected_].gameType;
}
EI_REWRITTEN(0x0061CCE0, CMpConnectScreen::Connect);

// --- 0061CF50: add or forget an address -----------------------------------------------------------

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
        stlp::string text;
        CopyText(boxText_, entry.address.ToString(&text)->data());
    }
    OpenBox(interface_, texts_[kTextAddress], boxText_, kBoxAddress, kBoxFlagsEdit);
}
EI_REWRITTEN(0x0061CF50, CMpConnectScreen::EditAddress);

// --- 0061D0D0: ask the master server --------------------------------------------------------------

void CMpConnectScreen::MasterRequest() {
    CopyText(boxText_, EmptyText());
    net::NetAddress master{};
    // EI_Plugin's NewMaster (its call at 0061D138): with a second master set, the master is tried first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster) && net::second_master::Configured())
        net::second_master::ParseMaster(&master, MasterHost(), kMasterPort);
    else
        master.Parse(MasterHost(), kMasterPort);
    net::ClientSession* client = net::GameClient();
    std::uint32_t extra = client ? client->queries_.extra : 0;
    void* memory = gamecrt::operator_new(sizeof(CMasterRequestBox));
    CMasterRequestBox* box =
        memory ? static_cast<CMasterRequestBox*>(memory)->Construct(interface_, texts_[kTextMaster], boxText_, kBoxMaster, &master, extra)
               : nullptr;
    interface_->PushWindow(box, 1, 1);
}
EI_REPLACE_METHOD(0x0061D0D0, CMpConnectScreen::MasterRequest);

// --- 0061D1C0: config\addrbook.ini ----------------------------------------------------------------

void CMpConnectScreen::SaveAddressBook() {
    LocalArchive local;
    LocalFileStream file;
    if (!file.stream.Open(TheDataFolder() + AddressBookFile(), netlink::FileStream::kOpenWrite, 0)) return;
    ChunkArchive& archive = local.archive;
    archive.Open(&file.stream, 0, 1);
    net::ClientQueries* queries = &net::GameClient()->queries_;
    if (archive.EnterChunk(1)) {
        queries->Serialize(&archive);
        archive.LeaveChunk();
    }
    archive.Close(&file.stream);
    file.stream.Close();
}
EI_REWRITTEN(0x0061D1C0, CMpConnectScreen::SaveAddressBook);

// --- 0061D470: the list from the client's servers -------------------------------------------------

void CMpConnectScreen::RebuildList() {
    MpAddressesErase(&entries_, nullptr, entries_.begin(), entries_.end());
    unsigned index = 0;
    for (net::ServerEntry* server = net::GameClient()->ServerAt(0); server;
         server = net::GameClient()->ServerAt(static_cast<int>(++index))) {
        MpAddressesPushDefault(&entries_);
        MpAddressEntry& entry = entries_.back();
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

    // std::sort(begin, end, key) as the original inlines it.
    MpAddressEntry* first = entries_.begin();
    MpAddressEntry* last = entries_.end();
    int key = sortKey_;
    if (first != last) {
        int count = static_cast<int>(last - first);
        int depth = 0;
        for (int n = count; n != 1; n >>= 1) ++depth;
        stl_sorts::ServerIntroSort(first, last, nullptr, depth * 2, key);
        constexpr int kThreshold = 16;
        if (count > kThreshold) {
            stl_sorts::ServerInsertionSort(first, first + kThreshold, key);
            stl_sorts::ServerUnguardedInsertionSort(first + kThreshold, last, nullptr, key);
        } else {
            stl_sorts::ServerInsertionSort(first, last, key);
        }
    }

    MpAddressesInsertDefault(&entries_, nullptr, entries_.begin());
    MpAddressEntry& newGame = entries_[0];
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
        scroll_->CallSetPosition(position);
    } else {
        scroll_->SetMaximum(0);
    }
    scrollTop_ = 0;
}
EI_REWRITTEN(0x0061D470, CMpConnectScreen::RebuildList);

// --- 0061B2C0: Create (the vanilla body) ----------------------------------------------------------

void CMpConnectScreen::Create() {
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
        added->CallSetRect(left, top, right, bottom);
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
    // ExtendedServerName (0061B5CE): the selection's right edge 370 -> 381.5.
    const bool extended = ei::rebirth::Enabled(ei::rebirth::Patch::ExtendedServerName);
    plain(sprite(3.0f, 108.0f, 108.0f, extended ? kExtendedSelectionRight : 370.0f, 132.0f, 0, 0, 0x100, 0x100),
          kSelectionColour);
    {
        // The sort arrow, from the scroll bar's texture.
        CUiMesh* arrow = AddSprite(nullptr);
        arrow->texture_ = objects_.data[kScrollbarTexture];
        arrow->SetRectangle(4.0f, 100.0f, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0x80, 0x50, 0xB0);
        arrow->visible_ = 0;
    }
    sprite(4.0f, 440.0f, 346.0f, 461.0f, 367.0f, 0xD1, 0x37, 0xE6, 0x4C)->textureOffsetU_ = -kTabSelected;
    sprite(4.0f, 440.0f, 370.0f, 461.0f, 391.0f, 0xD1, 0x37, 0xE6, 0x4C);

    void* texts = GameClientTexts();   // 00578E20: the client's text table
    mfc::CString text;
    for (int i = 0, top = 238; top < 334; top += 24, ++i) {
        text.Format(AllodNameFormat(), i);
        TextsGetString(texts, nullptr, text, &text);
        allodNames_.SetAtGrow(allodNames_.GetSize(), text);
        const int bottom = top + 21;
        sprite(4.0f, 460.0f, static_cast<float>(top), 481.0f, static_cast<float>(bottom), 0xD1, 0x37, 0xE6, 0x4C);
        region(460, top, 670, bottom, 0);
    }
    SceneMesh(kSpriteTabFirst)->textureOffsetU_ = kTabSelected;
    gameType_ = 0;
    unlisted_ = true;
    hostPassword_.Empty();
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
        void* memory = gamecrt::operator_new(sizeof(CUiScrollbar));
        scroll_ = memory ? static_cast<CUiScrollbar*>(memory)->Construct(interface_, RECT{108, 132, 384, 492}, 3.0f, 0, 1.0f)
                         : nullptr;
        AddChild(scroll_);
    }
    {
        // EI_Plugin's NPlayers (0061BAF7, 0061BB82): the slider's maximum is max(players, 7) - 1 and its
        // position max(players, 1) - 1 instead of 5 and 5. addon's ExtendedServerName (0061BB51) makes the
        // right edge of the layout rectangle, set after the construction, and of the name's field 680.
        const bool plugin = ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNPlayers);
        const int players = ei::rebirth::PluginNPlayers();
        const int maximum = plugin ? (players < 7 ? 7 : players) - 1 : 5;
        const int position = plugin ? (players < 1 ? 1 : players) - 1 : 5;
        const int right = extended ? kExtendedFieldRight : 650;
        void* memory = gamecrt::operator_new(sizeof(CUiSlider));
        playersSlider_ =
            memory ? static_cast<CUiSlider*>(memory)->Construct(interface_, RECT{450, 196, 650, 206}, 3.0f, maximum, 1.0f)
                   : nullptr;
        AddChild(playersSlider_);
        RECT rect{450, 196, right, 206};
        playersSlider_->CallSetRect(&rect);
        playersSlider_->CallSetPosition(position);
        maxPlayers_ = 6;
    }
    {
        void* memory = gamecrt::operator_new(sizeof(CUiEditField));
        nameEdit_ = memory ? static_cast<CUiEditField*>(memory)->Construct(
                                 interface_, RECT{450, 144, extended ? kExtendedFieldRight : 650, 160})
                           : nullptr;
        AddChild(nameEdit_);
        // ExtendedServerName (0061BC09, 0061BC0B, 0061BC0E): the classes 15, font 0, at most 50
        // characters (the name taken from the hero below still stops at 10).
        if (extended)
            nameEdit_->SetLook(15, 0, 0, kExtendedNameLength, TextColour(), kCaretColour);
        else
            nameEdit_->SetLook(3, 0, 1, kNameLength, TextColour(), kCaretColour);
    }
    // The new game's name: the first hero's, to its '|', at most 10 characters.
    mfc::CString name(reinterpret_cast<CParty*>(LocalPlayerData())->members_.data[0].texts[0]);
    {
        const int end = name.Find(NameEnd());
        if (end >= 0) name = name.Left(end);
        if (name.GetLength() > kNameLength) name = name.Left(kNameLength);
        name.TrimLeft();
        name.TrimRight();
        nameEdit_->SetText(name);
        nameEdit_->SetEnabled(0, 1);
        nameEdit_->SetFocused(false);
    }
    for (int i = 0; i < kTextCount; ++i) {
        text.Format(ConnectionTextFormat(), i);
        TextsGetString(texts, nullptr, text, &text);
        texts_.SetAtGrow(texts_.GetSize(), text);
    }

    TheGameFlow()->StartClient();
    spare_ = 0;
    // The address book: the queries the client asks.
    LocalQueries book;
    LocalArchive local;
    LocalFileStream file;
    if (file.stream.Open(TheDataFolder() + AddressBookFile(), netlink::FileStream::kOpenRead, 1)) {
        ChunkArchive& archive = local.archive;
        archive.Open(&file.stream, 1, 1);
        if (archive.EnterChunk(1)) {
            book.queries.Serialize(&archive);
            archive.LeaveChunk();
        }
        archive.Close(&file.stream);
        file.stream.Close();
        if (book.queries.extra == 0) {
            const std::uint32_t random = IsaacRandom::Global().Next();
            book.queries.extra = gameapi::GetTickCount() ^ random;
        }
        net::GameClient()->SetQueries(&book.queries);
    }

    sortKey_ = 0;
    joining_ = false;
    selected_ = 0;
    newGameTips_ = false;
    selectedAddress_ = net::NetAddress{};
    RebuildList();
    DrawListAt(this);
    interface_->OpenHelp(0);
    refreshTimer_ = 0.0f;
    // Surface 1: "connecting".
    CUiSurface* note = AddSurface(NewSurface(interface_, 0, 0, 300, 20));
    UiSurfaceGetDC(note);
    SurfaceClearOriginal(note);
    RECT rect{0, 0, 300, 20};
    SurfaceDrawText(note, nullptr, texts_[kTextConnecting], &rect, 2, TextColour(), 0, 1);
    UiSurfaceReleaseDC(note);
}
EI_REPLACE_METHOD(0x0061B2C0, CMpConnectScreen::Create);
// Rebirth's patches in Create: addon's ExtendedServerName sites (0061B5CE, 0061BB51, 0061BC09, 0061BC0B,
// 0061BC0E) and EI_Plugin's NPlayers (0061BAF7, 0061BB82).

// --- 0061E2F0 / 0061E4E0: the master request box --------------------------------------------------

CMasterRequestBox* CMasterRequestBox::Construct(CInterface3D* ui, const char* titleKey, const char* textKey,
                                                std::uint32_t result, const net::NetAddress* address,
                                                std::uint32_t extra) {
    static_cast<CMessageBox*>(this)->Construct(ui, titleKey, textKey, static_cast<int>(result), kBoxFlagsCancel, kBoxFormat);
    link_.Construct(address);
    vtable_ = Vtable();
    inputVtable_ = InputVtable();
    bar_ = AddSprite(nullptr);
    bar_->texture_ = nullptr;
    bar_->flags_ |= kPlainSprite;
    bar_->flatColour_ = kBarColour;
    BufferPut(&link_.out, &extra, sizeof(extra));
    return this;
}
EI_REWRITTEN(0x0061E2F0, CMasterRequestBox::Construct);

void CMasterRequestBox::Update() {
    double share = net::GameClient()->BufferRatioImpl(&link_.in);
    float right = static_cast<float>(share * BarWidth() + BarLeft());
    bar_->SetRectangle(2.5f, 100.0f, 220.0f, 280.0f, right, 300.0f, 0, 0, 0x100, 0x100);
    if (MasterLinkTickAt(&link_) == net::MasterLink::kDone) net::GameClient()->ReadServerListImpl(&link_.in);
    if (MasterLinkTickAt(&link_) == net::MasterLink::kDone || MasterLinkTickAt(&link_) == net::MasterLink::kClosed)
        interface_->SetPendingValue(CMpConnectScreen::kBoxMaster | kAnswerCancel);
}
EI_REPLACE_METHOD(0x0061E4E0, CMasterRequestBox::Update);
