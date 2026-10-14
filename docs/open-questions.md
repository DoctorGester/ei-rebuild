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

## Harness runs that do not repeat on the virtual clock (2026-10-14, SV1)

Harness runs use the virtual clock by default (tests/verify/harness_clock.h; a `realtime` line opts a
scenario out, none does). `python tools\compare.py --runs 3` on the vanilla suite, 2026-10-14: 61 of
77 scenarios repeat on both sides and match; of the other 16, ten only failed on dgVoodoo's minimise
(below; run_all.py now runs those again). What is left:

- **combat, combat_more, sound: neither side repeats.** The runs part in a fight (which hits land,
  `skill=` +-3.4e38 in "was attacked", and whether an AI unit's GuardMode goes idle or a "Delayed
  sound" line comes before or after a hit). Random traces (`--clock virtual,randtrace`) are the same
  draw for draw up to where they part, and no clock read, step or sound start/end differs before it
  (clocktrace and the trace's notes). Not explained yet; a guess is memory layout (an order by heap
  address, or an unwritten field read). SV2's state digest would name the first field that differs.
- **units, sandbox_actions: both sides repeat, ours differs.** The wolf that the hero fights dies
  sooner in ours: `waitfor 20 Not(IsAlive(GetObjectByName("Enemy1")))` is done after 400 ms of virtual
  time in ours and 700 ms in the original, every run (one "0" fewer in the console). A behaviour
  difference in the rewritten combat or AI. To find it: random traces side by side by value (the
  verify build draws more, its wrappers run the original on scratch copies), or SV2's digest.
- **rebirth_flags** fails on the original by design (its `rebirthflags` steps need our code). Ours
  passes but does not repeat either, and that is a lead for the combat case: the script variables
  it prints list the units of a group in a different order from run to run (`Peasans : group =
  <CUnit:1884>, <CUnit:702>, <CUnit:699>`, then `..., 699, 702`; a loop's last unit `i` with them),
  on both installs. An order that changes between runs with the same clock and draws points to an
  order by heap address.
- **dgVoodoo's minimise.** A game that Windows gave the foreground (when the game in front closed)
  and that lost it to a starting game is minimised by dgVoodoo; each time it came twice within
  ~100 ms, the first time taking the surfaces (the harness restores them, harness.cpp
  RestoreAfterMinimise) and the second the exclusive mode, which a background window does not get
  back (the renderer's own restore, 00504780, hung). The run ends on "Can't begin scene" / "Can't lock
  surface" (DDERR_SURFACELOST). Seen only in ours, on the last run of a scenario (it lives longest).
  run_all.run runs such a run again, up to twice. A cure would keep harness games from ever taking
  the foreground (dgVoodoo's own activation at SetCooperativeLevel gives it to them).
- Rebirth (`compare.py --rebirth --runs 3`, the 19 scenarios config/rebirth_scenarios.csv runs there):
  18 repeat and match; rebirth_flags as above. Three runs were run again after the minimise and passed.

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
