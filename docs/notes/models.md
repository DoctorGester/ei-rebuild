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
