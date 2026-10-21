// Small members of the interface classes in 005E0000-00640000 (A-I22): member constructors and
// assignments, default slot answers, one-line setters and handlers. See ui.h, ui_camp.h,
// ui_screens.h and docs/notes/ui.md.
#include "console.h"
#include "game_strings.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "ui_screens.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "originals_ui.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <new>

namespace {
void* Vtable(std::uintptr_t address) { return ei::GamePointer<void>(address); }

// Slot `slot` of `object`'s vtable, as a function of the object alone returning EAX.
using ObjectSlot = int(__thiscall*)(void*);
ObjectSlot SlotOf(void* object, int slot) { return reinterpret_cast<ObjectSlot>((*static_cast<void***>(object))[slot]); }
constexpr int kSlotBlit = 0;
constexpr int kSlotRelease = 4;
} // namespace

// --- 005F6080 / 005F5F90: the battle screen's members constructed by CreateScreen 1 ---------------------

CBattleScreen::PartyIds* CBattleScreen::PartyIds::Construct() {
    for (IdVector* ids : {&withUnit, &without}) {
        ids->Start() = nullptr;
        ids->Finish() = nullptr;
        ids->EndOfStorage() = nullptr;
    }
    return this;
}
EI_REWRITTEN(0x005F6080, CBattleScreen::PartyIds::Construct);

LockKey* LockKey::ConstructItemUseKey() {
    mask = 1;
    id = 0;
    level = 0;
    return this;
}
EI_REWRITTEN(0x005F5F90, LockKey::ConstructItemUseKey);

// --- 00632490 / 006324E0: a global map place assigned ---------------------------------------------------

GlobalMapPlace* GlobalMapPlace::Assign(const GlobalMapPlace& from) {
    zone = from.zone;
    position = from.position;
    name = from.name;
    title = from.title;
    return this;
}
EI_REWRITTEN(0x00632490, GlobalMapPlace::Assign);

GlobalMapPlace* GlobalMapPlace::AssignCopy(const GlobalMapPlace& from) { return Assign(from); }
EI_REWRITTEN(0x006324E0, GlobalMapPlace::AssignCopy);

// --- Default answers of the windows' slots ---------------------------------------------------------------

int UiInputSide::WheelIgnored(int, int, int, int) { return 0; }
EI_REPLACE_METHOD(0x005E28B0, UiInputSide::WheelIgnored);

void* CUiWindow::NoItemAt(int, int, int* topic) {
    *topic = 0;
    return nullptr;
}
EI_REPLACE_METHOD(0x005EE400, CUiWindow::NoItemAt);

bool CItemScreen::CanEquipDefault(CItem*, int) { return false; }
EI_REPLACE_METHOD(0x005F6380, CItemScreen::CanEquipDefault);

void CItemScreen::BestPartySkillDefault(int, int* skill, int* mana) {
    *skill = 0;
    *mana = 0;
}
EI_REPLACE_METHOD(0x005F63A0, CItemScreen::BestPartySkillDefault);

void* CItemScreen::HeroUnitDefault(int) { return nullptr; }
EI_REPLACE_METHOD(0x005F6400, CItemScreen::HeroUnitDefault);

float CItemScreen::WeightDefault() { return 0.0f /*0073B858*/; }
EI_REPLACE_METHOD(0x005F6430, CItemScreen::WeightDefault);

int CItemScreen::PriceDefault(CItem*, int) { return 0; }
EI_REPLACE_METHOD(0x005F6440, CItemScreen::PriceDefault);

void* CUiItemStrip::FigureItemAt(int, int, int*) { return nullptr; }
EI_REPLACE_METHOD(0x006157C0, CUiItemStrip::FigureItemAt);

bool CGlobalMap::MayTravel(int) { return true; }
EI_REWRITTEN(0x006316A0, CGlobalMap::MayTravel);

const char* CGlobalMap::HelpTopic() { return noCamp_ == 1 ? "global_map_nocamp" : "global_map"; }
EI_REPLACE_METHOD(0x0062EE70, CGlobalMap::HelpTopic);

// --- One-line setters and readers ------------------------------------------------------------------------

void CBattleScreen::SetPortraitMode(int mode) { portraitMode_ = mode; }
EI_REPLACE_METHOD(0x005F5FB0, CBattleScreen::SetPortraitMode);

void CBriefingScreen::SetChatActive(int mode) { chatActive_ = mode; }
EI_REPLACE_METHOD(0x005F6B70, CBriefingScreen::SetChatActive);

CUnit* CCampPages::HeroAt(int index) { return heroes_[index]; }
EI_REPLACE_METHOD(0x005F65F0, CCampPages::HeroAt);

void CUiItemStrip::SetSlots(int slots) { slots_ = slots; }
EI_REWRITTEN(0x00621F20, CUiItemStrip::SetSlots);

std::uint8_t CTradePanel::Bought() const { return records_.begin[recordIndex_].bought; }
EI_REWRITTEN(0x00625510, CTradePanel::Bought);

CItem* CItemInfoPanel::FirstItem() const { return itemCount_ ? items_[0] : nullptr; }
EI_REWRITTEN(0x005EE720, CItemInfoPanel::FirstItem);

void CUiMesh::SetScaleYBits(std::uint32_t bits) { std::memcpy(&scaleMatrix_.m[5], &bits, sizeof(bits)); }
EI_REWRITTEN(0x005E7090, CUiMesh::SetScaleYBits);

float CUiMesh::GetScaleX() const { return scaleMatrix_.m[0]; }
EI_REWRITTEN(0x005E70B0, CUiMesh::GetScaleX);

// --- Member constructors and destructors -----------------------------------------------------------------

CScreenFade* CScreenFade::Construct() {
    alpha_ = 0.0f;
    active_ = 0;
    letterbox_ = 0;
    return this;
}
EI_REWRITTEN(0x005E6990, CScreenFade::Construct);

CInterface3D* CInterface3D::Construct() {
    stackBegin_ = nullptr;
    stackEnd_ = nullptr;
    stackCapacity_ = nullptr;
    messages_.Construct();
    helpTopics_.Construct();
    new (&messageText_) mfc::CString();
    new (&savedText_) mfc::CString();
    return this;
}
EI_REPLACE_METHOD(0x005F46A0, CInterface3D::Construct);

void CUiMesh::DestroySprite() {
    material_ = nullptr;
    texture_ = nullptr;
    ClearMesh();
}
EI_REPLACE_METHOD(0x005E6D70, CUiMesh::DestroySprite);

// The original writes the counted root's vtable first and its own only for a non-null object;
// the first store is not seen by anything.
void ItemTurn::ResetVtable() { vtable = Vtable(0x007403F8); }
EI_REPLACE_METHOD(0x005ED760, ItemTurn::ResetVtable);

// --- Handlers ----------------------------------------------------------------------------------------------

int console::ConsoleWindow::Blit() {
    if (!surface_) return 0;
    return SlotOf(surface_, kSlotBlit)(surface_);
}
EI_REWRITTEN(0x005E3FC0, console::ConsoleWindow::Blit);

void CInterface3D::ChatKeyEnter() {
    if (!root_->chatOpen_) root_->SetOpen(true);
}
EI_REWRITTEN(0x005F7C60, CInterface3D::ChatKeyEnter);

void CInterface3D::ChatKeyBackspace() {
    if (!root_->chatOpen_) root_->ClearLines();
}
EI_REWRITTEN(0x005F7C80, CInterface3D::ChatKeyBackspace);

void CCampPages::OnKeyUp(int key, int repeat, int flags) { InputOf(this)->OnKeyUp(key, repeat, flags); }
EI_REPLACE_METHOD(0x0060F690, InputForward<&CCampPages::OnKeyUp>::Call);

int CHeroStatsPanel::OnLButtonUp(int, int, int) {
    interface_->mouseCapture_ = nullptr;
    heldRegion_ = -1;
    holdTime_ = 0.0f;
    return 0;
}
EI_REPLACE_METHOD(0x00624A40, InputForward<&CHeroStatsPanel::OnLButtonUp>::Call);

int CGlobalMap::OnLButtonUp(int, int, int) {
    if (turning_ != 0) selectPending_ = 1;
    turning_ = 0;
    return 0;
}
EI_REPLACE_METHOD(0x0062FEF0, InputForward<&CGlobalMap::OnLButtonUp>::Call);

void CEscMenu::OnClose() { window.models_.data[0]->Play(0); }
EI_REPLACE_METHOD(0x0062ACB0, CEscMenu::OnClose);

void CMessageBox::OnClose() {
    ClearSceneObjects();
    ClearSurfaces();
}
EI_REPLACE_METHOD(0x00634D00, CMessageBox::OnClose);
