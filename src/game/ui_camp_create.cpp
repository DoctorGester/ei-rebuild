// The camp's Create (CampPages). Rebirth's WideScreen replaces the function with the game's version followed
// by two curtain sprites over the screen's margins (widescreen::AddCurtains), which this reproduces; nothing
// in our code calls it but the screen's vtable. See ui_camp.h and docs/notes/ui.md, section 3.
#include "app.h"
#include "item_manager.h"
#include "unit_create.h"
#include "ui_camp.h"

#include "class_templates.h"
#include "text_manager.h"
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
#include "lib/containers.h"
#include "lib/crt.h"
#include "platform/clock.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "world_glue.h"
#include "render/render_state.h"
#include "game_singletons.h"
#include "party_manager.h"


#include <cstddef>
#include <new>

namespace {

constexpr std::uint32_t kRecordBias = 100000000;   // added to the records' ids while the client world takes them
constexpr SkillAnimation kIdleAnimation = SkillAnimation::Special14;    // what the heroes play in the camp
constexpr int kPriceWeights = 9;

// The pages (CampPages::page) and the strips' action modes.
constexpr int kPageEquipment = 0;
constexpr int kPageSpellConstructor = 2;
constexpr int kPageItemConstructor = 4;
constexpr int kActionEquip = 1, kActionSpellParts = 2, kActionItemParts = 3;
constexpr int kModeSpellTrade = 4, kModeItemTrade = 5;
constexpr int kFilterItems = 0, kFilterSpells = 1, kFilterArmour = 2;

// Whether the local player's variable `name` is 1. The original also takes an
// unordered result (a NaN variable) as equal.
bool PlayerSwitch(const char* name) { return LocalPlayerVariable(name) == 1.0f; }

UiSurface* NewSurface(Interface3D* ui, int left, int top, int right, int bottom) {
    return new UiSurface(ui, left, top, right, bottom, SurfaceBlend::Keyed, true);
}

// An item list of the constructors: made through its factory (not checked).
Party* NewItemList() {
    Party* list = new Party();
    list->Create();
    return list;
}

// The time in animation frames: milliseconds / 1024 * 15 (a double: the milliseconds since the system started).
double FramesNow() { return platform::Milliseconds() * (15.0 / 1024.0); }

} // namespace

void CampPages::CreateScreen() {
    BuildPages();
    widescreen::AddCurtains(this, 1);
}

void CampPages::BuildPages() {
    spellConstructor = false;
    itemConstructor = false;
    spellParts[0] = nullptr;
    itemParts[0] = nullptr;
    GameSetServerPaused(1);
    constructor = nullptr;
    int maker = static_cast<int>(LocalPlayerVariable("constr.current"));
    if (maker > 0) {
        constructor = g_partyManager.templates.Find(maker);
        if (!constructor) {
            GameRejoin();
            return;
        }
        spellConstructor = constructor->sellsSpells != 0;
        itemConstructor = constructor->sellsItems != 0;
        std::memcpy(priceWeights, constructor->weights, kPriceWeights * sizeof(float));     // the dwords as they are
        spellParts[0] = constructor->SpellsParty();
        itemParts[0] = constructor->ItemsParty();
    }
    noConstructor = PlayerSwitch("i.noconstr");
    if (PlayerSwitch("i.offspellconstr")) spellConstructor = false;
    if (PlayerSwitch("i.onspellconstr")) spellConstructor = true;
    if (PlayerSwitch("i.offitemconstr")) itemConstructor = false;
    if (PlayerSwitch("i.onitemconstr")) itemConstructor = true;

    LoadTexture("campinfo", TextureKind::Interface);
    // The regions: the two action buttons, the two hero turn areas, the eight page buttons (a
    // page the camp does not offer has no tip).
    auto region = [this](int left, int top, int right, int bottom, int tip) {
        UiRegion* added = AddRegion();
        added->SetRect(left, top, right, bottom);
        if (tip) added->SetTip(tip);
    };
    region(12, 450, 52, 490, 0x4EE8);
    region(148, 450, 188, 490, 0x4EE9);
    region(300, 470, 330, 500, 0);
    region(470, 470, 500, 500, 0);
    region(620, 430, 660, 460, 0x4E84);
    region(620, 460, 660, 490, 0x4E85);
    region(660, 460, 700, 490, spellConstructor ? 0x4E89 : 0);
    region(700, 460, 740, 490, spellConstructor && !noConstructor ? 0x4E8A : 0);
    region(660, 430, 700, 460, itemConstructor ? 0x4E86 : 0);
    region(700, 430, 740, 460, itemConstructor && !noConstructor ? 0x4E87 : 0);
    region(740, 430, 780, 460, itemConstructor ? 0x4E88 : 0);
    region(740, 460, 780, 490, 0x4E8B);

    // The buttons' sprites, in the regions' order without the turn areas.
    auto button = [this](float left, float top, float right, float bottom, int u0, int v0, int u1, int v1) {
        UiMesh* sprite = AddBlankSprite();
        sprite->SetRectangle(3.0f, 100.0f, left, top, right, bottom, u0, v0, u1, v1);
        return sprite;
    };
    button(12.0f, 450.0f, 52.0f, 490.0f, 0xA5, 0xAC, 0xCD, 0xD4);
    button(148.0f, 450.0f, 188.0f, 490.0f, 0xA5, 0xD6, 0xCD, 0xFE);
    button(620.0f, 430.0f, 660.0f, 460.0f, 0x02, 0xC1, 0x2A, 0xDF);
    button(620.0f, 460.0f, 660.0f, 490.0f, 0x02, 0xE0, 0x2A, 0xFE);
    UiMesh* sprite = button(660.0f, 460.0f, 700.0f, 490.0f, 0x2B, 0xE0, 0x53, 0xFE);
    sprite->visible = spellConstructor;
    sprite = button(700.0f, 460.0f, 740.0f, 490.0f, 0x54, 0xE0, 0x7C, 0xFE);
    sprite->visible = spellConstructor;
    if (noConstructor) sprite->visible = false;
    sprite = button(660.0f, 430.0f, 700.0f, 460.0f, 0x2B, 0xC1, 0x53, 0xDF);
    sprite->visible = itemConstructor;
    sprite = button(700.0f, 430.0f, 740.0f, 460.0f, 0x54, 0xC1, 0x7C, 0xDF);
    sprite->visible = itemConstructor;
    if (noConstructor) sprite->visible = false;
    sprite = button(740.0f, 430.0f, 780.0f, 460.0f, 0x7D, 0xC1, 0xA5, 0xDF);
    sprite->visible = itemConstructor;
    sprite = button(740.0f, 460.0f, 780.0f, 490.0f, 0x7D, 0xE0, 0xA5, 0xFE);
    sprite->tint[0] = 0.5f;
    sprite->tint[1] = 0.5f;
    sprite->tint[2] = 0.5f;

    CreateBase();
    AddSurface(NewSurface(ui, 0, 400, 200, 500));
    AddSurface(NewSurface(ui, 600, 400, 800, 430));

    // The constructors' lists (the first of each is the class template's).
    spellParts[1] = NewItemList();
    spellParts[2] = NewItemList();
    spellResults = NewItemList();
    itemParts[1] = NewItemList();
    itemParts[2] = NewItemList();
    itemResults = NewItemList();
    itemExtra = NewItemList();

    // The description panel on the right.
    ItemInfoPanel* info = new ItemInfoPanel(ui, this);
    descPanel = info;
    AddChild(info);
    descPanel->Build(600, 100);

    // The hero's statistics.
    auto* stats = new HeroStatsPanel(ui, this);
    statsPanel = stats;
    AddChild(stats);
    statsPanel->AddObject(objects[0]);
    statsPanel->Build();

    // The help panel on the left.
    info = new ItemInfoPanel(ui, this);
    helpPanel = info;
    AddChild(info);
    helpPanel->Build(0, 100);

    // The party's items.
    UiItemStrip* strip = new ForwardingStrip(ui, this);
    itemStrip = strip;
    AddChild(strip);
    itemStrip->Setup(&LocalPlayerData()->items, false);
    itemStrip->mode = kActionEquip;
    itemStrip->SetFilter(kFilterItems);

    // The hero's equipment (with the body) and quick items.
    auto* equipPanel = new HeroEquipPanel(ui, this);
    equipment = equipPanel;
    AddChild(equipPanel);
    equipment->AddObject(objects[2]);
    equipment->Build(true);

    auto* quick = new HeroQuickStrip(ui, this);
    equipment2 = quick;
    AddChild(quick);
    equipment2->AddObject(objects[2]);
    equipment2->Build(true);

    // The skills for experience.
    auto* tradeWith = new TradePanel(ui, this);
    trade = tradeWith;
    AddChild(tradeWith);
    trade->AddObject(objects[0]);
    trade->Build();

    if (spellConstructor) {
        strip = new ForwardingStrip(ui, this);
        spellStrip = strip;
        AddChild(strip);
        spellStrip->Setup(&spellParts[0]->items, true);
        spellStrip->mode = kActionSpellParts;
        spellStrip->SetFilter(kFilterSpells);
        spellStrip->slots = 0;

        auto* grid = new TradeGrid(ui, this);
        spellConstructor1 = grid;
        AddChild(grid);
        spellConstructor1->Build(&spellParts[1]->items, &spellParts[2]->items, true, false);
        spellConstructor1->mode = kModeSpellTrade;

        auto* makerPage = new SpellMakerPage(ui, this);
        spellConstructor2 = makerPage;
        AddChild(makerPage);
        spellConstructor2->BuildSpellMaker(spellResults);
    }

    if (itemConstructor) {
        strip = new ForwardingStrip(ui, this);
        partStrip = strip;
        AddChild(strip);
        partStrip->Setup(&itemParts[0]->items, true);
        partStrip->mode = kActionItemParts;
        partStrip->SetFilter(kFilterArmour);
        partStrip->slots = 4;

        auto* grid = new TradeGrid(ui, this);
        itemConstructor1 = grid;
        AddChild(grid);
        itemConstructor1->Build(&itemParts[1]->items, &itemParts[2]->items, false, false);
        itemConstructor1->mode = kModeItemTrade;

        auto* makerPage = new ConstructorPage(ui, this);
        itemConstructor2 = makerPage;
        AddChild(makerPage);
        itemConstructor2->BuildItemMaker(itemResults);

        auto* repairs = new RepairGrid(ui, this);
        itemConstructor3 = repairs;
        AddChild(repairs);
        itemConstructor3->Build(&itemExtra->items);
    }

    // The party's heroes become units of the client world: every record's id is raised while the
    // world takes the hero records, and put back after the units were set up.
    heroTurn = 0.0f;
    turning = 0;
    std::vector<PartyUnitRecord*> records;
    Party* player = LocalPlayerData();
    const int count = lib::Count(player->members);
    for (PartyUnitRecord& record : player->members) {
        record.unitId += kRecordBias;
        if (PrototypeObjectClass(record.texts[5]) == MapObjectType::Hero) records.push_back(&record);
    }
    GameCreatePartyUnits(&records, 1);

    heroes.assign(count, nullptr);
    for (int i = 0; i < count; ++i) {
        if (PrototypeObjectClass(player->members[i].texts[5]) != MapObjectType::Hero) continue;
        auto* unit = GameFindClientUnit(player->members[i].unitId);
        heroes[i] = unit;
        // The idle animation from a random frame (up to 50 back).
        if (FigureAnimation* animation = unit->FindSkillAnimation(kIdleAnimation)) {
            std::uint32_t random = g_clientRandom.Next();
            double frames = FramesNow();
            int start = static_cast<int>(frames - static_cast<int>(random % 51));
            unit->SetAnimationEntries(-1, static_cast<std::int8_t>(animation->index), start, 0);
        }
        unit->Controller()->ShowWeapon(kNoWeaponSlot);
        unit->Controller()->RefreshModel();
        UnitController* controller = unit->Controller();
        controller->RestartAnimation(static_cast<float>(FramesNow()));
        PartyUnitRecord* record = &player->members[i];
        unit->SetFromStats(&record->stats);
        unit->Controller()->RefreshWounds();
        // The armour the record wears.
        auto* heroStats = &record->stats;
        heroStats->ClearOuterLayers();
        for (int piece = 0; piece < std::ssize(record->armor); ++piece)
            heroStats->AddArmourLayer(AsArmor(record->armor[piece]));
    }
    for (int i = 0; i < count; ++i) player->members[i].unitId -= kRecordBias;

    hero = -1;
    NextHero();

    // The pages' titles.
    TextManager* texts = g_clientWorld.texts;
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
        title = texts->GetString(key);
        pageTitles.push_back(title);
    }
    pageTitles.push_back("quit");

    page = itemConstructor ? kPageItemConstructor : spellConstructor ? kPageSpellConstructor : kPageEquipment;
    ShowPage(page);
    UpdateAction();

    // A constructor's stock on the first visit: from the party's lowest and highest skill in each
    // spell school.
    if (constructor && constructor->restockDue) {
        float lowest[kSchoolCount];
        float highest[kSchoolCount];
        for (int school = 0; school < kSchoolCount; ++school) {
            lowest[school] = 1e20f;
            highest[school] = -1e20f;
            for (int i = 0; i < std::ssize(LocalPlayerData()->members); ++i) {
                auto* memberStats = &LocalPlayerData()->members[i].stats;
                float skill = static_cast<float>(memberStats->SpellSchoolSkill(school));
                if (skill < lowest[school]) lowest[school] = skill;
                if (highest[school] < skill) highest[school] = skill;
            }
        }
        constructor->SetLevels(lowest, highest);
        ClassTemplateGenerateMagic(constructor);
        ClassTemplateGenerateItems(constructor);
        constructor->restockDue = 0;
    }

    // The music of the allod the party's zone is on.
    const MapRecord* zone = g_mapDatabase.FindMap(LocalPlayerZone());
    g_clientWorld.music->PlayConstructor(zone ? std::string_view(zone->allod) : std::string_view("gipat"));

}
