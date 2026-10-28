#include "object.h"

#include "figure_mesh.h"
#include "unit.h"

namespace {
const char* const kHooverName = "CHoover";
} // namespace

CHoover::CHoover() { ClearFields(); }

CHoover::~CHoover() {}

int CHoover::Create() { return 1; }
const char* CHoover::ClassName() { return kHooverName; }
ClassId CHoover::GetClassId() { return ClassId::Hoover; }
