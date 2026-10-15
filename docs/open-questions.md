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

- **combat, combat_more, sound: solved (SV1b, 2026-10-15).** The runs parted in a fight (which hits
  land, `skill=` +-3.4e38 in "was attacked", whether a GuardMode goes idle or a "Delayed sound" line
  comes before or after a hit), with the same random draws up to there. The cause was the heap:
  game.exe's CRT (VC6, static) puts every malloc on one Win32 heap of its own (_crtheap 007CAF08,
  _heap_init 006F54E6; on NT 5+ __heap_select picks the system heap, no small-block heap), and on
  Windows 8+ such a heap gets the low-fragmentation front end, which hands out a bucket's free slots
  in a random order. The same calls got other addresses every run, and the game reads addresses: a
  unit's update-map countdown is 0x80 + bits 5..8 of its address (SV2; 0058D5D0 and seven more
  places), which decides when an idle unit leaves the update map (UpdateUnits 0058D670). The
  harness now makes the CRT's heap without the front end (HEAP_NO_SERIALIZE, with a lock of its own;
  tests/verify/harness_heap.h), and on that heap the original repeats bit for bit (`golden.py diff
  --all-fields`: all 630 / 635 / 530 records the same; with `--clock virtual,lfheap`, the old heap,
  combat does not repeat). Ours and the original still allocate differently (the verify wrappers),
  so the harness also has both sides spread the countdown by the object's id instead of its address
  (the eight original sites patched, CWorldServer::UpdateCountdownFor in ours). What was left
  between ours and the original was a rewrite bug: AIViewer::Refresh (005CA1D0) appended the units
  near a unit to its nearby list, where the original inserts each at the head, so two units coming
  into view in one update were "seen" in the other order. With that fixed, the three repeat and match
  (and SV1a's wrapper fix, merged before). Left: in sound the digests of ours and the original part
  at update 272 in `crt.rand` only (a client-side animation pick, StartIdleAnimation -> FindAnimation,
  one frame earlier in ours) without changing what the game prints. Before SV1a's fix ours alone did
  not always repeat that pick (in 3 of 6 sound runs its timing moved); after it, 6 of 6 repeated.
- **units, sandbox_actions: solved (SV1a, 2026-10-15).** The wolf died after 400 ms of virtual time in
  ours against 700 ms in the original. Not the rewrite: with `EI_PLAIN=1` (no verify wrappers) ours
  matched the original step for step. A bisection over the verify wrappers (verified.txt) named
  `UnitReadNetStateVerify` (0052EF90, tests/verify/verify_net_state.cpp): after ours read a unit's
  network state, the original read it again with the delta globals put back but not the random
  generators, so the animation field's restart (slot 14 -> FindAnimation 005301B0) drew rand() a
  second time, and every later draw (the AI's sight countdowns, the fight) moved. A read in which
  ours draws now runs ours only. After the fix (digest every 5 updates, `golden.py diff`) units,
  sandbox_actions and harness_digest match the original in every record and field but those left
  out on purpose; SV2's harness_digest difference (only crt.rand from update 230 on, an animation
  pick seemingly one update early) was the same wrapper. One earlier units run (before merging
  main) still parted at world update 550, in the crawl after the fight (the world's update map, 14
  against 15 entries), in the plain build as well: likely the update-map countdown above, which
  follows the heap layout.
- **rebirth_flags** fails on the original by design (its `rebirthflags` steps need our code). Ours
  did not repeat: the units of a group were listed in a different order from run to run (`Peasans :
  group = <CUnit:1884>, <CUnit:702>, <CUnit:699>`, then `..., 699, 702`), an order by heap address,
  which the repeatable heap above fixed. The other difference was the scenario's own:
  `#DistancePointPoint(3,4)` printed the two floats addon's handler reads past its arguments (5,
  1.#INF or a NaN); the scenario now only checks that it is not below 0 (SV1b).
- **Digests of the whole suite (SV3, 2026-10-15, a record every 20 world updates):** two runs of the
  original part in 46 of 110 runs (31 of 86 vanilla, 15 of 24 Rebirth), most at the first record
  where zone1's idle wolves (obj1936/1937, unmoto) think: ai.standCooldown or regenTicks, and the
  generators' counts. Ours repeats in all but two of the rest, but 14 of those differ from the
  original the same way in both of ours' runs. The lists are in standalone-verification.md (SV3).
- **dgVoodoo's minimise.** A game that Windows gave the foreground (when the game in front closed)
  and that lost it to a starting game is minimised by dgVoodoo; each time it came twice within
  ~100 ms, the first time taking the surfaces (the harness restores them, harness.cpp
  RestoreAfterMinimise) and the second the exclusive mode, which a background window does not get
  back (the renderer's own restore, 00504780, hung). The run ends on "Can't begin scene" / "Can't lock
  surface" (DDERR_SURFACELOST). Seen only in ours, on the last run of a scenario (it lives longest).
  run_all.run runs such a run again, up to twice. A cure would keep harness games from ever taking
  the foreground (dgVoodoo's own activation at SetCooperativeLevel gives it to them).
- **Start-up hangs on a busy machine.** With many games on the machine (other workers' suites), some
  runs on either side and either clock hang before the main menu until the 60 s limit: the main
  thread is in DirectSound's device open inside Miles' waveOutOpen (mss32 -> DSOUND, stack at the
  timeout). Environmental; run_all's retry usually passes them.
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
