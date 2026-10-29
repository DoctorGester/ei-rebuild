// The hero panels shared by the camp and the multiplayer hero screen: the statistics column.
// See docs/notes/ui.md ("Camp, global map, hero screens").
// Floating point follows the x87 code of the original game: `double` for values it keeps on the FPU
// stack, static_cast<float> where it stores to a float. Constants are read from memory.
#include "combat.h"
#include "console.h"
#include "game_singletons.h"
#include "item.h"
#include "item_manager.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/nan_audit.h"
#include "party.h"
#include "text_manager.h"
#include "ui.h"
#include "ui_camp.h"
#include "ui_float.h"
#include "unit.h"

#include <cstdint>
#include <cstring>
#include <format>
#include <initializer_list>
#include <string>

namespace {

void Down(RECT* rect) { gameapi::OffsetRect(rect, 0, 15); }

void AddRect(UiWindow* window, const RECT& rect) { window->AddRegion()->SetRectFrom(rect); }

void ShowFrom(UiWindow* window, int first, bool shown) {
    for (int i = first; i < std::ssize(window->sceneObjects); ++i) window->SceneMesh(i)->visible = shown;
}

constexpr std::uint32_t kBright = 0xA7D7E4;
constexpr std::uint32_t kGrey = 0x828282;

// The 4 x 4 cell grid over 200..600 x 100..500 shared by the trade and repair grids: a picture (textures 0-3 by quarter)
// and a frame per cell, a region per
// cell, then the surface. Texels alternate between 0x1C and 0x80 across and down; each row's
// pictures lie twice as deep as their frames, and half as deep as the row above.
void BuildCellGrid(UiWindow* window) {
    int u = 0x1C;
    float depth = 32.0f;
    int v = 0x1C;
    for (int top = 100; top < 500; top += 100) {
        float pictureDepth = static_cast<float>(D(depth) + D(depth));
        int bottom = top + 100;
        for (int left = 200; left < 600; left += 100) {
            CTexture** textures = window->objects.data();
            CTexture* texture = top <= 200 ? (left <= 300 ? textures[0] : textures[1])
                                                : (left <= 300 ? textures[2] : textures[3]);
            int right = left + 100;
            UiMesh* picture = window->AddBlankSprite();
            picture->SetRectangle(pictureDepth, 100.0f, static_cast<float>(left), static_cast<float>(top),
                                  static_cast<float>(right), static_cast<float>(bottom), u, v, u + 100, v + 100);
            picture->texture = texture;
            UiMesh* frame = window->AddBlankSprite();
            frame->Framed(depth, 100.0f, static_cast<float>(left), static_cast<float>(top),
                                   static_cast<float>(right), static_cast<float>(bottom), 15.0f, u, v, u + 100, v + 100);
            frame->texture = texture;
            AddRect(window, RECT{left, top, right, bottom});
            u = 0x9C - u;
        }
        depth = static_cast<float>(D(depth) * 0.5);   // * 0.5
        v = 0x9C - v;
    }
    window->AddSurface(new UiSurface(window->ui, 200, 100, 600, 500, SurfaceBlend::Keyed, true));
}

constexpr int kGridCellsPerList = 8;
constexpr int kGridCells = 16;
constexpr int kTopicExchangeBuy = 0x1C, kTopicExchangeSell = 0x1D;
constexpr int kTopicItemBuy = 0x0B, kTopicItemSell = 0x0C;       // the item constructor's kinds
constexpr int kTopicSpellBuy = 0x11, kTopicSpellSell = 0x12;     // the spell constructor's
constexpr int kTopicRepair = 0x19;

// The pages' common part: the 2 x 4 grid of regions over 400..600 x 100..500 (column by column),
// the four quarter pictures (textures 0-3) and a hidden highlight frame.
void BuildMakerPage(ConstructorPage* page) {
    for (int left = 400; left < 600; left += 100)
        for (int top = 100; top < 500; top += 100) AddRect(page, RECT{left, top, left + 100, top + 100});
}
void AddMakerPictures(ConstructorPage* page) {
    const float quarters[4][4] = {{200, 100, 400, 300}, {400, 100, 600, 300}, {200, 300, 400, 500}, {400, 300, 600, 500}};
    for (int i = 0; i < 4; ++i) {
        UiMesh* picture = page->AddBlankSprite();
        picture->SetRectangle(7.0f, 100.0f, quarters[i][0], quarters[i][1], quarters[i][2], quarters[i][3], 0x1C, 0x1C,
                              0xE4, 0xE4);
        picture->texture = page->objects[i];
    }
    UiMesh* frame = page->AddBlankSprite();
    frame->Framed(3.0f, 100.0f, -40.0f, -40.0f, 40.0f, 40.0f, 5.0f, 0, 0, 0x100, 0x100);
    frame->flatColour = 0x96FFFFFF;
    frame->flags |= FigureMeshFlag::Flat;
    frame->texture = nullptr;
    frame->visible = false;
}

} // namespace

// --- Statistics page ----------------------------------------------------------------------------

void HeroStatsPanel::ShowStats(int newPage) {
    RECT area = {0x14, 0xA5, 0xB4, 0xB4};
    page = newPage;
    while (std::ssize(regions) > 2) RemoveRegionAt(2);
    ShowFrom(this, 4, false);
    if (page == 0) {
        Down(&area);
        for (int i = 0; i < 4; ++i) {
            AddRect(this, area);
            Down(&area);
        }
        Down(&area);
        Down(&area);
        AddRect(this, area);
        Down(&area);
        AddRect(this, area);
        Down(&area);
        AddRect(this, area);
        Down(&area);
        Down(&area);
        AddRect(this, area);
        Down(&area);
        Down(&area);
        AddRect(this, area);
    } else if (page == 10) {
        ShowFrom(this, 4, true);
        const RECT columns[3] = {{0x14, 0xA5, 0x8C, 0xB4}, {0x8C, 0xA5, 0xA0, 0xB4}, {0xA0, 0xA5, 0xB4, 0xB4}};
        for (const RECT& column : columns) {
            area = column;
            for (int i = 0; i < 4; ++i) {
                AddRect(this, area);
                Down(&area);
                if (i == 2) {
                    Down(&area);
                    Down(&area);
                }
            }
        }
        AddRect(this, RECT{0x14, 0xD2, 0xB4, 0xE1});
    } else {
        for (int i = 0; i < 8; ++i) {
            AddRect(this, area);
            Down(&area);
            if (i == 2) Down(&area);
        }
    }
    DrawStats();
}

// --- Statistics drawing ---------------------------------------------------------------------------

void HeroStatsPanel::DrawStats() {
    UiSurface* draw = surfaces[0];
    draw->Clear();
    RECT titleArea = {0, 0x1B, 0xC8, 0x2B};
    draw->DrawTextIn(title, &titleArea, 1, 0xFFFFFF, 1, 1);

    auto hero = [this]() { return owner->HeroRecord(); };
    UnitStats* stats = &hero()->stats;
    CUnit* unit = owner->HeroUnit(heroIndex);
    std::string line;
    std::string text;
    TextManager* clientWorldTexts = g_clientWorld.texts;
    RECT area;

    if (page == 10) {
        // A new hero's statistics: points left = 75 - (strength + dexterity + intelligence).
        int strength = static_cast<int>(libcrt::Ftol(D(stats->strengthBase)));
        int dexterity = static_cast<int>(libcrt::Ftol(D(stats->dexterityBase)));
        int intelligence = static_cast<int>(libcrt::Ftol(D(stats->intelligenceBase)));
        int sum = intelligence + dexterity + strength;
        area = RECT{0x14, 0x41, 0x82, 0x50};
        DrawRow(area, "Strength", strength, -1);
        Down(&area);
        DrawRow(area, "Dexterity", dexterity, -1);
        Down(&area);
        DrawRow(area, "Intelligence", intelligence, -1);
        Down(&area);
        float sumF = static_cast<float>(static_cast<double>(sum));
        DrawRow(area, texts[5], static_cast<int>(libcrt::Ftol(D(75.0f) - D(sumF))), -1);
        Down(&area);
        Down(&area);
        DrawRow(area, texts[6], points, -1);

        // "-": while above 15 (the integer compared before it is stored as float).
        area = RECT{0x8C, 0x41, 0xA0, 0x50};
        double minimum = D(15.0f);
        int values[3] = {strength, dexterity, intelligence};
        float storedStats[3];
        for (int i = 0; i < 3; ++i) {
            double value = static_cast<double>(values[i]);
            storedStats[i] = static_cast<float>(value);
            std::uint32_t colour = value > minimum ? kBright : kGrey;
            draw->DrawTextIn("-", &area, 0, colour, 1, 1);
            Down(&area);
        }
        Down(&area);
        Down(&area);
        draw->DrawTextIn("-", &area, 0, points <= 0 ? kGrey : kBright, 1, 1);

        // "+": while below 35 and points are left.
        int left = static_cast<int>(libcrt::Ftol(D(75.0f) - D(sumF)));
        area = RECT{0xA0, 0x41, 0xB4, 0x50};
        double maximum = D(35.0f);
        for (int i = 0; i < 3; ++i) {
            EI_NAN_AUDIT(storedStats[i]);
            bool below = D(storedStats[i]) < maximum;
            std::uint32_t colour = below && left > 0 ? kBright : kGrey;
            draw->DrawTextIn("+", &area, 0, colour, 1, 1);
            Down(&area);
        }
        Down(&area);
        Down(&area);
        draw->DrawTextIn("+", &area, 0, points >= 100 ? kGrey : kBright, 1, 1);
    } else if (page == 0) {
        area = RECT{0x14, 0x41, 0xAF, 0x50};
        text = clientWorldTexts->GetString("infoitem_25");
        draw->DrawTextIn(text, &area, 0, kBright, 0, 1);
        Down(&area);
        DrawPart(area, "skull", 0);
        Down(&area);
        DrawPart(area, "arms", 2);
        Down(&area);
        DrawPart(area, "torso", 1);
        Down(&area);
        DrawPart(area, "legs", 4);
        Down(&area);
        Down(&area);
        if (hero()->weaponIndex == kNoWeaponSlot) {
            for (int i = 0; i < 5; ++i) Down(&area);
        } else {
            draw->DrawTextIn(texts[3], &area, 0, kBright, 0, 1);
            Down(&area);
            AttackParams info;
            unit->Controller()->GetAttackParams(&info);
            line = std::format("     {}", texts[0]);
            draw->DrawTextIn(line, &area, 0, kBright, 0, 1);
            line = std::format("{}", info.attack);
            draw->DrawTextIn(line, &area, 0, kBright, 2, 1);
            Down(&area);
            line = std::format("     {}", texts[1]);
            draw->DrawTextIn(line, &area, 0, kBright, 0, 1);
            line = std::format("{}", info.defence);
            draw->DrawTextIn(line, &area, 0, kBright, 2, 1);
            Down(&area);
            if (static_cast<std::uint32_t>(info.damageType) < 7) {
                line = std::format("damage_{}", info.damageType);
                text = clientWorldTexts->GetString(line);
            }
            line = std::format("     {} {}", texts[2], text);
            draw->DrawTextIn(line, &area, 0, kBright, 0, 1);
            int high = static_cast<int>(libcrt::Ftol(D(info.damageMax)));
            int low = static_cast<int>(libcrt::Ftol(D(info.damageMin)));
            line = std::format("{}-{}", low, high);
            draw->DrawTextIn(line, &area, 0, kBright, 2, 1);
            Down(&area);
            Down(&area);
        }
        int weight = static_cast<int>(libcrt::Ftol(hero()->CarriedWeight()));
        int capacity = static_cast<int>(libcrt::Ftol(D(stats->capacity)));
        DrawRow(area, "Encumbrance", weight, capacity);
        Down(&area);
        if (weight > capacity) draw->DrawTextIn(texts[4], &area, 0, 0xFF, 2, 1);
        Down(&area);
        draw->DrawTextIn(texts[7], &area, 0, kBright, 0, 1);
    } else {
        area = RECT{0x14, 0x41, 0xB4, 0x50};
        DrawRow(area, "Strength", static_cast<int>(libcrt::Ftol(D(stats->strength))), -1);
        Down(&area);
        DrawRow(area, "Dexterity", static_cast<int>(libcrt::Ftol(D(stats->dexterity))), -1);
        Down(&area);
        DrawRow(area, "Intelligence", static_cast<int>(libcrt::Ftol(D(stats->intelligence))), -1);
        Down(&area);
        Down(&area);
        DrawRow(area, "Health", static_cast<int>(libcrt::Ftol(stats->HitPointCurve())), -1);
        Down(&area);
        DrawRow(area, "Mana", static_cast<int>(libcrt::Ftol(stats->ManaCurve())), -1);
        Down(&area);
        DrawRow(area, "Actions", static_cast<int>(libcrt::Ftol(D(stats->rate))), -1);
        Down(&area);
        DrawRow(area, "Experience",
                static_cast<int>(libcrt::Ftol(D(stats->pendingExperience) + D(stats->experience))), -1);
        Down(&area);
        double carried;
        if (page == 11) {
            carried = owner->Weight();
        } else {
            carried = hero()->CarriedWeight();
        }
        int weight = static_cast<int>(libcrt::Ftol(carried));
        int capacity = static_cast<int>(libcrt::Ftol(D(stats->capacity)));
        DrawRow(area, "Encumbrance", weight, capacity);
        Down(&area);
        if (weight > capacity) draw->DrawTextIn(texts[4], &area, 0, 0xFF, 2, 1);
        Down(&area);
    }
}

// --- The equipment panel's Build ---------------------------------------------------------

void HeroEquipPanel::Build(bool withBody) {
    body = withBody;
    rect = RECT{0, 0, 800, withBody ? 500 : 100};
    mode = 0;
    for (int x = 0; x < 800; x += 100) AddRect(this, RECT{x, 0, x + 100, 100});
    for (int i = 0; i < 2; ++i) {
        UiMesh* frame = AddBlankSprite();
        frame->Framed(3.0f, 100.0f, -40.0f, -40.0f, 40.0f, 40.0f, 5.0f, 0, 0, 0x100, 0x100);
        frame->flags |= FigureMeshFlag::Flat;
        frame->flatColour = 0xFF8B8B37;
        frame->texture = nullptr;
        frame->visible = false;
    }
    // The slots' frames: four on the left half, four on the right, each half with its strip.
    float cell = 100.0f;
    for (int i = 0; i < 4; ++i)
        AddBlankSprite()->Framed(8.0f, 100.0f, static_cast<float>(static_cast<double>(i) * D(cell)), 0.0f,
                                      static_cast<float>(static_cast<double>(i + 1) * D(cell)), 100.0f, 15.0f, 0xE, 0xE, 0x72, 0x72);
    AddBlankSprite()->SetGrid(16.0f, 100.0f, 0.0f, 0.0f, 400.0f, 100.0f, 0xE, 0xE, 0x72, 0x72, 4, 1);
    for (int i = 4; i < 8; ++i)
        AddBlankSprite()->Framed(8.0f, 100.0f, static_cast<float>(static_cast<double>(i) * D(cell)), 0.0f,
                                      static_cast<float>(static_cast<double>(i + 1) * D(cell)), 100.0f, 15.0f, 0x8E, 0xE, 0xF2, 0x72);
    AddBlankSprite()->SetGrid(16.0f, 100.0f, 400.0f, 0.0f, 800.0f, 100.0f, 0x8E, 0xE, 0xF2, 0x72, 4, 1);
    if (body) {
        const int* xs = kBodySlotX;
        const int* ys = kBodySlotY;
        for (int i = 0; i < 7; ++i) AddRect(this, RECT{xs[i] - 50, ys[i] - 50, xs[i] + 50, ys[i] + 50});
        for (const char* name : {"camp1", "camp2", "camp3", "camp4"}) LoadTexture(name, TextureKind::Interface);
        const float quarters[4][4] = {{200, 100, 400, 300}, {400, 100, 600, 300}, {200, 300, 400, 500}, {400, 300, 600, 500}};
        for (int i = 0; i < 4; ++i) {
            UiMesh* picture = AddBlankSprite();
            picture->SetRectangle(7.0f, 100.0f, quarters[i][0], quarters[i][1], quarters[i][2], quarters[i][3], 0x1C,
                                  0x1C, 0xE4, 0xE4);
            picture->texture = objects[i + 1];
        }
    }
}

// --- The statistics panel's Build ---------------------------------------------

void HeroStatsPanel::Build() {
    rect = RECT{0, 100, 200, 400};
    AddRect(this, RECT{0, 0x7D, 0x1E, 0x91});      // previous hero
    AddRect(this, RECT{0xAA, 0x7D, 200, 0x91});    // next hero
    AddSurface(new UiSurface(ui, 0, 100, 200, 400, SurfaceBlend::Keyed, true));
    AddBlankSprite()->SetRectangle(5.0f, 100.0f, 176.0f, 125.0f, 192.0f, 145.0f, 2, 0x81, 0x12, 0x95);
    AddBlankSprite()->SetRectangle(5.0f, 100.0f, 8.0f, 125.0f, 24.0f, 145.0f, 0x12, 0x81, 2, 0x95);
    AddBlankSprite()->SetRectangle(6.0f, 100.0f, 14.0f, 110.0f, 100.0f, 160.0f, 0xE7, 0x4C, 0x91, 0x88);
    AddBlankSprite()->SetRectangle(6.0f, 100.0f, 100.0f, 110.0f, 186.0f, 160.0f, 0x91, 0x4C, 0xE7, 0x88);
    // The new hero page's -/+ buttons (scene objects 4-11, shown only there).
    const float buttons[8][4] = {{140, 165, 160, 180}, {140, 180, 160, 195}, {140, 195, 160, 210}, {140, 240, 160, 255},
                                 {160, 165, 180, 180}, {160, 180, 180, 195}, {160, 195, 180, 210}, {160, 240, 180, 255}};
    for (const auto& b : buttons) AddBlankSprite()->SetRectangle(5.0f, 100.0f, b[0], b[1], b[2], b[3], 0x90, 0xB1, 0xA4, 0xC0);
    TextManager* clientWorldTexts = g_clientWorld.texts;
    std::string text;
    for (const char* key : {"infounit_3", "infounit_4", "infounit_9", "infounit_14", "infounit_17", "lmp_points_left",
                            "lmp_pers_tall", "magic_skills"}) {
        text = clientWorldTexts->GetString(key);
        texts.push_back(text);
    }
}

// --- The trade grid's Build ----------------------------------------------------------------

void TradeGrid::Build(std::vector<Item*>* boughtItems, std::vector<Item*>* soldItems, bool spells, bool isExchange) {
    bought = boughtItems;
    rect = RECT{200, 100, 600, 500};
    sold = soldItems;
    buyKind = spells ? 0 : 4;
    sellKind = spells ? 1 : 5;
    exchange = isExchange;
    for (const char* name : {"trade1", "trade2", "trade3", "trade4"}) LoadTexture(name, TextureKind::Interface);
    BuildCellGrid(this);
}

// --- The item under the mouse ---------------------------------------------------------------

Item* TradeGrid::ItemAt(int x, int y, int* topic) {
    *topic = 0;
    if (!enabled) return nullptr;
    int region = RegionAt(x, y);
    if (region >= 0 && region < kGridCellsPerList) {
        *topic = exchange ? kTopicExchangeBuy : buyKind ? kTopicItemBuy : kTopicSpellBuy;
        if (region < std::ssize(*bought)) return (*bought)[region];
        return nullptr;
    }
    if (region < kGridCellsPerList || region >= kGridCells) return nullptr;
    // The sold list's topic also follows buyKind.
    *topic = exchange ? kTopicExchangeSell : buyKind ? kTopicItemSell : kTopicSpellSell;
    if (region - kGridCellsPerList < std::ssize(*sold)) return (*sold)[region - kGridCellsPerList];
    return nullptr;
}

Item* RepairGrid::ItemAt(int x, int y, int* topic) {
    *topic = 0;
    if (!enabled) return nullptr;
    int region = RegionAt(x, y);
    if (region < 0 || region >= kGridCells) return nullptr;
    *topic = kTopicRepair;
    return region < std::ssize(*items) ? (*items)[region] : nullptr;
}

// --- The repair grid's Build ---------------------------------------------------------------

void RepairGrid::Build(std::vector<Item*>* damaged) {
    rect = RECT{200, 100, 600, 500};
    mode = 8;
    items = damaged;
    for (const char* name : {"repair1", "repair2", "repair3", "repair4"}) LoadTexture(name, TextureKind::Interface);
    BuildCellGrid(this);
}

// --- The quick strip's Build -------------------------------------------------------------

void HeroQuickStrip::Build(bool top) {
    y = top ? 0 : 500;
    rect = RECT{0, y, 800, y + 100};
    mode = 0;
    for (int x = 0; x < 800; x += 100) AddRect(this, RECT{x, y, x + 100, y + 100});
    UiMesh* frame = AddBlankSprite();
    frame->Framed(3.0f, 100.0f, -40.0f, -40.0f, 40.0f, 40.0f, 5.0f, 0, 0, 0x100, 0x100);
    frame->flatColour = 0xFF8B8B37;
    frame->flags |= FigureMeshFlag::Flat;
    frame->texture = nullptr;
    frame->visible = false;
    AddBlankSprite()->SetGrid(16.0f, 100.0f, 0.0f, static_cast<float>(y), 800.0f, static_cast<float>(y + 100), 0xE,
                                 0x8E, 0x72, 0xF2, 8, 1);
}

// --- The constructors' second pages ---------------------------------------------------------------

void SpellMakerPage::BuildSpellMaker(CParty* party) {
    rect = RECT{200, 100, 600, 500};
    mode = 6;
    slots = party;
    for (const char* name : {"constrspell1", "constrspell2", "constrspell3", "constrspell4"}) LoadTexture(name, TextureKind::Interface);
    AddRect(this, RECT{200, 250, 300, 350});   // the container
    AddRect(this, RECT{300, 250, 400, 350});   // the spell
    BuildMakerPage(this);
    AddRect(this, RECT{200, 450, 400, 470});
    AddRect(this, RECT{200, 470, 400, 490});
    AddMakerPictures(this);
    AddSurface(new UiSurface(ui, 200, 450, 400, 490, SurfaceBlend::Keyed, true));
    TextManager* clientWorldTexts = g_clientWorld.texts;
    std::string text;
    for (int key : {0x23, 0x24}) {
        text = std::format("infoitem_{}", key);
        text = clientWorldTexts->GetString(text);
        texts.push_back(text);
    }
}

void ConstructorPage::BuildItemMaker(CParty* party) {
    rect = RECT{200, 100, 600, 500};
    mode = 7;
    slots = party;
    for (const char* name : {"constritem1", "constritem2", "constritem3", "constritem4"}) LoadTexture(name, TextureKind::Interface);
    AddRect(this, RECT{200, 250, 300, 350});   // the blueprint
    AddRect(this, RECT{300, 200, 400, 300});   // the parts
    AddRect(this, RECT{300, 300, 400, 400});
    BuildMakerPage(this);
    AddMakerPictures(this);
}

// --- The constructors' second pages (ConstructorPage, ui_camp.h): the slot item under a point which the
// camp's hover and help ask every panel for; the spell maker, the item maker.

namespace {

// Help topics.
constexpr int kTopicNone = 0;
constexpr int kTopicSpellContainer = 0x13;
constexpr int kTopicSpell = 0x14;
constexpr int kTopicModifier = 0x15;
constexpr int kTopicSkillLine = 0x1A;
constexpr int kTopicManaLine = 0x1B;
constexpr int kTopicBuildable = 0xD;
constexpr int kTopicPart = 0xE;
constexpr int kTopicMaterial = 0xF;
constexpr int kTopicItemContainer = 0x10;

// The `nth` (from 0) of the slots' items that `matches`, or null.
template <typename Predicate>
Item* NthSlot(CParty* slots, int nth, Predicate matches) {
    const auto& items = slots->items;
    int found = 0;
    for (int i = 0; i < std::ssize(items); ++i) {
        if (!matches(items[i])) continue;
        if (found == nth) return items[i];
        ++found;
    }
    return nullptr;
}

} // namespace

Item* SpellMakerPage::ItemAt(int x, int y, int* topic) {
    *topic = kTopicNone;
    if (!enabled) return nullptr;
    int region = RegionAt(x, y);
    if (region == 0) {
        *topic = kTopicSpellContainer;
        return NthSlot(slots, 0, IsSpellContainer);
    }
    if (region == 1) {
        *topic = kTopicSpell;
        return NthSlot(slots, 0, IsSpell);
    }
    if (region >= 2 && region < 10) {
        *topic = kTopicModifier;
        if (Item* item = NthSlot(slots, region - 2, IsModifier)) return item;
    }
    if (region == 10) *topic = kTopicSkillLine;
    else if (region == 11) *topic = kTopicManaLine;
    return nullptr;
}

Item* ConstructorPage::ItemAt(int x, int y, int* topic) {
    *topic = kTopicNone;
    if (!enabled) return nullptr;
    int region = RegionAt(x, y);
    if (region == 0) {
        *topic = kTopicBuildable;
        return NthSlot(slots, 0, IsBuildable);
    }
    if (region == 1) {
        *topic = kTopicPart;
        return NthSlot(slots, 0, IsPart);
    }
    if (region == 2) {
        *topic = kTopicItemContainer;
        return NthSlot(slots, 0, IsSpellContainer);
    }
    if (region > 2) {
        *topic = kTopicMaterial;
        return NthSlot(slots, region - 3, IsMaterial);
    }
    return nullptr;
}
