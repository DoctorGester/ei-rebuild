# Models, resources, the 3D device layer and terrain sectors

Code: `src/game/model.h/.cpp` (3D library side of figures), `model_db.cpp` (databases, water level changes),
`renderer_init.cpp`, `renderer_strip.cpp`, `renderer_sprites.cpp`, `device_mesh.cpp`, `device_draw.cpp`,
`device_shadow.cpp`, `figure_*.cpp`, `unit_skins.cpp`, `terrain*.cpp`, `resource*.cpp`. Algorithms are described in each
file's header comment. Verify: `tests/verify/verify_models.cpp` and one `verify_<file>.cpp` per file. Scenario:
`tests/scenarios/models.txt` (the draw functions run in every scenario; ribbons and sprites in `spells` / `spells_cast`).
V: P pure, S the original on a scratch copy / side by side, O ours only (they read resources, allocate, draw or free).

## Map

**004F0000-00500000, the 3D transform library**: 004F0B90 loads `3DfpFPU.dll` / `GetTLPipeline` (once, from 00500540);
004F68C0 (Rebirth-patched) is the `CLinkFigureLoader` constructor (vtable 0073D808; opens `figures.res` and `menus.res`
through 004F6D30); the link / FIG loading 004F7150..004F87D0 is in script.md ("Link figures"); 004F16A0 frustum
classification of a part's bounds (callers all over the renderer); 004F1330 (Rebirth-patched), 004F1370, 004F13F0, 004F0E80,
004F1440 / 004F15C0 matrix builders; 004F57B0 triangle index buffer resize; 004F93A0 `ClipBounds::SetBox` (called by the
figure manager for every part it loads); 004FA000-00500000 STLport instantiations, the D3D state-block manager (004FCA60,
004FD2A0, 004FD9D0) and the effect manager.

**006D0000-006E0000, databases and bitmaps**: 006D0630 (Rebirth-patched) loads `prints.db` from `database.res` (records
0x28, 0x34, 0x28 bytes), 006D7620 (Rebirth-patched) `acks.db` (0x1C4, 0x5C, 0x38), both once at startup (00456440) through
a `ChunkArchive` over a memory stream. `ChunkArchive` (ctor 0053A7B0, open 00409AE0, close 0040A000): two `CMemoryStream`s
(index and data, data buffer +0x40), open chunk list +0x50 (last node = current chunk; its +0x14 picks the occurrence the
next EnterChunk looks for), loading flag +0x58; Serialize functions read and write through the same code; a failed
EnterChunk when loading still pushes a chunk (callers never pop it). 006D4400-006D49C0: the world's water level changes
(`TweenArray`, 16-byte entries at 007AFF60 + 0x70): SetTarget (script `SetWaterLevel` via 00593FA0), Tick (every world
update from 0058DCD0), Save, Load (applies each level through 006CC7F0). 006D4A60-006D6AB0 `CDib` (vtable 00744288): BMP /
`.mmp` readers, drawing, writing; used only for loose `textures\*.bmp` overrides (0068FBF0), movie capture screenshots
(00458010) and unreferenced code; not rewritten.

## 3D library and databases: rewritten

| Address | Name | V |
|---|---|---|
| 004F16A0 | `CTransformPipeline::ClipTest` | P |
| 004F93A0 | `ClipBounds::SetBox` (returns the flags) | S |
| 004F1370, 004F13F0, 004F0E80 | `ViewportMatrix`, `ShearMatrix`, `ProjectionMatrix` (`__stdcall`, return out) | S |
| 004F57B0 | `TriangleIndexArray::Resize` | S |
| 004F6D30 | `CLinkFigureLoader::AddArchive` (runs twice at startup; weak check) | O |
| 006D23A0 / 006D25F0, 006DB430 / 006DB680 / 006DB8D0 | `ChunkArchive::SerializePrints` / `2`, `SerializeAcks` / `2` / `3` (loaded count = record chunks counted beforehand) | O |
| 006D41D0 / 006D4270 | `PrintRecord::Serialize` / `PrintRecord2::Serialize` (loading S; the original's side run can push failed chunks: `RestoreTop` pops them) | S / O |
| 006D43E0, 006D4840, 006D4410, 006D44E0, 006D4570 / 006D47E0, 006D45D0, 006D4660 | `TweenArray::RemoveAll`, `SetSize`, `SetTarget`, `Tick` (bool in AL), `Save` (identical copies; capture streams), `Load` (the original for real, recording the stream), `LoadEntries` | S |

`ClipTest` and `ProjectionMatrix` are compiled with `float_control(precise)` (/fp:fast regrouped the corner sums);
`TweenArray::Tick`'s removal test is "value == 0 or unordered", written `!(v < 0) && !(v > 0)`. Acks records are not
rewritten. Under Rebirth the database loaders (006D0630, 006D7620) and the link loader constructor (004F68C0) are patched;
whether the patched code still calls the rewritten functions is unknown.

## Renderer (00500B70-005053FF)

The renderer is the global at 007AF060 (vtable 0073D93C). +8 the transform pipeline (matrix stacks as aligned arrays of
64-byte matrices: data/count at +0x14, +0x1C, +0x24, +0x3C, +0x44, +0x4C, +0x54; +0xC the transform object, whose slots
project and clip vertices), +0x20 the counted device holder (004E5E50: +8 last HRESULT, +0xC DirectDraw, +0x14 primary
surface, +0x18 render target (+0x14 width, +0x18 height), +0x54 IDirect3DDevice7, +0xAC the pipeline again (its transform
object's slot 35 sees every vertex batch when +0x158 is set), +0x150 override colour of the mesh draws, +0xE0 vertices sent
to vertex buffers, +0x16C / +0x170 / +0x174 vertex, triangle and call counters), +0x24 the counted state-block helper
(004FCA60). Device draws hold the lock 007AEE78 and store the HRESULT at +8. Shared buffers of the mesh / flat draws:
007AEDE0 clip output, 007AEE00 TL vertices, 007AEE10 projected vertices (16 bytes each), 007AEE20 corner outcodes, 007AEE30
vertex outcode bytes (all ..007AEE3C); the vertex buffer path 004E7820 is dead (always returns null). Ribbon / sprite
buffers are function-local statics (007AF000..007AF05C, 007AEF60..007AEFF8).

| Address | Name | V |
|---|---|---|
| 00500B70 | `RendererCreate` (slot 2, RET 8) | O |
| 00501D90 | `DrawRibbon` (slot 16, RET 0x18) | O |
| 00502A20, 00503680 | `DrawTlTriangles`, `DrawIndexed` (holder thiscall; RET 0x1C) | O |
| 00502AC0, 00502B90 | `EmitSection` (naked transcription), `StripDirection` | S / P |
| 00502CA0 | `DrawSprites` (slot 17, RET 0xC) | O |
| 00503710 / 005037D0 | `SpriteRadixPass` / `SpriteMergePass` (compared only for batches over 128 / 2..128 visible particles) | S |
| 004E9010 / 004E9A50 | `DeviceDrawMesh` (device thiscall, RET 0x48) / `DeviceDrawMesh2` (two textures, RET 0x50) | O |
| 004EA4A0 | `DeviceDrawFlat` (RET 0x24) | O |
| 004EB550 / 004EB370 | `DeviceDrawIndexed` (RET 0x1C) / `DeviceDrawTwoTextures` (RET 0x24) | O |
| 005A7A70, 005A7640 | `DrawFigurePart` (node thiscall, RET 0x1C), plain `DrawFigurePart` (RET 0x18) | O |
| 005A7290, 005A0E80 | clip-box lines (finishing call of both draws), `RasteriseFigureMesh` | S through a stand-in renderer / rasteriser |
| 00568800 | `BuildUnitSkins` (zone client thiscall, RET 8) | O |

Facts not in the code comments:
- **Renderer creation**: the helper's flag argument is pushed as EDX with only DL set; the upper bytes are what the game's
  operator new left in EDX, reproduced by capturing EDX after the same allocation. The depth range call takes two arguments
  (settings +0x264, +0x1E4). The settings argument is `options::GameOptions` (video fields +0x2C..+0x78, FarClipDistance
  +0x1E4, FogDayStartDistance +0x264); `CSceneRenderer` +4 flags, +0x24 a counted `CRenderHelper` (0x118), +0x50 creation
  time; transform object slots 1 SetViewportSize, 2 AddSun, 4 SetProjection, 5 SetDepthRange, 40 LightMesh.
- **Layouts** (C9): the figure at CObject3D +0xF0 is a `CLinkMesh` (slots 7-12 draw a mesh part, 13 / 14 project /
  rasterise its shadow); 005A7A70's `this` is a `FigureMeshDrawer` (+0x1C the CFigureMesh, +0x20 its screen rect);
  CFigureMesh +0x94 drawer, +0x98 owner, +0xD4 / +0xD8 flat colour / specular; CLight +0x28 / +0x108 position / direction
  in the lit mesh's space, +0xD4 the direction; *007AF748 is `CLightFactory` (spell.h). FigurePart (map_object_figures.h)
  and CFigureMesh describe the same objects (bounds +0x188, +0x158 / +0x15C); not merged. Debug panel 0079B930: file-local
  `CDebugPanel` in string_map.cpp. Left raw in map_object_shadow.cpp: BuildMatrix's part matrix (+0x34 / +0x54, which
  FigurePart's layout contradicts), the scene's matrix stack (+0x64) and object (+0xC, slot 7), the material's colour key
  (+0x60), the device's blur slots 11 / 12, the world's slot 34.
- **Mesh draws**: callers are figures (005A7A70, 005A7640) and the 3D library's meshes (004F3C60 ...); flag 4 overwrites the
  mesh's own colour and specular arrays with the device's +0x150 colour / alpha << 24; `DeviceDrawFlat` is used by shadows
  (0051CE60), the renderer (00503A40) and 006CEDE0.
- **Figure parts**: mesh +8 flags 0x40 drawn (cleared first), 0x80 hidden (nothing more, not even the finishing call), 0x10
  lit, 0x100 flat colours; +0x98 owner (CObject3D, its figure at +0xF0 draws the mesh when present), +0x15C mesh type
  (13..16 animated), +0x188 clip bounds. With 0079BD28 set the mesh named "crown" has every colour pulled halfway to the
  average. The plain variant (005A7640) clears the drawn flag only after the hidden test, has its own moved-positions static
  (007C2E60, guard 007C2E70, freed by 005A7A50) and sets the drawn flag whenever the draw did not return 2.
- **Material draw**: the device's current material (+0x14C): +8 blend flag, +0xC / +0x10 source / destination blend, +0x14
  technique (+8 / +0xC state block handles per pass), +0x18 stage records (0x14 bytes: coordinate index, texture name, +0x10
  counted texture looked up through 00693C60 on first use).
- **Unit skins** (`BuildUnitSkins`): called by the zone load 00567AB0 (Rebirth-patched for its map paths; this function is
  not); `this` +0x18C the texture cache, +0x34 the figure maker; prototype table 007AF358, model table 007AF398 (+0xE8
  texture prefix).
- Naked transcriptions where the original keeps unrounded values on the FPU stack: `EmitSection`, the ribbon's first cross
  section, `AddOffset` of the mesh draws, the light / sun transforms and crown blend of `DrawFigurePart`; `DrawSprites`
  copies the view matrix's upper triangle through FLD / FSTP (`ThroughFpu`, an sNaN would come out quiet) and loads the clip
  limits as zero-extended qwords.

Figure models, factory, loading (O): 005AA5F0 `FindFigureModel` (the manager's hash_map<string, model*> +0xBC; opens
`<name>.mod / .bon / .anm` through slot 19, nothing without the .mod), 005A9C90 / 005A9A50 `DestroyFigureModel` /
`ConstructFigureModel` (map ctor inlined as STLport's: buckets the first listed prime not below 100; three 0x6C-byte parts via
006721D0), 005AA8E0 `CreateFigureObject` (class ids 0x41-0x44, 0x300B-0x300D; +0x10 gets the id), 005AC880
`LoadSkeletonLinks` (`<name>.lnk`: (child, parent) pairs to 0050CC60, then 0050D2E0), 005ACB80 `LoadFigureBones`
(`<name>.bon`: 0x48 bytes to 0050BC00, other sizes to 0050B8C0; data not released), 005AC010 `LoadFigureBounds` (`<name>.fig`:
"FIG" headers to 005AAE60, others to 005AB9A0), 005ACCF0 `SetFigureAnimation`, 005A9EF0 `FindModelAnimation`, 005AA1C0
`DestroyFigureManager`.

Kept: `CLinkFigureLoader::AddArchive` leaks the `CMMIOResource` when opening fails and passes the `stlp::string` by value to
`"%s"`. `TweenArray::Load` reads the count into its stream argument's stack slot, `LoadEntries` into an uninitialised local
(ours starts from 0); `SetTarget` appends an uninitialised entry before overwriting all of it; `ShearMatrix` sets m[10] to 0
after zero-filling. `DrawFigurePart`: without a rectangle the device path never sets the drawn flag; a singular (or NaN)
model matrix leaves the original's inverse in uninitialised stack slots, ours uses the previous call's inverse. `DrawSprites`:
without a primary surface the clip path uses two uninitialised stack slots for the screen size (ours keeps the last values);
a failed draw returns 0 without restoring the blend states; the 16-byte array at 007AEFA0 is sized to 0x200 every call and
never used; the sort writes a dropped sprite's key into the next free record. `DrawRibbon`: the index array is sized for the
ribbon and freed at once before the indices are added.

Skipped: 004F0B90 (TL library loading: C++ throws, CPUID, once per run), 004F1330 (Rebirth-patched), the `CDib` class, the
prints.db / acks.db loaders themselves (Rebirth-patched), STLport instantiations.

## Terrain sectors, lighting, prints (006BE3A0-006CFEA0)

Code: `terrain_sector.h/.cpp` (`terrain::SectorData` / `TerrainFields` are views of terrain.h's `TerrainSector` / `Terrain`;
the sector file format is in the code), `terrain.cpp` (PlaneHeight, VertexAt, HeightAt), `terrain_draw.cpp`
(`DrawSectorPage`). Reached by every scenario (zone1 loads several sectors, water sectors only where the map has water; the
terrain pass draws every visible page each frame). Sound parts of the range are in `sound.md`.

Map (R = Direct3D / renderer calls, left original): 006BF5F0 / 006BF8A0 `SectorData` ctor / dtor (0x394 bytes); 006BFC80
`Project`; 006C13A0 `Load` (`<zone>XXXYYY.sec`, magic CF4BF774); 006C1C00 (+ chunk 006C29BD) `Build`; 006C30E0
`ApplyWaterLevels`; 006C3DD0 `Ensure` (build 2 / light 4 on demand); 006C53C0 `TileUv`; vertex lighting 006C31C0 / 006C36B0
/ 006C3A00 (3A00 switches the FPU to chop rounding with `_controlfp(0x300, 0x300)`; 36B0 tests shadows through 006C92C0);
landscape object (vtable 0073F6A8): per-frame Update 006CB1E0 (light countdown 006C8ED0, animated tiles, ambience 006CB390,
frame counter +0x218); draw passes 006C6230 / 006C6330 / 006C6460 / 006C66E0 (from 00569E40, Rebirth-patched) (R) ->
`DrawSectorPage` 006BFF40; 006C68B0 map loader (Rebirth-patched); 006C8140 `Terrain::SectorAt` sector cache (grid +0x1F8, LRU
list +0x200 / +0x204, 240-frame age limit); 006C85A0 / 006C8680 `VertexAt` / `HeightAt`; 006C8930 `GroundAndWaterDepth`;
006C8D40 `SurfaceClass` (texture class under a point: footprints, blood, effects); 006CA5D0-006CAC10 point lights register
with the vertices they reach (pooled 16-byte LightRef nodes, blocks of 1000 at 007C8840); 006CB020 point test (8 callers);
006CF370-006CFEA0 foot / blood / fire prints.

| Address | Name | V |
|---|---|---|
| 006C1C00 | SectorData::Build (random generator and the owner's water row restored) | S |
| 006C13A0 | SectorData::Load (original on a scratch copy with emptied arrays) | S |
| 006C30E0 | SectorData::ApplyWaterLevels | S |
| 006C3DD0 | SectorData::Ensure (calls Build through its original address) | O |
| 006C53C0, 006C8D40, 006C8930 | TileUv, SurfaceClass, GroundAndWaterDepth | P / S |
| 006BF5F0 | SectorData::Construct (sizes the shared projection buffers 007C88F8 / 007C88E8 / 007C88D0 to 0x330) | S |
| 006BFC80 | SectorData::Project (RET 4: the pipeline; the pass 006C6460): per quarter not culled, positions at the LOD step through 004F29F0 against the quarter box (a ClipBounds prefix); records +0x00 projected, +0x10 outcode | S |
| 006BFF40 | DrawSectorPage (`__fastcall` for the thiscall with four stack arguments: context, unused, texture page, unused) | O |
| 006C3A00 / 006C36B0 / 006C31C0 | SectorData::Light / ShadeSun / LightVertex (terrain_lighting.cpp; chop rounding through the game's `_controlfp` 006EBBB4, `gamecrt::Ftol`; Ensure lights through 006C3A00's address) | S |

Facts: terrain_sector.cpp is compiled with `float_control(precise, on)` (running x / y, heights in flight and bounding box
intermediates are doubles; constants read from the exe; HeightAt taken unrounded through a double-returning forwarder to
006C8680). `DrawSectorPage` returns 1 at once when sector +0x60 is 2; its function-local statics (guard byte 007C88A0:
vertex buffer 007C8890, index CWordArray 007C8878, clipped indices 007C8860, per-vertex CByteArray 007C8848; clipped-vertex
arrays 007C88B8 / 007C88A8, guard 007C88C8; exit handlers 006C1370 / 006C1360 / 006C1350 / 006C1340 / 006C1320 / 006C1300)
are built on first use and the last two arrays do not keep their contents when they grow; level-of-detail tables 0079699C /
007969A8 are read from the exe, an out-of-range level reads past them as the original does; the vertex buffer's element is 32
bytes, so it is four times larger than needed (as in the original); render state 0x1D goes through `*007AF080 + 0x54`.
Kept: Load passes the error message to FatalError as its format string; Build's texture class index is compared against
perPage * pages (not 16) before falling back to 15; Load picks a vertex's water material by lowest priority, Build the first
found (Ghidra's listing of 006C1C00 drops the FNSTSW AX at 006C1F48: rewrite guide rule 1).
Lighting: owner (`TerrainFields`) +0x94 flags (4 edge fade, 0x10 sun shadows), +0x224 ambient, +0x24C..+0x254 the vertex
being lit (read by the forwarded shadow test 006C92C0, which loads the sectors the ray crosses), +0x258 the sun; record +0x11
colour, +0x15 specular, +0x37 lit colour; material +0x14 absorption; 007C8908 the water falloff. Kept: ShadeSun passes an
uninitialised fourth colour component (ours 0, only copied through by 0068D310); channel FISTPs are or-ed whole into the word.

Skipped: the other draw functions (R); 006C68B0 and 006D0630 / 006D7620 (Rebirth-patched); point light registration 006CAC10 (global
node pool), 006C3E80 animated tiles; dead code 006C8990, 006C9560, 006C5EA0, 006C5F50, 006C84E0, 006C4790 / 006C4A40 /
006C4C90; STLport / MFC instantiations (006B01F0-006B4230, 006BDDD0-006BE300, 006CD540-006CEA10). Risk: the animated-tile path
draws from the game's random generator (a mistake would shift later draws).

## Game code left in 0x006C0000-0x00740000

All of it lies in 006C1390-006E5E30 (beyond is the CRT / MFC). Groups (R = Rebirth-patched, forwarded):
- **Landscape / sectors** 006C1390-006CCF20: shadow test 006C92C0; animated tiles 006C3E80; light countdown 006C8ED0;
  ambience 006CB390 / 006CBC00 / 006CBCD0; map loading 006C68B0 (R; vanilla body ported, A-H26), 006C5AA0, 006C7940 / 006C7D70 / 006C7EB0, 006C5430-006C57E0 (client setup from 00561E50 / 00562B80); draw passes 006C6230 / 006C6330 / 006C6460 /
  006C66E0 / 006C67E0 / 006C42A0 / 006C4310; point lights 006CA5D0-006CAC10, 006CB020; water level 006CC7F0; decal
  triangles 006CCF20; sector parts (Clear 006C3C80, CArray element pairs 006C4EE0-006C5230).
- Ground prints 006CECD0-006CFEA0: ported (A-X5, below).
- **prints.db / acks.db** 006D00E0-006E5140: database ctors / dtors, the records' compiler-generated members, acks record
  serialisers 006E3780 / 006E3DC0 / 006E3FD0 / 006E42F0 / 006E4410 / 006E46C0 / 006E4F40; loaders 006D0630 / 006D7620 (R).
  98 STLport / MFC instantiations are in config/library.csv ("A-X1 review").
- **CDib** 006D49E0-006D6FC0 (BMP overrides and movie screenshots).
- Dead (config/dead_code.csv): 006C98F0 (reached only from the unreferenced chunk 006CA170), 006D5480, 006D6800.

Ported (V as above; terrain lighting is in the terrain table):

| Address | Name | V |
|---|---|---|
| 006CA260 | `Terrain::SetVisibleSectors` (terrain_landscape.cpp; the original on a raw copy with its own array blocks) | S |
| 006C8140 | `Terrain::SectorAt` (LRU cache `mfc::CList<SectorData*>` +0x1FC, grid +0x1F8, frame +0x218; checks: the sector returned and, every 64th call, the cache's links, grid entries and frame order) | O |
| 006CD5C0, 006CE890 / 006CDD50, 006CDE90 | `lib::AlignedArray<uint32>::SetSize` / `SetAtGrow` copies | S |
| 006D9A30 / 006D9CE0 / 006D9F80 | `acks::AckTable<UnitAcks / ModelAcks / ExtraAcks>::Find` (acks_db.cpp; index operator[], adds a null entry for a missing name) | P |
| 006D8780 / 006D88E0 | `acks::PickSpeech` / `PickEnvironmentSpeech` (rand() and ISAAC state restored between the runs and compared) | S |
| 006D8430 / 006D8A40 | `acks::SayAck` / `SayNamedAck` (call the five above through their addresses so their wrappers run) | O |
| 006CB1E0 | `Terrain::RunTick` (slot 6; terrain_animation.cpp) | O |
| 006C8ED0 | `Terrain::SetSun` (ray steps; relight countdowns of the lit cached sectors, sorted through the game's qsort) | S |
| 006C3E80 | `SectorData::Animate` (animated tiles, water waves; ISAAC restored between the runs) | S |
| 006C3C80, 006C3D70, 006C3E30, 006C3E50 | `SectorData::Clear` (O), `ResetFields`, `ScheduleRelight`, `RelightTick` | S |
| 006CB390, 006CBCD0 | `Terrain::PlayAmbience` / `StopAmbience` (terrain_ambience.cpp; sounds, ISAAC, sector loads) | O |
| 006CBC00 | `CWorldClient::AmbienceTimeOfDay` (CString out) | S |
| 006CCF20 | `Terrain::AddDecalTriangles` (terrain_decal.cpp; triangles through the unhooked original 004D8490, X17 / X18; appended data compared) | S |

acks.db (acks_db.h): database 007C8A60 = three tables {vector of records, hash_map<string, record*>}: 0x1C4-byte records by
prototype name (kinds up to 0x29; 0x29 = the environment speeches +0x1AC, 0x2C bytes, flags +0x28 matched against
`CWorldClient::EnvironmentFlags`), 0x5C by model name (0x2E..0x32, fallback through the prototype's model), 0x38 by
prototype name (0x2A..0x2D); each record: name, hash_map<int kind, vector<SpeechRecord>*>. `CUnitController` slots 48-52
were declared for Say (51) / StopSpeech (52). The original picks through a temporary vector of pointers; ours counts and
indexes (same draws, no allocation).

Landscape fields (terrain.h / TerrainFields): +0x94 flags (1 waves, 2 animated tiles, 8 ambient sounds); +0x21C / +0x220 light
interval / countdown; +0x234..+0x248 the sun's ray steps; +0x274..+0x28C waves (direction, height, glow, phase, speed); +0x290
per-vertex wave phases; +0x2A0 a 512-entry sine table. Kept: SetSun's qsort comparator (006C8E90) reads +0xC / +8 of the array
slots, i.e. the pointers three and two slots on (past the end for the last ones), so the relight order follows pointer values.
Animate's sine index uses 256 / pi as a double (the original divides at run time; a .5 edge may rarely differ).

Ambience: sound classes at 007969D0 (16 x 0x18: name, variants, weight, class it counts as, pan limit, chance %; read from
memory, most have variants -1, so the one-shot index is a draw % 0xFFFFFFFF + 1); loop slots +0xAC8 handles / +0xAD4 classes,
+0xAE0 the sound set. The class votes are sorted by the game's qsort (comparator 006CB360 rewritten as `ByWeight`).

A-X4 (terrain_landscape.cpp, terrain_lights.cpp, acks_db.cpp; wrappers in verify_terrain_landscape.cpp,
verify_terrain_lights.cpp, verify_acks_db.cpp):

| Address | Name | V |
|---|---|---|
| 006C5430 / 006C54D0 | `Terrain::Create` / `Destroy` (slots 2 / 3; Reset and the wave phases through their addresses) | O |
| 006C5620 | `Terrain::Reset` (original on a raw copy with empty arrays and texts; all bytes but the two texts) | S |
| 006C57E0 | `Terrain::BuildWavePhases` (rand() state restored between the runs) | S |
| 006C7D70 / 006C7EB0 | `Terrain::SetWaveStrength` / `SetWaves` | S |
| 006C5AA0 | `Terrain::SetupPages` (texture pages from the map loader; `CMaterial`, material.h) | O |
| 006CAA70, 006BF580, 006BF4A0 | `Terrain::LightAreaOf`, `LightAreaFromBounds`, `SetLightArea` (the last two through the first's side run) | S |
| 006CA5D0 / 006CA720 / 006CA830 / 006CA9C0 / 006CAB50 | `Terrain::AddLight` / `RemoveLight` / `MoveLight` / `LightSector` / `RelightArea` (areas checked against the original 006CAA70) | O |
| 006CAC10 | `UpdateVertexLights` (each vertex of the range has the light once or not at all, lists linked) | O |
| 006C5230, 006C5150, 006C4EE0 | `FreeLightRef`, `SectorVertex::ReleaseLights`, `DestroyRecords` (pool: `LightRefBlock`, terrain_sector.h) | O |
| 006E3780 / 006E3FD0 / 006E3DC0 | `UnitAcks` / `ModelAcks` / `ExtraAcks::Serialize` (loading: the original into a scratch element made by the original resize; failed-search chunks popped) | S |
| 006E42F0 / 006E4410 / 006E4F40 | `SpeechListField` / `EnvironmentSpeechListField` / `SpeechField` (archive helpers; reached from the originals' side runs) | S |

Forwarders: 006C5620 shared as
`LandscapeResetOriginal` (originals_world.h), 0068E970 as `MaterialNewOriginal` (originals_ui.h); 006E4560 / 006E47F0
(speech list resize). Kept: RemoveLight of an unregistered light loops over an uninitialised area in the original (ours
over none); MoveLight of an unregistered light writes its area to address 4 (ours to null: both crash); RemoveLight /
MoveLight / AddLight do nothing (no Release) before a map is loaded. 006D5BF0 (CDib .mmp reader) is dead: only called from
dead 006D6800 (config/dead_code.csv).

Ground prints (A-X5; ground_print.h / .cpp, wrappers in verify_ground_print.cpp): `GroundPrint` (scorch marks, 0x8C,
vtable 0073F76C; list copies get 0073F778), `Footprint` (0x94, 0073F760), `BloodPrint` (0x9C, 0073F754) in the client lists
+0x1B8 blood, +0x1BC footprints, +0x1C0 scorch (world.h `bloodPrints_` / `footprints_` / `scorchMarks_`). prints.db
(`PrintsDatabase`, model.h, 007C89C8): chunk 1 blood and bloody footprints, 2 footprints, 3 scorch; chunks 2 / 3 of a record
are {alpha, life, lifetime} (`PrintLook`), the client's +0x7A8 picks the second.

| Address | Name | V |
|---|---|---|
| 006CECD0 / 006CFA20 | `GroundPrint::Tick` / `BloodPrint::Tick` (raw copies) | S |
| 006CF370 / 006CFE10 / 006CFAC0 | `Save` of the three (capture streams) | S |
| 006CF4F0 / 006CFE60 / 006CFB40 | `Load` (ours from the stream, recorded; the original on a copy from the recording) | S |
| 006CFEA0 / 006CFBA0 / 006CF660 | `ConstructScorch`, `Footprint::Construct`, `BloodPrint::Construct` (random state restored) | S |
| 006CEDE0 | `GroundPrint::Draw` (alpha byte and geometry consistency only; the original would draw twice) | O |
| 006CF960 | `GroundPrint::Destruct` (only from the constructors' unwinding) | O |
| 0056CE90 / 0056D150 / 0056D4C0 | `CWorldClient::AddBloodPrint` / `AddFootprint` / `AddScorchMark` (the original's node taken out again; records compared) | S |

Facts: Draw's uv loop is unrolled four times in the original: the fourth vertex rounds its u part to a float, the others
their v part (kept). Blood: spin = 30 / life degrees per tick, spreads min(life / 2, 45) ticks (rebuilt each tick), angle a
random multiple of 0.001 below 2 pi; footprints take the kind of a death's blood print they step into (fresh hits' are
skipped) and return it. Kept: Load does not restore alpha_ (the next tick does). Risks: Draw's corners use FCOS / FSIN through
x87::fcos / fsin and plain double maths (CheckNear-level differences possible); the footprint's blood test compares
`limit >= d2` (NaN positions would differ).

Also (terrain_landscape.cpp): 006C7A80 `Terrain::BuildVertexTiles` (S: the original on a raw copy; each entry's count and
first `count` tiles: the original leaves the rest as its reused stack record held) and 006C7750 `Terrain::ClearMap` (O);
Create / Destroy call them through their addresses. terrain.h now declares `archive` (+0x15C, CMMIOResource) and
`vertexTiles` (+0x1E8; `terrain::VertexTiles` moved there from terrain_sector.h).

The decal projector 0056C750 is `CWorldClient::ProjectDecal` (world_client_decals.cpp; S: the original into private copies of
the three arrays, ours into the caller's, sizes / capacities / used elements compared; callers reach it through
`ClientProjectDecalOriginal`, originals_world.h). Statics: the builder 007AF488 and the part points (Vector4 array 007AF478),
guard 007AF504, exit handlers 0056CE30 / 0056CE10. The platform pass walks the platform faces (an object with several faces
adds its parts once per face, kept), pushes the object's and each part's matrix on the scene pipeline's stack 14
(`kDecalStack`), transforms the part's packets with the transposed top through the transform object's slot 26
(`TransformPoints`) and adds each triangle (AddTriangle through its unhooked original). GetPartData's result is not checked
(kept; ours starts from no part data). The parts are used as CObject3D-like scene nodes (+0x34 orientation, +0x54 matrix),
which FigurePart's declaration contradicts (C9).

X29 (2026-10-10): ProjectDecal's pushes on stack 14 are inlined in the original and multiply through 004D9880
(`MatrixMultiply`), not PushMatrix 004D9B80 whose terms sum in another order; ours called PushMatrix (last-bit differences
in the part points). Now `PushDecalMatrix` in world_client_decals.cpp. Both checks were also wrong about the texture
coordinate array: the builder leaves it at its reserved size (Reserve: triangles * 3 + vertices; Finish never trims it), so
its size is not one per vertex, and the elements past the last written vertex (and the last packet's padding vertices)
keep whatever the buffer held (heap leftovers in the wrapper's copies). ProjectDecal's wrapper now compares only the
vertices the indices use (and prints both uv values); Draw's check wants whole triangles, indices below the vertex and
coordinate counts, and at least vertices - 3 coordinates (it prints which part failed). Both re-enabled.

### Landscape passes, light tests, CDib (A-X6)

Code: `terrain_passes.cpp`, `terrain_shadow.cpp`, `terrain_landscape.cpp` (tail), `dib.h/.cpp`, `surface_capture.cpp`,
`acks_db.cpp`. Every scenario
that shows the world reaches the passes and light tests each frame; `terrain_water_load` (new: a save made while a script
water change runs, loaded back) reaches SetWaterLevel; `camera` (Scroll Lock screenshot) reaches the CDib part marked *.

| Address | Name | V |
|---|---|---|
| 006C6230 / 006C6330 | `Terrain::DrawGround` / `DrawWater` (the landscape's matrix on pipeline stacks 2 and 1 around the pages; checks: stack depths, the matrix's last column and row) | O |
| 006C6460 | `Terrain::DrawPages` (per visible sector: detail, clip test, seams, projection; per page: texture, DrawSectorPage; callees through their addresses) | O |
| 006C66E0 / 006C1390 | `Terrain::ChooseDetail` / `SectorData::SetDetail` | S |
| 006C92C0 | `Terrain::SunReaches` (the sun shadow ray over the vertex grid, from ShadeSun) | S |
| 006CB020 | `Terrain::EdgeFade` (the map edge's fade byte) | P |
| 006C6070 | `Terrain::CombineRotation` (slot 14: the base's, then the world matrix +0x98) | S |
| 006C9290 / 006CC7F0 / 006C7940 | `Terrain::SetLightInterval` / `SetWaterLevel` / `BuildTileUv` | S |
| 006D4BA0*, 006D4B30*, 006D4AA0, 006D49E0 | `CDib::Create`, `ConstructSized`, `Construct`, CreateObject (scratch objects compared by content) | S |
| 006D6AB0*, 006D54D0 | `CDib::AllocateBits`, `MakePalette` (the original's block / palette freed between the runs) | S |
| 006D69B0*, 006D4CD0*, 006D4B10*, 006D5BB0 | `CDib::Empty`, `Destruct`, `Delete` (slot 1), `Serialize` (slot 2) | O |
| 006D4D10, 006D4A60 | `CDib::Size`, `GetRuntimeClass` (slot 0) | P |
| 006D6D40* | `CaptureSurfaceRect` (surface_capture.cpp: the surface read twice, the bitmaps' headers and pixels compared) | S |
| 006D6FC0* | `CaptureSurface` (the whole surface, through 006D6D40) | O |
| 006D8E30 | `acks::AckTable<ExtraAcks>::RecordAt` (from the loader at startup) | P |

Facts. The two passes differ only in the page range: ground 0..pages+1, water pages+2..pages*2+3; a page's texture is
`pageTextures[page % (pages + 2)]`, bound at the first sector that has tiles on it. They do not touch the device (matrix
pushes, the renderer's SetTexture, DrawSectorPage), so they are ported; the sector's clip test 006BFB20 and seams 006BFB80
(below 006C0000) are forwarders in terrain_passes.cpp. Level of detail: 0 / 1 below the squared distances 0079BD2C /
0079BD30 from the camera to the sector's centre unless `minDetail` (+0xAA0) forbids it, else 2. The shadow ray: a
crossing into the next sector in x skips that iteration's y step; a negative slope gives the sign of the sun's z.
Terrain members named: `drawMatrix` +0x54, `worldMatrix` +0x98, `materials` +0x12C, `lightingSector / X / Y` +0x24C,
`minDetail` +0xAA0, `waterLevels` +0xAE8 (per sector, per material). `CDib` (0x3C bytes, dib.h): colour table, bits,
header, who owns them (1 new, 2 global memory), image size, mapped file handles, palette. Kept: AllocateBits sizes the
block without row padding while Write writes `imageSize` (rows padded to four bytes): a screenshot whose row is not a
multiple of four bytes is written from past the block's end. The captures (`CDXSurface` holder in ECX, the rectangle in
EDX for 006D6D40): GetPixelFormat, a read Lock under the device lock 007AEE78, a new CDib in the surface's depth with its
channel masks, the rows copied from the last to the first (16, 24 and 32 bits only), Unlock; a failed DirectDraw call is
reported and the capture goes on. They read the DirectDraw surface as before: a holder without one (the renderer's
switch-over) is not handled, as in the original. texture_bmp.cpp, app_frame.cpp and ui_interface_keys.cpp now use `CDib`
and the shared capture forwarders instead of their own.

A-H26: 006D0630 `PrintsDatabase::Load` and 006D7620 `acks::AckDatabase::Load` are ported as their vanilla bodies
(model_db_open.cpp; hooked in the vanilla launch only, rule 9; wrappers ours only in verify_database_open.cpp, reached at
every start): `folder` + "database.res" opened through 00671730 (a failure is reported as "databases.res"), the file copied
into a memory stream, each table read through a ChunkArchive of its own (prints chunks 1 blood, 2 footprints, 3 scorch;
acks chunks 1 units, 2 models, 3 extras) and filed under its records' names (`PrintsDatabase` gained `scorchByName` at
+0x4C; 0x60 bytes). The ack load then fills every record's kind map: units kinds 0..0x15 to lists 0..21, 0x16..0x1B entered
as null, 0x1C..0x1F to 22..25, 0x20..0x22 to 28..30, 0x23 / 0x24 to 26 / 27, 0x27 / 0x28 to 31 / 32, 0x2A the named
speeches; models 0x2E..0x32; extras 0x2C / 0x2D.

A-H26: 006C68B0 `Terrain::LoadMap(path, zone, dungeon)` (RET 0xC; terrain_landscape.cpp) is ported as its vanilla body
(Rebirth redirects its ListFiles call at 006C6921; hooked in the vanilla launch only). Its only caller is the zone load
00567AB0 (original, Rebirth-patched), with the zone's lower-cased name and "the zone is a dungeon" as a byte. ClearMap;
the archive `path` opened (0 when not); +0xFC `textFC` the path without its extension; +0xF8 `textF8` the name of the
archive's first "*.mp" file up to its dot (an archive without one is not tested for). The map file `<map>.MP` (the
extension through the pointer at 007441D4), 0x26 bytes of header: magic CE4AF672, height range, sectors x / y, pages, page
size, texture count, tile size, material count (16 bits), animated texture count; then the materials (0x2C bytes each), a
class per texture, and (texture, frames) word pairs; the file's size must be exactly that. A missing file is FatalError
"Unable to open file \"path\"", a short or mismatched one "Wrong file format: path" (through 00480080's address; its
result is returned, after ClearMap). Then: the sector grid (+0x1F8, rows of null nodes); width / height = sectors x 32;
+0x118 `pageSize`, +0x120 `tileSize`, +0x124 `tilesPerRow` = page / tile, perPage its square; the materials (kind and
colour copied, +0x10 `opacity`, absorption 1 / (1 - opacity), +0x18 `opacityByte` = _ftol(opacity x 255), colour = the
file's light factor x colour capped at 1, priority, +0x30 from the record's +0x1C, level 0; +0x28 is left as it was);
+0x13C `textureClass` (the map's classes, then sixteen of class 15 for the extra pages' textures) and +0x14C
`textureFrames` (0, or the frame count of an animated texture), both aligned int arrays; SetupPages (0 and ClearMap when a
texture does not load); the uv tables (tile side 1 / tilesPerRow, margin side / tileSize x 8: BuildTileUv per rotation for
every tile, and with "whole page" for one row of the last pages' tables); +0x100 `mapLoaded` = 1; the ambient sound set
(+0xAE0: "Dungeon", or "Ingos" / "Suslanger" by `zone`, else "Gipat"); for each of the sixteen sound classes with its own
vote (table 007969D0, +0x0C negative) the one-shot count +0x04 = how many of `ambient\<set>\<class>\day\<n>.wav`
(n from 1) the wave sources have (`terrain::CountAmbientVariants`, terrain_ambience.cpp; each probe takes a reference on
the wave file that is never given back); flags |= 0xFFFF; a water level per sector and material, zeroed (the outer
CArray's elements are CArray<float>, vtable 0073D3E8: `terrain::LevelArrayElements` for 00575690 / 005755A0).
Float note (rule 7): the quotients and products are rounded to float where the original's FPU does at 24-bit precision
(RoundToFloat); the cap keeps 1 for a NaN, as the original's FCOMP does.
V: side by side (verify_terrain_map_load.cpp). The loader frees the map itself, so the original runs first and ours
replaces its result; texts, sizes, flags, materials' written fields, classes, frame counts, uv tables (by bits), page
counts, one-shot counts compared, the grid and the levels checked empty; `extraPages` (only Reset clears it, SetupPages adds
to it) and the flags are put back between the runs; a map the original does not load is not loaded again. Cost: the page
textures load twice per zone in the verify build. Scenario: every one that enters a zone.

Stay forwarded: `CDib::Read` 006D5910 / `Write` 006D5AC0 (MFC TRY / CATCH
frames around the file calls, catching the game's MFC exceptions: as 004F0B90). Dead (config/dead_code.csv): 006C67E0,
006C42A0, 006C4310 (a third draw pass reached only from the dead 006CC8D0). 50 rows in config/library.csv ("A-X6 review"): the landscape's CArray /
aligned array element helpers and the prints / acks records' and databases' compiler-generated members. With these the
range has no other unported game code.

Next: `CDib::Read` / `Write` once the file layer is ours (no MFC exceptions to catch); the captures need the new
renderer's frame when the switch-over draws without a DirectDraw surface.

## Resources and the sound file reader (00671280-00675B40)

Code: `resource_stream.h/.cpp`; earlier resource work (CMappedResource, CMMIOResource methods, CResourceManager slots 16-22,
CMappedFile) is in `resource.cpp` / `resource_manager.cpp`. Every scenario reaches it (archives open at startup, the sound
file reader at every sound).

CMappedResource (vtable 007432E0), CMMIOResource (00743300), CResourceBase (00743324), CResourceManager (00743338, ctor
00674D90, dtor 00674DF0 / 00674DD0, slot 3 00674F40); FindEntry 00674850, accelerator 006748F0 / 00674910, ListFiles 00674AC0,
WildcardMatch 00674CE0. The sound system's file reader (vtable 00743320, 0x34 bytes, created by 006B6040): CResourceBase
fields plus +0x24 archive HMMIO, +0x28 entry offset (1 loose file, 0 nothing open), +0x2C entry size, +0x30 loose file HMMIO;
OpenFile tries a readable loose file first, then the archive's entry; the archive part is dead code (Open 00674310 and
006745C0 have no callers, so +0x14 / +0x18 stay null).

| Address | Name | V |
|---|---|---|
| 00674790, 00674810, 006747C0, 00674A90 | CResourceBase Construct (S), Destruct, deleting, FreeAccelTable | O |
| 00674280, 006742C0, 006742A0, 00674430, 006744C0, 006746B0, 006746F0 | CResourceStream ctor / dtor / deleting, Close, OpenFile, Read, Seek | O |
| 00674CE0, 00674AC0 | WildcardMatch, CMappedResource::ListFiles | P / S |
| 006721D0, 006722F0, 006722D0, 00674D90, 00674DF0, 00674DD0, 00674F40 | CMMIOResource ctor / dtor / deleting; CResourceManager ctor / dtor / deleting / slot 3 | O |

`CMMIOResource::Destroy` calls our `CMappedResource::Destroy` directly (the original has an inline copy of that destructor, not
a call to the Rebirth-patched 006714A0). Kept: `CResourceStream::Seek` treats "from the current position" of a loose file as
"from the start", "from the end" of an archive entry uses +0x2C, which OpenFile never sets; OpenFile leaks the `_findfirst`
handle; `ListFiles` copies a name into a 1 KB buffer without a length check. Dead code: 00674310, 00674910, 006748F0,
006745C0, 00674670. Skipped: STLport instantiations (006713C0-00674070, 00675560-00675690).
