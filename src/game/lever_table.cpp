// The global lever table that levers look up (loaded from WinMain by the Rebirth-patched loader): the records
// and their index by name. See docs/notes/app.md.
#include "lever.h"

#include <cstdlib>

// The table, empty at the start: built by its static initialiser and destroyed by the exit function it registers.
constinit lib::StaticObject<LeverRecordTable> g_leverTable = /*0079C1A0*/ {};

namespace {
void DestroyLeverTable() { g_leverTable.Destroy(); }
} // namespace

void ConstructLeverTable() {
    g_leverTable.Construct();
    std::atexit(DestroyLeverTable);
}
