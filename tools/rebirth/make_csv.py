"""Builds config/rebirth_patches.csv from the analysis: addon_sites.tsv (every site addon.log names:
patch #n, address, size), site_instr.txt (the original instructions at each site and the function
it lies in), the tables below (summaries, statuses) and src/hook/function_sizes.inc.

usage: python tools/rebirth/make_csv.py, then python tools/gen_rebirth_sites.py
(docs/notes/rebirth.md, "Flags in the rebuild").
"""
import csv, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

# patch -> (category, single-player relevant, summary)
META = {
 'GameExeDebug': ('infrastructure', 'no', 'Disabled here. With a mod config.reg [Dev] GameDebugFile: JMP at the empty DebugLog 0044C470 to addon code that writes the game\'s debug messages to that file.'),
 'BinkPatch': ('rendering', 'no', 'Disabled here. Needs a mod [Dev] Plugin: hooks Bink (_BinkOpen@8 / _BinkSetVolume@12 of binkw32.dll) through the plugin.'),
 'WideScreen': ('widescreen', 'yes', 'Settings\\WideScreen (default 1) and EvilIslands\\General Settings\\VIDEORESOLUTION "WxH": for a mode wider than 4:3 (at least 800x600) the 800x600 interface layout is widened: layout constants (00742B08 800.0, 007428AC 695.0, panel x/width immediates), the CInterface3D scaling and CUiRegion functions replaced, sprite placement, panel builders, camp/hero screen layouts and the console/cursor rectangles adjusted.'),
 'ObjectsDrawRadius': ('rendering', 'yes', 'Settings\\ObjectsDrawRadius (default 100 = not applied; 5376 here): the 100.0 margin (0073BAB0) that 0056AFE0 adds around the view when collecting objects to draw is replaced by a pointer to the setting as a float.'),
 'LandscapeDrawRadius': ('rendering', 'yes', 'Settings\\LandscapeDrawRadius (default 100 = not applied; 5376 here): the terrain draw distance immediate (push 100.0) in 00569E40 becomes the setting.'),
 'AllowScriptArgsInLoops': ('scripting', 'yes', 'The script parser parses a For/ForIf loop body with the script\'s parameters and local variables (vanilla: none, so using them in a loop body is "Unexpected token").'),
 'AllowScriptsInMainMenu': ('scripting', 'yes', 'When the main menu zone (ZoneMainMenuNew) is started (0057C460), CWorldServer+0x160 (007B00C0) is set to 1 so the world script command queue runs in the menu.'),
 'FixLeversActivate': ('bugfix', 'yes', 'LockKey::Opens (00522A80): the key and lock levels are compared signed (vanilla unsigned), and a lock of level 0 is opened by any key whose level is not -1 (returns level + 1).'),
 'FixSpellEffectsOverflow': ('bugfix', 'yes', 'An object keeps at most 20 spell effects (CMapObject::AddEffect 0050F1E0 drops more and logs), older duplicates of a re-applied effect are removed, and units the world update skips get their effect durations ticked down (CWorldServer::UpdateUnits 0058D670).'),
 'FixProgressNames': ('bugfix', 'yes', 'The loading movie for a zone (00595080): the islands gipat2 and jigran (Rebirth content) get progres2.bik and progres5.bik instead of progres.bik.'),
 'AddNewConsoleCommands': ('console', 'yes', 'New console commands registered on every console execution (005DC620): help (the game\'s, then two addon lines), addon (info, debug: addon\'s log levels), execs (load and run a script file), listgvar (the global script variables), drawall (0079BE6C), all after the cheat word, and nogui (public: hide the GUI and/or cursor); the battle screen\'s drawing (00636020) applies nogui to its panels.'),
 'ImproveConsole': ('console', 'yes', 'Console input (005E44A0): a line equal to the last history entry is not added again; Ctrl+A/C/X/V select, copy, cut and paste through the clipboard.'),
 'CommandLine': ('infrastructure', 'no', 'Disabled here. JMP from 004565D4 to 004567D5: WinMain skips its command line parsing (the addon command line interface, "game.exe addon ...").'),
 'NewStringEvents': ('scripting', 'yes', 'String events (005F8070): "gameover" plays buttons\\gameover.wav and shows a game over message (title and text from the event\'s arguments, else the game_over / game_over_msg texts).'),
 'FixSpellSelfCasting': ('bugfix', 'yes', 'CSpell::IsOffensive (0068B770) also for spell types 0x28 and 0x29 (Rebirth\'s new spells); CBattleScreen::SelectUnit (00638EF0) passes 0 instead of the spell\'s +0x7F flag to 00637500.'),
 'FixProcessLogin': ('multiplayer', 'no', 'Server login (00443080): field +0x18 of the login record is cleared before 00443720. Written directly into game memory (no addon.log line).'),
 'FixMoveObject': ('bugfix', 'yes', 'CSectorMap::Move (005C11D0) refuses a target with x < 0 or y < 0 (or NaN) and returns false; failures are logged.'),
 'GraphGenPatches': ('infrastructure', 'no', 'Disabled here. Support for "addon aigraph" (AI graph generation for maps).'),
 'FixAddMapObject': ('bugfix', 'yes', 'World object creation (005927C0): a unit whose prototype is missing no longer dereferences a null record (logs "Invalid unit\'s prototype").'),
 'FixPlaingBattleMusicFromAntimagic': ('bugfix', 'yes', '0054B580 (does the unit\'s current order count as a fight, for the battle music in 0066E430): casting a spell of type 0x1A (antimagic) no longer does.'),
 'FixRepairPrice': ('bugfix', 'yes', 'Camp prices (CCampPages slot 54, 0060FAB0): the wear fraction 1 - durability / maximum is clamped to [0, 1], negative values counting as 0 (vanilla gives negative or inflated repair prices for odd durabilities).'),
 'FixRemoveMapObject': ('bugfix', 'yes', 'CWorldServer::RemoveObject (005935C0): removing an id not in the object map logs and returns instead of dereferencing null.'),
 'FixCreateItem': ('bugfix', 'yes', 'The item factory (006AC970): an unknown item prototype or an invalid prototype/material no longer calls FatalError (which exits the game); it logs "GiveItem: ..." and creates nothing.'),
 'DisableExitOnCrashReport': ('infrastructure', 'yes', 'FatalError (00480080) logs instead of showing its message box and no longer exits; 005641F0 no longer calls 00665A10 before its FatalError.'),
 'ExtendedServerName': ('multiplayer', 'no', 'Longer server names in the multiplayer screens (0061B2C0, 0061D780): field sizes and widths changed, three text calls redirected.'),
 'SetDefaultRate': ('multiplayer', 'no', 'Disabled here. Writes Settings\\Rate over the default network rate 00783E34 (2500).'),
 'NivalRegistry': ('resources', 'yes', 'WinMain\'s registry root "Software\\Nival Interactive\\EvilIslands" (007848A8) becomes addon\'s "Software\\Gipat.ru\\EI_Starter\\EvilIslands": the game reads and writes its settings under the starter\'s key.'),
 'AutoExec': ('console', 'yes', 'Before each of three "exec autoexec.cfg" console executions (in 0057ABA0 load game, 0057BD10 new game, 0057C700 the new-game request) every Console AutoExec line (HKCU\\Software\\Gipat.Ru\\EI_Starter\\Settings REG_MULTI_SZ "Console AutoExec", then the lines of 2utoexec.cfg) is executed first.'),
 'MQClient': ('multiplayer', 'no', 'Settings\\MQ client: the multiplayer client does not take .mq files from the server (0041ECC0).'),
 'MPSpeedControl': ('multiplayer', 'no', 'Settings\\MP Speed-control: the game speed keys and buttons also work in multiplayer (0063A560, 00648D70).'),
 'MPConsole': ('multiplayer', 'no', 'Disabled here. Settings\\MP console: the console in multiplayer (005DC620).'),
 'SetEiPluginMode': ('infrastructure', 'no', 'Writes 0xAADD0080 at 0073AF10 (padding after .text): tells EI_Plugin.asi that addon applies the patches moved out of it (bit 0x80: the plugin\'s Scripts feature off). Written directly (no addon.log line).'),
 'PlayerNumbers': ('multiplayer', 'partly', 'Moved from EI_Plugin (its Scripts feature): the call at 0066C1C2 (0066C180) goes through addon (player numbers for scripts).'),
 'AddNewScriptCommands': ('scripting', 'yes', 'The script command table 0078BCA0 (0x12C commands) is copied and extended with addon\'s commands (GiveUnitWeapon, StrUpper, UnitsNearPoint, ...); the parser and executor (004B4030, 004B41E0, 004B5B00, 004B56C0, 004B6760, 004CF470, 004CF600, 004CFBC0, 004D0B10, 004D0BA0, 004D1330, 004D41C0) use the new table and dispatch the new commands.'),
 'PersistentGlobalVars': ('scripting', 'yes', 'Global script variables named "gvar.*" survive where the game empties a variable table in its multiplayer paths (0057EB10 the party outside a game, 005800C0 the client update, 00662FB0 the saved parties): addon copies them into its own std::unordered_map (MSVC 2010) and sets them again in that list\'s order.'),
 'MenuMultiDisable': ('infrastructure', 'no', 'Disabled here. Mod option: the main menu\'s multiplayer buttons off (00633234, 0063353C).'),
 'MenuSingleDisable': ('infrastructure', 'no', 'Disabled here. Mod option: the main menu\'s single player buttons off.'),
 'SavesPath': ('resources', 'no', 'Disabled here. Mod GameSaves: the saves\\ and mp\\ paths moved into the mod folder (string pointers in 00456440, 0057A..., 00655..., 00615622).'),
 'MpZone': ('multiplayer', 'no', 'Disabled here. Mod [MPZone]: the multiplayer zone names for Gipath, Ingos, Suslanger.'),
 'PreventZone1Animation': ('content', 'yes', 'Disabled here. Mod option: skips the zone 1 intro animation (0058FA13).'),
 'ChatLog': ('multiplayer', 'no', 'Disabled here. Mod [Dev] ChatHtmlFile: the chat is also written to an HTML file (0041B25B).'),
 'Maps': ('resources', 'yes', 'Zone files (.mob/.mpr) are looked up in Mods\\<mod>\\Maps and Rebirth\'s maps first: ten call sites in the zone loaders (00595480 NewZone, 00595BA0, 00567AB0, 0057F560, 005809D0, 005812F0, 005813A0) and the map path setter 0055E990.'),
 'ResOverride': ('resources', 'yes', 'Resource archives (.res): open/close/init and every file find go through addon, so files in the mod\'s Res folder (and Rebirth\'s overrides) replace the game\'s; 24 call sites and the res file-find vtable slot 007432F4.'),
 'ConfigMusicReg': ('resources', 'no', 'Disabled here. Mod [Config] Music.reg.'),
 'MoviesPatch': ('resources', 'yes', 'The BinkOpen import slot (0073B720) points at addon: movies are looked up in the mod\'s movies folder first.'),
 'CameraPatch': ('resources', 'yes', 'Camera files (Camera\\*.cam, 0040ECD0) are looked up in the mod\'s Camera folder first.'),
 'ConfigStreamSNReg': ('resources', 'no', 'Disabled here. Mod [Config] StreamSN.reg.'),
 'StreamPatch': ('resources', 'yes', 'Music stream files (006BB3A0) are looked up in the mod\'s Stream\\ folder first.'),
 'ForceShowTutorial': ('content', 'yes', 'Disabled here. Mod option: the tutorial hints always on (0045C028, 00652663).'),
 'ConfigMovieIni': ('resources', 'no', 'Disabled here. Mod [Config] Movie.Ini.'),
 'ConfigLightsIni': ('resources', 'no', 'Disabled here. Mod [Config] lights.'),
 'ConfigTutorialIni': ('resources', 'no', 'Disabled here. Mod [Config] Tutorial.Ini.'),
 'EiPluginInit': ('infrastructure', 'no', 'EI_Plugin.asi\'s loader hook: when Miles loads it in AIL_startup its DllMain writes CALL 10001BD0 at 006B6272 (right after AIL_startup in the CSoundManager constructor 006B6040) and markers at 0073AF10/0073AF20; that call applies the features below, then runs the original 006B73E0.'),
 'EiPluginPK': ('multiplayer', 'no', 'EI_Plugin.ini PK (0 here): player killing in multiplayer.'),
 'EiPluginNPlayers': ('multiplayer', 'no', 'EI_Plugin.ini NPlayers (7 here): number of players in a multiplayer game (lobby screens 0061B2C0, 00640060, 00608D20, 006095A0).'),
 'EiPluginPause': ('multiplayer', 'no', 'EI_Plugin.ini Pause (1 here): pause in multiplayer.'),
 'EiPluginAutoAim': ('multiplayer', 'no', 'EI_Plugin.ini AutoAim (0 here).'),
 'EiPluginGraphGen': ('infrastructure', 'no', 'EI_Plugin.ini GraphGen (0 here): AI graph generation.'),
 'EiPluginTest': ('infrastructure', 'no', 'EI_Plugin.ini Test (0 here): test script commands.'),
 'EiPluginNewMaster': ('multiplayer', 'no', 'EI_Plugin.ini NewMaster (default 1): the new master server (lobby list, login); the Master Server Name is no longer saved by GameOptions::SaveToRegistry (0045C560, rewritten); a network thread.'),
 'EiPluginScripts': ('scripting', 'no', 'EI_Plugin.ini Scripts (default 1, switched off by addon\'s mode bit 0x80: moved to addon\'s AddNewScriptCommands / PlayerNumbers).'),
 'EiPluginBriefings': ('multiplayer', 'no', 'EI_Plugin.ini Briefings (default 1): 00592090 (code replaced and four jump offsets moved by 0x14).'),
 'EiPluginFixes': ('multiplayer', 'partly', 'EI_Plugin.ini Fixes (default 1): checks for a 0x66 byte at +4 inserted in 00613EA0, 005ED7B0, 006091C0, 006407E0; a jump in 005F13A0; 00536100 NOPs; two jumps redirected into the plugin.'),
 'EiPluginUnload': ('infrastructure', 'no', 'Written when the plugin unloads (game exit) if its state has bit 2: 00455980.'),
}

STATUS = {
 'FixLeversActivate': 'implemented: LockKey::Opens (src/game/unit_stats.cpp)',
 'FixMoveObject': 'implemented: CSectorMap::Move (src/game/sector_map.cpp)',
 'AllowScriptArgsInLoops': 'implemented: CScript::LoadCommands (src/game/script_parse_load.cpp); Rebirth comparison waits for AddNewScriptCommands (same function)',
 'FixPlaingBattleMusicFromAntimagic': 'implemented: new rewrite CUnitServer::IsFighting 0054B580 (src/game/combat.cpp)',
 'FixRepairPrice': 'implemented: new rewrite CCampPages::Price 0060FAB0 (src/game/ui_camp.cpp)',
 'FixProgressNames': 'implemented: new rewrite SelectLoadingMovie 00595080 (src/game/world_zone.cpp)',
 'FixRemoveMapObject': 'implemented: new rewrite CWorldServer::RemoveObject 005935C0 (src/game/world_server.cpp)',
 'AllowScriptsInMainMenu': 'implemented: new rewrite GameFlow::StartZone 0057C460 (src/game/world_game.cpp)',
 'FixCreateItem': 'implemented: new rewrite CItemManager::CreateItem 006AC970 (src/game/item_constructor.cpp)',
 'FixAddMapObject': 'implemented: new rewrite CWorldServer::CreateObject 005927C0 (src/game/world_units.cpp)',
 'NewStringEvents': 'implemented: new rewrite CInterface3D::OnStringEvent 005F8070 (src/game/ui_interface_frame.cpp)',
 'ImproveConsole': 'implemented: new rewrite ConsoleWindow::OnChar 005E44A0 (src/game/ui_console_window.cpp)',
 'AddNewConsoleCommands': 'implemented: Execute 005DC620 (src/game/console.cpp) + new rewrite CBattleScreenSetup::Draw 00636020 (src/game/ui_battle_setup.cpp); commands in src/game/console_addon.cpp',
 'AutoExec': 'implemented: new rewrites GameFlow::LoadGame 0057ABA0, GameFlow::StartNewGame 0057BD10, GameFlow::ProcessRequest 0057C700 (src/game/world_game.cpp); lines in src/game/console_addon.cpp',
 'PersistentGlobalVars': 'implemented: new rewrites GameFlow::AfterServerStart 0057EB10, GameFlow::UpdateFromServer 005800C0, SavedPartyList::Reload 00662FB0 (src/game/world_game.cpp); addon\'s container order in src/game/persistent_globals.cpp',
}
SITE_STATUS = {
 ('FixSpellSelfCasting', 0x0068B770): 'implemented: CSpell::IsOffensive (src/game/spell.cpp)',
 ('FixSpellSelfCasting', 0x00639447): 'implemented: new rewrite CBattleScreen::SelectUnit 00638EF0 (src/game/ui_hud.cpp)',
 ('FixSpellEffectsOverflow', 0x0050F1E0): 'implemented: new rewrite CMapObject::AddEffect 0050F1E0 (src/game/map_object.cpp)',
 ('DisableExitOnCrashReport', 0x004800A7): 'implemented: new rewrite FatalError 00480080 (src/game/app.cpp; the addon.log line is not reproduced)',
 ('DisableExitOnCrashReport', 0x004800C9): 'implemented: new rewrite FatalError 00480080 (src/game/app.cpp)',
 ('DisableExitOnCrashReport', 0x00564330): 'implemented: new rewrite CWorldClient::Segment 005641F0 (src/game/world_client.cpp)',
 ('FixSpellEffectsOverflow', 0x0058D82E): 'implemented: new rewrite CWorldServer::UpdateUnits 0058D670 (TickSleepingEffects; src/game/world_tick.cpp)',
}
# What addon writes at each site, for the sites worth saying more than the patch summary.
SITE_SUMMARY = {
 ('PersistentGlobalVars', 0x00663083): 'JNE target moved from 00663129 to 0066316C: the inlined clear of the loaded records\' variables is skipped',
 ('PersistentGlobalVars', 0x00663170): 'context hook (mode 0: the replaced count = 0 is dropped): addon 1001E3C1 clears the table keeping the gvar. entries',
 ('PersistentGlobalVars', 0x0057EB25): 'CALL 00461540 -> addon 1001E3C1 (the table cleared keeping the gvar. entries)',
 ('PersistentGlobalVars', 0x00580177): 'CALL 00461540 -> addon 1001E3C1',
 ('AllowScriptArgsInLoops', 0x004D1614): 'JMP to a stub: the recursive LoadCommands call for the loop body gets the caller\'s locals ([esp+0x19C]) as its 4th argument instead of 0',
 ('AllowScriptsInMainMenu', 0x0057C69F): 'JMP to a stub (addon 10017FC6): MOV EDI,4; MOV DWORD [007B00C0],1 (CWorldServer+0x160); back to 0057C6A4',
 ('FixLeversActivate', 0x00522AB6): 'JMP to a stub: lock level 0 -> key level + 1; else signed key level >= lock level (vanilla: SBB/INC unsigned compare)',
 ('FixSpellEffectsOverflow', 0x0058D82E): 'context hook before the original instructions: for each map object (007C2D00 = the server 007AFF60 + 0x12DA0) that is referenced, not flagged 1, has effects, a companion (+0x170) and +0x174 <= 0 (asleep), every effect\'s ticks are counted down; if one ran out the object goes back into the update map (0058D5D0), whose update removes it',
 ('FixSpellEffectsOverflow', 0x0050F1E0): 'function replaced (thiscall adapter to addon 100181D5): at most 20 effects; the original adds; then older duplicates of an effect lasting > 1 tick are removed',
 ('FixProgressNames', 0x00595130): 'context hook after the original MOV (progres.bik): allod gipat2 -> progres2.bik, jigran -> progres5.bik',
 ('AddNewConsoleCommands', 0x006360DF): 'call replaced (addon 100162D7): while nogui asks for it, every panel of the battle screen gets SetEnabled(GUI shown, 0) (006002A0), then the original 00600EF0',
 ('AddNewConsoleCommands', 0x005DD565): 'context hook: registers help (wrapped), addon, execs, listgvar, drawall in 007C7B38 and nogui in 007C7B28',
 ('ImproveConsole', 0x005E44DD): 'context hook: a line equal to the last history entry skips the history add (to 005E4503)',
 ('ImproveConsole', 0x005E46CF): 'context hook: Ctrl+A/C/X/V on the input line (clipboard)',
 ('NewStringEvents', 0x005F80EF): 'context hook: event "gameover" -> sound and game over message, then 005F8259',
 ('FixSpellSelfCasting', 0x0068B770): 'function replaced (thiscall adapter to addon 100185DC): offensive table extended to types 0x28 and 0x29',
 ('FixSpellSelfCasting', 0x00639447): 'MOV CL,0; NOP (the argument from +0x7F of the selected spell becomes 0)',
 ('FixMoveObject', 0x005C11D0): 'function replaced (thiscall adapter to addon 10018EF1): x < 0 or y < 0 or NaN -> false, else the original (failures logged)',
 ('FixAddMapObject', 0x00592F68): 'context hook: ESI null -> log and ESI = 0, else ESI = [ESI+8]; EDX += ECX',
 ('FixPlaingBattleMusicFromAntimagic', 0x0054B5F6): 'JMP short +8 over CMP EAX,0x1A (type 0x1A no longer returns 1)',
 ('FixRepairPrice', 0x0060FADC): 'context hook: wear = clamp(1 - max(durability,0) / max(maximum,0), 0, 1) stored at [esp+0x14]; EAX = [ESI], ECX = ESI; to 0060FAF0',
 ('FixRemoveMapObject', 0x00593615): 'context hook: object id not found -> log and jump to the epilogue 005939C7',
 ('FixCreateItem', 0x006ACBBD): 'context hook: no prototype -> log "GiveItem: can\'t find prototype" and go to the failure exit 006ACBD1 (no FatalError)',
 ('FixCreateItem', 0x006ACCD0): 'context hook: invalid prototype or material -> log and go to 006ACBD1 (no FatalError)',
 ('DisableExitOnCrashReport', 0x004800A7): 'context hook replacing the message box call 004560D0: the text goes to addon.log',
 ('DisableExitOnCrashReport', 0x004800C9): '9 NOPs: the error counter test and exit(-1)',
 ('DisableExitOnCrashReport', 0x00564330): '5 NOPs: CALL 00665A10 before the FatalError',
 ('NivalRegistry', 0x004568EA): 'PUSH operand 007848A8 -> addon\'s key string',
 ('AutoExec', 0x0057ABE1): 'CALL 005DC620 -> addon 1001DF57 (runs the Console AutoExec lines, then 005DC620)',
 ('AutoExec', 0x0057BD30): 'CALL 005DC620 -> addon 1001DF57',
 ('AutoExec', 0x0057C9A3): 'CALL 005DC620 -> addon 1001DF57',
 ('MoviesPatch', 0x0073B720): 'import slot binkw32!_BinkOpen@8 -> addon',
 ('PlayerNumbers', 0x0066C1C2): 'CALL 00665A60 -> addon 10021B4A',
}

def generic_summary(patch, fn, orig, size):
    if fn == 'data':
        return 'data overwritten'
    if orig.startswith('(at'):
        return 'operand replaced'
    if orig.startswith('call ') and size == 5:
        return 'call redirected to addon code'
    return 'JMP to addon code (the replaced instructions are redone there)'

def wide_summary(name, fn, orig, size):
    if fn == 'data':
        return 'data/code outside the function list overwritten'
    f, off = fn.split('+')
    if off == '0':
        return 'function replaced: JMP to addon\'s widescreen version'
    if orig.startswith('(at'):
        return 'operand changed (layout constant for the wide layout)'
    if 'ret' in orig.split(';')[0] or orig.startswith('pop') or orig.startswith('mov eax, esi'):
        return 'epilogue hooked: addon adjusts what the function built'
    return 'hooked: addon code (wide layout)'

rows = []
instr = {}
for line in open(os.path.join(HERE, 'site_instr.txt'), encoding='utf-8'):
    name, a, n, fn, orig = line.rstrip('\n').split('\t')
    instr[(name, int(a, 16))] = (fn, orig)

def patch_of(sitename):
    if sitename == 'Set new Nival Registry path': return 'NivalRegistry'
    if sitename in ('ResOpen', 'ResClose') or sitename.startswith('Res'): return 'ResOverride'
    if sitename == 'Movies': return 'MoviesPatch'
    return sitename.split(' #')[0].split(' ')[0]

for line in open(os.path.join(HERE, 'addon_sites.tsv')):
    name, a, n = line.rstrip('\n').split('\t')
    if name == 'HookWinMain':
        continue
    a = int(a, 16); n = int(n)
    patch = patch_of(name)
    fn, orig = instr[(name, a)]
    cat, sp, summ = META[patch]
    if patch == 'WideScreen':
        summ = wide_summary(name, fn, orig, n)
    else:
        summ = SITE_SUMMARY.get((patch, a)) or generic_summary(patch, fn, orig, n)
    status = SITE_STATUS.get((patch, a)) or STATUS.get(patch, 'not started')
    rows.append(dict(patch=patch, source='addon', site='%08X' % a, size=n, function=fn, category=cat,
                     single_player=sp, original=orig, summary=summ, status=status, variant=''))

# Sites addon writes without logging them, and sites of the patches disabled in this install.
EXTRA = [
 ('FixProcessLogin', 0x004433D6, 5, '00443080+356', 'push edx; (+1)mov dword ptr [esp + 0x54], ecx', 'JMP to a stub: clears [edx+0x18] then runs the two instructions'),
 ('SetEiPluginMode', 0x0073AF10, 4, 'data', '00000000', 'mode word 0xAADD0080 for EI_Plugin.asi'),
 ('FixRepairPrice', 0x0060FB87, 5, '', 'fmul dword ptr [edi + ebx*4 + 0x21c]', 'JMP to a stub written directly (no addon.log line): FMUL as before, then for kind 7 FADD the repair cost (to 0060FB92) instead of the FSUB at 0060FB8E'),
 ('GameExeDebug', 0x0044C470, 6, '0044C470+0', '', 'disabled here: JMP to addon\'s debug log writer'),
 ('CommandLine', 0x004565D4, 6, '00456440+194', '', 'disabled here: JMP 004567D5 and NOPs'),
 ('SetDefaultRate', 0x00783E34, 4, 'data', '2500', 'disabled here: the default network rate'),
 ('MPConsole', 0x005DD558, 5, '005DC620+F38', '', 'disabled here'),
 ('MPConsole', 0x005DD780, 5, '005DC620+1160', '', 'disabled here'),
 ('MenuMultiDisable', 0x00633234, 4, '00632D70+4C4', '', 'disabled here'),
 ('MenuMultiDisable', 0x0063353C, 4, '00632D70+7CC', '', 'disabled here'),
 ('MenuSingleDisable', 0x00633220, 4, '00632D70+4B0', '', 'disabled here'),
 ('MenuSingleDisable', 0x00633224, 4, '00632D70+4B4', '', 'disabled here'),
 ('MenuSingleDisable', 0x00633528, 4, '00632D70+7B8', '', 'disabled here'),
 ('MenuSingleDisable', 0x0063352C, 4, '00632D70+7BC', '', 'disabled here'),
 ('MpZone', 0x0057F6B9, 4, '0057F560+159', '', 'disabled here'),
 ('MpZone', 0x0057F6C2, 4, '0057F560+162', '', 'disabled here'),
 ('MpZone', 0x0057F6E6, 4, '0057F560+186', '', 'disabled here'),
 ('MpZone', 0x0057F6EB, 4, '0057F560+18B', '', 'disabled here'),
 ('MpZone', 0x0057F71D, 4, '0057F560+1BD', '', 'disabled here'),
 ('PreventZone1Animation', 0x0058FA13, 2, '', '', 'disabled here'),
 ('ChatLog', 0x0041B25B, 5, '', '', 'disabled here'),
 ('ConfigMusicReg', 0x006B477E, 5, '', '', 'disabled here'),
 ('ConfigStreamSNReg', 0x006BAE8B, 5, '', '', 'disabled here'),
 ('ForceShowTutorial', 0x0045C028, 6, '0045BFF0+38', '', 'disabled here'),
 ('ForceShowTutorial', 0x00652663, 6, '', '', 'disabled here'),
 ('ConfigMovieIni', 0x004582DB, 5, '', '', 'disabled here'),
 ('ConfigLightsIni', 0x005639BE, 5, '', '', 'disabled here'),
 ('ConfigTutorialIni', 0x005F4D9C, 5, '', '', 'disabled here'),
 ('ConfigTutorialIni', 0x005F5048, 5, '', '', 'disabled here'),
]
for a in (0x456C5F, 0x456C8C, 0x57A290, 0x57A813, 0x57AC6C, 0x57B4AE, 0x57BD51, 0x57BE90, 0x57C9C4, 0x57ED85,
          0x57AC4E, 0x57B498, 0x57BC65):
    EXTRA.append(('SavesPath', a, 4, '', 'operand 007AF460 (the saves path)', 'disabled here: string pointer'))
for a in (0x655677, 0x656EF6, 0x6572ED, 0x6575F4, 0x655B97, 0x655D7E, 0x615622):
    EXTRA.append(('SavesPath', a, 5, '', 'call', 'disabled here: CALL redirected'))
for patch, a, n, _fn, orig, summ in EXTRA:
    cat, sp, _ = META[patch]
    rows.append(dict(patch=patch, source='addon', site='%08X' % a, size=n, function='', category=cat,
                     single_player=sp, original=orig, summary=summ,
                     status='n/a: disabled in this install' if summ.startswith('disabled') else STATUS.get(patch, 'not started'),
                     variant=''))

PLUGIN = [
 ('EiPluginInit', 0x006B6272, 5, 'always', 'CALL into the plugin after AIL_startup'),
 ('EiPluginInit', 0x0073AF20, 4, 'always', 'marker'),
 ('EiPluginNPlayers', 0x0061BB82, 1, 'always', 'player count - 1'),
 ('EiPluginNPlayers', 0x0061BAF7, 2, 'always', 'bytes'),
 ('EiPluginNPlayers', 0x00640069, 4, 'always', 'bytes'),
 ('EiPluginNPlayers', 0x00608D9D, 4, 'always', '0'),
 ('EiPluginNPlayers', 0x00608DE4, 4, 'always', '0'),
 ('EiPluginNPlayers', 0x006095FB, 4, 'always', '0x2E8'),
 ('EiPluginNPlayers', 0x0064016B, 0x1B, 'nplayers>7', 'bytes (more than 7 players)'),
 ('EiPluginNPlayers', 0x006401C0, 4, 'nplayers>7', 'bytes'),
 ('EiPluginNPlayers', 0x006401D7, 8, 'nplayers>7', 'bytes'),
 ('EiPluginNPlayers', 0x006401DF, 3, 'nplayers>7', 'bytes'),
 ('EiPluginNPlayers', 0x006401E2, 4, 'nplayers>7', 'bytes'),
 ('EiPluginNPlayers', 0x00640240, 4, 'nplayers>7', 'bytes'),
 ('EiPluginPause', 0x0066FAFE, 4, 'pause12', 'JMP +0x62, NOPs'),
 ('EiPluginPause', 0x0066FB62, 14, 'pause12', 'bytes'),
 ('EiPluginPause', 0x0066FB10, 5, 'pause12', 'Pause=2 only: mov eax,[edx+0x1C]; nop'),
 ('EiPluginPause', 0x0066FB2C, 1, 'pause3', 'JMP short'),
 ('EiPluginPause', 0x005942C0, 4, 'pause3', 'RET 4 at the start of CWorldServer::SetUnknown6C (rewritten)'),
 ('EiPluginAutoAim', 0x00637209, 1, 'always', '0'),
 ('EiPluginAutoAim', 0x005D45BD, 1, 'always', '0'),
 ('EiPluginGraphGen', 0x005955DD, 5, 'always', 'CALL into the plugin (00595480 NewZone)'),
 ('EiPluginTest', 0x0065C9A8, 2, 'always', 'bytes 5E 75'),
 ('EiPluginTest', 0x00664BBC, 5, 'always', 'CALL into the plugin'),
 ('EiPluginTest', 0x00553FB3, 4, 'withtest', 'call target in CUnitServer::TakeHit (rewritten)'),
 ('EiPluginNewMaster', 0x006330A9, 5, 'newmaster|test', 'CALL into the plugin'),
 ('EiPluginNewMaster', 0x0045D330, 0x2A, 'always', 'NOPs: GameOptions::SaveToRegistry (rewritten) does not save the Master Server Name'),
 ('EiPluginNewMaster', 0x0041ACB7, 2, 'always', 'NOPs'),
 ('EiPluginNewMaster', 0x0041ABDA, 5, 'always', 'CALL into the plugin'),
 ('EiPluginNewMaster', 0x0061D138, 5, 'always', 'CALL into the plugin'),
 ('EiPluginNewMaster', 0x00440F9F, 5, 'always', 'CALL into the plugin'),
 ('EiPluginScripts', 0x0066C1C2, 5, 'always', 'CALL into the plugin (addon\'s PlayerNumbers instead here)'),
 ('EiPluginBriefings', 0x005920FF, 0x19, 'always', 'code replaced'),
 ('EiPluginBriefings', 0x005920C3, 1, 'always', 'jump offset +0x14'),
 ('EiPluginBriefings', 0x005920D5, 1, 'always', 'jump offset +0x14'),
 ('EiPluginBriefings', 0x005920E9, 1, 'always', 'jump offset +0x14'),
 ('EiPluginBriefings', 0x005920F0, 1, 'always', 'jump offset +0x14'),
 ('EiPluginFixes', 0x00614BB5, 0x16, 'always', 'cmp word [ecx+4],0x6566; jne; jmp; NOPs'),
 ('EiPluginFixes', 0x00614D1B, 0x1A, 'always', 'jmp; NOPs'),
 ('EiPluginFixes', 0x005ED889, 8, 'always', 'cmp byte [ecx+4],0x66; je; jmp'),
 ('EiPluginFixes', 0x00609427, 8, 'always', 'cmp byte [eax+4],0x66; je; jmp'),
 ('EiPluginFixes', 0x006408E6, 8, 'always', 'cmp byte [eax+4],0x66; je; jmp'),
 ('EiPluginFixes', 0x00640D2F, 8, 'always', 'cmp byte [eax+4],0x66; je; jmp'),
 ('EiPluginFixes', 0x005F1401, 1, 'always', 'JMP short'),
 ('EiPluginFixes', 0x00536132, 6, 'always', 'NOPs'),
 ('EiPluginFixes', 0x006093B7, 5, 'always', 'JMP into the plugin'),
 ('EiPluginFixes', 0x00640C97, 5, 'always', 'JMP into the plugin'),
 ('EiPluginPK', 0x00554144, 1, 'always', 'JMP short in CUnitServer::TakeHit (rewritten)'),
 ('EiPluginPK', 0x0054D9F0, 6, 'always', 'NOPs in CUnitServer::ExecuteOrder (rewritten)'),
 ('EiPluginPK', 0x0058E2C4, 7, 'always', 'bytes'),
 ('EiPluginPK', 0x0058BB3B, 6, 'always', 'bytes'),
 ('EiPluginPK', 0x0058BAB9, 6, 'always', 'bytes'),
 ('EiPluginUnload', 0x00455991, 5, 'never', 'written at unload'),
]
VARIANT = {'always': 'kAlways', 'nplayers>7': 'kNPlayersOver7', 'pause12': 'kPause12', 'pause3': 'kPause3',
           'newmaster|test': 'kNewMasterOrTest', 'withtest': 'kWithTest', 'never': 'kNever'}
import bisect
sizes = [(int(m.group(1), 16), int(m.group(2))) for m in re.finditer(r'\{0x([0-9A-Fa-f]+), (\d+)\}', open(os.path.join(ROOT, 'src', 'hook', 'function_sizes.inc')).read())]
sizes.sort(); starts = [s for s, _ in sizes]
def fn(a):
    i = bisect.bisect_right(starts, a) - 1
    if i >= 0 and a < starts[i] + sizes[i][1]: return '%08X+%X' % (starts[i], a - starts[i])
    return 'data'
for r in rows:
    if not r['function']:
        r['function'] = fn(int(r['site'], 16))
active = {'EiPluginInit', 'EiPluginNPlayers', 'EiPluginPause', 'EiPluginNewMaster', 'EiPluginBriefings', 'EiPluginFixes'}
for patch, a, n, var, summ in PLUGIN:
    cat, sp, _ = META[patch]
    act = patch in active and var not in ('nplayers>7', 'pause3', 'withtest', 'never')
    rows.append(dict(patch=patch, source='EI_Plugin', site='%08X' % a, size=n, function=fn(a), category=cat,
                     single_player=sp, original='', summary=summ,
                     status=('active here' if act else 'inactive here') + '; not started', variant=var))

GROUP = {'bugfix': 'bug fix', 'widescreen': 'widescreen/rendering', 'rendering': 'widescreen/rendering',
         'scripting': 'scripting/console', 'console': 'scripting/console', 'multiplayer': 'multiplayer',
         'resources': 'resources/paths', 'infrastructure': 'infrastructure', 'content': 'infrastructure'}
for r in rows:
    r['category'] = GROUP[r['category']]
with open(os.path.join(ROOT, 'config', 'rebirth_patches.csv'), 'w', newline='', encoding='utf-8') as f:
    w = csv.DictWriter(f, fieldnames=['patch', 'source', 'site', 'size', 'function', 'category', 'single_player',
                                      'original', 'summary', 'status', 'variant'], lineterminator='\n')
    w.writeheader()
    for r in rows:
        w.writerow(r)
print(len(rows), 'rows')
