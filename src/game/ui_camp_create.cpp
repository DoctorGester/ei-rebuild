// The camp's Create as the unpatched game does it (0060C870, CCampPages slot 25). Rebirth patches
// the function for its wide layout (docs/rebirth-patched-functions.txt), so this body is hooked in
// the vanilla launch only; nothing in the rewritten code calls it but the screen's vtable. See
// ui_camp.h and docs/notes/ui.md, section 3.
#include "device.h"   // the old device's calls
#include "ui_camp.h"

#include "class_templates.h"
#include "item_db_text.h"
#include "map_object_figures.h"
#include "object_tables.h"
#include "party.h"
#include "random.h"
#include "sound.h"
#include "texture.h"
#include "ui_briefing.h"
#include "ui_item_info.h"
#include "ui_screens.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_world.h"
#include "render/render_state.h"

#include <d3d.h>

#include <cstddef>
#include <cstdint>
#include <new>

namespace {

const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
void** Vtable(std::uintptr_t address) { return reinterpret_cast<void**>(address); }
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

VtablePair ItemPanelVtables() { return {Vtable(0x00741B58), Vtable(0x00741B24)}; }          // CUiItemPanel
VtablePair InfoPanelVtables() { return {Vtable(0x007404DC), Vtable(0x007404A8)}; }          // CItemInfoPanel
VtablePair StatsPanelVtables() { return {Vtable(0x00741ABC), Vtable(0x00741A88)}; }         // CHeroStatsPanel
VtablePair PlainStripVtables() { return {Vtable(0x00741A1C), Vtable(0x007419E8)}; }
VtablePair ForwardingStripVtables() { return {Vtable(0x0074197C), Vtable(0x00741948)}; }
VtablePair EquipPanelVtables() { return {Vtable(0x007418E0), Vtable(0x007418AC)}; }         // CHeroEquipPanel
VtablePair QuickStripVtables() { return {Vtable(0x00741844), Vtable(0x00741810)}; }         // CHeroQuickStrip
VtablePair TradePanelVtables() { return {Vtable(0x007417A8), Vtable(0x00741774)}; }         // CTradePanel
VtablePair TradeGridVtables() { return {Vtable(0x0074170C), Vtable(0x007416D8)}; }          // CTradeGrid
VtablePair SpellMakerVtables() { return {Vtable(0x00741670), Vtable(0x0074163C)}; }         // CSpellMakerPage
VtablePair ItemMakerVtables() { return {Vtable(0x007415D4), Vtable(0x007415A0)}; }          // CConstructorPage
VtablePair RepairGridVtables() { return {Vtable(0x00741538), Vtable(0x00741504)}; }         // CRepairGrid
std::uintptr_t ItemArrayVtable() { return std::uintptr_t(0x00741BC0); }      // the strips' CArray<char*>
void* RecordArrayVtable() { return reinterpret_cast<void*>(0x0073FAA4); }    // CArray<record*>

// The objects' sizes where the class declares less than the original allocates.
constexpr std::size_t kTradePanelSize = 0x1B8;
constexpr std::size_t kSpellMakerSize = 0x16C;

constexpr int kSurfaceFormat = 5;
constexpr int kSlotBuild = 18;          // the statistics and trade panels' Build
constexpr int kSlotNextHero = 48;
constexpr int kHeroClass = 0x32;        // the prototype class of a party record that becomes a unit
constexpr std::uint32_t kRecordBias = 100000000;   // added to the records' ids while the client world takes them
constexpr int kIdleAnimation = 0x21;    // the skill animation the heroes play in the camp
constexpr int kSchools = 8;
constexpr int kPriceWeights = 9;

// The pages (CCampPages::page_) and the strips' action modes.
constexpr int kPageEquipment = 0;
constexpr int kPageSpellConstructor = 2;
constexpr int kPageItemConstructor = 4;
constexpr int kActionEquip = 1, kActionSpellParts = 2, kActionItemParts = 3;
constexpr int kModeSpellTrade = 4, kModeItemTrade = 5;
constexpr int kFilterItems = 0, kFilterSpells = 1, kFilterArmour = 2;

const ClassTemplateVector* ClassTemplates() { return reinterpret_cast<const ClassTemplateVector*>(0x007AA5D8); }
void* ObjectManagerNow() { return *reinterpret_cast<void* const*>(0x007AF5F8); }
CMusicSystem* Music() { return *reinterpret_cast<CMusicSystem* const*>(0x007AF760); }
CParty* LocalParty() { return reinterpret_cast<CParty*>(LocalPlayerData()); }
CParty* AsParty(void* list) { return static_cast<CParty*>(list); }
CUiContainer* AsContainer(CCampPages* camp) { return reinterpret_cast<CUiContainer*>(camp); }

// Whether the local player's variable `name` is 1 (0073B84C). The original's FCOMP also takes an
// unordered result (a NaN variable) as equal.
bool PlayerSwitch(const char* name) { return LocalPlayerVariable(name) == GameFloat(0x0073B84C); }

void SetVtables(CUiWindow* window, VtablePair vtables) {
    window->vtable_ = vtables.main;
    window->inputVtable_ = vtables.input;
}

// A panel of `size` bytes built on the window base, owned by `owner`, with the vtable pair; null
// when the allocation fails.
CUiItemPanel* NewPanel(std::size_t size, CInterface3D* ui, CUiWindow* owner, VtablePair vtables) {
    void* memory = gamecrt::operator_new(size);
    if (!memory) return nullptr;
    auto* panel = static_cast<CUiItemPanel*>(memory);
    panel->Construct(ui);
    panel->owner_ = owner;
    SetVtables(panel, vtables);
    return panel;
}

// A description panel (its item list and text arrays constructed in place).
CItemInfoPanel* NewInfoPanel(CInterface3D* ui, CUiWindow* owner) {
    auto* panel = static_cast<CItemInfoPanel*>(NewPanel(sizeof(CItemInfoPanel), ui, owner, ItemPanelVtables()));
    if (!panel) return nullptr;
    PartyRecordsConstructOriginal(panel->itemList_);
    panel->itemTexts_.Construct();
    panel->skillTexts_.Construct();
    SetVtables(panel, InfoPanelVtables());
    return panel;
}

// An item strip that hands its clicks to `owner` (the blank panel, the plain strip, its list,
// then the forwarding strip's vtables).
CUiItemStrip* NewStrip(CInterface3D* ui, CUiWindow* owner) {
    void* memory = gamecrt::operator_new(sizeof(CUiItemStrip));
    if (!memory) return nullptr;
    auto* strip = static_cast<CUiItemStrip*>(memory);
    static_cast<CUiItemPanel*>(memory)->ConstructBlank(ui, owner);
    SetVtables(&strip->window, PlainStripVtables());
    strip->list_.Construct(ItemArrayVtable());
    SetVtables(&strip->window, ForwardingStripVtables());
    return strip;
}

CUiSurface* NewSurface(CInterface3D* ui, int left, int top, int right, int bottom) {
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    return memory ? static_cast<CUiSurface*>(memory)->Construct(ui, left, top, right, bottom, kSurfaceFormat, 1) : nullptr;
}

// An item list of the constructors: made through its slot 2 (not checked, as in the original).
CParty* NewItemList() {
    void* memory = gamecrt::operator_new(sizeof(CParty));
    CParty* list = memory ? static_cast<CParty*>(PartyRecordsConstructOriginal(memory)) : nullptr;
    list->Create();
    return list;
}

// Slot 18 of a panel: its Build.
void BuildPanel(CUiWindow* panel) {
    using BuildFn = void(__thiscall*)(CUiWindow*);
    reinterpret_cast<BuildFn>(panel->vtable_[kSlotBuild])(panel);
}

UiPtrArray<char>* ItemsOf(void* list) { return reinterpret_cast<UiPtrArray<char>*>(&AsParty(list)->items_); }

// The time in animation frames: milliseconds / 1024 (0073BB24) * 15 (0073C518).
double FramesNow() {
    return x87::FromUnsigned(gameapi::timeGetTime()) * GameFloat(0x0073BB24) * GameFloat(0x0073C518);
}

} // namespace

void CCampPages::Create() {
    spellConstructor_ = false;
    itemConstructor_ = false;
    spellParts_[0] = nullptr;
    itemParts_[0] = nullptr;
    SetServerFlagOriginal(1);
    constructor_ = nullptr;
    int constructor = static_cast<int>(gamecrt::Ftol(LocalPlayerVariable(GameText(0x00790384))));   // "constr.current"
    if (constructor > 0) {
        constructor_ = ClassTemplatesFind(ClassTemplates(), nullptr, constructor);
        if (!constructor_) {
            GlueBackToWorldOriginal(UiClientGlue());
            return;
        }
        spellConstructor_ = constructor_->flag0D != 0;
        itemConstructor_ = constructor_->flag0C != 0;
        for (int i = 0; i < kPriceWeights; ++i) priceWeights_[i] = constructor_->weights[i];
        spellParts_[0] = reinterpret_cast<CParty*>(constructor_->spells);
        itemParts_[0] = reinterpret_cast<CParty*>(constructor_->items);
    }
    noConstructor_ = PlayerSwitch(GameText(0x007936AC));                      // "i.noconstr"
    if (PlayerSwitch(GameText(0x00793698))) spellConstructor_ = false;        // "i.offspellconstr"
    if (PlayerSwitch(GameText(0x00793688))) spellConstructor_ = true;         // "i.onspellconstr"
    if (PlayerSwitch(GameText(0x00793678))) itemConstructor_ = false;         // "i.offitemconstr"
    if (PlayerSwitch(GameText(0x00793668))) itemConstructor_ = true;          // "i.onitemconstr"

    LoadTexture(GameText(0x0079365C), 3, nullptr);   // "campinfo"
    // The regions: the two action buttons, the two hero turn areas, the eight page buttons (a
    // page the camp does not offer has no tip).
    auto region = [this](int left, int top, int right, int bottom, int tip) {
        CUiRegion* added = AddRegion();
        added->CallSetRect(left, top, right, bottom);
        if (tip) added->SetTip(tip, 1, 0);
    };
    region(12, 450, 52, 490, 0x4EE8);
    region(148, 450, 188, 490, 0x4EE9);
    region(300, 470, 330, 500, 0);
    region(470, 470, 500, 500, 0);
    region(620, 430, 660, 460, 0x4E84);
    region(620, 460, 660, 490, 0x4E85);
    region(660, 460, 700, 490, spellConstructor_ ? 0x4E89 : 0);
    region(700, 460, 740, 490, spellConstructor_ && !noConstructor_ ? 0x4E8A : 0);
    region(660, 430, 700, 460, itemConstructor_ ? 0x4E86 : 0);
    region(700, 430, 740, 460, itemConstructor_ && !noConstructor_ ? 0x4E87 : 0);
    region(740, 430, 780, 460, itemConstructor_ ? 0x4E88 : 0);
    region(740, 460, 780, 490, 0x4E8B);

    // The buttons' sprites, in the regions' order without the turn areas.
    auto button = [this](float left, float top, float right, float bottom, int u0, int v0, int u1, int v1) {
        CUiMesh* sprite = AddSprite(nullptr);
        sprite->SetRectangle(3.0f, 100.0f, left, top, right, bottom, u0, v0, u1, v1);
        return sprite;
    };
    button(12.0f, 450.0f, 52.0f, 490.0f, 0xA5, 0xAC, 0xCD, 0xD4);
    button(148.0f, 450.0f, 188.0f, 490.0f, 0xA5, 0xD6, 0xCD, 0xFE);
    button(620.0f, 430.0f, 660.0f, 460.0f, 0x02, 0xC1, 0x2A, 0xDF);
    button(620.0f, 460.0f, 660.0f, 490.0f, 0x02, 0xE0, 0x2A, 0xFE);
    CUiMesh* sprite = button(660.0f, 460.0f, 700.0f, 490.0f, 0x2B, 0xE0, 0x53, 0xFE);
    sprite->visible_ = spellConstructor_;
    sprite = button(700.0f, 460.0f, 740.0f, 490.0f, 0x54, 0xE0, 0x7C, 0xFE);
    sprite->visible_ = spellConstructor_;
    if (noConstructor_) sprite->visible_ = 0;
    sprite = button(660.0f, 430.0f, 700.0f, 460.0f, 0x2B, 0xC1, 0x53, 0xDF);
    sprite->visible_ = itemConstructor_;
    sprite = button(700.0f, 430.0f, 740.0f, 460.0f, 0x54, 0xC1, 0x7C, 0xDF);
    sprite->visible_ = itemConstructor_;
    if (noConstructor_) sprite->visible_ = 0;
    sprite = button(740.0f, 430.0f, 780.0f, 460.0f, 0x7D, 0xC1, 0xA5, 0xDF);
    sprite->visible_ = itemConstructor_;
    sprite = button(740.0f, 460.0f, 780.0f, 490.0f, 0x7D, 0xE0, 0xA5, 0xFE);
    sprite->tint_[0] = 0.5f;
    sprite->tint_[1] = 0.5f;
    sprite->tint_[2] = 0.5f;

    reinterpret_cast<CItemScreen*>(this)->CreateBase();
    AddSurface(NewSurface(interface_, 0, 400, 200, 500));
    AddSurface(NewSurface(interface_, 600, 400, 800, 430));

    // The constructors' lists (the first of each is the class template's).
    spellParts_[1] = NewItemList();
    spellParts_[2] = NewItemList();
    spellResults_ = NewItemList();
    itemParts_[1] = NewItemList();
    itemParts_[2] = NewItemList();
    itemResults_ = NewItemList();
    itemExtra_ = NewItemList();
    itemMade_ = nullptr;
    spellMade_ = nullptr;

    // The description panel on the right.
    CItemInfoPanel* info = NewInfoPanel(interface_, this);
    descPanel_ = info;
    AsContainer(this)->AddChild(info);
    static_cast<CItemInfoPanel*>(descPanel_)->Build(600, 100);

    // The hero's statistics.
    auto* stats = reinterpret_cast<CHeroStatsPanel*>(NewPanel(sizeof(CHeroStatsPanel), interface_, this, ItemPanelVtables()));
    if (stats) {
        new (&stats->title_) mfc::CString;
        stats->texts_.Construct();
        SetVtables(stats, StatsPanelVtables());
    }
    statsPanel_ = stats;
    AsContainer(this)->AddChild(stats);
    statsPanel_->AddObject(objects_.data[0]);
    BuildPanel(statsPanel_);

    // The help panel on the left.
    info = NewInfoPanel(interface_, this);
    helpPanel_ = info;
    AsContainer(this)->AddChild(info);
    static_cast<CItemInfoPanel*>(helpPanel_)->Build(0, 100);

    // The party's items.
    CUiItemStrip* strip = NewStrip(interface_, this);
    itemStrip_ = strip;
    AsContainer(this)->AddChild(strip ? &strip->window : nullptr);
    itemStrip_->Setup(ItemsOf(LocalPlayerData()), 0);
    itemStrip_->actionMode_ = kActionEquip;
    itemStrip_->SetFilter(kFilterItems);

    // The hero's equipment (with the body) and quick items.
    auto* equipment = static_cast<CHeroEquipPanel*>(NewPanel(sizeof(CHeroEquipPanel), interface_, this, EquipPanelVtables()));
    equipment_ = equipment;
    AsContainer(this)->AddChild(equipment);
    equipment_->AddObject(objects_.data[2]);
    static_cast<CHeroEquipPanel*>(equipment_)->Build(true);

    auto* quick = static_cast<CHeroQuickStrip*>(NewPanel(sizeof(CHeroQuickStrip), interface_, this, QuickStripVtables()));
    equipment2_ = quick;
    AsContainer(this)->AddChild(quick);
    equipment2_->AddObject(objects_.data[2]);
    static_cast<CHeroQuickStrip*>(equipment2_)->Build(true);

    // The skills for experience.
    auto* trade = reinterpret_cast<CTradePanel*>(NewPanel(kTradePanelSize, interface_, this, ItemPanelVtables()));
    if (trade) {
        trade->learnable_ = {};
        trade->perks_ = {};
        trade->schools_ = {};
        trade->science_ = {};
        trade->records_ = {};
        trade->texts_.Construct();
        SetVtables(trade, TradePanelVtables());
    }
    trade_ = trade;
    AsContainer(this)->AddChild(trade);
    trade_->AddObject(objects_.data[0]);
    BuildPanel(trade_);

    if (spellConstructor_) {
        strip = NewStrip(interface_, this);
        spellStrip_ = strip;
        AsContainer(this)->AddChild(strip ? &strip->window : nullptr);
        spellStrip_->Setup(ItemsOf(spellParts_[0]), 1);
        spellStrip_->actionMode_ = kActionSpellParts;
        spellStrip_->SetFilter(kFilterSpells);
        spellStrip_->SetSlots(0);

        auto* grid = static_cast<CTradeGrid*>(NewPanel(sizeof(CTradeGrid), interface_, this, TradeGridVtables()));
        spellConstructor1_ = grid;
        AsContainer(this)->AddChild(grid);
        static_cast<CTradeGrid*>(spellConstructor1_)->Build(&AsParty(spellParts_[1])->items_, &AsParty(spellParts_[2])->items_, 1, 0);
        static_cast<CTradeGrid*>(spellConstructor1_)->mode_ = kModeSpellTrade;

        auto* maker = static_cast<CSpellMakerPage*>(NewPanel(kSpellMakerSize, interface_, this, ItemPanelVtables()));
        if (maker) {
            maker->texts_.Construct();
            SetVtables(maker, SpellMakerVtables());
        }
        spellConstructor2_ = maker;
        AsContainer(this)->AddChild(maker);
        static_cast<CSpellMakerPage*>(spellConstructor2_)->BuildSpellMaker(spellResults_);
    }

    if (itemConstructor_) {
        strip = NewStrip(interface_, this);
        partStrip_ = strip;
        AsContainer(this)->AddChild(strip ? &strip->window : nullptr);
        partStrip_->Setup(ItemsOf(itemParts_[0]), 1);
        partStrip_->actionMode_ = kActionItemParts;
        partStrip_->SetFilter(kFilterArmour);
        partStrip_->SetSlots(4);

        auto* grid = static_cast<CTradeGrid*>(NewPanel(sizeof(CTradeGrid), interface_, this, TradeGridVtables()));
        itemConstructor1_ = grid;
        AsContainer(this)->AddChild(grid);
        static_cast<CTradeGrid*>(itemConstructor1_)->Build(&AsParty(itemParts_[1])->items_, &AsParty(itemParts_[2])->items_, 0, 0);
        static_cast<CTradeGrid*>(itemConstructor1_)->mode_ = kModeItemTrade;

        auto* maker = static_cast<CConstructorPage*>(NewPanel(sizeof(CConstructorPage), interface_, this, ItemMakerVtables()));
        itemConstructor2_ = maker;
        AsContainer(this)->AddChild(maker);
        static_cast<CConstructorPage*>(itemConstructor2_)->BuildItemMaker(itemResults_);

        auto* repairs = static_cast<CRepairGrid*>(NewPanel(sizeof(CRepairGrid), interface_, this, RepairGridVtables()));
        itemConstructor3_ = repairs;
        AsContainer(this)->AddChild(repairs);
        static_cast<CRepairGrid*>(itemConstructor3_)->Build(&AsParty(itemExtra_)->items_);
    }

    // The party's heroes become units of the client world: every record's id is raised while the
    // world takes the hero records, and put back after the units were set up.
    heroTurn_ = 0;
    turning_ = 0;
    RecordPtrArray records = {RecordArrayVtable(), nullptr, 0, 0, 0};
    CParty* player = LocalParty();
    void* objects = ObjectManagerNow();
    const int count = player->members_.size;
    for (int i = 0; i < count; ++i) {
        player->members_.data[i].unitId += kRecordBias;
        if (PrototypeClass(objects, nullptr, player->members_.data[i].texts[5]) != kHeroClass) continue;
        PartyUnitRecord* record = &player->members_.data[i];
        int size = records.size;
        RecordArraySetSizeOriginal(&records, nullptr, size + 1, -1);
        records.data[size] = reinterpret_cast<std::uint8_t*>(record);
    }
    GameTakeUnitRecordsFlagOriginal(UiClientGlue(), nullptr, &records, 1);

    heroes_.SetSize(count);
    for (int i = 0; i < count; ++i) heroes_[i] = nullptr;
    for (int i = 0; i < count; ++i) {
        if (PrototypeClass(objects, nullptr, player->members_.data[i].texts[5]) != kHeroClass) continue;
        auto* unit = static_cast<CUnit*>(GameUnitByIdOriginal(UiClientGlue(), nullptr, player->members_.data[i].unitId));
        heroes_[i] = unit;
        // The idle animation from a random frame (up to 50 back).
        if (FigureAnimation* animation = reinterpret_cast<CObject3D*>(unit)->FindSkillAnimation(kIdleAnimation)) {
            std::uint32_t random = IsaacRandom::Global().Next();
            double frames = FramesNow();
            int start = static_cast<int>(gamecrt::Ftol(frames - static_cast<int>(random % 51)));
            unit->SetAnimationEntries(-1, static_cast<std::int8_t>(animation->index), start, 0);
        }
        unit->Controller()->ShowWeapon(0xFFFFFFFF);
        unit->Controller()->RefreshModel();
        CUnitController* controller = unit->Controller();
        controller->RestartAnimation(static_cast<float>(FramesNow()));
        PartyUnitRecord* record = &player->members_.data[i];
        unit->SetFromStats(reinterpret_cast<UnitStats*>(record->stats));
        unit->Controller()->RefreshWounds();
        // The armour the record wears.
        auto* heroStats = reinterpret_cast<UnitStats*>(record->stats);
        heroStats->ClearOuterLayers();
        for (int piece = 0; piece < static_cast<int>(record->armorCount); ++piece)
            heroStats->AddArmourLayer(record->armor[piece]);
    }
    for (int i = 0; i < count; ++i) player->members_.data[i].unitId -= kRecordBias;

    hero_ = -1;
    using NextHeroFn = void(__thiscall*)(CCampPages*);
    reinterpret_cast<NextHeroFn>(vtable_[kSlotNextHero])(this);

    // The pages' titles.
    auto* texts = static_cast<CTextManager*>(GameFlowOriginal());
    mfc::CString title;
    auto& titles = pageTitles_;
    const char* const titleKeys[] = {
        GameText(0x0079364C),   // "mode_weapons"
        GameText(0x00793640),   // "mode_spells"
        GameText(0x00793630),   // "mode_spelltrade"
        GameText(0x0079361C),   // "mode_spellconstr"
        GameText(0x0079360C),   // "mode_itemtrade"
        GameText(0x007935FC),   // "mode_itemconstr"
        GameText(0x007935EC),   // "mode_itemrepair"
    };
    for (const char* key : titleKeys) {
        texts->GetString(key, &title);
        titles.SetAtGrow(titles.size, title);
    }
    titles.SetAtGrow(titles.size, GameText(0x0079239C));   // "quit"

    page_ = itemConstructor_ ? kPageItemConstructor : spellConstructor_ ? kPageSpellConstructor : kPageEquipment;
    ShowPage(page_);
    UpdateAction();

    // A constructor's stock on the first visit: from the party's lowest and highest skill in each
    // spell school.
    if (constructor_ && constructor_->flag38) {
        float lowest[kSchools];
        float highest[kSchools];
        for (int school = 0; school < kSchools; ++school) {
            lowest[school] = 1e20f;
            highest[school] = -1e20f;
            for (int i = 0; i < LocalParty()->members_.size; ++i) {
                auto* memberStats = reinterpret_cast<UnitStats*>(LocalParty()->members_.data[i].stats);
                float skill = static_cast<float>(memberStats->SpellSchoolSkill(school));
                if (skill < lowest[school]) lowest[school] = skill;
                if (highest[school] < skill) highest[school] = skill;
            }
        }
        ClassTemplateSetLevels(constructor_, nullptr, lowest, highest);
        ClassTemplateGenerateMagic(constructor_);
        ClassTemplateGenerateItems(constructor_);
        constructor_->flag38 = 0;
    }

    // The music of the allod the party's zone is on.
    const MapRecord* zone;
    {
        BriefingPlayerName name;
        zone = reinterpret_cast<const MapRecord*>(MapDatabaseFind(GameMapDatabase(), nullptr, name));
    }
    Music()->PlayConstructor(zone ? zone->allod : GameText(0x0079014C));   // "gipat"

    // Antialiasing while the camp shows, when the device does it without sorting (Close turns it off).
    CTextureDevice* display = RenderDeviceObject();
    D3DDEVICEDESC7 description = {};
    long result = device::GetCaps(static_cast<IDirect3DDevice7*>(display->d3dDevice_), &description);
    display->lastLoadResult_ = result;
    if (result != 0) FatalDirectX(result, GameText(0x0078EE20));   // "Can't get device description."
    if (description.dpcTriCaps.dwRasterCaps & D3DPRASTERCAPS_ANTIALIASSORTINDEPENDENT) {
        device::SetRenderState(static_cast<IDirect3DDevice7*>(RenderDeviceObject()->d3dDevice_), D3DRENDERSTATE_ANTIALIAS, D3DANTIALIAS_SORTINDEPENDENT);
        render::state::Render(D3DRENDERSTATE_ANTIALIAS, D3DANTIALIAS_SORTINDEPENDENT);
    }
    if (records.data) gamecrt::operator_delete(records.data);
}
EI_REPLACE_METHOD(0x0060C870, CCampPages::Create);
