// Checks for the multiplayer connection screen, its master request box and the address list's
// vector members (ui_connect_screen.cpp). Multiplayer only: the screen opens from the multiplayer
// hero screen's Continue with a hero, which no scenario does, so none of these is reached. All
// ours only (they open windows, write config\addrbook.ini, talk to the network or reallocate the
// list); the checks are invariants of the result.
#include "verify.h"

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
