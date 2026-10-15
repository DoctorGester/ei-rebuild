// Checks for the camp trade grid's draw (ui_trade_grid_draw.cpp).
#include "verify.h"

#include "../../src/game/ui_camp.h"


// 00627390 draws (Direct3D clears, models, surface text): ours only. Checked: the result is 1, the
// last Direct3D result (+8 of the renderer, the final depth clear) is a success, and the item lists
// are unchanged.
static void* s_tradeGridDraw;
static int __fastcall TradeGridDrawVerify(CTradeGrid* self, void*, int pass) {
    void* bought = self->bought_;
    void* sold = self->sold_;
    int result = self->DrawGrid(pass);
    long hr = *reinterpret_cast<long*>(reinterpret_cast<unsigned char*>(self->interface_->renderer_) + 8);
    bool ok = result == 1 && hr >= 0 && self->bought_ == bought && self->sold_ == sold;
    ei::verify::Check("CTradeGrid::Draw", true, ok, "result %d, clear %08lX", result, static_cast<unsigned long>(hr));
    return result;
}
EI_VERIFY(0x00627390, TradeGridDrawVerify, 0, s_tradeGridDraw);
