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
  WSOCK32 by ordinal. `2DintMMX.dll` and `3DfpFPU.dll` (VC6, 2000) are loaded by name; the rebuild loads neither
  (R28, R31: their work is plain C++ or the GPU's).

## Maths and transforms

File: `core_maths.cpp`, `core_transform.cpp`, `core_transform_math.cpp`, `quaternion.cpp`,
`random.cpp`. Verify: `verify_core_maths`, `verify_core_transform`, `verify_quaternion`,
`verify_random_init`.

Rewritten (grep the address): `AddVectors` 0040CDC0, `NegateVector` 00402200, `ScaleVector`
00410770, `ClampUnit` 004022D0 ([-1, 1], NaN gives -1), `Vector3::AddInPlace` 00410750 (returns its argument),
`Quaternion::SetIdentity` 0040CB00 (returns this), `PreMultiply` 004018A0 (`*this = r * *this`), `AxisX/Y/Z` 0040D940 / 0040F560 /
0040D6B0, `SetAngleAxis` 0049E790, `SetAxisAngle` 0049E800, `PreMultiplyConjugate` 0049E870, `MultiplyByConjugate` 0049E950, `Slerp`
0049ED20, `CubicSegment::SetBSpline` 0040E3D0 / `Evaluate` 0040E440 (`CubicCurve3::Evaluate` 0040E460 and its struct: no port, nothing calls them), `TransformObject` slots 13-17
(CopyTransform 00401490, CombineRotation 0040CB80, UpdateWorld 0040CC30, SaveTransform 00401510, LoadTransform 00401540), WorldMatrix
00401570 (4x4), TiltTowards 00401680 (down axis a tenth of the way to a target), TurnTowards 00401940 (forward axis onto a target),
RollTowards 00401B50, Rotate(axis, angle) 00401E50, TurnUpTowards 00401F00, SetHeading 00402220 (atan2 about +Z), `IsaacRandom::Next`
004667F0, `Refill` 0049EE70, `Init` 0049F150 (the generator is at 007A9D40), `Seed` 0049F660 (1 KB of a random file on drive C, xored with
`rand()`; A-S10), `FindSeedFile` 0049F4B0 (its recursive CFileFind walk).

The client effects' generator `g_clientRandom` (random.h, R34a) is ours: a second ISAAC that `CWorld::Create` seeds right after
the game's (`InitFrom`: Init with the game's memory words as the seed, no file, no `rand()`), drawn by every effect whose result
only reaches the picture or the sound (`ClientRand()` for the ones that drew `rand()`; the client's idle animation picks
through `AnimationRandom::Client`, R34c, the server's keep `rand()`). The list of the moved sites and how they
were found: standalone-verification.md section 5 and harness_client_effects.cpp (the original's functions, patched alike).

- `TransformObject` (vtable 0073BA38, abstract, 0x54 bytes): EngineObject + local position (+0x1C),
  world position (+0x28), local rotation (+0x34), world rotation (+0x44). Slots 13-17 are the five
  methods above, shared by ~90 vtables; slot 14 is overridden by 00516370 in the unit classes
  (writes the world rotation only). Slots 16 / 17 take a `lib::File*` and Write / Read it (the original: an MFC `CFile*`, its slots 14 / 13).
- The camera's quaternion products in `ApplyView` and `Update` add the terms in different orders; each is written out separately.
- Float rules: plain C++ (rule 7). `SetBSpline`'s 1/6 factor was distributed by `/fp:fast` and gave
  different results at 24 bit, so that function keeps `float_control(precise, on)`. Values the
  original rounds through a float temporary go through `StoreFloat`.

## Reference counting

File `core_refcount.cpp`, `core_refcount.h`.

- `RefCounted` (vtable 0073BB68: 16-bit count at +4, copy-on-share flag at +6 bit 0, detached flag at
  +7 bit 7, slot 0 deleting destructor, slot 1 Serialize). `RefObject` (0073BB58) derives from it
  and adds slot 2 shutdown, slot 3 clone. The network sessions derive from the root because their
  slot 2 is RemoveClient. The original's `RefPtr` is the one-dword owning pointer: ours is a `Ref<T>` with the
  owner's operations as free functions (`DestroyOwned`, `ShareOwned`, `ShareAssignOwned`) and `SharedRef<T>` as
  the container element that owns its share.
- Rewritten: 0040A3A0 `RefObject::Release`, 00417DF0 `Shutdown`, 00419CD0 `RefPtr::Assign`, 00418BE0
  `Share`, 00413270 `Destroy` (dry run on stand-in objects whose vtable records the destructor /
  shutdown / clone calls: headers, call records, return value and pointer compared).
- The compiler emitted ~100 out-of-line copies of these helpers all over 0x400000-0x480000 (21 of
  `Destroy`, 21 of the 28-byte release, 9 `RefPtrShareReturn`, ...): ours are the templates' inline code. The
  scalar deleting destructors that only call their body (the counted pointer holders, the sessions, the network
  messages) are their classes' C++ virtual destructors.
- Differences: our release tests the pointer for null after the shutdown (the original's helpers use it first; same
  behaviour); the original's `Destroy` leaves the pointer set, `DestroyOwned` empties the Ref (every caller set it
  again with Share or dropped it). A Ref's `Assign` does nothing when it holds the object already (the original's
  added and dropped a reference: the same count).

### Counted pointers

- Every counted class has `AddRef()` and `Release()`: EngineObject (int refCount; at 0 the owner's ReleaseObject),
  RefCounted (int16 refs; deleted at 0), LinkMeshBase, LinkFigureLoader, DeviceMaterial, RenderPreset,
  RenderHelper and RendererBase (deleted at 0). A site that keeps a raw pointer counts with these, not `++refCount`.
  The original's drops without the release at zero (sic: the object-list iterators' manager, the constructor's handed-back
  item, the figure's parts) are `EngineObject::ForgetReference()`.
- A counted field is a `Ref<T>` (src/game/ref.h): `Assign(p)` takes p's reference first, then drops the old one (nothing
  when p is held already); `Reset()` nulls the field, then drops the reference; `Take()` hands the reference on. There
  is no store of a raw pointer, so the compiler lists every hand-counted store. A Ref starts null and its destructor
  drops the reference: an owner's destructor resets a Ref by hand only where the release must come before other work
  or in an order the members' reverse declaration order does not give (the original's member destructors released
  in that order too). Every owner that holds a Ref needs T complete where its constructor or destructor is defined
  (the constructor's unwinding destroys the members built so far): include T's header there.
- A container element that holds its reference is a `HeldRef<T>`: a Ref that can be copied (taking a reference) and
  moved.
- Ids: `SaveRef(object, stream)` writes the id (-1: none), `LoadRef(ref, stream, world)` looks it up in the world's map
  objects (engine_object.h, counted_ref.cpp).
- Shared objects (core_refcount.h): a field that owns its share of a RefObject is a `Ref<T>` released with
  `DestroyOwned` (a still-shared object is detached and shut down first, then the Ref emptied) and set with `ShareOwned` /
  `ShareAssignOwned` (a clone of a copy-on-share object; the object held is marked copy-on-share); a container element
  that owns its share is a `SharedRef<T>` (copy: share, destructor: DestroyOwned). The network messages' plain counted
  fields are `Ref<T>` (the copy constructors Assign), their plain lists `HeldRef<T>`. The AI brain's `modes` are a
  `std::vector<std::unique_ptr<AIMode>>` (AddMode and Load append the new mode; `current` points at a mode object,
  never into the vector).
- Item's `spell` (Ref<CSpell>) and `link` (Ref<RefObject>: the server's ItemLink or the interface's ItemTurn) are
  Refs. CObject3D's `parts` are a `std::vector<HeldRef<FigureMesh>>` whose references the original never releases
  through the vector: CopyFigure copies the pointers uncounted (the old ones taken without a release, each copy's
  reference forgotten again), Destroy3D takes each and forgets its reference, RemovePart takes the element and releases
  it after the detach.
- The game client's `info`, `binding`, `map` and `trade` are plain `Ref<T>` (the handlers Assign / Reset; the
  destructor resets them in the original's order between the lists' clears).
- Not merged: `ScriptEffect<T>` (its reset removes the timed object first).
- Counted lists outside the interface: the world's missiles, a CObject3D's effect sources and the joined links
  (TakeNew) are lists of `HeldRef`, the AI memory's four unit lists (`HeldUnits`: nearby, visible, seen corpses,
  corpses) vectors of them; a removal that must release after a later step moves the element out first
  (RemoveEffectSource detaches before the release). The candidate and party lists the AI builds on the stack hold no
  references (`UnitList`, a vector), nor does SectorMap::FindUnitsInRadius's result.

## Object manager (EngineObject, BaseManager)

File `engine_object.cpp`, `base_manager.cpp`, `hash_tables_400.cpp`. Verify `verify_base_manager`.

- `BaseManager` (vtable 0073B870, 25 slots) layout: +0x1C objects (STLport hash_map id -> object,
  highest id at +0x30), +0x34 MFC CPtrList of registered objects, +0x50 id -> list position, +0x68 type
  -> CPtrList* of spare objects, +0x80 type -> spare count (each table is followed by its highest
  key), +0x98 / +0x9C id range [low, limit) for new ids, +0xA0 / +0xA4 (-1 constructed, 0 / 300 after
  Create), +0xA8 last id, +0xAC context (Init / SetContext). Slots: 2 Create, 3 Destroy, 9 / 10
  Load / Save, 13 Init, 14 NewObject, 15 DestroyAll, 23 CreateObject, 24 ReleaseObject.
- Objects are never freed while the manager lives: RemoveObject turns them into spares of their type
  (id -1) and CreateObject takes a spare before asking slot 14. Ids are maxId + 1 while that fits the
  range, else `rand()` probing (100000 tries, then the fatal "BaseManager::RegisterInstant" and id -1).
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

- `ByteStream` (vtable 0073BDB0): +4 direction byte, +5 ok, +8 limit / save format version (2000000000
  when constructed). Slots 0 destructor, 1 Read, 2 Write, 3 Seek, 4 Size, 5 Position, 6 Clear (memory
  streams empty, file streams rewind), 7 CopyFrom (004A4D40).
- Typed values (C44): one value is `stream->ReadValue(x)` / `stream->WriteValue(x)` (sizeof(x) bytes, trivially copyable; also
  `lib::File`, and `ChunkArchive::Field(id, &x)`). A read or write whose size is not the variable's (a byte into an int, a pair of
  floats or a struct read as one block of 8 / 12 / 0x1C bytes, a dword of a wider record) stays `Read(&x, N)` / `Write(&x, N)` with the size
  written out: `grep -rE "(->|\.)(Read|Write)\(&"` lists them. Bytes in a raw buffer (file image, network cursor) are
  `lib::LoadBytes(x, src)` / `lib::StoreBytes(dst, x)` (lib/bytes.h; not for floats: x87::CopyBits).
- `MemoryStream` (0073BD90; the queue variant 0073C960): +0xC byte being assembled, +0x10 mask of the
  last bit (0x80: a new byte is due), +0x14 index of the byte the bits go to (-1: none), +0x1C
  capacity. Slots 1 Read 004A5380, 2 Write 004A53F0 (grows to twice the need), 3 Seek 004A5490 (also stores
  pending bits), 4 Size, 5 Position, 6 Clear 00417350, 7 CopyFrom 004A55D0. `Resize` 004A54D0 keeps the
  contents (1.5x + 0x10), `SetSize` 004A5570 is exact and drops them.
- `FileStream` (0073C8D8, 0x1C): a ByteStream over a `lib::File` (src/lib/file_io.h; the original: an MFC `CFile` at
  +0xC, each slot calling the CFile's virtual: 3 GetPosition, 8 Open, 10 Seek, 12 GetLength, 13 Read, 14 Write, 19
  Close). The save code builds these on the stack. Defined in `net_link.h`. Open takes MFC's CFile open flags
  (lib::File's names: `modeCreate | modeWrite` to save, `modeRead` to load).
- `Chunk` (vtable 0073BE94, 0x18: vtable, id, consumed, size, start, parent; not the models'
  `ChunkArchive`): slot 0 a handler called for every chunk `ForEachChild` reads (empty in the base),
  slot 1 the file (base asks its parent). Sizes include the 8-byte header; every byte read or
  written is counted in the chunk and all its parents. `RootChunk` (0073BE9C, the original's CFile at +0x18; the .mob
  loader uses vtable 0073FAD4) owns the file (a `lib::File`): Open 0042D160, constructor 0042D1C0, Destruct 0042D270.
  `ChunkArchive` is a different format over two memory streams (below).
- `ChunkArchive` (0x74, constructor 0053A7B0): +0 index stream, +0x28 data stream, +0x54 version (1),
  +0x5C hash_map of loaded objects (ours: `loaded`, key -> object, plus `numbers`, object -> its number by first save, while saving; the original shared one map), +0x70 list of objects to save; class tables 0079B284 (class id ->
  factory) and 0079B270 (vtable -> class id). A saved archive has three top-level chunks: 1 (scratch
  data, read back on load), 0 (index: class id and key per object), 2 (data: one child 1 per object with
  child 0 = key, child 1 = the object's fields). A loaded archive's Close gives every loaded object one
  reference and takes it back.
- `CoderState` (0x2828) and the LZ coder are in `network.md` (compression).

Rewritten (grep the address): `MemoryStream` (a real class since C28e) constructor 0041DD70 / 0066C4F0, destructor 004173D0 / 004790A0
(two copies) and the deleting copies 004173B0 / 004790D0, Clear 00417350; `ByteStream` constructor 0047A200, destructor 004174E0 / 00417400; `RefObject` / `RefValue` base
destructors and deleting destructors 00412720 / 00413430, 00412730, 00419720, 00412700; `AlignedPointerArrayAlloc` 0047B940 and
`AlignedObjectArray::Clear` 0047B620 (32-byte aligned blocks, header pointer before the data); CoderState's constructor 0042FCA0; the `Chunk`
family 0042BEC0-0042D0E0 (Construct, Destruct, Advance, File, Begin/EndWrite, WriteHeader, SkipContent, ReadHeader, ForEachChild, Read / Write
Float / Int / Byte / Vector3 / Quad / Quaternion / Bytes / Blob; ReadString 0042C580, ReadEncodedString 0042C650 (content decoded by 004D8E10),
WriteString 0042C820, ReadStringArray 0042CA20, WriteStringArray 0042CB80; RootChunk Open / its destructor); the `ChunkArchive` family
00409070-0040A5A0 (NextChild, FindChild, CountChildrenOf, CountChunks, Field, EnterChunk, LeaveChunk, AppendChild, ResizeToCurrent,
`Ref::Reset`, Read / Find / WriteStreamChunk, Open, Close, CStringField 004093A0, StringField 00409470, StreamField 00409640,
SaveObjectRef 0040A440, ObjectRef 0040A5A0).

- The saves (FileStream, the chunks, the transforms' files) are checked by the goldens' decoded saves and, for
  C14d's move to lib::File, byte for byte: every file the image suite's runs leave in harness_saves (the saves, info
  and shot files, the texture cache, keyboard.ini, tutorial.ini) the same as main's runs of the same scenario
  (the image's saves are byte-for-byte repeatable: two scenarios' autosaves at the same point are identical).
- Kept: `ReadQuad` returns the file's values in the order 2, 0, 3, 1 (`ReadVector3` returns the bits as read:
  the original's FPU copy of x and z would quiet a signalling NaN); `ForEachChild` reads
  the parent without a null check; `MemoryStream`'s destructor leaves the freed pointer;
  `SaveObjectRef` registers the object (list and number) while loading too and then searches the field without using it (nothing calls it loading);
  `ObjectRef` writes key 0 when saving; `LeaveChunk` (writing) takes the child's content pointer before
  `AppendChild` resizes the stream; a failed `EnterChunk` while loading leaves the chunk pushed.
- Not rewritten: 0042D260 (RootChunk::File). The memory stream's own slots, Resize / SetSize, the bit
  helpers and the sized / header helpers (004A4D40-004A5650) are in `core_memory_stream.cpp` (script.md,
  "Leftovers").

## Triangle clipping (0x0042D910-0x0042E6A0)

Not in the rebuild: the screen-space clipper (`ClipTriangle` 0042D910, the six plane clippers 0042DB70 (X below)
.. 0042E340 (Z above), `LerpVertex` 0042E4D0) clipped the crossing triangles of the mesh draws (until R30f) and of
the sprite draw (slot 17 of vtable 0073D93C, 00502CA0; until R31a). The GPU clips every draw now (renderer.md,
section 10, "R31a"). 0042E700 / 0042F4C0 / 0042F890 are dead polygon-clipping variants.

## Camera (0x0040C8B0-0x004114F0)

File `camera.cpp`, `camera_motion.cpp`, `camera_object.cpp`, header `camera.h`. Verify
`verify_camera*.cpp`, scenario `camera.txt` (walks the hero, pans with arrows, F9-F12, the two script
camera commands, then PlayCamera on `camera\bz3g.cam` until IsCameraPlaying is 0). The camera object
is at 0079B2A8 (flags at 0079B2B0; 007AF7C8 points to it for the camera-file player). The per-frame
call is `Camera::Update` from the game frame 00456440 (WinMain; Rebirth patches the caller).
`Camera` ("Camera", class id 0x43, vtable 0073B9E0) derives from `TransformObject`.

Layout is in `camera.h` (named members). Not obvious from it: flags 0x8 follow the target, 0x10 file open (recording), 0x20 playing, 0x40 free placement, 0x100
bounded, 0x200 briefing limits; the view stack at +0x1E0 is a deque (push 00410E70 / pop 00410EF0, no Ghidra functions); keys at +0x2F0 are 0x24 bytes (time, ?,
position, rotation); +0x2E0..+0x2E8 near / far / fov are read by `renderer_init`; pitch is kept in degrees (+0x104, settings +0x1D8 / +0x1DC).

Rewritten (grep the address): `Update(dt)` 0040FB30 (listener, follow / track, pan, tilt, turn, zoom, limits, ApplyView, shakes;
as the original's; the world's draw shows its pose less the unreached part of the last tick's step: renderer.md section 14),
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
never to the heading); `SampleTerrain` counts the centre sample once more than the grid (ours has no 2nd / 3rd argument; the original ignored them);
`StopMotion` multiplies the view axes by 0 (so (+-0, +-0, 0), NaN for NaN) and
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
(006FD9D7). Call sites passing `len - k` (scene_loader, scene_material, script_parse, item_manager, options,
spell_text, world_game) therefore use `Right`; camera_object's `title.Right(4)` is 0040E9A0 calling 006FDA6D with 4.
Views with their own `Mid(int)` (console::Str, ...) still call 006FD9B4. config/library.csv has them in this order.

## Shared containers

The game's MFC and STLport containers are standard types in our code: nothing of `src/lib` models them any more, and
`tools/check_cleanup.py` counts per-file `struct ...Array/...List` layouts (none remain). What the original's
instantiations were, for reading game.exe (config/library.csv names them):
- MFC `CArray` (0x14 bytes: vtable, data, size, maxSize, growBy; growth to max(newSize, maxSize + step), step = growBy or
  size / 8 clamped to 4..1024), `CList` (0x1C bytes: vtable, head, tail, count, free, CPlex blocks, blockSize),
  `CMap`, CByteArray / CWordArray / CDWordArray and CTypedPtrList: our members are `std::vector` / `std::list` /
  `std::map`. The engine's aligned arrays (data, size, capacity, growBy in 32-byte blocks, new slots left
  uninitialised) are `std::vector` too (the build has no SIMD, the alignment is not kept). `lib::FreeStorage(v)`
  (src/lib/containers.h) frees a vector's storage (`v = {}` / clear() keep it); `lib::SetAtGrow(v, i, x)` grows to i + 1.
- STLport `vector` (12 bytes, as std::vector), `list` (a sentinel pointer: 4 bytes where std::list is 8), the hashtable,
  the tree and the string: std types, or `lib::HashMap` / `lib::HashMultimap` where the walk order matters
  ("Containers" below). Layout asserts do not survive such a retype; archive formats are element-wise, so saves and
  messages keep their bytes.
- game.exe's own layouts of these exist only in tests: tests/verify/exe_objects.h's views (`Vector<T>`, `List<T>`,
  `HashTable`, `StlpList`, `exe::CString`) and the recorders' `Original*` records (record_stats.cpp's
  `OriginalSkill*`, script_original.h). A recorder declares game.exe's layout itself, never our header with a sizeof
  stride. Record such a change once (`EI_VECTORS=1`, `--no-hooks --vanilla-only` on scenarios that reach it,
  `vectors.py merge <runs> --out <scratch>`, `ei_vectors.exe <scratch>`): tests/vectors is not in the original runs'
  key without EI_VECTORS, so the plain original suite only replays its cache. The path family's raw recording does
  not replay without `tools/path_vectors.py prune` (its @AIMap records are sampled: searches on maps not kept
  crash). Replay a recorded script family together with tests/vectors/data/crt (the game CRT's character classes the
  lexer reads): without that folder ei_vectors loops forever at full CPU. Replay a scratch folder with a timeout.
  ei_vectors links only some of src/game (CMakeLists.txt's ei_vectors sources): a replayed function that calls a
  helper defined elsewhere fails to link there; use a local helper.
- The device's mesh buffers (the original's 007AEDE0-007AEE3C; plain globals of ours): `src/game/device_buffers.h`. src/render reads the clip stack through
  the typed `TransformPipeline::stacks`; render_terrain.cpp once read it at pipeline +0x24 / +0x28 and drew no
  terrain after a layout change (the goldens did not notice, the render_shadows* / render_terrain_seams /
  render_loose_bmp checks did: run the render_* scenarios after a layout change).

## Strings: CString to std::string (C14a)

- The game's text is `std::string`. What CString did that std::string has no member for is in `src/lib/str.h` with
  CString's answers: `str::Find` / `ReverseFind` / `FindOneOf` (int positions, -1 when absent, the multibyte CRT's
  searches), `Mid` / `Left` / `Right` (a start or count outside the text clamped, never a throw), `Format` (the game
  CRT's vsprintf, `crt_format.cpp`: "1.#INF" and three-digit exponents, C26), `MakeLower`, `Remove`, `TrimLeft(s, char)`
  (CString::TrimLeft(TCHAR)), the trims (byte stepping, ASCII white space: `text::IsSpace`, what `_ismbcspace` answered),
  `MbsLess` (`_mbscmp < 0` on the text up to its first zero byte: the order of the maps keyed by name), `Buffer` /
  `CutAtNull` / `SetLength` for GetBuffer / ReleaseBuffer(-1) / GetBufferSetLength.
- String lists are `std::vector<std::string>`, returned by value. `MappedResource::ListFiles` keeps its out parameter:
  the vanilla body replaces the list, Rebirth's ResOverride (modfolders::ListFiles) appends to it, so the reused scratch
  vector of TextSources::ListKeys and SoundManager::ListWaveFiles grows over the archives as game.exe's array did. `console::Arguments` is the console's command arguments. Text that crosses a std::string (a hash of a
  name) hashes its bytes as the CString did, up to the first zero byte.
- A reader of the original's `CString::ReadString` (CStdioFile): 128-byte chunks through a local buffer, fgets given 129
  as MFC gave it, each chunk counted up to its first zero byte (ReleaseBuffer(-1)), FALSE when the last fgets read
  nothing, even after a full chunk (`lib::TextFile`).
- Rules from real bugs:
  - A `const char*` given to a std::string parameter builds the string from it: CString took a null pointer as ""
    where std::string's constructor does not. `GameFlow::SetNetworkZone(nullptr)` crashed net_pair_leave (it stores ""
    for null now); the credits' script tokenizer gave null past a line's last word (`WordOrEmpty`);
    `Object3DClientSpecific::SetSkins(first, nullptr)` stores "" for a null name. A crash there is the first place
    to look.
  - A retyped member makes its class bigger, so everything that knew the old size or offsets breaks without a compile
    error once the layout asserts are gone. Before retyping, grep for the class's old sizeof and the offsets after the
    member: the factory's allocation sizes are `sizeof(T)` (heap corruption C0000374 once CUnit and CMapObject grew),
    structs that pad up to an offset (`unused_000[0xE8]`) over another class are the same hazard (`CameraSettings`
    read the options' camera block through a `reinterpret_cast` and the camera distance was 0 once GameOptions grew),
    and so are element counts taken from a byte difference (the hero screen counted its voices as
    `(end - begin bytes) >> 3`): grep `reinterpret_cast<...*>(` of the object's pointer, `>> 3`, `/ 0x..` and
    `reinterpret_cast<char*>(...end())` near the class's containers. A class held by value grows with its member (CParty
    sits in CPlayer, ClassTemplate, PartyRecord, ClientSession and TradeRecord through
    `lib::InPlace<CParty>`, in MpHeroRecord as a plain member): grep the embedders too.
  - Casts hide retypes: a `std::string*` compiles through a cast or a `void*` parameter and is then read as another
    string layout (`ComposeSkin`'s `void*` target crashed in every scenario with a hero); a function returning a
    `const T&` of a retyped member can return a reference to a temporary (`RecordMob` / `RecordMap` handed
    LoadZoneScripts a dead path).
  - A rewrite that turns one statement into two (`return std::construct_at(out, a);` -> `*out = a; return out;`)
    braces them where the statement was an if's body: unbraced, the return ran always (BriefingSpeakerInto, 8 MP
    goldens).
  - A pointer into a CString that another CString shares (refcounted buffers) outlived the local copy; a std::string's
    does not (`FigureStrip::Draw`'s `name`).
  - The harness digest reads our std::string members through `Text(const std::string&)` in
    tests/verify/image/harness_digest_image.cpp (the shared .inc reads game.exe's CStrings as const char*).
- Checked by: the build (every changed call shape is a compile error or a /W4 warning first), the function vectors
  (`Script::Load` / Tokenize / the script copies replay the loader's string handling), both suites with goldens.

## Containers: STLport vector and list to std (C14b)

- Lifetimes: a container member of a class with a real constructor and destructor is the compiler's (no
  construct_at / destroy_at, no clear before the end). A container in raw storage (a class built by
  `Construct()` on a raw block) is built and destroyed by hand with `std::construct_at` / `std::destroy_at`.
  Destroying a std::list twice frees its sentinel twice; an element loop before a destroy_at of the vector destroys the
  elements twice. The item stock looks raw (Construct / Destruct) but is a member of ClassTemplate: its maps are the
  compiler's (a construct_at there built and freed them twice, a heap corruption at start-up).
- constinit vector globals with game-order exit functions (item_stock's scratch vectors, the frame's draw lists, the
  quest files, the zone table, the fog's dead cells): the exit function is `lib::FreeStorage(v)` (a swap with an empty
  one), so the C++ destructor at our CRT's exit finds nothing to free. A std::list is not constinit (its constructor
  allocates the sentinel): a global list is a plain global with no address. named_data's TYPES has `std::vector`
  ('III', zero is empty). A std::unordered_map is not valid zeroed; lib::HashMap is (an empty bucket vector).
- What the inlined STLport bodies did is the std member: reserve / push_back / resize / erase(begin, end) / the list's
  clear and destructor walks. `std::next(end())` is no way to the first node (use begin()). lib::IntroSort takes
  pointers: vectors pass `data()`. Value-initialised elements (`emplace_back()`, `resize`) zero what the original left
  as stack bytes (save entries, connect list rows, trade rows, bindings): the fields are written before they are read.
  `resize()` value-initialises slots where the original kept stale ones: a site that reads a slot before writing it
  differs.
- Walks that erase the entry they stand on step first (`next = std::next(node)`; CPlayer::ReleaseTamedUnits: the game's
  walk read the freed node); remove(value) is an explicit walk that erases as it goes (the original destroyed each
  element on the way, std::list::remove defers that). "No node" is a value-initialised iterator (`{}`).
- Elements that move: an element holding a std::string or counted pointer has real special members (a move for
  DeltaField and MemoryStream); an element whose destructor needs a complete type its header only declares
  (EffectParticle's ParticleSource) declares its move and destructor and defaults them in a .cpp that has the type.

## Containers: STLport hash_map, map and set to std (C14c)

- `std::map` / `std::multimap` (sorted, so the walks keep their order) where the original had a tree: the link meshes'
  headers and ranges, the mesh cache (a multimap; ClearCache releases from the back as the tree did), the console
  commands and help topics (`str::MbsLess`: _mbscmp order), the person texts, the camp's arrivals, the item
  object caches.
- A hash map whose walk decides something (which object goes first, what is saved or listed in which order, which
  entry a loop stops at) is `lib::HashMap<K, V[, lib::TextHash]>` (src/lib/hash_map.h): STLport's order, the prime
  bucket counts (100 asks for 193), a new key at the head of its chain, resize before each insert with the chains
  moved head first (keys that share a new bucket come out reversed), erase giving the next entry in walk order. A
  std::string key hashes `h = 5 * h + c` over signed chars up to its zero byte. tests/vectors/replay_hash_map.cpp
  checks those cases. lib::HashMap holds (each declaration says what its order feeds; C14zj audit): the world's id
  maps and line lists, the player's objects / timed objects / units, the base manager's objects and spares, the
  archive's loaded objects, the held particles, the fog cells, the .mob reader's records, the deferred sounds, the
  terrain lights, the named floats (party and player variables: saved, listed and walked in it), the item stock's
  recipes (the walk feeds its random rolls), the unit prototypes by name (EI_Plugin's fallback reads bucket heads),
  the party manager's parties (`HashMap<int, CParty>`, saved chunk by chunk), UnitSet's tables (Write's walk is the
  zone message's bytes). Release / delete walks of counted objects stay too: a destructor releases counted members
  (a particle source's texture) into other managers' spare lists, which later creations reuse in order.
- An int-keyed lib::HashMap whose keys all stay below its bucket count (193) and that never grows past it walks
  ascending: the acks' speeches by kind (kinds < 0x33) are a std::map, the acks dump's order unchanged. The spell
  sequences and their containers (only ever null entries) are std::unordered_map.
- The rest (looked up only, or walked to free or log) are `std::unordered_map` / `std::unordered_set`: the record
  tables' name and code indexes, the quests, the class and message registries, the saved numbers, the positions and
  spare counts, the bridge cells, the material colours, the pixel formats, the loaded files, the figure models and
  sets, the render presets, the wave cache, the pending dynamic objects, the unit shapes by standing height (the
  heights are tenths above 0.2, never NaN, so float equality is the game's), and the two int sets.
- `lib::HashMultimap` (hash_map.h) is STLport's hash_multimap: insert grows the table for one more and links the value
  right after the first entry with its key (else at its bucket's head); equal_range ends at the entry after the equal
  ones in walk order. UnitSet's figured groups are `HashMap<prototype, HashMultimap<scale key, FiguredUnitRecord>>`, the
  dressed records `HashMultimap<prototype, DressedUnitRecord>`; Add reaches the group twice (`figured[prototype]` before
  the search and before the insert), as the game's find_or_insert did: each grows the groups for one more entry, and the
  bucket count is the walk order. A new party goes through `PartyAt` (party_records.cpp): a new party, a copy for the
  pair, a copy of that in the map, as the game's operator[] did.
- Where the original read the count after the insert (the saved numbers), the slot is taken first, then the count read.
  A hash_map's operator[] builds its value in place, so a value holding a std::vector (RenderPresetRecord) is
  constructed, not assigned over raw memory. The render helper's destructor collects the presets before releasing them.

## Containers: the kept vectors and lists (C14n)

- Records have real special members or none (the compiler's); the layout only matters for the tests' game.exe views.
- Counted elements are RAII types: `HeldRef<T>` (ref.h; any class with AddRef / Release: RefCounted's refs, EngineObject's
  refCount and owner release) and `SharedRef<T>` (core_refcount.h: owning a share: copy ShareOwned, destroy
  DestroyOwned). A move hands the
  reference over, so a reallocation takes and drops none. A SharedRef copy of a copy-on-share object is a clone, as
  STLport's fill insert made it (the file message's chunks).
- The record databases' tables hold their records in a `std::vector` member (`records`); the chunk serializers share
  `RecordChunks` (chunk_archive.h: chunk `id` with one chunk 1 per record, `occurrence` set before each).
- A copy with side effects keeps the original's copies: a saved hero's (MpHeroRecord) copy loads its party again,
  drawing object ids and random numbers, so `SavedParties::ReserveOne` doubles the storage before a record is added
  (STLport's growth, the old records copied before the new one) and the new-hero record at capacity is a blank's copy.
  Without it the multiplayer goldens differ (the hero's #unit and the random state). A CParty copy runs its save and
  load (CParty::Create), so the copies are kept; `TradeRecord` holds its three CParty by value with a copy that copies
  them in order, and RecordTrade adds a record as insert(end()) did: a blank, copied into the list, the blank destroyed
  before the parties are assigned.

## Containers: the last STLport hash tables and trees (C14q)

- net::Connection's five trees are `std::map` members (Reset destroys them by hand before the placement rebuild): the
  entries by sent id, by object and per priority (`std::array` of 11) hold `EntryRef` (`HeldRef<ReplicaEntry>`), the
  records `Record` (a `SharedRef<RefObject>` and the version: copied with Share, released as the owner), the attached ids plain
  values. Pointer keys compare as the game's unsigned compares, the sent ids signed. The destructor is the compiler's:
  the original's teardown is the reverse member order (the reliable streams, the chunk lists, the maps, the packet
  lists, the freed-id batches). operator[] (`bySentId[key] = entry`) takes the new reference before dropping the old,
  as the inlined Assign did.
- `UnitShapeCache::Resolve` is the find-or-create both callers inline.

## Containers: the record and held lists (C14s)

- The sector grids' cells, the client's heroes and the frame's deferred units are `SectorList`
  (`std::list<HeldRef<CMapObject>>`, world.h): an insert takes a reference, an erase or clear drops it. `RemoveHeld`
  (world_client_maps.cpp) holds the object while its entries are erased, as the game's remove(value) did. A grid is
  `unique_ptr` cells and row pointers; `SectorGrid::Free` destroys the cells from the last, and the client's destructor
  frees both grids where the original did (the second grid still holds objects then). The client's filed objects
  (`MapObjectList`, `std::list<CMapObject*>`) keep their reference by hand: the original's destructor freed that list
  without releasing (Destroy has released and emptied it by then).
- The connection's lists: `SentItem` holds an `EntryRef`, `SentPacket` its items as `std::list<SentItem>`; the
  history, chunk and freed-id lists are members. Splices are std::list::splice.
- `LinkArchive` (model.h; the loader's `std::list<LinkArchive>`), `TextSources::archives` (text_manager.h) and the
  loopback link's `StreamQueue` (`std::list<MemoryStream>`): Send appends an empty stream and copies into it. The
  link's copy constructor keeps the game's queue copy (a new stream of the source's size whose Read takes the
  source's buffer: the fresh bytes are read into the source).
- The clients' find by address is SessionBase::FindClient's loop.

## MFC CArray and CList to std (C14m, C14u)

- Every MFC collection of the game is a std container: plain-element ones copy as the original's bytes did (the
  default policy zero-filled and destroyed nothing); elements with a policy have default member initializers for what
  the policy's Init set (Behaviour, ZoneExit, MapRecord, EffectParticle, SkinLayer's 0 / -1 / -1), a move for DeltaField
  and MemoryStream, HeldRef for counted pointers (SharedObject::objects, EffectParticle::source, the client's exit
  markers) and PartyUnitRecord's destructor releases its items (its move assignment releases first, so erase drops a
  record as the original's Destruct + relocate did).
- The script (script.h): every record has real special members. Object references are
  `ScriptObjectRef{HeldRef<EngineObject>}` (a ScriptGroup is a `std::vector<ScriptObjectRef>`, still kept sorted by
  ScriptGroup::After); the effects (point lights, particle / FX / lightning sources) hold a move-only `ScriptEffect<T>`
  whose reset is ScriptDropEffect (the timed object removed, the reference released); a nested call owns its command
  (`std::unique_ptr<SSCCmd>`, deep-copied). Script has a constructor (256 point light slots, one temporary group and
  one temporary reference, then ScriptReset) and a destructor (ScriptReset, then the members); the world builds its two
  scripts with construct_at / destroy_at. Tokens are a `std::list<ScriptToken>` and a parse position (ScriptPos) its
  const_iterator; "Unexpected end of file" reports the list's back().line. References are released by the containers'
  destructors, so the order of Release calls within one clear can differ from the original's (no count reaches 0
  earlier than before); a run left unfinished (RunCommands) copies the instance's values only, as the game does. The
  script's vector recorders read game.exe's script through script_original.h's views (exe::Vector / List); the replay
  (replay_script_load.cpp) builds a real Script: as many empty blocks as the record's counts (new blocks are hashed
  after them) and a token list whose first token carries the tail's line.
- A record that hands its references on without releasing them (the shared objects' items given at loot: the original
  frees the node and leaks the references) uses `HeldRef::Take()`. Counted elements in a plain list (the world's
  counted items, the trap's victims, the armour spells' items): the release is
  explicit before erase / clear; the original's DestructElements did nothing, so the counts are what they were.
  `ReleaseAndClear(c)` (engine_object.h) releases and clears a list of engine objects.
- Writes that compile on any pointer and corrupt a std container: memset / memcpy over a struct holding one, a
  reinterpret_cast of a container to another layout, a function taking a container as `void*` (SceneRenderer::
  DrawSprites' point and sprite arrays: every caller's container must be retyped together; the lightning glow's records
  stayed another array type and crashed it). Find them with temporary `= delete` overloads for std::vector /
  std::list and a forced-include check that static_asserts memset / memcpy / memmove destinations are trivially
  copyable; neither sees an explicit `(void*)` / `static_cast<void*>` argument, so grep those too.
- Gone from the code: the collections' vtable tags and their ABI helpers; config/vtables.csv keeps the tables as stored
  only by game.exe.

## Open items

- Static initialisers run before the hooks install (004632A0 the lever table constructor, 0045E820): a port of one is
  compared at WinMain's entry instead (start-up checks; 00444740, 00458870 and others, app.md "A-E2"). `WinMain`
  (00456440) itself can be hooked and is ported (app.md).

## A-E1: text helpers and the object id map

- text_helpers.cpp. `TextTokenizer` (00445D20 `Start`, 00445D30 `Next`): splits a CString at a separator
  string; `Next` returns the word's text and moves past the separator, the last word runs to the end of the
  text, and a position at the end gives null. `RemoveCarriageReturns` (004461E0, CString::Remove of 0x0D).
  `FindListedName` (00447F50): the entry of a CStringArray that equals a name ignoring case (`_mbsicmp`), else
  the name; the name comes by value and is destroyed by the callee (ours: a `std::string` by value; `str::Mid(first,
  count)` is 006FD9D7, `str::Remove(ch)` 006FD963).
  Checks: `Next` and `FindListedName` side by side by content; `Start` against the original `Next` on a rewound
  copy (its own original would enter `Next`'s wrapper); `RemoveCarriageReturns` ours only. The credits screen
  (0062B7D0) is the only user of the first three and the scene registry of the fourth; no scenario reaches them.
- `ObjectIdMap::ReleaseAll` (0047B670, object_id_map.cpp): every object released up to the first empty entry,
  the nodes freed, the count and maxId cleared (the buckets stay). The world's object map (map close) and the
  player's two maps of sent objects call it through `ObjectMapClearOriginal`. Ours only (objects can go with
  their last reference); the map must end empty.
- A-E1: `Camera::LoadPath` (0040ECD0, RET 8, camera_path_load.cpp; Rebirth-patched at its file open
  0040ED42, so the vanilla launch only): `<data folder>camera\<name>` opened with a lib::File (0 when that fails),
  the keys (a `std::vector<CameraKey>`) resized to length / 36 and filled with one read. With the second
  argument above 1 and below the key count, the first key among that many that is more than 500 time units
  after its predecessor gives its position and rotation to every key before it. Side by side on the camera
  itself (the original loads, the keys are copied, ours loads again; results, counts and key bytes compared).
  Reached when a recorded path is played (the glue 00578820 / 00578880 calls it through its address).
- A-E1: constructors of globals that run from static initialisers, before the hooks install (their hooks never
  fire; written so the standalone build has them): the map database (0079BF00, game.exe's 0045E820 builds two empty CArrays; ours is a plain global), the `LeverRecordTable` constructor (004632A0,
  the lever table at 0079C1A0: no records, an index of 193 buckets; ours is a plain global since C50b) and the party
  manager's (004A5870, at 007AA5A0: a party map of 193 buckets, no entries, records or templates, two empty
  strings; +0x20 is not written; ours is PartyManager's implicit constructor since C50b). The unused functor bytes the originals copy from uninitialised stack bytes
  are not written. Ours-only wrappers in verify_static_constructors.cpp. 00458870 was ported by A-E2 (app.md).

## Containers: the net lists and the last node-pool users (C14t)

- The sessions' message lists are `std::vector<SharedRef<T>>` (copied with Share, released as the owner) and
  `std::vector<HeldRef<T>>` (a plain reference). An insert of a raw-held pointer is `emplace_back(object)`: the element's
  constructor is the policy's copy. Growth moves the elements (both moves are noexcept and hand the reference over): a
  copy of a SharedRef to a copy-on-share object would share a clone. Removing by value is `std::erase_if`; a SharedRef's
  move assignment swaps, so the erased object is the one released.
- The timed lists are `std::vector<TimedEntry<SharedRef<RefObject> / HeldRef<RefObject>>>` (net_classes.h; the key's bits,
  then the reference; a static_assert keeps their move noexcept). The queueing code keeps the original's temporaries: a
  share of a share of a copy-on-share object is a clone, so the client's timers and the server's awards hold a clone of
  the entry's share (the awards a clone of a clone). `TimedEntry(bits, Holder)` takes the reference by value (one share,
  moved in).
- Two stay lists (C63d): the connections (`std::list<SharedRef<Connection>>`; FindClient returns an iterator, end() for
  none, RemoveClient ignores end()) because OnUpdate keeps its connection's node while the update's handlers may kick
  other clients (ServerOnCommand), and the client's trade records, whose copy draws object ids (TradeRecord). The
  connection's packet history, reliable chunk queues and freed-id batches are vectors; the loopback link's queue is a
  deque (MemoryStream moves but does not move-assign). The driver session's drivers / holders are vectors; OnShutdown
  clears the holders, then the drivers, as the destructor (reverse member order).
- The sessions' destructors clear the lists in their bodies where the original released them, between the counted
  pointers' releases (the compiler's destructor then frees empty lists); ClientBase's list is the compiler's.
- The flow's changed variables (UpdateFromServer) are sorted with std::stable_sort by stamp: the server gives every
  set its own serial (g_variableSerial), so no two variables share one and the STLport introsort's order is the same.
- Also std: the briefing screen's movers and shown (std::list members, built with push_front), the container's
  extra input sides (`std::vector<UiInputSide*> inputs`), the world's local id lists (std::list<unsigned>). The
  original's node allocator has no port (standalone.md, "STLport"); `lib::IntroSort` (lib/intro_sort.h) is the sort.
- The harness's netstate reader walks both sides with begin / end (game.exe's view of the server's connections is a
  StlpList<RefPtr>, exe_objects.h; `HeldObject` reads either side's element).
