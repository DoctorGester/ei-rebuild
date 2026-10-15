#pragma once

// Watches for tickets SV7b4 / SV3f (verify build; all off by default). Lines go to watch.txt in the
// run folder; addresses print as game.exe's, addon.dll's as A+offset, ei_hook.dll's as H+offset.
//
// EI_WATCH_CUNIT=<id>: which code writes a client unit's orientation. After each world update,
// while the client world has a unit with that id, the main thread's four debug registers watch its
// orientation quaternion (CUnit +0x34, one register per component, writes only); every write is a
// line: the world update, the code after the writing instruction, the four components as bit
// patterns and the first return addresses on the stack. Two runs' files are compared line by line.
// EI_WATCH_CUNIT=<id>,flags watches the unit's flags_ (+0x08) instead, logging the writes that
// change it (SV7b8a).
//
// EI_WATCH_ADDR=<hex>[,<from>,<to>]: one dword (a stack slot, say) watched for reads and writes from
// the end of world update <from> to the end of <to>: each access with the code, the value and the
// callers.
//
// Experiments (SV7b4: they tell a stack leftover from a timing effect): EI_STACK_SCRUB=<byte>
// [,<from>,<to>[,<skipFrom>,<skipTo>]] fills the stack below the server update's caller after each
// world update (bytes from..to below the top, default 64 KB, minus the skipped range);
// EI_SPIN_US=<n> busy-waits n microseconds there instead (no stack writes).
//
// Every debug-register hit raises an exception on the main thread, which both changes the stack
// below it and costs time: SV7b4's orientation ulp disappears under the watch, so compare runs that
// were all watched.

namespace ei::verify::watch {

bool Enabled();   // EI_WATCH_CUNIT or EI_WATCH_ADDR
void Install();
// After every world update on the main thread: arms or clears the registers.
void AfterWorldUpdate();
// After every world update: EI_STACK_SCRUB / EI_SPIN_US.
void ScrubStack();

} // namespace ei::verify::watch
