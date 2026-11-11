// The hero panels shared by the camp and the multiplayer hero screen: the statistics column.
// See docs/notes/ui.md ("Camp, global map, hero screens"). The panels' small methods, the equipment panel's shown
// weapon / quick item, its right button and item lookup, and the quick strip's selection and item lookup, are at the
// end (docs/notes/ui.md, section 5).
#include "game/combat/combat.h"
#include "game/console/console.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/items/item_manager.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "game/players/party.h"
#include "engine/data/text_manager.h"
#include "game/ui/ui.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_camp.h"
#include "game/ui/ui_screens.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "game/ui/screen_layout.h"
#include "engine/scene/scene_renderer.h"
#include "engine/models/texture.h"
#include "game/ui/ui_item_info.h"

#include <cstdint>
#include <cstring>
#include <format>
#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

void Down(Rect* rect) { rect->Offset(0, 15); }

UiRegion* AddRect(UiWindow* window, const Rect& rect) {
    UiRegion* region = window->AddRegion();
    region->SetRect(rect);
    return region;
}

constexpr std::uint32_t kGrey = 0x828282;

// The 4 x 4 cell grid over 200..600 x 100..500 shared by the trade and repair grids: a picture (textures 0-3 by quarter)
// and a frame per cell, a region per
// cell (into `cells`), then the surface, painted from `labels`. Texels alternate between 0x1C and 0x80 across and
// down; each row's pictures lie twice as deep as their frames, and half as deep as the row above.
void BuildCellGrid(UiWindow* window, const std::vector<UiLabel>& labels, UiRegion* (&cells)[16]) {
    int cell = 0;
    int u = 0x1C;
    float depth = 32.0f;
    int v = 0x1C;
    for (int top = 100; top < 500; top += 100) {
        float pictureDepth = depth + depth;
        int bottom = top + 100;
        for (int left = 200; left < 600; left += 100) {
            Texture** textures = window->objects.data();
            Texture* texture = top <= 200 ? (left <= 300 ? textures[0] : textures[1])
                                                : (left <= 300 ? textures[2] : textures[3]);
            int right = left + 100;
            UiMesh* picture = window->AddBlankSprite();
            picture->SetRectangle(pictureDepth, kSpriteScale, static_cast<float>(left), static_cast<float>(top),
                                  static_cast<float>(right), static_cast<float>(bottom), u, v, u + 100, v + 100);
            picture->texture = texture;
            UiMesh* frame = window->AddBlankSprite();
            frame->Framed(depth, 100.0f, static_cast<float>(left), static_cast<float>(top),
                                   static_cast<float>(right), static_cast<float>(bottom), 15.0f, u, v, u + 100, v + 100);
            frame->texture = texture;
            cells[cell++] = AddRect(window, Rect{left, top, right, bottom});
            u = 0x9C - u;
        }
        depth *= 0.5f;   // * 0.5
        v = 0x9C - v;
    }
    window->AddSurface(std::make_unique<UiSurface>(200, 100, 600, 500, SurfaceBlend::Keyed, true))
        ->PaintWith([&labels](UiSurface& out) { PaintLabels(out, labels); });
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
    int cell = 0;
    for (int left = 400; left < 600; left += 100)
        for (int top = 100; top < 500; top += 100)
            page->gridCells[cell++] = AddRect(page, Rect{left, top, left + 100, top + 100});
}
void AddMakerPictures(ConstructorPage* page) {
    const float quarters[4][4] = {{200, 100, 400, 300}, {400, 100, 600, 300}, {200, 300, 400, 500}, {400, 300, 600, 500}};
    for (int i = 0; i < 4; ++i) {
        UiMesh* picture = page->AddBlankSprite();
        picture->SetRectangle(7.0f, kSpriteScale, quarters[i][0], quarters[i][1], quarters[i][2], quarters[i][3], 0x1C, 0x1C,
                              0xE4, 0xE4);
        picture->texture = page->objects[i];
    }
    UiMesh* frame = page->AddBlankSprite();
    frame->Framed(3.0f, 100.0f, -40.0f, -40.0f, 40.0f, 40.0f, 5.0f, 0, 0, kUiTextureSize, kUiTextureSize);
    frame->flatColour = 0x96FFFFFF;
    frame->flags |= FigureMeshFlag::Flat;
    frame->texture = nullptr;
    frame->visible = false;
    page->highlight = frame;
}

// Help topics.
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
Item* NthSlot(Party* slots, int nth, Predicate matches) {
    const auto& items = slots->items;
    int found = 0;
    for (Item* item : items) {
        if (!matches(item)) continue;
        if (found == nth) return item;
        ++found;
    }
    return nullptr;
}

const char* const kChangeSound = "buttons\\camp\\change.wav";

constexpr int kWeaponSlots = 4;      // the cells before the quick items
constexpr int kTopicWeapon = 2;
constexpr int kTopicQuickItem = 3;
constexpr int kTopicFirstBodyPart = 4;   // plus the body part
constexpr int kTopicEntry = 1;

// `item`'s place in `list`, or -1.
int IndexIn(const std::vector<Item*>& list, const Item* item) {
    auto found = std::ranges::find(list, item);
    return found == list.end() ? -1 : static_cast<int>(found - list.begin());
}

} // namespace

// --- Statistics page ----------------------------------------------------------------------------

void HeroStatsPanel::ShowStats(int newPage) {
    Rect area = {0x14, 0xA5, 0xB4, 0xB4};
    page = newPage;
    for (UiRegion* row : rows) RemoveRegion(row);
    rows.clear();
    for (UiMesh* button : pointButtons) button->visible = false;
    auto addRow = [this](const Rect& row) { rows.push_back(AddRect(this, row)); };
    if (page == 0) {
        Down(&area);
        for (int i = 0; i < 4; ++i) {
            addRow(area);
            Down(&area);
        }
        Down(&area);
        Down(&area);
        addRow(area);
        Down(&area);
        addRow(area);
        Down(&area);
        addRow(area);
        Down(&area);
        Down(&area);
        addRow(area);
        Down(&area);
        Down(&area);
        addRow(area);
    } else if (page == 10) {
        for (UiMesh* button : pointButtons) button->visible = true;
        const Rect columns[3] = {{0x14, 0xA5, 0x8C, 0xB4}, {0x8C, 0xA5, 0xA0, 0xB4}, {0xA0, 0xA5, 0xB4, 0xB4}};
        for (const Rect& column : columns) {
            area = column;
            for (int i = 0; i < 4; ++i) {
                addRow(area);
                Down(&area);
                if (i == 2) {
                    Down(&area);
                    Down(&area);
                }
            }
        }
        addRow(Rect{0x14, 0xD2, 0xB4, 0xE1});
    } else {
        for (int i = 0; i < 8; ++i) {
            addRow(area);
            Down(&area);
            if (i == 2) Down(&area);
        }
    }
    DrawStats();
}

// --- Statistics drawing ---------------------------------------------------------------------------

void HeroStatsPanel::DrawStats() {
    labels.clear();
    labels.push_back({title, Rect{0, 0x1B, 0xC8, 0x2B}, {1, 0xFFFFFF, render::text::Format::Center}});

    auto hero = [this]() { return owner->HeroRecord(); };
    UnitStats* stats = &hero()->stats;
    Unit* unit = camp ? camp->heroes[heroIndex] : nullptr;   // not checked
    std::string line;
    std::string text;
    TextManager* clientWorldTexts = g_clientWorld.texts.get();
    Rect area;

    if (page == 10) {
        // A new hero's statistics: points left = 75 - (strength + dexterity + intelligence).
        int strength = static_cast<int>(stats->strengthBase);
        int dexterity = static_cast<int>(stats->dexterityBase);
        int intelligence = static_cast<int>(stats->intelligenceBase);
        int sum = intelligence + dexterity + strength;
        area = Rect{0x14, 0x41, 0x82, 0x50};
        DrawRow(area, "Strength", strength, -1);
        Down(&area);
        DrawRow(area, "Dexterity", dexterity, -1);
        Down(&area);
        DrawRow(area, "Intelligence", intelligence, -1);
        Down(&area);
        float sumF = static_cast<float>(sum);
        DrawRow(area, texts[5], static_cast<int>(75.0f - sumF), -1);
        Down(&area);
        Down(&area);
        DrawRow(area, texts[6], points, -1);

        // "-": while above 15 (the integer compared before it is stored as float).
        area = Rect{0x8C, 0x41, 0xA0, 0x50};
        float minimum = 15.0f;
        int values[3] = {strength, dexterity, intelligence};
        float storedStats[3];
        for (int i = 0; i < 3; ++i) {
            float value = static_cast<float>(values[i]);
            storedStats[i] = value;
            std::uint32_t colour = value > minimum ? kUiTextColour : kGrey;
            labels.push_back({"-", area, {0, colour, render::text::Format::Center}});
            Down(&area);
        }
        Down(&area);
        Down(&area);
        labels.push_back({"-", area, {0, points <= 0 ? kGrey : kUiTextColour, render::text::Format::Center}});

        // "+": while below 35 and points are left.
        int left = static_cast<int>(75.0f - sumF);
        area = Rect{0xA0, 0x41, 0xB4, 0x50};
        float maximum = 35.0f;
        for (int i = 0; i < 3; ++i) {
            bool below = storedStats[i] < maximum;
            std::uint32_t colour = below && left > 0 ? kUiTextColour : kGrey;
            labels.push_back({"+", area, {0, colour, render::text::Format::Center}});
            Down(&area);
        }
        Down(&area);
        Down(&area);
        labels.push_back({"+", area, {0, points >= 100 ? kGrey : kUiTextColour, render::text::Format::Center}});
    } else if (page == 0) {
        area = Rect{0x14, 0x41, 0xAF, 0x50};
        text = clientWorldTexts->GetString("infoitem_25");
        labels.push_back({text, area, {0, kUiTextColour}});
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
            labels.push_back({texts[3], area, {0, kUiTextColour}});
            Down(&area);
            AttackParams info;
            unit->controller->GetAttackParams(&info);
            line = std::format("     {}", texts[0]);
            labels.push_back({line, area, {0, kUiTextColour}});
            line = std::format("{}", info.attack);
            labels.push_back({line, area, {0, kUiTextColour, render::text::Format::Right}});
            Down(&area);
            line = std::format("     {}", texts[1]);
            labels.push_back({line, area, {0, kUiTextColour}});
            line = std::format("{}", info.defence);
            labels.push_back({line, area, {0, kUiTextColour, render::text::Format::Right}});
            Down(&area);
            if (static_cast<std::uint32_t>(info.damageType) < 7) {
                line = std::format("damage_{}", info.damageType);
                text = clientWorldTexts->GetString(line);
            }
            line = std::format("     {} {}", texts[2], text);
            labels.push_back({line, area, {0, kUiTextColour}});
            int high = static_cast<int>(info.damageMax);
            int low = static_cast<int>(info.damageMin);
            line = std::format("{}-{}", low, high);
            labels.push_back({line, area, {0, kUiTextColour, render::text::Format::Right}});
            Down(&area);
            Down(&area);
        }
        int weight = static_cast<int>(hero()->CarriedWeight());
        int capacity = static_cast<int>(stats->capacity);
        DrawRow(area, "Encumbrance", weight, capacity);
        Down(&area);
        if (weight > capacity) labels.push_back({texts[4], area, {0, 0xFF, render::text::Format::Right}});
        Down(&area);
        labels.push_back({texts[7], area, {0, kUiTextColour}});
    } else {
        area = Rect{0x14, 0x41, 0xB4, 0x50};
        DrawRow(area, "Strength", static_cast<int>(stats->strength), -1);
        Down(&area);
        DrawRow(area, "Dexterity", static_cast<int>(stats->dexterity), -1);
        Down(&area);
        DrawRow(area, "Intelligence", static_cast<int>(stats->intelligence), -1);
        Down(&area);
        Down(&area);
        DrawRow(area, "Health", static_cast<int>(stats->HitPointCurve()), -1);
        Down(&area);
        DrawRow(area, "Mana", static_cast<int>(stats->ManaCurve()), -1);
        Down(&area);
        DrawRow(area, "Actions", static_cast<int>(stats->rate), -1);
        Down(&area);
        DrawRow(area, "Experience",
                static_cast<int>(stats->pendingExperience + stats->experience), -1);
        Down(&area);
        float carried;
        if (page == MpHeroScreen::kModeStats) {   // the hero screen's (the camp's pages end at 7)
            carried = heroScreen->Weight();
        } else {
            carried = hero()->CarriedWeight();
        }
        int weight = static_cast<int>(carried);
        int capacity = static_cast<int>(stats->capacity);
        DrawRow(area, "Encumbrance", weight, capacity);
        Down(&area);
        if (weight > capacity) labels.push_back({texts[4], area, {0, 0xFF, render::text::Format::Right}});
        Down(&area);
    }
}

// --- The equipment panel's Build ---------------------------------------------------------

void HeroEquipPanel::Build(bool withBody) {
    body = withBody;
    rect = Rect{0, 0, 800, withBody ? 500 : 100};
    mode = 0;
    for (int x = 0; x < 800; x += 100) cells[x / 100] = AddRect(this, Rect{x, 0, x + 100, 100});
    auto addFrame = [this] {
        UiMesh* frame = AddBlankSprite();
        frame->Framed(3.0f, 100.0f, -40.0f, -40.0f, 40.0f, 40.0f, 5.0f, 0, 0, kUiTextureSize, kUiTextureSize);
        frame->flags |= FigureMeshFlag::Flat;
        frame->flatColour = 0xFF8B8B37;
        frame->texture = nullptr;
        frame->visible = false;
        return frame;
    };
    weaponFrame = addFrame();
    quickItemFrame = addFrame();
    // The slots' frames: four on the left half, four on the right, each half with its strip.
    float cell = 100.0f;
    for (int i = 0; i < 4; ++i)
        AddBlankSprite()->Framed(8.0f, 100.0f, i * cell, 0.0f,
                                      (i + 1) * cell, 100.0f, 15.0f, 0xE, 0xE, 0x72, 0x72);
    AddBlankSprite()->SetGrid(16.0f, 100.0f, 0.0f, 0.0f, 400.0f, 100.0f, 0xE, 0xE, 0x72, 0x72, 4, 1);
    for (int i = 4; i < 8; ++i)
        AddBlankSprite()->Framed(8.0f, 100.0f, i * cell, 0.0f,
                                      (i + 1) * cell, 100.0f, 15.0f, 0x8E, 0xE, 0xF2, 0x72);
    AddBlankSprite()->SetGrid(16.0f, 100.0f, 400.0f, 0.0f, 800.0f, 100.0f, 0x8E, 0xE, 0xF2, 0x72, 4, 1);
    if (body) {
        const int* xs = kBodySlotX;
        const int* ys = kBodySlotY;
        for (int i = 0; i < 7; ++i) bodySlots[i] = AddRect(this, Rect{xs[i] - 50, ys[i] - 50, xs[i] + 50, ys[i] + 50});
        for (const char* name : {"camp1", "camp2", "camp3", "camp4"}) LoadTexture(name, TextureKind::Interface);
        const float quarters[4][4] = {{200, 100, 400, 300}, {400, 100, 600, 300}, {200, 300, 400, 500}, {400, 300, 600, 500}};
        for (int i = 0; i < 4; ++i) {
            UiMesh* picture = AddBlankSprite();
            picture->SetRectangle(7.0f, kSpriteScale, quarters[i][0], quarters[i][1], quarters[i][2], quarters[i][3], 0x1C,
                                  0x1C, 0xE4, 0xE4);
            picture->texture = objects[i + 1];
        }
    }
}

// --- The statistics panel's Build ---------------------------------------------

void HeroStatsPanel::Build() {
    rect = Rect{0, 100, 200, 400};
    previousButton = AddRect(this, Rect{0, 0x7D, 0x1E, 0x91});
    nextButton = AddRect(this, Rect{0xAA, 0x7D, 200, 0x91});
    AddSurface(std::make_unique<UiSurface>(0, 100, 200, 400, SurfaceBlend::Keyed, true))
        ->PaintWith([this](UiSurface& out) { PaintLabels(out, labels); });
    nextArrow = AddBlankSprite();
    nextArrow->SetRectangle(5.0f, kSpriteScale, 176.0f, 125.0f, 192.0f, 145.0f, 2, 0x81, 0x12, 0x95);
    previousArrow = AddBlankSprite();
    previousArrow->SetRectangle(5.0f, kSpriteScale, 8.0f, 125.0f, 24.0f, 145.0f, 0x12, 0x81, 2, 0x95);
    AddBlankSprite()->SetRectangle(6.0f, kSpriteScale, 14.0f, 110.0f, 100.0f, 160.0f, 0xE7, 0x4C, 0x91, 0x88);
    AddBlankSprite()->SetRectangle(6.0f, kSpriteScale, 100.0f, 110.0f, 186.0f, 160.0f, 0x91, 0x4C, 0xE7, 0x88);
    // The new hero page's -/+ buttons (shown only there).
    const float buttons[kPointButtons][4] = {{140, 165, 160, 180}, {140, 180, 160, 195}, {140, 195, 160, 210},
                                             {140, 240, 160, 255}, {160, 165, 180, 180}, {160, 180, 180, 195},
                                             {160, 195, 180, 210}, {160, 240, 180, 255}};
    for (int i = 0; i < kPointButtons; ++i) {
        const auto& b = buttons[i];
        pointButtons[i] = AddBlankSprite();
        pointButtons[i]->SetRectangle(5.0f, kSpriteScale, b[0], b[1], b[2], b[3], 0x90, 0xB1, 0xA4, 0xC0);
    }
    TextManager* clientWorldTexts = g_clientWorld.texts.get();
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
    rect = Rect{200, 100, 600, 500};
    sold = soldItems;
    buyKind = spells ? PriceKind::SpellBuy : PriceKind::ItemBuy;
    sellKind = spells ? PriceKind::SpellSell : PriceKind::ItemSell;
    exchange = isExchange;
    for (const char* name : {"trade1", "trade2", "trade3", "trade4"}) LoadTexture(name, TextureKind::Interface);
    BuildCellGrid(this, labels, cells);
}

// --- The item under the mouse ---------------------------------------------------------------

ItemUnder TradeGrid::ItemAt(Point point) {
    if (!enabled) return {};
    const int cell = RegionIndex(cells, RegionUnder(point));
    if (cell < 0) return {};
    if (cell < kGridCellsPerList) {
        const int topic = exchange ? kTopicExchangeBuy : buyKind == PriceKind::ItemBuy ? kTopicItemBuy : kTopicSpellBuy;
        return {cell < std::ssize(*bought) ? (*bought)[cell] : nullptr, topic};
    }
    // The sold list's topic also follows buyKind.
    const int topic = exchange ? kTopicExchangeSell : buyKind == PriceKind::ItemBuy ? kTopicItemSell : kTopicSpellSell;
    const int index = cell - kGridCellsPerList;
    return {index < std::ssize(*sold) ? (*sold)[index] : nullptr, topic};
}

ItemUnder RepairGrid::ItemAt(Point point) {
    if (!enabled) return {};
    const int cell = RegionIndex(cells, RegionUnder(point));
    if (cell < 0) return {};
    return {cell < std::ssize(*items) ? (*items)[cell] : nullptr, kTopicRepair};
}

// --- The repair grid's Build ---------------------------------------------------------------

void RepairGrid::Build(std::vector<Item*>* damaged) {
    rect = Rect{200, 100, 600, 500};
    mode = 8;
    items = damaged;
    for (const char* name : {"repair1", "repair2", "repair3", "repair4"}) LoadTexture(name, TextureKind::Interface);
    BuildCellGrid(this, labels, cells);
}

// --- The quick strip's Build -------------------------------------------------------------

void HeroQuickStrip::Build(bool top) {
    y = top ? 0 : 500;
    rect = Rect{0, y, 800, y + 100};
    mode = 0;
    for (int x = 0; x < 800; x += 100) cells[x / 100] = AddRect(this, Rect{x, y, x + 100, y + 100});
    highlightFrame = AddBlankSprite();
    highlightFrame->Framed(3.0f, 100.0f, -40.0f, -40.0f, 40.0f, 40.0f, 5.0f, 0, 0, kUiTextureSize, kUiTextureSize);
    highlightFrame->flatColour = 0xFF8B8B37;
    highlightFrame->flags |= FigureMeshFlag::Flat;
    highlightFrame->texture = nullptr;
    highlightFrame->visible = false;
    AddBlankSprite()->SetGrid(16.0f, 100.0f, 0.0f, static_cast<float>(y), 800.0f, static_cast<float>(y + 100), 0xE,
                                 0x8E, 0x72, 0xF2, 8, 1);
}

// --- The constructors' second pages ---------------------------------------------------------------

void SpellMakerPage::BuildSpellMaker(Party* party) {
    rect = Rect{200, 100, 600, 500};
    mode = 6;
    slots = party;
    for (const char* name : {"constrspell1", "constrspell2", "constrspell3", "constrspell4"}) LoadTexture(name, TextureKind::Interface);
    containerSlot = AddRect(this, Rect{200, 250, 300, 350});
    spellSlot = AddRect(this, Rect{300, 250, 400, 350});
    BuildMakerPage(this);
    costLines[0] = AddRect(this, Rect{200, 450, 400, 470});
    costLines[1] = AddRect(this, Rect{200, 470, 400, 490});
    AddMakerPictures(this);
    AddSurface(std::make_unique<UiSurface>(200, 450, 400, 490, SurfaceBlend::Keyed, true))
        ->PaintWith([this](UiSurface& out) { PaintLabels(out, labels); });
    TextManager* clientWorldTexts = g_clientWorld.texts.get();
    std::string text;
    for (int key : {0x23, 0x24}) {
        text = std::format("infoitem_{}", key);
        text = clientWorldTexts->GetString(text);
        texts.push_back(text);
    }
}

void ConstructorPage::BuildItemMaker(Party* party) {
    rect = Rect{200, 100, 600, 500};
    mode = 7;
    slots = party;
    for (const char* name : {"constritem1", "constritem2", "constritem3", "constritem4"}) LoadTexture(name, TextureKind::Interface);
    blueprintSlot = AddRect(this, Rect{200, 250, 300, 350});
    partSlot = AddRect(this, Rect{300, 200, 400, 300});
    containerSlot = AddRect(this, Rect{300, 300, 400, 400});
    BuildMakerPage(this);
    AddMakerPictures(this);
}

// --- The constructors' item lookup ------------------------------------------------------------------

// (ConstructorPage, ui_camp.h): the slot item under a point which the camp's hover and help ask every panel for; the
// spell maker, the item maker.

ItemUnder SpellMakerPage::ItemAt(Point point) {
    if (!enabled) return {};
    const UiRegion* region = RegionUnder(point);
    if (!region) return {};
    if (region == containerSlot) return {NthSlot(slots, 0, IsSpellContainer), kTopicSpellContainer};
    if (region == spellSlot) return {NthSlot(slots, 0, IsSpell), kTopicSpell};
    if (const int cell = RegionIndex(gridCells, region); cell >= 0)
        return {NthSlot(slots, cell, IsModifier), kTopicModifier};
    if (region == costLines[0]) return {nullptr, kTopicSkillLine};
    if (region == costLines[1]) return {nullptr, kTopicManaLine};
    return {};
}

ItemUnder ConstructorPage::ItemAt(Point point) {
    if (!enabled) return {};
    const UiRegion* region = RegionUnder(point);
    if (!region) return {};
    if (region == blueprintSlot) return {NthSlot(slots, 0, IsBuildable), kTopicBuildable};
    if (region == partSlot) return {NthSlot(slots, 0, IsPart), kTopicPart};
    if (region == containerSlot) return {NthSlot(slots, 0, IsSpellContainer), kTopicItemContainer};
    if (const int cell = RegionIndex(gridCells, region); cell >= 0)
        return {NthSlot(slots, cell, IsMaterial), kTopicMaterial};
    return {};
}

// The hero's weapons (the strip's left part), quick items (from the right) and worn armour, with the frames of
// the shown weapon and the selected quick item.
void HeroEquipPanel::DrawSprites(SceneRenderer* pass) {
    PartyUnitRecord* hero = owner->HeroRecord();
    std::uint32_t shown = hero->weaponIndex;
    if (shown < static_cast<std::uint32_t>(std::ssize(hero->weapons))) {
        weaponFrame->visible = true;
        weaponFrame->Place(static_cast<float>(shown * 100u + 50u), 50.0f, 3.0f);
    } else {
        weaponFrame->visible = false;
    }
    int currentSelected = selected;
    if (currentSelected >= 0 && currentSelected < std::ssize(hero->quickItems)) {
        quickItemFrame->visible = true;
        quickItemFrame->Place(static_cast<float>(750 - currentSelected * 100), 50.0f, 3.0f);
    } else {
        quickItemFrame->visible = false;
    }
    DrawBase(pass);
    ItemScreen* currentOwner = owner;
    for (int i = 0; i < std::ssize(hero->weapons); ++i) {
        ItemCell cell{hero->weapons[i], static_cast<float>(50 + 100 * i), 50.0f, 12.0f, 100.0f,
                      mode};
        currentOwner->DrawItem(pass, cell);
    }
    for (int i = 0; i < std::ssize(hero->quickItems); ++i) {
        ItemCell cell{hero->quickItems[i], static_cast<float>(750 - 100 * i), 50.0f, 12.0f, 100.0f,
                      mode};
        currentOwner->DrawItem(pass, cell);
    }
    if (!body) return;

    // The seven body slots' rectangles: 70 x 70 around their centres.
    const int* centreX = kBodySlotX;
    const int* centreY = kBodySlotY;
    Rect slots[7];
    for (int i = 0; i < 7; ++i) {
        Rect r = screen_layout::ToScreen(Rect{centreX[i] - 35, centreY[i] - 35, centreX[i] + 35, centreY[i] + 35});
        slots[i] = r;
    }
    ClearUiDepth(0.0f);
    ClearUiDepth(1.0f, slots, 7);
    for (Item* item : hero->armor) {
        int armorType = static_cast<int>(item->AsArmor()->GetType());
        ItemCell cell{item, static_cast<float>(centreX[armorType]), static_cast<float>(centreY[armorType]), 6.0f, 100.0f,
                      mode};
        currentOwner->DrawItem(pass, cell);
    }
    ClearUiDepth(1.0f);
}

// --- the shown entries ---------------------------------------------------------------------------

void HeroEquipPanel::ShowWeapon(Item* weapon) {
    PartyUnitRecord* hero = owner->HeroRecord();
    hero->weaponIndex = static_cast<std::uint32_t>(IndexIn(hero->weapons, weapon));
}

void HeroEquipPanel::ShowQuickItem(Item* item) {
    PartyUnitRecord* hero = owner->HeroRecord();
    selected = IndexIn(hero->quickItems, item);
}

void HeroQuickStrip::ShowEntry(Item* item) {
    PartyUnitRecord* hero = owner->HeroRecord();
    selected = IndexIn(hero->spells, item);
}

// --- the equipment panel's right button ----------------------------------------------------------

void HeroEquipPanel::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::RightDown: case UiEventKind::RightDouble: RightDown(event); break;
    default: UiItemPanel::OnEvent(event); break;
    }
}

void HeroEquipPanel::RightDown(UiEvent& event) {
    if (!body || !enabled) return;
    const UiRegion* region = RegionUnder(event.at);
    if (!region) return;
    PartyUnitRecord* hero = owner->HeroRecord();
    const int cell = RegionIndex(cells, region);
    if (cell >= 0 && cell < std::ssize(hero->weapons) && cell < kWeaponSlots) {
        g_interface.PlayInterfaceSound(kChangeSound);
        hero->weaponIndex = static_cast<std::uint32_t>(cell);
    }
    if (camp) camp->RefreshWeapons();
}

// --- the item under a point ----------------------------------------------------------------------

ItemUnder HeroEquipPanel::ItemAt(Point point) {
    if (!enabled) return {};
    const UiRegion* region = RegionUnder(point);
    if (!region) return {};
    PartyUnitRecord* hero = owner->HeroRecord();
    if (const int cell = RegionIndex(cells, region); cell >= 0) {
        if (cell < kWeaponSlots)
            return {cell < std::ssize(hero->weapons) ? hero->weapons[cell] : nullptr, kTopicWeapon};
        const int fromEnd = kCells - 1 - cell;
        return {fromEnd < std::ssize(hero->quickItems) ? hero->quickItems[fromEnd] : nullptr, kTopicQuickItem};
    }
    const int part = RegionIndex(bodySlots, region);
    if (!body || part < 0) return {};
    for (Item* worn : hero->armor) {
        ItemArmor* armour = worn->AsArmor();
        if (static_cast<int>(armour->GetType()) == part) return {armour, kTopicFirstBodyPart + part};
    }
    return {nullptr, kTopicFirstBodyPart + part};
}

ItemUnder HeroQuickStrip::ItemAt(Point point) {
    if (!enabled) return {};
    const int cell = RegionIndex(cells, RegionUnder(point));
    PartyUnitRecord* hero = owner->HeroRecord();
    if (cell < 0) return {};
    return {cell < std::ssize(hero->spells) ? hero->spells[cell] : nullptr, kTopicEntry};
}
