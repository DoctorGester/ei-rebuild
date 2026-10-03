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
| Unit motion / movement: CArray instantiations 005C8400 / 005C87F0 / 005C8950 / 005C8560, cubic value 004E5660, unit layer list NewNode 005BE7B0, shape cache helpers 005BCCB0 / 005BD7F0 / 005BE320 / 005BE450 / 005BE370 / 005BE280 / 005BE730, cell passability 005B8BA0, visibility grid growth 005C8D40; globals 00791988 (direction table), 007912BC (class heights), 007C2F70 / 007C2F78 (shape cache) | `src/game/ai_motion.cpp`, `ai_movement.cpp`, `ai_unit_layer.cpp` | rewrite / own containers (see `docs/notes/ai.md`) |
| Units: CObject3D 00517700 / 00517500, CMapObject 0050E890 / 0050E970, CUnitServer 00552F40 / 00552780, CUnit 00532AC0, prototype lookup 00514930; `_CIpow` 006E81A0 and `_ftol` 006E853C in inline assembly; the ostream inserters behind `UnitLogStream` (full list in `docs/notes/units.md`, second round) | `src/game/unit_*.cpp` | rewrite / own maths (the curves must keep FYL2X + `_CIpow` results) |
| Models: `ChunkArchive` Enter/Leave/Field/StringField/CountChunks (004098D0, 00409980, 004092B0, 00409470, 00409A60); record destructors and `vector::resize` of the prints/acks records (006D29A0, 006D29E0, 006D26E0, 006D2830, 006DC390, 006DC920, 006DCAF0, 006DB9C0, 006DBBD0, 006DBE20); acks record Serialize (006E3780, 006E3FD0, 006E3DC0); water level apply 006CC7F0; link loader list insert/erase (004FB2E0, 004FAE00); `ConstructMmioResource` 006721D0 (second copy) | `src/game/model*.cpp` | rewrite / own containers (see `docs/notes/models.md`) |
| Script loader: `CScript::Error` 004CF470, command blocks 004BF540/004BF900, `RunWorldScript` 004BCC60, `SSCCmd::Clear`/destructor, `ScriptDefinition`/`ScriptVars` constructors, destructors and helpers, the CArray/CList members of the script types, CString Mid/Left/Right/Find/LockBuffer, CRT isdigit/isspace/strncmp/atof (full list in `docs/notes/script.md`, "Loader and parser") | `src/game/script_parse*.cpp`, `script_parse.h` | rewrite / own containers |
| `NetGame::Step` 0057F190 (per-frame network step) | `src/game/net_session.cpp` | rewrite |
| `BitStream::StartByte` 004A5610, `EndBits` 004A5650, `Resize` 004A54D0, `ReadString` 00461190 (CMemoryStream bit helpers) | `src/game/net_delta.cpp` | rewrite with CMemoryStream |
| `WordArray::SetSize` 006FEEFA (MFC CWordArray of the clip work buffer at 0079B668) | `src/game/clip.cpp` | own array |
| Interface screens: sound 005F7E60, help 005F86F0, window/container base slots (005FEB90, 005FECC0, 00600270, 006010E0, 00600EF0, 00601040, input defaults 00601110 / 00601570), surface/model/region helpers (005FD760, 005FC2F0, 005FC690), renderer flushes 004E7840 / 004E7A40, battle screen and panel helpers, save/load and options actions, local view orders 0066AE70 / 0066AED0, cursor 005E5200, server command 0041D260, sound setters 006B7740 / 006B7A00 / 006B78A0, display settings 00500960 (full list in `docs/notes/ui.md`, "Interface screens") | `src/game/ui_hud.cpp`, `ui_menus.cpp` | rewrite |

## Indirections

- Calls routed through an original address so a mod's patch there still runs (e.g.
  `DestroyAtOriginalAddress` in `src/game/resource.cpp`, the `Original` forwarders between the
  script loader functions in `src/game/script_parse_load.cpp`). Standalone: call the function directly.
- `KeywordTable()` (`src/game/script_parse.cpp`) reads the script keyword table's address from the
  code of 004CF600 (Rebirth points it at an extended table). Standalone: the table itself.

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
- `StoreRatio` (`src/game/clip.cpp`, inline x87) stores the clip parameter as a float while comparing
  the unrounded quotient; `FpuCopyBits` (`net_delta.cpp`, `chunk_stream.cpp`) copies floats through
  FLD/FSTP so a signalling NaN comes out quiet, as in the original. Keep both or prove it cannot matter.
- x87 maths (`/arch:IA32 /fp:fast`, `include/ei/x87.h`) and the FPU control word the game runs with
  (Direct3D sets single precision) must stay the same, or float results drift.

## Original bugs kept on purpose

Decide case by case whether the standalone build keeps them (behaviour-exact) or fixes them.

- `CMappedResource::CloseFile`: the write-failure message prints the data pointer as the file name.
- `CMappedResource::GetFile`: leaks the data buffer when the read fails.
- `CMappedResource::GetFile` on an in-memory archive does not report the size.
- `CTextureManager::LoadTexture` / `LoadTextureFrom`: leak the file buffer when the .mmp header is
  not recognised; the .bmp path reports format number 0 in its error message.
- `CLinkFigureLoader::AddArchive`: leaks the archive object when opening fails; its log message
  passes the path string object by value to `%s`.
- `TweenArray::Load` reads the count into the slot of its stream argument (a stream that wrote
  nothing would leave the pointer value as the count).
- Delta replication, chunked files, clipper, network pump: see "Original bugs and oddities kept" in
  `docs/notes/unmapped_410_470.md` (e.g. the 16-bit float packing's mantissa carry, `ReadQuad`'s order).

## Functions left original because Rebirth patches them

Rebirth's `addon.dll` (or another Rebirth component) patches these in memory, so the hybrid build cannot replace them. The
standalone build still needs source for them (vanilla behaviour):

- `CMappedResource::OpenResource` 00671730, `CMappedResource::~CMappedResource` 006714A0: now
  rewritten and verified through the vanilla launch (`launch-vanilla.cmd`); skipped under Rebirth.
- `CSectorMap::Move` 005C11D0: its first byte is patched in memory at runtime (not listed in
  addon.log, so probably `EI_Plugin.asi`). Source written in `src/game/sector_map.cpp`, never run.
