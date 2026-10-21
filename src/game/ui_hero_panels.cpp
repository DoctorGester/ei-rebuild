// The hero panels shared by the camp and the multiplayer hero screen: the statistics column
// (006233C0, 00623880). See docs/notes/ui.md ("Camp, global map, hero screens").
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float. Constants are read from memory.
#include "ui_camp.h"
#include "game_strings.h"

#include "console.h"
#include "item_db_text.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include "originals_ui.h"
#include "originals_units.h"
#include "unit.h"
#include "game_data.h"

namespace {

inline double D(float f) { return f; }

float GameFloat(std::uintptr_t address) { return ei::GameData<const volatile float>(address); }

template <typename T>
T Read(const std::uint8_t* p, int offset) {
    T v;
    std::memcpy(&v, p + offset, sizeof(T));
    return v;
}

// Unit statistics (UnitStats) values left in ST0 (not rewritten here).

struct SurfaceDraw {
    void* Begin() { return UiSurfaceGetDC(reinterpret_cast<CUiSurface*>(this)); }                                                                   // 005FDC00
    void Clear() { SurfaceClearOriginal(this); }                                                                   // 005FE2F0
    int Text(const char* text, RECT* rect, int a, std::uint32_t colour, int format, int b);   // 005FDE20 (Rebirth)
    void End() { UiSurfaceReleaseDC(reinterpret_cast<CUiSurface*>(this)); }                                                                     // 005FDC60
};
int SurfaceDraw::Text(const char* text, RECT* rect, int format, std::uint32_t colour, int a, int b) { return SurfaceDrawText(this, nullptr, text, rect, format, colour, a, b); }

void Down(RECT* rect) { gameapi::OffsetRect(rect, 0, 15); }

CUiRegion* AddRect(CUiWindow* window, const RECT& r) {
    CUiRegion* region = window->AddRegion();
    region->CallSetRect(r.left, r.top, r.right, r.bottom);
    return region;
}

void ShowFrom(CUiWindow* window, int first, std::uint32_t shown) {
    for (int i = first; i < window->sceneObjects_.size; ++i)
        window->SceneMesh(i)->visible_ = static_cast<int>(shown);
}

constexpr std::uint32_t kBright = 0xA7D7E4;
constexpr std::uint32_t kGrey = 0x828282;

} // namespace

// --- 006233C0 ----------------------------------------------------------------------------------

void CHeroStatsPanel::ShowStats(int page) {
    RECT rect = {0x14, 0xA5, 0xB4, 0xB4};
    page_ = page;
    while (regions_.size > 2) RemoveRegionAt(2);
    ShowFrom(this, 4, 0);
    if (page_ == 0) {
        // Equipment: four rows, a gap, three rows, the weapon's two lines.
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
        // A new hero: the three statistics with their - and + buttons, then the points.
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
EI_REPLACE_METHOD(0x006233C0, CHeroStatsPanel::ShowStats);

// --- 00623880 ----------------------------------------------------------------------------------

void CHeroStatsPanel::DrawStats() {
    SurfaceDraw* draw = reinterpret_cast<SurfaceDraw*>(surfaces_.data[0]);
    draw->Begin();
    draw->Clear();
    RECT title = {0, 0x1B, 0xC8, 0x2B};
    draw->Text(title_, &title, 1, 0xFFFFFF, 1, 1);

    using HeroFn = std::uint8_t*(__thiscall*)(CUiWindow*);
    using UnitFn = std::uint8_t*(__thiscall*)(CUiWindow*, int);
    auto hero = [this]() { return reinterpret_cast<HeroFn>(owner_->vtable_[51])(owner_); };   // +0xCC
    std::uint8_t* stats = hero() + 0x20;
    std::uint8_t* unit = reinterpret_cast<UnitFn>(owner_->vtable_[50])(owner_, heroIndex_);  // +0xC8
    mfc::CString line;
    mfc::CString text;
    CTextManager* texts = ei::GameData<CTextManager*>(0x007AF788);
    RECT rect;

    if (page_ == 10) {
        // A new hero's statistics: points left = 75 - (strength + dexterity + intelligence).
        int strength = static_cast<int>(gamecrt::Ftol(D(Read<float>(stats, 0x0C))));
        int dexterity = static_cast<int>(gamecrt::Ftol(D(Read<float>(stats, 0x14))));
        int intelligence = static_cast<int>(gamecrt::Ftol(D(Read<float>(stats, 0x1C))));
        int sum = intelligence + dexterity + strength;
        rect = RECT{0x14, 0x41, 0x82, 0x50};
        DrawRow(&rect, "Strength", strength, -1);
        Down(&rect);
        DrawRow(&rect, "Dexterity", dexterity, -1);
        Down(&rect);
        DrawRow(&rect, "Intelligence", intelligence, -1);
        Down(&rect);
        float sumF = static_cast<float>(static_cast<double>(sum));
        DrawRow(&rect, texts_[5], static_cast<int>(gamecrt::Ftol(D(GameFloat(0x00741F04)) - D(sumF))), -1);
        Down(&rect);
        Down(&rect);
        DrawRow(&rect, texts_[6], points_, -1);

        // "-": while above 15 (the integer compared before it is stored as float).
        rect = RECT{0x8C, 0x41, 0xA0, 0x50};
        double minimum = D(GameFloat(0x00741EFC));
        int values[3] = {strength, dexterity, intelligence};
        float storedStats[3];
        for (int i = 0; i < 3; ++i) {
            double value = static_cast<double>(values[i]);
            storedStats[i] = static_cast<float>(value);
            std::uint32_t colour = value > minimum ? kBright : kGrey;   // FCOMP; TEST AH,0x41
            draw->Text("-", &rect, 0, colour, 1, 1);
            Down(&rect);
        }
        Down(&rect);
        Down(&rect);
        draw->Text("-", &rect, 0, points_ <= 0 ? kGrey : kBright, 1, 1);

        // "+": while below 35 and points are left.
        int left = static_cast<int>(gamecrt::Ftol(D(GameFloat(0x00741F04)) - D(sumF)));
        rect = RECT{0xA0, 0x41, 0xB4, 0x50};
        double maximum = D(GameFloat(0x00741F00));
        for (int i = 0; i < 3; ++i) {
            bool below = !(D(storedStats[i]) >= maximum);   // FCOMP; TEST AH,1 (below or unordered)
            std::uint32_t colour = below && left > 0 ? kBright : kGrey;
            draw->Text("+", &rect, 0, colour, 1, 1);
            Down(&rect);
        }
        Down(&rect);
        Down(&rect);
        draw->Text("+", &rect, 0, points_ >= 100 ? kGrey : kBright, 1, 1);
    } else if (page_ == 0) {
        // Equipment: armour by body part, the weapon, the encumbrance.
        rect = RECT{0x14, 0x41, 0xAF, 0x50};
        texts->GetString("infoitem_25", &text);
        draw->Text(text, &rect, 0, kBright, 0, 1);
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
        if (Read<int>(hero(), 0x774) == -1) {
            for (int i = 0; i < 5; ++i) Down(&rect);
        } else {
            draw->Text(texts_[3], &rect, 0, kBright, 0, 1);
            Down(&rect);
            // The weapon's figures (the unit's slot 50 of +0x240 fills them).
            struct WeaponInfo {
                std::uint32_t unused_00;
                std::uint32_t damageKind;    // +0x04
                std::uint32_t unused_08;
                float damageMin;             // +0x0C
                float damageMax;             // +0x10
                std::uint32_t unused_14;
                int value18;                 // +0x18
                int value1C;                 // +0x1C
                std::uint8_t unused_20[0x20];
            } info;
            void* inner = Read<void*>(unit, 0x240);
            using InfoFn = void(__thiscall*)(void*, WeaponInfo*);
            reinterpret_cast<InfoFn>((*static_cast<void***>(inner))[50])(inner, &info);
            line.Format("     %s", static_cast<const char*>(texts_[0]));
            draw->Text(line, &rect, 0, kBright, 0, 1);
            line.Format("%d", info.value18);
            draw->Text(line, &rect, 0, kBright, 2, 1);
            Down(&rect);
            line.Format("     %s", static_cast<const char*>(texts_[1]));
            draw->Text(line, &rect, 0, kBright, 0, 1);
            line.Format("%d", info.value1C);
            draw->Text(line, &rect, 0, kBright, 2, 1);
            Down(&rect);
            if (info.damageKind < 7) {
                line.Format("damage_%d", info.damageKind);
                texts->GetString(line, &text);
            }
            line.Format("     %s %s", static_cast<const char*>(texts_[2]), static_cast<const char*>(text));
            draw->Text(line, &rect, 0, kBright, 0, 1);
            int high = static_cast<int>(gamecrt::Ftol(D(info.damageMax)));
            int low = static_cast<int>(gamecrt::Ftol(D(info.damageMin)));
            line.Format("%d-%d", low, high);
            draw->Text(line, &rect, 0, kBright, 2, 1);
            Down(&rect);
            Down(&rect);
        }
        int weight = static_cast<int>(gamecrt::Ftol(reinterpret_cast<const PartyUnitRecord*>(hero())->CarriedWeight()));
        int capacity = static_cast<int>(gamecrt::Ftol(D(Read<float>(stats, 0x54))));
        DrawRow(&rect, "Encumbrance", weight, capacity);   // Encumbrance
        Down(&rect);
        if (weight > capacity) draw->Text(texts_[4], &rect, 0, 0xFF, 2, 1);
        Down(&rect);
        draw->Text(texts_[7], &rect, 0, kBright, 0, 1);
    } else {
        rect = RECT{0x14, 0x41, 0xB4, 0x50};
        DrawRow(&rect, "Strength", static_cast<int>(gamecrt::Ftol(D(Read<float>(stats, 0x10)))), -1);   // Strength
        Down(&rect);
        DrawRow(&rect, "Dexterity", static_cast<int>(gamecrt::Ftol(D(Read<float>(stats, 0x18)))), -1);   // Dexterity
        Down(&rect);
        DrawRow(&rect, "Intelligence", static_cast<int>(gamecrt::Ftol(D(Read<float>(stats, 0x20)))), -1);   // Intelligence
        Down(&rect);
        Down(&rect);
        DrawRow(&rect, "Health", static_cast<int>(gamecrt::Ftol(reinterpret_cast<UnitStats*>(stats)->HitPointCurve())), -1);              // Health
        Down(&rect);
        DrawRow(&rect, "Mana", static_cast<int>(gamecrt::Ftol(reinterpret_cast<UnitStats*>(stats)->ManaCurve())), -1);            // Mana
        Down(&rect);
        DrawRow(&rect, "Actions", static_cast<int>(gamecrt::Ftol(D(Read<float>(stats, 0x30)))), -1);   // Actions
        Down(&rect);
        DrawRow(&rect, "Experience",
                  static_cast<int>(gamecrt::Ftol(D(Read<float>(stats, 0x700)) + D(Read<float>(stats, 0x04)))), -1);   // Experience
        Down(&rect);
        double carried;
        if (page_ == 11) {
            using WeightFn = double(__thiscall*)(CUiWindow*);
            carried = reinterpret_cast<WeightFn>(owner_->vtable_[53])(owner_);   // +0xD4
        } else {
            carried = reinterpret_cast<const PartyUnitRecord*>(hero())->CarriedWeight();
        }
        int weight = static_cast<int>(gamecrt::Ftol(carried));
        int capacity = static_cast<int>(gamecrt::Ftol(D(Read<float>(stats, 0x54))));
        DrawRow(&rect, "Encumbrance", weight, capacity);   // Encumbrance
        Down(&rect);
        if (weight > capacity) draw->Text(texts_[4], &rect, 0, 0xFF, 2, 1);
        Down(&rect);
    }
    draw->End();
}
EI_REPLACE_METHOD(0x00623880, CHeroStatsPanel::DrawStats);

// --- 00622000: the equipment panel's Build ---------------------------------------------------------

namespace {

// Sprite builders (CUiMesh): 005E8450 a framed rectangle (not patched), 005E7680 a strip of
// `count` framed cells (Rebirth-patched; called through its address).
struct SpriteBuild {
    void Framed(float depth, float scale, float left, float top, float right, float bottom, float border, int u0,
                int v0, int u1, int v1);   // 005E8450
    void Cells(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0, int u1,
               int v1, int count, int flag);   // 005E7680
};
void SpriteBuild::Framed(float a0, float a1, float a2, float a3, float a4, float a5, float a6, int a7, int a8, int a9, int a10) { UiMeshFramedOriginal(this, nullptr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10); }
void SpriteBuild::Cells(float a0, float a1, float a2, float a3, float a4, float a5, int a6, int a7, int a8, int a9, int a10, int a11) { UiMeshCellsOriginal((void*)this, nullptr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11); }
SpriteBuild* Builder(CUiMesh* sprite) { return reinterpret_cast<SpriteBuild*>(sprite); }

void SetPlainFlag(CUiMesh* sprite) { sprite->flags_ |= 0x100; }

// The 4 x 4 cell grid over 200..600 x 100..500 shared by the trade and repair grids (006270A0,
// 006279E0 inline it): a picture (textures 0-3 by quarter) and a frame per cell, a region per
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
            CEngineObject** textures = window->objects_.data;
            CEngineObject* texture = top <= 200 ? (left <= 300 ? textures[0] : textures[1])
                                                : (left <= 300 ? textures[2] : textures[3]);
            int right = left + 100;
            CUiMesh* picture = window->AddSprite(0);
            picture->SetRectangle(pictureDepth, 100.0f, static_cast<float>(left), static_cast<float>(top),
                                  static_cast<float>(right), static_cast<float>(bottom), u, v, u + 100, v + 100);
            picture->texture_ = texture;
            CUiMesh* frame = window->AddSprite(0);
            Builder(frame)->Framed(depth, 100.0f, static_cast<float>(left), static_cast<float>(top),
                                   static_cast<float>(right), static_cast<float>(bottom), 15.0f, u, v, u + 100, v + 100);
            frame->texture_ = texture;
            AddRect(window, RECT{left, top, right, bottom});
            u = 0x9C - u;
        }
        depth = static_cast<float>(D(depth) * D(GameFloat(0x0073B86C)));   // * 0.5
        v = 0x9C - v;
    }
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    window->AddSurface(memory ? static_cast<CUiSurface*>(memory)->Construct(window->interface_, 200, 100, 600, 500, 5, 1)
                              : nullptr);
}

} // namespace

void CHeroEquipPanel::Build(bool body) {
    body_ = body;
    rect_ = RECT{0, 0, 800, body ? 500 : 100};
    mode_ = 0;
    for (int x = 0; x < 800; x += 100) AddRect(this, RECT{x, 0, x + 100, 100});
    // Two hidden highlight frames.
    for (int i = 0; i < 2; ++i) {
        CUiMesh* frame = AddSprite(0);
        Builder(frame)->Framed(3.0f, 100.0f, -40.0f, -40.0f, 40.0f, 40.0f, 5.0f, 0, 0, 0x100, 0x100);
        SetPlainFlag(frame);
        frame->flatColour_ = 0xFF8B8B37;
        frame->texture_ = nullptr;
        frame->visible_ = 0;
    }
    // The slots' frames: four on the left half, four on the right, each half with its strip.
    float cell = GameFloat(0x0073BAB0);   // 100
    for (int i = 0; i < 4; ++i)
        Builder(AddSprite(0))->Framed(8.0f, 100.0f, static_cast<float>(static_cast<double>(i) * D(cell)), 0.0f,
                                      static_cast<float>(static_cast<double>(i + 1) * D(cell)), 100.0f, 15.0f, 0xE, 0xE, 0x72, 0x72);
    Builder(AddSprite(0))->Cells(16.0f, 100.0f, 0.0f, 0.0f, 400.0f, 100.0f, 0xE, 0xE, 0x72, 0x72, 4, 1);
    for (int i = 4; i < 8; ++i)
        Builder(AddSprite(0))->Framed(8.0f, 100.0f, static_cast<float>(static_cast<double>(i) * D(cell)), 0.0f,
                                      static_cast<float>(static_cast<double>(i + 1) * D(cell)), 100.0f, 15.0f, 0x8E, 0xE, 0xF2, 0x72);
    Builder(AddSprite(0))->Cells(16.0f, 100.0f, 400.0f, 0.0f, 800.0f, 100.0f, 0x8E, 0xE, 0xF2, 0x72, 4, 1);
    if (body_) {
        // The body's slots: 100x100 around the centres of two tables (00741EC4 x, 00741EE0 y).
        const int* xs = ei::GamePointer<const int>(0x00741EC4);
        const int* ys = ei::GamePointer<const int>(0x00741EE0);
        for (int i = 0; i < 7; ++i) AddRect(this, RECT{xs[i] - 50, ys[i] - 50, xs[i] + 50, ys[i] + 50});
        LoadTexture("camp1", 3, 0);   // camp1
        LoadTexture("camp2", 3, 0);   // camp2
        LoadTexture("camp3", 3, 0);   // camp3
        LoadTexture("camp4", 3, 0);   // camp4
        const float quarters[4][4] = {{200, 100, 400, 300}, {400, 100, 600, 300}, {200, 300, 400, 500}, {400, 300, 600, 500}};
        for (int i = 0; i < 4; ++i) {
            CUiMesh* picture = AddSprite(0);
            picture->SetRectangle(7.0f, 100.0f, quarters[i][0], quarters[i][1], quarters[i][2], quarters[i][3], 0x1C,
                                  0x1C, 0xE4, 0xE4);
            picture->texture_ = objects_.data[i + 1];
        }
    }
    selected_ = -1;
}
namespace {
void __fastcall EquipBuild(CHeroEquipPanel* self, void*, int body) { self->Build(static_cast<std::uint8_t>(body) != 0); }
} // namespace
EI_REPLACE(0x00622000, EquipBuild);

// --- 00622DB0: the statistics panel's Build (slot 18) ---------------------------------------------

void CHeroStatsPanel::Build() {
    rect_ = RECT{0, 100, 200, 400};
    AddRect(this, RECT{0, 0x7D, 0x1E, 0x91});      // previous hero
    AddRect(this, RECT{0xAA, 0x7D, 200, 0x91});    // next hero
    {
        void* memory = gamecrt::operator_new(sizeof(CUiSurface));
        AddSurface(memory ? static_cast<CUiSurface*>(memory)->Construct(interface_, 0, 100, 200, 400, 5, 1) : nullptr);
    }
    // The arrows and the hero's frame.
    AddSprite(0)->SetRectangle(5.0f, 100.0f, 176.0f, 125.0f, 192.0f, 145.0f, 2, 0x81, 0x12, 0x95);
    AddSprite(0)->SetRectangle(5.0f, 100.0f, 8.0f, 125.0f, 24.0f, 145.0f, 0x12, 0x81, 2, 0x95);
    AddSprite(0)->SetRectangle(6.0f, 100.0f, 14.0f, 110.0f, 100.0f, 160.0f, 0xE7, 0x4C, 0x91, 0x88);
    AddSprite(0)->SetRectangle(6.0f, 100.0f, 100.0f, 110.0f, 186.0f, 160.0f, 0x91, 0x4C, 0xE7, 0x88);
    // The new hero page's -/+ buttons (scene objects 4-11, shown only there).
    const float buttons[8][4] = {{140, 165, 160, 180}, {140, 180, 160, 195}, {140, 195, 160, 210}, {140, 240, 160, 255},
                                 {160, 165, 180, 180}, {160, 180, 180, 195}, {160, 195, 180, 210}, {160, 240, 180, 255}};
    for (const auto& b : buttons) AddSprite(0)->SetRectangle(5.0f, 100.0f, b[0], b[1], b[2], b[3], 0x90, 0xB1, 0xA4, 0xC0);
    // The texts the panel draws (CStringArray at +0x15C).
    CTextManager* texts = ei::GameData<CTextManager*>(0x007AF788);
    mfc::CString text;
    mfc::CStringArray* array = &texts_;
    for (const char* key : {"infounit_3", "infounit_4", "infounit_9", "infounit_14", "infounit_17", "lmp_points_left",
                            "lmp_pers_tall", "magic_skills"}) {
        texts->GetString(key, &text);
        array->SetAtGrow(array->GetSize(), text);
    }
    heldRegion_ = -1;
    holdTime_ = 0.0f;
}
EI_REPLACE_METHOD(0x00622DB0, CHeroStatsPanel::Build);

// --- 006270A0: the trade grid's Build ----------------------------------------------------------------

void CTradeGrid::Build(void* bought, void* sold, char spells, char flag) {
    bought_ = static_cast<UiItemList*>(bought);
    rect_ = RECT{200, 100, 600, 500};
    sold_ = static_cast<UiItemList*>(sold);
    buyKind_ = spells ? 0 : 4;
    sellKind_ = spells ? 1 : 5;
    flag_ = flag;
    LoadTexture("trade1", 3, 0);   // trade1
    LoadTexture("trade2", 3, 0);   // trade2
    LoadTexture("trade3", 3, 0);   // trade3
    LoadTexture("trade4", 3, 0);   // trade4
    BuildCellGrid(this);
}
EI_REPLACE_METHOD(0x006270A0, CTradeGrid::Build);

// --- 00627920 / 00628060: the item under the mouse (slot 25) -----------------------------------------

namespace {
constexpr int kGridCellsPerList = 8;
constexpr int kGridCells = 16;
constexpr int kTopicTraderBuy = 0x1C, kTopicTraderSell = 0x1D;   // flag_ set
constexpr int kTopicItemBuy = 0x0B, kTopicItemSell = 0x0C;       // the item constructor's kinds
constexpr int kTopicSpellBuy = 0x11, kTopicSpellSell = 0x12;     // the spell constructor's
constexpr int kTopicRepair = 0x19;
} // namespace

CItem* CTradeGrid::ItemAt(int x, int y, int* topic) {
    *topic = 0;
    if (!enabled_) return nullptr;
    int region = RegionAt(x, y);
    if (region >= 0 && region < kGridCellsPerList) {
        *topic = flag_ ? kTopicTraderBuy : buyKind_ ? kTopicItemBuy : kTopicSpellBuy;
        if (region < bought_->size) return bought_->data[region];
        return nullptr;
    }
    if (region < kGridCellsPerList || region >= kGridCells) return nullptr;
    // The sold list's topic also follows buyKind_.
    *topic = flag_ ? kTopicTraderSell : buyKind_ ? kTopicItemSell : kTopicSpellSell;
    if (region - kGridCellsPerList < sold_->size) return sold_->data[region - kGridCellsPerList];
    return nullptr;
}
EI_REPLACE_METHOD(0x00627920, CTradeGrid::ItemAt);

CItem* CRepairGrid::ItemAt(int x, int y, int* topic) {
    *topic = 0;
    if (!enabled_) return nullptr;
    int region = RegionAt(x, y);
    if (region < 0 || region >= kGridCells) return nullptr;
    *topic = kTopicRepair;
    return region < items_->size ? items_->data[region] : nullptr;
}
EI_REPLACE_METHOD(0x00628060, CRepairGrid::ItemAt);

// --- 006279E0: the repair grid's Build ---------------------------------------------------------------

void CRepairGrid::Build(void* items) {
    rect_ = RECT{200, 100, 600, 500};
    mode_ = 8;
    items_ = static_cast<UiItemList*>(items);
    LoadTexture("repair1", 3, 0);   // repair1
    LoadTexture("repair2", 3, 0);   // repair2
    LoadTexture("repair3", 3, 0);   // repair3
    LoadTexture("repair4", 3, 0);   // repair4
    BuildCellGrid(this);
}
EI_REPLACE_METHOD(0x006279E0, CRepairGrid::Build);

// --- 00622A80: the quick strip's Build -------------------------------------------------------------

void CHeroQuickStrip::Build(bool top) {
    y_ = top ? 0 : 500;
    rect_ = RECT{0, y_, 800, y_ + 100};
    mode_ = 0;
    for (int x = 0; x < 800; x += 100) AddRect(this, RECT{x, y_, x + 100, y_ + 100});
    CUiMesh* frame = AddSprite(0);
    Builder(frame)->Framed(3.0f, 100.0f, -40.0f, -40.0f, 40.0f, 40.0f, 5.0f, 0, 0, 0x100, 0x100);
    frame->flatColour_ = 0xFF8B8B37;
    SetPlainFlag(frame);
    frame->texture_ = nullptr;
    frame->visible_ = 0;
    Builder(AddSprite(0))->Cells(16.0f, 100.0f, 0.0f, static_cast<float>(y_), 800.0f, static_cast<float>(y_ + 100), 0xE,
                                 0x8E, 0x72, 0xF2, 8, 1);
    selected_ = -1;
}
namespace {
void __fastcall QuickStripBuild(CHeroQuickStrip* self, void*, int top) { self->Build(static_cast<std::uint8_t>(top) != 0); }
} // namespace
EI_REPLACE(0x00622A80, QuickStripBuild);

// --- 006280C0 / 00628CC0: the constructors' second pages ---------------------------------------------

namespace {
// The pages' common part: the 2 x 4 grid of regions over 400..600 x 100..500 (column by column),
// the four quarter pictures (textures 0-3) and a hidden highlight frame.
void BuildMakerPage(CConstructorPage* page) {
    for (int left = 400; left < 600; left += 100)
        for (int top = 100; top < 500; top += 100) AddRect(page, RECT{left, top, left + 100, top + 100});
}
void AddMakerPictures(CConstructorPage* page) {
    const float quarters[4][4] = {{200, 100, 400, 300}, {400, 100, 600, 300}, {200, 300, 400, 500}, {400, 300, 600, 500}};
    for (int i = 0; i < 4; ++i) {
        CUiMesh* picture = page->AddSprite(0);
        picture->SetRectangle(7.0f, 100.0f, quarters[i][0], quarters[i][1], quarters[i][2], quarters[i][3], 0x1C, 0x1C,
                              0xE4, 0xE4);
        picture->texture_ = page->objects_.data[i];
    }
    CUiMesh* frame = page->AddSprite(0);
    Builder(frame)->Framed(3.0f, 100.0f, -40.0f, -40.0f, 40.0f, 40.0f, 5.0f, 0, 0, 0x100, 0x100);
    frame->flatColour_ = 0x96FFFFFF;
    SetPlainFlag(frame);
    frame->texture_ = nullptr;
    frame->visible_ = 0;
}
} // namespace

void CConstructorPage::BuildSpellMaker(void* slots) {
    rect_ = RECT{200, 100, 600, 500};
    mode_ = 6;
    slots_ = slots;
    LoadTexture("constrspell1", 3, 0);   // constrspell1
    LoadTexture("constrspell2", 3, 0);   // constrspell2
    LoadTexture("constrspell3", 3, 0);   // constrspell3
    LoadTexture("constrspell4", 3, 0);   // constrspell4
    AddRect(this, RECT{200, 250, 300, 350});   // the container
    AddRect(this, RECT{300, 250, 400, 350});   // the spell
    BuildMakerPage(this);
    AddRect(this, RECT{200, 450, 400, 470});
    AddRect(this, RECT{200, 470, 400, 490});
    AddMakerPictures(this);
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    AddSurface(memory ? static_cast<CUiSurface*>(memory)->Construct(interface_, 200, 450, 400, 490, 5, 1) : nullptr);
    pulse_ = 0.0f;
    // The cost line's texts: "infoitem_35", "infoitem_36" (CStringArray at +0x158).
    CTextManager* texts = ei::GameData<CTextManager*>(0x007AF788);
    mfc::CStringArray* array = &static_cast<CSpellMakerPage*>(this)->texts_;
    mfc::CString text;
    for (int key : {0x23, 0x24}) {
        text.Format("infoitem_%d", key);
        texts->GetString(text, &text);
        array->SetAtGrow(array->GetSize(), text);
    }
}
EI_REPLACE_METHOD(0x006280C0, CConstructorPage::BuildSpellMaker);

void CConstructorPage::BuildItemMaker(void* slots) {
    rect_ = RECT{200, 100, 600, 500};
    mode_ = 7;
    slots_ = slots;
    LoadTexture("constritem1", 3, 0);   // constritem1
    LoadTexture("constritem2", 3, 0);   // constritem2
    LoadTexture("constritem3", 3, 0);   // constritem3
    LoadTexture("constritem4", 3, 0);   // constritem4
    AddRect(this, RECT{200, 250, 300, 350});   // the blueprint
    AddRect(this, RECT{300, 200, 400, 300});   // the parts
    AddRect(this, RECT{300, 300, 400, 400});
    BuildMakerPage(this);
    AddMakerPictures(this);
    pulse_ = 0.0f;
}
EI_REPLACE_METHOD(0x00628CC0, CConstructorPage::BuildItemMaker);
