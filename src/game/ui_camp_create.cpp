// The camp's Create (CCampPages). Rebirth's WideScreen replaces the function with the game's version followed
// by two curtain sprites over the screen's margins (widescreen::AddCurtains), which this reproduces; nothing
// in our code calls it but the screen's vtable. See ui_camp.h and docs/notes/ui.md, section 3.
#include "app.h"
#include "unit_create.h"
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
#include "ui_widescreen.h"
#include "unit.h"
#include "world.h"

#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "world_glue.h"
#include "render/render_state.h"
#include "game_singletons.h"
#include "party_manager.h"

#include <d3d.h>

#include <cstddef>
#include <new>

namespace {

constexpr int kSurfaceFormat = 5;
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

const TemplateTable* ClassTemplates() { return &g_partyManager->templates; }
CParty* LocalParty() { return LocalPlayerData(); }
CUiContainer* AsContainer(CCampPages* camp) { return camp; }

// Whether the local player's variable `name` is 1. FCOMP also takes an
// unordered result (a NaN variable) as equal.
bool PlayerSwitch(const char* name) { return LocalPlayerVariable(name) == 1.0f /*0073B84C*/; }

CUiSurface* NewSurface(CInterface3D* ui, int left, int top, int right, int bottom) {
    return new CUiSurface(ui, left, top, right, bottom, kSurfaceFormat, 1);
}

// An item list of the constructors: made through its factory (not checked).
CParty* NewItemList() {
    CParty* list = new CParty();
    list->Create();
    return list;
}

// A panel's Build.
void BuildPanel(CUiWindow* panel) {
    panel->Build();
}

// The time in animation frames: milliseconds / 1024 * 15.
double FramesNow() {
    return x87::FromUnsigned(gameapi::timeGetTime()) * 0.0009765625f /*0073BB24*/ * 15.0f /*0073C518*/;
}

} // namespace

void CCampPages::CreateScreen() {
    BuildPages();
    widescreen::AddCurtains(this, 1);
}

void CCampPages::BuildPages() {
    spellConstructor_ = false;
    itemConstructor_ = false;
    spellParts_[0] = nullptr;
    itemParts_[0] = nullptr;
    GameSetServerPaused(1);
    constructor_ = nullptr;
    int constructor = static_cast<int>(libcrt::Ftol(LocalPlayerVariable("constr.current")));
    if (constructor > 0) {
        constructor_ = ClassTemplates()->Find(constructor);
        if (!constructor_) {
            GameRejoin();
            return;
        }
        spellConstructor_ = constructor_->sellsSpells != 0;
        itemConstructor_ = constructor_->sellsItems != 0;
        std::memcpy(priceWeights_, constructor_->weights, kPriceWeights * sizeof(float));   // the dwords as they are
        spellParts_[0] = constructor_->SpellsParty();
        itemParts_[0] = constructor_->ItemsParty();
    }
    noConstructor_ = PlayerSwitch("i.noconstr");
    if (PlayerSwitch("i.offspellconstr")) spellConstructor_ = false;
    if (PlayerSwitch("i.onspellconstr")) spellConstructor_ = true;
    if (PlayerSwitch("i.offitemconstr")) itemConstructor_ = false;
    if (PlayerSwitch("i.onitemconstr")) itemConstructor_ = true;

    LoadTexture("campinfo", 3, nullptr);
    // The regions: the two action buttons, the two hero turn areas, the eight page buttons (a
    // page the camp does not offer has no tip).
    auto region = [this](int left, int top, int right, int bottom, int tip) {
        CUiRegion* added = AddRegion();
        added->SetRect(left, top, right, bottom);
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

    CreateBase();
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
    CItemInfoPanel* info = new CItemInfoPanel(interface_, this);
    descPanel_ = info;
    AsContainer(this)->AddChild(info);
    descPanel_->Build(600, 100);

    // The hero's statistics.
    auto* stats = new CHeroStatsPanel(interface_, this);
    statsPanel_ = stats;
    AsContainer(this)->AddChild(stats);
    statsPanel_->AddObject(objects_[0]);
    BuildPanel(statsPanel_);

    // The help panel on the left.
    info = new CItemInfoPanel(interface_, this);
    helpPanel_ = info;
    AsContainer(this)->AddChild(info);
    helpPanel_->Build(0, 100);

    // The party's items.
    CUiItemStrip* strip = new CForwardingStrip(interface_, this);
    itemStrip_ = strip;
    AsContainer(this)->AddChild(strip);
    itemStrip_->Setup(&LocalPlayerData()->items_, 0);
    itemStrip_->mode_ = kActionEquip;
    itemStrip_->SetFilter(kFilterItems);

    // The hero's equipment (with the body) and quick items.
    auto* equipment = new CHeroEquipPanel(interface_, this);
    equipment_ = equipment;
    AsContainer(this)->AddChild(equipment);
    equipment_->AddObject(objects_[2]);
    equipment_->Build(true);

    auto* quick = new CHeroQuickStrip(interface_, this);
    equipment2_ = quick;
    AsContainer(this)->AddChild(quick);
    equipment2_->AddObject(objects_[2]);
    equipment2_->Build(true);

    // The skills for experience.
    auto* trade = new CTradePanel(interface_, this);
    trade_ = trade;
    AsContainer(this)->AddChild(trade);
    trade_->AddObject(objects_[0]);
    BuildPanel(trade_);

    if (spellConstructor_) {
        strip = new CForwardingStrip(interface_, this);
        spellStrip_ = strip;
        AsContainer(this)->AddChild(strip);
        spellStrip_->Setup(&spellParts_[0]->items_, 1);
        spellStrip_->mode_ = kActionSpellParts;
        spellStrip_->SetFilter(kFilterSpells);
        spellStrip_->SetSlots(0);

        auto* grid = new CTradeGrid(interface_, this);
        spellConstructor1_ = grid;
        AsContainer(this)->AddChild(grid);
        spellConstructor1_->Build(&spellParts_[1]->items_, &spellParts_[2]->items_, 1, 0);
        spellConstructor1_->mode_ = kModeSpellTrade;

        auto* maker = new CSpellMakerPage(interface_, this);
        spellConstructor2_ = maker;
        AsContainer(this)->AddChild(maker);
        spellConstructor2_->BuildSpellMaker(spellResults_);
    }

    if (itemConstructor_) {
        strip = new CForwardingStrip(interface_, this);
        partStrip_ = strip;
        AsContainer(this)->AddChild(strip);
        partStrip_->Setup(&itemParts_[0]->items_, 1);
        partStrip_->mode_ = kActionItemParts;
        partStrip_->SetFilter(kFilterArmour);
        partStrip_->SetSlots(4);

        auto* grid = new CTradeGrid(interface_, this);
        itemConstructor1_ = grid;
        AsContainer(this)->AddChild(grid);
        itemConstructor1_->Build(&itemParts_[1]->items_, &itemParts_[2]->items_, 0, 0);
        itemConstructor1_->mode_ = kModeItemTrade;

        auto* maker = new CConstructorPage(interface_, this);
        itemConstructor2_ = maker;
        AsContainer(this)->AddChild(maker);
        itemConstructor2_->BuildItemMaker(itemResults_);

        auto* repairs = new CRepairGrid(interface_, this);
        itemConstructor3_ = repairs;
        AsContainer(this)->AddChild(repairs);
        itemConstructor3_->Build(&itemExtra_->items_);
    }

    // The party's heroes become units of the client world: every record's id is raised while the
    // world takes the hero records, and put back after the units were set up.
    heroTurn_ = 0;
    turning_ = 0;
    std::vector<PartyUnitRecord*> records = {};
    CParty* player = LocalParty();
    const int count = std::ssize(player->members_);
    for (int i = 0; i < count; ++i) {
        player->members_[i].unitId += kRecordBias;
        if (PrototypeObjectClass(player->members_[i].texts[5]) != kHeroClass) continue;
        PartyUnitRecord* record = &player->members_[i];
        int size = std::ssize(records);
        records.resize(size + 1);
        records[size] = record;
    }
    GameCreatePartyUnits(&records, 1);

    heroes_.resize(count);
    for (int i = 0; i < count; ++i) heroes_[i] = nullptr;
    for (int i = 0; i < count; ++i) {
        if (PrototypeObjectClass(player->members_[i].texts[5]) != kHeroClass) continue;
        auto* unit = GameFindClientUnit(player->members_[i].unitId);
        heroes_[i] = unit;
        // The idle animation from a random frame (up to 50 back).
        if (FigureAnimation* animation = unit->FindSkillAnimation(kIdleAnimation)) {
            std::uint32_t random = g_random.Next();
            double frames = FramesNow();
            int start = static_cast<int>(libcrt::Ftol(frames - static_cast<int>(random % 51)));
            unit->SetAnimationEntries(-1, static_cast<std::int8_t>(animation->index), start, 0);
        }
        unit->Controller()->ShowWeapon(0xFFFFFFFF);
        unit->Controller()->RefreshModel();
        CUnitController* controller = unit->Controller();
        controller->RestartAnimation(static_cast<float>(FramesNow()));
        PartyUnitRecord* record = &player->members_[i];
        unit->SetFromStats(&record->stats);
        unit->Controller()->RefreshWounds();
        // The armour the record wears.
        auto* heroStats = &record->stats;
        heroStats->ClearOuterLayers();
        for (int piece = 0; piece < std::ssize(record->armor); ++piece)
            heroStats->AddArmourLayer(AsArmor(record->armor[piece]));
    }
    for (int i = 0; i < count; ++i) player->members_[i].unitId -= kRecordBias;

    hero_ = -1;
    NextHero();

    // The pages' titles.
    CTextManager* texts = GameClientTexts();
    std::string title;
    const char* const titleKeys[] = {
        "mode_weapons",
        "mode_spells",
        "mode_spelltrade",
        "mode_spellconstr",
        "mode_itemtrade",
        "mode_itemconstr",
        "mode_itemrepair",
    };
    for (const char* key : titleKeys) {
        texts->GetString(key, &title);
        pageTitles_.push_back(title);
    }
    pageTitles_.push_back("quit");

    page_ = itemConstructor_ ? kPageItemConstructor : spellConstructor_ ? kPageSpellConstructor : kPageEquipment;
    ShowPage(page_);
    UpdateAction();

    // A constructor's stock on the first visit: from the party's lowest and highest skill in each
    // spell school.
    if (constructor_ && constructor_->restockDue) {
        float lowest[kSchools];
        float highest[kSchools];
        for (int school = 0; school < kSchools; ++school) {
            lowest[school] = 1e20f;
            highest[school] = -1e20f;
            for (int i = 0; i < std::ssize(LocalParty()->members_); ++i) {
                auto* memberStats = &LocalParty()->members_[i].stats;
                float skill = static_cast<float>(memberStats->SpellSchoolSkill(school));
                if (skill < lowest[school]) lowest[school] = skill;
                if (highest[school] < skill) highest[school] = skill;
            }
        }
        constructor_->SetLevels(lowest, highest);
        ClassTemplateGenerateMagic(constructor_);
        ClassTemplateGenerateItems(constructor_);
        constructor_->restockDue = 0;
    }

    // The music of the allod the party's zone is on.
    const MapRecord* zone = g_mapDatabase->FindMap(LocalPlayerZone());
    TheMusicSystem()->PlayConstructor(zone ? zone->allod.c_str() : "gipat");

    // Antialiasing while the camp shows, when the device does it without sorting (Close turns it off).
    CTextureDevice* display = TheRenderDevice();
    D3DDEVICEDESC7 description = {};
    long result = device::GetCaps(display->d3dDevice_, &description);
    display->lastLoadResult_ = result;
    if (result != 0) app::FatalErrorCode(result, "Can't get device description.\n");   // "Can't get device description."
    if (description.dpcTriCaps.dwRasterCaps & D3DPRASTERCAPS_ANTIALIASSORTINDEPENDENT) {
        device::SetRenderState(TheRenderDevice()->d3dDevice_, D3DRENDERSTATE_ANTIALIAS, D3DANTIALIAS_SORTINDEPENDENT);
        render::state::Render(D3DRENDERSTATE_ANTIALIAS, D3DANTIALIAS_SORTINDEPENDENT);
    }
}
