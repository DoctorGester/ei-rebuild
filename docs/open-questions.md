# Open questions and parked checks

Things to come back to. Remove an entry once it is resolved.

## Parked

- **`CAnimationNode::SetMorph` (005064C0) has never run in game.** Rewritten in `src/game/figure.cpp`
  with a side-by-side verify wrapper, but neither map loading nor hero creation calls it (runs on
  2026-10-02). Its only caller found so far is code around 00508C50 that Ghidra had not disassembled.
  To do: after Ghidra's Aggressive Instruction Finder run completes, find the function containing
  00508CA5 and what calls it, work out which game action triggers it, and get a verify run. Until
  then it stays out of `tests/verify/verified.txt`.
- **Exit from the main menu hangs** when the game is launched through `ei_launch` (reported
  2026-10-02). Possibly a crash at shutdown with Rebirth's error box hidden behind the fullscreen
  window. Check `starter\Dumps` after an exit, and look again when startup/shutdown code is rewritten.
