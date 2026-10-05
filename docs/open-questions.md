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
- A second thread (sound, 006B5xxx-006BBxxx) reads the clock on its own schedule. Mapped since
  (docs/notes/sound.md): the sound threads draw no random numbers and touch no game state; the
  "Delayed sound" / "hear delayed sound" events are the AI brain's own countdowns (005CB700 ->
  005CC9D0, world thread). What does shift the shared random generator (007A9D40) between runs is
  main-thread sound code driven by the wall clock: CSoundManager::Tick's ambient emitters (every
  100 ms of GetTickCount), the music's calm delay and track choice (timeGetTime), random sound
  sequences, Play2D/Play3D's random start position and the terrain ambience 006CB390.
Next steps would be: find what the loader waits on; keep the virtual clock for GetTickCount /
timeGetTime on the main thread too, or run harness games with sound disabled (CSoundManager +0x898
= 0 makes all of the above return early).

## Rare Rebirth crash in the unit sort during new-game load (seen once, 2026-10-03)

One of ~12 Rebirth runs of a new game crashed in the original `UnitOrderLess::Less` (00574300,
called through the verify trampoline) reading `[0x880003C4+0x94]`: the sort at 005740D0 (called
from 0056AFE0, client view update) passed it a garbage element. Three reruns of the same scenario
passed. The comparator itself (rewritten and original) is a consistent strict ordering, so the bad
element came from the array the caller sorts. Unknown whether this happens without our hooks; watch
for it in run_all output (look for exceptions at 00574318).
Likely cause (from reading the code, not yet confirmed by a run; docs/notes/world.md, "World client frame"): the comparator's depth test treats NaN as "less", so an object
with a NaN view depth (+0x30) makes Less(x, x) true and STLport's unguarded partition walks off the
front of the vector. 0056AFE0 also reads the view matrix from an unchecked view stack depth, a
possible source of NaN depths while a new game loads. The verify build logs both ("not orderable",
"view stack depth" in ei_hook.log).

Update (2026-10-03, full run): the diagnostic logged "IntroSortByModel: element 41 of -11 is not
orderable" (a range with first > last) and one "MISMATCH IntroSortByModel: n 49" in 937,548 calls.
Both follow from the NaN case, not from a caller: 0056AFE0's nine calls pass begin/end of the
frame's vectors (never reversed). A reversed range is what 005740D0's own recursive call
(00574274) passes when its partition's left scan `while (Less(*first, pivot))` ran past `last`:
with a NaN pivot depth, Less(x, pivot) is true for every element of the same rank and +0xF0, so
the scan runs into the memory after the vector, and the cut it returns lies beyond `last`. The
original then returns at once from the reversed call (its size test is signed), but the scans have
already read pointers from outside the array and dereferenced them (+0x94): that is the crash, and
it is an original bug (kept). Worse, the loop then continues with `last = cut`, beyond the real
end, so it partitions the memory past the vector too: garbage words can be swapped into the vector
(the garbage element the comparator then crashes on) and vector entries into the memory after it. The verify missed it because its NaN test `x != x` was folded to false by /fp:fast;
the range was sorted on a scratch copy, whose overrun read different memory than ours on the real
vector, hence the mismatch. Fixed in the verify (NaN tested on the bits; reversed ranges and
NaN ranges go to the original alone and are logged; mismatches now log keys and both orders).
The algorithm was rechecked step by step against 005740D0, 00574390, 00577FC0 and 00577730
(median of three, unguarded partition, recursion on the right part, depth limit, heap fallback,
insertion threshold 16) with no difference found. Still open: where the NaN depths come from
(look for "view stack depth" and the depth/position bits of "not orderable" lines).

- Original bug (fix candidate): CUnitClientSpecific::Level (00539160) returns INT_MIN for a unit with 0 experience (FISTP of ln 0 = -inf). Kept for now; see units.md.
