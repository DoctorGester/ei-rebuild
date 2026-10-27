// The global lever table that levers look up (loaded from WinMain by the Rebirth-patched loader): an STLport
// vector of the records and their index by name. Its destructor is defined here. See docs/notes/app.md.
#include "lever.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "game_data.h"

LeverRecordTable::~LeverRecordTable() {
    byName.Destroy();
    records.clear();
    records.FreeStorage();
}
EI_REWRITTEN_SPECIAL(0x00463120, LeverRecordTable::~LeverRecordTable);

// The table, empty at the start: built by its static initialiser and destroyed by the exit function it registers.
constinit lib::StaticObject<LeverRecordTable> g_leverTable = /*0079C1A0*/ {};

namespace {
void __cdecl ConstructLeverTable() {
    g_leverTable.Construct();
    libcrt::AtExit(ei::Code<void(__cdecl*)()>(0x00463110));
}
void __cdecl DestroyLeverTable() { g_leverTable.Destroy(); }
} // namespace

EI_REPLACE(0x004630F0, ConstructLeverTable);
EI_REPLACE(0x00463110, DestroyLeverTable);
