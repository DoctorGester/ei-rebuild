// Rebirth's WideScreen layout (src/game/ui_widescreen.cpp).
//
// The scaling, SetRect / AddPoint and Place are verified by their wrappers (verify_ui.cpp,
// verify_ui_text_draw.cpp): side by side with addon's code in a Rebirth launch, the panel offset
// shared with addon's panel builders. This file checks the layout itself: in a Rebirth launch where
// addon applied the patch, the two layout widths it wrote before WinMain (what our startup found
// there) must be the ones we compute from the same settings, and so must the two floats it wrote
// into the code (W4): the font scale at 005F4F81 and the axis swap's x scale, the operand of its
// MOV DWORD PTR [EAX],imm32 at 004F135A. Scenario: every Rebirth launch (all wide here).
#include "verify.h"

#include "../../src/game/ui_widescreen.h"
#include "ei/hook.h"

#include <cstdint>
#include <cstring>

namespace {

std::uint32_t Bits(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float Read(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

void WideScreenLayoutCheck() {
    const widescreen::Layout& layout = widescreen::Get();
    if (!layout.active) return;
    const float found = widescreen::FoundLayoutWidth();
    if (found == 800.0f) return;   // nobody wrote it before us: a vanilla launch with the flag on
    ei::verify::Check("WideScreen layout width (00742B08)", Bits(found), Bits(Read(std::uintptr_t(0x00742B08))),
                      "%dx%d extra %d: addon %g", layout.width, layout.height, layout.extra, double(found));
    ei::verify::Check("WideScreen panel right (007428AC)", Bits(widescreen::FoundPanelRight()),
                      Bits(Read(std::uintptr_t(0x007428AC))), "%dx%d extra %d: addon %g", layout.width, layout.height,
                      layout.extra, double(widescreen::FoundPanelRight()));
    ei::verify::Check("WideScreen font scale (005F4F81)", Bits(Read(std::uintptr_t(0x005F4F81))), Bits(layout.fontScale),
                      "%dx%d: addon %g, ours %g", layout.width, layout.height, double(Read(std::uintptr_t(0x005F4F81))),
                      double(layout.fontScale));
    ei::verify::Check("WideScreen axis scale (004F135C)", Bits(Read(std::uintptr_t(0x004F135C))), Bits(layout.axisScale),
                      "%dx%d: addon %g, ours %g", layout.width, layout.height, double(Read(std::uintptr_t(0x004F135C))),
                      double(layout.axisScale));
}
EI_STARTUP_CHECK(WideScreenLayoutCheck);

} // namespace
