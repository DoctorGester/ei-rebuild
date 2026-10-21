// Drawing slots (20) of the camp's and the hero screen's panels: the quick strip 00622C10, the
// repair grid 00627C90, the constructors' spell maker 006284A0 and item maker 00628F80. See docs/notes/ui.md, section 5. The trade grid's (00627390) is in
// ui_trade_grid_draw.cpp.
#include "ui_camp.h"
#include "ui_item_info.h"
#include "item.h"
#include "item_db.h"
#include "unit.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_ui.h"
#include "render/render.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cmath>
#include <cstdint>

namespace {

constexpr std::uint32_t kTextColour = 0xA7D7E4;
constexpr std::uint32_t kClearZBuffer = 2;       // D3DCLEAR_ZBUFFER
constexpr std::uint32_t kClearStencil = 4;       // D3DCLEAR_STENCIL
constexpr std::uint32_t kCapsStencil = 0x4000;

const char* NumberFormat() { return "%d"; }
const char* CostFormat() { return "%s %d (%d)"; }

// 005ECA10 (rewritten, ui_item_view.cpp): the owner screen draws
// an item's model in a cell (RET 8).
void DrawItem(CUiWindow* owner, int pass, ItemCell* cell) { reinterpret_cast<CItemScreen*>(owner)->DrawItem(pass, cell); }

// The window's surface text (005FDE20 Rebirth-patched: through its address).
struct SurfaceText {
    void Begin() { UiSurfaceGetDC(reinterpret_cast<CUiSurface*>(this)); }
    void Clear() { SurfaceClearOriginal(this); }
    void Draw(const char* text, RECT* rect, int format, int align) {
        SurfaceDrawText(this, nullptr, text, rect, format, kTextColour, align, 1);
    }
    void End() { UiSurfaceReleaseDC(reinterpret_cast<CUiSurface*>(this)); }
};

// The depth buffer (and the stencil when the device has one) cleared over `rect` (null: all of
// it) to `z` under the device lock, as the original inlines it; the new renderer gets the clear too.
void ClearDepth(UiRenderer* renderer, RECT* rect, float z) {
    std::uint32_t flags = (renderer->caps_ & kCapsStencil) && renderer->stencil_ ? kClearZBuffer | kClearStencil
                                                                                    : kClearZBuffer;
    gameapi::EnterCriticalSection(TheDeviceLock());
    renderer->result_ = renderer->device_->Clear(rect ? 1 : 0, rect, flags, 0, z, 0);
    gameapi::LeaveCriticalSection(TheDeviceLock());
    if (!rect) {
        render::ClearDepth(z, nullptr, 0);
        return;
    }
    render::Rect r{rect->left, rect->top, rect->right, rect->bottom};
    render::ClearDepth(z, &r, 1);
}

} // namespace

// --- 00622C10: the quick strip ------------------------------------------------------------------

BOOL CHeroQuickStrip::Draw(int pass) {
    PartyUnitRecord* hero = ShownHero(owner_);
    auto* frame = static_cast<CUiMesh*>(static_cast<void*>(sceneObjects_.data[0]));
    int count = static_cast<int>(hero->spellCount);
    if (selected_ >= 0 && selected_ < count) {
        frame->visible_ = 1;
        frame->Place(static_cast<float>(selected_ * 100 + 50), static_cast<float>(y_ + 50), 3.0f);
    } else {
        frame->visible_ = 0;
    }
    for (int i = 0; i < static_cast<int>(hero->spellCount); ++i) {
        ItemCell cell{reinterpret_cast<CItem*>(hero->spells[i]), static_cast<float>(i * 100 + 50),
                      static_cast<float>(y_ + 50), 12.0f, 100.0f, mode_};
        DrawItem(owner_, pass, &cell);
    }
    return DrawBase(pass);
}
EI_REPLACE_METHOD(0x00622C10, CHeroQuickStrip::Draw);

// --- 00627C90: the repair grid ------------------------------------------------------------------

BOOL CRepairGrid::Draw(int pass) {
    CInterface3D* ui = interface_;
    RECT grid{200, 100, 600, 500};
    ui->CallScaleRectToScreen(&grid);
    RECT screen{0, 0, ui->width_, ui->height_};
    ClearDepth(ui->renderer_, &screen, 0.0f);
    RECT inside = grid;
    ClearDepth(ui->renderer_, &inside, 1.0f);

    mfc::CString text;
    auto* surface = reinterpret_cast<SurfaceText*>(surfaces_.data[0]);
    surface->Begin();
    surface->Clear();
    const UiItemList* list = items_;
    for (int i = 0; i < list->size; ++i) {
        int x = (i % 4) * 100;
        int y = (i / 4) * 100;
        // 1 << (3 - row) as SHL does it (the count masked to five bits).
        auto scale = static_cast<int>(1u << ((3 - (i >> 2)) & 31));
        CItem* item = list->data[i];
        ItemCell cell{item, static_cast<float>(x) + 250.0f, static_cast<float>(y) + 150.0f,
                      static_cast<float>(scale) * 6.0f, 100.0f, mode_};
        DrawItem(owner_, pass, &cell);
        int pieces = static_cast<int>(item->count_);   // the stack's count
        if (pieces > 1) {
            text.Format(NumberFormat(), pieces);
            RECT r{x + 0x0F, y + 0x0F, x + 0x55, y + 0x23};
            surface->Draw(text, &r, 1, 2);
        }
        using PriceFn = int(__thiscall*)(CUiWindow*, CItem*, int);
        int price = reinterpret_cast<PriceFn>(owner_->vtable_[54])(owner_, item, 8);
        text.Format(NumberFormat(), price);
        RECT r{x + 0x0F, y + 0x41, x + 0x55, y + 0x55};
        surface->Draw(text, &r, 1, 1);
    }
    surface->End();
    DrawBase(pass);
    ui = interface_;
    RECT whole{0, 0, ui->width_, ui->height_};
    ClearDepth(ui->renderer_, &whole, 1.0f);
    return 1;
}
EI_REPLACE_METHOD(0x00627C90, CRepairGrid::Draw);

// --- 006284A0 / 00628F80: the constructors' second pages ----------------------------------------

namespace {

int Channel(int value) { return value > 0xFF ? 0xFF : value < 0 ? 0 : value; }

std::uint32_t Colour(int alpha, int red, int blue) {
    return static_cast<std::uint32_t>(((Channel(alpha) << 8 | Channel(red)) << 8 | Channel(red)) << 8 | Channel(blue));
}

// The highlight's pulse: the page's time advanced by the frame time (stored as float, the sum kept
// unrounded), (cos(5 t) + 1) / 2 stored as float; then the colour's three values, truncated: the
// alpha 130 (1 - p) + 240 p, `low` 80 (1 - p) and `high` 160 p + 80 (1 - p).
struct Pulse {
    int alpha;
    int low;
    int high;
};
Pulse Advance(CConstructorPage* page) {
    double t = WindowFrameTimeOriginal(page) + page->pulse_;
    page->pulse_ = static_cast<float>(t);
    float p = static_cast<float>((x87::fcos(t * 5.0) + 1.0) * 0.5);
    double a = 1.0 - p;
    double low = a * 80.0;
    return Pulse{static_cast<int>(a * 130.0 + p * 240.0), static_cast<int>(low), static_cast<int>(p * 160.0 + low)};
}

bool IsSpellContainer(const CItem* item) { return static_cast<std::uint8_t>(UiItemKinds::IsSpellContainer(item)) != 0; }

void DrawSprite(CUiMesh* sprite, int pass, float x, float y) {
    sprite->Place(x, y, 3.0f);
    sprite->Draw(reinterpret_cast<CSceneRenderer*>(static_cast<std::intptr_t>(pass)));
}

CUiMesh* Highlight(CUiWindow* window) { return static_cast<CUiMesh*>(static_cast<void*>(window->sceneObjects_.data[4])); }

struct SpellSlots {
    std::uint8_t unused_00[0x40];
    CItem** items;                   // +0x40
    int count;                       // +0x44
};

} // namespace

BOOL CConstructorPage::DrawSpellMaker(int pass) {
    auto* self = static_cast<CSpellMakerPage*>(this);
    auto* slots = static_cast<SpellSlots*>(slots_);
    int needed = 0;
    float manaSum = 0.0f;
    float lastMana = 0.0f;
    int school = 0;
    bool hasContainer = false;
    bool hasSpell = false;
    int row = 0;
    int column = 0;
    for (int i = 0; i < slots->count; ++i) {
        CItem* item = slots->items[i];
        ItemCell cell{item, 0.0f, 0.0f, 6.0f, 100.0f, mode_};
        if (IsSpellContainer(item)) {
            cell.x = 250.0f;
            cell.y = 300.0f;
            DrawItem(owner_, pass, &cell);
            hasContainer = true;
        } else if (UiItemKinds::IsSpell(item)) {
            // A record out of range is read through null, as in the original.
            auto* spell = reinterpret_cast<const itemdb::SpellRecord*>(
                itemdb::Spells().RecordAt(reinterpret_cast<const CItemLoot*>(item)->index_, itemdb::kSpellSize));
            needed += spell->skill;
            manaSum = static_cast<float>(static_cast<double>(manaSum) + spell->mana);
            lastMana = spell->mana;
            school = spell->school;
            cell.x = 350.0f;
            cell.y = 300.0f;
            DrawItem(owner_, pass, &cell);
            hasSpell = true;
        } else if (UiItemKinds::IsModifier(item)) {
            auto* modifier = reinterpret_cast<const itemdb::SpellModifierRecord*>(itemdb::SpellModifiers().RecordAt(
                reinterpret_cast<const CItemLoot*>(item)->index_, itemdb::kSpellModifierSize));
            manaSum = static_cast<float>(static_cast<double>(manaSum) + modifier->mana);
            needed += modifier->skill;
            cell.x = static_cast<float>(column) + 450.0f;
            cell.y = static_cast<float>(row * 100) + 150.0f;
            DrawItem(owner_, pass, &cell);
            if (++row > 3) {
                row = 0;
                column += 100;
            }
        }
    }
    int skill = 0;
    int mana = 0;
    if (hasSpell) {
        using BestFn = void(__thiscall*)(CUiWindow*, int, int*, int*);   // slot 45: the party's best
        reinterpret_cast<BestFn>(owner_->vtable_[45])(owner_, school, &skill, &mana);
    }
    auto* surface = reinterpret_cast<SurfaceText*>(surfaces_.data[0]);
    surface->Begin();
    surface->Clear();
    if (hasSpell) {
        mfc::CString text;
        text.Format(CostFormat(), static_cast<const char*>(self->texts_[0]), skill, needed);
        RECT first{10, 0, 200, 20};
        surface->Draw(text, &first, 1, 0);
        // The larger of the two (the last spell's when the sum is smaller or unordered).
        float shown = manaSum >= lastMana ? manaSum : lastMana;
        text.Format(CostFormat(), static_cast<const char*>(self->texts_[1]), mana, static_cast<int>(shown));
        RECT second{10, 20, 200, 40};
        surface->Draw(text, &second, 1, 0);
    } else {
        RECT first{10, 0, 200, 20};
        surface->Draw(self->texts_[0], &first, 1, 0);
        RECT second{10, 20, 200, 40};
        surface->Draw(self->texts_[1], &second, 1, 0);
    }
    surface->End();
    DrawBase(pass);

    CUiMesh* highlight = Highlight(this);
    highlight->visible_ = 1;
    Pulse pulse = Advance(this);
    if (!hasContainer) {
        if (hasSpell) {
            highlight->flatColour_ = Colour(pulse.alpha, pulse.low, pulse.high);
            for (int x = 450; x < 650; x += 100)
                for (int y = 150; y < 550; y += 100) DrawSprite(highlight, pass, static_cast<float>(x), static_cast<float>(y));
        } else {
            highlight->flatColour_ = Colour(pulse.alpha, pulse.high, pulse.low);
            DrawSprite(highlight, pass, 250.0f, 300.0f);
            DrawSprite(highlight, pass, 350.0f, 300.0f);
        }
    }
    highlight->visible_ = 0;
    return 1;
}
EI_REPLACE_METHOD(0x006284A0, CConstructorPage::DrawSpellMaker);

namespace {
// A slot's item drawn between depth clears: the screen to 0, the slot (layout `slot`) to 1, the
// model, then the screen to 1 again.
void DrawInSlot(CConstructorPage* page, int pass, ItemCell* cell, RECT slot) {
    CInterface3D* ui = page->interface_;
    ui->CallScaleRectToScreen(&slot);
    RECT inside = slot;
    ClearDepth(ui->renderer_, nullptr, 0.0f);
    ClearDepth(ui->renderer_, &inside, 1.0f);
    DrawItem(page->owner_, pass, cell);
    ClearDepth(page->interface_->renderer_, nullptr, 1.0f);
}

// A part's prototype by its record type (weapons, armour, quick items), or null.
const itemdb::ItemRecordHead* PartRecord(const CItemLoot* part) {
    switch (part->recordType_) {
    case 0: return reinterpret_cast<const itemdb::ItemRecordHead*>(itemdb::Weapons().RecordAt(part->index_, itemdb::kWeaponSize));
    case 1: return reinterpret_cast<const itemdb::ItemRecordHead*>(itemdb::Armors().RecordAt(part->index_, itemdb::kArmorSize));
    case 2:
        return reinterpret_cast<const itemdb::ItemRecordHead*>(itemdb::QuickItems().RecordAt(part->index_, itemdb::kQuickItemSize));
    default: return nullptr;
    }
}
} // namespace

BOOL CConstructorPage::DrawItemMaker(int pass) {
    DrawBase(pass);
    auto* slots = static_cast<SpellSlots*>(slots_);
    bool hasBlueprint = false;   // or a spell container
    bool hasPart = false;
    const itemdb::ItemRecordHead* part = nullptr;
    int materials = 0;
    int row = 0;
    int column = 0;
    for (int i = 0; i < slots->count; ++i) {
        CItem* item = slots->items[i];
        ItemCell cell{item, 0.0f, 0.0f, 6.0f, 100.0f, mode_};
        if (UiItemKinds::IsBuildable(item)) {
            cell.x = 250.0f;
            cell.y = 300.0f;
            DrawInSlot(this, pass, &cell, RECT{215, 265, 285, 335});
            hasBlueprint = true;
        } else if (UiItemKinds::IsPart(item)) {
            cell.x = 350.0f;
            cell.y = 250.0f;
            DrawInSlot(this, pass, &cell, RECT{315, 215, 385, 285});
            hasPart = true;
            // Another record type keeps the part found before.
            auto* loot = reinterpret_cast<const CItemLoot*>(item);
            if (loot->recordType_ <= 2) part = PartRecord(loot);
        } else if (IsSpellContainer(item)) {
            cell.x = 350.0f;
            cell.y = 350.0f;
            DrawItem(owner_, pass, &cell);
            hasBlueprint = true;
        } else if (UiItemKinds::IsKindOne(item)) {
            ++materials;
            cell.x = static_cast<float>(column) + 450.0f;
            cell.y = static_cast<float>(row * 100) + 150.0f;
            DrawItem(owner_, pass, &cell);
            if (++row > 3) {
                row = 0;
                column += 100;
            }
        }
    }
    CUiMesh* highlight = Highlight(this);
    highlight->visible_ = 1;
    Pulse pulse = Advance(this);
    highlight->flatColour_ = Colour(pulse.alpha, pulse.high, pulse.low);
    if (!hasBlueprint) {
        if (!hasPart) {
            DrawSprite(highlight, pass, 250.0f, 300.0f);
            DrawSprite(highlight, pass, 350.0f, 250.0f);
        } else if (part) {
            // The grid cells the part has room for (+0x4C) beyond the materials in it.
            int room = static_cast<int>(part->materialUnits);
            int cellIndex = 0;
            bool none = true;
            for (int x = 450; x < 650; x += 100) {
                for (int y = 150; y < 550; y += 100) {
                    ++cellIndex;
                    if (cellIndex > materials && cellIndex <= room) {
                        DrawSprite(highlight, pass, static_cast<float>(x), static_cast<float>(y));
                        none = false;
                    }
                }
            }
            if (none) {
                // Full: the container slot, with the colour's values the other way round.
                highlight->flatColour_ = Colour(pulse.alpha, pulse.low, pulse.high);
                DrawSprite(highlight, pass, 350.0f, 350.0f);
            }
        }
    }
    highlight->visible_ = 0;
    return 1;
}
EI_REPLACE_METHOD(0x00628F80, CConstructorPage::DrawItemMaker);