// Checks for the multiplayer connection screen, its master request box and the address list's
// vector members (ui_connect_screen.cpp). Multiplayer only: the screen opens from the multiplayer
// hero screen's Continue with a hero (ui_mp_host does: the list draw 0061D780 is reached and compared
// side by side; the rest is not reached). The others are ours only (they open windows, write
// config\addrbook.ini, talk to the network or reallocate the list); the checks are invariants of the result.
#include "verify.h"
#include "ui_surface_pixels.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/ui.h"
#include "../../src/game/ui_screens.h"

#include <cstring>

namespace {
void* s_unused;

bool SameAddress(const net::NetAddress& a, const net::NetAddress& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }
bool ZeroAddress(const net::NetAddress& a) { return SameAddress(a, net::NetAddress{}); }

// The selection is a row of the list (or the list is empty) and its address is the row's.
bool SelectionValid(const CMpConnectScreen* self) {
    unsigned rows = static_cast<unsigned>(self->entries_.size());
    if (rows == 0) return true;
    return self->selected_ >= 0 && static_cast<unsigned>(self->selected_) < rows;
}
bool NewGameFirst(const CMpConnectScreen* self) {
    return self->entries_.size() > 0 && self->entries_[0].gameType == 4 && self->entries_[0].ping == 9999 &&
           ZeroAddress(self->entries_[0].address);
}
} // namespace

// 0061EE80 / 0061E8F0: every field equal to the source's afterwards; the result is the entry.
static MpAddressEntry* __fastcall AddressCopyVerify(MpAddressEntry* self, void*, const MpAddressEntry* other) {
    MpAddressEntry* result = self->CopyConstruct(other);
    bool ok = result == self && std::strcmp(self->name, other->name) == 0 &&
              std::strcmp(self->description, other->description) == 0 && self->ping == other->ping &&
              SameAddress(self->address, other->address) && self->state == other->state && self->password == other->password;
    ei::verify::Check("MpAddressEntry::CopyConstruct", true, ok, "");
    return result;
}
EI_VERIFY(0x0061EE80, AddressCopyVerify, 0, s_unused);

static MpAddressEntry* __fastcall AddressAssignVerify(MpAddressEntry* self, void*, const MpAddressEntry* other) {
    MpAddressEntry* result = self->AssignFrom(*other);
    bool ok = result == self && std::strcmp(self->name, other->name) == 0 &&
              std::strcmp(self->description, other->description) == 0 && self->players == other->players &&
              self->gameType == other->gameType && SameAddress(self->address, other->address) &&
              self->foreign == other->foreign && self->password == other->password;
    ei::verify::Check("MpAddressEntry::AssignFrom", true, ok, "");
    return result;
}
EI_VERIFY(0x0061E8F0, AddressAssignVerify, 0, s_unused);

// 0061E8A0: nothing to check afterwards (the texts are released).
static void __fastcall AddressDestroyVerify(MpAddressEntry* self) {
    self->Destroy();
    ei::verify::Check("MpAddressEntry::Destroy", true, true, "");
}
EI_VERIFY(0x0061E8A0, AddressDestroyVerify, 0, s_unused);

// 0061E5A0 / 0061E660 / 0061EA90: the list's size changes by one (by the range erased); a new
// entry has empty texts and address 0; the results are where the original returns them.
static void __fastcall AddressesPushVerify(MpAddressList* list) {
    std::size_t size = list->size();
    MpAddressesPushDefault(list);
    const MpAddressEntry& entry = list->back();
    bool ok = list->size() == size + 1 && entry.name.GetLength() == 0 && entry.description.GetLength() == 0 &&
              ZeroAddress(entry.address);
    ei::verify::Check("MpAddressesPushDefault", true, ok, "size %u -> %u", unsigned(size), unsigned(list->size()));
}
EI_VERIFY(0x0061E5A0, AddressesPushVerify, 0, s_unused);

static MpAddressEntry* __fastcall AddressesInsertVerify(MpAddressList* list, void*, MpAddressEntry* pos) {
    std::size_t size = list->size();
    std::ptrdiff_t index = pos - list->begin();
    MpAddressEntry* result = MpAddressesInsertDefault(list, nullptr, pos);
    bool ok = list->size() == size + 1 && result == list->begin() + index && result->name.GetLength() == 0 &&
              result->description.GetLength() == 0 && ZeroAddress(result->address);
    ei::verify::Check("MpAddressesInsertDefault", true, ok, "index %d size %u", int(index), unsigned(list->size()));
    return result;
}
EI_VERIFY(0x0061E660, AddressesInsertVerify, 0, s_unused);

static MpAddressEntry* __fastcall AddressesEraseVerify(MpAddressList* list, void*, MpAddressEntry* first,
                                                        MpAddressEntry* last) {
    std::size_t size = list->size();
    std::size_t erased = static_cast<std::size_t>(last - first);
    MpAddressEntry* result = MpAddressesErase(list, nullptr, first, last);
    ei::verify::Check("MpAddressesErase", true, result == first && list->size() == size - erased, "size %u -> %u",
                      unsigned(size), unsigned(list->size()));
    return result;
}
EI_VERIFY(0x0061EA90, AddressesEraseVerify, 0, s_unused);

// 0061C430: the selection stays a row; the timer below the period (unless a refusal box ended
// the frame first).
static void __fastcall ConnectUpdateVerify(CMpConnectScreen* self) {
    self->Update();
    ei::verify::Check("CMpConnectScreen::Update", true, SelectionValid(self) && !(self->refreshTimer_ > 0.5f),
                      "selected %d timer %f", self->selected_, double(self->refreshTimer_));
}
EI_VERIFY(0x0061C430, ConnectUpdateVerify, 0, s_unused);

// 0061C2E0: the new game's password answers keep the box text.
static void __fastcall ConnectBoxResultVerify(CMpConnectScreen* self, void*, std::uint32_t result) {
    self->OnBoxResult(result);
    bool password = result == 0x30001 || result == 0x30002;
    ei::verify::Check("CMpConnectScreen::OnBoxResult", true,
                      !password || std::strcmp(self->hostPassword_, self->boxText_) == 0, "result %08X", result);
}
EI_VERIFY(0x0061C2E0, ConnectBoxResultVerify, 0, s_unused);

// 0061C700 / 0061CA30 / 0061CAD0: the selection stays a row of the list.
static void __fastcall ConnectLButtonDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CMpConnectScreen>(input);
    self->OnLButtonDown(buttons, x, y);
    ei::verify::Check("CMpConnectScreen::OnLButtonDown", true, SelectionValid(self), "selected %d", self->selected_);
}
EI_VERIFY(0x0061C700, ConnectLButtonDownVerify, 0, s_unused);

static void __fastcall ConnectLButtonDoubleVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CMpConnectScreen>(input);
    self->OnLButtonDouble(buttons, x, y);
    ei::verify::Check("CMpConnectScreen::OnLButtonDouble", true, SelectionValid(self), "selected %d", self->selected_);
}
EI_VERIFY(0x0061CA30, ConnectLButtonDoubleVerify, 0, s_unused);

static void __fastcall ConnectKeyDownVerify(void* input, void*, int key, int repeat, int flags) {
    auto* self = WindowFromInput<CMpConnectScreen>(input);
    self->OnKeyDown(key, repeat, flags);
    ei::verify::Check("CMpConnectScreen::OnKeyDown", true, SelectionValid(self), "key %d selected %d", key,
                      self->selected_);
}
EI_VERIFY(0x0061CAD0, ConnectKeyDownVerify, 0, s_unused);

// 0061CC00: a non-empty list's selection is a row, its address the row's, and it lies at or below
// the first row shown.
static void __fastcall ConnectSelectVerify(CMpConnectScreen* self) {
    self->SelectAndScroll();
    bool ok = self->entries_.size() == 0 ||
              (SelectionValid(self) && SameAddress(self->selectedAddress_, self->entries_[self->selected_].address) &&
               self->scrollTop_ <= self->selected_);
    ei::verify::Check("CMpConnectScreen::SelectAndScroll", true, ok, "selected %d top %d", self->selected_,
                      self->scrollTop_);
}
EI_VERIFY(0x0061CC00, ConnectSelectVerify, 0, s_unused);

// 0061CCE0 / 0061CF50 / 0061D1C0: they host, join, open boxes or write the address book; the
// list is not changed.
static void __fastcall ConnectConnectVerify(CMpConnectScreen* self) {
    std::size_t rows = self->entries_.size();
    self->Connect();
    ei::verify::Check("CMpConnectScreen::Connect", rows, self->entries_.size(), "rows %u", unsigned(rows));
}
EI_VERIFY(0x0061CCE0, ConnectConnectVerify, 0, s_unused);

static void __fastcall ConnectEditVerify(CMpConnectScreen* self) {
    std::size_t rows = self->entries_.size();
    self->EditAddress();
    ei::verify::Check("CMpConnectScreen::EditAddress", rows, self->entries_.size(), "rows %u", unsigned(rows));
}
EI_VERIFY(0x0061CF50, ConnectEditVerify, 0, s_unused);

static void __fastcall ConnectSaveVerify(CMpConnectScreen* self) {
    std::size_t rows = self->entries_.size();
    self->SaveAddressBook();
    ei::verify::Check("CMpConnectScreen::SaveAddressBook", rows, self->entries_.size(), "rows %u", unsigned(rows));
}
EI_VERIFY(0x0061D1C0, ConnectSaveVerify, 0, s_unused);

// 0061D0D0: the request box is the window on top.
static void __fastcall ConnectMasterVerify(CMpConnectScreen* self) {
    self->MasterRequest();
    CUiWindow* top = self->interface_->GetActiveWindow();
    ei::verify::Check("CMpConnectScreen::MasterRequest", true, top && top->vtable_ == CMasterRequestBox::Vtable(),
                      "top %p", static_cast<void*>(top));
}
EI_VERIFY(0x0061D0D0, ConnectMasterVerify, 0, s_unused);

// 0061D470: the new game entry first, the selection a row (row 0 with address 0), scrolled to the top.
static void __fastcall ConnectRebuildVerify(CMpConnectScreen* self) {
    self->RebuildList();
    bool ok = NewGameFirst(self) && SelectionValid(self) && self->scrollTop_ == 0 &&
              (self->selected_ != 0 || ZeroAddress(self->selectedAddress_));
    ei::verify::Check("CMpConnectScreen::RebuildList", true, ok, "rows %u selected %d", unsigned(self->entries_.size()),
                      self->selected_);
}
EI_VERIFY(0x0061D470, ConnectRebuildVerify, 0, s_unused);

// 0061E2F0: the box's vtables, its bar sprite (plain, colour 0xFFA06800) and the result kept.
static CMasterRequestBox* __fastcall MasterBoxConstructVerify(CMasterRequestBox* self, void*, CInterface3D* ui,
                                                              const char* title, const char* text, std::uint32_t result,
                                                              const net::NetAddress* address, std::uint32_t extra) {
    CMasterRequestBox* box = self->Construct(ui, title, text, result, address, extra);
    bool ok = box == self && self->vtable_ == CMasterRequestBox::Vtable() && self->bar_ &&
              self->bar_->plainColour_ == 0xFFA06800u && self->result_ == result;
    ei::verify::Check("CMasterRequestBox::Construct", true, ok, "");
    return box;
}
EI_VERIFY(0x0061E2F0, MasterBoxConstructVerify, 0, s_unused);

// 0061E4E0: the link's state is one of its five.
static void __fastcall MasterBoxUpdateVerify(CMasterRequestBox* self) {
    self->Update();
    ei::verify::Check("CMasterRequestBox::Update", true, self->link_.state >= 0 && self->link_.state <= 4, "state %d",
                      self->link_.state);
}
EI_VERIFY(0x0061E4E0, MasterBoxUpdateVerify, 0, s_unused);

// 0061D780 CMpConnectScreen::DrawListVanilla (Rebirth-patched: WideScreen and ExtendedServerName are
// reproduced, so in a Rebirth launch this compares with addon's code, in the vanilla launch with the
// original): side by side on the real screen. What the function writes besides the picture (the
// sprites 2-9: visible, place; the new game's tips flag; the text colour 00793AD8) is put back
// before each run, recorded after the original and compared after ours; the surface's pixels too
// (ui_surface_pixels.h). Scenario: ui_mp_host (the list with the new game's row, its keys and sort
// columns). No server answers there, so three servers are appended for a moment (a short name,
// names too long for their column and for the details' line, a password, the states, the game
// types), each one selected in turn, and removed again before the real state is compared last.
static void(__fastcall* s_drawList)(CMpConnectScreen*);
namespace {
void CompareList(CMpConnectScreen* self, const char* what) {
    constexpr int kFirst = 2, kEnd = 10;
    struct SpriteState {
        int visible;
        Vector3 position;
    };
    auto sprite = [self](int index) { return reinterpret_cast<CUiMesh*>(self->sceneObjects_.data[index]); };
    std::uint32_t& colour = *reinterpret_cast<std::uint32_t*>(0x00793AD8);
    const std::uint32_t colourBefore = colour;
    const bool tips = self->newGameTips_;
    SpriteState before[kEnd - kFirst], expected[kEnd - kFirst];
    for (int i = kFirst; i < kEnd; ++i) before[i - kFirst] = {sprite(i)->visible_, sprite(i)->position_};
    bool expectedTips = tips;
    std::uint32_t expectedColour = colourBefore;
    auto restore = [&] {
        self->newGameTips_ = tips;
        colour = colourBefore;
        for (int i = kFirst; i < kEnd; ++i) {
            sprite(i)->visible_ = before[i - kFirst].visible;
            sprite(i)->position_ = before[i - kFirst].position;
        }
    };
    CUiSurface* surface = self->surfaces_.size > 0 ? self->surfaces_.data[0] : nullptr;
    ei::verify::gdi::DrawComparison c = ei::verify::gdi::CompareSurfaceDraws(
        surface,
        [&] {
            restore();
            s_drawList(self);
            expectedTips = self->newGameTips_;
            expectedColour = colour;
            for (int i = kFirst; i < kEnd; ++i) expected[i - kFirst] = {sprite(i)->visible_, sprite(i)->position_};
        },
        [&] {
            restore();
            self->DrawListVanilla();
        });
    int mismatches = 0;
    for (int i = kFirst; i < kEnd; ++i) {
        const SpriteState& e = expected[i - kFirst];
        const CUiMesh* mesh = sprite(i);
        if (mesh->visible_ != e.visible || !ei::verify::NearlyEqual<float>(mesh->position_.x, e.position.x, 4, 1e-5f) ||
            !ei::verify::NearlyEqual<float>(mesh->position_.y, e.position.y, 4, 1e-5f) ||
            !ei::verify::NearlyEqual<float>(mesh->position_.z, e.position.z, 4, 1e-5f))
            ++mismatches;
    }
    const bool ok = mismatches == 0 && self->newGameTips_ == expectedTips && colour == expectedColour && c.diff.count == 0 && c.compared && !c.unstable;
    ei::verify::Check("CMpConnectScreen::DrawListVanilla", true, ok,
                      "%s: selected %d of %u top %d: %d sprites differ, tips %d / %d, colour %06X / %06X; %d pixels differ in "
                      "%d,%d-%d,%d%s",
                      what, self->selected_, static_cast<unsigned>(self->entries_.size()), self->scrollTop_, mismatches,
                      expectedTips ? 1 : 0, self->newGameTips_ ? 1 : 0, expectedColour, colour, c.diff.count, c.diff.left,
                      c.diff.top, c.diff.right, c.diff.bottom, ei::verify::gdi::Unread(c));
}

struct SyntheticServer {
    const char* name;
    const char* description;
    int players, maxPlayers, ping, gameType, state;
    bool password;
};
const SyntheticServer kServers[] = {
    {"Short", "Map", 1, 6, 35, 0, 0, false},
    {"A server whose name is far too long for its column on the list", "A map title that is long enough to wrap over lines", 3, 8,
     120, 2, 1, true},
    {"A very very very very very very very long server name of eighty-odd characters!!", "Second", 6, 6, 9999, 4, 2, false},
};
} // namespace

static void __fastcall DrawListVerify(CMpConnectScreen* self) {
    if (self->surfaces_.size > 0 && self->entries_.size() > 0 && self->entries_.size() < 5) {
        const int selected = self->selected_;
        const int scrollTop = self->scrollTop_;
        const std::size_t kept = self->entries_.size();
        for (const SyntheticServer& server : kServers) {
            MpAddressesPushDefault(&self->entries_);
            MpAddressEntry& entry = self->entries_[self->entries_.size() - 1];
            entry.name = server.name;
            entry.description = server.description;
            entry.players = server.players;
            entry.maxPlayers = server.maxPlayers;
            entry.ping = server.ping;
            entry.gameType = server.gameType;
            entry.state = server.state;
            entry.password = server.password;
        }
        self->scrollTop_ = 0;
        for (std::size_t i = kept; i < self->entries_.size(); ++i) {
            self->selected_ = static_cast<int>(i);
            CompareList(self, "server selected");
        }
        self->selected_ = 0;
        CompareList(self, "servers listed, new game selected");
        MpAddressesErase(&self->entries_, nullptr, &self->entries_[kept], self->entries_.end());
        self->selected_ = selected;
        self->scrollTop_ = scrollTop;
    }
    CompareList(self, "list");
}
EI_VERIFY(0x0061D780, DrawListVerify, 7, s_drawList);

// 0061B2C0 CMpConnectScreen::Create (Rebirth-patched: the vanilla launch only): ours only (it
// loads textures and a model, makes children and starts the network client). Read back, as the
// differences Create makes: 29 regions (10 controls, 4 game type tabs, 15 rows), 18 sprites, two
// surfaces, three children, 4 allod names and 22 texts; the new game's defaults and row 0
// selected. Not reached (multiplayer only; see above).
static void __fastcall ConnectCreateVerify(CMpConnectScreen* self) {
    const int regions = self->regions_.size;
    const int sprites = self->sceneObjects_.size;
    const int surfaces = self->surfaces_.size;
    const int children = self->children_.size;
    const int names = self->allodNames_.GetSize();
    const int texts = self->texts_.GetSize();
    self->Create();
    const bool counts = self->regions_.size == regions + 29 && self->sceneObjects_.size == sprites + 18 &&
                        self->surfaces_.size == surfaces + 2 && self->children_.size == children + 3 &&
                        self->allodNames_.GetSize() == names + 4 && self->texts_.GetSize() == texts + 22;
    ei::verify::Check("CMpConnectScreen::Create counts", true, counts, "regions %d sprites %d surfaces %d children %d",
                      self->regions_.size, self->sceneObjects_.size, self->surfaces_.size, self->children_.size);
    const bool state = self->maxPlayers_ == 6 && self->gameType_ == 0 && self->unlisted_ && self->selected_ == 0 &&
                       self->sortKey_ == 0 && !self->joining_ && self->scroll_ && self->playersSlider_ && self->nameEdit_;
    ei::verify::Check("CMpConnectScreen::Create state", true, state, "players %d type %d unlisted %d selected %d",
                      self->maxPlayers_, self->gameType_, self->unlisted_ ? 1 : 0, self->selected_);
}
EI_VERIFY(0x0061B2C0, ConnectCreateVerify, 0, s_unused);
