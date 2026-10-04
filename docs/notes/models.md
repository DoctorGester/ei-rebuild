# Models and resource loading (004F0000-00500000, 006D0000-006E0000)

Files: `src/game/model.h`, `src/game/model.cpp` (3D library side of figures), `src/game/model_db.cpp`
(databases, water level changes), `tests/verify/verify_models.cpp`, scenario
`tests/scenarios/models.txt`. Shared headers: none changed (`model.h` includes `script.h` for
`MfcArray` and `ScriptStream`).

## Map

### 004F0000-00500000: the 3D transform library

- 004F0B90: constructor of the FPU transform library object: loads `3DfpFPU.dll`, calls its
  `GetTLPipeline` (throws on failure; contains CPUID). Called once from 00500540.
- 004F68C0 (Rebirth-patched): constructor of `CLinkFigureLoader` (vtable 0073D808, created at startup
  by 00563710 from 00456440); opens `figures.res` and `menus.res` through 004F6D30.
- 004F77C0: loads a `.lnk` link file ("Can't find link file for ...") and each part's figure:
  004F7F00 for "FIG8" (8 morph components, trilinear morph of the vertex data) and other "FIG"
  versions, 004F87D0 for the rest; both build the mesh through 004F4B90 / 004F30F0 and upload the
  vertices through the device (slot 7). Reached only through the loader's vtable (slots 4, 5, 7:
  004F7150, 004F7080, 004F75E0); which game action uses it is not known.
- 004F16A0: frustum classification of a figure part's bounds (cube, then box). Callers all over the
  renderer (00467AD0, 004976B0, 004E1EB0, 0051E300, 0056A990, 005A7640, ...).
- 004F1330 (Rebirth-patched), 004F1370, 004F13F0, 004F0E80, 004F1440/004F15C0: matrix builders
  (Rebirth patches the 004F1330 one; probably its aspect handling).
- 004F57B0: resize of a triangle index buffer (3 x uint16 per triangle, 32-byte aligned), used when
  figures load (004F30F0, 005A6C80, 0059F8F0).
- 004F93A0: `ClipBounds::SetBox`, called by the figure manager (005AAE60, 005AB9A0, 005A18E0) for
  every figure part it loads.
- 004FA000-00500000: STLport list/map/string instantiations for the loader, the D3D state-block
  manager (004FCA60, 004FD2A0, 004FD9D0: "Can't register default effect", "Can't delete state
  block") and the effect manager.

### 006D0000-006E0000: databases and bitmaps

- 006D0630 (Rebirth-patched): loads `prints.db` from `database.res` into three vectors (records
  0x28, 0x34, 0x28 bytes) and their name maps. 006D7620 (Rebirth-patched): loads `acks.db` (records
  0x1C4, 0x5C, 0x38 bytes). Both run once at startup (00456440) through a `ChunkArchive` over a
  memory stream.
- `ChunkArchive` (constructor 0053A7B0, open 00409AE0, close 0040A000): two `CMemoryStream`s (chunk
  index and data, data buffer at +0x40), a list of open chunks at +0x50 (the last node is the
  current chunk; its +0x14 picks the occurrence the next EnterChunk looks for), loading flag at
  +0x58. Serialize functions read and write through the same code.
  Quirk: a failed EnterChunk when loading still pushes a chunk (the callers never pop it).
- 006D1000-006D3C00, 006D9000-006E0000: STLport hash_map / vector instantiations for the database
  maps (resize, insert, erase, copy, destructors).
- 006D4400-006D49C0: the world's water level changes (`TweenArray`, a `CArray` of 16-byte entries
  at 007AFF60 + 0x70): SetTarget (script `SetWaterLevel(index, level, time)` through 00593FA0), Tick
  (every world update from 0058DCD0), Save, Load (applies each level through 006CC7F0), RemoveAll,
  SetSize.
- 006D4A60-006D6AB0: a `CDib` class (vtable 00744288; 0x3C bytes): BMP from a mapped file
  (006D4D40, "Empty bitmap file" / "Invalid bitmap file"), from a `CFile` (006D5910), from an `.mmp`
  texture with DXT1-5 decoding and 4444 to 1555 conversion (006D5BF0, "wrong level for mipmap"),
  palette creation, GDI drawing, writing (006D5AC0), creation from a surface (006D6D40, "Driver
  can't do compression" nearby). It is only used for loose `textures\*.bmp` overrides (0068FBF0),
  the movie capture screenshots (00458010) and code with no callers (006D6800 calls the .mmp
  reader). Not rewritten: none of it runs in a normal game, so no verify run would cover it.

## Rewritten

| Address | Name | Verify |
|---|---|---|
| 004F16A0 | `CTransformPipeline::ClipTest` | pure, side by side |
| 004F93A0 | `ClipBounds::SetBox` (returns the flags) | original on a scratch copy of the 0x2C bytes |
| 004F1370 | `ViewportMatrix` (`__stdcall`, returns out) | ours into a scratch matrix, compare 64 bytes |
| 004F13F0 | `ShearMatrix` (`__stdcall`) | same |
| 004F0E80 | `ProjectionMatrix` (`__stdcall`) | same |
| 004F57B0 | `TriangleIndexArray::Resize` | original on a copy with its own buffer; sizes and kept triangles |
| 004F6D30 | `CLinkFigureLoader::AddArchive` | ours only; the list must (not) hold the archive afterwards |
| 006D23A0 / 006D25F0 | `ChunkArchive::SerializePrints` / `SerializePrints2` | ours only; loaded count = record chunks counted beforehand |
| 006DB430 / 006DB680 / 006DB8D0 | `ChunkArchive::SerializeAcks` / `2` / `3` | same |
| 006D41D0 / 006D4270 | `PrintRecord::Serialize` / `PrintRecord2::Serialize` | loading: original into a scratch record, compare fields and name; saving: ours only |
| 006D43E0 | `TweenArray::RemoveAll` | original on a copy |
| 006D4840 | `TweenArray::SetSize` | original on a copy with its own buffer |
| 006D4410 | `TweenArray::SetTarget` | same |
| 006D44E0 | `TweenArray::Tick` (returns bool in AL) | same, plus the result |
| 006D4570 / 006D47E0 | `TweenArray::Save` (identical copies) | both into capture streams |
| 006D45D0 | `TweenArray::Load` | original for real (it applies water levels), recording the stream; ours reads the bytes into a copy |
| 006D4660 | `TweenArray::LoadEntries` | original on a copy from the real stream, ours from the recorded bytes |

All names are provisional (set in the code; not renamed in Ghidra).

### Floating point

- `ClipTest`: the corner coordinates (centre +- radius, centre + box corner) stay on the FPU stack
  and are recomputed in each product; the four transformed values, 2W, X+W and Y+W are stored to
  float (`StoreFloat`). The sums are evaluated ((a + b) + c) + d; /fp:fast regrouped them as
  (a + b) + (c + d), so the function is compiled with `float_control(precise)` (checked in the
  object code). Compare bits are `!(a >= b)` (less or unordered, as FCOMP/C0).
- `ProjectionMatrix`: also `float_control(precise)`. sin and cos go through `x87::fsin/fcos`; the
  sine is kept as a double (the original keeps it on the FPU stack): identical while the FPU runs
  at 24 or 53-bit precision.
- `TweenArray::Tick`'s removal test is "value == 0 or unordered" (FCOMP / TEST AH,0x40), written
  `!(v < 0) && !(v > 0)`.

## Forwarders added

ChunkArchive 004098D0, 00409980, 004092B0, 00409470, 00409A60; record destructors 006D29A0,
006D29E0, 006DC390, 006DC920, 006DCAF0; vector resize 006D26E0, 006D2830, 006DB9C0, 006DBBD0,
006DBE20; acks record Serialize 006E3780, 006E3FD0, 006E3DC0; water level apply 006CC7F0; the link
loader's list insert 004FB2E0 and erase 004FAE00; CMMIOResource constructor 006721D0. prints.db
records are serialised through their vtable. None of these is Rebirth-patched.

## Original bugs kept on purpose

- `CLinkFigureLoader::AddArchive` leaks the `CMMIOResource` when opening fails, and its log call
  passes the `stlp::string` by value to `"%s"` (the three words go on the stack).
- `TweenArray::Load` reads the count into its stream argument's stack slot; `LoadEntries` into an
  uninitialised local (our version starts from 0). Both only matter for a stream that writes
  nothing; the game's `CMemoryStream` zero-fills short reads.
- `ShearMatrix` sets m[10] to 0 after zero-filling.
- `TweenArray::SetTarget` appends an uninitialised entry before overwriting all of its fields.

## Verify notes and risks

- The vector serialisers only check the record count. The record contents are checked by the
  record Serialize wrappers (prints) or not at all (acks records, not rewritten).
- Running the original record Serialize on the side when loading can push failed chunks;
  `RestoreTop` pops the chunk stack back before ours runs (loading LeaveChunk only pops).
- Under Rebirth the database loaders (006D0630, 006D7620) and the link loader constructor (004F68C0)
  are patched; whether the patched code still calls the rewritten functions is unknown.
- `CLinkFigureLoader::AddArchive` is called twice at startup; its verify check is weak (list
  contents only).

## Skipped

- FIG / link file loading (004F77C0, 004F7F00, 004F87D0, 004F7B10, 004F7D20 ...): build meshes
  through not-yet-mapped mesh code and upload to the device; no way to run them twice. Worth doing
  together with the mesh class (004F30F0, 004F4B90).
- 004F0B90 (TL library loading): constructor with C++ throws and CPUID; once per run.
- 004F1440 / 004F15C0 (shadow matrix with asin/acos), 004F1330 (Rebirth-patched).
- The CDib class (see the map): never runs in a normal game.
- prints.db / acks.db loaders themselves (Rebirth-patched) and the STLport instantiations.

## Renderer (00500B70-005053FF)

Files: `src/game/renderer_init.cpp`, `src/game/renderer_strip.cpp`, `src/game/renderer_sprites.cpp`,
`src/game/device_mesh.cpp`, `src/game/figure_part_draw.cpp`, `tests/verify/verify_renderer_init.cpp`,
`verify_renderer_strip.cpp`, `verify_renderer_sprites.cpp`, `verify_device_mesh.cpp`,
`verify_figure_part_draw.cpp`.
Shared headers: none changed (`clip.h` gives `TLVertex` and the clipper).

The renderer is the global at 007AF060 (vtable 0073D93C). +8 is the transform pipeline (matrix
stacks as aligned arrays of 64-byte matrices: data/count at +0x14, +0x1C, +0x24, +0x3C, +0x44,
+0x4C, +0x54; +0xC the transform object, whose slots project and clip vertices), +0x20 the counted
device holder (004E5E50: +8 the last HRESULT, +0xC DirectDraw, +0x14 the primary surface, +0x18 the
render target (+0x14 width, +0x18 height), +0x54 the IDirect3DDevice7, +0xAC the pipeline again
(its transform object's slot 35 sees every vertex batch when +0x158 is set), +0x150 the override
colour of the mesh draws, +0xE0 vertices sent to vertex buffers, +0x16C / +0x170 / +0x174 vertex,
triangle and call counters), +0x24 the counted state-block helper (004FCA60).

### Rewritten

| Address | Name | Verify |
|---|---|---|
| 00500B70 | `RendererCreate` (slot 2, RET 8) | ours only; holder, helper, projection stack entry and flag 2 of +4 set |
| 00501D90 | `DrawRibbon` (slot 16, RET 0x18) | ours only; result 0 / 1 and whole quads in the index list |
| 00502A20 | `DrawTlTriangles` (holder thiscall) | ours only; call counter + 1 |
| 00502AC0 | `EmitSection` (naked transcription) | original into scratch vertices, 64 bytes compared |
| 00502B90 | `StripDirection` | pure, both compared |
| 00502CA0 | `DrawSprites` (slot 17, RET 0xC) | ours only; result 0 / 1, the last batch's corners back to front |
| 00503680 | `DrawIndexed` (holder thiscall, RET 0x1C) | ours only; call counter + 1 |
| 00503710 | `SpriteRadixPass` | original into a scratch list, lists compared |
| 005037D0 | `SpriteMergePass` | same |
| 004E9010 | `DeviceDrawMesh` (device thiscall, RET 0x48) | ours only; drawn vertices are the corners (position, colours, plain uv), or clipped: the corners' outcodes copied |
| 004E9A50 | `DeviceDrawMesh2` (two textures, RET 0x50) | same |
| 005A7A70 | `DrawFigurePart` (node thiscall, RET 0x1C) | ours only, after the (pure) clip test: result 1; hidden or outside the view: not marked drawn; else lit unless flat |

All run in every game: 00500B70 once at start-up; 00502CA0 / 00503680 / 00503710 / 005037D0 for
particles (004976B0: spell effects, fires; scenarios `spells_cast`, `spells`); 00501D90 and its
helpers for ribbons (lightning-type spell effects, `spells_cast`); 004E9010 / 004E9A50 for every
figure and 3D-library mesh on screen (any scenario). Names are provisional.

### Creation (00500B70)

Driver from the settings (+0x2C index, else the first hardware driver; none: fatal error "no
driver" and exit), the device object (004E5E50), the pipeline's matrix stacks (projection from the
camera settings' near / far / field of view with aspect 4/3, the viewport matrix of the holder's
rectangle, the fixed axis swap 004F1330 and their products; the transposed projection to the
transform object's slot 4), the viewport size and depth range, render states, the texture stage
filters for every stage the device can use at once (settings +0x74 point / linear, +0x70 mipmaps,
cleared when the driver caps lack bit 0x400000 of +0x16C), a default white light at (5, 5, -8),
ambient grey 50, and the helper (+0x24). The helper's flag argument is pushed as EDX with only DL
set: the upper bytes are what the game's operator new left in EDX, reproduced by capturing EDX after
the same allocation.

### Ribbons (00501D90)

A polyline of world points (16 bytes each) projected by the transform object (mode 0: slot 13 with
the stack at +0x1C; mode 1: slot 8 with the stack at +0x24 and an outcode per point, 2 = all off
screen) becomes a strip of cross sections, each two TL vertices offset across the screen direction by
width * rhw * 15 / |direction|; u runs from 0 (0.875 when the length compares equal to 0, NaN
included) in steps of length / (n - 1), v from vStart over vSize. The ends are transparent. Mode 1
skips segments whose both points are flagged and restarts the strip after them. Drawn additively
(source alpha, one) with the states restored afterwards. Mode 2 only sizes the buffers. The x87
sequences of 00502AC0 and of the first cross section (inlined at the first point) are naked
transcriptions (the hypot result divides an unrounded product).

### Sprites (00502CA0)

Points (x, y, z, size) and sprites (colour, pointer to a u0 v0 u1 v1 rectangle) in batches of up to
511: four TL vertices per sprite (colour, black specular, corners (u0,v0) (u1,v0) (u1,v1) (u0,v1)),
then the transform object's slot 24 (the stack top at +0x24 transposed, points, vertices, outcodes,
count) places the corners and returns 0 (all on screen), 1 (some off) or else (nothing to draw).
The batch is sorted back to front on the first corner's z bits (negative z dropped): four radix
passes (00503710, bytes 0, 8, 16, 24, bucket order alternating ascending / descending, buckets
filled last-in-first) above 128 sprites, else merge passes (005037D0) with run widths from the
largest power of two below the count down to 1. Both give larger keys first. Then two triangles per
sprite, (0, 2, 1) and (0, 3, 2), each through the screen clipper (0042D910) when the transform said
so. With the additive flag the blend is source alpha + one for the call.

### Mesh draws (004E9010, 004E9A50)

Identical code; 004E9A50 has two texture arguments in front and draws through 004EB370 instead of
004EB550. The positions come in blocks of four vertices (64 bytes, x x x x y y y y ...); the
transform object projects them into 007AEE10 (16 bytes per vertex: slot 14 with the stack top at
+0x1C, or with flag 2 slot 9 with the stack at +0x24 and an outcode byte per vertex in 007AEE30) and
returns 0 (on screen), 1 (crossing an edge) or 2 (nothing visible: returned as is). Flag 4 first
overwrites the mesh's colours with the device's +0x150 and its speculars with the alpha byte << 24
(4 * colourCount entries each: the mesh's own arrays change). Each corner (three words: position,
colour, texture coordinate) becomes a TL vertex in 007AEE00; the texture coordinate is moved by the
u / v offsets unless both compare equal to 0 (NaN counts as equal). On screen: flag 1 collects the
screen bounds (004E8900), the slot 35 observer, then the draw. Crossing: buffers for ten times the
corners and indices, the corners' outcodes (007AEE20), the clipper (slot 37: vertices, outcodes,
indices in, 007AEDE0 out, both counts), nothing more when either count is 0, else the counts set, the
vertices projected (slot 34), the observer, flag 1 bounds of the clipped vertices (004E86B0), the
draw. The vertex buffer path (004E7820) is dead: it always returns null. Returns 1 when drawn on
screen, else the transform's result.

### Figure parts (005A7A70)

Thiscall on a figure part's scene node (+8 flags, +0x1C the CFigureMesh, +0x20 its screen
rectangle) with the device, the lights ({data, count}), the material (+8 flag 0x10, +0x50 a
vector), an alpha byte, an optional RECT* and two textures (+0x5C each). The mesh's +8 flags: 0x40
drawn (cleared first), 0x80 hidden (nothing more, not even the finishing call), 0x10 lit, 0x100
flat colours; +0x98 the owner (CObject3D), whose figure (+0xF0) draws the mesh when there is one;
+0xCC / +0xD0 the texture offsets, +0xD4 / +0xD8 the flat colour and specular, +0x128 / +0x12C
per-vertex offsets, +0x138 / +0x148 the colour and specular arrays (4 per normal packet), +0x15C
the mesh type, +0x188 the clip bounds.

1. The clip test (004F16A0 on the device's pipeline); 2 = outside: return.
2. Types 13..16: the node's flags take 00790C0C and 005A7160 animates it to ticks + fraction.
3. Flat meshes: the figure's slot 7 (name, colour, specular, the arrays) or both arrays sized and
   filled. Unlit meshes: the inverse of the model matrix (the pipeline's stack at +0x2C) moves each
   light's position (+0x1C -> +0x28) and the sun's (007AF07C) negated direction (+0xD4 -> +0x108)
   into the mesh's space; 0068D280 sets each light's colours for the material; the sun goes to
   the transform object (slot 2); the figure (slot 8) or the transform object (slot 40) lights the
   mesh. With 0079BD28 set the mesh named "crown" has every colour pulled halfway to the average.
   The speculars become alpha << 24 unless the material has flag 0x10; the mesh is marked lit.
4. Draw flags: 2 crossing the view, 1 rectangle wanted, 8 the material's flag 0x10. With a figure:
   slot 11 / 9 (with the offsets array) or 12 / 10, textured first, each with the mesh's name as
   an STLport string; the rectangle from the figure's piece of that name; drawn (0x40) unless the
   slot returns 2. Without: the offsets are added into a copy of the positions (007C2E48), then
   004E9A50 (both textures) or 004E9010; with a rectangle: empty when the draw returned 2, else the
   device's +0x15C, and only then the drawn flag.
5. 005A7290 with the renderer.

Quirks kept: without a rectangle the device path never sets the drawn flag; the string members
are called with the original's allocator and tag arguments. A singular (or NaN) model matrix leaves
the original's inverse in uninitialised stack slots: ours uses the previous call's inverse, which is
what those slots usually hold.

### Floating point

- 005A7A70: the inverse, the light and sun transforms, the crown blend (the red sum reaches the
  first colour unrounded, the others reload it rounded) and the offset adds are naked
  transcriptions; their x87 sequences were compared with the original's instruction by instruction
  (same opcodes and register operands). The time is FILD ticks + fraction, rounded once.
- 004E9010 / 004E9A50: positions and texture coordinates are copied with FILD / FISTP qword (exact,
  memcpy here); the offset add is FLD offset, FADD coordinate, FSTP (`AddOffset`, naked so that the
  operand order and the offset's own bits are kept).
- 00502AC0 and the ribbon's first cross section: naked transcriptions (see above).
- 00501D90: the u step is a double division rounded once to float; the "length equals 0" test is
  `!(a < 0) && !(a > 0)` (FCOMP / C3 semantics, NaN counts as equal).
- 00502CA0: the view matrix's upper triangle goes through FLD / FSTP (an sNaN would come out quiet,
  `ThroughFpu`), the lower as dwords; the clip limits are the render target's size loaded as
  zero-extended qwords (`x87::FromUnsigned`) and rounded to float once. Corners and colours are
  copied as dwords.
- 00500B70: the ambient grey is three FISTPs of 50.0f; the projection aspect is the float bits
  0x3FAAAAAB.

### Original quirks kept

- 00502CA0: without a primary surface (holder +0x14 null) the clip path uses two stack slots the
  original never initialises for the screen size; ours keeps the last values, 0 at first. The
  surface exists whenever the device draws, so this should not be reachable.
- 00502CA0: when a draw fails it returns 0 without restoring the blend states. The vertex buffer's
  resize to the batch drops the contents if it ever has to grow (it never does: it is reserved for
  0x2000 vertices first). The 16-byte array at 007AEFA0 is sized to 0x200 every call and never used.
- 00502CA0: the sort writes the key of a dropped sprite into the next free record.
- 00501D90: the index array is sized for the ribbon and freed at once before the indices are added.

### Verify notes and risks

- The draws themselves (00500B70, 00501D90, 00502CA0) can only run once, so they are checked by
  invariants, not side by side; the pure helpers are compared with the original.
- 00502CA0's radix and merge passes compare only when a batch has more than 128 (radix) or 2..128
  (merge) visible particles; a big fire or a long spell gives both.

## Unit skins (00568800)

Files: `src/game/unit_skins.cpp`, `tests/verify/verify_unit_skins.cpp`. Shared files: none.

| Address | Name | Verify |
|---|---|---|
| 00568800 | `BuildUnitSkins` (thiscall on the zone client object, RET 8: others, dressed) | ours only (texture cache and figure maker side effects): the two record vectors are left as they were |

Called by the zone load 00567AB0 (Rebirth-patched for its map paths; this function is not), so by
every zone load. `this` +0x18C is the texture cache, +0x34 the figure maker.

- Dressed units (the second argument, 0x24-byte records: +0 the prototype index into 007AF358
  (0x120 bytes, +8 the model index into 007AF398, 0x168 bytes, +0xE8 its texture prefix), +4 the
  item ids (7 dwords: 0 head, 1 / 2 the weapons, 3..6 the armour), +0x10 / +0x1C a weapon list and
  an index byte, +0x20 the unit number or -1): the texture records (005683D0 with 1) are copied
  into the set's byte array (SetSize inlined), the names are "<prefix>skin_NN.mmp" (NN the
  prototype's +0xC; "skin_00" without a prototype), each armour piece and the weapons
  "<prefix><item>.mmp" (006A3480; the weapon whose armour record +0xB8 is lower first when both
  are there), and the set (size 256) is composed by the cache (00693180, name "mo<n>p" or
  "prot<prototype>p") and registered (00693F10, kind 1); then the records again with 0, the head
  item and the extra weapon (weapon 0056E440 and material 0056E400 records, 006A0BF0), size 128,
  "mo<n>s" / "prot<n>s", kind 2. The names are built with the original's inlined strcpy / strcat
  into 256-byte buffers.
- Other units (the first argument, 0x3C-byte records): their texture names (from the prototype,
  00535CB0, when the record's name list +0x30 is empty, else the record's names appended to a
  vector<string> after an inlined reserve) are joined with the second list (00575E50) and given
  to the figure maker's slot 3 with the model prefix and the record's +0x24 / +0x28 / +0x2C.

## Flat geometry draw (004EA4A0)

Files: `src/game/device_shadow.cpp`, `tests/verify/verify_device_shadow.cpp`.

| Address | Name | Verify |
|---|---|---|
| 004EA4A0 | `DeviceDrawFlat` (device thiscall, RET 0x24) | ours only: result 0 / 2; with one vertex per position every vertex carries the colour pair |

Callers: the object shadows (0051CE60), the renderer (00503A40), 006CEDE0. Arguments: positions
(blocks of four), one (u, v) per vertex, the block count, indices and their count, the colour and
specular of every vertex, a value for the projection, a z scale. The projection is the pipeline's
004F2650 (projected vertices into 007AEE10, sized ten times the vertex count, outcodes into
007AEE30). On screen: the vertices (projected position, the colour pair, the texture coordinate,
all copied as qwords) go to the vertex buffer (004E7820, always null) or to 007AEE00, z is scaled
(FLD scale, FMUL z) unless the scale compares equal to 1 (NaN too), and 004EB550 draws them.
Crossing: buffers for ten times the vertices and indices, the outcodes copied to 007AEE20, the
transform object's clipper (slot 37), the counts set (kept contents), slot 34, then the scale and
the draw unless a count is 0. Returns 2 when nothing is visible, else 0.
