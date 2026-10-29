# Cleanup phase 1: script and console files (ticket C2sc)

How the mission script runtime (`script_reset.cpp`, `script_save.cpp`, `script_blocks.cpp`, `script_parse_load.cpp`, `script_addon*.cpp`,
`script_exec_helpers.cpp`, `script_group.cpp`, `script_vars.cpp`) and the developer console (`console.cpp`, `console_commands.cpp`,
`console_addon.cpp`, `console_window.cpp`) read their objects after the cleanup. The engines themselves are in `script.md` and `console.md`.

## Declarations used

- `CScript` (script.h) is laid out whole; the reset, save and block code walk its typed members (`pointLights`, `particles`, `fxSources`,
  `lightning`, `blocks`, `definitions`, `globals`, `instances`, `runs`, `worldBlocks`, `areas`, `tempGroups`, `tempObjects`, `forDepth`,
  `loaded`, `fileCount`); `RemoveAll` / `WriteList` are templates over the container. `SSCCmd::calledScript->id` is the called block's id.
- The parser's `VarsAt` / `DefinitionAt` / `TokenListAt` views are typed `__fastcall` forwarders (`VarsFindVariable`, `VarsHasVariable`,
  `VarsAppendFrom`, `VarsResetValues`, `VarsClear`, `DefinitionReset`, `TokensDestruct`), still through the original addresses as the file's header
  asks.
- A unit's controller (+0x240) is `CUnit::controller`; the addon's death test is `CMapObject::IsDead` (slot 55, a real virtual call: `jmp
  [eax+0DCh]`); CreatePointLight's light comes from `CBaseManager::CreateObject` (slot 23, `[vtable+5Ch]`) of the world's timed-object manager
  (`WorldServer()->timedObjectManager`, 007AFFA8).
- Console: the interface is `CInterface3D` (`SetConsoleRect`, `SetConsoleHistory`, `ShowFps`, `ConsoleAddText`, `fade->Start`),
  the debug display switches are `CWorldClient::Show* / Hide*`, the world is `WorldServer()` (`LootDeadUnits`; players through the shared
  `WorldGetPlayer`), a player is `CPlayer` (`Server()`, `Party()->members` / `variables` / `Money()`, `logTraffic` +0x10C), the local player is
  `LocalView()->player`, the network client is `net::GameClient()` (active unless `flags7 & kDetached`; the host test reads
  `BindingMessage::PlayerRef()` as a `PlayerMessage::host`), the frame timer is `CSceneRenderer::FrameTime` (slot 6).
- The original's CTime (006FE3BB `Construct`) is lib/calendar.h and CString::FormatV (006FDC12) `str::Format`; `ConsoleStreambuf::StreambufConstruct` /
  `StreambufDestruct` (the old iostream base, 006FCAD2 / 006FCB35); `CUiWindow::VirtualPlayerList(mode)` (slot 38, kick / ban) and
  `VirtualDebugFill()` (slot 39, "give assortment"). `listgvar` reads `CScript::globals` of the console script 007C27D4 (`ScriptObjectRef`,
  `ScriptGroup`, `CEngineObject::ClassName` slot 11, `id_`); `execs` prints its `errorLine` / `errorText`.
- `RunScriptText` (005785D0) stays a call through its address. The player state calls keep the shared forwarders
  (`PlayerStateSetVariableOriginal` with a computed float, `PlayerStateRefreshOriginal`, `PlayerResendPartyOriginal`).

## Left

- `tests/verify/verify_script_exec.cpp` still uses `CScript::Field(offset)` (script.h) and `Field<int>`; with it gone the member can be removed.
- `EffectManager()` is defined in three script files (script_effects, script_exec_run, script_exec_helpers); the local `World` views
  (`RemoveTimedObject` / `AddTimedObject` through the forwarders) remain in script_reset and script_exec_helpers.
- Per-file `Interface()` accessors for 007C7D50 remain.
- Raw game addresses (strings, globals such as 0079BC60, 00783E34) and `__asm` blocks are not covered.
