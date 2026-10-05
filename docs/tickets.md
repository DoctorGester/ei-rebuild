# Ticket queue

D1 done 2026-10-05: docs/notes 1.38 MB -> 0.46 MB (current-state references; history in git). D2 queued: retarget ~150 code comments that cite removed section names ("Nth round", "pass", renderer section numbers) to the file / new section; tools/progress.py misses UI_INPUT_HANDLER / UI_WHEEL_HANDLER registrations.

From 2026-10-05 (user): workers own one area for about a usage session ("area tickets"), commit every
3-5 functions with all checks green, and the coordinator merges the branch head periodically and runs
the suite. Forwarder cleanup batches are 50.

Mark a ticket taken by adding the date; remove it when merged.

## Renderer (src/render, docs/notes/renderer.md)

- R8 (first part merged 2026-10-05; second part taken: shadow textures, .mmp/.bmp textures without DirectDraw) Device setup and texture management (last). Includes R7's leftovers: 004EFA60 texture holders (save/load pictures, weather texture, item pictures) via the texture manager 0068F6C0..00693C60; presenting without the old Flip/BltFast.

## Units and combat, 0x00500000-0x00540000 (docs/notes/units.md, combat.md)

- A-U done 2026-10-05 (40 functions).
- U2 done (00535850 ClientPlaceOnPath, 0052FCC0 / 0052FDF0 in follow-up for a crash).

## World, AI and server, 0x00540000-0x005E0000 (docs/notes/world.md, ai.md, unit_ai.md)

- A-W done 2026-10-05 (33 functions; leftovers listed in unit_ai.md / world.md). A-W2 (taken 2026-10-05): 0x005A0000-0x005E0000 (paths, world client) plus A-W's leftovers.

## App and startup, 0x00440000-0x00480000


## Interface, 0x005E0000-0x00640000 (docs/notes/ui.md)

- A-I (cut off by the usage limit; 12 functions merged). A-I2 done 2026-10-05 (36 functions). A-I3 done (27). A-I4 (taken 2026-10-05).


- I11 Next interface functions (ui.md, battle screen section); 0063EF60 / 0063E020 need modifier keys held during a click (harness step).

## HUD, battle screen, spells and items, 0x00640000-0x006C0000

- A-H done 2026-10-05 (38 functions). A-H2 done 2026-10-05 (41 functions; next candidates at the end of items_spells.md). A-H3 done (44 addresses). A-H4 (taken 2026-10-05) (candidates at the end of items_spells.md's Party section; 0064A7A0). (candidates at the end of items_spells.md "Area ticket A-H").


## Scripts, 0x004C0000-0x00500000 (docs/notes/script.md)

- A-S done 2026-10-05 (40 functions in 0x004B0000-0x004C0000). A-S2 (cut off by the usage limit; 20 functions merged). A-S3 done 2026-10-05 (38 functions; leftovers: .mob reader 00481660 / 00481F00, particle attach 004849F0 / 00484C00, colour quantisation 0049BEA0). Was: scripts 0x004C0000-0x00500000 plus the 0x00480000-0x004B0000 leftovers.


## Verify checks




## Cleanup (docs/cleanup-plan.md)

- C2 Phase 1 for the engine base and core networking (0x00400000-0x00440000, fully ported): one
  real declaration per class, members instead of At<>/offsets, virtual calls instead of VCall,
  globals instead of addresses, duplicates merged; behaviour unchanged. Split per class family:
  - (C2d camera merged 2026-10-05.)
- C3b (top 10, C3c-C3e and C3f, the next 15 by site count each; C3g merged; C3h merged; C3i merged; C3j merged; C3k merged (283 "differ" left); C3l done 2026-10-05 (261 "differ" left: 209 already rewritten, 52 not yet rewritten of which 41 wait for a ui_* site once I10 is merged and 11 keep a src/lib copy; notes list the ui_* sites to convert, so the next C3 ticket is those, then calling the rewritten ones directly); batches are 50 from now on (user, 2026-10-05)) The 504 forwarders whose declarations differ between files (docs/originals-report.md "differ"
  group): decide per address which declaration is right (RET n, callers, Ghidra), keep one in the
  shared originals_<area>.h header. Start with the top 10 by site count (0x006E727B, 0x00455950,
  0x0040C720, ...), then by area. C3 first part (shared headers, 38 void forwarders typed) merged 2026-10-05.
- C5 C4 leftovers: UnitStream, SaveStream, FileStream, the unit_ai stream, verify_magic_trap's wrapper, the other per-file Text(0x...) helpers (docs/notes/cleanup-engine-base.md).
