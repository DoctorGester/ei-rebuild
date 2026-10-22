// The item screens' panels' compiled tables (C19; ui_window_class.h, ui_input_class.h) in place of game.exe's: the
// blank item panel 00741B58 / 00741B24, the description panel 007404DC / 007404A8, the statistics column 00741ABC /
// 00741A88, the equipment panel 007418E0 (with its tail 007418E8) / 007418AC, the quick strip 00741844 / 00741810,
// the trade panel 007417A8 / 00741774, the trade grid 0074170C / 007416D8, the spell maker 00741670 / 0074163C, the
// item maker 007415D4 / 007415A0 and the repair grid 00741538 / 00741504. The item strips' tables (00741A1C,
// 0074197C, 00741BF4 and their input sides) stay game.exe's for now. The camp, the hero screen and the exchange
// (ui_camp_create.cpp, ui_hero_screen_create.cpp, ui_exchange.cpp) store these through the panels' accessors.
#include "ui.h"
#include "ui_camp.h"
#include "ui_input_class.h"
#include "ui_item_info.h"
#include "ui_window_class.h"

namespace {

// An item panel's window slots: the window's, with 25 the item (or help topic) under a point; the panels without
// items of their own answer none (005EE400). `Panel` is the panel's class (AsPanel), Delete its slot 0.
template <typename Self, auto Delete, typename Panel>
class ItemPanelClass : public PanelClass<Self, Delete, Panel> {
public:
    virtual void* ItemAt(int x, int y, int* topic) { return this->AsWindow()->CUiWindow::NoItemAt(x, y, topic); }   // 25
};

// An item panel's input side: the left button down hands the item under the point to the owner (005EE310).
template <typename Self, auto Delete, typename Panel>
class ItemPanelInputClass : public InputSideClass<Self, Delete> {
public:
    Panel* AsPanel() { return static_cast<Panel*>(this->SideWindow()); }
    int LeftDown(int b, int x, int y) override { return AsPanel()->CUiItemPanel::OnLeftDown(b, x, y), 0; }   // 2
};

// The blank panel (CUiItemPanel::ConstructBlank, the item strips' base).
struct BlankPanelClass final : ItemPanelClass<BlankPanelClass, &CUiWindow::UiWindowDelete, CUiItemPanel> {};
struct BlankPanelInputClass final : ItemPanelInputClass<BlankPanelInputClass, &CUiWindow::UiWindowDelete, CUiItemPanel> {};

// The description panel: 20 the described item's model; input 1 redraws the description (005EE6B0), 9 / 11 the
// Ctrl key (005EE640 / 005EE680).
struct InfoPanelClass final : ItemPanelClass<InfoPanelClass, &CItemInfoPanel::ItemInfoPanelDelete, CItemInfoPanel> {
    BOOL Draw(int pass) override { return AsWindow()->CItemInfoPanel::Draw(pass); }   // 20: 005EE5C0
};
struct InfoPanelInputClass final : ItemPanelInputClass<InfoPanelInputClass, &CItemInfoPanel::ItemInfoPanelDelete, CItemInfoPanel> {
    void Deactivate() override { AsPanel()->DescribeItem(); }                                          // 1: 005EE6B0
    int KeyDown(int k, int r, int f) override { return AsPanel()->CItemInfoPanel::OnKeyDown(k, r, f), 0; }   // 9
    int KeyUp(int k, int r, int f) override { return AsPanel()->CItemInfoPanel::OnKeyUp(k, r, f), 0; }       // 11
};

// The statistics column: 18 Build, 24 the held button; input 2 / 3 its buttons.
struct StatsPanelClass final : ItemPanelClass<StatsPanelClass, &CHeroStatsPanel::StatsPanelDelete, CHeroStatsPanel> {
    void Build() override { AsWindow()->CHeroStatsPanel::Build(); }     // 18: 00622DB0
    void Update() override { AsWindow()->CHeroStatsPanel::Update(); }   // 24: 00624A60
};
struct StatsPanelInputClass final : InputSideClass<StatsPanelInputClass, &CHeroStatsPanel::StatsPanelDelete> {
    int LeftDown(int b, int x, int y) override { return SideWindow()->CHeroStatsPanel::OnLButtonDown(b, x, y), 0; }   // 2
    int LeftUp(int b, int x, int y) override { return SideWindow()->CHeroStatsPanel::OnLButtonUp(b, x, y); }          // 3
};

// The equipment panel: 20 its draw, 25 the item under a point; input 5 the right button (a weapon shown).
struct EquipPanelClass final : ItemPanelClass<EquipPanelClass, &CUiWindow::UiWindowDelete, CHeroEquipPanel> {
    BOOL Draw(int pass) override { return AsPanel()->DrawImpl(pass); }                                      // 20: 00622420
    void* ItemAt(int x, int y, int* topic) override { return AsPanel()->CHeroEquipPanel::ItemAt(x, y, topic); }   // 25: 00622860
};
struct EquipPanelInputClass final : ItemPanelInputClass<EquipPanelInputClass, &CUiWindow::UiWindowDelete, CHeroEquipPanel> {
    int RightDown(int b, int x, int y) override { return AsPanel()->CHeroEquipPanel::OnRightDown(b, x, y), 0; }   // 5: 00622960
};

// The quick strip: 20 its draw, 25 the entry under a point.
struct QuickStripClass final : ItemPanelClass<QuickStripClass, &CUiWindow::UiWindowDelete, CHeroQuickStrip> {
    BOOL Draw(int pass) override { return AsPanel()->CHeroQuickStrip::Draw(pass); }                              // 20: 00622C10
    void* ItemAt(int x, int y, int* topic) override { return AsPanel()->CHeroQuickStrip::ItemAt(x, y, topic); }   // 25: 00622D40
};
struct QuickStripInputClass final : ItemPanelInputClass<QuickStripInputClass, &CUiWindow::UiWindowDelete, CHeroQuickStrip> {};

// The trade panel: 18 Build, 24 the held school or science row; input 2 / 3 its rows.
struct TradePanelClass final : ItemPanelClass<TradePanelClass, &CTradePanel::TradePanelDelete, CTradePanel> {
    void Build() override { AsWindow()->CTradePanel::Build(); }        // 18: 00624F10
    void Update() override { AsWindow()->CTradePanel::UpdateHeld(); }  // 24: 00626C50
};
struct TradePanelInputClass final : InputSideClass<TradePanelInputClass, &CTradePanel::TradePanelDelete> {
    int LeftDown(int b, int x, int y) override { return SideWindow()->CTradePanel::OnLButtonDown(b, x, y), 0; }   // 2: 00626920
    int LeftUp(int b, int x, int y) override { return SideWindow()->CTradePanel::OnLButtonUp(b, x, y), 0; }       // 3: 00626C30
};

// The trade grid: 20 its cells, 25 the item under a point.
struct TradeGridClass final : ItemPanelClass<TradeGridClass, &CUiWindow::UiWindowDelete, CTradeGrid> {
    BOOL Draw(int pass) override { return AsPanel()->DrawGrid(pass); }                                       // 20: 00627390
    void* ItemAt(int x, int y, int* topic) override { return AsPanel()->CTradeGrid::ItemAt(x, y, topic); }   // 25: 00627920
};
struct TradeGridInputClass final : ItemPanelInputClass<TradeGridInputClass, &CUiWindow::UiWindowDelete, CTradeGrid> {};

// The spell maker: 20 its slots and cost lines, 25 the slot item under a point.
struct SpellMakerClass final : ItemPanelClass<SpellMakerClass, &CSpellMakerPage::SpellMakerDelete, CSpellMakerPage> {
    BOOL Draw(int pass) override { return AsWindow()->DrawSpellMaker(pass); }                         // 20: 006284A0
    void* ItemAt(int x, int y, int* topic) override { return AsWindow()->SpellMakerItemAt(x, y, topic); }   // 25: 00628B50
};
struct SpellMakerInputClass final : ItemPanelInputClass<SpellMakerInputClass, &CSpellMakerPage::SpellMakerDelete, CSpellMakerPage> {};

// The item maker: 20 its slots, 25 the slot item under a point.
struct ItemMakerClass final : ItemPanelClass<ItemMakerClass, &CUiWindow::UiWindowDelete, CConstructorPage> {
    BOOL Draw(int pass) override { return AsPanel()->DrawItemMaker(pass); }                                // 20: 00628F80
    void* ItemAt(int x, int y, int* topic) override { return AsPanel()->ItemMakerItemAt(x, y, topic); }   // 25: 006298B0
};
struct ItemMakerInputClass final : ItemPanelInputClass<ItemMakerInputClass, &CUiWindow::UiWindowDelete, CConstructorPage> {};

// The repair grid: 20 its cells and prices, 25 the item under a point.
struct RepairGridClass final : ItemPanelClass<RepairGridClass, &CUiWindow::UiWindowDelete, CRepairGrid> {
    BOOL Draw(int pass) override { return AsPanel()->CRepairGrid::Draw(pass); }                               // 20: 00627C90
    void* ItemAt(int x, int y, int* topic) override { return AsPanel()->CRepairGrid::ItemAt(x, y, topic); }   // 25: 00628060
};
struct RepairGridInputClass final : ItemPanelInputClass<RepairGridInputClass, &CUiWindow::UiWindowDelete, CRepairGrid> {};

template <typename Tag>
void** Table() { return static_cast<void**>(Tag::Vtable()); }

} // namespace

void** CUiItemPanel::BlankVtable() { return Table<BlankPanelClass>(); }
void** CUiItemPanel::BlankInputVtable() { return Table<BlankPanelInputClass>(); }
void** CItemInfoPanel::Vtable() { return Table<InfoPanelClass>(); }
void** CItemInfoPanel::InputVtable() { return Table<InfoPanelInputClass>(); }
void** CHeroStatsPanel::Vtable() { return Table<StatsPanelClass>(); }
void** CHeroStatsPanel::InputVtable() { return Table<StatsPanelInputClass>(); }
void** CHeroEquipPanel::Vtable() { return Table<EquipPanelClass>(); }
void** CHeroEquipPanel::InputVtable() { return Table<EquipPanelInputClass>(); }
void** CHeroQuickStrip::Vtable() { return Table<QuickStripClass>(); }
void** CHeroQuickStrip::InputVtable() { return Table<QuickStripInputClass>(); }
void** CTradePanel::Vtable() { return Table<TradePanelClass>(); }
void** CTradePanel::InputVtable() { return Table<TradePanelInputClass>(); }
void** CTradeGrid::Vtable() { return Table<TradeGridClass>(); }
void** CTradeGrid::InputVtable() { return Table<TradeGridInputClass>(); }
void** CSpellMakerPage::Vtable() { return Table<SpellMakerClass>(); }
void** CSpellMakerPage::InputVtable() { return Table<SpellMakerInputClass>(); }
void** CConstructorPage::ItemMakerVtable() { return Table<ItemMakerClass>(); }
void** CConstructorPage::ItemMakerInputVtable() { return Table<ItemMakerInputClass>(); }
void** CRepairGrid::Vtable() { return Table<RepairGridClass>(); }
void** CRepairGrid::InputVtable() { return Table<RepairGridInputClass>(); }
