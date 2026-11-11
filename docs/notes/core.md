# Engine base

Maths and transforms, reference counting, the object manager, streams and chunked files, the camera, strings and
containers. Network code is in `network.md`, the application layer in `app.md`, float rules in `float-maths.md`.

The original: game.exe 1.06 (Russian, VC6, 2001), MFC 4.2 and STLport linked in, no RTTI (standalone.md section 1).

## Transforms

- `TransformObject` (EngineObject plus local / world position and rotation): CombineRotation, UpdateWorld,
  WorldMatrix (row vectors), TiltTowards (the down axis a tenth of the way to a target), TurnTowards, RollTowards,
  Rotate(axis, angle), TurnUpTowards, SetHeading (atan2 about +Z). The unit classes override CombineRotation (world rotation only).
- `ClampUnit` gives -1 for NaN. The camera's quaternion products in `ApplyView` and `Update` add their terms in
  different orders; each is written out.

## Random generators

- The game's generator is an ISAAC (`IsaacRandom`, random.h) plus the CRT's `rand()`. The original seeded ISAAC with
  1 KB of a random file found by walking drive C, xored with `rand()`; ours fills the 1 KB from `std::random_device`
  and seeds `rand()` from it (the bytes only have to be unpredictable). The harness fixes the seed through
  `IsaacRandom::SetSeedHook`.
- `g_clientRandom` is a second ISAAC that `World::Create` seeds right after the game's (from its memory words, no file,
  no `rand()`), drawn by every effect whose result only reaches the picture or the sound (`ClientRand()` where they drew
  `rand()`; the client's idle animation picks through `AnimationRandom::Client`; the server's keep `rand()`). Client
  draws never move game state, so a renderer or interface change cannot change a golden through the generators.
  The server-side effect objects keep `g_random` where the world's tick decides the draws (weather's strikes, rain
  and the rain bolts' positions, a tornado's launch speed); what the client does with an effect it unpacks (sound
  sequence choosers, the scorch mark's angle of a FireBlast / LightningBlast particle, the bird's perch) and the main
  menu's bolt choices (`ShowBolt`'s `picks`) draw `g_clientRandom`.
- `g_particleRandom` is a third ISAAC, the particle sources' own (every draw in `particle_*.cpp`), that `World::Create`
  seeds from `g_clientRandom`'s memory right after it (`InitFrom`; the harness's `ReseedRandom` the same way). So a
  change to a particle effect moves only the particles: not the client units' idle picks, the scorch marks' angles
  or the sounds that share `g_clientRandom`. It is kept in visuals.sav as the client one is (renderer.md,
  "What a save keeps of the picture": ParticleGenerator).

## Reference counting

- `RefCounted` (16-bit count, copy-on-share flag, detached flag; virtual destructor and Serialize) and `RefObject`
  (adds Shutdown and Clone). The network sessions derive from RefCounted directly.
- The original's owning `RefPtr` is a `Ref<T>` with the owner's operations as free functions: `DestroyOwned` (a
  still-shared object is detached and shut down first, then the Ref emptied), `ShareOwned` / `ShareAssignOwned` (a
  clone of a copy-on-share object). `SharedRef<T>` is a container element that owns its share (copy: share; destroy:
  DestroyOwned). A Ref's `Assign` of the object it holds does nothing (the original added and dropped a reference).

### Counted pointers

- Every counted class has `AddRef()` / `Release()`: EngineObject (at 0 the owner's ReleaseObject), RefCounted (deleted
  at 0), LinkMesh, LinkFigureLoader, DeviceMaterial, RenderPreset, RenderHelper, RendererBase. A site that keeps a
  raw pointer counts with these. The original's drops without the release at zero (the constructor's handed-back
  item, a figure's parts) are `EngineObject::ForgetReference()`; `ObjectListIterator` (and `PlayerIterator`) counts
  its manager from `First` on and drops the count in its destructor, so the manager must outlive the iterator.
- A manager has no owner, so its count never reaches a release: the worlds hold theirs as `std::unique_ptr`s, without
  a count of their own.
- A counted field is a `Ref<T>` (src/engine/core/ref.h): `Assign(p)` takes p's reference, then drops the old one; `Reset()`
  nulls, then drops; `Take()` hands the reference on. There is no raw store. The destructor drops the reference, so
  an owner resets a Ref by hand only where the release must come before other work or in another order than the
  members' reverse declaration order. T must be complete where the owner's constructor and destructor are defined.
- A container element that holds its reference is a `HeldRef<T>` (copyable, movable; a move hands the reference over,
  so a reallocation takes and drops none). `HeldRef::Take()` hands a reference on without releasing (the shared
  objects' items given at loot: the original leaked them).
- Ids in saves: `stream->WriteId(object)` writes the id (kNoObjectId, -1: none), `LoadRef(ref, stream, world)` looks it
  up.
- A side half is held by its owner's Ref and points back at it; one setter per owner swaps it (`Object3D::SetHalf`,
  `MapObject::SetCompanion`, `Unit::SetController`, `TorchObject::SetSpec`, `Object3D::SetAnimationState`,
  `FigureMesh::SetDrawer`): the old half's back pointer is cleared before its release, the new one's set after it is
  taken.

## Object manager

- `BaseManager`: objects by id (`lib::HashMap`), the registered list, spares per type and their counts, an id range
  for new ids, the context (`ManagerContext`). Objects are never freed while the manager lives: RemoveObject turns
  them into spares of their type (id -1) and CreateObject takes a spare before making a new one. Ids are maxId + 1
  while that fits the range, else `rand()` probing (100000 tries, then the fatal "BaseManager::RegisterInstant").
- LoadObjects / SaveObjects end their list with the mark -0x521. Kept: LoadObjects reads an id equal to the end mark as
  an id when a 1 byte follows.

## Streams and chunked files

- `ByteStream`: direction, ok flag, limit / save format version (2000000000 when constructed); virtual Read, Write,
  Seek, Size, Position, Clear (memory streams empty, file streams rewind), CopyFrom. One value is
  `ReadValue(x)` / `WriteValue(x)` (sizeof(x) bytes, a format value: "Values read and written as bytes"); a read whose
  size is not the variable's (a byte into an int, a block of several members) is `Read(&x, N)` with the size written
  out. The rest of the vocabulary: `ReadAs<T>()` (the value returned), `ReadCount` / `WriteCount` (the four-byte
  count in front of a list; a negative one reads as 0), `ReadId` / `WriteId` (an engine object's id, kNoObjectId for
  none; the reader looks it up) and `Skip(n)` (bytes nothing uses any more). A value in a raw buffer is
  `lib::Load<T>(address)` / `lib::Store(address, value)` (lib/bytes.h; every format is little-endian, as the host:
  no file keeps its own 16/32-bit readers).
- `MemoryStream`: a byte buffer with a bit writer (the byte being assembled, the mask of the last bit, the index of the
  byte the bits go to). Write grows to twice the need; `Resize` keeps the contents (1.5x + 0x10), `SetSize` is exact
  and drops them.
- `FileStream` (net_link.h): a ByteStream over a `platform::File`; the saves build these on the stack.
- `Chunk`: a tree of chunks, each an 8-byte header (id, size including the header) and its content; every byte read or
  written counts in the chunk and all its parents. `RootChunk` owns the file. Strings: ReadString, ReadEncodedString
  (content decoded), string arrays. Kept: `ReadQuad` returns the file's values in the order 2, 0, 3, 1;
  `ReadVector3` returns the bits as read.
- `ChunkArchive` (the object archive of the model files and saves): an index stream and a data stream; a saved
  archive has three top-level chunks: 1 (scratch data, read back on load), 0 (index: class id and key per object), 2
  (data: one child 1 per object, with child 0 the key and child 1 the object's fields). A loaded archive's Close
  gives every loaded object one reference and takes it back. The class id is the object's `ArchiveType()` (an
  `ArchiveClass`: 1 class template, 2 party record, -1 any other); Open creates each indexed object by it
  (`CreateArchived`). Never key a class by its vtable address: /OPT:ICF may fold tables. `RecordChunks`
  (chunk_archive.h) is the shared record-table serializer: chunk `id` with one chunk 1 per record.
- The saves are byte-for-byte repeatable: two runs' autosaves at the same point are identical.

## Values read and written as bytes

Every Windows build is x64; formats are 32-bit.
- A value read or written as its bytes is a format value (`lib::kFormatValue`, src/lib/bytes.h): plain, no padding,
  no pointer, no integer wider than 32 bits. `ReadValue / WriteValue`, `platform::File`'s, `ChunkArchive::Field<T>`,
  `lib::Load / Store`, the network cursors' `Put / Get / Take` and the figure readers assert it.
- No padding (`lib::kNoPadding`): a struct copied whole writes every byte, and padding holds whatever the memory held
  (MoveModeState's two padding bytes once differed in mission.sav between Linux and Windows). Integer-only structs pass the standard `has_unique_object_representations`; a struct with a
  float (which that trait rejects) declares its fields beside its definition, in its own namespace:
  `constexpr bool NoPadding(const Vector3*) { return lib::FieldsFill<&Vector3::x, &Vector3::y, &Vector3::z>(); }`.
  FieldsFill fails to compile unless the listed fields (each once, each padding-free) add up to the struct's size, so
  a new member or a gap breaks the build. Padding found is named as a zero field (`std::uint16_t unused = 0;`), which
  writes the zeros the goldens hold. A raw `Write(&x, sizeof(x))` of a struct goes through `WriteValue` instead.
  A count goes in as `static_cast<std::int32_t>(...)` (`std::ssize` and `size_t` are 64-bit; the armour spells', item
  ids' and recharging ids' counts once went out as 8 bytes).
- A struct read or written whole has a `static_assert(sizeof(...) == n)` beside it; no pointer, `long`, `size_t`,
  `time_t` or handle member belongs in one.
- A read or write spanning several members (`Read(&panVelocity, 0x1C)`) holds only while no pointer sits between them,
  and never across a base class's end: a base with a vtable or pointer is 8-byte aligned, so its last 4-byte member may
  be followed by padding (ArmorRecord's field 0x15 read 0x24 bytes across it and shifted every absorption; it goes
  through `AbsorbRow`). Grep `Read(&x, <number>)` and literal-size `memset` / `memcpy` near a class before
  changing its members.
- A pointer kept as an integer is `std::uintptr_t`, a socket `platform::net::Socket`, never `std::uint32_t` (a vtable
  key and a socket were once cut to 32 bits; the image sits above 4 GB).
- `long` is 32 bits on Win64 but 64 on Linux and macOS: none is in a format.
- The original's garbage reproduced (a stale or wrong slot read as a value; only its low 32 bits used, never
  dereferenced) stays correct on x64 with another value, as it differs between runs anyway: ai_local_search's stale
  path cell words, combat_steps' animation key, ui_scroll_text, world_effect, world_units' CreateObject result for an
  unknown kind.

## Camera

`Camera` (class id 0x43, a TransformObject) is a global; `Camera::Update` runs once per frame from the main loop. The
members are named in camera.h. Flags: 0x8 follow the target, 0x10 recording to a file, 0x20 playing a path, 0x40 free
placement, 0x100 bounded, 0x200 briefing limits. Pitch is kept in degrees. F9-F12 save (with a modifier) and restore
views; the mouse wheel zooms by 1.05^steps (`ZoomBy`); a shake's distance uses `_hypot`. Recorded paths are `camera\*.cam`
(36-byte keys: time, position, rotation; `Camera::LoadPath`: with a second argument above 1, the first key more than
500 time units after its predecessor gives its pose to every key before it).

Kept: `RestoreView` sets distance and pitch only; `SampleTerrain` counts the centre sample once more; `StopMotion`
multiplies the view axes by 0 and leaves the turn / tilt rates; `PlaybackStep` at the last key places
the camera without clearing the playing flag; `Update` tracks a target only with flag 8 clear, and panning clears both;
the script commands SetCameraPosition / SetCameraOrientation do nothing; `OpenFile` renames an existing
camera.cam to the next number after the highest camera0*.cam (read from the last four characters of the title).

The limits (settings_catalog.inc's defaults; app.md, "Options") let the pitch go from -45 (looking up) to 90
(straight down) and the distance from 3 to 50 (hard limits 3 and 70), on the maps and the village screens. What copes
with them:
- `Pan` moves along the ground by the camera's X axis turned a quarter about Z. The camera's own Y axis, which the
  original took, points back along the ground below level (and nowhere when level): looking up, the keys and the screen
  edges panned backwards, and at a level camera its normalisation gave NaN.
- Pitched below level the camera sits below its focus; `ClampAboveObjects` lifts it over the ground and `ApplyView`
  aims it back up at the focus.
- `ClampAboveObjects` keeps the camera a clearance over the highest ground (its 5 x 5 samples 2 around) and the tops of
  the objects it counts: CameraMinHeight (0), but at least `NearPlaneReach`, how far the near plane's corners lie from
  the camera's point (near 1, the field of view over the window's 4:3 part widened by the axis swap: 1.17 at 4:3,
  1.24 at 16:9). The original kept only the point clear (3 over the ground, near 2 inside it): with a clearance of 0
  the near plane's lower edge cut up to 0.6 into a slope in front of a camera looking up, and the view showed the
  ground's underside. A shake's offset is followed by the clamp again, so a shake down stops there too.
- The least distance 3: the focus is 2 over a hero's place (its bounds: 1.78 tall, the top 1.2 under the focus, a
  footprint of radius 0.97); at pitch -45 and distance 3 the near plane's nearest point stays 1.16 from the hero's axis,
  outside it, and at any pitch from 0 up the camera is above the hero.
- Panning scales with the distance's place between the least and the most, so a change of the least distance changes
  the pan speed at every distance; recorded inputs that click the ground after a pan then land elsewhere.
- When the least and the hard least distance are equal the limits' easing (`Update`) never divides by their
  difference: it runs only between them.
- Clicking the sky picks a ground point along the ray behind the camera (`PickPoint`'s bounded iteration), as the
  original did.
- Check: scenario `camera_limits` (`camrange` steps: pitch 90 / -45 by the keys, the zoom keys and the wheel to 3 and
  out, the up key's pan at pitch 45 and -30 in the same direction, `camrange ground 0 ...` standing and panning on
  slopes looking up).

## Strings

- The game's text is `std::string`, UTF-8 inside the game (`standalone.md`, "UTF-8"). What CString did that
  std::string has no member for is `src/lib/str.h`, with CString's answers: `str::Find` / `ReverseFind` / `FindOneOf`
  (int, -1 when absent), `Mid` / `Left` / `Right` (clamped, never a throw), `Format` (the game CRT's vsprintf:
  standalone.md, "The CRT"), `MakeLower`, `Remove`, the trims (ASCII white space), `MbsLess`
  (`_mbscmp` order: the maps keyed by name), `Buffer` / `CutAtNull` / `SetLength`.
- The game's line files (keyboard.ini, tutorial.ini, the console's loadvar and exec files, sysmsg.txt, 2utoexec.cfg)
  are read whole and cut by `str::Lines` (at '\n', a '\r' before it dropped) and written with `File::WriteLine`
  ("\r\n").
- Case and character classes past ASCII: standalone.md section 4 ("Text as bytes"). A name hashed for a table hashes
  its bytes up to the first zero byte.
- Rules from real bugs:
  - A `const char*` given to a std::string parameter must not be null: CString took null as "" (crashes in
    `GameFlow::SetNetworkZone`, the credits' tokenizer, `SetSkins`). A crash there is the first place to look.
  - A class that grows breaks everything that knew its old size or offsets: factory sizes, structs padded up to an
    offset over another class, element counts from byte differences (`>> 3`), embedders (Party sits in Player,
    ClassTemplate, PartyRecord, ClientSession, TradeRecord, MpHeroRecord). Grep before retyping.
  - A function returning `const T&` to a converted member can return a temporary.
  - Turning one statement into two inside an unbraced if runs the second always (8 multiplayer goldens once).

## Shared containers

Containers are `std::`; `lib::FreeStorage(v)` (src/lib/containers.h) frees a vector's storage, `lib::SetAtGrow(v, i,
x)` grows to i + 1. A sequence is a `std::vector` unless pointers or iterators into it are kept while it grows or it
grows while walked (then a `std::list`, with a comment saying which); queues are deques.
- **Order is behaviour.** A hash map whose walk decides something (which object goes first, what is saved or listed in
  which order, where a loop stops) is `lib::HashMap` / `lib::HashMultimap` (src/lib/hash_map.h): STLport's order
  (prime bucket counts: 100 asks for 193; a new key at its chain's head; resize before each insert with chains moved
  head first; a string key hashes `h = 5 * h + c` over signed chars). Each declaration says what its order feeds:
  the world's id maps, the player's objects, the base manager's objects and spares, the archive's loaded objects, the
  named floats (saved and listed in it), the item stock's recipes (feeds random rolls), the unit prototypes by name,
  the party manager's parties, UnitSet's tables (the zone message's bytes), and release walks of counted objects (the
  spares they free are reused in order). `lib::HashMultimap::insert` links the value after the first entry with its key.
  tests/unit/hash_map.cpp checks the order. Trees are `std::map` (sorted, the same order); the rest
  `std::unordered_map`.
- A hash map keyed by pointer values walks in an order that depends on the addresses (`MappedResource::FreeAllFiles`):
  harmless so far, but check before changing allocators.
- `lib::IntroSort` (lib/intro_sort.h) is the game's sort where the order of equal keys is behaviour; the flow's changed
  variables use std::stable_sort (every set has its own serial, so no ties).
- **Copies with side effects keep the original's copies.** A saved hero's copy loads its party again (drawing ids and
  random numbers), so `SavedParties::ReserveOne` doubles storage as STLport did, and a Party copy runs its save and
  load; `TradeRecord` copies its three parties in order. Without that the multiplayer goldens differ. A copy of a
  SharedRef to a copy-on-share object is a clone, so growth must move (the moves are noexcept).
- Value-initialised elements zero what the original left as stack bytes; the fields are written before they are read.
  A walk that erases its own entry steps first; `remove(value)` is a walk that erases as it goes.
- Writes that corrupt a std container compile on any pointer: a memset / memcpy over a struct holding one, a
  reinterpret_cast to another layout, a container passed as `void*`.
- Containers that must stay lists: the server's connections (an update's handlers may kick other clients while its
  node is held), the client's trade records, the server world's projectiles and dynamic objects, the sound manager's
  wave cache, the sector grid cells.
- Global vectors with game-order exit functions free their storage with `lib::FreeStorage`, so the C++ destructor at
  exit finds nothing; a `std::list` is not constinit (a global list is a plain global).
