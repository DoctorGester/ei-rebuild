# Open questions and parked checks

Things to come back to. Remove an entry once it is resolved.

## Parked

- **`CBaseManager` instance create/destroy (00403730, 00403AE0, 00404040).** Object pooling across
  four STLport maps (+0x1C objects by id, +0x50, +0x68 per-class free lists, +0x80 per-class counts),
  an MFC `CPtrList` at +0x34 and pool lists. Deferred as high-risk; needs each map's key/value types
  and the list helpers (004069A0, 004050C0, 00404F80 ...) mapped first.

- **`CAnimationNode::SetMorph` (005064C0) has never run in game.** Rewritten in `src/game/figure.cpp`
  with a side-by-side verify wrapper, but neither map loading nor hero creation calls it (runs on
  2026-10-02). Its only caller found so far is code around 00508C50 that Ghidra had not disassembled.
  To do: after Ghidra's Aggressive Instruction Finder run completes, find the function containing
  00508CA5 and what calls it, work out which game action triggers it, and get a verify run. Until
  then it stays out of `tests/verify/verified.txt`.
- **Exit from the main menu hangs** when the game is launched through `ei_launch` (reported
  2026-10-02). Possibly a crash at shutdown with Rebirth's error box hidden behind the fullscreen
  window. Check `starter\Dumps` after an exit, and look again when startup/shutdown code is rewritten.
  The vanilla launch (`launch-vanilla.cmd`) exited cleanly with all hooks active on 2026-10-02, so
  the hang is likely Rebirth-specific.

## Deterministic harness runs (parked, 2026-10-03)

`deterministic` in a scenario (tests/verify/harness.cpp) makes timeGetTime/GetTickCount a virtual
clock that advances four world updates per main-loop frame, fixes the date, and replaces the ISAAC
seeding (0049F660, which seeds from timeGetTime and 0x400 bytes of a random file under C:\). The goal
is `tools/compare.py`: run a scenario with and without hooks and diff the console output.

It does not repeat yet: the same scenario run twice on the original code diverges. Seen so far:
- A new game sometimes never finishes loading under the virtual clock (loading waits on something
  real-time; the fallback advances the clock by 1 ms per 10000 off-loop reads), and otherwise takes
  a different number of frames each run.
- A second thread (sound, 006B5xxx-006BBxxx) reads the clock on its own schedule, and its "Delayed
  sound: unit heard unit" events feed AI hearing.
Next steps would be: find what the loader waits on; make the sound thread's events land on frame
boundaries (or disable sound in harness runs).

## Rare Rebirth crash in the unit sort during new-game load (seen once, 2026-10-03)

One of ~12 Rebirth runs of a new game crashed in the original `UnitOrderLess::Less` (00574300,
called through the verify trampoline) reading `[0x880003C4+0x94]`: the sort at 005740D0 (called
from 0056AFE0, client view update) passed it a garbage element. Three reruns of the same scenario
passed. The comparator itself (rewritten and original) is a consistent strict ordering, so the bad
element came from the array the caller sorts. Unknown whether this happens without our hooks; watch
for it in run_all output (look for exceptions at 00574318).
