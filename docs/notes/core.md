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

Rewritten (grep the address): `AddVectors` 0040CDC0 (`__fastcall`: out ECX, a EDX, b stack), `NegateVector` 00402200, `ScaleVector`
00410770, `ClampUnit` 004022D0 (`__stdcall`, [-1, 1], NaN gives -1), `Vector3::AddInPlace` 00410750 (returns its argument),
`Quaternion::SetIdentity` 0040CB00 (returns this), `PreMultiply` 004018A0 (`*this = r * *this`), `AxisX/Y/Z` 0040D940 / 0040F560 /
0040D6B0, `SetAngleAxis` 0049E790, `SetAxisAngle` 0049E800, `PreMultiplyConjugate` 0049E870, `MultiplyByConjugate` 0049E950, `Slerp`
0049ED20, `CubicSegment::SetBSpline` 0040E3D0 / `Evaluate` 0040E440, `CubicCurve3::Evaluate` 0040E460, `CTransformObject` slots 13-17
(CopyTransform 00401490, CombineRotation 0040CB80, UpdateWorld 0040CC30, SaveTransform 00401510, LoadTransform 00401540), WorldMatrix
00401570 (4x4), TiltTowards 00401680 (down axis a tenth of the way to a target), TurnTowards 00401940 (forward axis onto a target),
RollTowards 00401B50, Rotate(axis, angle) 00401E50, TurnUpTowards 00401F00, SetHeading 00402220 (atan2 about +Z), `IsaacRandom::Next`
004667F0, `Refill` 0049EE70, `Init` 0049F150 (the generator is at 007A9D40), `Seed` 0049F660 (1 KB of a random file on drive C, xored with
`rand()`; A-S10), `FindSeedFile` 0049F4B0 (its recursive CFileFind walk).

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
  call their body share one template in `deleting_destructors_400.cpp`, `DeletingDestructor<body>` with the body
  rewrite as its argument (a member pointer or a __fastcall function).
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
  +0x5C hash_map of loaded objects (ours: `loaded_`, key -> object, plus `numbers_`, object -> its number by first save, while saving; the original shared one map), +0x70 list of objects to save; class tables 0079B284 (class id ->
  factory) and 0079B270 (vtable -> class id). A saved archive has three top-level chunks: 1 (scratch
  data, read back on load), 0 (index: class id and key per object), 2 (data: one child 1 per object with
  child 0 = key, child 1 = the object's fields). A loaded archive's Close gives every loaded object one
  reference and takes it back.
- `CoderState` (0x2828) and the LZ coder are in `network.md` (compression).

Rewritten (grep the address): `CMemoryStream` (a real class since C28e) constructor 0041DD70 / 0066C4F0, destructor 004173D0 / 004790A0
(two copies) and the deleting copies 004173B0 / 004790D0, Clear 00417350; `CStream` constructor 0047A200, destructor 004174E0 / 00417400; `CRefObject` / `RefValue` base
destructors and deleting destructors 00412720 / 00413430, 00412730, 00419720, 00412700; `AlignedPointerArrayAlloc` 0047B940 and
`AlignedObjectArray::Clear` 0047B620 (32-byte aligned blocks, header pointer before the data); `CoderState::Construct` 0042FCA0; the `Chunk`
family 0042BEC0-0042D0E0 (Construct, Destruct, Advance, File, Begin/EndWrite, WriteHeader, SkipContent, ReadHeader, ForEachChild, Read / Write
Float / Int / Byte / Vector3 / Quad / Quaternion / Bytes / Blob; ReadString 0042C580, ReadEncodedString 0042C650 (content decoded by 004D8E10),
WriteString 0042C820, ReadStringArray 0042CA20, WriteStringArray 0042CB80; RootChunk Open / its destructor); the `ChunkArchive` family
00409070-0040A5A0 (NextChild, FindChild, CountChildrenOf, CountChunks, Field, EnterChunk, LeaveChunk, AppendChild, ResizeToCurrent,
`Ref::Reset`, Read / Find / WriteStreamChunk, Open, Close, CStringField 004093A0, StringField 00409470, StreamField 00409640,
SaveObjectRef 0040A440, ObjectRef 0040A5A0).

- Recorded-file verify (chunks, FileStream, transform save / load): ours runs with the CFile's vtable swapped for a
  recorder, the original on a byte copy whose CFile replays the log; calls, bytes, chunk chains and outputs compared
  (over 256 calls / 256 KB skipped; checks do not nest). Memory streams are deep-copied only for the two CMemoryStream
  vtables; other stream classes run ours only.
- Kept: `ReadQuad` returns the file's values in the order 2, 0, 3, 1; `ReadVector3` copies x and z
  through the FPU (a signalling NaN becomes quiet; `FpuCopyBits`), y as an integer; `ForEachChild` reads
  the parent without a null check; `CMemoryStream`'s destructor leaves the freed pointer;
  `SaveObjectRef` registers the object (list and number) while loading too and then searches the field without using it (nothing calls it loading);
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

Rewritten (grep the address): `Update(dt)` 0040FB30 (listener, follow / track, pan, tilt, turn, zoom, limits, ApplyView, shakes;
the smooth camera option changes its drift, height lag and followed position: renderer.md section 14),
`ApplyView` 0040F8B0, `SampleTerrain` 0040D9B0 (`__stdcall`, 9 args: average / min / max surface height on a grid), `SetFocus` 0040F5C0,
`Pan` 0040F230, `StopMotion` 0040F430, `ZoomBy` 004107A0 (mouse wheel, 1.05^steps via `_CIpow`), `GroundPoint` 0040E810,
`SetTurnRate` / `SetTiltRate` / `SetZoomTarget` 0040F360 / 0040F3C0 / 0040F420, `SetTarget` / `SetTargetId` 0040D3B0 / 0040D420,
`SaveView` / `RestoreView` 00410860 / 004108D0 (F9-F12; a modifier saves), `SetDistancePitch` 00411470, `ViewsToSettings` /
`FromSettings` 00410D70 / 00410D10 (`_CIacos`), `SetHeading` 004109E0, `SetFree` / `LookFrom` / `SetBounds` 00410E50 / 00411060 /
00411360, `LookDirection` 004110D0, `AlignToTarget` 0040D710, `ClampAboveObjects` 0040E4C0, `Shake` 004113A0 (distance by `_hypot`, node
added to the list at +0x108), `PlaybackStart` / `PlaybackStep` 0040F1B0 / 0040EFB0 (camera\*.cam, PlayCamera), the constructor,
destructors, class name / id, Create / Destroy, Init, GetRange, Anchor in 0040C8B0 .. 0040FB20, OpenFile 0040E910 (`.\camera\` + name; an
existing camera.cam renamed first) / RecordFrame 0040EB60 / CloseFile 0040ECB0, LoadState / SaveState 00410AB0 / 00410C10 (slots 0073BA04 /
08; recording only with camera recording on), the recorded-path array allocation 00412120 (36-byte records, 32-byte aligned).

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
  rerun of `tools/find_dead_code.py` keeps these hand-added rows (C20k).

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
- STLport `vector`, `hash_map`, `list`, `map` (strings are std::string; stlp::string is gone): `src/lib/stlp.h`. `list<T>` is the sentinel pointer (nodes
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

## Strings: CString to std::string (C14a)

- The game's text moves to `std::string`. What CString did that std::string has no member for is in `src/lib/str.h`
  with CString's answers: `str::Find` / `ReverseFind` / `FindOneOf` (int positions, -1 when absent, the multibyte
  CRT's searches), `Mid` / `Left` / `Right` (a start or count outside the text clamped, never a throw), `Format` (the
  game CRT's vsprintf, `crt_format.cpp`: "1.#INF" and three-digit exponents, C26), `MakeLower`, `Remove`, the trims
  (multibyte stepping, `_ismbcspace`), `Buffer` / `CutAtNull` / `SetLength` for GetBuffer / ReleaseBuffer(-1) /
  GetBufferSetLength.
- `tools/c14_strings.py` does the rewrite: it retypes `mfc::CString` declarations and fixes what the verify build then
  reports, one rule per diagnostic shape (the rules: its docstring); a variable no rule carries stays a CString, and
  the next round starts from the original files without it (`build/c14_strings/excluded.json`, `manual.txt` for the
  rest). Batch 1 (scope `locals`): 550 of 845 local variables in src/game; the others stay CStrings because their
  address is taken (passed to a `mfc::CString*` or `void*` parameter, cast) or a non-const reference goes to a
  function taking `mfc::CString&` (they move with those signatures). The 49 the tool could still try after batch 1
  all stay for now (42 address or reference, the rest CFigureStrip::Draw's kept `name`).
- Scope `params` (batch 2): 65 `const mfc::CString&` parameters of functions defined in src/game are
  `const std::string&`, in the definition and every declaration of the name and position. Kept CStrings: virtual
  functions (an override retyped alone silently stops overriding), overload sets on the string type (CopyMember /
  AssignMember of record_members.h, the `Ops` cast views of console.h, text_reader.cpp, options.cpp, which
  static_cast the CString to a subclass, CharacterCount / CharacterBytes beside their string_view overloads), Sum and
  Compose, and `MapsRoot`, which returns its parameter by `const mfc::CString&` (a std::string parameter would
  return a temporary: C4172, a code-generator warning without a column). A CString given to a retyped parameter
  passes `.GetString()` (a std::string copy of its text; the game's text has no embedded zero bytes), the seam the
  members' retyping removes.
- CStrings built and destroyed by hand in place (the records' own Construct / Destruct, the element policies) are
  `std::construct_at(&field, ...)` / `std::destroy_at(&field)` (`c14_strings.py lifetimes`): the type comes from the
  field, so retyping a member changes them with it. A placement new of `mfc::CString` on a std::string compiles
  silently; the forms left are typed by an explicit cast (`reinterpret_cast<mfc::CString*>(p)->~CString()`,
  src/lib's element policies at byte offsets, `new (this)` in the CString view classes), the placement news on raw storage (a `void*` out parameter or a
  byte array: CStream::ReadString, WriteCopy, HumanFigureName, MobFile::Load's scripts, the interface save's text),
  and a member those reach
  (mfc_slots.h's `StringsAt` offsets, `DestroyStringsAt`) must stay a CString until they are typed.
- Scope `members` (batch 3): `--members Class::member ...` retypes those data members (an array member too) and
  keeps a member no rule carries by its name (`<member>` in excluded.json; `why.txt` says where and why). A member
  already declared std::string is counted, so its uses get the rules after a hand retype. Done: CUnit::scriptName,
  CMapObject::name2, CScript::errorText_, ScriptErrorInfo::text, PartyUnitRecord::texts[7], the unit database's
  prototype / NPC / model / model slot / level record strings, the item database's spell, spell modifier and material
  record strings and the item records' head (text04, text08, materialClass) and QuickItemRecord::text7C (batch 4;
  the kinds' CopyOwn / default constructors name their byte ranges by member now, and record_stats.cpp reads
  game.exe's weapon skill bonus at its original +0x7B). Their classes (and the classes deriving from or
  holding them: Lever, CTorchObject, MagicTrap, CTrapObject, FakeUnitScene, CWorldServer) lost the layout asserts the
  bigger member broke; the prototype index divides by sizeof(UnitPrototypeRecord). Helpers that take either text type:
  `ChunkArchive::CStringField<Text>`, `CStream::ReadInto<Text>` (the CString instantiations are game.exe's), records::
  CopyMember / AssignMember / DefaultMember for std::string, `InlineStrings<Text>` (unit_db.cpp; a vector of either).
  Batch 5: the 16 GameOptions strings (the loader and writer go by member pointers now: `int` / `float GameOptions::*`
  per registry value, the camera motions and shortcuts by their structs' members; the `field` offsets went),
  QuestItemRecord::zones (`stlp::vector<std::string>`), CSoundManager's soundPath / streamName / nextName / midiName /
  midiNextName, MusicTheme's name / briefing / constructor (and sound_music.cpp's LocalTheme beside it; the theme
  array's `ThemeElements::Relocate` moves the names) and CMusicSystem's credits / mainMenu / combatTrack / jingle.
  record_stats.cpp reads game.exe's difficulty at its original options +0x250.
  world.h: GameFlow's requestTarget_ / zone7C_, CWorldServer's zoneName_ / mobName_ / mapName_, CWorldClient's
  zoneName_ / texturePath_ and window names, MpHeroRecord::file, SavedParties::folder_.
  map_spec.h: the zone records' Spec::name / comment, ObjectSpec's model / texture / primaryTexture / parentIname /
  skinTexture, FlameSpec::sound, UnitSpec::prototype, MagicTrapRecord::spell (the spec readers' ReadInto take
  std::string&).
  Batch 6, the interface: ui.h (CInterface3D's messageText_ / savedText_, HudPortraitSlot's face and model texts,
  CHudUnitPanel::strings_, CHudTopPanel::pageTexts_, UiWordToken::text_, SaveEntry's four texts, CSaveLoadWindow::name_,
  OptionSetting::text_, OptionBinding's two, CObjectivesWindow's zone names, CUiEditField::text_, CMessageBox's and
  CHelpWindow's title / text, ChatLine's two, CChatOverlay::typed_), ui_screens.h (MpVoiceEntry, MpAddressEntry,
  CMpHeroScreen::text_, CMpConnectScreen::hostPassword_, TitleLine, CCreditsScreen::name_) and ui_camp.h (CGlobalMap's
  three, GlobalMapZone, GlobalMapPlace, TradeRow, CHeroStatsPanel::title_). The forwarding accessors that returned
  `mfc::CString&` (HudPortraitSlot's FaceName / ModelPrefix / ModelSuffix, CHudTopPanel::PageText, CUiEditField::Text)
  went; CBattleScreen::UnitName fills a `std::string*`. The global map's links and quests are `GlobalMapPlaces`
  (typed element policy).
  object_tables.h: AllodRecord's names, ZoneExit::target and MapRecord's eight strings. The map database's arrays are
  typed first: `CArray<AllodRecord, AllodRecordElements>` / `CArray<MapRecord, MapRecordElements>` (game_arrays.cpp;
  zero fill, the strings, the inner array's table), grown by CArray::SetSize (map_database.cpp's untyped SetSize over
  `ElementFn`s went); ExitArrayClass's destructor is gamearrays::Exits' (the `ExitTargets` byte-offset policy went),
  AllodArrayClass moved to game_arrays.h over the typed records, and the instantiations' registrations (0x004606A0,
  0x00460720, 0x004606F0, 0x0045FBD0, 0x004600F0, 0x0045FCB0) name the typed functions.
  Batch 7: the skill database's SkillRecord / SkillLevelRecord names (tests/vectors/record_stats.cpp reads game.exe's
  skill database through its own `OriginalSkillRecord` / `OriginalSkillLevelRecord` / `OriginalSkillDatabase`, 0x18 /
  0x44-byte records; replay_stats.cpp maps IndexOfLevel's recorded byte offset from game.exe's 0x44-byte records to
  ours), CSysInfo's five, CIniFile::path_, CParty::zone_, CCamera::fileName_, CEffectArrow::soundName_,
  CEffectSoundSeq::name_, CTorchObjectSpec::name_, CSpellContainer::name_, the briefing's BriefingSpeaker /
  BriefingShownItem names and CBriefingScreen's topic_ / dialogueText_, Terrain's textF8 / textFC / textAE0,
  PartyRecord::name, NamedPartyRecords' text0C / text10, PartyManagerEntry's two, CMapObjectServer's name78 / name7C,
  AICallRecord::text (AICallElements relocates by MoveElements), ConsoleStreambuf::line_, ConsoleWindow::input_,
  the zone table's ZoneTableEntry first / second, ItemHoverInfo::text and CObject3DClientSpecific's skinName /
  secondSkinName, CAnimationNode's name_ and spare names and FigureBone's three names (the figure factory allocates
  by sizeof; the classes' own sizes stay asserted in their headers). FigureBone was read through the
  `CageInterpolator` overlay (`unused_00[0x5C]`, then the point and t / u / w: the bone's offset and morph
  coefficients); with the bigger names it wrote the cage point over the bone's parent pointer (a crash in
  FigureBone::Place in every scenario with a figure). The interpolations are FigureBone's members now.
  Batch 8: the script's strings (ScriptToken::text, ScriptDefine's name / value, ScriptDefinition's name / params,
  ScriptFxSource::name, ScriptQuestObjective::text). The recorders read game.exe's tokens, defines and definitions
  through tests/vectors/script_original.h's `OriginalScriptToken` / `OriginalScriptDefine` /
  `OriginalScriptDefinition` (CString layout; `OriginalDefinitions(script)` views CScript::definitions_), and
  script_load_vectors.h's PutDefinitionsAfter takes either definition type (recording: game.exe's, replay: ours).
  The scene's names (SceneMesh / SceneNode name and parent / FaceGroup / SceneSkin), CMaterial's name / name2 /
  name3 and CFigureMesh's name_ / directory_ after the fake unit's arrays were typed (record_arrays.h's
  FigureParts / SceneMeshes / Materials destroy by element type; the byte-stride `mfc::impl::FigureParts` /
  `SceneRecords` / `FakeUnitRecords9C` and the mesh:: / node:: offset constants went). CObject3D::name (so every
  map object grows; CMapObject's offset asserts went), CTexture::name_ (the `Name()` accessor that handed out a
  `mfc::CString*` went: callers assign name_), TextTokenizer::word (the unused `TokenWord` overlay went) and the
  lever records' four strings.
  Batch 9: TextureNameRecord::name. The CArray tables (compiled tags) of string records are typed tags over their
  records next to them: scene::TextureNameArrayClass, MemoryRecordArrayClass (the AI's heard calls, unit_ai.h),
  GlobalMapZoneArrayClass / GlobalMapPointArrayClass / GlobalMapPathArrayClass (ui_camp.h); their slots,
  SetSize / ConstructElements and element destructors are registered on the typed records. mfc_slots.h's
  byte-offset string policies (StringsAt, BuiltStringsAt, NamedStrings, InitStringsAt, DestroyStringsAt) and
  mfc_array.h's WithString went. The FX source list: batch 8 made ScriptFxSource bigger, but CreateFXSource and
  ScriptFxSourcesLoad still built nodes through the lib's 0x34-byte FxSourceList (CPlex blocks of the old node
  size; the new name's bytes written as a CString); they use gameslots::FxSourceNodes now, and `ListAs` / `NodeAs`
  static_assert equal node sizes (ArrayAs already did). LocalPlayerName (00578A90) is `std::string LocalPlayerZone()`
  (it is the party's zone; `BriefingPlayerName` went). The text manager's out parameters are `std::string*`:
  ReadLine, ReadToHash, PeekWord, GetString, GetItemTexts, FormatItemName, text::ReplaceChar, and their callers'
  locals (c14_strings.py --string-out), CHudQuickItems::ItemTip, the trade panel's PerkBaseName, the options'
  ReadTip; Split collects into a std::string word.
  Batch 10: ScriptStrings (the variables' names and string values, the commands' string arguments) is a
  `mfc::CArray<std::string>`: mfc.h's `PlainElements<std::string>` builds, destroys and relocates the elements (the
  CString one's counterpart), and `mfc::impl::Strings` (StringArrayClass, ScriptStringValuesClass) is its ABI. The
  script's hand-inlined SetSizes of string arrays (`kStrings`, LoadFunction's InlineSetSize, addon's
  ArgumentsSetSize) are CArray::SetSize or element-type generic; the GetAt-by-value temporaries (`TempString`) went:
  the copies assign. ScriptStringLoad, CopyTo, CopyAt, CalcString's result, ScriptQuestAddObjective's text take
  std::strings. The recorders read game.exe's through script_original.h's `OriginalScriptVars`,
  `OriginalSSCCmd` / `OriginalScriptSubCall`, `OriginalScriptDecl` (`OriginalBlocks`, `OriginalGlobals`);
  script_load_vectors.h's PutVars / PutCmd / PutCmds / PutBlocksAfter are templates over either side's types. The
  harness digest reads the strings through `Text()`. Also: text::Join fills a std::string, Split cuts a std::string,
  `CWorldClient::ObjectVariableName` returns the name. The first CStringArray members are
  `std::vector<std::string>`: the HUD tooltips' `labels_` (CHudWeaponBar, CHudQuickItems, CHudSpellColumn,
  CHudActionColumn; the classes are allocated by `new`, so their sizes follow) and COptionsWindow::groupNames_
  (the layout asserts after them went).
  Batch 11: the ui's CStringArray members are `std::vector<std::string>`: CUiTooltip::lines_ (filled by
  `text::SplitStrings`, Split into a vector), CHudUnitPanel's texts_ / partLabels_, CUiSpin::texts_,
  SaveEntry::stringArrays_, CObjectivesWindow's quests_ / objectiveTexts_, CHelpWindow::lines_, CChatWindow::texts_,
  CTradePanel / CHeroStatsPanel / CSpellMakerPage::texts_, CCampPages::pageTitles_, CMpConnectScreen's allodNames_ /
  texts_, CItemInfoPanel's item and skill texts (the layout asserts and the padding bytes after them went). Chunk::
  ReadStringArray / WriteStringArray take a vector: ObjectSpec::parts, SoundSpec::sounds, UnitRecord::lists and
  ZoneDiplomacy::names (its constructor is the old DiplomacyConstruct; MobFile::Load holds it as a local). The sound
  names: CEffectSoundSeq / CEffectAmbientSound::names_, ReadSavedStrings and `SoundNameArray` (SoundEmitter /
  SoundSequence names, MusicTheme's calm / combat, AddEmitter / AddSequence); CSoundManager's constructor and
  destructor build the emitters and sequences (their Construct / Destruct functions went). CStringVector went: the
  unit database's and the quest briefings' name lists are `stlp::vector<std::string>` (`ChunkArchive::
  SerializeStrings`; `records::ElementOf<std::string>` is ObjectElement, so Memberwise copies them as strings).
  tests/vectors/record_stats.cpp's OriginalSkillRecord keeps game.exe's `stlp::vector<mfc::CString>`; replay_stats.cpp's
  stand-in skill records point their stat name vectors at std::strings. PartyForScript takes a `std::string*`.
  Batch 12: the map objects' part names are `std::vector<std::string>`: CMapObjectServer::strings (the companion
  takes ObjectSpec::parts by assignment), CObject3D::partNames, LoadParts / MakeFigure / CollectBoneNames and the
  MakeFigure callers' locals (the network's new objects, the party units, the UI model, the arrow and the moshka); the
  footprint reads the names directly (its by-value StringArrayGetAt went). Also SceneSkin::names, ParseMaterial's
  parts, CWorldServer::mobFiles_, the item records' list20 / list50 (nothing fills them), CBriefingScreen::topics_
  (`TopicCount()`) and SkinRequest::textures (CTextureManager::DrawLayers takes the vector; the requests are plain
  locals, `SkinRequest request{}`, not raw storage destroyed by hand), WeaponModelParts' out parameter. CBriefingScreen and SkinRequest lost the
  layout asserts after the new members.
  Batch 13: the list out parameters are `std::vector<std::string>*`: CFileRegistry::GetSectionNames / GetStringArray,
  CMappedResource::ListFiles / ListFilesPlain and modfolders::ListFiles, CIniFile::GetList / GetKeyNames (ini_file.cpp's
  TextOps view went), TextSources / CTextManager::ListKeys, CSoundManager::ListWaveFiles, the item and spell
  List*Names, FindListedName's list, and their callers' locals (the AI map's terrain sections and map name, the
  figure registry's sections, the music themes' sections and calm / combat lists, the quest places and briefing lists,
  the terrain's map list, the person texts' keys, the sound counts' files, the class templates (their by-pointer
  List* wrappers went), the camp debug fill, the light tables' keys and hour, the movie list in PlayMovies, whose
  path is a std::string now). The clears and appends keep the CStringArray calls' order: ListFilesPlain empties its
  out parameter, modfolders::ListFiles and GetList append to it, so with Rebirth's ResOverride a reused scratch
  vector (ListWaveFiles, TextSources::ListKeys) grows over the archives as game.exe's array did. `str::TrimLeft(s,
  char)` is CString::TrimLeft(TCHAR) (the quest places' braces).
- stlp::string is std::string in src/game (the stlp batch beside batch 12, 491 -> 0 uses outside src/lib): the members, locals,
  parameters, the vectors (`stlp::vector<std::string>`, policy `stlp::ObjectElement<std::string>`), the trees and hash
  maps keyed by name (stlp_hash.h's `StringKeyTraits` / `StringListTraits` / `NamedSlot` and `stlp::StringHash` hash a
  std::string the same way: `h = 5 * h + c` over signed chars, so bucket and iteration order are kept; trees order by
  `operator<`, unsigned bytes like STLport's memcmp). Out parameters that built a string in raw storage return it now
  (`NetAddress::Format` / `ToString`, `CLinkMesh::CopyName`). The bridge batch removed the rest: stlp::string with
  StringElement, StringAbi and stlp_string.cpp, stlp_pair.h's StringDwordPair / StringPair, StringHash's stlp::string
  overload, and STLport's out_of_range (NetAddress::Parse's substr check could not fire: the colon is inside the text;
  game.exe's out_of_range tables are retired with file `-`). The records that grew lost
  their size asserts; every node or block size that was a literal now is a sizeof (the animation caches' and the figure manager's
  hash nodes `deallocate(n, 20)`, the blood prints' list nodes 164, the text sources' nodes 0x18, the string vectors'
  `capacity * 12`): a literal size with a string-holding element frees into the wrong pool list.
- Members that must stay CStrings for now: the console's (console::CStringArray history, lines and command arguments),
  Split / SplitWords / Join's CStringArrays and the locals their callers fill (the quest places' `parts`).
  To read game.exe's objects where our layout differs, a recorder declares game.exe's layout itself
  (record_stats.cpp's `OriginalSkill*`, script_original.h), never our header with a sizeof stride. Record such a
  change once (`EI_VECTORS=1`, `--no-hooks --vanilla-only` on scenarios that reach it, `vectors.py merge <runs>
  --out <scratch>`, `ei_vectors.exe <scratch>`): tests/vectors is not in the original runs' key without
  EI_VECTORS, so the plain original suite only replays its cache. Batch 8 did it for stats and script (14
  scenarios: 0 problems); the path family's raw recording does not replay without `tools/path_vectors.py prune`
  (its @AIMap records are sampled: searches on maps not kept crash), so check it only through that procedure.
  Replay a recorded script family together with tests/vectors/data/crt (the game CRT's character classes the
  lexer reads): without that folder ei_vectors loops forever at full CPU (batch 10). Replay a scratch folder
  with a timeout.
- `mfc::CArray` moves elements with `Relocate`: bytes, or the element policy's `Relocate` when it has one. An element
  holding a std::string needs one (PartyUnitRecordElements: move-construct, destroy, in the direction the overlap
  needs; `mfc::MoveElements` does it for any element type): bytes are fine for MSVC's std::string at
  _ITERATOR_DEBUG_LEVEL 0, but the /MTd build's level 2 keeps a proxy pointing back at the string.
- The harness digest reads our std::string members through `Text(const std::string&)` in
  tests/verify/image/harness_digest_image.cpp (the shared .inc reads game.exe's CStrings as const char*).
- A `const char*` given to a retyped parameter builds the std::string from it: CString took a null pointer as ""
  where std::string's constructor does not. No caller in the suite passes null; a crash there is the first place
  to look. The same for a member assigned a `const char*`: ServerSession::LeaveZone calls
  `GameFlow::SetNetworkZone(nullptr)`, which crashed net_pair_leave once zone7C_ was a std::string (it stores "" for
  null now). The credits' script tokenizer gave null past a line's last word (`WordOrNull`), which TitleLine's
  style / text took as "" while they were CStrings (now `WordOrEmpty`; every titles run crashed in between).
  `CObject3DClientSpecific::SetSkins(first, nullptr)` (ApplyMaterials with the second skin "none") stores "" for a
  null name now.
- While both types exist, `mfc.h` bridges them: `CString(const std::string&)` and `operator+` of a CString and a
  std::string (the std::string side's type). A `const mfc::CString&` parameter given a std::string binds a temporary
  copy, which is the same text. The rewrite does not retype a variable whose address is taken: a `std::string*`
  compiles silently through a cast or a `void*` parameter and is then read as a CString
  (`reinterpret_cast<StringOps*>(&text)` in Tokenize, caught by the script-load vectors; `ComposeSkin(1, &name)`'s
  `void*` target, a crash in every scenario with a hero).
- A retyped member makes its class bigger, so everything that knew the old size or offsets breaks without a compile
  error once the layout asserts are gone. Before retyping, grep for the class's old sizeof and the offsets after the
  member: `CMapObjectManager::NewObject`'s factory allocated 0x4DC / 0x238 / ... bytes for each class id (now
  sizeof(T); heap corruption C0000374 about a second into every run once CUnit and CMapObject grew), and
  `FigurePrototypeRecord` read a model record's figure name as a CString at +0xE8 (the multiplayer HUD portraits;
  now the UnitModelRecord itself). Structs that pad up to an offset (`unused_000[0xE8]`) over another class are the
  same hazard: `CameraSettings` (`unused_000[0xFC]`) read the options' camera block through
  `reinterpret_cast<CameraSettings*>(options)` (camera distance 0 and the random draws off in every scenario once
  GameOptions grew); CCamera reads the GameOptions members now. The client world allocated CMusicSystem as 0x58 bytes
  (now sizeof). Grep `reinterpret_cast<...*>(` of the object's pointer too, not only its sizes. Element counts taken
  from a byte difference are the same hazard: the hero screen counted its voices as `(end - begin bytes) >> 3`
  (MpVoiceEntry was 8 bytes; a crash in DrawVoices in every multiplayer hero scenario once it grew). Grep `>> 3`,
  `/ 0x..` and `reinterpret_cast<char*>(...end())` near the class's containers.
- Kept on purpose: a pointer into a CString that another CString shares (refcounted buffers) outlives the local copy;
  with std::string it would not. `CFigureStrip::Draw`'s `name` (its cells keep `name`'s pointer) stays a CString.
- A function returning `const mfc::CString&` of a retyped member compiles silently and returns a reference to a
  temporary CString built by the bridge constructor: world_game.cpp's `RecordMob` / `RecordMap` (forwarders of
  MapRecord's members) handed LoadZoneScripts a dead path (CFileException in every zone with scripts). They went;
  before retyping, grep `mfc::CString& Name(...) { return ...member; }` for the members.
- c14_strings.py has no rule for an overload set that took the CString through its conversion (`gamecrt::mbscmp`'s
  C2665): it keeps the member. Retype such a member by hand and add the `.c_str()`s, then run the tool with it (a
  member already declared std::string gets the rules).
- A class held by value grows with its member: CParty (zone_) sits in CPlayer, ClassTemplate, PartyRecord,
  CItemInfoPanel, ClientSession, TradeRecord and MpHeroRecord (all through `lib::InPlace<CParty>`, so their
  sizes follow), and their layout asserts after it went too. Grep the class's embedders (`InPlace<T>`, members of
  type T) as well as its own sizes.
- c14_strings.py's locals scope skips a variable whose address is taken (`&x`); `--string-out F G ...` names
  functions whose pointer parameters are typed `std::string*`, so `&x` handed to one of them does not keep `x` (a
  CString* there is a compile error, which the rounds then fix by keeping the variable). Retype the function by
  hand first, then run the scope on its callers' files with it listed.
- c14_strings.py keys a kept member by its name and remembers it in build/c14_strings/excluded.json across runs: a
  later run with another `X::name_` keeps that one too ("1 excluded"). Run with `--fresh` after retyping the kept one
  by hand. It has no rule for a std::string streamed into the console's ostream (`<<`, C2679: add `.c_str()`) or for
  the console.h `Ops(x)` cast views (retype by hand: `str::Mid(x, ...)`, `x += c`), and a `const mfc::CString& r =
  member;` local binds a bridge temporary silently (harness_utf8_image.cpp read the console input so).
- c14_strings.py sometimes applies a `.c_str()` fix one line late (a one-line function after one already fixed:
  manual.txt says "already converted"; the next line gets a stray `.c_str()`, e.g. `return "CFigure".c_str();`
  under `GetName() { return name_; }`, which stays unfixed). Read the diff of every run. A failing static_assert in
  a header stops that translation unit, so the diagnostics behind it only show in the next run: drop the broken
  layout asserts, then run the tool again with the same members.
- The tool keys a kept member by its bare name across classes: one `name` it cannot carry (an address handed to a
  `mfc::CString*`, e.g. world_units.cpp's NameString or ScriptStringLoad) keeps every listed `X::name`. Fix that
  site by hand (assign the member, a local loader helper), clear build/c14_strings/excluded.json and run again.
- ei_vectors links only some of src/game (CMakeLists.txt's ei_vectors sources): a replayed function that calls a
  helper defined elsewhere (CStream::ReadInto in exit_area.cpp) fails to link there; use a local helper.
- Checked by: the build (every changed call shape is a compile error or a /W4 warning first), the function vectors
  (`CScript::Load` / Tokenize / the script copies replay the loader's string handling), both suites with goldens.

## Open items

- Library instantiations (config/library.csv, ~270 in 0x400000-0x440000) are ported (src/lib, src/game's game_* files);
  C14a-d replace them with standard types.
- Static initialisers run before the hooks install (004632A0 the lever table constructor, 0045E820): a port of one is
  compared at WinMain's entry instead (start-up checks; 00444740, 00458870 and others, app.md "A-E2"). `WinMain`
  (00456440) itself can be hooked and is ported (app.md).

## A-E1: text helpers and the object id map

- text_helpers.cpp. `TextTokenizer` (00445D20 `Start`, 00445D30 `Next`): splits a CString at a separator
  string; `Next` returns the word's text and moves past the separator, the last word runs to the end of the
  text, and a position at the end gives null. `RemoveCarriageReturns` (004461E0, CString::Remove of 0x0D).
  `FindListedName` (00447F50): the entry of a CStringArray that equals a name ignoring case (`_mbsicmp`), else
  the name; the name comes by value and is destroyed by the callee, which a C++ by-value `mfc::CString`
  parameter reproduces. `mfc::CString` gained `Mid(first, count)` (006FD9D7) and `Remove(ch)` (006FD963).
  Checks: `Next` and `FindListedName` side by side by content; `Start` against the original `Next` on a rewound
  copy (its own original would enter `Next`'s wrapper); `RemoveCarriageReturns` ours only. The credits screen
  (0062B7D0) is the only user of the first three and the scene registry of the fourth; no scenario reaches them.
- `ObjectIdMap::ReleaseAll` (0047B670, object_id_map.cpp): every object released up to the first empty entry,
  the nodes freed, the count and maxId cleared (the buckets stay). The world's object map (map close) and the
  player's two maps of sent objects call it through `ObjectMapClearOriginal`. Ours only (objects can go with
  their last reference); the map must end empty.
- A-E1: `CCamera::LoadPath` (0040ECD0, RET 8, camera_path_load.cpp; Rebirth-patched at its file open
  0040ED42, so the vanilla launch only): `<data folder>camera\<name>` opened with a CFile (0 when that fails),
  the keys resized to length / 36 as the engine's aligned arrays are (+0x2F0..+0x2FC are an
  `AlignedArray<CameraKey>`: data, size, capacity, grow step) and filled with one read. With the second
  argument above 1 and below the key count, the first key among that many that is more than 500 time units
  after its predecessor gives its position and rotation to every key before it. Side by side on the camera
  itself (the original loads, the keys are copied, ours loads again; results, counts and key bytes compared).
  Reached when a recorded path is played (the glue 00578820 / 00578880 calls it through its address).
- A-E1: constructors of globals that run from static initialisers, before the hooks install (their hooks never
  fire; written so the standalone build has them): `CMapDatabase::Construct` (0045E820, the database at
  0079BF00: two empty CArrays with the vtables 0073C3DC / 0073C3E8), the `LeverRecordTable` constructor (004632A0,
  the lever table at 0079C1A0: no records, an index of 193 buckets) and `PartyManager::Construct` (004A5870,
  the party manager at 007AA5A0: a party map of 193 buckets, no entries, records or templates, two empty
  strings; +0x20 is not written). The unused functor bytes the originals copy from uninitialised stack bytes
  are not written. Ours-only wrappers in verify_static_constructors.cpp. 00444740 (the debug
  info panel at 0079B930) and 00458870 were ported by A-E2 (app.md).
