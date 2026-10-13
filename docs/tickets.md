# Ticket queue

From 2026-10-05 (user): workers own one area for about a usage session ("area tickets"), commit every
3-5 functions with all checks green, and the coordinator merges the branch head periodically and runs
the suite. Forwarder cleanup batches are 50. Mark a ticket taken by adding the date; remove it when merged.

Docs: D1 done 2026-10-05; trims 2026-10-09 (notes 0.82 -> 0.57 MB, then the per-ticket sections of C6-C10, A-H17..A-H21,
A-I19..A-I21, A-U8, A-W9, A-X1 / A-X2, V2 / V3 folded into the area tables). D2 queued: retarget ~150 code comments that
cite removed section names ("Nth round", "pass", renderer section numbers, items_spells.md "(A-H17)") to the file / new
section; tools/progress.py misses UI_INPUT_HANDLER / UI_WHEEL_HANDLER registrations.

## Renderer (src/render, docs/notes/renderer.md)

- R8 (first part merged 2026-10-05; second part taken: shadow textures, .mmp/.bmp textures without DirectDraw) Device setup and texture management (last). Includes R7's leftovers: 004EFA60 texture holders (save/load pictures, weather texture, item pictures) via the texture manager 0068F6C0..00693C60; presenting without the old Flip/BltFast.
- R-next (parked until porting is finished, user 2026-10-09): renderer device removal (no DirectDraw / D3D7 device in the switch-over, own swap chain, renderer.md section 9), then native UI drawing window by window.

## Area tickets (done; next candidates)

Priority (user, 2026-10-13): finish the port; three area workers at once, refilled by code left; cleanup and V3 wait. Rebirth-patched functions get ported too (hooked in vanilla only), after the unpatched ones in each area.

- Units and combat 0x00500000-0x00560000 (units.md, combat.md): A-U .. A-U8 done (A-U8 cut off, merged 2026-10-09). A-U9 done 2026-10-09 (12; FigureBone names fixed to "none"). A-U10, A-U11 merged 2026-10-10 (units 456 -> 317 unported; next: map_objects.md, CObject3DClientSpecific part sets then 0051B050..0051CB30, materials 0051E300-0051F0D0): branch worktree-agent-a7113885b53ce585e head b662e96f (50: animation state, animated nodes, renderer scene, wounds texture, skill database, animation tracks). A-U12 taken 2026-10-13. Next: map_objects.md "Skipped and why" (0050D7C0, 0050CC60, 0050C730, 0050C5F0, 0050DFF0, 00506710 / 005068C0, renderer 00504860 / 00503F40 / 005040A0 / 00504E50 / 00504F70 / 00505270), then 0051C3D0 wounds texture, perks database 0051F5B0 / 00521630 / 005218E0.
- World, AI, server 0x00540000-0x005E0000 (world.md, ai.md, unit_ai.md): A-W .. A-W12 done (A-W2, A-W3, A-W9 cut off). A-W13 merged 2026-10-13 (12 groups: generateacks, sector pool, AI map grids, order checks, figure meshes, game flow glue, world getters; 266 functions / 23 KB left, 12.8 KB of it Rebirth-patched). A-W14 taken 2026-10-13 (the list A-W13 left ready, deleting destructors, larger leftovers, getters, static initialisers, then the Rebirth-patched ones). Next: world.md, the last A-W13 rows.
- Interface 0x005E0000-0x00640000 (ui.md): A-I .. A-I21 done (A-I, A-I4, A-I5, A-I19 cut off). A-I22 taken 2026-10-13 (unpatched leftovers, then the Rebirth-patched ones, vanilla only). Next: ui.md section 3 "Next".
- HUD, spells, items 0x00640000-0x006C0000 (items_spells.md): A-H .. A-H24 done (A-H18, A-H21, A-H24 cut off). A-H25 merged 2026-10-13 (12 groups: name lists, textures, effects, item records, DirectInput mouse, titles, edit field, lights, sound streams). The unpatched part is finished; left: the 24 Rebirth-patched functions listed in items_spells.md "Left in 0x640000-0x6C0000 after A-H25" (vanilla only), 0066D730 (sends an uninitialised byte, kept original), 0067E4E0 (runs before the hooks), 006BFB00 / 006BFB20 / 006BFB80 (SectorData, terrain files).
- Scripts 0x004C0000-0x00500000 (script.md): A-S .. A-S9 done (A-S2 cut off). A-S10 merged 2026-10-10: branch worktree-agent-abfbe1ab53cfc96da head 13f9b5c1 (particle manager, ISAAC seed, script unit mode slots, GameError, rain/snow, zone exit stream, fog slots; the rest of 0x4C0000-0x500000 is renderer). Next: script.md "Next candidates in this range".
- CRT / MFC range 0x006C0000-0x00740000 (models.md): A-X1 .. A-X5 done (A-X2 cut off). A-X6 merged 2026-10-13 (25: landscape draw passes, light tests, CDib, surface captures, acks RecordAt; new scenario terrain_water_load). Left: 006C68B0, 006D0630, 006D7620 (Rebirth-patched), CDib::Read / Write 006D5910 / 006D5AC0 (MFC TRY / CATCH).
- I11 (open): 0063EF60 / 0063E020 need modifier keys held during a click (harness step); ui.md section 6.

- X28 done 2026-10-09: LoadSky's check expected GetClassId 0x44 (the factory class; CSkyObject's is 0x20); UnitTextureRecords put items[1..6] in records 1..6, the original in 2..7. Both re-enabled.

- X29 done 2026-10-10: ProjectDecal 0056C750 pushed its matrices through PushMatrix (004D9B80's sum order) instead of the original's inlined push via 004D9880; both checks expected one texture coordinate per vertex, but the builder leaves the array at its reserved size with unwritten tail elements (ProjectDecal now compares the indexed vertices only). Both re-enabled.

## Vector math (planned 2026-10-09)

Vector and matrix maths is mostly written out by hand: x*x + y*y (+ z*z) forms in 25 files, 164 sqrt calls, 235 raw float[3] / [4] / [9] / [16] arrays, and four separate 3-float structs (maths.h Vector3, ground_decal.h, render/render_internal.h, ui_camp.h) plus chunk_stream.h's Float4. Goal: one set of types and named operations, used everywhere, without changing results.
Constraints: the original is MSVC6 x87 code that inlined this maths, so the order of operations decides the rounding. Every helper is __forceinline and evaluates in a fixed, documented order (x, y, z left to right); where the original uses another order, the helper is a named variant, never a silent reorder. Side-by-side checks that compare floats exactly must still pass; CheckNear tolerances are not widened to make a migration pass. The x87 __asm helpers are a separate ticket.
- V1, V2, V3 part 1, V3 part 2 done 2026-10-09 (507 -> 377). Next: vector-math.md "V3 status" (2D sums on `position`, batch 3; the members left).

## Standalone exe and UTF-8 (planned 2026-10-13; docs/notes/standalone.md)

Not started; both wait for the port. Counts are on the dashboard ("Standalone readiness").
- S: first standalone exe with our own layout-compatible MFC / STLport / CRT classes (6-8 tickets), data definitions and vtables (4-6), direct calls, entry and link (3-4), first boot (2-4), old renderer code (3-4). Standard-library types come after the hook build is retired.
- U1-U5: UTF-8 inside, cp1251 assets converted when read, behind a switch in the standalone build (5-6 tickets). Saves and multiplayer need not match vanilla (user, 2026-10-13).

## Pending work

- Republish the dashboard and remove merged worktrees.
- Cleanup C6-C10 done 2026-10-09 (combat, unit, script, world, scene and renderer files). Left from C9: map_object_shadow.cpp's raw offsets (models.md, "Layouts").
- X27 (low): CPlayerClient::SendLeave04 state check mismatched once (1 of 44) in an effects_bow Rebirth run that also timed out; effects_bow passes alone. Watch.
- X22 flaky: render_present (vanilla) crashed once under 16 parallel runs, null call inside DDRAW.dll from render::CopyFrameTo <- DevicePresentVerify <- ShowLoadingMovie (2026-10-07). Passed 3/3 alone. Watch; if it recurs, guard CopyFrameTo against a lost surface.
- Drafts DR-C1 .. DR-C5, DR-H16 merged 2026-10-09. Open: function_sizes.inc needs a real regeneration once the Ghidra database is whole (Ghidra reported cut-short bodies, e.g. 0040D710 555 -> 60; only the handler rows from config/extra_function_sizes.csv were added by hand).
- Done: X17 (ground decal ClipPlane / AddTriangle / AddTerrainTriangle and CHudTopPanel::Build stay in tests/verify/disabled.txt), X23, X26, D3 .. D3d (core.md, "Shared containers").

## Cleanup (docs/cleanup-plan.md)

- C2 Phase 1 for the engine base and core networking (0x00400000-0x00440000): one declaration per class, members instead of At<>/offsets, virtual calls instead of VCall, named globals, duplicates merged; behaviour unchanged. Split per class family (C2d camera merged 2026-10-05); state and leftovers in cleanup-engine-base.md / cleanup-network.md.
- C3: the forwarders whose declarations differ between files (docs/originals-report.md "differ" group; 261 left after C3l, 209 of them already rewritten): decide per address which declaration is right (RET n, callers, Ghidra) and keep one in originals_<area>.h; next the ui_* sites, then calling the rewritten ones directly. Batches of 50.
- C5 C4 leftovers: UnitStream, SaveStream, FileStream, the unit_ai stream, verify_magic_trap's wrapper, the other per-file Text(0x...) helpers (docs/notes/cleanup-engine-base.md).
