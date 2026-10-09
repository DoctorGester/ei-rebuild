# Engine base (0x00401000-0x00430000, quaternion / ISAAC at 0x0049E790-0x0049F810)

Maths, transforms, reference counting, the object manager, streams and chunked files, the camera,
triangle clipping. Network code of the same address range is in `network.md`; the application layer
(0x00440000-0x00470000) in `app.md`. How the classes are declared after the cleanup:
`cleanup-engine-base.md`. Names are provisional (set in Ghidra). Unless a row says otherwise a
function is reached by every scenario (startup databases, new game, save / load, exit).

## Target binary

- Target: `starter\Engine\game.exe` (GOG 1.06, Russian, built 2001-03-26, VC6 12.00.8966; PDB path
  `D:\home\Allods3\Game\ReleaseRussian\Game.pdb`). The root-folder copy of the Rebirth install
  differs in two bytes: the Large-Address-Aware flag (0x13E) and the disabled CD check
  (`EB` instead of `74` at 00456BB5).
- Linked in: MFC 4.2 (static), static CRT with old iostreams, STLport (version not identified). The game's own classes
  have no RTTI: classes are recovered from vtables (`config/vtables.csv`). Bink, Miles (MSS), DirectX 7,
  WSOCK32 by ordinal. `2DintMMX.dll` and `3DfpFPU.dll` (VC6, 2000) are loaded by name and are not
  rebuilt.

## Maths and transforms

File: `core_maths.cpp`, `core_transform.cpp`, `core_transform_math.cpp`, `quaternion.cpp`,
`random.cpp`. Verify: `verify_core_maths`, `verify_core_transform`, `verify_quaternion`,
`verify_random_init`.

| Address | Name | Verify |
|---|---|---|
| 0040CDC0, 00402200, 00410770, 004022D0 | `AddVectors` (`__fastcall`: out ECX, a EDX, b stack), `NegateVector`, `ScaleVector`, `ClampUnit` (`__stdcall`, [-1, 1], NaN gives -1) | pure, side by side |
| 00410750 | `Vector3::AddInPlace` (returns its argument) | scratch copy |
| 0040CB00, 004018A0 | `Quaternion::SetIdentity` (returns this), `PreMultiply` (`*this = r * *this`) | pure / scratch copy |
| 0040D940, 0040F560, 0040D6B0 | `Quaternion::AxisX/Y/Z` | pure |
| 0049E790, 0049E800, 0049E870, 0049E950, 0049ED20 | `SetAngleAxis`, `SetAxisAngle`, `PreMultiplyConjugate`, `MultiplyByConjugate`, `Slerp` | pure / scratch copy |
| 0040E3D0, 0040E440, 0040E460 | `CubicSegment::SetBSpline`, `Evaluate`; `CubicCurve3::Evaluate` | writes its object only / pure |
| 00401490, 0040CB80, 0040CC30, 00401510, 00401540 | `CTransformObject` slots 13-17: CopyTransform, CombineRotation, UpdateWorld, SaveTransform, LoadTransform | scratch copy; UpdateWorld on a scratch copy with the base vtable; Save / Load against a recording file |
| 00401570, 00401680, 00401940, 00401B50, 00401E50, 00401F00, 00402220 | `CTransformObject` WorldMatrix (4x4), TiltTowards (down axis a tenth of the way to a target), TurnTowards (forward axis onto a target), RollTowards, Rotate(axis, angle), TurnUpTowards, SetHeading (atan2 about +Z) | scratch copy, rotation / matrix bitwise |
| 004667F0, 0049EE70, 0049F150 | `IsaacRandom::Next`, `Refill`, `Init` (the generator is at 007A9D40; seeding 0049F660 reads 1 KB of a random file from C:\ with the recursive CFileFind 0049F4B0, mixed with `rand()`; not rewritten) | Init: original on a scratch copy with the seed material replayed through a wrapper on 0049F660 |

- `CTransformObject` (vtable 0073BA38, abstract, 0x54 bytes): CEngineObject + local position (+0x1C),
  world position (+0x28), local rotation (+0x34), world rotation (+0x44). Slots 13-17 are the five
  methods above, shared by ~90 vtables; slot 14 is overridden by 00516370 in the unit classes
  (writes the world rotation only). Slots 16 / 17 take an MFC `CFile*` and call its slot 14 / 13.
- The camera's quaternion products in `ApplyView` and `Update` add the terms in different orders; each is written out separately.
- Float rules: plain C++ (rule 7). `SetBSpline`'s 1/6 factor was distributed by `/fp:fast` and gave
  different results at 24 bit, so that function keeps `float_control(precise, on)`. Values the
  original rounds through a float temporary go through `StoreFloat`.

## Reference counting

File `core_refcount.cpp`, `refptr_copies.cpp`, `deleting_destructors_400.cpp`.

- `CRefCounted` (vtable 0073BB68: 16-bit count at +4, copy-on-share flag at +6 bit 0, detached flag at
  +7 bit 7, slot 0 deleting destructor, slot 1 Serialize). `CRefObject` (0073BB58) derives from it
  and adds slot 2 shutdown, slot 3 clone. The network sessions derive from the root because their
  slot 2 is RemoveClient. `RefPtr` is the one-dword owning pointer.
- Rewritten: 0040A3A0 `CRefObject::Release`, 00417DF0 `Shutdown`, 00419CD0 `RefPtr::Assign`, 00418BE0
  `Share`, 00413270 `Destroy` (dry run on stand-in objects whose vtable records the destructor /
  shutdown / clone calls: headers, call records, return value and pointer compared).
- The compiler emitted ~100 out-of-line copies of these helpers all over 0x400000-0x480000 (21 of
  `Destroy`, 21 of the 28-byte release, 9 `RefPtrShareReturn`, ...): all in `refptr_copies.cpp`, verified
  with the same dry run. The release copies cannot have a trampoline (short jump at +4); one copy of
  their identical 28-byte body is made when the DLL loads. 33 scalar deleting destructors that only
  call their body share one template in `deleting_destructors_400.cpp`.
- Kept: Release / RefPtr helpers test the pointer for null after already using it (omitted, same
  behaviour); `Destroy` leaves the pointer set.

## Object manager (CEngineObject, CBaseManager)

File `engine_object.cpp`, `base_manager.cpp`, `hash_tables_400.cpp`. Verify `verify_base_manager`.

- `CBaseManager` (vtable 0073B870, 25 slots) layout: +0x1C objects (STLport hash_map id -> object,
  highest id at +0x30), +0x34 MFC CPtrList of registered objects, +0x50 id -> list position, +0x68 type
  -> CPtrList* of spare objects, +0x80 type -> spare count (each table is followed by its highest
  key), +0x98 / +0x9C id range [low, limit) for new ids, +0xA0 / +0xA4 (-1 constructed, 0 / 300 after
  Create), +0xA8 last id, +0xAC context (Init / SetContext). Slots: 2 Create, 3 Destroy, 9 / 10
  Load / Save, 13 Init, 14 NewObject, 15 DestroyAll, 23 CreateObject, 24 ReleaseObject.
- Objects are never freed while the manager lives: RemoveObject turns them into spares of their type
  (id -1) and CreateObject takes a spare before asking slot 14. Ids are maxId + 1 while that fits the
  range, else `rand()` probing (100000 tries, then the fatal "CBaseManager::RegisterInstant" and id -1).
- Rewritten: ConstructBase 00402B20, DestructBody 00402DF0, deleting destructors 00402DD0 / 00402D20, CopyFrom 00402D40, name / description 00402CE0 / 00402CF0, Create
  00403020, Init 004031B0, Destroy 004031E0, DestroyAll 004034E0, CreateObject 00403730, RemoveObject 00404040, Unregister 004039B0, RegisterInstant 00403BD0, FreeRecords
  00404410, Load / Save 004046E0 / 00404760, LoadObjects / SaveObjects 004047E0 / 00404860 (end mark -0x521), ReleaseUnused 004048F0, `ObjectListIterator::First / Next`
  00404920 / 00404960, SetContext 00403720, the empty slots 00402D60 / 70 / 90 / A0 / B0, the id tables 00404E80-00406AA0 (`hash_tables_400.cpp`) and the class registry
  0079B270 (00408EA0, 00408D60). Verify: ours only with state checks, or a scratch copy (ConstructBase: `BuiltCompare`); DestroyAll and Unregister / RegisterInstant (a derived
  manager around vtable 00743D10) are reached only through vtables.
- Not hookable (shorter than the 5-byte jump): 00402CC0, 00402CD0, 00402D00, 00402D10, 00402D80,
  00402DC0, 00403010 (RETs / `return 0` slots).
- ConstructBase and the registry constructor copy uninitialised stack bytes into the hashtable functor bytes (+0x1C /
  +0x50 / +0x68 / +0x80); ours writes zeros (the wrapper copies them from ours before comparing).
- Kept: DestroyAll and Destroy read a node's next pointer after Release may have freed the object (the
  STLport pool keeps the memory); LoadObjects reads an id equal to the end mark as an object id when a
  1 byte follows.

## Streams, chunked files, archives

File `core_streams.cpp`, `chunk_stream.cpp`, `chunk_archive.cpp`, `net_coder.cpp` (GameBuffer).
Verify `verify_core_streams`, `verify_chunk_stream`, `verify_chunk_archive`.

- `CStream` (vtable 0073BDB0): +4 direction byte, +5 ok, +8 limit / save format version (2000000000
  when constructed). Slots 0 destructor, 1 Read, 2 Write, 3 Seek, 4 Size, 5 Position, 6 Clear (memory
  streams empty, file streams rewind), 7 CopyFrom (004A4D40).
- `CMemoryStream` (0073BD90; the queue variant 0073C960): +0xC byte being assembled, +0x10 mask of the
  last bit (0x80: a new byte is due), +0x14 index of the byte the bits go to (-1: none), +0x1C
  capacity. Slots 1 Read 004A5380, 2 Write 004A53F0 (grows to twice the need), 3 Seek 004A5490 (also stores
  pending bits), 4 Size, 5 Position, 6 Clear 00417350, 7 CopyFrom 004A55D0. `Resize` 004A54D0 keeps the
  contents (1.5x + 0x10), `SetSize` 004A5570 is exact and drops them.
- `FileStream` (0073C8D8, 0x1C): a CStream over an MFC `CFile` at +0xC; each slot calls the CFile's own
  virtual (MFC 4.2: 3 GetPosition, 8 Open, 10 Seek, 12 GetLength, 13 Read, 14 Write, 19 Close). The
  save code builds these on the stack. Defined in `net_link.h`.
- `Chunk` (vtable 0073BE94, 0x18: vtable, id, consumed, size, start, parent; not the models'
  `ChunkArchive`): slot 0 a handler called for every chunk `ForEachChild` reads (empty in the base),
  slot 1 the file (base asks its parent). Sizes include the 8-byte header; every byte read or
  written is counted in the chunk and all its parents. `RootChunk` (0073BE9C, CFile at +0x18; the .mob
  loader uses vtable 0073FAD4) owns the file: Open 0042D160, constructor 0042D1C0, Destruct 0042D270.
  `ChunkArchive` is a different format over two memory streams (below).
- `ChunkArchive` (0x74, constructor 0053A7B0): +0 index stream, +0x28 data stream, +0x54 version (1),
  +0x5C hash_map of loaded objects, +0x70 list of objects to save; class tables 0079B284 (class id ->
  factory) and 0079B270 (vtable -> class id). A saved archive has three top-level chunks: 1 (scratch
  data, read back on load), 0 (index: class id and key per object), 2 (data: one child 1 per object with
  child 0 = key, child 1 = the object's fields). A loaded archive's Close gives every loaded object one
  reference and takes it back.
- `CoderState` (0x2828) and the LZ coder are in `network.md` (compression).

| Address | Name | Verify |
|---|---|---|
| 0041DD70, 004173D0 / 004790A0, 004173B0 / 004790D0, 00417350 | `CMemoryStream` Construct, Destruct (two copies), DeletingDestruct (two), Clear | scratch copy (buffer cleared where the original would free) |
| 0047A200, 004174E0, 00417400 | `CStream` ConstructBase / DestructBase / DeletingDestruct | scratch copy |
| 00412720 / 00413430, 00412730, 00419720, 00412700 | `CRefObject` / `RefValue` base destructors and deleting destructors | scratch copy |
| 0047B940, 0047B620 | `AlignedPointerArrayAlloc`, `AlignedObjectArray::Clear` (32-byte aligned blocks, header pointer before the data) | ours only: alignment, emptied array |
| 0042FCA0 | `CoderState::Construct` | scratch copy (0x2828 bytes) |
| 0042BEC0-0042D0E0 (30) | `Chunk` Construct, Destruct, Advance, File, Begin/EndWrite, WriteHeader, SkipContent, ReadHeader, ForEachChild, Read / Write Float / Int / Byte / Vector3 / Quad / Quaternion / Bytes / Blob; 0042C580 ReadString, 0042C650 ReadEncodedString (content decoded by 004D8E10), 0042C820 WriteString, 0042CA20 / 0042CB80 Read / WriteStringArray; RootChunk Open / Destruct | recorded file (below); ForEachChild, strings through the recording proxy; ForEachChild ours only |
| 00409070-0040A5A0 (24) | `ChunkArchive` NextChild, FindChild, CountChildrenOf, CountChunks, Field, EnterChunk, LeaveChunk, AppendChild, ResizeToCurrent, `Ref::Reset`, Read / Find / WriteStreamChunk, Open, Close, CStringField (004093A0), StringField (00409470), StreamField (00409640), SaveObjectRef (0040A440), ObjectRef (0040A5A0) | pure side by side; EnterChunk original first (chunk popped) then ours; others ours only + depth / size; loading fields: original into a scratch |

- Recorded-file verify (chunks, FileStream, transform save / load): ours runs with the CFile's vtable swapped for a
  recorder, the original on a byte copy whose CFile replays the log; calls, bytes, chunk chains and outputs compared
  (over 256 calls / 256 KB skipped; checks do not nest). Memory streams are deep-copied only for the two CMemoryStream
  vtables; other stream classes run ours only.
- Kept: `ReadQuad` returns the file's values in the order 2, 0, 3, 1; `ReadVector3` copies x and z
  through the FPU (a signalling NaN becomes quiet; `FpuCopyBits`), y as an integer; `ForEachChild` reads
  the parent without a null check; `CMemoryStream::Destruct` leaves the freed pointer;
  `SaveObjectRef` registers the object while loading and then searches the field without using it;
  `ObjectRef` writes key 0 when saving; `LeaveChunk` (writing) takes the child's content pointer before
  `AppendChild` resizes the stream; a failed `EnterChunk` while loading leaves the chunk pushed.
- Not rewritten: 0042D260 (RootChunk::File). The memory stream's own slots, Resize / SetSize, the bit
  helpers and the sized / header helpers (004A4D40-004A5650) are in `core_memory_stream.cpp` (script.md,
  "Leftovers").

## Triangle clipping (0x0042D910-0x0042E6A0)

File `clip.cpp`, `verify_clip`. `ClipTriangle` 0042D910 and the six plane clippers 0042DB70 (X below),
0042DD00, 0042DE90, 0042E020, 0042E1B0, 0042E340 (Z above) and `LerpVertex` 0042E4D0 clip the mesh
renderer's software path (meshes drawn through slot 17 of vtable 0073D93C, 00502CA0, whose triangles
cross the viewport). Verified ours first, inputs restored, original, all outputs compared (the original `ClipTriangle`
calls the clippers through their hooked addresses).
Compiled with `float_control(precise, on)` (`(b - a) * t + a`); the intersection ratio is computed in
a small naked helper (`StoreRatio`) because the original stores the float with FST while comparing
the unrounded quotient with 1.
Kept: a vertex exactly on a "keep above" plane is kept out but the t = 0 intersection is added; t above
1, below 0 or NaN emits the edge's end vertex; the 32-index work buffer is not bounds-checked.
0042E700 / 0042F4C0 / 0042F890 are dead polygon-clipping variants.

## Camera (0x0040C8B0-0x004114F0)

File `camera.cpp`, `camera_motion.cpp`, `camera_object.cpp`, header `camera.h`. Verify
`verify_camera*.cpp`, scenario `camera.txt` (walks the hero, pans with arrows, F9-F12, the two script
camera commands, then PlayCamera on `camera\bz3g.cam` until IsCameraPlaying is 0). The camera object
is at 0079B2A8 (flags at 0079B2B0; 007AF7C8 points to it for the camera-file player). The per-frame
call is `CCamera::Update` from the game frame 00456440 (WinMain; Rebirth patches the caller).
`CCamera` ("CCamera", class id 0x43, vtable 0073B9E0) derives from `CTransformObject`.

Layout is in `camera.h` (named members). Not obvious from it: flags 0x8 follow the target, 0x10 file open (recording), 0x20 playing, 0x40 free placement, 0x100
bounded, 0x200 briefing limits; the view stack at +0x1E0 is a deque (push 00410E70 / pop 00410EF0, no Ghidra functions); keys at +0x2F0 are 0x24 bytes (time, ?,
position, rotation); +0x2E0..+0x2E8 near / far / fov are read by `renderer_init`; pitch is kept in degrees (+0x104, settings +0x1D8 / +0x1DC).

| Address | Name | Verify |
|---|---|---|
| 0040FB30, 0040F8B0 | `Update(dt)` (listener, follow / track, pan, tilt, turn, zoom, limits, ApplyView, shakes), `ApplyView` | scratch copy of the first 0x120 bytes; Update ours only while shakes run |
| 0040D9B0 | `SampleTerrain` (`__stdcall`, 9 args: average / min / max surface height on a grid) | outputs into scratch |
| 0040F5C0, 0040F230, 0040F430, 004107A0 | `SetFocus`, `Pan`, `StopMotion`, `ZoomBy` (mouse wheel, 1.05^steps via `_CIpow`) | scratch copy |
| 0040E810, 0040F360 / 0040F3C0 / 0040F420, 0040D3B0 / 0040D420 | `GroundPoint`, `SetTurnRate` / `SetTiltRate` / `SetUnknownD0`, `SetTarget` / `SetTargetId` | output / scratch copy |
| 00410860 / 004108D0, 00411470, 00410D70 / 00410D10, 004109E0, 00410E50 / 00411060 / 00411360, 004110D0, 0040D710, 0040E4C0 | `SaveView` / `RestoreView` (F9-F12; a modifier saves), `SetDistancePitch`, `ViewsToSettings` / `FromSettings` (`_CIacos`), `SetHeading`, `SetFree` / `LookFrom` / `SetBounds`, `LookDirection`, `AlignToTarget`, `ClampAboveObjects` | scratch copy (0x1E0 + settings record) |
| 004113A0 | `Shake` (distance by `_hypot`, node added to the list at +0x108) | original on a scratch camera with a private list, ours on the real one; new node compared |
| 0040F1B0 / 0040EFB0 | `PlaybackStart` / `PlaybackStep` (camera\*.cam, PlayCamera) | scratch copy (0x308) + result |
| 0040C8B0 .. 0040FB20 (17) | constructor, destructors, class name / id, Create / Destroy, Init, GetRange, Anchor, engine-object / transform destructors | `BuiltCompare`, side by side |
| 0040E910 / 0040EB60 / 0040ECB0, 00410AB0 / 00410C10 | OpenFile (`.\camera\` + name; an existing camera.cam renamed first) / RecordFrame / CloseFile; LoadState / SaveState (slots 0073BA04 / 08) | ours; recording only with camera recording on |
| 00412120 | the recorded-path array allocation (36-byte records, 32-byte aligned) | |

Float notes: `camera.cpp` and `camera_motion.cpp` use `float_control(precise, on)`; constants the
original multiplies at run time (pi, 1/180, 90, 1.05, the 0 of `StopMotion`) are read from the exe
through volatile reads; `TerrainOwner::SurfaceHeight` (00564070) and `Vector3::Length` (00401E20) are
called through `double`-returning forwarders (ST0 unrounded); the shake's `exp` is the original's
inline FLDL2E / FRNDINT / F2XM1 / FSCALE sequence (naked `Exp`).

Kept: `RestoreView` sets distance and pitch only (the saved rotation goes to the settings record but
never to the heading); `SampleTerrain` counts the centre sample once more than the grid and ignores its
2nd / 3rd argument; `StopMotion` multiplies the view axes by 0 (so (+-0, +-0, 0), NaN for NaN) and
replaces turn / tilt rate only when NaN; `PlaybackStep` at the last key places the camera without clearing
the playing flag; `Update` tracks a target only with flag 8 clear and panning clears target and flag 8;
`SetCameraPosition` (0x13) / `SetCameraOrientation` (0x14) script commands do nothing;
`CameraOpenFile` reads each numbered file's number from the title past the fourth character, which is
always 0, so an existing camera.cam always becomes camera0001.cam; `CameraRecordFrame` writes 4 stack
bytes the original never sets (ours zero).
Not hooked: 0040ECD0 (camera file load, Rebirth-patched, `CameraPatch`), 0040C7E0 (static initialiser),
0040DB50 (2 KB spline camera path, no references).

## Startup tables and dead code

- 0042FAE0 (static initialiser from the .data list 007830C0) fills the coder tables 0079B6B0 /
  0079B738 / 0079B7BC / 0079B844; rewritten in `net_coder.cpp` for the standalone build only (it runs
  before the hooks are installed). The other static initialisers in the .data lists (00783038-007830D0,
  007831F8, 007831FC) run before the hooks too and are not hookable.
- Dead (config/dead_code.csv): 00431070-00432D20 (a RIPEMD-like key exchange and stream cipher),
  00402310, 00402780 (radix sort), 0042E700 / 0042F4C0 / 0042F890, 0047A710-0047ACA0 (Huffman builder),
  the 3DS readers at 004A0350-004A4800 and more found by scanning aligned starts after a RET. A
  rerun of `tools/find_dead_code.py` drops hand-added rows that only coincidental dwords keep alive.

## MFC CString: Mid and Right

006FDA6D is `CString::Right(nCount)`: it clamps a negative count to 0, returns a copy when the count reaches the
length, else `AllocCopy(dest, nCount, len - nCount, 0)`. 006FD9B4 is `Mid(nFirst)`: it calls `Mid(nFirst, len - nFirst)`
(006FD9D7). Call sites passing `len - k` (scene_loader, scene_material, script_parse, item_constructor, options,
spell_text, world_game) therefore use `Right`; camera_object's `title.Right(4)` is 0040E9A0 calling 006FDA6D with 4.
Views with their own `Mid(int)` (console::Str, ...) still call 006FD9B4. config/library.csv has them in this order.

## Shared containers

The game's container templates are declared once in `src/lib` and reused by every port (rewrite guide, "Clean code";
`tools/check_cleanup.py` counts per-file `struct ...Array/...List` layouts: none remain). Which original addresses belong
to which template:

- `mfc::CArray<T, Elements>` (`src/lib/mfc.h`, 0x14 bytes: vtable, data, size, maxSize, growBy). MFC 4.2's SetSize /
  SetAtGrow / Add / InsertAt / RemoveAt / Copy written once: growth to max(newSize, maxSize + step), step = growBy or
  size / 8 clamped to 4..1024, memcpy of the old elements, only the new ones constructed, the old block freed without
  destruction; SetSize(0) destroys and frees; RemoveAt memmoves the tail down. `Elements` is the instantiation's
  ConstructElements / DestructElements: `PlainElements` (zero-filled, nothing destroyed) by default, `RawElements`
  (nothing), or its own pair (terrain records 006C4F10 / 006C4EE0, flag arrays 006C4F90 / 006C4F60, tile lists 006C5050 /
  006C5010; the bone's morph frames 00452E20 / 00452DF0; the scene loader's `OriginalElements<T, Make, Unmake>`). The
  vtable is a field: each instantiation keeps its own constant. CByteArray / CWordArray / CDWordArray / CUIntArray share
  the layout (`CArray<uint8_t / uint16_t / uint32_t>`); their SetSize stays MFC's own code (006FF64A, 006FEEFA, 006FF993,
  006FF29A via originals_library.h). `mfc::CStringArray`: CArray's layout over CStrings without constructors (it sits in
  layouts); its members are MFC's own code through their addresses (Construct 006FE925, Destruct 006FE958, SetSize
  006FE9B8, SetAtGrow 006FEBFA / 006FEBCE, RemoveAt 006FED3C, Copy 006FEB61; src/lib/mfc.cpp); `console::CStringArray`
  adds the constructor / destructor for locals.
  Aliases and registrations: `RecordPtrArray` (world.h) = `CArray<uint8_t*>` (vtable 0073FAA4; SetSize 005719B0 /
  SetAtGrow forwarded); `ZoneExitArray` = `CArray<ZoneExit>` (SetSize 00573080, `ZoneExitsSetSizeOriginal`); world_save's
  id / object / shared lists and world_party's carried items (+0xB0, `AlignedArray<CEngineObject*>`, 0059CB50 the aligned
  SetSize) with their original members (005985C0, 0059CA60, 0059CC60, 004C98F0) as free forwarders; particle arrays
  00497D50 (sprites) / 00499200 (draw order); PathCellList NewNode / AddHead / AddTail 005B1B70 / 005BD200 / 005BD2A0;
  AILookArray SetSize / Copy 005D8EE0 / 005D8B60 (Copy assigns element by element where 005D8B60 memcpys: same bytes);
  TweenArray RemoveAll / SetSize 006D43E0 / 006D4840; PartyMemberArray SetSize 00663FA0; the trap's targets
  (`CArray<TargetRecord>`). Out-of-line instantiations still forwarded with the template as their type: 00557B80 (combat
  steps), 005B16D0 (excluded regions), 005C8560 / 005C8950 (motion points / step speeds), 00453CA0 / 00453E00 (scene
  corner records), 0044F7E0 (bone morph frames), 00474DB0 (scene mesh groups), the IndexArray (`CArray<uint32_t>`, 0073C800)
  SetAtGrow / Add / destructor 004773F0 / 00476640 / 00476780; 005C4C20 (the path smoother's node pool) is
  `CArray<SmoothNode>::SetSize`.
- `mfc::CList<T, Elements>` (0x1C bytes: vtable, head, tail, count, free, CPlex blocks, blockSize) with NewNode / FreeNode /
  RemoveAll / AddHead / AddTail / RemoveHead / RemoveTail / InsertBefore / InsertAfter / RemoveAt / Find / FindIndex.
  `mfc::CTypedPtrList<T>` is `CList<T*>` (CPtrList's nodes: data zeroed, nothing destroyed; vtable 0073D454;
  `ProjectileList` with FreeNode 0059C810 forwarded). The combat / ai_refresh / magic_trap / world_quests / script lists,
  BlockCostList, the sound cache (`WaveList`, CSoundManager +0x854), ScriptDefineList / ScriptList and ai_plan's region /
  waypoint lists are aliases.
- `mfc::CMap<K, V>` (0x1C bytes) with its `CMapAssoc` (pack 4: a 64-bit key at +8); the inlined GetNextAssoc walk (`Next`)
  and RemoveAll. Hashing and the association pool stay with each instantiation's GetAssocAt / InitHashTable / NewAssoc
  (scene meshes: 004540A0 / 00453FD0 / 00454040, 004541C0 / 004540F0 / 00454160, 004542E0 / 00454210 / 00454280).
- `lib::AlignedArray<T>` (`src/lib/containers.h`, 0x10 bytes: data, size, capacity, growBy): the engine's arrays in 32-byte
  aligned blocks (operator new of count * sizeof(T) + 0x24, the block pointer stored before the data; `AllocAligned32` /
  `FreeAligned32`). The per-size allocators 00406B30 / 00406B50, 00452C20, 00452D80, 00452DA0, 00452E70, 00452E90, 00453A10,
  00468F70, 004ECB50, 004ECB70, 004F65B0, 00600A80, 00695B00-00695B40, 00476EF0, 00477250 all compute the same. SetSize(n,
  growBy) keeps the contents (no element construction); SetSizeDiscard is the renderer's inlined variant that drops them
  when it grows; Add / SetAtGrow take the element by value; RemoveAt shifts down; RemoveAll frees; `Copy` = operator=.
  Registered on it: 005A6E70 (figure mesh colours), 0044F960 / 0044FA90 / 00452FC0 (ui_mesh_build sprite arrays), 004F57B0
  (`TriangleIndexArray`, 6-byte triangles), 006003D0 / 006004E0 / 00600730 / 00600840 / 00600950 / 00601D00 SetSize and
  006005F0 SetAtGrow (`UiPtrArray<T>` = `AlignedArray<T*>`), AlignedPtrArray = `AlignedArray<void*>` SetAtGrow / SetSize /
  Add 00519E80 / 00519D70 / 00519A10 and copies (SetAtGrow(-1) faults like the original's), the scene loader's vertex
  arrays (`PointBuffer`, `UvBuffer`, `FaceBuffer`, `BlockBuffer`, `DwordBuffer`; 00452AF0, 0044FBB0, 0044F410, 0044F1D0,
  0044F1B0), `CUiRegion::points_`, unit_skins' texture set bytes. The animation / figure loader copies (0044F1B0-00454A30)
  are in aligned_arrays.cpp (element types `Blob<K>`).
- STLport `stlp::string`, `vector`, `hash_map`, `list`, `map`: `src/lib/stlp.h`. `list<T>` is the sentinel pointer (nodes
  next, prev, `data`); the instantiations keep their own insert / erase / clear, reached as free or static `__fastcall`
  forwarders (world_client_maps `GridOpsA/B`, `PlainOps`, `CellOps`; the text manager's 004E1530 / 004E1550 / 004E1590 /
  004DE930 / 004DEAF0 / 004DEB60). `vector<T>` has `Start` / `Finish` / `EndOfStorage` references for code that inlines
  the original's reserve / push_back (unit_skins, party_records' `NamedPartyRecords`). The device's mesh buffers
  (007AEDE0-007AEE3C): `src/game/device_buffers.h`.
- Scene loader: `scene::VtArray` is `CArray<char, RawElements<char>>`, an untyped view of the caller's record arrays
  (meshes, nodes, helpers, figures, bone links), resized only through `Typed<...>` (the CArray of the same layout with the
  original's element pair). Owners `SceneNodes` / `TextureLinks` / `TextureNames` are RAII wrappers over VtArray.
- Left off the templates: map_database.cpp's own `ArrayBody` + generic `SetSize` (allods, maps, zone exits; untyped, not
  counted by the checker); the trap's areas array (`PlainArray`, 12-byte records) forwards to its SetSize 0046EAD0.

## Open items

- Library instantiations (config/library.csv, ~270 in 0x400000-0x440000) stay original and are reached through forwarders;
  the standalone build replaces them (`cleanup-plan.md`, phase 2).
- Static initialisers and `WinMain` (00456440) cannot run through the hook; 004632A0 (lever table constructor), 00444740
  (debug panel), 00458870 (GameOptions) and 0045E820 are examples.
