# Ticket queue

From 2026-10-05 (user): workers own one area for about a usage session ("area tickets"), commit every
3-5 functions with all checks green, and the coordinator merges the branch head periodically and runs
the suite. Forwarder cleanup batches are 50. Mark a ticket taken by adding the date; remove it when merged.

Docs: D1 done 2026-10-05; trims 2026-10-09 (notes 0.82 -> 0.57 MB, then the per-ticket sections of C6-C10, A-H17..A-H21,
A-I19..A-I21, A-U8, A-W9, A-X1 / A-X2, V2 / V3 folded into the area tables). D2 queued: retarget ~150 code comments that
cite removed section names ("Nth round", "pass", renderer section numbers, items_spells.md "(A-H17)") to the file / new
section; tools/progress.py misses UI_INPUT_HANDLER / UI_WHEEL_HANDLER registrations.

## Renderer (src/render, docs/notes/renderer.md)

- R9a2 merged 2026-10-14: the five present scenarios pass on vanilla and --native (movie holders, video-memory pictures, the harness activates a game without DirectDraw). Present under Rebirth still crashes (not in the suite; R9c). R9b: the 73 calling places in the old-device log.
- R8 merged (both parts; textures without DirectDraw in the switch-over, renderer.md section 16).
- R9 (user 2026-10-14: in parallel with the cleanup; starts when C2d's device-file cleanup is merged) our own swap chain, dgVoodoo gone, renderer.md section 9, one Opus ticket at a time: R9a device setup without DirectDraw / Direct3D in the switch-over (the device object filled for the game, the fixed texture format table, the screen format from the colour depth) and `render::Present` on a flip-model swap chain on the game window, plus a run config without dgVoodoo; R9b every old-device call left guarded for a device object without a device (524 sites in 143 functions); R9c movies and the loading screen through Present, full screen / resize, gamma as a shader step, lost device / WM_ACTIVATE, the software cursor between frames. Then native UI drawing window by window.

## Area tickets (done; next candidates)

Priority (user, 2026-10-13): finish the port; three area workers at once, refilled by code left; cleanup and V3 wait. Rebirth-patched functions get ported too (hooked in vanilla only), after the unpatched ones in each area.

- Units and combat 0x00500000-0x00560000 (units.md, combat.md, map_objects.md): A-U .. A-U11 done. A-U12 merged 2026-10-13 (3D object halves, map object slots, manager, renderer base, unit small methods, controller slots, the Rebirth-patched unit / skill database loads). Left: 30 tiny world slots 0055E1B0-0055FDC0 (A-W14).
- World, AI, server 0x00540000-0x005E0000 (world.md, ai.md, unit_ai.md): finished 2026-10-13 (A-W .. A-W15; the Rebirth-patched loaders vanilla only; static tables checked once at hook time by EI_STARTUP_CHECK). Left: 46 functions under 5 bytes.
- Interface 0x005E0000-0x00640000 (ui.md): A-I .. A-I21 done. A-I22 merged 2026-10-13: the range is finished (including the Rebirth-patched ones, vanilla only); 31 RET stubs under 5 bytes are left. Unchecked by any scenario: the connection screen Create / list draw, exchange slots 0062D100 / 0062D200.
- Engine base, network, app, script runtime 0x00400000-0x004C0000 (core.md, app.md, network.md): finished 2026-10-14 (A-E1, A-E2: WinMain 00456440 vanilla only, static initialisers by start-up checks, MFC try / catch through src/lib/mfc_try.h). Left: 69 under 5 bytes. No scenario drives: WinMain's second-instance, disc prompt and frame-recording branches, 0041ECC0, 00443080, CDib's catch handlers.
- HUD, spells, items 0x00640000-0x006C0000 (items_spells.md): finished 2026-10-13 (A-H .. A-H26, the Rebirth-patched ones vanilla only). Left: 006462A0 (unit panel draw, drives the D3D device), 0066D730 (uninitialised byte, kept original), 0067E4E0 (before the hooks), functions under 5 bytes.
- Scripts and old renderer 0x004C0000-0x00500000 (script.md, renderer.md section 12): finished 2026-10-14 (A-S .. A-S13; the direct D3D7 / DirectDraw calls ported as they are, the interfaces stay the boundary). Left: 8 under 5 bytes.
- CRT / MFC range 0x006C0000-0x00740000 (models.md): finished 2026-10-13 (A-X1 .. A-X6; 006C68B0, 006D0630, 006D7620 by A-H26). CDib::Read / Write by A-E2.
- A-Z1 merged 2026-10-14: the last eight (three sector draw-pass helpers, SendValue12, FootprintCellSet by start-up check, a thunk, two string addresses read as code). **The port is complete**: every hookable game function is rewritten (99.97% of game code); 201 stubs of 1-4 bytes wait for the standalone build. function_sizes.inc is hand-patched for two sizes (gen_function_sizes.py no longer reproduces it).
- X34 done by C6a 2026-10-14.
- I11 (open): 0063EF60 / 0063E020 need modifier keys held during a click (harness step); ui.md section 6.

- X28 done 2026-10-09: LoadSky's check expected GetClassId 0x44 (the factory class; CSkyObject's is 0x20); UnitTextureRecords put items[1..6] in records 1..6, the original in 2..7. Both re-enabled.

- X29 done 2026-10-10: ProjectDecal 0056C750 pushed its matrices through PushMatrix (004D9B80's sum order) instead of the original's inlined push via 004D9880; both checks expected one texture coordinate per vertex, but the builder leaves the array at its reserved size with unwritten tail elements (ProjectDecal now compares the indexed vertices only). Both re-enabled.

## Unknown fields (user 2026-10-14: to 0, each named or unused)

tools/check_cleanup.py counts `unknown field` (members and locals named unknownXX / unknown_XXX: 727 in 97 files, 2026-10-14) and `unused field accessed` (must stay 0). Each unknown field becomes:
- a name for what it holds, from the code that reads and writes it (a field only written and saved or sent is named by what is written);
- or `unused_XXX` (same offset suffix, same type and size) when no code in src reads or writes it apart from construction / memset / copy of the whole object. Byte gaps that are partly used are split into named members and unused gaps. Rebirth's DLL may still read a field we call unused; the name speaks for game.exe only.
Layout never changes (static_asserts / offsetof checks stay); renames only, so the builds, checks and suite decide. Notes go into the area's cleanup-*.md only where a meaning was found that the code alone does not show.
- F1 world, combat, effects, spells (~180): world.h, combat.h, spell.h, effects.h, world_effect.h, sky_object.h, lightning / trap / lever / cage, particle_emitters.cpp.
- F2 units, AI, items (~175): unit.h, unit_db.h (+ unit_db_destroy.cpp), unit_ai.h, ai_map.h, ai_*, item_db.h, item*.h/.cpp, unit_looks.h, unit_client_*, class_templates.h, party*.
- F3 interface (~125): ui.h, ui_camp.h, ui_screens.h, ui_briefing.h and the other ui_* files.
- F4 scene, figures, terrain, map objects, textures (~140): scene_loader.h/.cpp, map_object_figures.h, terrain_sector.h, terrain*, figure*.h/.cpp, camera.h, texture.h, map_object*.h/.cpp, link_figure.h, object_tables.h, material.h, model.h, renderer_lines.h.
- F5 the rest (~105): map_spec.h, net_*, player_*, sound*, script*, app_*, bink_video.cpp, quest_records.h, base_manager.h, src/lib, everything else left.

## Vector math (planned 2026-10-09)

Vector and matrix maths is mostly written out by hand: x*x + y*y (+ z*z) forms in 25 files, 164 sqrt calls, 235 raw float[3] / [4] / [9] / [16] arrays, and four separate 3-float structs (maths.h Vector3, ground_decal.h, render/render_internal.h, ui_camp.h) plus chunk_stream.h's Float4. Goal: one set of types and named operations, used everywhere, without changing results.
Constraints: the original is MSVC6 x87 code that inlined this maths, so the order of operations decides the rounding. Every helper is __forceinline and evaluates in a fixed, documented order (x, y, z left to right); where the original uses another order, the helper is a named variant, never a silent reorder. Side-by-side checks that compare floats exactly must still pass; CheckNear tolerances are not widened to make a migration pass. The x87 __asm helpers are a separate ticket.
- V3b merged 2026-10-14 (Opus): unrounded 2D helpers, 30 sites (369 -> 339); left: volatile sums, UpdateFollow (register allocation), unit_ai_moves:150.
- V3 batch 3 tried on Sonnet 2026-10-14: nothing migrated (370 left). The 2D sums on `position` are mostly double (kept on the FPU stack), and the float helpers add store / load round trips at the 6 float sites. Needs unrounded 2D helper variants first: Opus.
- V1, V2, V3 part 1, V3 part 2 done 2026-10-09 (507 -> 377). Next: vector-math.md "V3 status" (2D sums on `position`, batch 3; the members left).

## Standalone exe and UTF-8 (planned 2026-10-13; docs/notes/standalone.md)

First, before any S ticket: the verification loop SV1-SV5 (docs/notes/standalone-verification.md; user, 2026-10-14).

Not started; both wait for the port. Counts are on the dashboard ("Standalone readiness").
- S: first standalone exe with our own layout-compatible MFC / STLport / CRT classes (6-8 tickets), data definitions and vtables (4-6), direct calls, entry and link (3-4), first boot (2-4), old renderer code (3-4). Standard-library types come after the hook build is retired.
- U1-U5: UTF-8 inside, cp1251 assets converted when read, behind a switch in the standalone build (5-6 tickets). Saves and multiplayer need not match vanilla (user, 2026-10-13).

## Pending work

- Republish the dashboard and remove merged worktrees.
- Cleanup C6-C10 done 2026-10-09 (combat, unit, script, world, scene and renderer files). Left from C9: map_object_shadow.cpp's raw offsets (models.md, "Layouts").
- X32 (watch): several Rebirth runs print "game still running 10 s after the scenario quit" since 2026-10-13 (exit path; runs pass). Suspects: wrappers that now fire at exit (PartyManager::Destruct, mouse destructor, UnitShapeCache / SmoothTable Destruct).
- X35 (fixed 2026-10-14): Windows crashed twice (NVIDIA driver hang, bugcheck 0x133) with 24 harness games at once; run_all now caps the machine at 16 (EI_HARNESS_SLOTS).
- X30 (watch): sandbox_actions (vanilla) failed once of three full runs (combat-vanilla did the same once) on 2026-10-13, `waitfor Not(IsAlive(Enemy1))` timed out, 0 mismatches; passes alone. world-vanilla did the same once on 2026-10-14 (AlarmTime check; passes alone 3 of 3).
- X27 (low): CPlayerClient::SendLeave04 state check mismatched once (1 of 44) in an effects_bow Rebirth run that also timed out; effects_bow passes alone. Watch.
- X22 flaky: render_present (vanilla) crashed once under 16 parallel runs, null call inside DDRAW.dll from render::CopyFrameTo <- DevicePresentVerify <- ShowLoadingMovie (2026-10-07). Passed 3/3 alone. Watch; if it recurs, guard CopyFrameTo against a lost surface.
- Drafts DR-C1 .. DR-C5, DR-H16 merged 2026-10-09. Open: function_sizes.inc needs a real regeneration once the Ghidra database is whole (Ghidra reported cut-short bodies, e.g. 0040D710 555 -> 60; only the handler rows from config/extra_function_sizes.csv were added by hand).
- Done: X17 (ground decal ClipPlane / AddTriangle / AddTerrainTriangle and CHudTopPanel::Build stay in tests/verify/disabled.txt), X23, X26, D3 .. D3d (core.md, "Shared containers").

## Cleanup (docs/cleanup-plan.md)

Next after the port is finished and before the standalone tickets (user, 2026-10-14). Judge each ticket: the simple mechanical ones go to Sonnet workers (`model: "sonnet"`), as many as possible; type recovery and tricky debugging stay on Opus.
Split by model (counts from tools/progress.py, 2026-10-14):
- Sonnet, mechanical, checked by the build scripts and the suite: C3m merged 2026-10-14 (73 of 75; duplicates 313 -> 240); C6a merged (9 struct names, 74 -> 64); C3n batches 1-5 and C3o / C3p merged: duplicate forwarders 313 -> 0 (2026-10-14; keep at 0); C2s batches 1-3 merged (offset access 290 -> 69, 237 -> 25, 159 -> 39; what remains needs type decisions, Opus); C8a / C8b merged (55 + 123 shims into members); C9a merged (per-file vector/map/set structs 41 -> 21; 11 of the 21 are not containers: CellOffset, SpiralOffset, CRegionMap, FootprintCellSet, LinkFigureSet, CacheMap, TextureSet, UnitSoundSet, ObjectIdMap, PlatformSet, UnitSet); C3n the rest of those that differ where the address is already rewritten (the rewritten function's declaration decides; batches of 50); X34; `__fastcall Fn(T* self, void*)` shims into members (1,281) per file; `constexpr uintptr_t kX = 0x00...` (371) into the subsystem's named globals; per-file `At()` copies (60) replaced by the shared one (C7a).
- Opus: C2m map object family merged 2026-10-14 (offsets 30 -> 0, VCall 30 -> 3; leftovers in cleanup-map-objects.md); C2u unit family merged (offsets 44 -> 0, VCall 22 -> 0; CUnit derives from CMapObject; leftovers in cleanup-units.md); C2i item family merged (offsets 22 -> 0, VCall 16 -> 0; cleanup-items.md); C2ui interface screens batch 1, C2e (world effects, spells), C2ui batch 1 (interface screens; hero rename field fixed) and C2w (world server / client, combat) merged (cleanup-effects.md, cleanup-ui.md, cleanup-world.md); C2ui batch 2 (rest of the interface), C2f (figures, models), C2a (AI) and C2n (network; cleanup-network.md) merged; C2sc (script, console; cleanup-script.md) and C2ms (map specs, unit stats, fake unit, class templates; cleanup-units.md) merged; C2d (device layer, old renderer; cleanup-device.md) and C2z (app + the scattered rest of the widescreen gate) merged 2026-10-14: offsets, VCall, *At views, SlotNN and containers at 0 across src, byte arithmetic 4 (src/render, R9a's) (C2z: also QueryVector as stlp::vector<Query> plus its dword, the last per-file container, and the 8 placeholder SlotNN virtuals named); hand-written vector maths to 0 (332; V3 batches per area, vector-math.md "V3 status"): V3c (unit / ai / combat), V3d (ui / camera / world) and V3e (particles, terrain, scene, figures, map objects, core; 165 -> 40) merged; left: the 40 remaining sites and SectorPosition (render_terrain.cpp, after R9a2); C2 per class family (members instead of At<> / Field<>, VCall into virtual calls, unknown_XXX fields: 975 declared, about 360 never read); C3 addresses that differ and are not rewritten (RET n decides); V3 (the remaining sites need new unrounded helper variants; vector-math.md "V3 status"); `__asm` blocks (224) and float_control files (82); anything that turns a check red.
- Later, with the standalone work (changes what the verify wrappers see): the 2,377 forwarders to rewritten code into direct calls.
- Gate for widescreen (user, 2026-10-14): queue the WideScreen Rebirth patch (docs/notes/rebirth.md, 100 sites, ~4 Opus sessions plus a wide-resolution Rebirth run config) once these reach zero: duplicate forwarders, offset access (At<>/Field<>), VCall slot calls, byte offset arithmetic, *At view structs, per-file containers.
- Rule for Sonnet tickets: spelling only, never a signature or grouping change; skip and list what does not fit; I review the first diff of each kind before sending more.

- C2 Phase 1 for the engine base and core networking (0x00400000-0x00440000): one declaration per class, members instead of At<>/offsets, virtual calls instead of VCall, named globals, duplicates merged; behaviour unchanged. Split per class family (C2d camera merged 2026-10-05); state and leftovers in cleanup-engine-base.md / cleanup-network.md.
- C3: the forwarders whose declarations differ between files (docs/originals-report.md "differ" group; 261 left after C3l, 209 of them already rewritten): decide per address which declaration is right (RET n, callers, Ghidra) and keep one in originals_<area>.h; next the ui_* sites, then calling the rewritten ones directly. Batches of 50.
- C5 C4 leftovers: UnitStream, SaveStream, FileStream, the unit_ai stream, verify_magic_trap's wrapper, the other per-file Text(0x...) helpers (docs/notes/cleanup-engine-base.md).
