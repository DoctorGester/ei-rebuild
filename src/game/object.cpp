#include "object.h"

#include "ei/hook.h"
#include "figure_mesh.h"
#include "scene_loader.h"
#include "unit.h"


namespace {
const char* const kHooverName = "CHoover";
constexpr int kClassHoover = 0x1F;
} // namespace

CHoover::CHoover() { ClearFields(); }
EI_REWRITTEN_SPECIAL(0x00461C10, CHoover::CHoover);

CHoover::~CHoover() {}
EI_REWRITTEN_SPECIAL(0x00461C80, CHoover::~CHoover);
EI_REWRITTEN_SPECIAL(0x00461C60, CHoover::~CHoover);

int CHoover::Create() { return 1; }
EI_REWRITTEN(0x00461C90, CHoover::Create);
const char* CHoover::GetClassName() { return kHooverName; }
EI_REWRITTEN(0x00461C40, CHoover::GetClassName);
int CHoover::GetClassId() { return kClassHoover; }
EI_REWRITTEN(0x00461C50, CHoover::GetClassId);

// --- Unit and figure getters --------------------------------------------------------------------

