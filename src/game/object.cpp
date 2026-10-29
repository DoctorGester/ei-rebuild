#include "object.h"

#include "figure_mesh.h"
#include "unit.h"

namespace {
const char* const kHooverName = "CHoover";
} // namespace

CHoover::~CHoover() {}

void CHoover::Create() {}
const char* CHoover::ClassName() { return kHooverName; }
ClassId CHoover::GetClassId() { return ClassId::Hoover; }
