#pragma once

// The orientation watch (EI_WATCH_CUNIT=<id>, verify build; tickets SV7b4 / SV3f): which code
// writes a client unit's orientation. After each world update, while the client world has a unit
// with that id, the main thread's four debug registers watch its orientation quaternion (CUnit
// +0x34, one register per component, writes only); every write is a line in watch.txt in the run
// folder: the world update, the code after the writing instruction, the four components as bit
// patterns and the first return addresses on the stack (game.exe as is, addon.dll as A+offset,
// ei_hook.dll as H+offset). Two runs' files (another heap layout, ours against the original) are
// compared line by line: the first line that differs names the writer. Off by default.

namespace ei::verify::watch {

bool Enabled();   // EI_WATCH_CUNIT
void Install();
// After every world update on the main thread: arms the registers on the unit (again), or clears
// them when it is gone.
void AfterWorldUpdate();
// EI_STACK_SCRUB=<byte> (experiment): 64 KB of the stack below the caller filled with the byte.
void ScrubStack();

} // namespace ei::verify::watch
