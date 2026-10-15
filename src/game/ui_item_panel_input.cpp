// The item panels' shared left-button handler (005EE310, input slot 2 of the description panel,
// the hero equipment, the quick strip, the trade and repair grids, both constructor pages and the
// blank panel), and the blank panel's constructor (0060E210). See docs/notes/ui.md, section 5.
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
} // namespace
EI_REPLACE_METHOD(0x005EE310, InputForward<&CUiItemPanel::OnLeftDown>::Call);

// 0060E210: the blank panel's constructor.
CUiItemPanel* CUiItemPanel::ConstructBlank(CInterface3D* ui, CUiWindow* owner) {
    Construct(ui);
    vtable_ = reinterpret_cast<void**>(std::uintptr_t(0x00741B58));
    owner_ = owner;
    inputVtable_ = reinterpret_cast<void**>(std::uintptr_t(0x00741B24));
    return this;
}
EI_REPLACE_METHOD(0x0060E210, CUiItemPanel::ConstructBlank);
