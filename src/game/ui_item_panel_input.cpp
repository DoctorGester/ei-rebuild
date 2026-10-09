// The item panels' shared left-button handler (005EE310, input slot 2 of the description panel,
// the hero equipment, the quick strip, the trade and repair grids, both constructor pages and the
// blank panel). See docs/notes/ui.md, section 5.
#include "ui.h"
#include "ui_screens.h"

#include "ei/hook.h"

void CUiItemPanel::OnLeftDown(int, int x, int y) {
    // The original hands the address of its x argument as the topic out-parameter.
    int topic = x;
    if (void* item = VirtualItemAt(x, y, &topic)) {
        OwnerTakeItem(item);
        return;
    }
    if (topic) OwnerTopicClicked(topic);
}
namespace {
void __fastcall ItemPanelLeftDown(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CUiItemPanel>(input)->OnLeftDown(buttons, x, y);
}
} // namespace
EI_REPLACE(0x005EE310, ItemPanelLeftDown);
