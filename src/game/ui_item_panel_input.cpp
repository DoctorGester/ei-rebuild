// The item panels' shared left-button handler (of the description panel,
// the hero equipment, the quick strip, the trade and repair grids, both constructor pages and the
// blank panel), and the blank panel's constructor. See docs/notes/ui.md, section 5.
#include "ui.h"
#include "ui_item_info.h"
#include "ui_screens.h"

#include "ei/hook.h"
#include "game_data.h"

void CUiItemPanel::OnLeftDown(int, int x, int y) {
    // The original hands the address of its x argument as the topic out-parameter.
    int topic = x;
    if (CItem* item = ItemAt(x, y, &topic)) {
        OwnerTakeItem(item);
        return;
    }
    if (topic) OwnerTopicClicked(topic);
}
EI_REWRITTEN(0x005EE310, CUiItemPanel::OnLeftDown);

CUiItemPanel::CUiItemPanel(CInterface3D* ui, CItemScreen* owner) : CUiWindow(ui), owner_(owner) {}
EI_REWRITTEN_SPECIAL(0x0060E210, CUiItemPanel::CUiItemPanel);
