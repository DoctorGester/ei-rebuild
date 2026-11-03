// ei_vectors.exe links our game sources without the hook framework and without game.exe.
#include "hook/log.h"
#include "vectors.h"

namespace ei {

// The run log (the NaN audit, lib/nan_audit.h, writes to it): no log in the checks.
void Log(const char*, ...) {}

} // namespace ei
