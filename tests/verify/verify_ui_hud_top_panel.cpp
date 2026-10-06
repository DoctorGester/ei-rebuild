// Checks for the top panel's setup and page 0 lines (ui_hud_top_panel.cpp). All ours only: Build
// creates regions and sprites, the line adders append to the page text and the message log.
// - Build (00649AF0): every battle screen setup (newgame): rectangle 180..620 x 0..100, four more
//   regions, eight more sprites, the page and the slide state as `shown` says.
// - AddPickedUp (0064A4E0) / AddMoney (0064A630): from the battle screen's OnItemsPicked (00636150,
//   no scenario known): page 0's text grew by one line ending in "\n".
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/lib/mfc.h"

#include <cstring>

namespace {
void* s_unused;

int Length(const char* text) { return static_cast<int>(std::strlen(text)); }
bool EndsWithNewline(const char* text) {
    int n = Length(text);
    return n > 0 && text[n - 1] == '\n';
}
} // namespace

static void __fastcall TopPanelBuildVerify(CHudTopPanel* self, void*, int page, int shown) {
    const int regions = self->regions_.size_;
    const int sprites = self->sceneObjects_.size_;
    self->CHudTopPanel::Build(page, shown);
    const bool in = static_cast<char>(shown) != 0;
    const bool ok = self->rect_.left == 0xB4 && self->rect_.top == 0 && self->rect_.right == 0x26C &&
                    self->rect_.bottom == 100 && self->regions_.size_ == regions + 4 &&
                    self->sceneObjects_.size_ == sprites + 8 && self->page_ == page && self->sliding_ == 1 &&
                    self->travel_ == 0x5F && self->slideDirection_ == (in ? 1.0f : -1.0f);
    ei::verify::Check("CHudTopPanel::Build", true, ok, "page %d shown %d regions %d sprites %d", page, shown,
                      self->regions_.size_, self->sceneObjects_.size_);
}
EI_VERIFY(0x00649AF0, TopPanelBuildVerify, 0, s_unused);

static void __fastcall TopPanelPickedUpVerify(CHudTopPanel* self, void*, CItem* item, int crippled) {
    const int before = Length(self->PageText(0));
    self->CHudTopPanel::AddPickedUp(item, crippled);
    const char* text = self->PageText(0);
    const bool ok = Length(text) > before + 1 && EndsWithNewline(text);
    ei::verify::Check("CHudTopPanel::AddPickedUp", true, ok, "page text %d -> %d", before, Length(text));
}
EI_VERIFY(0x0064A4E0, TopPanelPickedUpVerify, 0, s_unused);

static void __fastcall TopPanelMoneyVerify(CHudTopPanel* self, void*, int money) {
    const int before = Length(self->PageText(0));
    self->CHudTopPanel::AddMoney(money);
    const char* text = self->PageText(0);
    const bool ok = money <= 0 ? Length(text) == before : Length(text) > before + 1 && EndsWithNewline(text);
    ei::verify::Check("CHudTopPanel::AddMoney", true, ok, "money %d page text %d -> %d", money, before, Length(text));
}
EI_VERIFY(0x0064A630, TopPanelMoneyVerify, 0, s_unused);
