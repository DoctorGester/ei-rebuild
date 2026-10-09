# Ticket queue

D1 done 2026-10-05: docs/notes 1.38 MB -> 0.46 MB (current-state references; history in git). D2 queued: retarget ~150 code comments that cite removed section names ("Nth round", "pass", renderer section numbers) to the file / new section; tools/progress.py misses UI_INPUT_HANDLER / UI_WHEEL_HANDLER registrations.

From 2026-10-05 (user): workers own one area for about a usage session ("area tickets"), commit every
3-5 functions with all checks green, and the coordinator merges the branch head periodically and runs
the suite. Forwarder cleanup batches are 50.

Mark a ticket taken by adding the date; remove it when merged.

## Renderer (src/render, docs/notes/renderer.md)

- R8 (first part merged 2026-10-05; second part taken: shadow textures, .mmp/.bmp textures without DirectDraw) Device setup and texture management (last). Includes R7's leftovers: 004EFA60 texture holders (save/load pictures, weather texture, item pictures) via the texture manager 0068F6C0..00693C60; presenting without the old Flip/BltFast.

## Units and combat, 0x00500000-0x00540000 (docs/notes/units.md, combat.md)

- A-U done 2026-10-05 (40 functions). A-U2 done (40). A-U3 done (20). A-U4 done (28). A-U5 done (39 + 5 dead). A-U6 done (105). A-U7 done (37 + 45 library; head 7e22909 reconciled with A-U6, merge pending after reboot). Was ( CUnitServer ctor 00545580, AIBrain ctor 00545990, 005457E0, 00545CB0, 00545110).
- U2 done (00535850 ClientPlaceOnPath, 0052FCC0 / 0052FDF0 in follow-up for a crash).

## World, AI and server, 0x00540000-0x005E0000 (docs/notes/world.md, ai.md, unit_ai.md)

- A-W done 2026-10-05 (33 functions; leftovers listed in unit_ai.md / world.md). A-W2 cut off (4 merged). A-W3 cut off (nothing committed). A-W4 done (36). A-W5 done (39): 0x005A0000-0x005E0000 (paths, world client) plus A-W's leftovers. A-W6 done (24). A-W7 done (11 + 9 library). A-W8 done (15; head 670008d includes A-W7, merge pending after X25; next: client 00566360 day lights, 0056C750, 0056D150).
- A-W done 2026-10-05 (33 functions; leftovers listed in unit_ai.md / world.md). A-W2 cut off (4 merged). A-W3 cut off (nothing committed). A-W4 done (36). A-W5 done (39): 0x005A0000-0x005E0000 (paths, world client) plus A-W's leftovers. A-W6 done (24). A-W7 done (11 + 9 library; next in unit_ai.md, "A-W7").
- A-W done 2026-10-05 (33 functions; leftovers listed in unit_ai.md / world.md). A-W2 cut off (4 merged). A-W3 cut off (nothing committed). A-W4 done (36). A-W5 done (39): 0x005A0000-0x005E0000 (paths, world client) plus A-W's leftovers. A-W6 done (24). A-W7 done (11 + 9 library). A-W8 done (15; next in unit_ai.md, "A-W8": client 00566360 / 0056C750 / 0056D150).

## App and startup, 0x00440000-0x00480000


## Interface, 0x005E0000-0x00640000 (docs/notes/ui.md)

- A-I (cut off by the usage limit; 12 functions merged). A-I2 done 2026-10-05 (36 functions). A-I3 done (27). A-I4 cut off (12 merged). A-I5 cut off (4 merged). A-I6 done (29). A-I7 done (21). A-I8 done (21). A-I9 done (12; Mid/Right fixed). A-I10 done (15; ui_base_dialogue reaches the dialogue). A-I11 done (9). A-I12 done (3). A-I13 done. A-I14 (taken 2026-10-07) (briefing Create, Update 00604130) (006064E0, 00604C50, 00608450, briefing Create / Update) (next: hero screen 006159E0 / 00617820 / 00615850, camp deals 00611220 / 00611760, main menu destructor 00633260). A-I14 done (14). A-I15 done (47 + 126 library). A-I16 done (125 + 26 library; head 2078944, merge pending; next: HUD input thunks 0064BCF0.., sprite draw 005E6D90, cursor loading 005E60B0/005E63F0).
- A-I (cut off by the usage limit; 12 functions merged). A-I2 done 2026-10-05 (36 functions). A-I3 done (27). A-I4 cut off (12 merged). A-I5 cut off (4 merged). A-I6 done (29). A-I7 done (21). A-I8 done (21). A-I9 done (12; Mid/Right fixed). A-I10 done (15; ui_base_dialogue reaches the dialogue). A-I11 done (9). A-I12 done (3). A-I13 done. A-I14 (taken 2026-10-07) (briefing Create, Update 00604130) (006064E0, 00604C50, 00608450, briefing Create / Update) (next: hero screen 006159E0 / 00617820 / 00615850, camp deals 00611220 / 00611760, main menu destructor 00633260). A-I14 done (14). A-I15 done (47 + 126 library). A-I16 done (125 + 26 library; next: HUD input thunks 0064BCF0-0064BD80, sprite draw 005E6D90, cursor loading 005E60B0 / 005E63F0, HUD column clicks 0063DB20 / 0063E020 / 0063EF60, 00631CC0, 006298B0 / 00628B50, lobby last).


- I11 Next interface functions (ui.md, battle screen section); 0063EF60 / 0063E020 need modifier keys held during a click (harness step).

## HUD, battle screen, spells and items, 0x00640000-0x006C0000

- A-H .. A-H13 done. A-H15 taken (client senders 0066AA20/0066AB80/0066B080..0066B510/0066A280, server 0066C1E0..0066D490, sound ctor/dtor 006B6040/006B65D0, 006BA9C0, save screen 006537E0/00653800/00654490). A-H14 done (54 + 16 library; merged, regressions in X23) (sound.md next candidates; CPlayer 00664BF0..00665D20, save screen, message handlers).


## Scripts, 0x004C0000-0x00500000 (docs/notes/script.md)

- A-S done 2026-10-05 (40 functions in 0x004B0000-0x004C0000). A-S2 (cut off by the usage limit; 20 functions merged). A-S4 done (37; 78 quantiser functions marked dead). A-S5 done (28; 25 library, 5 dead). A-S6 done (24). A-S7 done (33). A-S8 done 2026-10-07 (20; 31 library, 33 dead; next candidates in script.md "Next candidates in this range"). A-S3 done 2026-10-05 (38 functions; leftovers: .mob reader 00481660 / 00481F00, particle attach 004849F0 / 00484C00, colour quantisation 0049BEA0). Was: scripts 0x004C0000-0x00500000 plus the 0x00480000-0x004B0000 leftovers.


## Pending work (2026-10-07, waiting for a reboot)

Builds and the suite can't run until the PC is rebooted (memory: process-zombie leak). After the reboot, in this order, running the suite after each step (16 jobs again once the memory is back):
1. Recheck main as it is: A-U6 (merged as 73d3d8b) was not fully tested. 3 Rebirth render scenarios failed on memory even at --jobs 1.
2. Merge A-I16 interface: branch worktree-agent-a7ce999abb7f598a8, head 2078944 (125 functions + 26 library).
3. Merge A-H15 players/sound: worktree-agent-a4c370962466aabe8, head 61301bf (29 + 11 library).
4. Merge A-S9 scripts: worktree-agent-a51e508a53d867bc4, head db11956 (20 + 39 library).
5. Merge A-U7 units: worktree-agent-a598d0f2ff4a3dcaf, head 7e22909 (37 + 45 library; already reconciled with A-U6).
6. Build, fix and merge the drafts (section "Draft passes") in this order: DR-C2 (slot names; fix the void return types the others need), DR-C3 (after A-U7), DR-C1, DR-C4, DR-C5, DR-H16. Each has notes in docs/notes/drafts/.
7. Republish the dashboard. Remove worktrees whose branches are merged.

## Draft passes (no build, 2026-10-07)

The PC can't build or run the suite until a reboot (process-zombie leak, memory notes). These tickets write code on their own branches without building and leave a notes file docs/notes/drafts/<ticket>.md saying what is drafted, what is unverified and where they stopped. A later worker builds, fixes and finishes each one.
- DR-C1 (drafted: world_units.cpp and world_client_maps.cpp clean, world_game.cpp half (At<> 155 -> 80; stopped at the network sessions +0x25A0..+0x261C); head 86931a0, branch draft-dr-c1 in worktree agent-a9b8a6df2bce6075b). Overlaps DR-C2: both named CMapObject slot 29, CWorld 18/24, CUnitController 29 and 41-43; reconcile when merging (DR-C2 first, then take its names): world_units.cpp done, world_client_maps.cpp partly, head 064e0d0; part 2 taken for the rest; branch draft-dr-c1 in worktree agent-a9b8a6df2bce6075b). Overlaps DR-C2: both named CMapObject slot 29 (IsDynamicSlot vs IsSynchronised), CWorld 18/24 and CUnitController 29; reconcile when merging (DR-C2 first, then take its names): cleanup draft of world_client_maps.cpp, world_units.cpp, world_game.cpp (At<>/Field<> 470, VCall 106 between them).
- DR-C2 (drafted, 102 slots named; branch worktree-agent-a08350e7d77d16777, head c05589b; low-confidence names in its notes): name the placeholder virtuals (SlotNN) in map_object.h, unit.h, world.h, map_object_figures.h, party.h (102 of 123) and update their callers.
- DR-C3 (drafted: all 3 files, 14 VCalls left in net_state for DR-C2's slot names; head 52430cc; its CUnitServer members overlap A-U7's (queue +0x24, savedQueue +0x148, itemSpells/armourSpells +0x91C, voice +0xB10): merge A-U7 first and take its names; branch draft-dr-c3 in worktree agent-a2cd3e0425537b848): cleanup draft of unit_motion_tick.cpp, combat_motion.cpp, net_state.cpp (At<>/Field<> 283, VCall 28+).
- DR-C5 (drafted: ui_surface_create.cpp clean, ui_hud.cpp 6 VCalls left (map object slots 38/39/42 need DR-C2's names with int returns), world_client_draw.cpp 4 VCalls left; head 38582dd; written against DR-C1's CWorldClient names, needs `CCamera* camera_` at CWorldClient +0x210 after DR-C1 (notes); branch draft-dr-c5 in worktree agent-abcd1eeb0aab855a0): cleanup draft of ui_hud.cpp, ui_surface_create.cpp, world_client_draw.cpp.
- DR-C4 (drafted: 3 files clean, head 976571a; needs DR-C2 first with signatures CWorld::PlayerList()->CEngineObject* (20), ItemFactory()->CEngineObject* (18), CUnitController::RefreshVitals(char) (19), CParty::ItemConstructor()->CItemConstructor* (16); 13 server handlers missing from functions.csv listed in its notes (0066F890 login, 0066FA90, 00670CE0, 0066FAC0, 006708D0, 00670820, 00670960, 006709D0, 00670810, 00670A40, 00670A90, 0066FB70, 00670B20); branch draft-dr-c4 in worktree agent-a116a5a6e1c9b6cb7): cleanup draft of world_savefile.cpp, world_save.cpp, item_stock.cpp (VCall 59 between them, plus their At<>/Field<>); then the server message handler table, looking for handlers missing from functions.csv as DR-H16 found on the client side.
- DR-H16 (drafted: 9 functions + wrappers, 23 missing client handlers added to functions.csv; head 3013405; needs function_sizes.inc regenerated with Ghidra running; branch draft-dr-h16 in worktree agent-a3e4cab37d7cedfc9): porting draft, options/keyboard screens 00651940 / 00651350 / 00651600 / 00652200 / 00651D70 / 006524A0, widgets 0065B8B0 / 0065D150 / 0065C610; plus the client message handlers missing from config/functions.csv (objects.md).

## Verify checks


- X24 note: with the Windows session locked, 16 parallel runs give random D3D11 texture failures, C++ exceptions (E06D7363) and step-0 hangs; 6 at once is clean (132 runs ALL PASS at 7b1ec35). Use --jobs 6 while locked; after hours locked even 6 fails (crashes, step-0 hangs), 3 is clean (X25, A-W7 was not the cause).

- X23 (done 2026-10-07: title keys "zone "/"allod ", DeleteSave check via the game import, harness quit only from WinMain's loop; rebirth_flags / render_scene / render_movie_present crashes not traced to A-H14, re-run) A-H14 merge regressions (main 9d61b1d): magic_traps both (game did not exit 5 s after quit), script_fx both, ui_saveload / ui_saveload_delete both, rebirth_flags rebirth (crash E06D7363), render_scene vanilla (crash C0000005 in DDraw), render_movie_present rebirth (hang). Mismatches: CSaveLoadWindow::DeleteSave 2/2, SaveEntry::AllodTitle 34/34, SaveEntry::ZoneTitle 82/82.

- X22 flaky: render_present (vanilla) crashed once under 16 parallel runs, null call inside DDRAW.dll from render::CopyFrameTo <- DevicePresentVerify <- ShowLoadingMovie (2026-10-07, after A-S7). Passed 3/3 alone. Watch; if it recurs, guard CopyFrameTo against a lost surface.



- X17 (done 2026-10-07, branch worktree-agent-a480cc2226016ece2) Four rewrites in tests/verify/disabled.txt: A-S5's ground decal builder 004D7BD0 ClipPlane / 004D85F0 AddTriangle / 004D8490 AddTerrainTriangle (mismatch e.g. "vertices 0 -> 3 / 3, indices 0 -> 3 / 3", 0.03-6% of calls) and A-H11's 00649AF0 CHudTopPanel::Build (its check fails every call: "page 0 shown 0 regions 4 sprites 6").






## Cleanup (docs/cleanup-plan.md)

- D3/D3b/D3c done 2026-10-07: per-file containers 155 -> 6. D3d (done 2026-10-07, 6 -> 0): the last 6 (scene_loader ArrayBody/VtArray family) and the inlined SetSize in ui_window, map_spec, party_lifetime, unit_skins CopyRecords (docs/notes/core.md, "Shared containers").

- C2 Phase 1 for the engine base and core networking (0x00400000-0x00440000, fully ported): one
  real declaration per class, members instead of At<>/offsets, virtual calls instead of VCall,
  globals instead of addresses, duplicates merged; behaviour unchanged. Split per class family:
  - (C2d camera merged 2026-10-05.)
- C3b (top 10, C3c-C3e and C3f, the next 15 by site count each; C3g merged; C3h merged; C3i merged; C3j merged; C3k merged (283 "differ" left); C3l done 2026-10-05 (261 "differ" left: 209 already rewritten, 52 not yet rewritten of which 41 wait for a ui_* site once I10 is merged and 11 keep a src/lib copy; notes list the ui_* sites to convert, so the next C3 ticket is those, then calling the rewritten ones directly); batches are 50 from now on (user, 2026-10-05)) The 504 forwarders whose declarations differ between files (docs/originals-report.md "differ"
  group): decide per address which declaration is right (RET n, callers, Ghidra), keep one in the
  shared originals_<area>.h header. Start with the top 10 by site count (0x006E727B, 0x00455950,
  0x0040C720, ...), then by area. C3 first part (shared headers, 38 void forwarders typed) merged 2026-10-05.
- C5 C4 leftovers: UnitStream, SaveStream, FileStream, the unit_ai stream, verify_magic_trap's wrapper, the other per-file Text(0x...) helpers (docs/notes/cleanup-engine-base.md).
