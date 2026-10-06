// Drawing slots (20) of the camp's and the hero screen's panels: the quick strip 00622C10, the
// repair grid 00627C90. See docs/notes/ui.md, section 5. The trade grid's (00627390) is in
// ui_trade_grid_draw.cpp.
#include "ui_camp.h"
#include "ui_item_info.h"
#include "item.h"
#include "unit.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_ui.h"
#include "render/render.h"

#include <cstdint>

namespace {

// The items of a camp item list (its +0x40 part; the list is 0x70 bytes, 00661050).
struct CampItemList {
    void** first;                    // +0x00
    int count;                       // +0x04
};

constexpr std::uint32_t kTextColour = 0xA7D7E4;
constexpr int kD3DClear = 10;                    // IDirect3DDevice7::Clear
constexpr std::uint32_t kClearZBuffer = 2;       // D3DCLEAR_ZBUFFER
constexpr std::uint32_t kClearStencil = 4;       // D3DCLEAR_STENCIL
constexpr std::uint32_t kCapsStencil = 0x4000;

const char* NumberFormat() { return reinterpret_cast<const char*>(0x0078EEEC); }   // "%d"
CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// 005ECA10 (rewritten, ui_item_view.cpp; through its address so its wrapper runs): the owner screen
// draws an item's model in a cell (RET 8).
struct ItemModelDraw {
    void DrawItem(int pass, ItemCell* cell);
};
__declspec(naked) void ItemModelDraw::DrawItem(int, ItemCell*) EI_ORIGINAL(0x005ECA10)
void DrawItem(CUiWindow* owner, int pass, ItemCell* cell) { reinterpret_cast<ItemModelDraw*>(owner)->DrawItem(pass, cell); }

// The window's surface text (005FDE20 Rebirth-patched: through its address).
struct SurfaceText {
    void Begin() { SurfaceGetDC(this); }
    void Clear() { SurfaceClearOriginal(this); }
    void Draw(const char* text, RECT* rect, int format, int align) {
        SurfaceDrawText(this, nullptr, text, rect, format, kTextColour, align, 1);
    }
    void End() { SurfaceReleaseDC(this); }
};

// The depth buffer (and the stencil when the device has one) cleared over `rect` to `z` under the
// device lock, as the original inlines it; the new renderer gets the clear too.
void ClearDepth(UiRenderer* renderer, RECT* rect, float z) {
    std::uint32_t flags = (renderer->caps_ & kCapsStencil) && renderer->stencil_ ? kClearZBuffer | kClearStencil
                                                                                    : kClearZBuffer;
    using ClearFn = long(__stdcall*)(UiD3DDevice*, std::uint32_t, RECT*, std::uint32_t, std::uint32_t, float,
                                     std::uint32_t);
    gameapi::EnterCriticalSection(DeviceLock());
    renderer->result_ = reinterpret_cast<ClearFn>(renderer->device_->vtable_[kD3DClear])(renderer->device_, 1, rect,
                                                                                        flags, 0, z, 0);
    gameapi::LeaveCriticalSection(DeviceLock());
    render::Rect r{rect->left, rect->top, rect->right, rect->bottom};
    render::ClearDepth(z, &r, 1);
}

void FlushBatches(CInterface3D* ui) {
    ui->renderer_->FlushBatchA();
    ui->renderer_->FlushBatchB();
}

} // namespace

// --- 00622C10: the quick strip ------------------------------------------------------------------

BOOL CHeroQuickStrip::Draw(int pass) {
    PartyUnitRecord* hero = ShownHero(owner_);
    auto* frame = static_cast<CUiMesh*>(static_cast<void*>(sceneObjects_.data_[0]));
    int count = static_cast<int>(hero->spellCount);
    if (selected_ >= 0 && selected_ < count) {
        frame->visible_ = 1;
        frame->Place(static_cast<float>(selected_ * 100 + 50), static_cast<float>(y_ + 50), 3.0f);
    } else {
        frame->visible_ = 0;
    }
    for (int i = 0; i < static_cast<int>(hero->spellCount); ++i) {
        ItemCell cell{reinterpret_cast<CItem*>(hero->spells[i]), static_cast<float>(i * 100 + 50),
                      static_cast<float>(y_ + 50), 12.0f, 100.0f, unknown_148};
        DrawItem(owner_, pass, &cell);
    }
    return DrawBase(pass);
}
namespace {
BOOL __fastcall QuickStripDraw(CHeroQuickStrip* self, void*, int pass) { return self->Draw(pass); }
} // namespace
EI_REPLACE(0x00622C10, QuickStripDraw);

// --- 00627C90: the repair grid ------------------------------------------------------------------

BOOL CRepairGrid::Draw(int pass) {
    CInterface3D* ui = interface_;
    FlushBatches(ui);
    RECT grid{200, 100, 600, 500};
    ui->CallScaleRectToScreen(&grid);
    RECT screen{0, 0, ui->width_, ui->height_};
    ClearDepth(ui->renderer_, &screen, 0.0f);
    RECT inside = grid;
    ClearDepth(ui->renderer_, &inside, 1.0f);

    mfc::CString text;
    auto* surface = reinterpret_cast<SurfaceText*>(surfaces_.data_[0]);
    surface->Begin();
    surface->Clear();
    auto* list = static_cast<CampItemList*>(items_);
    for (int i = 0; i < list->count; ++i) {
        int x = (i % 4) * 100;
        int y = (i / 4) * 100;
        // 1 << (3 - row) as SHL does it (the count masked to five bits).
        auto scale = static_cast<int>(1u << ((3 - (i >> 2)) & 31));
        auto* item = static_cast<CItem*>(list->first[i]);
        ItemCell cell{item, static_cast<float>(x) + 250.0f, static_cast<float>(y) + 150.0f,
                      static_cast<float>(scale) * 6.0f, 100.0f, unknown_148};
        DrawItem(owner_, pass, &cell);
        int pieces = static_cast<int>(item->unknown_48);   // the stack's count
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
    FlushBatches(ui);
    RECT whole{0, 0, ui->width_, ui->height_};
    ClearDepth(ui->renderer_, &whole, 1.0f);
    return 1;
}
namespace {
BOOL __fastcall RepairGridDraw(CRepairGrid* self, void*, int pass) { return self->Draw(pass); }
} // namespace
EI_REPLACE(0x00627C90, RepairGridDraw);
