// The camp trade grid's draw (CTradeGrid). The depth buffer is cleared to 0 over the screen and to 1 over the
// grid (200..600 x 100..500 in layout coordinates), so the cells' item models draw only there; then the
// window's sprites and models, and on its surface: every bought item and sold item in its cell of a 4 x 4
// layout (100 pixels apart; the item's model is drawn there), its count when more than one, and unless a flag
// its price (the owner's price virtual with the kind). Last the depth buffer is cleared to 1 again. The new
// renderer (src/render) gets the depth clears. See docs/notes/ui.md ("Camp") and docs/notes/renderer.md.
#include "item.h"
#include "game_strings.h"
#include "ui.h"
#include "texture.h"
#include "ui_camp.h"
#include "ui_item_info.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "render/render.h"

#include <cstdint>
#include "game_data.h"
#include "game_singletons.h"

namespace {


constexpr std::uint32_t kTextColour = 0xA7D7E4;



// IDirect3DDevice7::Clear of the depth buffer (and the stencil when the device has one) over
// `rect` to `z`, result in the renderer's +8; handed to the new renderer too.
void ClearDepth(CTextureDevice* renderer, RECT* rect, float z) {
    unsigned long flags = uidevice::HasStencil(renderer) ? 6ul : 2ul;
    gameapi::EnterCriticalSection(TheDeviceLock());
    renderer->lastLoadResult_ = uidevice::Clear(renderer, 1ul, rect, flags, 0ul, z, 0ul);
    gameapi::LeaveCriticalSection(TheDeviceLock());
    render::Rect nr{rect->left, rect->top, rect->right, rect->bottom};
    render::ClearDepth(z, &nr, 1);
}

// One list's items: cell i at column i % 4, row i / 4; `y0` and the sizes differ between the
// bought (upper) and sold (lower) lists, as do the text rows.
void DrawItems(CTradeGrid* grid, CSceneRenderer* pass, UiItemList* list, CUiSurface* surface, mfc::CString& text, float yOffset,
               float bigSize, float smallSize, int countTop, int priceTop, int kind) {
    CItem** items = list->data;
    int count = list->size;
    for (int i = 0; i < count; ++i) {
        int x = (i % 4) * 100;
        int y = (i / 4) * 100;
        ItemCell cell{items[i], static_cast<float>(x) + 250.0f /*0073BF48*/, static_cast<float>(y) + yOffset,
                      i <= 3 ? bigSize : smallSize, 100.0f, grid->mode_};
        grid->owner_->DrawItem(pass, &cell);
        int pieces = static_cast<int>(cell.item->count_);
        if (pieces > 1) {
            text.Format("%d", pieces);
            RECT r{x + 15, y + countTop, x + 0x55, y + countTop + 20};
            surface->DrawLine(text, &r, 1, kTextColour, 2, 1);
        }
        if (!grid->flag_) {
            int price = static_cast<CCampPages*>(grid->owner_)->Price(cell.item, kind);   // the camp
            text.Format("%d", price);
            RECT r{x + 15, y + priceTop, x + 0x55, y + priceTop + 20};
            surface->DrawLine(text, &r, 1, kTextColour, 1, 1);
        }
    }
}

} // namespace

// Returns 1.
int CTradeGrid::DrawGrid(CSceneRenderer* pass) {
    CInterface3D* ui = interface_;
    RECT grid{200, 100, 600, 500};
    ui->ScaleRectToScreenEntry(&grid);
    RECT screen{0, 0, ui->width_, ui->height_};
    ClearDepth(ui->renderer_, &screen, 0.0f);
    RECT inside = grid;
    ClearDepth(ui->renderer_, &inside, 1.0f);
    DrawBase(pass);

    mfc::CString text;
    auto* surface = surfaces_.data[0];
    surface->GetDC();
    surface->Clear();
    DrawItems(this, pass, bought_, surface, text, 150.0f /*0073D618*/, 48.0f, 24.0f, 0x0F, 0x41,
              buyKind_);
    DrawItems(this, pass, sold_, surface, text, 350.0f /*00741F18*/, 12.0f, 6.0f, 0xD7, 0x109,
              sellKind_);
    surface->ReleaseDC();
    ui = interface_;
    RECT whole{0, 0, ui->width_, ui->height_};
    ClearDepth(ui->renderer_, &whole, 1.0f);
    return 1;
}
EI_REWRITTEN(0x00627390, CTradeGrid::DrawGrid);
