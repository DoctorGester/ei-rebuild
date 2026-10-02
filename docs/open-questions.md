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
