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

- Units and combat 0x00500000-0x00560000 (units.md, combat.md, map_objects.md): A-U .. A-U11 done. A-U12 merged 2026-10-13 (3D object halves, map object slots, manager, renderer base, unit small methods, controller slots, the Rebirth-patched unit / skill database loads). Left: 30 tiny world slots 0055E1B0-0055FDC0 (A-W14).
- World, AI, server 0x00540000-0x005E0000 (world.md, ai.md, unit_ai.md): finished 2026-10-13 (A-W .. A-W15; the Rebirth-patched loaders vanilla only; static tables checked once at hook time by EI_STARTUP_CHECK). Left: 46 functions under 5 bytes.
- Interface 0x005E0000-0x00640000 (ui.md): A-I .. A-I21 done. A-I22 merged 2026-10-13: the range is finished (including the Rebirth-patched ones, vanilla only); 31 RET stubs under 5 bytes are left. Unchecked by any scenario: the connection screen Create / list draw, exchange slots 0062D100 / 0062D200.
- Engine base, network, app, script runtime 0x00400000-0x004C0000 (core.md, app.md, network.md): A-E1 merged 2026-10-13. A-E2 taken 2026-10-13 (everything listed here, 006462A0, the static initialisers of the scripts range). Left: 004560D0 (flips the surface to GDI), 0041ECC0 and 00443080 (Rebirth-patched), 00456440 (application start-up, Rebirth-patched, 4.8 KB), 00458870 and 00444740 (static-initialiser constructors), 69 under 5 bytes.
- HUD, spells, items 0x00640000-0x006C0000 (items_spells.md): finished 2026-10-13 (A-H .. A-H26, the Rebirth-patched ones vanilla only). Left: 006462A0 (unit panel draw, drives the D3D device), 0066D730 (uninitialised byte, kept original), 0067E4E0 (before the hooks), functions under 5 bytes.
- Scripts and old renderer 0x004C0000-0x00500000 (script.md, renderer.md section 12): A-S .. A-S11 done (A-S11 merged 2026-10-13: 173 left classified, 70 rewritten, 31 to the CSVs). Taken 2026-10-13: A-S12 (CLinkMesh slots, CRenderHelper, fog draw, weather buffers, mesh bounds, pipeline constructor), A-S13 (device object, DirectDraw holder, rain / snow draw: direct D3D7 / DirectDraw calls ported as they are, the interfaces stay the boundary). Left after them: static initialisers and what only they reach (A-E2), 004D5880 (MFC TRY / CATCH), 8 under 5 bytes.
- CRT / MFC range 0x006C0000-0x00740000 (models.md): finished 2026-10-13 (A-X1 .. A-X6; 006C68B0, 006D0630, 006D7620 by A-H26). Left: CDib::Read / Write 006D5910 / 006D5AC0 (MFC TRY / CATCH).
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
- X32 (watch): several Rebirth runs print "game still running 10 s after the scenario quit" since 2026-10-13 (exit path; runs pass). Suspects: wrappers that now fire at exit (PartyManager::Destruct, mouse destructor, UnitShapeCache / SmoothTable Destruct).
- X30 (watch): sandbox_actions (vanilla) failed once of three full runs (combat-vanilla did the same once) on 2026-10-13, `waitfor Not(IsAlive(Enemy1))` timed out, 0 mismatches; passes alone.
- X27 (low): CPlayerClient::SendLeave04 state check mismatched once (1 of 44) in an effects_bow Rebirth run that also timed out; effects_bow passes alone. Watch.
- X22 flaky: render_present (vanilla) crashed once under 16 parallel runs, null call inside DDRAW.dll from render::CopyFrameTo <- DevicePresentVerify <- ShowLoadingMovie (2026-10-07). Passed 3/3 alone. Watch; if it recurs, guard CopyFrameTo against a lost surface.
- Drafts DR-C1 .. DR-C5, DR-H16 merged 2026-10-09. Open: function_sizes.inc needs a real regeneration once the Ghidra database is whole (Ghidra reported cut-short bodies, e.g. 0040D710 555 -> 60; only the handler rows from config/extra_function_sizes.csv were added by hand).
- Done: X17 (ground decal ClipPlane / AddTriangle / AddTerrainTriangle and CHudTopPanel::Build stay in tests/verify/disabled.txt), X23, X26, D3 .. D3d (core.md, "Shared containers").

## Cleanup (docs/cleanup-plan.md)

- C2 Phase 1 for the engine base and core networking (0x00400000-0x00440000): one declaration per class, members instead of At<>/offsets, virtual calls instead of VCall, named globals, duplicates merged; behaviour unchanged. Split per class family (C2d camera merged 2026-10-05); state and leftovers in cleanup-engine-base.md / cleanup-network.md.
- C3: the forwarders whose declarations differ between files (docs/originals-report.md "differ" group; 261 left after C3l, 209 of them already rewritten): decide per address which declaration is right (RET n, callers, Ghidra) and keep one in originals_<area>.h; next the ui_* sites, then calling the rewritten ones directly. Batches of 50.
- C5 C4 leftovers: UnitStream, SaveStream, FileStream, the unit_ai stream, verify_magic_trap's wrapper, the other per-file Text(0x...) helpers (docs/notes/cleanup-engine-base.md).
