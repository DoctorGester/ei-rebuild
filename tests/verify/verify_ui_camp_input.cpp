// Checks for the camp's input side, message-result and help-topic slots (src/game/ui_camp_input.cpp).
//
// HelpTopic is pure: side by side. The handlers pass events to the children (the item strips'
// handlers must not run twice), play sounds and change pages: ours only, the wrapper checks the
// state they leave. Scenario: ui_camp / ui_describe (`screen 5`: page regions, the hero's turn
// buttons, Esc; the help topic when the camp opens); OnMessageResult is not known to be reached.
#include "verify.h"

#include "../../src/game/ui_camp.h"
#include "../../src/game/unit.h"

namespace {
void* s_unused;
void* s_helpTopic;
} // namespace

static const char* __fastcall HelpTopicVerify(CCampPages* self, void*) {
    const char* expected = reinterpret_cast<const char*(__fastcall*)(CCampPages*, void*)>(s_helpTopic)(self, nullptr);
    const char* actual = self->HelpTopic();
    ei::verify::Check("CCampPages::HelpTopic", expected, actual, "page %d", self->page_);
    return expected;
}
// 0060E980: MOV EAX,[ECX+0x190]; CMP EAX,6 (6 + 3 bytes).
EI_VERIFY(0x0060E980, HelpTopicVerify, 9, s_helpTopic);

static void __fastcall LButtonDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* camp = WindowFromInput<CCampPages>(input);
    int region = camp->RegionAt(x, y);
    int page = camp->page_;
    camp->OnLButtonDown(buttons, x, y);
    bool ok = true;
    if ((region == 2 || region == 3) && page == 0)
        ok = camp->turning_ == region - 1 && camp->interface_->mouseCapture_ == input;
    else if (region >= 4 && region <= 11)
        ok = camp->page_ == region - 4 || camp->page_ == page;   // an unoffered page keeps the current one
    ei::verify::Check("CCampPages::OnLButtonDown", true, ok, "region %d, page %d -> %d", region, page, camp->page_);
}
EI_VERIFY(0x0060F470, LButtonDownVerify, 0, s_unused);

static void __fastcall LButtonUpVerify(void* input, void*, int buttons, int x, int y) {
    auto* camp = WindowFromInput<CCampPages>(input);
    camp->OnLButtonUp(buttons, x, y);
    bool ok = camp->turning_ == 0 && camp->interface_->mouseCapture_ != input;
    ei::verify::Check("CCampPages::OnLButtonUp", true, ok, "(%d,%d)", x, y);
}
EI_VERIFY(0x0060F5B0, LButtonUpVerify, 0, s_unused);

static void __fastcall KeyDownVerify(void* input, void*, int key, int repeat, int flags) {
    auto* camp = WindowFromInput<CCampPages>(input);
    camp->OnKeyDown(key, repeat, flags);
    ei::verify::Check("CCampPages::OnKeyDown", true, key != VK_ESCAPE || camp->inputHandled_, "key %X", key);
}
EI_VERIFY(0x0060F600, KeyDownVerify, 0, s_unused);

static void __fastcall MessageResultVerify(CCampPages* self, void*, int result) {
    if (result != 0x10001) {
        self->OnMessageResult(result);
        return;
    }
    PartyUnitRecord* hero = ShownHero(self);
    int before = static_cast<int>(hero->weaponCount);
    self->OnMessageResult(result);
    bool ok = before <= 0 || (static_cast<int>(hero->weaponCount) == before - 1 && hero->weaponIndex == 0xFFFFFFFFu);
    ei::verify::Check("CCampPages::OnMessageResult", true, ok, "weapons %d -> %d", before,
                      static_cast<int>(hero->weaponCount));
}
EI_VERIFY(0x0060E900, MessageResultVerify, 0, s_unused);
