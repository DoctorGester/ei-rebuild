// The item panels' shared left-button handler (of the description panel, the hero equipment, the quick
// strip, the trade and repair grids, both constructor pages and the blank panel). See docs/notes/ui.md,
// section 5.
#include "ui.h"
#include "ui_item_info.h"
#include "ui_screens.h"

void UiItemPanel::LeftDown(int, int x, int y) {
    // The address of the x argument is the topic out-parameter, like the original.
    int topic = x;
    if (Item* item = ItemAt(x, y, &topic)) {
        owner->TakeItem(item, mode);
        return;
    }
    if (topic) owner->TopicClicked(topic);
}
