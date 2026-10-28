// The interface windows' scalar deleting destructors (the destructor body, then the memory freed when bit 0
// of the flags is set; they return the object), the input sides' copies of them (`this` moved back from the
// input side to the window, then a jump to the window's), and the short destructor bodies only they call.
// Bodies that only jump to the window's or container's destructor are registered as those. docs/notes/ui.md,
// section 2 "Deleting destructors".
#include "global_map_clouds.h"
#include "library_trees.h"
#include "party.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "ui_screens.h"

#include "core_refcount.h"
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "game_data.h"
#include "lib/mfc_slots.h"

#include <memory>

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

// A "pers" texts map destroyed as the screens' destructors inline it: with nodes, every node
// erased and the header reset, the count 0; then the header freed.
void DestroyPersonTexts(PersonTextMap& map) {
    PersonTextNode* header = map.end();
    if (map.size() != 0) {
        static_cast<lib_trees::PersonTexts*>(&map)->EraseSubtree(header->parent);
        map.ResetToEmpty();
    }
    stlp::deallocate(map.end(), sizeof(PersonTextNode));
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
EI_REWRITTEN_SPECIAL(0x005EA850, UiInputSide::~UiInputSide);

// The inert extra side's deleting destructor, the base side's table back.
UiInertInput::~UiInertInput() {}
EI_REWRITTEN_SPECIAL(0x00602BA0, UiInertInput::~UiInertInput);
EI_REWRITTEN_SPECIAL(0x00602B80, UiInertInput::~UiInertInput);

CItemInfoPanel::~CItemInfoPanel() {
    ItemParty()->CParty::~CParty();
}
EI_REWRITTEN_SPECIAL(0x005EE380, CItemInfoPanel::~CItemInfoPanel);
EI_REWRITTEN_SPECIAL(0x005EE420, CItemInfoPanel::~CItemInfoPanel);
EI_REWRITTEN_SPECIAL(0x005F4600, CItemInfoPanel::~CItemInfoPanel);

CChatWindow::~CChatWindow() {
    for (CRefObject* player : players_)
        if (player) player->Release();
    players_.FreeStorage();
}
EI_REWRITTEN_SPECIAL(0x00608ED0, CChatWindow::~CChatWindow);
EI_REWRITTEN_SPECIAL(0x00608EB0, CChatWindow::~CChatWindow);
EI_REWRITTEN_SPECIAL(0x0060C7D0, CChatWindow::~CChatWindow);

CForwardingStrip::~CForwardingStrip() {
    list_.vtable = ItemArrayVtable();
    list_.FreeData();
}
EI_REWRITTEN_SPECIAL(0x0060E2B0, CForwardingStrip::~CForwardingStrip);
EI_REWRITTEN_SPECIAL(0x0060E290, CForwardingStrip::~CForwardingStrip);
EI_REWRITTEN_SPECIAL(0x00613DC0, CForwardingStrip::~CForwardingStrip);

CSpellMakerPage::~CSpellMakerPage() = default;
EI_REWRITTEN_SPECIAL(0x0060E600, CSpellMakerPage::~CSpellMakerPage);
EI_REWRITTEN_SPECIAL(0x0060E5E0, CSpellMakerPage::~CSpellMakerPage);
EI_REWRITTEN_SPECIAL(0x00613DF0, CSpellMakerPage::~CSpellMakerPage);

CHeroStatsPanel::~CHeroStatsPanel() = default;
EI_REWRITTEN_SPECIAL(0x0060E360, CHeroStatsPanel::~CHeroStatsPanel);
EI_REWRITTEN_SPECIAL(0x0060E340, CHeroStatsPanel::~CHeroStatsPanel);
EI_REWRITTEN_SPECIAL(0x00613DD0, CHeroStatsPanel::~CHeroStatsPanel);

CMessageBox::~CMessageBox() = default;
EI_REWRITTEN_SPECIAL(0x0061E3E0, CMessageBox::~CMessageBox);
EI_REWRITTEN_SPECIAL(0x00633750, CMessageBox::~CMessageBox);   // the difficulty question's copy
EI_REWRITTEN_SPECIAL(0x00633730, CMessageBox::~CMessageBox);
EI_REWRITTEN_SPECIAL(0x00634770, CMessageBox::~CMessageBox);
EI_REWRITTEN_SPECIAL(0x006345E0, CMessageBox::~CMessageBox);
EI_REWRITTEN_SPECIAL(0x00634FD0, CMessageBox::~CMessageBox);

// The link, then the box's.
CMasterRequestBox::~CMasterRequestBox() = default;
EI_REWRITTEN_SPECIAL(0x0061E460, CMasterRequestBox::~CMasterRequestBox);
EI_REWRITTEN_SPECIAL(0x0061E440, CMasterRequestBox::~CMasterRequestBox);
EI_REWRITTEN_SPECIAL(0x00620300, CMasterRequestBox::~CMasterRequestBox);

void ChatLine::Destroy() {
    if (CUiSurface* shown = surface) {
        delete shown;
    }
    surface = nullptr;
    std::destroy_at(&name);
    std::destroy_at(&text);
}
EI_REWRITTEN(0x005E2670, ChatLine::Destroy);

CChatOverlay::~CChatOverlay() { DestroyList(lines_, [](ChatLine* line) { line->Destroy(); }); }
EI_REWRITTEN_SPECIAL(0x005E28E0, CChatOverlay::~CChatOverlay);
EI_REWRITTEN_SPECIAL(0x005E28C0, CChatOverlay::~CChatOverlay);
EI_REWRITTEN_SPECIAL(0x005E3C80, CChatOverlay::~CChatOverlay);

CBattleScreen::~CBattleScreen() {
    FreeVectorStorage(clientParty_.without.begin(), clientParty_.without.EndOfStorage());
    FreeVectorStorage(clientParty_.withUnit.begin(), clientParty_.withUnit.EndOfStorage());
    DestroyPersonTexts(personTexts_);
    heroes_.vtable = DwordArrayVtable();
    heroes_.FreeData();
    party_.vtable = PartyArrayVtable();
    party_.FreeData();
}
EI_REWRITTEN_SPECIAL(0x005F6130, CBattleScreen::~CBattleScreen);
EI_REWRITTEN_SPECIAL(0x005F5FC0, CBattleScreen::~CBattleScreen);
EI_REWRITTEN_SPECIAL(0x005FC0E0, CBattleScreen::~CBattleScreen);

static_assert(sizeof(BriefingMover) == 0x30);

CBriefingScreen::~CBriefingScreen() {
    for (BriefingSpeaker* speaker = speakers_; speaker != speakersEnd_; ++speaker) std::destroy_at(&speaker->name);
    FreeVectorStorage(speakers_, speakersCapacity_);
    people_.FreeStorage();
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
    DestroyPersonTexts(personTexts_);
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
EI_REWRITTEN_SPECIAL(0x005F6BA0, CBriefingScreen::~CBriefingScreen);
EI_REWRITTEN_SPECIAL(0x005F6B80, CBriefingScreen::~CBriefingScreen);
EI_REWRITTEN_SPECIAL(0x005FC0B0, CBriefingScreen::~CBriefingScreen);

CMpConnectScreen::~CMpConnectScreen() {
    for (MpAddressEntry& entry : entries_) {
        std::destroy_at(&entry.description);
        std::destroy_at(&entry.name);
    }
    entries_.FreeStorage();
}
EI_REWRITTEN_SPECIAL(0x00618570, CMpConnectScreen::~CMpConnectScreen);
EI_REWRITTEN_SPECIAL(0x00618550, CMpConnectScreen::~CMpConnectScreen);
EI_REWRITTEN_SPECIAL(0x0061B220, CMpConnectScreen::~CMpConnectScreen);

CHelpWindow::~CHelpWindow() {
    DestroyList(pictures_, [](CUiMesh**) {});
    DestroyList(pulsing_, [](CUiMesh**) {});
}
EI_REWRITTEN_SPECIAL(0x005F8970, CHelpWindow::~CHelpWindow);
EI_REWRITTEN_SPECIAL(0x005F8950, CHelpWindow::~CHelpWindow);
EI_REWRITTEN_SPECIAL(0x005FC0D0, CHelpWindow::~CHelpWindow);

CObjectivesWindow::~CObjectivesWindow() {
    FreeVectorStorage(questPlaces_.begin(), questPlaces_.EndOfStorage());
    exits_.vtable = ObjectiveExitsVtable();
    if (exits_.data) gamecrt::operator_delete(exits_.data);
    for (mfc::CString* zone = zones_.begin(); zone != zones_.end(); ++zone) std::destroy_at(zone);
    FreeVectorStorage(zones_.begin(), zones_.EndOfStorage());
}
EI_REWRITTEN_SPECIAL(0x00603B90, CObjectivesWindow::~CObjectivesWindow);
EI_REWRITTEN_SPECIAL(0x00603B70, CObjectivesWindow::~CObjectivesWindow);
EI_REWRITTEN_SPECIAL(0x0060C7C0, CObjectivesWindow::~CObjectivesWindow);

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
EI_REWRITTEN_SPECIAL(0x006070C0, CSaveLoadWindow::~CSaveLoadWindow);
EI_REWRITTEN_SPECIAL(0x006070A0, CSaveLoadWindow::~CSaveLoadWindow);
EI_REWRITTEN_SPECIAL(0x0060C7B0, CSaveLoadWindow::~CSaveLoadWindow);

EI_REWRITTEN_SPECIAL(0x005F5F50, CUiContainer::~CUiContainer);
EI_REWRITTEN_SPECIAL(0x005F5F80, CUiContainer::~CUiContainer);
EI_REWRITTEN_SPECIAL(0x00603CF0, CUiContainer::~CUiContainer);
EI_REWRITTEN_SPECIAL(0x005EE040, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x005EE410, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x0060E280, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x0060E300, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x0060E330, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x0060E5D0, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x0060E670, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x0060E6A0, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x006157F0, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x00635DA0, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x00635DD0, CUiWindow::~CUiWindow);
// --- Deleting destructors ----------------------------------------------------------------------

// The plain window and the windows whose body is the window's.
EI_REWRITTEN_SPECIAL(0x005FE910, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x005EE020, CUiWindow::~CUiWindow);   // the item screens' icon window
EI_REWRITTEN_SPECIAL(0x0060E240, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x0060E260, CUiWindow::~CUiWindow);   // the item strip
EI_REWRITTEN_SPECIAL(0x0060E2E0, CUiWindow::~CUiWindow);   // the hero equipment
EI_REWRITTEN_SPECIAL(0x0060E310, CUiWindow::~CUiWindow);   // the quick item strip
EI_REWRITTEN_SPECIAL(0x0060E5B0, CUiWindow::~CUiWindow);   // the constructors' first pages
EI_REWRITTEN_SPECIAL(0x0060E650, CUiWindow::~CUiWindow);   // the item maker
EI_REWRITTEN_SPECIAL(0x0060E680, CUiWindow::~CUiWindow);   // the repair grid
EI_REWRITTEN_SPECIAL(0x006157D0, CUiWindow::~CUiWindow);   // the figure strips
EI_REWRITTEN_SPECIAL(0x00635D80, CUiWindow::~CUiWindow);   // the game clock
EI_REWRITTEN_SPECIAL(0x00635DB0, CUiWindow::~CUiWindow);   // the movement panel

// The plain container and the containers whose body is the container's.
EI_REWRITTEN_SPECIAL(0x00600B80, CUiContainer::~CUiContainer);
EI_REWRITTEN_SPECIAL(0x005F5F30, CUiContainer::~CUiContainer);   // screen 7
EI_REWRITTEN_SPECIAL(0x005F5F60, CUiContainer::~CUiContainer);   // the main menu
EI_REWRITTEN_SPECIAL(0x00603CD0, CUiContainer::~CUiContainer);   // the escape menu

// --- The input sides' deleting destructors (a jump to the window's) ---------------------------

EI_REWRITTEN_SPECIAL(0x00600AC0, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x005F45F0, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x00613D50, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x00613D60, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x00613D70, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x00613D80, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x00613D90, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x00613DA0, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x00613DE0, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x0061B210, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x00601E70, CUiContainer::~CUiContainer);
EI_REWRITTEN_SPECIAL(0x005FC140, CUiContainer::~CUiContainer);
EI_REWRITTEN_SPECIAL(0x005FC120, CUiContainer::~CUiContainer);
EI_REWRITTEN_SPECIAL(0x0060C7E0, CUiContainer::~CUiContainer);
EI_REWRITTEN_SPECIAL(0x0064BD60, CUiWindow::~CUiWindow);   // the game clock
EI_REWRITTEN_SPECIAL(0x0064BD70, CUiWindow::~CUiWindow);   // the movement panel
