#include "object.h"

#include "figure_mesh.h"
#include "unit.h"

namespace {
const char* const kHooverName = "CHoover";
} // namespace

Hoover::~Hoover() {}

void Hoover::Create() {}
const char* Hoover::ClassName() { return kHooverName; }
ClassId Hoover::GetClassId() { return ClassId::Hoover; }
