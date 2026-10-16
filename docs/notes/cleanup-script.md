# Cleanup phase 1: script and console files (ticket C2sc)

How the mission script runtime (`script_reset.cpp`, `script_save.cpp`, `script_blocks.cpp`, `script_parse_load.cpp`, `script_addon*.cpp`,
`script_exec_helpers.cpp`, `script_group.cpp`, `script_vars.cpp`) and the developer console (`console.cpp`, `console_commands.cpp`,
`console_addon.cpp`, `console_window.cpp`) read their objects after the cleanup. The engines themselves are in `script.md` and `console.md`.

## Declarations used

- `CScript` (script.h) is laid out whole; the reset, save and block code walk its typed members (`pointLights_`, `particles_`, `fxSources_`,
  `lightning_`, `blocks_`, `definitions_`, `globals_`, `instances_`, `runs_`, `worldBlocks_`, `areas_`, `tempGroups_`, `tempObjects_`, `forDepth_`,
  `loaded_`, `fileCount_`); `RemoveAll` / `WriteList` are templates over `mfc::CList<T>`. `SSCCmd::calledScript->id` is the called block's id.
- The parser's `VarsAt` / `DefinitionAt` / `TokenListAt` views are typed `__fastcall` forwarders (`VarsFindVariable`, `VarsHasVariable`,
  `VarsAppendFrom`, `VarsResetValues`, `VarsClear`, `DefinitionReset`, `TokensDestruct`), still through the original addresses as the file's header
  asks.
- A unit's controller (+0x240) is `CUnit::controller`; the addon's death test is `CMapObject::IsDead` (slot 55, a real virtual call: `jmp
  [eax+0DCh]`); CreatePointLight's light comes from `CBaseManager::CreateObject` (slot 23, `[vtable+5Ch]`) of the world's timed-object manager
  (`WorldServer()->manager48_`, 007AFFA8).
- Console: the interface is `CInterface3D` (`SetConsoleRect`, `SetDebugInfoRect`, `SetConsoleHistory`, `ShowFps`, `ConsoleAddText`, `fade_->Start`),
  the debug display switches are `CWorldClient::Show* / Hide*`, the world is `WorldServer()` (`LootDeadUnits`; players through the shared
  `WorldGetPlayer`), a player is `WorldPlayer` (`server`, `mercs`, `variables`, `Party()->Money()`, `logTraffic` +0x10C), the local player is
  `LocalViewData::player`, the network client is `net::GameClient()` (active unless `flags7_ & kDetached`; the host test reads
  `BindingMessage::PlayerRef()` as a `PlayerMessage::host_`), the frame timer is `CSceneRenderer::FrameTime` (slot 6).
- `mfc::CTime` (006FE3BB `Construct`) and `mfc::CString::FormatV` (006FDC12) in lib/mfc.h; `ConsoleStreambuf::StreambufConstruct` /
  `StreambufDestruct` (the old iostream base, 006FCAD2 / 006FCB35); `CUiWindow::VirtualPlayerList(mode)` (slot 38, kick / ban) and
  `VirtualDebugFill()` (slot 39, "give assortment"). `listgvar` reads `CScript::globals_` of the console script 007C27D4 (`ScriptObjectRef`,
  `ScriptGroup`, `CEngineObject::GetClassName` slot 11, `id_`); `execs` prints its `errorLine_` / `errorText_`.
- `RunScriptText` (005785D0) stays a call through its address. The player state calls keep the shared forwarders
  (`PlayerStateSetVariableOriginal` with a computed float, `PlayerStateRefreshOriginal`, `PlayerResendPartyOriginal`).

## Left

- `tests/verify/verify_script_exec.cpp` still uses `CScript::Field(offset)` (script.h) and `Field<int>`; with it gone the member can be removed.
- `EffectManager()` is defined in three script files (script_effects, script_exec_run, script_exec_helpers); the local `World` views
  (`RemoveTimedObject` / `AddTimedObject` through the forwarders) remain in script_reset and script_exec_helpers.
- `ScriptLoadError` (console.h) repeats `ScriptErrorInfo` (world_glue.h); `WorldPlayer` (world.h) and `CPlayer` (player_messages.h) declare the same
  object; per-file `Interface()` accessors for 007C7D50 remain.
- Raw game addresses (strings, globals such as 0079BC60, 00783E34) and `__asm` blocks are not covered.
