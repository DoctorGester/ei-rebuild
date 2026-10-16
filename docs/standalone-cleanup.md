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

Each needs a real implementation, or the caller must be rewritten first. The exact list is generated and always current:
`python tools/gen_originals.py` writes `docs/originals-report.md` (grep the address), and `python tools/progress.py` counts the forwarders; the
shared declarations are in `src/game/originals_<area>.h`. Grouped by what replaces them:

- **Library code** (own implementations or the modern library, after confirming identical results; see "Behaviour that the replacements must keep"):
  the `stlp` node allocator 00405950 / 00404D70 (`src/lib/stlp.cpp`: own pool or `std::allocator`); `gamecrt::*` (operator new/delete, tolower,
  toupper, strnicmp, stricmp, strncpy, isalpha, sprintf, rand, open/lseek/read/write/close; `src/lib/crt.*`); `game::FatalError` 00480080 and
  `game::DebugLog` 0044C470 (rewrite); `mfc::CString` members, `operator+`, `CString::Format` (own `CString`: MFC is not installed); `mfc::CFile`,
  `mfc::CFileFind` (own file classes or Win32 directly; `CFile::fileName_` is a raw `char*` only because naked forwarders cannot construct members);
  `mfc::PlexCreate` 006FE8EF / `PlexFreeDataChain` 006FE90F (own `CPlex`, the block allocator behind `CTypedPtrList`); `mfc::CArchive` 00708832 /
  00708941 / 00708B3D / 00708B0F; the STLport / MFC container members (CArray / CList instantiations, hash_map, string, CStringArray, CWordArray
  `SetSize` 006FEEFA of the clip work buffer at 0079B668) that the script, class-template, party, world-zone, unit, model, sound and effect files
  forward (own containers).
- **Game code not yet rewritten**, per area as the `EI_ORIGINAL` declarations of the file show: texture parsing (`ParseMmpHeader` 004E7C40,
  `FindFormat` 004E7DA0, `CreateFromMmp` 00690430, `LoadBmp` 00690260), terrain (`SectorAt` 006C8140, `GroundAndWaterDepth` 006C8930), resource
  constructors (`ConstructMappedResource` 00671280, `ConstructMmioResource` 006721D0), `ChunkArchive` Enter/Leave/Field/StringField/CountChunks
  (004098D0, 00409980, 004092B0, 00409470, 00409A60), `NetGame::Step` 0057F190, `BufferCursor::BeginRead` / `BeginWrite` 004DA450 / 004DA490,
  `BitStream` bit helpers 004A5610 / 004A5650 / 004A54D0 / 00461190, the script loader (`CScript::Error` 004CF470, command blocks 004BF540 /
  004BF900, `RunWorldScript` 004BCC60, the 004B savegame loaders), world zone loading (unit creation 0058EF20, RemoveObjectFromServer 005935C0
  (Rebirth), map path 00595BA0 (Rebirth), .mob object reader 00481660, unit record builder 00661050 / 006628D0 / 006611D0), the interface screens
  (full lists in `docs/notes/ui.md`, "Screens", "Large interface functions", "Interface screens"), lightning (`CLightning::Draw` 00467AD0), sound
  (Play3D 006B8600, Play2D 006B8250, LoadSample 006B89F0, `_CIacos` 006E7320; Miles through the import table 0073B750..0073B808; link Miles
  directly), effect objects (sound system, light manager, particle source, scorch, camera shake, bolt), player messages (variables table 00460BC0 /
  00460AF0 / 00460DB0, state payloads 0047DA80 / 006D4570), the window surface blit (DirectDraw error report 004F0B00; goes with the old renderer),
  resources (`_access` 006EAF20, `_findfirst` 006EB832, `_splitpath` 006E8BEE), 3D objects and the shadow (globals in `docs/notes/map_objects.md`,
  "Facts"), unit motion / movement, unit creation and armour models, and the AI movement helpers (`docs/notes/ai.md`).
- **Inline-assembly calls** of `_CIpow` 006E81A0, `_ftol` 006E853C, `_CIfmod` 006E95AA, `_CIacos` and a naked FPATAN: the curves must keep their
  FYL2X + `_CIpow` results; rewrite as own maths proven bit-identical.
- Globals that become ordinary variables: 00791988 (direction table), 007912BC (class heights), 007C2F70 / 007C2F78 (shape cache), 007A9D40 (random),
  0079CE78 (noise table), 0079C1F8 / 0079C838 (noise curves), 00787C8C (sway frequencies), 007AF61C / 007AFCA8 (world clock).

## Indirections

- Calls routed through an original address so a mod's patch there still runs (e.g.
  `DestroyAtOriginalAddress` in `src/game/resource.cpp`, the `Original` forwarders between the
  script loader functions in `src/game/script_parse_load.cpp`). Standalone: call the function directly.
- `KeywordTable()` (`src/game/script_parse.cpp`) reads the script keyword table's address from the
  code of 004CF600 (Rebirth points it at an extended table). Standalone: the table itself.

- The new renderer (`src/render`) reads the old Direct3D 7 device's state for every mesh pass (`render_mesh.cpp`, `ReadPassState`: the materials'
  state blocks are opaque) and reads textures and the interface windows' pixels out of the old DirectDraw surfaces (`render_textures.cpp`).
  Standalone: the materials from the game's data, textures from the .mmp files, the window surfaces as CPU buffers (or native drawing,
  `docs/notes/renderer.md` section 5). The window, cursor, cursor image and movie holders already work that way in the switch-over (`present` with
  `cpuwindows`, section 14): their pixels are only in the new renderer's memory (`render::SurfaceMemory`); outside it the same memory is still
  handed to DirectDraw as client memory, which goes with the old path.

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
- The effect objects' naked x87 helpers (`StepFraction`, `SetLightRange`, `WallScorchShape`,
  `RandomAngle`, `CopyThroughFpu` in `src/game/world_effect*.cpp`) repeat the original's FPU
  sequences (unrounded intermediates, FILD qword, FLD / FSTP copies). Keep them or prove the plain
  C++ gives the same bits at the FPU precision the game runs with.
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
- Effect objects and the sound file reader: see "Original bugs and oddities kept" in
  `docs/notes/effects.md` and `docs/notes/models.md` (a stopped sound is still moved, the ambient sound's count read into its
  argument slot, `_findfirst` handle leak, Seek origins).
- Delta replication, chunked files, clipper, network pump: see the "kept" lists in
  `docs/notes/network.md` and `docs/notes/core.md` (e.g. the 16-bit float packing's mantissa carry,
  `ReadQuad`'s order).

## Functions left original because Rebirth patches them

Rebirth's `addon.dll` (or another Rebirth component) patches these in memory, so the hybrid build cannot replace them. The
standalone build still needs source for them (vanilla behaviour):

- `CMappedResource::OpenResource` 00671730, `CMappedResource::~CMappedResource` 006714A0: now
  rewritten and verified through the vanilla launch (`launch-vanilla.cmd`); skipped under Rebirth.
- `CSectorMap::Move` 005C11D0: addon.dll's FixMoveObject replaces it (addon.log lists the site;
  docs/notes/rebirth.md). The rewrite reproduces the patch behind its flag, so with the flag on (as
  FollowAddon sets it in a Rebirth launch) it is hooked there too.

## Rebirth patch flags

- `ei::rebirth` (`include/ei/rebirth.h`, `src/lib/rebirth.cpp`): the flags are plain data read from
  `ei_rebirth.ini` next to the DLL; the standalone exe should read it next to itself (call
  `LoadSettings` first thing). `FollowAddon` and everything in `src/hook/rebirth_hook.cpp` (site
  table, detection of addon.dll / EI_Plugin.asi, trampoline re-targeting) only exist for the hook DLL.
- `EI_REBIRTH_IMPLEMENTS` declarations are only read by the hook installer; they can stay or go.
- Forwarder `EffectArray::SetSize` (map_object.cpp) -> 0050FE70 CArray<MapEffect>::SetSize.
- `CCampPages::Price` calls the item's price through its vtable (slot 16); fine standalone.

## Original functions reached only from rewritten code

Not ported because every caller is rewritten; dead once the hooks go (the standalone build drops them):
script area (A-S5) 004C17F0 (SSCCmd::Execute), 004DEBC0 (TextSourceList::Find, from GetItemTexts), 004D0AF0 (LoadFunction), 004D3650 / 004D3670 / 004D6310
(CScript::Load's #define pair and list),
004C17E0 (a counted-pointer release, 0069AC20 and unwind funclets). 004D5880 (LoadFile's CStdioFile read) and the
static `CSysInfo` object's code 004DDCB0-004DE8B0 are ported (A-E2); the object is still built by the static
initialiser table before the hooks and needs the standalone build's own startup.
