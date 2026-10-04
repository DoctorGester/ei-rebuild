# Rebirth: what it patches, and its patches as flags in the rebuild

The Rebirth community edition (F:\games\Evil Islands (Rebirth)) runs the same game.exe 1.06 as the
GOG install and changes it in memory. This note lists every change it makes, says which matter for
single player, and describes how the rebuild reproduces them behind flags named after them
(`include/ei/rebirth.h`). The site-by-site list is `config/rebirth_patches.csv` (one row per patched
address: patch, source, site, size, game function, category, single-player relevance, the original
instruction there, what is written, and our status); `src/hook/rebirth_sites.inc` is generated from
it by `tools/gen_rebirth_sites.py`.

Everything here comes from static analysis of starter\addon.dll (v0.12.4.2, built 2024-05-16),
EI_Plugin.asi (built 2026-08-12), starter\Engine\dinput.dll and the two mss32.dll versions (Ghidra
headless decompilation plus capstone), checked against the last session of starter\addon.log. No
game was run.

## Summary

- **addon.dll** has 52 patches in four tables (plus 35 mod-only ones that need a mod with an
  `[Addon]` section, not used here). In this install 34 are applied and 18 are logged as disabled.
  addon.log names 218 patch sites (plus its own WinMain hook); three more are written without a log
  line (FixProcessLogin at 004433D6, FixRepairPrice's second site at 0060FB87, SetEiPluginMode at
  0073AF10). A Rebirth launch now lists any other changed range in rebirth_unknown_diffs.txt.
- By group (applied / all): bug fixes 10/10, widescreen/rendering 3/4, scripting/console 8/8,
  multiplayer 5/9, resources/paths 6/12, crash handling/infrastructure 2/9.
- Single player: 28 of the 34 applied patches change single-player behaviour or presentation; the
  rest are multiplayer (FixProcessLogin, ExtendedServerName, MQClient, MPSpeedControl, partly
  PlayerNumbers) and the EI_Plugin handshake (SetEiPluginMode).
- **EI_Plugin.asi** (the multiplayer plugin) is loaded by Miles (mss32.dll) in `AIL_startup`, i.e.
  after WinMain, so none of its changes appear in `docs/rebirth-patched-functions.txt`. Here it applies
  its loader hook and the NPlayers, Pause, NewMaster, Briefings and Fixes features (about 30 sites),
  all multiplayer. It also checks for updates of itself at hat.gipath.org.
- The four "unexplained" functions of the patched list are addon's: 00443080 is FixProcessLogin
  (the unlogged JMP at 004433D6), and 005F7DA0, 005F7E00, 005F7E30 are covered by WideScreen #93,
  a 194-byte write starting at 005F7D9E. No patched function is left without an addon site.
- **FPU precision**: measured 24 bits on the main thread at every frame boundary in both installs
  (027F until Direct3D starts, 007F after); nothing in Rebirth sets it. The verify wrappers now log
  any function they see running at a higher precision (below).
- game.exe itself differs from GOG's by one byte (00456BB5, JE to JMP: the CD check).

## How Rebirth gets into the process

1. The starter runs starter\Engine\game.exe with the game root as working directory.
2. starter\Engine\dinput.dll is "Addon Loader v1.1.0.0", a proxy for the system dinput.dll (it
   forwards DirectInputCreateA/W/Ex). When game.exe loads it, it reads
   `HKCU\Software\Gipat.Ru\EI_Starter` (`path`) and `...\Settings` (`AddonPath`), loads
   starter\addon.dll and calls its init. addon.dll writes a CALL at 006EC301 (the CRT's call of
   WinMain, "HookWinMain") and returns.
3. When the CRT is about to call WinMain, addon runs AddonMain -> ApplyPatches: it makes .text
   (00401000, 0x33A000 bytes) and .rdata (0073B000, 0x48000) writable, applies the patch tables,
   locks them again and calls WinMain. ei_hook.dll installs its hooks when WinMain starts, so it sees
   all of addon's patches (`docs/rebirth-patched-functions.txt`).
4. WinMain creates the sound manager (006B6040), which calls `AIL_startup`. Miles loads every
   `*.asi` of the working directory as a provider: mp3dec.asi (the 1999 MP3 decoder) and
   EI_Plugin.asi. EI_Plugin's DllMain writes CALL 10001BD0 at 006B6272, the instruction right after
   `AIL_startup`; that call applies the plugin's features and then calls the original 006B73E0.

dgVoodoo (DDraw.dll, D3DImm.dll, D3D8/9.dll in starter\Engine) is the same in our vanilla runs.

## Configuration

addon.dll reads (defaults in brackets, this install's value after the colon):

- `HKCU\Software\Gipat.Ru\EI_Starter\Settings`: `WideScreen` [1]: 1, `ObjectsDrawRadius` [100]:
  5376, `LandscapeDrawRadius` [100]: 5376, `AntiCrash` [1], `Rate` [5000], `MQ client` [0]: 1,
  `MP Speed-control` [0]: 1, `MP console` [0]: 0, `Console AutoExec` (multi-string), `CtfMon`,
  `AutoRecover` [1], `Tracer` [0], `FullDump` [0], `LogLevel`, `ConsoleLogLevel`, `AddonPath`:
  starter\MODS\Standard (the selected mod).
- `HKCU\Software\Gipat.Ru\EI_Starter\EvilIslands\General Settings\VIDEORESOLUTION` ("WxH", for
  WideScreen); `...\EI_Starter` `path` (the starter folder).
- `2utoexec.cfg` in the game root (console lines; here only `//` comments).
- The mod's `config.reg` (the game's binary registry format): `[MOD] AddonVersion`,
  `[Dev] Plugin / GameDebugFile / ChatHtmlFile`, `[MPZone] Gipath / Ingos / Suslanger`,
  `[Config] Tutorial.Ini / Movie.Ini / lights / StreamSN.reg / Music.reg / Ai.reg`, `GameSaves`, and
  the rule options of the mod-only table (`[AddonExp]`, `[AddonPerk]`, `[Addon]` ...). The Standard
  mod sets only the title and version, so the mod table does not run and the mod-dependent patches
  of table 4 are disabled except the resource redirections.

EIstarter.ini only configures the starter's window; addon.dll does not read it. EI_Plugin.asi reads
`.\EI_Plugin.ini` `[CONFIG]`: PK [0]: 0, NPlayers [7]: 7, Pause [0]: 1, AutoAim [0]: 0,
GraphGen [0], Test [0], NewMaster [1], Scripts [1], Briefings [1], Fixes [1]; addon's mode word
(below) switches Scripts off.

## addon.dll's machinery

- **Tables** (`100AE910`, pairs of name and apply function, run in this order): 1) GameExeDebug ...
  SetDefaultRate, always; 2) NivalRegistry, AutoExec, MQClient, MPSpeedControl, MPConsole, only while
  004568EA still holds the original registry key pointer 007848A8; 3) SetEiPluginMode, PlayerNumbers,
  AddNewScriptCommands, PersistentGlobalVars ("moved patches"), only when a compatible EI_Plugin.asi
  is found (it scans the working directory for a PE file containing "EI_Plugin.ini" and
  "EI_Plugin.asi" and checks a marker and checksum; otherwise "Moved patches are disabled");
  4) MenuMultiDisable ... ConfigTutorialIni, with a mod selected; 5) the mod table (ConfigAiReg,
  PerkLimit, ExpCalc, ..., the Lua* patches), only for a mod with addon options.
- An apply function returns 0 (applied), 1 (disabled: its setting is off) or 2 (failed: fatal).
- **Site kinds** (what `config/rebirth_patches.csv` calls the summary):
  - *context hook* (1000F353): the site's whole instructions (at least 5 bytes) become JMP to a
    generated stub, NOP-padded. The stub pushes the resume address, PUSHFD, PUSHAD and calls
    `handler(arg, context)`; the context is {eip, eflags, edi, esi, ebp, esp, ebx, edx, ecx, eax}.
    The handler may change registers and the resume address. Mode 0 drops the replaced instructions,
    mode 1 runs them before the handler, mode 2 after.
  - *function replacement* (1000F1F3): JMP at the function start to an adapter that turns the
    original's convention (thiscall here) into a call of an addon function, and a trampoline
    (stolen bytes + JMP back) through which addon calls the original.
  - *call redirection* (1000F2D5): a CALL rel32 (or CALL [mem]) replaced by a CALL to an adapter.
  - plain JMP/CALL rel32 to an addon stub, raw bytes, NOPs, operands, data and import slots.
- Before writing, addon compares the bytes it replaces with its own copy of the original
  ("OrigChunk"; the "Cannot verify patch ... OrigChunk is missing" warnings in addon.log are sites
  it has no copy for). It logs every site as `InstallPatch <name> <address> <size>`.
- Crash handling: an unhandled-exception filter writes a minidump to starter\Dumps and shows
  "The Game has crashed..."; `AntiCrash` and the "Tracer" (a vectored handler) are options.
- addon embeds LuaJIT (only used by the mod table's Lua* patches) and a console help text
  ("Welcome to Evil Islands Addon Engine v0.12.4.2").

## The patches

Status: *implemented* = reproduced in the rebuild behind its flag (see "Flags in the rebuild").
Sites are listed in full in the CSV.

### Bug fixes (10, all applied, all single player)

- **FixLeversActivate** - `LockKey::Opens` (00522A80, the lever/door lock test): addon compares the
  key's and the lock's levels signed instead of unsigned, and for a lock of level 0 returns the key's
  level + 1 (any key except level -1 opens it). One JMP at 00522AB6 to a stub. *Implemented*
  (src/game/unit_stats.cpp).
- **FixSpellEffectsOverflow** - (#1) `CMapObject::AddEffect` (0050F1E0, slot 45) is replaced: an
  object with 20 effects gets no more (the first ten refusals are logged), and after adding an effect
  that lasts more than one tick, every older effect of the same type and end flag whose +0x0C is
  within 0.001, that lasts no longer and whose value is not higher is removed. (#0) In
  `CWorldServer::UpdateUnits` (0058D670) a context hook (0058D82E, before the walk of the update
  map) first walks the map objects (007C2D00, i.e. +0x12DA0 of the server at 007AFF60) and, for each
  object that is referenced, not flagged 1, has effects, a companion (+0x170) and +0x174 <= 0
  (asleep: out of the update map, so its own update does not count its effects down), counts every
  effect down by one tick; an object with an expired effect goes back into the update map
  (0058D5D0), whose next update removes the effect. *Implemented*: #1 in src/game/map_object.cpp,
  #0 as a new rewrite `CWorldServer::UpdateUnits` with `TickSleepingEffects`
  (src/game/world_tick.cpp).
- **FixProgressNames** - the loading movie (00595080): after the original picks progres.bik for an
  allod other than gipat/ingos/suslanger, addon picks progres2.bik for "gipat2" and progres5.bik for
  "jigran" (Rebirth's own islands; progres5.bik is Rebirth content). *Implemented* as a new rewrite
  `SelectLoadingMovie` (src/game/world_zone.cpp).
- **FixSpellSelfCasting** - (#0) `CSpell::IsOffensive` (0068B770) is replaced by a table that also
  marks spell types 0x28 and 0x29 offensive (Rebirth's new spells; the original table stops at 0x26);
  (#1) in `CBattleScreen::SelectUnit` (00638EF0) `MOV CL,[EAX+0x7F]` becomes `MOV CL,0; NOP`: the
  flag passed to 00637500 when a spell is cast on a clicked unit is always 0. *#0 implemented*
  (src/game/spell.cpp); #1 needs SelectUnit (3.6 KB).
- **FixMoveObject** - `CSectorMap::Move` (005C11D0) is replaced by a check that refuses (returns
  false) a target with x < 0, y < 0 or NaN before calling the original; failures are logged.
  Vanilla computes a negative sector there. *Implemented* (src/game/sector_map.cpp).
- **FixAddMapObject** - world object creation (005927C0): a unit whose prototype record is missing
  is logged ("Invalid unit's prototype") and continues with index 0 instead of reading [0+8].
  Context hook at 00592F68. Needs 005927C0 (3.5 KB).
- **FixPlaingBattleMusicFromAntimagic** - 0054B580 decides whether the unit's current order is a
  fight (for the battle music, 0066E430): an attack, or a cast of a spell of type 0-8 or 0x1A. addon
  writes `JMP +8` over the 0x1A test (antimagic no longer starts battle music). *Implemented* as a
  new rewrite `CUnitServer::IsFighting` (src/game/combat.cpp).
- **FixRepairPrice** - the camp's price function (0060FAB0, slot 54 of the camp pages): the wear
  fraction 1 - durability / maximum is computed with negative (or NaN) values as 0 and clamped to
  [0, 1] (context hook at 0060FADC); and when a weapon or armour is taken apart in the item
  constructor (deal kind 7) the repair cost of its wear is added to the price instead of subtracted
  (a JMP at 0060FB87 that addon writes directly, without an addon.log line). Vanilla gives negative
  repair prices for over-full durability and huge ones for negative durability. *Implemented* as a
  new rewrite `CCampPages::Price` (src/game/ui_camp.cpp).
- **FixRemoveMapObject** - `CWorldServer::RemoveObject` (005935C0): an id that is not in the object
  map is logged and the function returns, instead of dereferencing null (vanilla crashes).
  Context hook at 00593615 that jumps to the epilogue. *Implemented* as a new rewrite
  `CWorldServer::RemoveObject` (src/game/world_server.cpp); with the flag off it reads through the
  null pointer like the original.
- **FixCreateItem** - the item factory (006AC970): an unknown prototype or an invalid
  prototype/material no longer calls FatalError (which ends the game); addon logs
  "GiveItem: ..." and the factory returns no item. Two context hooks. Needs 006AC970 (1.6 KB).

### Widescreen and rendering (3 of 4 applied)

- **WideScreen** (100 sites; single player) - when VIDEORESOLUTION is wider than 4:3 (and at least
  800x600) the 800x600 interface layout is widened. Data: the layout width 00742B08 (800.0) and
  007428AC (695.0). Functions replaced: `CInterface3D::ScaleRectToScreen` / `ScalePointToScreen`
  (005F7CA0, 005F7D20), 005F7D60, and 194 bytes rewritten in place from 005F7D9E (005F7DA0,
  005F7E00, 005F7E30), `CUiRegion::SetRect` / `AddPoint` (005FC9E0, 005FCA70), sprite placement
  005E6FA0, the camp and hero screen layouts (0060C870, 00613EA0, 00615D30), the panel builders
  0063C050, 0063C800, 0063E4B0, 00641F20, 00642930, 00645F80, 006462A0, 00648520, 00649190,
  00649910. Elsewhere: the panels' x/width immediates (0x208, 0x2D0, 0x320, ...), hooks where code
  reads the screen size (+0x98), the console and minimap rectangles, the cursor clip. The
  scaling functions are why rule 9 of the rewrite guide exists.
- **ObjectsDrawRadius** (4 sites) - 0056AFE0 (the client's draw pass) widens the view box by 100.0
  (0073BAB0) when collecting objects to draw; addon points those four FADD/FSUB operands at its own
  float holding the setting (5376.0 here).
- **LandscapeDrawRadius** (2 sites) - the terrain draw distance `PUSH 100.0` in 00569E40 becomes the
  setting.
- BinkPatch (disabled): with a mod plugin, Bink's _BinkOpen/_BinkSetVolume through the plugin.

### Scripting and console (8 applied; single player)

- **AllowScriptArgsInLoops** - `CScript::LoadCommands` (004D1330): the recursive call that parses a
  For/ForIf body passes the enclosing locals instead of 0, so a loop body can use the script's
  parameters and local variables (vanilla: "Unexpected token"). *Implemented*
  (src/game/script_parse_load.cpp); a Rebirth launch keeps the original here until
  AddNewScriptCommands, which patches the same function, is reproduced too.
- **AllowScriptsInMainMenu** - starting the main menu zone (ZoneMainMenuNew, in 0057C460) also sets
  CWorldServer+0x160 (007B00C0; a JMP at 0057C69F to a stub that redoes `MOV EDI,4`), so Tick runs
  the first script command queue in the menu. *Implemented* as a new rewrite `GameFlow::StartZone`
  (src/game/world_game.cpp).
- **AddNewScriptCommands** (25 sites) - the script command table (0078BCA0, 0x12C commands) is copied
  and extended (GiveUnitWeapon, GiveUnitArmor, StrUpper, StrLower, StrMid, StrHas, StrLen, StrCat,
  StrToFloat, FloatToStr, RandObj, RandStr, Random, Floor, UnitsNearPoint, NearestUnit,
  DistancePointPoint, GiveExp, GetUnitExp, GiveUnitExp, ResetTarget, SendCurrentParty, GameOver,
  GameOverMSG, PlayerExists, GSSetPlayerVar, GSGetPlayerVar, GSListPlayerVars, GetDexterity,
  GetIntelligence, GetStrength, GetSkill, GetPerk, GetRace, GetPrototype, IsAlive, IsDead, IsPlayer,
  GetObjectsOnMap, GetObjectTypeID, ShowAllUnits, PauseGame, SetWeather, AddMobEx, IsFileExists,
  ConsoleString, StartCameraSave, StopCameraSave, ...); the parser and executor (004B4030, 004B41E0,
  004B56C0, 004B5B00, 004B6760, 004CF470, 004CF600, 004CFBC0, 004D0B10, 004D0BA0, 004D1330,
  004D41C0) read the new table and dispatch the new commands. Moved here from EI_Plugin.
- **PersistentGlobalVars** - global variables named `gvar.*` keep their values across zone changes
  and saves (00662FB0, 0057EB10, 005800C0).
- **NewStringEvents** - string events (005F8070): "gameover" plays buttons\gameover.wav and shows a
  game over message (title and text from the event's arguments or the game_over / game_over_msg
  texts).
- **AddNewConsoleCommands** - when the console builds its command table (005DC620) addon adds
  help (wrapped), addon, execs, listgvar, drawall and nogui; at the first 00636020 call the
  Console AutoExec lines run.
- **ImproveConsole** - console input (005E44A0): no duplicate history entries; Ctrl+A/C/X/V.
- **AutoExec** - the three calls of the console executor 005DC620 from save loading (0057ABA0),
  0057BD10 and zone start (0057C700) first run every Console AutoExec line.

### Multiplayer (5 of 9 applied; not single player)

FixProcessLogin (clears +0x18 of a login record, 00443080), ExtendedServerName (longer server names:
sizes and widths in 0061B2C0 / 0061D780, three text calls), MQClient (no .mq files from the server,
0041ECC0), MPSpeedControl (speed keys and buttons in multiplayer, 0063A560 / 00648D70),
PlayerNumbers (moved from EI_Plugin: a call in 0066C180). Disabled: SetDefaultRate (network rate
00783E34), MPConsole, MpZone, ChatLog.

### Resources and paths (6 of 12 applied; single player)

- **NivalRegistry** - WinMain's registry root becomes `Software\Gipat.ru\EI_Starter\EvilIslands`
  (the PUSH at 004568EA): every game setting lives under the starter's key, not Nival's.
- **Maps** (10 sites) - zone files are looked up in the mod's Maps folder and Rebirth's maps first:
  the loaders 00595480 (NewZone), 00595BA0, 00567AB0, 0057F560, 005809D0, 005812F0, 005813A0 and
  the path setter 0055E990.
- **ResOverride** (25 sites) - resource archive open/close/init (00671730, 006714A0, 0068E640,
  005AA510, 004F68C0, 00691500), every resource file find (4 mask finds, 13 finds) and the
  file-find vtable slot 007432F4 go through addon: files of the mod's (and Rebirth's) Res folders
  replace the game's.
- **MoviesPatch** (the BinkOpen import slot 0073B720), **CameraPatch** (camera files, 0040ECD0),
  **StreamPatch** (music streams, 006BB3A0): the mod's folders first.
- Disabled: SavesPath, ConfigMusicReg, ConfigStreamSNReg, ConfigMovieIni, ConfigLightsIni,
  ConfigTutorialIni.

### Crash handling and infrastructure (2 of 9 applied)

- **DisableExitOnCrashReport** (single player) - FatalError (00480080) logs its text instead of
  the message box (004800A7) and no longer exits (9 NOPs at 004800C9); 005641F0 no longer calls
  00665A10 before its own FatalError. The game carries on after errors that end vanilla.
- **SetEiPluginMode** - writes 0xAADD0080 at 0073AF10 (padding after .text) for EI_Plugin.asi.
- Disabled: GameExeDebug (debug log file), CommandLine (addon's command line mode), GraphGenPatches,
  MenuMultiDisable, MenuSingleDisable, PreventZone1Animation, ForceShowTutorial.

### Mod-only table (not applied here)

ConfigAiReg, PerkLimit, ExpCalc, MPShowAllQuests, DisableRunaway, EnableArchersDefence,
DisableDistanceModifier, DisableWeightBonus, ExpLimit, RandomMagicDamage, NegativeMagicEffect,
DeagrPlayerOnLoose, AttackAnimSpeed, NewRuneCodes, UsePrototypeActionsForMobAttack,
UsePrototypeActionsForMobCast, FastTeleport, DisableCrippledHL, UnlockExperimentalSpells,
LoadSpellAnimationFromSlotsField, ImproveInfoWindows, RemoveSpellConstructReqs,
SendSpellEffectValueToPlayers, AddWeaponsFromDbToAllUnits, MultipleLoadingMobs, UnlockCorpses and
the Lua* replacements of the game rules (perk prices, damage, regeneration, unit stats, running mana
cost, attack parameters, experience, shop prices, experience distribution). They change the rules
for mods that ask for them; not inventoried site by site.

## EI_Plugin.asi

The plugin checks game.exe (the PE header word at +0x150 is 0x2EC226), refuses when the mode word
at 0073AF10 says so, makes the whole image writable and patches 006B6272 (see above). Its first call
reads EI_Plugin.ini and addon's mode word (0xAADDxxxx: bit 0 PK, 1 NPlayers = 6, 2 Pause, 3 AutoAim,
4 GraphGen, 5 Test, 6 NewMaster, 7 Scripts, 8 Briefings, 9 Fixes off; addon writes 0x0080) and
applies (active here in bold):

- **NPlayers** (!= 6): player count bytes in the lobby code (0061BB82, 0061BAF7, 00640069, 00608D9D,
  00608DE4, 006095FB; more above 7 players).
- **Pause** (1): 0066FAFE and 14 bytes at 0066FB62 (2: more at 0066FB01/0066FB10; 3: 0066FB2C and
  `RET 4` at the start of 005942C0).
- **NewMaster**: the new master server: CALLs into the plugin at 006330A9, 0041ABDA, 0061D138,
  00440F9F, NOPs at 0041ACB7, and 42 NOPs at 0045D330 inside `GameOptions::SaveToRegistry`
  (0045C560, rewritten: the Master Server Name is no longer saved), plus a network thread.
- **Briefings**: 0x19 bytes at 005920FF and four jump offsets in 00592090.
- **Fixes**: checks for a 0x66 byte at +4 inserted in 00613EA0, 005ED7B0, 006091C0, 006407E0 (four
  places), a JMP in 005F13A0, NOPs in 00536100, two JMPs into the plugin (006093B7, 00640C97).
- Scripts (switched off by addon): its own script commands and the call at 0066C1C2 (addon's
  PlayerNumbers / AddNewScriptCommands instead). PK, AutoAim, GraphGen, Test: off here.

The plugin also contains an updater for itself (hat.gipath.org/plugin/latest.manifest, signed),
which explains its recent build date; its patch set may change with updates.

Because the plugin patches after WinMain, ei_hook used to hook functions it later patches, silently
dropping its change (0045C560 above). The installer now knows the plugin's sites and active features
(from EI_Plugin.ini and the mode word) and keeps the original where one would be lost.

## The four unexplained functions

00443080 (+0x356): FixProcessLogin, a JMP written directly at 004433D6 (addon's helper 10017DC5
writes into game memory without logging). 005F7DA0, 005F7E00, 005F7E30 (+0): inside WideScreen #93,
194 bytes written from 005F7D9E (two NOPs, then new code for the three functions). EI_Plugin.asi
patches nothing before WinMain.

## FPU precision

Measured (harness runs of every scenario, both installs, after this round's trace): the control word
is 027F (53-bit, the CRT default) at WinMain and still 027F before `AIL_startup`; it is 007F (24-bit,
round to nearest, all exceptions masked) the first time the message loop runs, i.e. after Direct3D
(dgVoodoo) created the device; and it does not change between frames afterwards. Vanilla and Rebirth
are the same at every frame boundary.

Statically, nothing in Rebirth raises it:

- addon.dll and EI_Plugin.asi use a VS2010-2013 static CRT. Their DLL init calls `_cinit(0)`, so
  `_fpmath` only does FNCLEX (`_setdefaultprecision` is not called for DLLs). Every FLDCW in them
  (Ghidra listing) is a save/restore inside a CRT maths routine (fabs and the like set 0x133F through
  `_ctrlfp` and put the caller's word back on every exit path; the transcendental dispatchers load
  027F only if it differs and restore it in `__math_exit`) or EI_Plugin's float-to-int helper (RC
  only). No `_controlfp`/`_control87` outside the CRT, no FNINIT. The loader (dinput.dll) has no
  FPU code. The handlers that run in game paths (the context hooks of FixSpellEffectsOverflow #0,
  FixRepairPrice, the console and script hooks, the WideScreen replacements) only save the integer
  registers and flags (PUSHAD/PUSHFD), not the FPU state, and change no control word.
- game.exe's own `_control87` calls (006C3A00 terrain lighting, 006C8990, 006C9560) switch the
  rounding mode (chop, down) for a while and restore the whole word; they never change precision.
- Both mss32.dll versions call `_controlfp` only from `_setdefaultprecision` at their own load, and
  load every `*.asi` (mp3dec.asi, EI_Plugin.asi) in `AIL_startup`, where mp3dec's VC CRT also sets
  _PC_53. That happens before Direct3D, which then sets 24 bits. Rebirth ships MSS 6.0h, GOG 5.0p;
  this does not change the outcome.
- Other threads start at 027F (53-bit): Miles' timer/mixer threads, the game's stream and MIDI
  threads, EI_Plugin's master-server and updater threads. Game code that runs there (the sound
  manager's callbacks) computes at 53 bits in both installs.

So the earlier conclusion ("under Rebirth the FPU runs higher", drawn from a CItemWeapon::RollDamage
mismatch) is not confirmed: the main thread runs at 24 bits in both installs at frame boundaries,
and no Rebirth component is known to raise it within a frame. Possible explanations of that mismatch:
the then rewrite's own operation order (/fp:fast reassociation changes results at 24 bits too), or
a transient raise not found statically. To catch the latter, every verify wrapper now checks the
control word when it reports: a function seen at 53 or 64 bits is logged once ("FPU precision ...
in <function> on the main / another thread; stack: ..."; at most 30 lines) and listed in
harness_report.txt. Returning ST0 results as `double` stays right either way (exact at 24 and 53
bits, and required where callers keep using ST0).

## Flags in the rebuild

### The flags

`include/ei/rebirth.h` declares `ei::rebirth::Patch`, one enumerator per patch named exactly as
addon.log names it (52), plus one per EI_Plugin feature (`EiPluginInit`, `EiPluginNPlayers`, ...),
from `include/ei/rebirth_patches.inc`. Rewritten code tests
`ei::rebirth::Enabled(Patch::FixRepairPrice)` and runs the patched behaviour when it is on.

Every flag is off by default (the original 1.06). They are set once, before any rewritten code
runs (`ei::rebirth::LoadSettings`, src/lib/rebirth.cpp, called by the hook installer at WinMain):

1. `ei_rebirth.ini` next to ei_hook.dll (the standalone exe will read it next to itself):
   `[Rebirth] Default=0|1`, `<PatchName>=0|1`, `FollowAddon=0|1`. A missing file changes nothing.
2. The `EI_REBIRTH` environment variable (`ei_launch --rebirth <spec>`; `harness.cmd` passes it on):
   `all`, `none`, or names separated by commas, each optionally `=0`/`=1`.
3. A harness scenario's `rebirthflags <spec>` line (read when the DLL loads).
4. In a Rebirth launch, with `FollowAddon=1` (the default), every flag is then set to whether
   addon.dll applied that patch (any of its sites differs from game.exe on disk at WinMain) or
   EI_Plugin.asi will apply that feature. The log lists both ("Rebirth: addon.dll applied: ...",
   "Rebirth flags on: ...").

The flags are plain data in src/lib, so the standalone exe uses the same code; only the detection
of what the mods applied (src/hook/rebirth_hook.cpp) belongs to the hook DLL.

### Installing and verifying flagged rewrites

A rewrite declares which patches it reproduces in its function:

    EI_REPLACE_METHOD(0x00522A80, LockKey::Opens);
    EI_REBIRTH_IMPLEMENTS(0x00522A80, ei::rebirth::Patch::FixLeversActivate);

The installer (src/hook/hook.cpp with rebirth_hook.cpp) decides per function:

- **Rebirth launch, function patched by addon.dll.** Every differing byte must lie in a site of a
  patch the rewrite declares, and that patch's flag must be on (with FollowAddon it is). Then the
  function is hooked ("Rebirth-patched ... implements ..." in the log) and its verify wrapper
  compares ours, flag on, with the patched original. Otherwise it is skipped as before, with the
  reason ("Rebirth patch X ... is not implemented by the rewrite", "... its flag is off").
- **Stolen bytes.** When an addon site lies in the wrapper's stolen bytes (addon's JMP at a function
  start: FixSpellSelfCasting #0, FixMoveObject, FixSpellEffectsOverflow #1, the WideScreen function
  replacements), the stolen count must end at or after the site's end (sites end on instruction
  boundaries of both versions), and the trampoline re-targets the copied JMP/CALL rel32 so calling it
  still runs addon's code. A relative short jump written there would refuse the hook.
- **EI_Plugin.asi** patches later: a function containing a site of an active plugin feature is not
  hooked unless the rewrite declares that feature and its flag is on (and not with the site in the
  stolen bytes, which the trampoline would copy unpatched).
- **Vanilla launch, flag on.** The original has no patched behaviour to compare with, so a wrapper
  that compares with the original (stolen bytes) is not installed and the plain replacement runs
  ("UNVERIFIED ... the original does not have it"); a wrapper that runs ours only and checks the
  resulting state keeps running. Flags off: everything as before.

What a vanilla run with flags on loses: the comparing wrappers of the functions that implement an
enabled patch, so those run unverified, their unpatched paths included (here LockKey::Opens,
CSpell::IsOffensive, IsFighting, CCampPages::Price, SelectLoadingMovie). Functions with no patch,
and the state-checking wrappers (Move, LoadCommands, AddEffect), are verified as usual. A Rebirth
run is therefore needed only to verify a function whose rewrite reproduces a Rebirth patch: once
when it is written or changed (the run hooks it with the flag on and compares with addon's code),
after which it can go to verified.txt and vanilla runs suffice for regressions. Functions Rebirth
patches but our rewrite does not reproduce keep being skipped there, so Rebirth runs no longer need
to cover every scenario: only those exercising flagged functions (tests/scenarios/rebirth_flags.txt),
plus an occasional smoke run to catch a new addon/EI_Plugin version (the inventory and the site
table would need updating; rebirth_sites_live.txt, written next to the DLL in every Rebirth launch,
shows each known site's bytes and where its CALL/JMP leads).

### Implemented

| Patch | Where | Verification |
|---|---|---|
| FixLeversActivate | LockKey::Opens 00522A80 (unit_stats.cpp) | existing pure wrapper (7 stolen bytes) compares with the patched original in Rebirth |
| FixSpellSelfCasting #0 | CSpell::IsOffensive 0068B770 (spell.cpp) | existing pure wrapper; the 6 stolen bytes are exactly addon's JMP+NOP, re-targeted in the trampoline |
| FixMoveObject | CSectorMap::Move 005C11D0 (sector_map.cpp) | ours only (as before): the moved object's position and sector lists |
| AllowScriptArgsInLoops | CScript::LoadCommands 004D1330 (script_parse_load.cpp) | ours only (as before); Rebirth keeps the original until AddNewScriptCommands is reproduced |
| FixPlaingBattleMusicFromAntimagic | new: CUnitServer::IsFighting 0054B580 (combat.cpp) | pure wrapper (10 stolen bytes), vanilla and Rebirth |
| FixRepairPrice | new: CCampPages::Price 0060FAB0 (ui_camp.cpp) | pure wrapper (6 stolen bytes), vanilla and Rebirth |
| FixProgressNames | new: SelectLoadingMovie 00595080 (world_zone.cpp) | wrapper (7 stolen bytes) runs both on the same starting value and compares the chosen movie names as text |
| FixSpellEffectsOverflow #0 | new: CWorldServer::UpdateUnits 0058D670 + TickSleepingEffects (world_tick.cpp) | ours only (it runs every awake object): afterwards no unit sent to sleep is still in the update map |
| FixRemoveMapObject | new: CWorldServer::RemoveObject 005935C0 (world_server.cpp) | ours only (destructive: slots, releases): afterwards the id is in no id map; an unknown id changes nothing |
| AllowScriptsInMainMenu | new: GameFlow::StartZone 0057C460 (world_game.cpp) | ours only (tears down and loads zones): the campaign mode is the one asked for (or 2 / 8), and +0x160 is set in the menu with the flag |
| FixSpellEffectsOverflow #1 | new: CMapObject::AddEffect 0050F1E0 (map_object.cpp) | the original runs first (in Rebirth addon's, through its re-targeted JMP), its effect list and fields are set aside and undone, ours runs, and the lists, count, changed parts, flags and change stamp are compared (5 stolen bytes) |

The new rewrites are also verified with every flag off in the vanilla runs that already reach them
(newgame for the loading movie, combat and spells for IsFighting and AddEffect, ui_camp for prices).
`tests/scenarios/rebirth_flags.txt` runs them with the flags on.

Patched behaviour reproduced exactly: FixLeversActivate returns the key level + 1 for a level-0
lock (addon's INC EAX), not 1; IsOffensive keeps returning only AL; the repair clamp stores the exact
1.0 / 0.0 constants and lets NaN through; AddEffect's duplicate test uses addon's float/double
comparisons (the +0x0C difference rounded to float, its absolute value against the double 0.001)
and steps past the record moved into a removed one's place, as addon does. Not reproduced: addon's
log lines (addon.log, or the console only with a mod's LogErrorsIn* options) and the FSTP ST(0) its
AddEffect wrapper executes on an empty FPU stack.

### Fixed after the first Rebirth run

- The detection counted MoviesPatch (an import slot) as applied in every launch, because the
  loader writes every import slot: vanilla runs were treated as Rebirth launches and FollowAddon
  switched the scenario's flags off. Import slots now count only when they point into addon.dll,
  and nothing counts without addon.dll loaded.
- CCampPages::Price was skipped in Rebirth ("byte +0xD7 differs"): FixRepairPrice's unlogged
  second site, now in the inventory and implemented.
- The first AddEffect wrapper only checked ours and treated a re-applied effect that replaced its
  older copy (count unchanged) as an error; it now compares with the original as above.

### Remaining work

Estimates are for a rewrite plus its flag and verify wrapper, as in this round.

| Patch | Work | Estimate |
|---|---|---|
| FixSpellSelfCasting #1 | CBattleScreen::SelectUnit 00638EF0 (3.6 KB) | 1-2 days |
| FixCreateItem | item factory 006AC970 (1.6 KB, many callees) | 1 day |
| FixAddMapObject | object creation 005927C0 (3.5 KB) | 2 days |
| DisableExitOnCrashReport | FatalError 00480080 (small) + 005641F0 (1.5 KB) | 0.5 + 1 day |
| NewStringEvents, ImproveConsole, AddNewConsoleCommands, AutoExec | console code (005DC620 is rewritten; 005E44A0, 005F8070, 00636020 not) | 2-3 days |
| PersistentGlobalVars | 00662FB0, 0057EB10, 005800C0 | 1-2 days |
| AddNewScriptCommands | ~50 new script commands plus the table switch in 12 parser/executor functions (mostly rewritten) | 1-2 weeks |
| ObjectsDrawRadius, LandscapeDrawRadius | two float settings in 0056AFE0 / 00569E40 (or in the client draw rewrite) | 0.5 day once those are rewritten |
| WideScreen | 100 sites in ~45 functions, the layout maths of addon's replacements, VIDEORESOLUTION | 2-3 weeks |
| NivalRegistry, Maps, ResOverride, Movies/Camera/StreamPatch | path policy (mod folders, starter key); belongs with the standalone's resource layer | 1 week |
| multiplayer (5 addon + EI_Plugin features) | lobby, master server, network code | later, with multiplayer |

## Shims and forwarders added

- `EffectArray::SetSize` (map_object.cpp): forwarder to 0050FE70, CArray<MapEffect>::SetSize.
- world_tick.cpp: `IdArray::SetAtGrow` (004538D0), `CountedRemoveAt` (0059A050, the counted list's
  RemoveAt, which also releases the object), `ServerRemoveObject` (005935C0).
- world_game.cpp: `WorldSetTimeSpeedF` / `WorldSetTimeF` (0058EEE0 / 0058EEA0, float argument),
  `ClientWorldSetPlayer` (00566CE0), `ClientWorldLocalView` (005699F0), `EnterZoneAt` (0057A060);
  the old `StartZone` forwarder is now `StartZoneAt`.
- world_server.cpp: `IdMapErase` (00599A80), `SectorMapRemove` (005C1010), `UnitSetController`
  (005300D0), `CommandQueuePurge` (004BED90), `ProjectileForget` (0067EF00), `ProjectileRemoveAt`
  (00599CC0), `PlayerStateForget` (0066DC90).
- `ItemPrice` (ui_camp.cpp): the item's slot 16 through its vtable.

## Original bugs kept

With the flags off every rewrite keeps the vanilla behaviour, bugs included (the unsigned level
compare, the unclamped wear fraction, antimagic battle music, effects without limit).
