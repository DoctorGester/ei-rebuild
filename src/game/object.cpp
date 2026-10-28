#include "object.h"

#include "figure_mesh.h"
#include "unit.h"


namespace {
const char* const kHooverName = "CHoover";
constexpr int kClassHoover = 0x1F;
} // namespace

CHoover::CHoover() { ClearFields(); }

CHoover::~CHoover() {}

int CHoover::Create() { return 1; }
const char* CHoover::GetClassName() { return kHooverName; }
int CHoover::GetClassId() { return kClassHoover; }
