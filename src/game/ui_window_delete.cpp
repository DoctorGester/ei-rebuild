// Slot 0 of the interface's windows: the scalar deleting destructors (the destructor body, then the
// memory freed when bit 0 of the flags is set; they return the object), the input sides' copies
// of them (input vtable slot 0: `this` moved back from the input side at +0x94 to the window,
// then a jump to the window's), and the short destructor bodies only they call. Bodies that only
// jump to the window's or container's destructor are registered as those. docs/notes/ui.md,
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
// erased (005FAFA0) and the header reset, the count 0; then the header freed.
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

// 005EA850 (RET 4): the base input side's deleting destructor (slot 0 of 007401D0).
UiInputSide::~UiInputSide() {}
EI_REWRITTEN_SPECIAL(0x005EA850, UiInputSide::~UiInputSide);

// 00602BA0, deleting 00602B80: the inert extra side's (00741154), the base side's table back.
UiInertInput::~UiInertInput() {}
EI_REWRITTEN_SPECIAL(0x00602BA0, UiInertInput::~UiInertInput);
EI_REWRITTEN_SPECIAL(0x00602B80, UiInertInput::~UiInertInput);

CItemInfoPanel::~CItemInfoPanel() {
    skillTexts_.Destruct();
    itemTexts_.Destruct();
    ItemParty()->CParty::~CParty();
}
EI_REWRITTEN_SPECIAL(0x005EE380, CItemInfoPanel::~CItemInfoPanel);
EI_REWRITTEN_SPECIAL(0x005EE420, CItemInfoPanel::~CItemInfoPanel);   // deleting
EI_REWRITTEN_SPECIAL(0x005F4600, CItemInfoPanel::~CItemInfoPanel);   // the input side's

CChatWindow::~CChatWindow() {
    texts_.Destruct();
    for (CRefObject* player : players_)
        if (player) player->Release();
    players_.FreeStorage();
}
EI_REWRITTEN_SPECIAL(0x00608ED0, CChatWindow::~CChatWindow);
EI_REWRITTEN_SPECIAL(0x00608EB0, CChatWindow::~CChatWindow);   // deleting
EI_REWRITTEN_SPECIAL(0x0060C7D0, CChatWindow::~CChatWindow);   // the input side's

CForwardingStrip::~CForwardingStrip() {
    list_.vtable = ItemArrayVtable();
    list_.FreeData();
}
EI_REWRITTEN_SPECIAL(0x0060E2B0, CForwardingStrip::~CForwardingStrip);
EI_REWRITTEN_SPECIAL(0x0060E290, CForwardingStrip::~CForwardingStrip);   // deleting
EI_REWRITTEN_SPECIAL(0x00613DC0, CForwardingStrip::~CForwardingStrip);   // the input side's

CSpellMakerPage::~CSpellMakerPage() { texts_.Destruct(); }
EI_REWRITTEN_SPECIAL(0x0060E600, CSpellMakerPage::~CSpellMakerPage);
EI_REWRITTEN_SPECIAL(0x0060E5E0, CSpellMakerPage::~CSpellMakerPage);   // deleting
EI_REWRITTEN_SPECIAL(0x00613DF0, CSpellMakerPage::~CSpellMakerPage);   // the input side's

// The texts, then the title (a member).
CHeroStatsPanel::~CHeroStatsPanel() { texts_.Destruct(); }
EI_REWRITTEN_SPECIAL(0x0060E360, CHeroStatsPanel::~CHeroStatsPanel);
EI_REWRITTEN_SPECIAL(0x0060E340, CHeroStatsPanel::~CHeroStatsPanel);   // deleting
EI_REWRITTEN_SPECIAL(0x00613DD0, CHeroStatsPanel::~CHeroStatsPanel);   // the input side's

CMessageBox::~CMessageBox() = default;
EI_REWRITTEN_SPECIAL(0x0061E3E0, CMessageBox::~CMessageBox);
EI_REWRITTEN_SPECIAL(0x00633750, CMessageBox::~CMessageBox);   // the difficulty question's copy
EI_REWRITTEN_SPECIAL(0x00633730, CMessageBox::~CMessageBox);   // deleting
EI_REWRITTEN_SPECIAL(0x00634770, CMessageBox::~CMessageBox);   // deleting
EI_REWRITTEN_SPECIAL(0x006345E0, CMessageBox::~CMessageBox);   // the input side's
EI_REWRITTEN_SPECIAL(0x00634FD0, CMessageBox::~CMessageBox);   // the input side's

// The link (00440D40), then the box's.
CMasterRequestBox::~CMasterRequestBox() = default;
EI_REWRITTEN_SPECIAL(0x0061E460, CMasterRequestBox::~CMasterRequestBox);
EI_REWRITTEN_SPECIAL(0x0061E440, CMasterRequestBox::~CMasterRequestBox);   // deleting
EI_REWRITTEN_SPECIAL(0x00620300, CMasterRequestBox::~CMasterRequestBox);   // the input side's

void ChatLine::Destroy() {
    if (CUiSurface* shown = surface) {
        shown->ReleaseContents();
        gamecrt::operator_delete(shown);
    }
    surface = nullptr;
    name.~CString();
    text.~CString();
}
EI_REWRITTEN(0x005E2670, ChatLine::Destroy);

CChatOverlay::~CChatOverlay() { DestroyList(lines_, [](ChatLine* line) { line->Destroy(); }); }
EI_REWRITTEN_SPECIAL(0x005E28E0, CChatOverlay::~CChatOverlay);
EI_REWRITTEN_SPECIAL(0x005E28C0, CChatOverlay::~CChatOverlay);   // deleting
EI_REWRITTEN_SPECIAL(0x005E3C80, CChatOverlay::~CChatOverlay);   // the input side's

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
EI_REWRITTEN_SPECIAL(0x005F5FC0, CBattleScreen::~CBattleScreen);   // deleting
EI_REWRITTEN_SPECIAL(0x005FC0E0, CBattleScreen::~CBattleScreen);   // the input side's

static_assert(sizeof(BriefingMover) == 0x30);

CBriefingScreen::~CBriefingScreen() {
    for (BriefingSpeaker* speaker = speakers_; speaker != speakersEnd_; ++speaker) speaker->name.~CString();
    FreeVectorStorage(speakers_, speakersCapacity_);
    people_.FreeStorage();
    // The shown items' list (node 0x18 bytes: next, prev, then the item record).
    for (BriefingShownItem* node = shown_->next; node != shown_;) {
        BriefingShownItem* next = node->next;
        node->name.~CString();
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
EI_REWRITTEN_SPECIAL(0x005F6B80, CBriefingScreen::~CBriefingScreen);   // deleting
EI_REWRITTEN_SPECIAL(0x005FC0B0, CBriefingScreen::~CBriefingScreen);   // the input side's

CMpConnectScreen::~CMpConnectScreen() {
    texts_.Destruct();
    allodNames_.Destruct();
    for (MpAddressEntry& entry : entries_) {
        entry.description.~CString();
        entry.name.~CString();
    }
    entries_.FreeStorage();
}
EI_REWRITTEN_SPECIAL(0x00618570, CMpConnectScreen::~CMpConnectScreen);
EI_REWRITTEN_SPECIAL(0x00618550, CMpConnectScreen::~CMpConnectScreen);   // deleting
EI_REWRITTEN_SPECIAL(0x0061B220, CMpConnectScreen::~CMpConnectScreen);   // the input side's

CHelpWindow::~CHelpWindow() {
    DestroyList(pictures_, [](CUiMesh**) {});
    DestroyList(pulsing_, [](CUiMesh**) {});
    lines_.Destruct();
}
EI_REWRITTEN_SPECIAL(0x005F8970, CHelpWindow::~CHelpWindow);
EI_REWRITTEN_SPECIAL(0x005F8950, CHelpWindow::~CHelpWindow);   // deleting
EI_REWRITTEN_SPECIAL(0x005FC0D0, CHelpWindow::~CHelpWindow);   // the input side's

CObjectivesWindow::~CObjectivesWindow() {
    objectiveTexts_.Destruct();
    FreeVectorStorage(questPlaces_.begin(), questPlaces_.EndOfStorage());
    quests_.Destruct();
    exits_.vtable = ObjectiveExitsVtable();
    if (exits_.data) gamecrt::operator_delete(exits_.data);
    for (mfc::CString* zone = zones_.begin(); zone != zones_.end(); ++zone) zone->~CString();
    FreeVectorStorage(zones_.begin(), zones_.EndOfStorage());
}
EI_REWRITTEN_SPECIAL(0x00603B90, CObjectivesWindow::~CObjectivesWindow);
EI_REWRITTEN_SPECIAL(0x00603B70, CObjectivesWindow::~CObjectivesWindow);   // deleting
EI_REWRITTEN_SPECIAL(0x0060C7C0, CObjectivesWindow::~CObjectivesWindow);   // the input side's

CSaveLoadWindow::~CSaveLoadWindow() {
    for (SaveEntry* entry = entries_.begin(); entry != entries_.end(); ++entry) {
        entry->stringArrays_[1].Destruct();
        entry->stringArrays_[0].Destruct();
        entry->zone_.~CString();
        entry->map_.~CString();
        entry->folder_.~CString();
        entry->title_.~CString();
    }
    FreeVectorStorage(entries_.begin(), entries_.EndOfStorage());
}
EI_REWRITTEN_SPECIAL(0x006070C0, CSaveLoadWindow::~CSaveLoadWindow);
EI_REWRITTEN_SPECIAL(0x006070A0, CSaveLoadWindow::~CSaveLoadWindow);   // deleting
EI_REWRITTEN_SPECIAL(0x0060C7B0, CSaveLoadWindow::~CSaveLoadWindow);   // the input side's

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
// --- Deleting destructors (slot 0) --------------------------------------------------------------

// 005FE910 (the plain window, 00740F60) and the windows whose body is the window's.
EI_REWRITTEN_SPECIAL(0x005FE910, CUiWindow::~CUiWindow);
EI_REWRITTEN_SPECIAL(0x005EE020, CUiWindow::~CUiWindow);   // the item screens' icon window (00740444)
EI_REWRITTEN_SPECIAL(0x0060E240, CUiWindow::~CUiWindow);   // 00741B58
EI_REWRITTEN_SPECIAL(0x0060E260, CUiWindow::~CUiWindow);   // the item strip (00741A1C)
EI_REWRITTEN_SPECIAL(0x0060E2E0, CUiWindow::~CUiWindow);   // the hero equipment (007418E0)
EI_REWRITTEN_SPECIAL(0x0060E310, CUiWindow::~CUiWindow);   // the quick item strip (00741844)
EI_REWRITTEN_SPECIAL(0x0060E5B0, CUiWindow::~CUiWindow);   // the constructors' first pages (0074170C)
EI_REWRITTEN_SPECIAL(0x0060E650, CUiWindow::~CUiWindow);   // the item maker (007415D4)
EI_REWRITTEN_SPECIAL(0x0060E680, CUiWindow::~CUiWindow);   // the repair grid (00741538)
EI_REWRITTEN_SPECIAL(0x006157D0, CUiWindow::~CUiWindow);   // the figure strips (00741BF4)
EI_REWRITTEN_SPECIAL(0x00635D80, CUiWindow::~CUiWindow);   // the game clock (007425C8)
EI_REWRITTEN_SPECIAL(0x00635DB0, CUiWindow::~CUiWindow);   // the movement panel (00742530)

// 00600B80 (the plain container, 00741008) and the containers whose body is the container's.
EI_REWRITTEN_SPECIAL(0x00600B80, CUiContainer::~CUiContainer);
EI_REWRITTEN_SPECIAL(0x005F5F30, CUiContainer::~CUiContainer);   // screen 7 (00740840)
EI_REWRITTEN_SPECIAL(0x005F5F60, CUiContainer::~CUiContainer);   // the main menu (00740688)
EI_REWRITTEN_SPECIAL(0x00603CD0, CUiContainer::~CUiContainer);   // the escape menu (007412A4)


// 005F6450 / 005F6600 / 005F6FF0: the item screen (00740A2C), the camp (0074091C), screen 9
// (00740D18).

// --- The input sides' slot 0 (SUB ECX,0x94; JMP to the window's) -------------------------------

// Each is the window class's deleting destructor reached through its input side.
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
