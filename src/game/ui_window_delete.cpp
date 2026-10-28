// The interface windows' scalar deleting destructors (the destructor body, then the memory freed when bit 0
// of the flags is set; they return the object), the input sides' copies of them (`this` moved back from the
// input side to the window, then a jump to the window's), and the short destructor bodies only they call.
// Bodies that only jump to the window's or container's destructor are those destructors themselves. docs/notes/ui.md,
// section 2 "Deleting destructors".
#include "global_map_clouds.h"
#include "party.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "ui_screens.h"

#include "core_refcount.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "game_data.h"
#include "lib/mfc_slots.h"

#include <memory>
#include <string>

namespace {

// The vtables the bodies write.
void* ItemArrayVtable() { return mfc::impl::ItemStripArrayClass::Vtable(); }   // CArray<char*>
void* PartyArrayVtable() { return mfc::impl::UnitPtrArrayClass::Vtable(); }       // the battle screen's party
void* DwordArrayVtable() { return mfc::impl::IntArrayClass::Vtable(); }       // CDWordArray
void* ObjectiveExitsVtable() { return mfc::impl::ObjectiveExitArrayClass::Vtable(); }   // CArray<ObjectiveExit>

// An STLport vector's storage back to the node pool (its elements already destroyed).
template <typename T>
void FreeVectorStorage(T* begin, T* capacity) {
    stlp::deallocate(begin, static_cast<std::size_t>(capacity - begin) * sizeof(T));
}

// An STLport list emptied and its sentinel freed (the destructor's inlined clear).
template <typename T, typename F>
void DestroyList(stlp::list<T>& list, F destroy) {
    list.ClearWith(destroy);
    stlp::deallocate(list.head, sizeof(typename stlp::list<T>::Node));
}

} // namespace

// --- Destructor bodies -------------------------------------------------------------------------

// The input sides' storage: the game's heap.
void* UiInputSide::operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
void UiInputSide::operator delete(void* block) { gamecrt::operator_delete(block); }

// The base input side's deleting destructor.
UiInputSide::~UiInputSide() {}

// The inert extra side's deleting destructor, the base side's table back.
UiInertInput::~UiInertInput() {}

CItemInfoPanel::~CItemInfoPanel() {
    ItemParty()->CParty::~CParty();
}

CChatWindow::~CChatWindow() {
    for (CRefObject* player : players_)
        if (player) player->Release();
    players_.FreeStorage();
}

CForwardingStrip::~CForwardingStrip() {
    list_.vtable = ItemArrayVtable();
    list_.FreeData();
}

CSpellMakerPage::~CSpellMakerPage() = default;

CHeroStatsPanel::~CHeroStatsPanel() = default;

CMessageBox::~CMessageBox() = default;

// The link, then the box's.
CMasterRequestBox::~CMasterRequestBox() = default;

void ChatLine::Destroy() {
    if (CUiSurface* shown = surface) {
        delete shown;
    }
    surface = nullptr;
    std::destroy_at(&name);
    std::destroy_at(&text);
}

CChatOverlay::~CChatOverlay() { DestroyList(lines_, [](ChatLine* line) { line->Destroy(); }); }

CBattleScreen::~CBattleScreen() {
    heroes_.vtable = DwordArrayVtable();
    heroes_.FreeData();
    party_.vtable = PartyArrayVtable();
    party_.FreeData();
}

static_assert(sizeof(BriefingMover) == 0x30);

CBriefingScreen::~CBriefingScreen() {
    for (BriefingSpeaker* speaker = speakers_.begin(); speaker != speakers_.end(); ++speaker) std::destroy_at(&speaker->name);
    speakers_.FreeStorage();
    // The shown items' list (node 0x18 bytes: next, prev, then the item record).
    for (BriefingShownItem* node = shown_->next; node != shown_;) {
        BriefingShownItem* next = node->next;
        std::destroy_at(&node->name);
        stlp::deallocate(node, sizeof(BriefingShownItem));
        node = next;
    }
    shown_->next = shown_;
    shown_->prev = shown_;
    stlp::deallocate(shown_, sizeof(BriefingShownItem));
    for (BriefingMover* node = movers_->next; node != movers_;) {
        BriefingMover* next = node->next;
        stlp::deallocate(node, sizeof(BriefingMover));
        node = next;
    }
    movers_->next = movers_;
    movers_->prev = movers_;
    stlp::deallocate(movers_, sizeof(BriefingMover));
    party_.vtable = PartyArrayVtable();
    party_.FreeData();
}

CMpConnectScreen::~CMpConnectScreen() {
    for (MpAddressEntry& entry : entries_) {
        std::destroy_at(&entry.description);
        std::destroy_at(&entry.name);
    }
    entries_.FreeStorage();
}

CHelpWindow::~CHelpWindow() {}

CObjectivesWindow::~CObjectivesWindow() {
    exits_.vtable = ObjectiveExitsVtable();
    if (exits_.data) gamecrt::operator_delete(exits_.data);
}

CSaveLoadWindow::~CSaveLoadWindow() {
    for (SaveEntry* entry = entries_.begin(); entry != entries_.end(); ++entry) {
        std::destroy_at(&entry->stringArrays_[1]);
        std::destroy_at(&entry->stringArrays_[0]);
        std::destroy_at(&entry->zone_);
        std::destroy_at(&entry->map_);
        std::destroy_at(&entry->folder_);
        std::destroy_at(&entry->title_);
    }
    FreeVectorStorage(entries_.begin(), entries_.EndOfStorage());
}
