# Models, resource loading and the 3D device layer

Code: `src/game/model.h/.cpp` (3D library side of figures), `model_db.cpp` (databases, water level
changes), `renderer_init.cpp`, `renderer_strip.cpp`, `renderer_sprites.cpp`, `device_mesh.cpp`,
`device_draw.cpp`, `device_shadow.cpp`, `figure_*.cpp`, `unit_skins.cpp`. Algorithms are described in
each file's header comment. Verify: `tests/verify/verify_models.cpp` and one `verify_<file>.cpp` per
file. Scenario: `tests/scenarios/models.txt` (the draw functions run in every scenario; ribbons and
sprites in `spells` / `spells_cast`). Names are provisional.

## Map

### 004F0000-00500000: the 3D transform library

- 004F0B90 loads `3DfpFPU.dll` and its `GetTLPipeline` (throws on failure; CPUID), once from 00500540.
- 004F68C0 (Rebirth-patched): `CLinkFigureLoader` constructor (vtable 0073D808, from 00563710 /
  00456440); opens `figures.res` and `menus.res` through 004F6D30.
- 004F77C0 loads a `.lnk` file and each part's figure: 004F7F00 for "FIG8" (8 morph components) and
  other "FIG" versions, 004F87D0 for the rest, building the mesh through 004F4B90 / 004F30F0. Reached
  only through the loader's vtable (slots 4, 5, 7: 004F7150, 004F7080, 004F75E0); which action uses it
  is not known.
- 004F16A0: frustum classification of a part's bounds (callers all over the renderer).
- 004F1330 (Rebirth-patched), 004F1370, 004F13F0, 004F0E80, 004F1440 / 004F15C0: matrix builders.
- 004F57B0 triangle index buffer resize (3 x uint16, 32-byte aligned); 004F93A0 `ClipBounds::SetBox`
  (called by the figure manager for every part it loads).
- 004FA000-00500000: STLport instantiations for the loader, the D3D state-block manager (004FCA60,
  004FD2A0, 004FD9D0) and the effect manager.

### 006D0000-006E0000: databases and bitmaps

- 006D0630 (Rebirth-patched) loads `prints.db` from `database.res` (records 0x28, 0x34, 0x28 bytes);
  006D7620 (Rebirth-patched) loads `acks.db` (0x1C4, 0x5C, 0x38). Both run once at startup (00456440)
  through a `ChunkArchive` over a memory stream.
- `ChunkArchive` (ctor 0053A7B0, open 00409AE0, close 0040A000): two `CMemoryStream`s (index and
  data, data buffer +0x40), open chunk list +0x50 (last node = current chunk; its +0x14 picks the
  occurrence the next EnterChunk looks for), loading flag +0x58. Serialize functions read and write
  through the same code. A failed EnterChunk when loading still pushes a chunk (callers never pop it).
- 006D4400-006D49C0: the world's water level changes (`TweenArray`, `CArray` of 16-byte entries at
  007AFF60 + 0x70): SetTarget (script `SetWaterLevel` via 00593FA0), Tick (every world update from
  0058DCD0), Save, Load (applies each level through 006CC7F0), RemoveAll, SetSize.
- 006D4A60-006D6AB0: `CDib` (vtable 00744288, 0x3C bytes): BMP / `.mmp` readers (DXT1-5, 4444 to
  1555), palette, GDI drawing, writing. Used only for loose `textures\*.bmp` overrides (0068FBF0), movie
  capture screenshots (00458010) and unreferenced code; not rewritten (never runs in a normal game).

## 3D library and databases: rewritten

| Address | Name | Verify |
|---|---|---|
| 004F16A0 | `CTransformPipeline::ClipTest` | pure, side by side |
| 004F93A0 | `ClipBounds::SetBox` (returns the flags) | original on a scratch copy of the 0x2C bytes |
| 004F1370, 004F13F0, 004F0E80 | `ViewportMatrix`, `ShearMatrix`, `ProjectionMatrix` (`__stdcall`, return out) | ours into a scratch matrix, 64 bytes compared |
| 004F57B0 | `TriangleIndexArray::Resize` | original on a copy with its own buffer; sizes and kept triangles |
| 004F6D30 | `CLinkFigureLoader::AddArchive` | ours only: the list must (not) hold the archive |
| 006D23A0 / 006D25F0 | `ChunkArchive::SerializePrints` / `2` | ours only: loaded count = record chunks counted beforehand |
| 006DB430 / 006DB680 / 006DB8D0 | `ChunkArchive::SerializeAcks` / `2` / `3` | same |
| 006D41D0 / 006D4270 | `PrintRecord::Serialize` / `PrintRecord2::Serialize` | loading: original into a scratch record, fields and name compared; saving: ours only |
| 006D43E0, 006D4840, 006D4410, 006D44E0 | `TweenArray::RemoveAll`, `SetSize`, `SetTarget`, `Tick` (bool in AL) | original on a copy (own buffer), result too |
| 006D4570 / 006D47E0 | `TweenArray::Save` (identical copies) | both into capture streams |
| 006D45D0 | `TweenArray::Load` | original for real (applies water levels), recording the stream; ours reads the bytes into a copy |
| 006D4660 | `TweenArray::LoadEntries` | original on a copy from the real stream, ours from the recorded bytes |

Floating point: `ClipTest` and `ProjectionMatrix` are compiled with `float_control(precise)`
(/fp:fast regrouped the corner sums); `ClipTest` compare bits are `!(a >= b)`. `TweenArray::Tick`'s
removal test is "value == 0 or unordered", written `!(v < 0) && !(v > 0)`.

Forwarders (none Rebirth-patched): ChunkArchive 004098D0, 00409980, 004092B0, 00409470, 00409A60;
record destructors, vector resizes, acks record Serialize 006E3780 / 006E3FD0 / 006E3DC0; water level
apply 006CC7F0; the link loader's list insert 004FB2E0 / erase 004FAE00; CMMIOResource ctor 006721D0.

## Renderer (00500B70-005053FF)

The renderer is the global at 007AF060 (vtable 0073D93C). +8 the transform pipeline (matrix stacks as
aligned arrays of 64-byte matrices: data/count at +0x14, +0x1C, +0x24, +0x3C, +0x44, +0x4C, +0x54; +0xC
the transform object, whose slots project and clip vertices), +0x20 the counted device holder
(004E5E50: +8 last HRESULT, +0xC DirectDraw, +0x14 primary surface, +0x18 render target (+0x14 width,
+0x18 height), +0x54 IDirect3DDevice7, +0xAC the pipeline again (its transform object's slot 35 sees
every vertex batch when +0x158 is set), +0x150 override colour of the mesh draws, +0xE0 vertices sent
to vertex buffers, +0x16C / +0x170 / +0x174 vertex, triangle and call counters), +0x24 the counted
state-block helper (004FCA60). Device draws hold the lock 007AEE78 and store the HRESULT at +8.

Shared buffers of the mesh / flat draws: 007AEDE0 clip output, 007AEE00 TL vertices, 007AEE10
projected vertices (16 bytes each), 007AEE20 corner outcodes, 007AEE30 vertex outcode bytes (all
..007AEE3C); the vertex buffer path 004E7820 is dead (always returns null). Ribbon / sprite buffers are
function-local statics (007AF000..007AF05C, 007AEF60..007AEFF8).

### Rewritten

| Address | Name | Verify |
|---|---|---|
| 00500B70 | `RendererCreate` (slot 2, RET 8) | ours only: holder, helper, projection stack entry, flag 2 of +4 |
| 00501D90 | `DrawRibbon` (slot 16, RET 0x18) | ours only: result 0 / 1, whole quads in the index list |
| 00502A20, 00503680 | `DrawTlTriangles`, `DrawIndexed` (holder thiscall; RET 0x1C) | ours only: call counter + 1 |
| 00502AC0 | `EmitSection` (naked transcription) | original into scratch vertices, 64 bytes compared |
| 00502B90 | `StripDirection` | pure, both compared |
| 00502CA0 | `DrawSprites` (slot 17, RET 0xC) | ours only: result 0 / 1, last batch's corners back to front |
| 00503710 / 005037D0 | `SpriteRadixPass` / `SpriteMergePass` | original into a scratch list, lists compared |
| 004E9010 / 004E9A50 | `DeviceDrawMesh` (device thiscall, RET 0x48) / `DeviceDrawMesh2` (two textures, RET 0x50) | ours only: drawn vertices are the corners, or clipped: outcodes copied |
| 004EA4A0 | `DeviceDrawFlat` (RET 0x24) | ours only: result 0 / 2; with one vertex per position every vertex carries the colour pair |
| 004EB550 / 004EB370 | `DeviceDrawIndexed` (RET 0x1C) / `DeviceDrawTwoTextures` (RET 0x24) | ours only: result 1; counters grow once per pass / two calls, second surface stays on stage 0 |
| 005A7A70 | `DrawFigurePart` (node thiscall, RET 0x1C) | ours only, after the (pure) clip test: result 1; hidden or outside: not marked drawn; else lit unless flat |
| 005A7640 | plain `DrawFigurePart` (RET 0x18) | ours only: flag consistency |
| 005A7290 | clip-box lines (finishing call of both draws) | differential through a stand-in renderer recording the line list |
| 005A0E80 | `RasteriseFigureMesh` | differential through a stand-in rasteriser |
| 00568800 | `BuildUnitSkins` (zone client thiscall, RET 8) | ours only (texture cache and figure maker side effects): the two record vectors unchanged |

Facts that are not in the code comments:
- **Renderer creation**: the helper's flag argument is pushed as EDX with only DL set; the upper bytes
  are what the game's operator new left in EDX, reproduced by capturing EDX after the same
  allocation. The depth range call takes two arguments (settings +0x264, +0x1E4).
- **Mesh draws**: callers are figures (005A7A70, 005A7640) and the 3D library's meshes (004F3C60 ...);
  flag 4 overwrites the mesh's own colour and specular arrays with the device's +0x150 colour / alpha
  << 24; `DeviceDrawFlat` is used by shadows (0051CE60), the renderer (00503A40) and 006CEDE0.
- **Figure parts**: mesh +8 flags 0x40 drawn (cleared first), 0x80 hidden (nothing more, not even the
  finishing call), 0x10 lit, 0x100 flat colours; +0x98 owner (CObject3D, its figure at +0xF0 draws the
  mesh when present), +0x15C mesh type (13..16 animated), +0x188 clip bounds. With 0079BD28 set the
  mesh named "crown" has every colour pulled halfway to the average. The plain variant (005A7640)
  clears the drawn flag only after the hidden test, has its own moved-positions static (007C2E60,
  guard 007C2E70, freed by 005A7A50) and sets the drawn flag whenever the draw did not return 2.
- **Material draw**: the device's current material (+0x14C): +8 blend flag, +0xC / +0x10 source /
  destination blend, +0x14 the technique (+8 / +0xC state block handles per pass, +0x14 first pass's
  stage count), +0x18 stage records (0x14 bytes: coordinate index, texture name as STLport string,
  +0x10 counted texture looked up through 00693C60 on first use).
- **Unit skins**: called by the zone load 00567AB0 (Rebirth-patched for its map paths; this function
  is not); `this` +0x18C the texture cache, +0x34 the figure maker; prototype table 007AF358 (0x120
  bytes), model table 007AF398 (0x168 bytes, +0xE8 texture prefix); names are built with the original's
  inlined strcpy / strcat into 256-byte buffers. See also world.md ("Unit skins").

### Figure models, factory, loading (all ours only: they read resources, allocate or free)

| Address | Name | Notes |
|---|---|---|
| 005AA5F0 | `FindFigureModel` | the manager's hash_map<string, model*> (+0xBC); opens `<name>.mod / .bon / .anm` through slot 19, nothing without the .mod |
| 005A9C90, 005A9A50 | `DestroyFigureModel`, `ConstructFigureModel` | map ctor inlined as STLport's (buckets: first listed prime not below 100); three 0x6C-byte parts via 006721D0 |
| 005AA8E0 | `CreateFigureObject` | class ids 0x41-0x44, 0x300B-0x300D; +0x10 gets the id |
| 005AC880 | `LoadSkeletonLinks` | `<name>.lnk`: (child, parent) pairs to 0050CC60, then 0050D2E0 |
| 005ACB80 | `LoadFigureBones` | `<name>.bon`: 0x48 bytes to 0050BC00, other sizes to 0050B8C0; data not released |
| 005AC010 | `LoadFigureBounds` | `<name>.fig`: "FIG" headers to 005AAE60, others to 005AB9A0 |
| 005ACCF0, 005A9EF0, 005AA1C0 | `SetFigureAnimation`, `FindModelAnimation`, `DestroyFigureManager` | |

## Original bugs and quirks kept on purpose

- `CLinkFigureLoader::AddArchive` leaks the `CMMIOResource` when opening fails; its log call passes the
  `stlp::string` by value to `"%s"`.
- `TweenArray::Load` reads the count into its stream argument's stack slot, `LoadEntries` into an
  uninitialised local (ours starts from 0); only matters for a stream that writes nothing (the game's
  `CMemoryStream` zero-fills short reads). `SetTarget` appends an uninitialised entry before overwriting
  all of it. `ShearMatrix` sets m[10] to 0 after zero-filling.
- `DrawFigurePart`: without a rectangle the device path never sets the drawn flag; a singular (or NaN)
  model matrix leaves the original's inverse in uninitialised stack slots, ours uses the previous
  call's inverse (what those slots usually hold).
- `DrawSprites` (00502CA0): without a primary surface (holder +0x14 null) the clip path uses two
  uninitialised stack slots for the screen size (ours keeps the last values; not reachable while the
  device draws); a failed draw returns 0 without restoring the blend states; the buffer's resize drops
  contents if it ever has to grow (it never does); the 16-byte array at 007AEFA0 is sized to 0x200 every
  call and never used; the sort writes a dropped sprite's key into the next free record.
- `DrawRibbon`: the index array is sized for the ribbon and freed at once before the indices are added.

## Verify notes and risks

- The vector serialisers check only the record count; record contents are checked by the record
  Serialize wrappers (prints) or not at all (acks records, not rewritten). Running the original record
  Serialize on the side when loading can push failed chunks; `RestoreTop` pops them back.
- Under Rebirth the database loaders (006D0630, 006D7620) and the link loader constructor (004F68C0) are
  patched; whether the patched code still calls the rewritten functions is unknown.
- `AddArchive` runs twice at startup; its check is weak (list contents only).
- The creation / ribbon / sprite draws run once or are state-dependent, so they are checked by
  invariants; the radix and merge passes compare only for batches over 128 (radix) / 2..128 (merge)
  visible particles (a big fire or long spell gives both).
- Floating point of the draws: naked transcriptions where the original keeps unrounded values on the FPU
  stack (`EmitSection`, the ribbon's first cross section, `AddOffset` of the mesh draws, the light /
  sun transforms and crown blend of `DrawFigurePart`); `DrawSprites` copies the view matrix's upper
  triangle through FLD / FSTP (`ThroughFpu`, an sNaN would come out quiet) and loads the clip limits as
  zero-extended qwords; the ribbon's u step is a double division rounded once.

## Skipped

- FIG / link file loading (004F77C0, 004F7F00, 004F87D0, 004F7B10, 004F7D20 ...): build meshes through
  not-yet-mapped mesh code and upload to the device; cannot run twice. Do with the mesh class (004F30F0,
  004F4B90).
- 004F0B90 (TL library loading: C++ throws, CPUID, once per run), 004F1440 / 004F15C0 (shadow matrix
  with asin/acos), 004F1330 (Rebirth-patched).
- The `CDib` class (never runs); the prints.db / acks.db loaders themselves (Rebirth-patched) and the
  STLport instantiations.
