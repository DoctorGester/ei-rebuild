// The hero panels shared by the camp and the multiplayer hero screen: the statistics column.
// See docs/notes/ui.md ("Camp, global map, hero screens").
// Floating point follows the x87 code of the original game: `double` for values it keeps on the FPU
// stack, static_cast<float> where it stores to a float. Constants are read from memory.
#include "ui.h"
#include "ui_camp.h"

#include "console.h"
#include "item_db_text.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstdint>
#include <string>

#include <cstring>
#include <initializer_list>
#include "unit.h"
#include "combat.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

namespace {

inline double D(float f) { return f; }

void Down(RECT* rect) { gameapi::OffsetRect(rect, 0, 15); }

CUiRegion* AddRect(CUiWindow* window, const RECT& r) {
    CUiRegion* region = window->AddRegion();
    region->SetRect(r.left, r.top, r.right, r.bottom);
    return region;
}

void ShowFrom(CUiWindow* window, int first, std::uint32_t shown) {
    for (int i = first; i < std::ssize(window->sceneObjects_); ++i)
        window->SceneMesh(i)->visible_ = static_cast<int>(shown);
}

constexpr std::uint32_t kBright = 0xA7D7E4;
constexpr std::uint32_t kGrey = 0x828282;

void SetPlainFlag(CUiMesh* sprite) { sprite->flags_ |= 0x100; }

// The 4 x 4 cell grid over 200..600 x 100..500 shared by the trade and repair grids: a picture (textures 0-3 by quarter)
// and a frame per cell, a region per
// cell, then the surface. Texels alternate between 0x1C and 0x80 across and down; each row's
// pictures lie twice as deep as their frames, and half as deep as the row above.
void BuildCellGrid(CUiWindow* window) {
    int u = 0x1C;
    float depth = 32.0f;
    int v = 0x1C;
    for (int top = 100; top < 500; top += 100) {
        float pictureDepth = static_cast<float>(D(depth) + D(depth));
        int bottom = top + 100;
        for (int left = 200; left < 600; left += 100) {
            CTexture** textures = window->objects_.data();
            CTexture* texture = top <= 200 ? (left <= 300 ? textures[0] : textures[1])
                                                : (left <= 300 ? textures[2] : textures[3]);
            int right = left + 100;
            CUiMesh* picture = window->AddBlankSprite();
            picture->SetRectangle(pictureDepth, 100.0f, static_cast<float>(left), static_cast<float>(top),
                                  static_cast<float>(right), static_cast<float>(bottom), u, v, u + 100, v + 100);
            picture->texture_ = texture;
            CUiMesh* frame = window->AddBlankSprite();
            frame->Framed(depth, 100.0f, static_cast<float>(left), static_cast<float>(top),
                                   static_cast<float>(right), static_cast<float>(bottom), 15.0f, u, v, u + 100, v + 100);
            frame->texture_ = texture;
            AddRect(window, RECT{left, top, right, bottom});
            u = 0x9C - u;
        }
        depth = static_cast<float>(D(depth) * D(0.5f /*0073B86C*/));   // * 0.5
        v = 0x9C - v;
    }
    window->AddSurface(new CUiSurface(window->interface_, 200, 100, 600, 500, 5, 1));
}

constexpr int kGridCellsPerList = 8;
constexpr int kGridCells = 16;
constexpr int kTopicTraderBuy = 0x1C, kTopicTraderSell = 0x1D;   // flag_ set
constexpr int kTopicItemBuy = 0x0B, kTopicItemSell = 0x0C;       // the item constructor's kinds
constexpr int kTopicSpellBuy = 0x11, kTopicSpellSell = 0x12;     // the spell constructor's
constexpr int kTopicRepair = 0x19;

// The pages' common part: the 2 x 4 grid of regions over 400..600 x 100..500 (column by column),
// the four quarter pictures (textures 0-3) and a hidden highlight frame.
void BuildMakerPage(CConstructorPage* page) {
    for (int left = 400; left < 600; left += 100)
        for (int top = 100; top < 500; top += 100) AddRect(page, RECT{left, top, left + 100, top + 100});
}
void AddMakerPictures(CConstructorPage* page) {
    const float quarters[4][4] = {{200, 100, 400, 300}, {400, 100, 600, 300}, {200, 300, 400, 500}, {400, 300, 600, 500}};
    for (int i = 0; i < 4; ++i) {
        CUiMesh* picture = page->AddBlankSprite();
        picture->SetRectangle(7.0f, 100.0f, quarters[i][0], quarters[i][1], quarters[i][2], quarters[i][3], 0x1C, 0x1C,
                              0xE4, 0xE4);
        picture->texture_ = page->objects_[i];
    }
    CUiMesh* frame = page->AddBlankSprite();
    frame->Framed(3.0f, 100.0f, -40.0f, -40.0f, 40.0f, 40.0f, 5.0f, 0, 0, 0x100, 0x100);
    frame->flatColour_ = 0x96FFFFFF;
    SetPlainFlag(frame);
    frame->texture_ = nullptr;
    frame->visible_ = 0;
}

} // namespace

// --- Statistics page ----------------------------------------------------------------------------

void CHeroStatsPanel::ShowStats(int page) {
    RECT rect = {0x14, 0xA5, 0xB4, 0xB4};
    page_ = page;
    while (std::ssize(regions_) > 2) RemoveRegionAt(2);
    ShowFrom(this, 4, 0);
    if (page_ == 0) {
        Down(&rect);
        for (int i = 0; i < 4; ++i) {
            AddRect(this, rect);
            Down(&rect);
        }
        Down(&rect);
        Down(&rect);
        AddRect(this, rect);
        Down(&rect);
        AddRect(this, rect);
        Down(&rect);
        AddRect(this, rect);
        Down(&rect);
        Down(&rect);
        AddRect(this, rect);
        Down(&rect);
        Down(&rect);
        AddRect(this, rect);
    } else if (page_ == 10) {
        ShowFrom(this, 4, 1);
        const RECT columns[3] = {{0x14, 0xA5, 0x8C, 0xB4}, {0x8C, 0xA5, 0xA0, 0xB4}, {0xA0, 0xA5, 0xB4, 0xB4}};
        for (const RECT& column : columns) {
            rect = column;
            for (int i = 0; i < 4; ++i) {
                AddRect(this, rect);
                Down(&rect);
                if (i == 2) {
                    Down(&rect);
                    Down(&rect);
                }
            }
        }
        AddRect(this, RECT{0x14, 0xD2, 0xB4, 0xE1});
    } else {
        for (int i = 0; i < 8; ++i) {
            AddRect(this, rect);
            Down(&rect);
            if (i == 2) Down(&rect);
        }
    }
    DrawStats();
}

// --- Statistics drawing ---------------------------------------------------------------------------

void CHeroStatsPanel::DrawStats() {
    CUiSurface* draw = surfaces_[0];
    draw->GetDC();
    draw->Clear();
    RECT title = {0, 0x1B, 0xC8, 0x2B};
    draw->DrawLine(title_, &title, 1, 0xFFFFFF, 1, 1);

    auto hero = [this]() { return owner_->HeroRecord(); };
    UnitStats* stats = &hero()->stats;
    CUnit* unit = owner_->HeroUnit(heroIndex_);
    std::string line;
    std::string text;
    CTextManager* texts = g_clientWorld->texts_;
    RECT rect;

    if (page_ == 10) {
        // A new hero's statistics: points left = 75 - (strength + dexterity + intelligence).
        int strength = static_cast<int>(libcrt::Ftol(D(stats->strengthBase)));
        int dexterity = static_cast<int>(libcrt::Ftol(D(stats->dexterityBase)));
        int intelligence = static_cast<int>(libcrt::Ftol(D(stats->intelligenceBase)));
        int sum = intelligence + dexterity + strength;
        rect = RECT{0x14, 0x41, 0x82, 0x50};
        DrawRow(&rect, "Strength", strength, -1);
        Down(&rect);
        DrawRow(&rect, "Dexterity", dexterity, -1);
        Down(&rect);
        DrawRow(&rect, "Intelligence", intelligence, -1);
        Down(&rect);
        float sumF = static_cast<float>(static_cast<double>(sum));
        DrawRow(&rect, texts_[5], static_cast<int>(libcrt::Ftol(D(75.0f /*00741F04*/) - D(sumF))), -1);
        Down(&rect);
        Down(&rect);
        DrawRow(&rect, texts_[6], points_, -1);

        // "-": while above 15 (the integer compared before it is stored as float).
        rect = RECT{0x8C, 0x41, 0xA0, 0x50};
        double minimum = D(15.0f /*00741EFC*/);
        int values[3] = {strength, dexterity, intelligence};
        float storedStats[3];
        for (int i = 0; i < 3; ++i) {
            double value = static_cast<double>(values[i]);
            storedStats[i] = static_cast<float>(value);
            std::uint32_t colour = value > minimum ? kBright : kGrey;   // FCOMP; TEST AH,0x41
            draw->DrawLine("-", &rect, 0, colour, 1, 1);
            Down(&rect);
        }
        Down(&rect);
        Down(&rect);
        draw->DrawLine("-", &rect, 0, points_ <= 0 ? kGrey : kBright, 1, 1);

        // "+": while below 35 and points are left.
        int left = static_cast<int>(libcrt::Ftol(D(75.0f /*00741F04*/) - D(sumF)));
        rect = RECT{0xA0, 0x41, 0xB4, 0x50};
        double maximum = D(35.0f /*00741F00*/);
        for (int i = 0; i < 3; ++i) {
            EI_NAN_AUDIT(storedStats[i]);
            bool below = D(storedStats[i]) < maximum;
            std::uint32_t colour = below && left > 0 ? kBright : kGrey;
            draw->DrawLine("+", &rect, 0, colour, 1, 1);
            Down(&rect);
        }
        Down(&rect);
        Down(&rect);
        draw->DrawLine("+", &rect, 0, points_ >= 100 ? kGrey : kBright, 1, 1);
    } else if (page_ == 0) {
        rect = RECT{0x14, 0x41, 0xAF, 0x50};
        texts->GetString("infoitem_25", &text);
        draw->DrawLine(text.c_str(), &rect, 0, kBright, 0, 1);
        Down(&rect);
        DrawPart(&rect, "skull", 0);   // skull
        Down(&rect);
        DrawPart(&rect, "arms", 2);   // arms
        Down(&rect);
        DrawPart(&rect, "torso", 1);   // torso
        Down(&rect);
        DrawPart(&rect, "legs", 4);   // legs
        Down(&rect);
        Down(&rect);
        if (static_cast<int>(hero()->weaponIndex) == -1) {
            for (int i = 0; i < 5; ++i) Down(&rect);
        } else {
            draw->DrawLine(texts_[3], &rect, 0, kBright, 0, 1);
            Down(&rect);
            AttackParams info;
            unit->Controller()->GetAttackParams(&info);
            line = str::Format("     %s", texts_[0].c_str());
            draw->DrawLine(line, &rect, 0, kBright, 0, 1);
            line = str::Format("%d", info.attack);
            draw->DrawLine(line, &rect, 0, kBright, 2, 1);
            Down(&rect);
            line = str::Format("     %s", texts_[1].c_str());
            draw->DrawLine(line, &rect, 0, kBright, 0, 1);
            line = str::Format("%d", info.defence);
            draw->DrawLine(line, &rect, 0, kBright, 2, 1);
            Down(&rect);
            if (static_cast<std::uint32_t>(info.damageType) < 7) {
                line = str::Format("damage_%d", info.damageType);
                texts->GetString(line.c_str(), &text);
            }
            line = str::Format("     %s %s", texts_[2].c_str(), text.c_str());
            draw->DrawLine(line, &rect, 0, kBright, 0, 1);
            int high = static_cast<int>(libcrt::Ftol(D(info.damageMax)));
            int low = static_cast<int>(libcrt::Ftol(D(info.damageMin)));
            line = str::Format("%d-%d", low, high);
            draw->DrawLine(line, &rect, 0, kBright, 2, 1);
            Down(&rect);
            Down(&rect);
        }
        int weight = static_cast<int>(libcrt::Ftol(hero()->CarriedWeight()));
        int capacity = static_cast<int>(libcrt::Ftol(D(stats->capacity)));
        DrawRow(&rect, "Encumbrance", weight, capacity);   // Encumbrance
        Down(&rect);
        if (weight > capacity) draw->DrawLine(texts_[4], &rect, 0, 0xFF, 2, 1);
        Down(&rect);
        draw->DrawLine(texts_[7], &rect, 0, kBright, 0, 1);
    } else {
        rect = RECT{0x14, 0x41, 0xB4, 0x50};
        DrawRow(&rect, "Strength", static_cast<int>(libcrt::Ftol(D(stats->strength))), -1);   // Strength
        Down(&rect);
        DrawRow(&rect, "Dexterity", static_cast<int>(libcrt::Ftol(D(stats->dexterity))), -1);   // Dexterity
        Down(&rect);
        DrawRow(&rect, "Intelligence", static_cast<int>(libcrt::Ftol(D(stats->intelligence))), -1);   // Intelligence
        Down(&rect);
        Down(&rect);
        DrawRow(&rect, "Health", static_cast<int>(libcrt::Ftol(stats->HitPointCurve())), -1);              // Health
        Down(&rect);
        DrawRow(&rect, "Mana", static_cast<int>(libcrt::Ftol(stats->ManaCurve())), -1);            // Mana
        Down(&rect);
        DrawRow(&rect, "Actions", static_cast<int>(libcrt::Ftol(D(stats->rate))), -1);   // Actions
        Down(&rect);
        DrawRow(&rect, "Experience",
                  static_cast<int>(libcrt::Ftol(D(stats->pendingExperience) + D(stats->experience))), -1);   // Experience
        Down(&rect);
        double carried;
        if (page_ == 11) {
            carried = owner_->Weight();
        } else {
            carried = hero()->CarriedWeight();
        }
        int weight = static_cast<int>(libcrt::Ftol(carried));
        int capacity = static_cast<int>(libcrt::Ftol(D(stats->capacity)));
        DrawRow(&rect, "Encumbrance", weight, capacity);   // Encumbrance
        Down(&rect);
        if (weight > capacity) draw->DrawLine(texts_[4], &rect, 0, 0xFF, 2, 1);
        Down(&rect);
    }
    draw->ReleaseDC();
}

// --- The equipment panel's Build ---------------------------------------------------------

void CHeroEquipPanel::Build(bool body) {
    body_ = body;
    rect_ = RECT{0, 0, 800, body ? 500 : 100};
    mode_ = 0;
    for (int x = 0; x < 800; x += 100) AddRect(this, RECT{x, 0, x + 100, 100});
    for (int i = 0; i < 2; ++i) {
        CUiMesh* frame = AddBlankSprite();
        frame->Framed(3.0f, 100.0f, -40.0f, -40.0f, 40.0f, 40.0f, 5.0f, 0, 0, 0x100, 0x100);
        SetPlainFlag(frame);
        frame->flatColour_ = 0xFF8B8B37;
        frame->texture_ = nullptr;
        frame->visible_ = 0;
    }
    // The slots' frames: four on the left half, four on the right, each half with its strip.
    float cell = 100.0f /*0073BAB0*/;
    for (int i = 0; i < 4; ++i)
        AddBlankSprite()->Framed(8.0f, 100.0f, static_cast<float>(static_cast<double>(i) * D(cell)), 0.0f,
                                      static_cast<float>(static_cast<double>(i + 1) * D(cell)), 100.0f, 15.0f, 0xE, 0xE, 0x72, 0x72);
    AddBlankSprite()->SetGridVanilla(16.0f, 100.0f, 0.0f, 0.0f, 400.0f, 100.0f, 0xE, 0xE, 0x72, 0x72, 4, 1);
    for (int i = 4; i < 8; ++i)
        AddBlankSprite()->Framed(8.0f, 100.0f, static_cast<float>(static_cast<double>(i) * D(cell)), 0.0f,
                                      static_cast<float>(static_cast<double>(i + 1) * D(cell)), 100.0f, 15.0f, 0x8E, 0xE, 0xF2, 0x72);
    AddBlankSprite()->SetGridVanilla(16.0f, 100.0f, 400.0f, 0.0f, 800.0f, 100.0f, 0x8E, 0xE, 0xF2, 0x72, 4, 1);
    if (body_) {
        const int* xs = kBodySlotX;
        const int* ys = kBodySlotY;
        for (int i = 0; i < 7; ++i) AddRect(this, RECT{xs[i] - 50, ys[i] - 50, xs[i] + 50, ys[i] + 50});
        LoadTexture("camp1", 3);   // camp1
        LoadTexture("camp2", 3);   // camp2
        LoadTexture("camp3", 3);   // camp3
        LoadTexture("camp4", 3);   // camp4
        const float quarters[4][4] = {{200, 100, 400, 300}, {400, 100, 600, 300}, {200, 300, 400, 500}, {400, 300, 600, 500}};
        for (int i = 0; i < 4; ++i) {
            CUiMesh* picture = AddBlankSprite();
            picture->SetRectangle(7.0f, 100.0f, quarters[i][0], quarters[i][1], quarters[i][2], quarters[i][3], 0x1C,
                                  0x1C, 0xE4, 0xE4);
            picture->texture_ = objects_[i + 1];
        }
    }
    selected_ = -1;
}

// --- The statistics panel's Build ---------------------------------------------

void CHeroStatsPanel::Build() {
    rect_ = RECT{0, 100, 200, 400};
    AddRect(this, RECT{0, 0x7D, 0x1E, 0x91});      // previous hero
    AddRect(this, RECT{0xAA, 0x7D, 200, 0x91});    // next hero
    {
        AddSurface(new CUiSurface(interface_, 0, 100, 200, 400, 5, 1));
    }
    AddBlankSprite()->SetRectangle(5.0f, 100.0f, 176.0f, 125.0f, 192.0f, 145.0f, 2, 0x81, 0x12, 0x95);
    AddBlankSprite()->SetRectangle(5.0f, 100.0f, 8.0f, 125.0f, 24.0f, 145.0f, 0x12, 0x81, 2, 0x95);
    AddBlankSprite()->SetRectangle(6.0f, 100.0f, 14.0f, 110.0f, 100.0f, 160.0f, 0xE7, 0x4C, 0x91, 0x88);
    AddBlankSprite()->SetRectangle(6.0f, 100.0f, 100.0f, 110.0f, 186.0f, 160.0f, 0x91, 0x4C, 0xE7, 0x88);
    // The new hero page's -/+ buttons (scene objects 4-11, shown only there).
    const float buttons[8][4] = {{140, 165, 160, 180}, {140, 180, 160, 195}, {140, 195, 160, 210}, {140, 240, 160, 255},
                                 {160, 165, 180, 180}, {160, 180, 180, 195}, {160, 195, 180, 210}, {160, 240, 180, 255}};
    for (const auto& b : buttons) AddBlankSprite()->SetRectangle(5.0f, 100.0f, b[0], b[1], b[2], b[3], 0x90, 0xB1, 0xA4, 0xC0);
    CTextManager* texts = g_clientWorld->texts_;
    std::string text;
    for (const char* key : {"infounit_3", "infounit_4", "infounit_9", "infounit_14", "infounit_17", "lmp_points_left",
                            "lmp_pers_tall", "magic_skills"}) {
        texts->GetString(key, &text);
        texts_.push_back(text);
    }
    heldRegion_ = -1;
    holdTime_ = 0.0f;
}

// --- The trade grid's Build ----------------------------------------------------------------

void CTradeGrid::Build(std::vector<CItem*>* bought, std::vector<CItem*>* sold, char spells, char flag) {
    bought_ = bought;
    rect_ = RECT{200, 100, 600, 500};
    sold_ = sold;
    buyKind_ = spells ? 0 : 4;
    sellKind_ = spells ? 1 : 5;
    flag_ = flag;
    LoadTexture("trade1", 3);   // trade1
    LoadTexture("trade2", 3);   // trade2
    LoadTexture("trade3", 3);   // trade3
    LoadTexture("trade4", 3);   // trade4
    BuildCellGrid(this);
}

// --- The item under the mouse ---------------------------------------------------------------

CItem* CTradeGrid::ItemAt(int x, int y, int* topic) {
    *topic = 0;
    if (!enabled_) return nullptr;
    int region = RegionAt(x, y);
    if (region >= 0 && region < kGridCellsPerList) {
        *topic = flag_ ? kTopicTraderBuy : buyKind_ ? kTopicItemBuy : kTopicSpellBuy;
        if (region < std::ssize(*bought_)) return (*bought_)[region];
        return nullptr;
    }
    if (region < kGridCellsPerList || region >= kGridCells) return nullptr;
    // The sold list's topic also follows buyKind_.
    *topic = flag_ ? kTopicTraderSell : buyKind_ ? kTopicItemSell : kTopicSpellSell;
    if (region - kGridCellsPerList < std::ssize(*sold_)) return (*sold_)[region - kGridCellsPerList];
    return nullptr;
}

CItem* CRepairGrid::ItemAt(int x, int y, int* topic) {
    *topic = 0;
    if (!enabled_) return nullptr;
    int region = RegionAt(x, y);
    if (region < 0 || region >= kGridCells) return nullptr;
    *topic = kTopicRepair;
    return region < std::ssize(*items_) ? (*items_)[region] : nullptr;
}

// --- The repair grid's Build ---------------------------------------------------------------

void CRepairGrid::Build(std::vector<CItem*>* items) {
    rect_ = RECT{200, 100, 600, 500};
    mode_ = 8;
    items_ = items;
    LoadTexture("repair1", 3);   // repair1
    LoadTexture("repair2", 3);   // repair2
    LoadTexture("repair3", 3);   // repair3
    LoadTexture("repair4", 3);   // repair4
    BuildCellGrid(this);
}

// --- The quick strip's Build -------------------------------------------------------------

void CHeroQuickStrip::Build(bool top) {
    y_ = top ? 0 : 500;
    rect_ = RECT{0, y_, 800, y_ + 100};
    mode_ = 0;
    for (int x = 0; x < 800; x += 100) AddRect(this, RECT{x, y_, x + 100, y_ + 100});
    CUiMesh* frame = AddBlankSprite();
    frame->Framed(3.0f, 100.0f, -40.0f, -40.0f, 40.0f, 40.0f, 5.0f, 0, 0, 0x100, 0x100);
    frame->flatColour_ = 0xFF8B8B37;
    SetPlainFlag(frame);
    frame->texture_ = nullptr;
    frame->visible_ = 0;
    AddBlankSprite()->SetGridVanilla(16.0f, 100.0f, 0.0f, static_cast<float>(y_), 800.0f, static_cast<float>(y_ + 100), 0xE,
                                 0x8E, 0x72, 0xF2, 8, 1);
    selected_ = -1;
}

// --- The constructors' second pages ---------------------------------------------------------------

void CSpellMakerPage::BuildSpellMaker(CParty* slots) {
    rect_ = RECT{200, 100, 600, 500};
    mode_ = 6;
    slots_ = slots;
    LoadTexture("constrspell1", 3);   // constrspell1
    LoadTexture("constrspell2", 3);   // constrspell2
    LoadTexture("constrspell3", 3);   // constrspell3
    LoadTexture("constrspell4", 3);   // constrspell4
    AddRect(this, RECT{200, 250, 300, 350});   // the container
    AddRect(this, RECT{300, 250, 400, 350});   // the spell
    BuildMakerPage(this);
    AddRect(this, RECT{200, 450, 400, 470});
    AddRect(this, RECT{200, 470, 400, 490});
    AddMakerPictures(this);
    AddSurface(new CUiSurface(interface_, 200, 450, 400, 490, 5, 1));
    pulse_ = 0.0f;
    CTextManager* texts = g_clientWorld->texts_;
    std::string text;
    for (int key : {0x23, 0x24}) {
        text = str::Format("infoitem_%d", key);
        texts->GetString(text.c_str(), &text);
        texts_.push_back(text);
    }
}

void CConstructorPage::BuildItemMaker(CParty* slots) {
    rect_ = RECT{200, 100, 600, 500};
    mode_ = 7;
    slots_ = slots;
    LoadTexture("constritem1", 3);   // constritem1
    LoadTexture("constritem2", 3);   // constritem2
    LoadTexture("constritem3", 3);   // constritem3
    LoadTexture("constritem4", 3);   // constritem4
    AddRect(this, RECT{200, 250, 300, 350});   // the blueprint
    AddRect(this, RECT{300, 200, 400, 300});   // the parts
    AddRect(this, RECT{300, 300, 400, 400});
    BuildMakerPage(this);
    AddMakerPictures(this);
    pulse_ = 0.0f;
}
