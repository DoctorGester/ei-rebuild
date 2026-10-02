# Cleanup for the standalone exe

Everything here exists only because rewritten code currently runs inside the original game.exe.
When the whole game builds as its own executable, each item has to be removed or replaced.
Add an entry whenever a new shim, stub or workaround lands.

## Hook framework (remove entirely)

- `src/hook/` (DLL entry, hook installer, WinMain deferral, `function_sizes.inc`) and `tools/launcher/`.
- `EI_REPLACE`, `EI_REPLACE_METHOD`, `EI_VERIFY` registrations at the bottom of each source file.
- `tests/verify/` wrappers and `verified.txt`. Keep the comparisons that make sense as unit tests.
- Virtual-method hook shims (`OpenFromMemoryHook`, `MmioGetFileHook`, ... in `src/game/resource.cpp`).
  They only exist because a pointer to a virtual member is a dispatch thunk. Delete them; the virtual
  methods keep their definitions.

## Calls into original code (`EI_ORIGINAL` forwarders)

Each needs a real implementation, or the caller must be rewritten first.

| Forwarder | Where | Replace with |
|---|---|---|
| `Terrain::SectorAt` 006C8140, `Terrain::GroundAndWaterDepth` 006C8930 | `src/game/terrain.cpp` | rewrite |
| `ConstructMappedResource` 00671280, `ConstructMmioResource` 006721D0 | `src/game/resource_manager.cpp` | real constructors |
| `stlp` node allocator 00405950 / 00404D70 | `src/lib/stlp.cpp` | own pool or `std::allocator` (see below) |
| `gamecrt::*` (operator new/delete, tolower, toupper, strnicmp, stricmp, strncpy, isalpha, sprintf, rand, open/lseek/read/write/close) | `src/lib/crt.*` | modern CRT, after confirming identical results (see below) |
| `game::FatalError` 00480080, `game::DebugLog` 0044C470 | `src/lib/crt.*` | rewrite |
| `mfc::CString` members and `operator+`, `CString::Format` | `src/lib/mfc.*` | own `CString` (MFC is not installed) |
| `mfc::CFile`, `mfc::CFileFind` | `src/lib/mfc.*` | own file classes or Win32 directly. `CFile::fileName_` is a raw `char*` only because naked forwarders cannot construct members |
| `CTextureDevice::ParseMmpHeader` 004E7C40, `FindFormat` 004E7DA0 | `src/game/texture.cpp` | rewrite |
| `CTexture::CreateFromMmp` 00690430, `CTexture::LoadBmp` 00690260 | `src/game/texture.cpp` | rewrite |
| `mfc::PlexCreate` 006FE8EF, `mfc::PlexFreeDataChain` 006FE90F | `src/lib/mfc.*` | own `CPlex` (block allocator behind `CTypedPtrList`) |
| `CAIMap::MarkFootprint` 005B6A80, `CUnit::OnLeftSectorMap` 0050E930, `SpiralIndex` 005BF7D0 | `src/game/sector_map.cpp` | rewrite |

## Indirections

- Calls routed through an original address so a mod's patch there still runs (e.g.
  `DestroyAtOriginalAddress` in `src/game/resource.cpp`). Standalone: call the function directly.

- `src/lib/gameapi.h` calls Win32 through game.exe's import table so other mods' API hooks apply.
  Standalone: call the APIs directly (or keep the header as a thin alias). Regenerate or delete
  `tools/gen_iat.py`.

- `ei::VCall` (`include/ei/vcall.h`) calls virtual slots by number on classes whose interface is not
  declared yet (e.g. `CTextureManager` slots 19/20). Replace with real virtual calls once the class
  hierarchy (`CBaseManager` -> `CResourceManager` -> `CTextureManager`) is declared.
- Globals read by address: game root `CString` at 0079BC90 and data path `char*` at 007AF460
  (`src/game/texture.cpp`); transparency table 00791744 (`src/game/ai_map.cpp`); spiral offset tables
  007C2FC4 / 007C2FD8 / 007C2FFC (`src/game/sector_map.cpp`). They become ordinary globals.
- `CSectorMap::FindUnitsInRadius` reaches the unit grid as `aiMap + 0x124`: the sector map appears to be
  embedded in `CAIMap` there. Make it a real member once `CAIMap`'s layout is complete.

## Placeholder names and partial layouts

- `Vector3::Construct`, `CMappedFile::Construct`: these are the original constructors, written as
  methods returning `this` so they can be hooked. Turn them into real constructors.
- `CMappedResource::Slot4`, `CMMIOResource::Slot4`, `CEngineObject::Slot5/Slot6`: unnamed vtable slots.
- `CEngineObject`, `CResourceManager`, `CTextureManager`, `CTexture`, `CAnimationNode`: provisional names and partial
  layouts (`unknown_*` fields), no constructors declared.
- `CResourceManager::vtable_` / `baseManager_`, `TerrainOwner::unknown_000`,
  `CMappedResource::unknown_1C/20`: opaque bytes until the base classes are recovered.
- `using BOOL = int` in `src/game/resource.h`: return values the original sets in all of EAX.

## Behaviour that the replacements must keep

- `gamecrt::tolower` / `strnicmp` use the game CRT's locale tables (Cyrillic archive names).
  A modern replacement must give identical results for bytes 0x80-0xFF.
- STLport `hash_map<void*, ...>` iteration order depends on pointer values, so allocation order can
  change the order in which `CMappedResource::FreeAllFiles` frees files. Harmless so far, but check
  before swapping allocators.
- `x87::fistp` rounds with the FPU's current mode (used by `Terrain::HeightAt`); a C++ cast truncates.
- x87 maths (`/arch:IA32 /fp:fast`, `include/ei/x87.h`) and the FPU control word the game runs with
  (Direct3D sets single precision) must stay the same, or float results drift.

## Original bugs kept on purpose

Decide case by case whether the standalone build keeps them (behaviour-exact) or fixes them.

- `CMappedResource::CloseFile`: the write-failure message prints the data pointer as the file name.
- `CMappedResource::GetFile`: leaks the data buffer when the read fails.
- `CMappedResource::GetFile` on an in-memory archive does not report the size.
- `CTextureManager::LoadTexture` / `LoadTextureFrom`: leak the file buffer when the .mmp header is
  not recognised; the .bmp path reports format number 0 in its error message.

## Functions left original because Rebirth patches them

Rebirth's `addon.dll` (or another Rebirth component) patches these in memory, so the hybrid build cannot replace them. The
standalone build still needs source for them (vanilla behaviour):

- `CMappedResource::OpenResource` 00671730, `CMappedResource::~CMappedResource` 006714A0: now
  rewritten and verified through the vanilla launch (`launch-vanilla.cmd`); skipped under Rebirth.
- `CSectorMap::Move` 005C11D0: its first byte is patched in memory at runtime (not listed in
  addon.log, so probably `EI_Plugin.asi`). Source written in `src/game/sector_map.cpp`, never run.
