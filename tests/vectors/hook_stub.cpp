// ei_vectors.exe links our game sources without the hook framework and without game.exe.
#include "hook/log.h"
#include "platform/win32/import_table.h"
#include "vectors.h"

namespace ei {

// The import table gameapi.cpp binds its references to: all null (no check calls an import through it).
ImportTable& Imports() {
    static ImportTable table{};
    return table;
}

// The run log (the NaN audit, lib/nan_audit.h, writes to it): no log in the checks.
void Log(const char*, ...) {}

} // namespace ei
