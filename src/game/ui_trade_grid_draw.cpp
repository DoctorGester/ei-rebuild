// The camp trade grid's draw (CTradeGrid). The depth buffer is cleared to 0 over the screen and to 1 over the
// grid (200..600 x 100..500 in layout coordinates), so the cells' item models draw only there; then the
// window's sprites and models, and on its surface: every bought item and sold item in its cell of a 4 x 4
// layout (100 pixels apart; the item's model is drawn there), its count when more than one, and except on
// the exchange window its price (the owner's price virtual with the kind). Last the depth buffer is cleared
// to 1 again. The new renderer (src/render) gets the depth clears. See docs/notes/ui.md ("Camp") and
// docs/notes/renderer.md.
#include "item.h"
#include "ui.h"
#include "texture.h"
#include "ui_camp.h"
#include "ui_item_info.h"

#include "lib/gameapi.h"
#include <cstdint>
#include <string>
#include "render/render.h"

#include "game_singletons.h"
#include "lib/str.h"

namespace {

constexpr std::uint32_t kTextColour = 0xA7D7E4;

// One list's items: cell i at column i % 4, row i / 4; `yOffset` and the sizes differ between the
// bought (upper) and sold (lower) lists, as do the text rows.
void DrawItems(CTradeGrid* grid, CSceneRenderer* pass, const std::vector<CItem*>& list, CUiSurface* surface,
               float yOffset, float bigSize, float smallSize, int countTop, int priceTop, int kind) {
    for (int i = 0; i < std::ssize(list); ++i) {
        int x = (i % 4) * 100;
        int y = (i / 4) * 100;
        ItemCell cell{list[i], static_cast<float>(x) + 250.0f, static_cast<float>(y) + yOffset,
                      i <= 3 ? bigSize : smallSize, 100.0f, grid->mode_};
        grid->owner_->DrawItem(pass, cell);
        int pieces = static_cast<int>(cell.item->count_);
        if (pieces > 1) {
            RECT r{x + 15, y + countTop, x + 0x55, y + countTop + 20};
            surface->DrawTextIn(str::Format("%d", pieces), &r, 1, kTextColour, 2, 1);
        }
        if (!grid->exchange_) {
            RECT r{x + 15, y + priceTop, x + 0x55, y + priceTop + 20};
            surface->DrawTextIn(str::Format("%d", grid->owner_->Price(cell.item, kind)), &r, 1, kTextColour, 1, 1);
        }
    }
}

} // namespace

void CTradeGrid::Draw(CSceneRenderer* pass) {
    CInterface3D* ui = interface_;
    RECT grid{200, 100, 600, 500};
    ui->ScaleRectToScreenEntry(&grid);
    RECT screen{0, 0, ui->width_, ui->height_};
    ClearUiDepth(0.0f, &screen, 1);
    ClearUiDepth(1.0f, &grid, 1);
    DrawBase(pass);

    CUiSurface* surface = surfaces_[0];
    surface->Clear();
    DrawItems(this, pass, *bought_, surface, 150.0f, 48.0f, 24.0f, 0x0F, 0x41, buyKind_);
    DrawItems(this, pass, *sold_, surface, 350.0f, 12.0f, 6.0f, 0xD7, 0x109, sellKind_);
    ClearUiDepth(1.0f, &screen, 1);
}
