# Ticket queue

D1 done 2026-10-05 (docs/notes 1.38 -> 0.46 MB). Docs trim done 2026-10-09 (docs/notes 0.82 -> 0.57 MB, drafts deleted, their open points in the area notes). D2 queued: retarget ~150 code comments that cite removed section names ("Nth round", "pass", renderer section numbers) to the file / new section; tools/progress.py misses UI_INPUT_HANDLER / UI_WHEEL_HANDLER registrations. Since the docs trim also: items_spells.md "Save screen pieces" / "HUD panel displays" are in ui.md sections 8 / 7, its players section in objects.md "Players"; the A-S7 / A-S8 / A-S9, A-W5..A-W7 and A-U5..A-U7 sections were folded into each file's tables.

From 2026-10-05 (user): workers own one area for about a usage session ("area tickets"), commit every
3-5 functions with all checks green, and the coordinator merges the branch head periodically and runs
the suite. Forwarder cleanup batches are 50.

Mark a ticket taken by adding the date; remove it when merged.

## Renderer (src/render, docs/notes/renderer.md)

- R8 (first part merged 2026-10-05; second part taken: shadow textures, .mmp/.bmp textures without DirectDraw) Device setup and texture management (last). Includes R7's leftovers: 004EFA60 texture holders (save/load pictures, weather texture, item pictures) via the texture manager 0068F6C0..00693C60; presenting without the old Flip/BltFast.

## Units and combat, 0x00500000-0x00540000 (docs/notes/units.md, combat.md)

- A-U .. A-U7 done (2026-10-05 .. 10-09); U2 done.

## World, AI and server, 0x00540000-0x005E0000 (docs/notes/world.md, ai.md, unit_ai.md)

- A-W .. A-W8 done (A-W2 / A-W3 cut off). Next: unit_ai.md "A-W8" (client 00566360 / 0056C750 / 0056D150).

## App and startup, 0x00440000-0x00480000


## Interface, 0x005E0000-0x00640000 (docs/notes/ui.md)

- A-I .. A-I18 done (A-I, A-I4, A-I5 cut off). A-I19 cut off, merged 2026-10-09. A-I20 taken (the rest of A-I19's list) (item panels, chat overlay, console window, exchange slots, top-window news); left: ui.md section 3 "Next" (00612CF0; multiplayer: the base screen's chat window 00608D20-006097E0, exchange 0062C250 / 0062C9C0, connection screens 0061B2C0-00620300).

- I11 Next interface functions (ui.md, battle screen section); 0063EF60 / 0063E020 need modifier keys held during a click (harness step).

## HUD, battle screen, spells and items, 0x00640000-0x006C0000

- A-H .. A-H17 done. A-H18 cut off, merged 2026-10-09. A-H19 taken (the rest of A-H18's list) (sound Open / Close / Reset, terrain sectors, item database destructor, item vector serialisers); left: 0064F550, server login 0066F890, 00661FB0 / 00661FE0 (items_spells.md "Next candidates").

## Scripts, 0x004C0000-0x00500000 (docs/notes/script.md)

- A-S .. A-S9 done (A-S2 cut off). Next: script.md "Next candidates in this range".

## Pending work

The four porting batches (A-I16, A-H15, A-S9, A-U7) are merged (2026-10-09, 140 runs pass). Left: build, fix and merge the drafts below all six drafts are merged (2026-10-09). Then republish the dashboard and remove merged worktrees.

- C6 done 2026-10-09 (combat.md "C6 cleanup"). C7 done 2026-10-09.
- C8 merged 2026-10-09 (8 script / world files).

## Vector math (planned 2026-10-09)

Vector and matrix maths is mostly written out by hand: x*x + y*y (+ z*z) forms in 25 files, 164 sqrt calls, 235 raw float[3] / [4] / [9] / [16] arrays, and four separate 3-float structs (maths.h Vector3, ground_decal.h, render/render_internal.h, ui_camp.h) plus chunk_stream.h's Float4. Goal: one set of types and named operations, used everywhere, without changing results.
Constraints: the original is MSVC6 x87 code that inlined this maths, so the order of operations decides the rounding. Every helper is __forceinline and evaluates in a fixed, documented order (x, y, z left to right); where the original uses another order, the helper is a named variant, never a silent reorder. Side-by-side checks that compare floats exactly must still pass; CheckNear tolerances are not widened to make a migration pass. The x87 __asm helpers are a separate ticket.
- V1 done 2026-10-09, V2 done 2026-10-09 (vector-math.md).
- V3 (taken 2026-10-09): a check_cleanup column for unrolled vector maths (the x*x + y*y forms and float[3] / [16] members), then migrations by area in batches, each through the full suite.

## Draft passes (no build, 2026-10-07)

- DR-C1 .. DR-C5 and DR-H16 merged 2026-10-09; docs/notes/drafts deleted, open points in world.md, map_objects.md, objects.md, ui.md, items_spells.md. Open: function_sizes.inc needs a real regeneration once the Ghidra database is whole (Ghidra reported cut-short bodies, e.g. 0040D710 555 -> 60; only the handler rows from config/extra_function_sizes.csv were added by hand).

## Verify checks

- X23 done 2026-10-07 (A-H14 merge regressions).

- X22 flaky: render_present (vanilla) crashed once under 16 parallel runs, null call inside DDRAW.dll from render::CopyFrameTo <- DevicePresentVerify <- ShowLoadingMovie (2026-10-07, after A-S7). Passed 3/3 alone. Watch; if it recurs, guard CopyFrameTo against a lost surface.

- X17 done 2026-10-07: ground decal ClipPlane / AddTriangle / AddTerrainTriangle and CHudTopPanel::Build stay in tests/verify/disabled.txt (script.md, ui.md).

## Cleanup (docs/cleanup-plan.md)

- D3 .. D3d done 2026-10-07 (core.md, "Shared containers").

- C2 Phase 1 for the engine base and core networking (0x00400000-0x00440000, fully ported): one
  real declaration per class, members instead of At<>/offsets, virtual calls instead of VCall,
  globals instead of addresses, duplicates merged; behaviour unchanged. Split per class family:
  - (C2d camera merged 2026-10-05.)
- C3b (top 10, C3c-C3e and C3f, the next 15 by site count each; C3g merged; C3h merged; C3i merged; C3j merged; C3k merged (283 "differ" left); C3l done 2026-10-05 (261 "differ" left: 209 already rewritten, 52 not yet rewritten of which 41 wait for a ui_* site once I10 is merged and 11 keep a src/lib copy; notes list the ui_* sites to convert, so the next C3 ticket is those, then calling the rewritten ones directly); batches are 50 from now on (user, 2026-10-05)) The 504 forwarders whose declarations differ between files (docs/originals-report.md "differ"
  group): decide per address which declaration is right (RET n, callers, Ghidra), keep one in the
  shared originals_<area>.h header. Start with the top 10 by site count (0x006E727B, 0x00455950,
  0x0040C720, ...), then by area. C3 first part (shared headers, 38 void forwarders typed) merged 2026-10-05.
- C5 C4 leftovers: UnitStream, SaveStream, FileStream, the unit_ai stream, verify_magic_trap's wrapper, the other per-file Text(0x...) helpers (docs/notes/cleanup-engine-base.md).
