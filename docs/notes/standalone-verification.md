# Verifying the standalone image

The image has no original code to compare with at run time, so every check is recorded from the original game
beforehand: whole-run goldens, function vectors for what scenarios reach rarely, and frame references for Rebirth's
WideScreen. This file says how that works, what the harness does to make runs repeat, and the rules that came from
real differences. Per-ticket history: `git log --grep=<ID>`.

## 1. How verification works now

- **A rewrite is checked** by a golden scenario that reaches it or by function vectors that replay it (rewrite guide,
  "Verification"); `tools/coverage.py` says which (section 4).
- **The suite** is the image's: `python tools/work.py test <ID> --golden [run_all args]` plays install `standalone`
  (vanilla data, Rebirth's bug fixes on: the defaults) and compares every run that has a golden. Rebirth's data
  (`standalone-rebirth`, `config/ei_rebirth_standalone.ini`'s flags) is a smoke set run on demand (`--rebirth`; SV9).
  GOLDENS THE SAME / GOLDENS DIFFER sets the exit code.
- **Goldens come from the original**: `--no-hooks` runs of game.exe (ei_launch always sets EI_NO_HOOKS for it), with
  the harness's canonicalising patches (section 5); on the vanilla data with addon.dll's bug fixes only
  (docs/notes/rebirth.md, "One test profile"). A scenario the original cannot run (`# no original: <install>`)
  gets the image's own golden instead (`golden.py record --image`, section 2).
- **Heap jitter** (SV7a, SV7c, SV7c2; `EI_HEAP_JITTER=<seed>[,fill]`, `EI_HEAP_FILL`) moves the blocks of game.exe's heap and of the
  image's heap alike (the image's malloc is a VC6-style CRT heap on the same import slots, SV7c2): a golden must hold
  under every seed. Goldens are recorded from runs that agree plain and under jitter.
- **Function vectors**: `set EI_VECTORS=1` then `python tools/work.py test <ID> <scenarios> --no-hooks
  --vanilla-only`: the original code with the recorders as its only hooks (refused under addon.dll); `vectors.py
  merge`, replayed by `ei_vectors.exe` in build.cmd (section 3).
- **WideScreen references**: `run_all --widescreen --record` keeps addon's frames once (`<main checkout>\build\
  widescreen\<scenario>\<steps key>\`); `run_all --widescreen` compares the image's frames on the Rebirth install with
  them (docs/notes/rebirth.md; S21b in git for the tolerance study). Rebirth install only.
- **Out of sight and on loopback** (section 6): harness games never take the foreground, the image's windows are
  cloaked, every game binds and sends on 127.0.0.1 only. The report's `WINDOW:` and `NET:` lines and run_all's
  summary lines check it.
- **Repeat a run of the original with `--fresh`**: passed runs of the original are cached in `build\reference` by
  scenario commit and install, not by harness. After a harness or digest change, record and compare against fresh runs.

## 2. Goldens

**What a golden holds** (tests/golden/<scenario>/<install>/, tools/golden.py): golden.json (date, commit, digest period,
fields left out, how many runs agreed, the reason, `"source": "image"` for image goldens), console.txt, digest.txt and
dump_*.txt in compact form, and saves.txt. A difference names the record, the object and the group; for the fields behind
a hash, run the original again (it repeats) and `golden.py diff` the two full digests.

**The digest** (SV2; tests/verify/harness_digest.{h,cpp}; `digest <N>` step or `EI_DIGEST=<N>`, the suite uses 20,
`run_all.GOLDEN_PERIOD`): one record every N world updates, taken after the server update; each record a header (record,
world update, frame, tick, zone, hashes) and the fields that changed. `dump <name>` writes the whole state once. Groups:
- `world` (tick, clock, weather, alarms, zone flags, id map counts, diplomacy), `random` (ISAAC count / a / b / c and
  hashes; the main thread's rand() state), `player<id>` (money, items, members, variables), `obj<id>` (every server
  object: class, flags, place, effects, vitals; units also owner, relations, animation, movement, motion, controller and
  AI brain with modes by name). No pointer is written: objects by id, AI modes by name.
- digest 2 (SV2a): `timed<id>` (effects by class), `missile<n>` (CSpellEffect), `aievents` (the AI event table: always
  empty so far; nothing calls its creator 005C9630), `script` (instances, waiting runs, globals by name, FX sources,
  particles, lights, lightning; a randomized FX source's countdown is left out: stack leftovers), `client` and
  `cunit<id>` (client units: place, orientation, animation, packed vitals, flags).
- Compact form: world, generators and players field by field; each unit as `#place`, `#vitals`, `#ai`, `#unit`; other
  objects one `#`; script one hash per kind (`#run`, `#inst`, `#global`, `#gobject`, `#fx`, ...). A golden compares only
  the groups it has, so an older digest still compares.
- Left out on purpose: a unit's motion floats before its first path (never written).
- Scenarios that load a save take `dump loaded_<save>` once the load is done, so the loaded world is compared field
  by field (SV3).

**Saves** (SV7d; tools/savefile.py with savefile_objects / _unit / _items / _players / _scenario.py; `show`, `diff`,
`raw`, `rules`): saves\<slot>\scenario.sav (magic 0xDEAD: party manager, interface, bottom window, camera),
mission.sav (0x102: CWorldServer::SaveWorld 00597BD0), info.sav; shot.sav left out. SaveFileWrite 00579B80 frames
magic, version 0x74, checksum, a compressed flag and the LZ coder's output. Every reader names the function it follows;
a decode that does not end exactly at the data's end is an error. Entities by id, fields by name in file order. A
golden's saves.txt lists each save's path and the hash of its decoded text; the texts are in the shared store
tests/golden/saves/<hash>.txt.gz. No leave-out rule is in use any more (SV7b2a): every former leftover is canonical
(section 5). A save made over the briefing or global map has a 4-byte bottom window block (SV7d6). Not covered by the
suite's saves (decoded from the ported writers, unchecked against data): fake units, Tamed / Follow modes, armour
spells, carried items, plan steps, most timed effects, items 0x3006 / 0x3007, time-wheel events, script lights /
particles / FX sources / areas, party manager parties, saves\current\<zone>.sav.

**Recording from the original** (`--fresh`; at least two plain passes, better also EI_HEAP_JITTER 1 and `2,fill`):
```
set EI_DIGEST=20 & python tools\work.py test <ID> --no-hooks --fresh --golden <scenarios>   (each pass)
python tools\golden.py snapshot <scratch>\passN                                             (after each pass)
python tools\golden.py record --install vanilla --runs <scratch>\pass1 <scratch>\pass2 ... --reason "..."
python tools\golden.py record --install rebirth --runs ...
python tools\golden.py saves --runs <dirs>         (only the saves of existing goldens, digests kept)
```
`record` keeps a scenario only when every run agrees with the first in console, every digest record, every dump and
the decoded saves. It refuses a vanilla golden for a scenario with a `rebirthflags` line (SV7b8a: our WideScreen draws
parts the vanilla original clips, so the client unit flags differ by design). An intended change (a Rebirth fix behind
its flag, the UTF-8 switch) re-records the affected goldens and says why; anything else that differs is a regression.

**Image goldens** (S20a): `golden.py record --image` / `snapshot --image` take the image's run folders
(`<scenario>-standalone[-rebirth]`), accept only scenarios whose `# no original:` line names the install, and want at
least `IMAGE_RUNS` (4) agreeing runs, plain and under jitter. Such a golden says the image keeps doing what it did, not
that it does what the original does: render_cursor_present (V+R), render_present_shot (V+R), render_device_lost,
render_device_removed, render_present_window and render_present_fullscreen (vanilla). Their digests do not follow the renderer: the world moves
only with frames on the virtual clock, presents are counted on the main thread, a stall is one long frame.

**WideScreen references** (S21b; tools/run_all.py `record_widescreen`, `compare_widescreen`): `run_all --widescreen
--record [names]` plays each scenario as `<scenario>-rebirth-nohooks` (game.exe with addon.dll, dgVoodoo at the
desktop's mode, alone) and keeps its frames in `<main checkout>\build\widescreen\<scenario>\<steps key>\` (shared by
the worktrees; the key hashes the scenario's lines without comments, so a comment edit keeps the references; 3 keys
kept): `frames\<frame>.bmp.z`, `skip\<frame>.bmp.z` (what moved in addon's run between the frame and its `_later` /
`_moving` twins, grown by 8) and `recorded.json`. Not in git (4K frames). `run_all --widescreen [names]` runs
`<scenario>-standalone-rebirth` and compares each frame with `WIDESCREEN_COMPARE` (`--tolerance 8 --shift 1
--max-fraction 0.006 --block 64 --max-block 0.25`: `--shift 1` absorbs the renderers' sub-texel sampling and edges,
`--max-block` catches a moved element), leaving out the skip mask, what moved between the image's own twins and
`WIDESCREEN_AREAS` (the Save window's picture). Through
`work.py test` the comparison runs after the batch, like `--golden`. A missing recording, other steps or another frame
size fail with the command that records them; if a harness change moves what addon's frames show, every frame of a
scenario fails the same way: record again with `--fresh`. Vanilla data cannot be compared (its textures differ from
Rebirth's); `--widescreen --vanilla-only` is refused. A one-layout-unit shift of the right panels (2.7 pixels) fails.

**No golden by rule**: the `rebirthflags` scenarios on vanilla, the three watchdogs (meant to time out),
harness_disk_full. `golden.py KNOWN` / `SAVES_KNOWN` (fields kept in clear but left out of comparisons until a ticket
lands; `compare --strict` keeps them) are empty.

## 3. Function vectors

**Record mode** (`EI_VECTORS=1`, game.exe with `--no-hooks`): a family's recorders (`EI_RECORD(addr, recorder, stolen,
orig)` in tests/vectors/record_<family>.cpp; stolen bytes checked by check_stolen.py and check_signatures.py) copy the
arguments and the state the function reads, the x87 control word, call the original and copy the result and state
after, as a plain payload (tests/vectors/<family>_vectors.h) offered to a `Sampler`: repeats dropped by hash, a
fixed-seed reservoir of `cap` distinct calls per bucket (so a rare path is kept). The report writes
`<run>/vectors/<family>/<function>.vec`.

**Data** (tests/vectors/data/<family>/<function>.vec, checked in): `python tools/vectors.py merge <run folders>`
(repeats dropped, `cap` per bucket by a stable hash), `vectors.py show`. The vectors are the original's behaviour, so a
change of ours is never a reason to re-record; an intended difference needs its own check.

**Replay** (`ei_vectors.exe`, every configuration, run by build.cmd): links the families' sources without the game
(tests/vectors/hook_stub.cpp keeps the registrations; an EI_ORIGINAL forwarder jumps to the replacement registered at
its address, one without stops the run naming it; ports no replay reaches are stubs in replay_unlinked.cpp). Each
record runs under its recorded control word and is compared bitwise. It fails on a differing record, a file without a
check, another payload size, or no files.
- Variable-size records: payload size 0 in the header; `Writer` / `Reader`, `Sampler::OfferBytes`,
  `EI_VECTOR_CHECK_BYTES`; a check must read its payload to the end; large state by `HashBytes`.
- Fixtures: a name starting with '@' replays first and its game data stays for the other checks; a failed fixture stops
  the run.
- The game's data seam (src/game/game_data.h, SV4d): `ei::GameData<T>(address)` is the address in the game and, with
  `EI_VECTORS_REPLAY`, a zeroed stand-in image of 00401000-007D7000 that a check fills (replay_game_data.cpp) and
  replay_main clears before every record. Named accessors go through it (GameLogicSettings, GameDifficulty,
  unitdb::Skills, SkillDb, IsaacRandom::Global, the coder tables, the keyword table).
- MFC and CRT in the replay: replay_mfc.cpp (CString / CPlex on the replay heap), replay_crt.cpp (the game locale's
  classes from "@GameCrtCtype"; multibyte calls for ASCII only). Finding (SV4b): isdigit / isspace of a negative char
  read heap memory before the locale's table, so the answers vary between runs; such records carry the answers given.

Families: `maths` (vectors, quaternions, matrices, CubicSegment::Evaluate via its live copy 004E5660,
CTransformPipeline::PushMatrix), `random` (IsaacRandom Next / Refill / Range), `stats` (SV4a, SV4d; unit statistics, damage, skill
and school prices, protection, hit location, LockKey::Opens), `crt` (vsprintf, atof, fixture "@CrtFormatLocale"),
`coder` (CoderState Compress / Decompress, "@CoderTables"), `script` (ClassifyToken, FunctionSignature, KeywordName,
Tokenize, ApplyDefines, the loader's Load / LoadFunction / LoadCommands / ExpectLexeme), `path` (SV4c; CAIMap::FindPath,
PlanLongPath, PathSmoother::Smooth with "@PathTables" and "@AIMap"; `tools/path_vectors.py prune --keep` keeps the maps
small). Not reached by any scenario: CubicSegment::SetBSpline (dead), Value6E9, GetFactors, PickLocation's draw.

Adding a family: a payload header, a record file (one recorder per function, `Sampler(family, name, sizeof(payload),
cap)`), a replay file, the family's sources in CMakeLists.txt's ei_vectors, then a record run and `vectors.py merge`. A
family whose functions read game objects needs their state in the payload (copied, never pointers) and a way to build
them outside the game. Check each new family with a corrupted copy (one bit of a result must fail).

## 4. Coverage

`tools/coverage.py list` writes tests/coverage/coverage.csv: per rewritten function its status, golden (a golden
scenario reaches it), vector (tests/vectors/data has its file), suite (only scenarios without a golden), unreached,
startup / dead (tests/coverage/unreachable.txt), with a risk rank (size x area weight, x2 unreached); `who <address or
name>`, `top [N] [--area]`. The reach data, tests/coverage/reach.txt, is the last hook build's collection (EI_COVER=hooks,
until S21c); `EI_COVER=<addr,...>` on the original still watches single addresses (src/hook/cover.cpp).
- Dead (SV5c): the fake unit (class 0x39) and its scene loader: no shipped data makes one, no code reaches slot 62, no
  .amf exists. CubicSegment::Evaluate 0040E440 and SetBSpline are dead (the game uses the copy at 004E5660).
- Scenarios added for gaps: unit_ai_heal, ui_base_topic, world_loot, unit_ai_cast, zone_gz15h_tornado (CEffectTornado
  launches only on Rebirth and only in about half the runs: a strike needs open ground at a random point), ui_camp_make,
  magic_traps_victims (`make_trap_mob.py --interval 0`), unit_cast_at, unit_ai_corpse, levers_key (SV5b: a quest item on a locked lever, `clickunit #<id>`),
  ui_options_remap / ui_options_remap_load (SV8: keys remapped in the options and used, the saved keyboard.ini loaded
  at start through `make_keyboard_ini.py`, which writes it into the run's overlay; the user's report of remapping not
  working did not reproduce on a clean build).
- Multiplayer (SV5d): `net_pair_join` is a pair of games (section 6): a second game finds the first's hosted game in the
  lobby list, joins it (join request, admit, login, the client's BeginMultiplayerGame into bz1mpg), both say a chat
  line, and the second leaves; goldens for both games. SV5d1: `net_pair_zone` (an exchange on the base, the host's
  map choice through the sheriff's topic, both games out through bz1mpg's gate into the game zone: the joiner as a
  remote player of the host's world) and `net_pair_kick` (kick by serial, ban by name, a refused join); goldens for
  both installs and both games. Walks use `clickground` (the console's `#` / `@` commands do nothing on the
  multiplayer base). SV5d2: `net_pair_leave` (shared files to a peer that lacks one, the peer lost in the game zone,
  the host back to the base and the map's reject topic: LeaveZone and its award). SV5d3: `net_pair_quest` (a
  prepared strong hero, `make_mp_hero.py`, finishes z9q2 and the sheriff's "_3" topic: CompleteZone and its awards
  to both games; a public game whose `fakemaster` answers: OnMaster). Walks in the zone: `lookat` (a minimap click
  under the target) before each `clickground`.
- Still unreached: TryTame (SV5a1), SuspectionMode
  Enter / Execute, patrol points, CTrapObject::UpdateTrap, PickEnvironmentSpeech (T2c: the bored remark, 1 in 1001 per
  update after 500 bored ticks).

## 5. What makes runs repeat

The harness makes the original and the image deterministic and layout-independent. User decision 2026-10-15: no golden
may depend on heap layout, timing or leftover memory; where the game reads never-set or address-ordered data, ours does
the canonical thing (kept in the image as a determinism fix) and the harness patches the original the same way.

**Virtual clock** (tests/verify/harness_clock.h; default, `realtime` or `EI_HARNESS_CLOCK=real` to leave it): only the
main loop moves it, five world updates a frame; the main thread's sleeps and timed waits take no real time; sample ends
fire on virtual time; date and seeds fixed; steps run once a frame. Other threads read an anchor that each main-thread
advance moves to max(their time, the new virtual time), never back and never standing still (SV1h: after a long load
they stood still and the cursor / music waits took seconds). `speed` speeds nothing up on it (each frame just gets less
virtual time) and no scenario uses it (SV1i). While the interface's close-all flag is set the main loop's peeks report
no message (HoldMessagesForCloseAll, SV1j), so the next pass always does the close instead of whichever real message
came first.

**Heap** (tests/verify/harness_heap.cpp, game.exe and the image): the CRT heap is made without the low-fragmentation
front end (whose random slot choice moved every block, SV1b) and is one fixed 256 MB reservation for the main thread's
blocks below 0x7F000; larger blocks, other threads' blocks and blocks made inside CHandleMap::FromHandle come from a
second, growable heap (SV2b: later segments landed wherever VirtualAlloc found room). At the first message loop pass
the harness takes every CRT lock once and runs __tzset (SettleCrt). `lfheap` in EI_HARNESS_CLOCK keeps the game's own
heap.

**Canonical values in the original** (each patch at a CALL or byte site, checked before patching):

| What | Original sites | Switch | Ticket |
| --- | --- | --- | --- |
| Units' update countdown from `id_ & 0xF`, not address bits | eight `SAR r,5 / AND r,0xF` sites (0058D4FA ... 00596BB1); ours `UpdateCountdownFor` | | SV1b |
| UnitStats / UnitVitals stack copies zeroed | GiveDefaults, ApplyPrototype, BuildFromPrototype, CUnit / CMapObject::TakePrototype, CParty::AddMember (harness_leftovers) | `EI_LEFTOVERS=<hex family mask>` | SV7b3 |
| Server objects, map objects, Suspection / Fear modes, class templates allocated zeroed | NewObjectImpl 004D6970, CreateObject 00512730, AddSuspectionMode / AddFearMode and their constructors' callers, ClassTemplate::Construct's callers | same | SV7b2, SV7d3, SV7d4 |
| View position zeroed (draw sort NaN) | CMapObject::Construct's CALL 0050E2F2 (bit 6) | same | S17a |
| Particle source zeroed (emission point read before Attach) | NewParticleSource's operator new 004981CD (bit 7) | same | SV7b7 |
| Lights zeroed (the sun's never-set position under water) | CLightFactory::NewObjectImpl's operator new 0068DA7C (bit 8) | same | SV7b6a |
| Path cells' speed word 0x200, terrain bits 0 | CALLs of SearchPathTo, BuildCells, Smooth (harness_path_cells) | | SV7d2 |
| Save padding, a sound's name tail, the stock's codes, archive keys numbered by first save | harness_save_leftovers | `EI_SAVE_LEFTOVERS=0` | SV7b2a |
| A heard claim's point 0,0,0 | CallsAddClaim's three loads (005CCF04..) | | S20d |
| Script groups ordered by object id | comparator 004B2D80, inline compares in Union / Difference / Intersection / Insert (harness_script_order) | | SV7b1 |
| Object sets by id; lit sectors by (sy, sx) | 0047ADF0, 0047B4C0, 006C8E90 (harness_address_order) | | T2f |
| Draw lists by texture / figure name, then depth | 005740D0 and 00574390 jump to ours (both: mixed orders ran off the array) | | SV7b6 |
| The shadow's turn undone exactly (no ulp drift per re-render) | CALLs 0051D479 / 0051D4E0 (harness_shadow_turn) | `EI_SHADOW_TURN=0` | SV7b4a |

Script groups order by id because the script engine saves groups by id and ids are unique while referenced; ties cannot
happen (the harness logs "share id" if one does). 

**Behaviour that only looks random**:
- Any change in frame count reshuffles the ISAAC draws (the client's particles and the server's AI share the
  generator), so functions driven by them (zone1's storm, wolf pathing, the bored remark) come and go when a wait is
  cut. Cut waits only where coverage stays.
- The game reads the heap's address order in more places than the table: a test that allocates around a load can still
  flip a script group's last member if the order patch is off.
- rebirth_flags' `DistancePointPoint(3,4)` reads four floats past an 8-byte argument block (any value, 1.#INF or NaN):
  the scenario checks `Or(DistancePointPoint(3,4),1)`, true for every value (T2e).

## 6. Harness isolation

- **No foreground** (SV1c, harness.cpp `noforeground::`): user32's activating calls are patched for every caller
  (SetForegroundWindow, BringWindowToTop, SetActiveWindow, SetFocus, SwitchToThisWindow do nothing; ShowWindow /
  SetWindowPos never activate; top-level windows get WS_EX_NOACTIVATE); the harness sends the game its WM_ACTIVATE. A
  game that lost the foreground used to be minimised and its device restores drew ISAAC numbers.
- **Out of sight** (WB12 the image, WB12a the original; `EI_HARNESS_SHOW_WINDOW=1` to watch): top-level windows are
  cloaked (DWMWA_CLOAK) and made tool windows; frames, posted input and window size stay the same. Message-only windows
  are skipped. `noforeground::Watch` checks every 200 ms; the report's `WINDOW:` line, run_all's `game windows:`
  summary. The original (game.exe, dgVoodoo windowed on D3D12) presents into its cloaked window as before and its
  frames come from the emulated render target (harness_capture.cpp), so its goldens are the same.
- **The harness's mouse is the only one** (S21c1, tests/verify/harness_input.cpp, both the original and the image): the
  game window's subclass answers WM_NCHITTEST with HTNOWHERE, so the user's real cursor over a window makes no
  WM_MOUSEMOVE, and the DirectInput mouse's GetDeviceState is zeroed after each read (DirectInputCreateEx's slot
  0073B044, routed at the game's CreateWindowExA). The report's `MOUSE:` line. The real cursor resting over the
  original's uncloaked, desktop-sized window had moved the camera and made originals not repeat (standalone.md, S21c1).
- **Loopback** (WB14, tests/verify/harness_net.cpp `netlocal::`): WSOCK32's bind (slot 0073B6DC) and sendto
  (0073B70C) are routed: a bind on every address binds 127.0.0.1, an unbound socket's datagram binds 127.0.0.1:0 first,
  a datagram outside loopback goes to 127.0.0.1 on the same port (the lobby's broadcast ping reaches the same game's
  server). Every game also gets a UDP port of its own instead of 8888. The watchdog checks the process's endpoints every
  second; the report's `NET:` line, run_all's `network:` summary. Harness runs start their build's own host (no shared
  copy, so the firewall never asks); a launch without a scenario keeps the shared copy and binds every address.
- **Pairs** (SV5d, tests/verify/harness_pair.{h,cpp}): a scenario's `peer <name>` line makes ei_launch start a second
  game on tests/scenarios/peer/<name>.txt (both the image or both the original, one job object, either side's hang
  ends both; the peer's report in the run folder's peer\, merged into the host's as `FAILED: peer: ...`). The two run
  in lockstep (turns a frame at a time through a shared mapping, so a datagram always arrives in the same frame) and
  their UDP sockets talk through a mailbox of virtual ports, not the loopback adapter (the real sockets are still
  bound on 127.0.0.1; the `NET:` check is unchanged); `peersignal` / `peerwait` order the two scenarios. `netcut`
  pulls one side's cable (its datagrams and those sent to it are lost; SV5d2), `hidefile <path>` makes an install
  file read as missing in one game until it writes it (a peer without the host's quest file). The report's
  `PAIR:` line. A pair takes two machine slots (run_all's machine_slot(count=2), work_sched's games_of; ei_launch takes
  the second itself when its caller held only one). Goldens keep both games (golden.py: the golden's peer\).
  `fakemaster <port>` (SV5d3): datagrams to that virtual port are the master's: each announcement is answered with a
  master packet (kind -1, the announcement's key, a new token); the report's `MASTER:` line.
- **Typed text** (SV5d2): the game's TranslateMessage (slot 0073B450) is the harness's: the US layout with no key
  held, and letters and digits make no WM_CHAR (the `key` step posts theirs). Windows' own translation followed the
  user's keyboard layout and held keys, so typed hero names came out doubled ("hhoosstt"), with capitals or with
  letters missing, differently per run.
- **Install files and registry** (harness.cpp, harness_settings.h; WB16): writes under the game folder
  (config\keyboard.ini on exit, the skin cache, mp\) go to the run's harness_saves overlay, read back from there.
  mp\ (multiplayer heroes) is the overlay's only (SV5d3): before, its listing fell back to the install and showed the
  user's own heroes; a run starts with none, or a prepare tool's (`make_mp_hero.py`).
  No run reads the user's settings: every registry call on HKCU / HKLM `Software\Nival Interactive` and
  `Software\Gipat.Ru` (the original's settings, Rebirth's game settings and starter switches) from any module
  (game, image, ei_hook.dll, addon.dll) is answered from the committed snapshot `config/harness/registry.reg`
  (advapi32's exports are stubs jumping through its own import slots, which the harness takes); such keys are never
  opened, created or written for real (game.exe's RegCreateKeyExA slot 0073B010 and RegSetValueExA slot 0073B018 too).
  A value or key a run reads that the snapshot lacks fails the run ("settings: registry value not in the snapshot");
  `"name"=-` marks one read but absent on purpose. config\keyboard.ini and config\addrbook.ini are read from
  `config/harness/<vanilla|rebirth>/` unless the run's overlay has its own (a prepare tool's, or the game's write).
  Why (2026-10-20): the user's play of 2026-10-16 set the original's General Settings\AudioStreaming (music volume) to
  0, and 13 vanilla goldens (ground_prints, hero_death, levers, magic_traps, net_pair_*, script_fx, world_load, ...)
  differed in random draw counts in the original and the image alike; the snapshot keeps 0x1c, which reproduces them
  (AudioSFX / AudioSpeech / ShowTutorial made no difference). To change a setting for every run, edit the snapshot and
  re-record what it moves; `EI_HARNESS_SETTINGS=<folder>` runs with another snapshot folder (tryouts).
- **No run writes into an install** (harness_install.h; WB17). Other modules' writes go to the overlay too: kernel32's
  exports are stubs jumping through its own import slots (CreateFileA/W, CreateFile2, DeleteFile, CreateDirectory,
  RemoveDirectory, SetFileAttributes, CopyFileW, MoveFileExW) or call kernelbase through them (CopyFileA:
  CopyFileExW; MoveFileW: MoveFileWithProgressW; MoveFileA / MoveFileExA: MoveFileWithProgressTransactedW), and the
  harness takes those slots, so addon.dll (statically linked CRT), EI_Plugin.asi, Miles and Bink are covered. Under
  them, ntdll's NtCreateFile / NtOpenFile / NtSetInformationFile (rename, link, delete) / NtDeleteFile stubs jump to
  a guard: anything under an install root (the working directory at start, EI_GAME_ROOT, EI_INSTALL_ROOTS, which
  ei_launch sets to both installs) opened for writing (write access or any disposition but FILE_OPEN), created,
  renamed, linked or deleted is refused and fails the run (`install: write refused: <call> <path> (<module>+<offset>)`).
  The report's stack lists return addresses (words just after a call) from the refused call up, e.g.
  `ei_hook.dll+B8AA5, addon.dll+7BD60, ...`; `ei_hook.map` names ei_hook's. The game's root itself is written to the
  overlay's root (emptying mp\ the game deletes every entry FindFirstFile lists, `mp\..` too: a directory, the delete
  fails in both). A log (*.log) or a file over 16 MB is not copied into the overlay before a keeping write (the write
  starts empty). Checked (2026-10-20) by adding the run's own `harness_saves\Starter` as a root for one Rebirth
  world_load: addon's log, errors.log and Dumps opens were refused and the run failed with their lines.
  What wrote into the installs before (2026-10-20): addon.dll's DllMain (addon+1000FFDE, AddonInit) opens
  `starter\addon.log` and appends ~60 KB per launch (1.3 GB by then; the user removed it) and its crash handler wrote
  minidumps to `starter\Dumps\` (58 from runs of 2026-10-02..15). addon.dll is loaded at game.exe's entry point (the
  dinput.dll proxy's DllMain makes the entry jump to its loader), after ei_launch injected ei_hook.dll, so the harness
  sees all its writes; in the image no addon.dll loads.
- **Starter answers** (harness_starter.cpp): resolution, widescreen, and CtfMon answered 0 (addon.dll otherwise closes
  the user's ctfmon at start and restarts it from DLL_PROCESS_DETACH, which hung every Rebirth exit; S3e). A game hung
  after quit is ended after 3 s with `exit_hang.txt` (each thread's EIP and return addresses).

## 7. Diagnostic tools

- `EI_HARNESS_CLOCK=virtual,randtrace`: every random draw with its frame, caller and generator position (`#<n>`) in
  randtrace.txt, plus clock moves and sound ends; `tools/randtrace.py A B` reports the first draw B makes in no frame or
  another. `clocktrace`: every clock read and wait in ei_hook.log. Through `work.py test`, set it in the environment.
- `golden.py diff A B [--ulps N] [--ignore F] [--records K]`, `golden.py state A --record N`: where two runs' digests
  part, field by field; with `digest 1` and `randtrace` on the scenario this finds the first update and draw.
- `heaptrace` (EI_HARNESS_CLOCK) writes every CRT heap call with callers until world update `EI_HEAPTRACE` (default 40),
  then every object's place; `EI_HEAPSEQ=<update>` keeps the events in memory and writes heapseq.txt (faster).
- `EI_HEAP_FILL=<spec>` (or `heapfill.txt` beside ei_hook.dll, since a variable of its own moves the blocks: the CRT
  copies the environment onto its heap): fill chosen blocks (by number), byte ranges and resized tails with a byte or
  seeded bytes; bisect the block number, then the offset, to name an uninitialised field (how SV7b7 was found).
- `EI_WATCH_CUNIT=<id>[,flags]` (a client unit's orientation or flags writes, by debug registers), `EI_WATCH_ADDR=<hex>
  [,from,to]` (one dword's reads and writes), `EI_STACK_SCRUB`, `EI_SPIN_US` (tests/verify/harness_watch.h).
- `EI_SAVETRACE=1` (every memory stream write up to each saved file, savetrace.txt) with `tools/save_trace.py plain |
  diff A B` (the writers of differing save bytes).
- `tools/compare.py <scenarios> --runs N`: the original's and the image's consoles side by side.

## 8. Rules from real differences

- **Float grouping** (SV7d1, SV7d1a): `/fp:fast` regrouped `(30x + 10) / 31` into `x * (30/31) + 10/31`, which settles
  on another fixed point (9.99999714 against 10.0). The project builds `/arch:IA32 /fp:precise`; precise calls the
  modern CRT for sqrt / sin / cos / atan2 / exp / log, so rewrites use `x87::fsqrt / fsin / fcos / fpatan / fexp / flog /
  flog10` as VC6 inlined them.
- **Constant products** the original computes at run time (FLD; FMUL / FDIV) round to 24 bits there; the compiler folds
  them to doubles. `tools/check_fp_fold.py` (build.cmd) fails on x87 constants the original never loads (terrain::Sine's
  256 / pi, the briefing's and the lighting's pi / 180 are floats now; the double pi / 180 moved three menu units by
  1-2 ulps).
- **Never-set memory** is made canonical, never emulated (section 5). Before adding a new site, find the field with
  EI_HEAP_FILL and check every pass (plain, jitter 1, 2, 3, `2,fill`, `3,fill`, `EI_HEAP_FILL=r7`).
- **Whether anything reads a field before a set**: poison it in ours right after its constructor (a temporary
  env-gated write, one bit per construction site; a quiet NaN and a finite 1e5), then run the `--golden` suite and
  hash the capture scenarios' frames against a plain pass. A positive control must move something (the light's
  positions, SV7b6a's field, moved 12 frames of frames_basic / ui_saveload).
- **Transform positions** (SV7b6b): CTransformObject's constructor (00453400) and its inlined copies leave
  localPosition_ / worldPosition_ (+0x1C..+0x34). Only the light reads them unset (SV7b6a, zeroed). Poisoned at the
  camera (0040C8B0), terrain (in 00561E50), figure meshes and parts (005AABC0), sky (004D9110), scene meshes,
  every effect (CEffect base), CObject3D (00515C21) and particle sources: 175 goldens and 174 frames the same.
  Why: every effect's Reset zeroes the position in its constructor (ClearBase; CEffectShowVisibility sets it from
  the seer before use), the terrain's reset sets it, the sky sets it before its matrix, the camera reads its own
  only when placed (else the focus), scene meshes and figure parts are zero-filled arrays, map objects and figures
  get theirs on placement / load. The 3DS readers' stack figure (004A0350) is unported dead code.
- **Address order**: a sort or hash walk by pointer that reaches behaviour, a save or a frame gets an id or name order
  in ours and a harness patch in the original. Pointer-keyed containers that reach no output (CRenderHelper::records_,
  CMappedResource::filesByData_, mod_folders' sets) need nothing.
- **Allocation order** (SV2f, SV2h1b): the game reads the heap's address order in places, and before the
  canonical orders a twin block allocated early moved a script group's last member. Those reads are canonical now,
  so a golden that changes with the allocation order is a missing canonicalisation: check with the jitter passes.
- **Comparators over floats** must stay irreflexive: a NaN key sent STLport's unguarded scans off the list (S17a).
- **The reference cache** keys on the scenario commit, not the harness: after a harness change compare fresh runs of
  the original (`--fresh`).

## 9. Open

- SV6c (watch): a startup crash inside AIL_waveOutOpen (harness MutedWaveOutOpen <- CSoundManager::Open), seen twice in
  thousands of runs (once in the image); a retry passes.
- SV7c1 (later): vanilla menu unit cunit2's flags 0x100004A2 against 0x100004AA once under jitter seed 3.
- SV2c (watch): unit_create parted once in crt.rand only.
- SV7b5: net replication ordered by pointer (Connection's maps keyed by ReplicaEntry* / CRefObject*; SendUpdate hands
  out net ids in address order; only multiplayer and the lobby reach it).
- I13: a read-before-write detector for never-written heap bytes.
- SV5d3 (watch): net_pair_quest's Rebirth original parted once in five passes at world update 10040 (a fight in
  z9q2: an extra block of random draws); the golden is from three agreeing passes.
- WB12a: the original's runs are not cloaked.

## 10. Run time (SV10)

Where a run's wall time goes, from the report's `TIMING:` lines (tests/verify/harness_timing.h: per frame the
main thread's wall time and CPU in the server's updates, the digest, the client's ticks, the steps, a pair's
wait, the window checks and the rest; start-up to the first frame; frames of 200 ms or more) and EI_PROFILE
samples (`tools/profile_report.py`). 2026-10-21, Ryzen 9 7950X (16 cores), RTX 4070 Ti SUPER, 4K back buffer.
- **The suite's wall is net_pair_quest's.** 124 runs of `--golden --jobs 4` (the scheduler fills all 16
  slots): 178-215 s; net_pair_quest starts first and takes 171-186 s of it, the golden comparison ~7 s; the
  other 119 runs sum to 725-860 s (about 55 s spread over the free slots). Median run 6-7 s.
- **Start-up** (process start to the first frame): image 1.5-2.3 s, original 2.3-3.2 s; exit and ei_launch
  ~0.3 s. About a third of all the suite's game time. Image start-up by profile: d3dcompiler compiling the
  renderer's shaders 0.35 s, GetDxVersion's DirectDraw probing 0.45 s, the sound device open 0.3 s, the exe
  check through imagehlp 0.18 s, the rest loading.
- **A frame** (four games, machine otherwise idle): median 6-8 ms (image), 7-9 ms (original); with 16 games
  1.3-1.6x that (the main thread's own CPU per frame grows too: shared cores and memory). Per frame: server
  0.2-0.4 ms (render_stats' fights 2.5-3), client 1-4, digest 0.3-0.8 (every 20 updates, about every 4th
  frame), steps ~0.1, window checks 0.01-0.07, the rest 6-10 ms. The rest is CPU (its wall time is its
  main-thread CPU within 10%): no vsync wait (Present(0, 0) on a flip-discard chain), the GPU does 0.6-1.0 ms a
  frame at 4K (renderstats). By profile (render_stats): the game's own drawing code about half the main thread
  (terrain projection, mesh lighting, the classic shadow textures rasterised on the CPU, GDI text on the
  interface surfaces), our renderer's submission ~6%, the camera ~5%, the harness build's upload read-backs
  (CheckUpload) ~3%.
- **Pairs take turns**: only one game of a pair runs at a time (harness_pair.h), so a frame of
  net_pair_quest costs both games' frames: 19.3 ms median, 8-10 ms each game's own work and 11-12 ms waiting
  for the other (image and original alike; 6,437 frames).
- Cut (no golden changes): the window checks (WB12's out-of-sight check asked DWM about every window five
  times a second: 0.4-2 ms of wall time a frame in the original's runs, ~6% of net_pair_quest's main thread in
  the image's under load) walk the game's own threads' windows and sweep / ask DWM every 2 s; the digest reuses
  the last-but-one record's map nodes (~20% of its time). Suites after: image 124 runs in 173 s (sum 1,033 s,
  net_pair_quest 153 s); original 114 runs in 227 s (sum 1,192 s, net_pair_quest 176 s), with other workers'
  games on the machine. Suite times move with that load by ±15%, so the per-frame numbers are the measure.
- Proposals (tickets SV10a-SV10e): the pair's frame step run while the other game takes its turn, start-up,
  more world updates per drawn frame, drawing only frames a step reads, the harness build's renderer checks.
