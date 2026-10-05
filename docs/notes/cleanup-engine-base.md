# Cleanup Phase 1: engine base (cleanup plan subsystem 1)

## Ticket C2a: CEngineObject / CBaseManager / CResourceManager (2026-10-05)

Behaviour unchanged; every rewritten address keeps its hook and its verify wrapper.

### Declarations

- `engine_object.h` `CEngineObject`: slots 2, 3, 11, 12 declared pure (they are `_purecall` in
  0073B8E0); `owner_` is a `CBaseManager*`; `kVtableAddress` (0073B8E0); new non-virtual members
  `DeletingDestruct` (00402D20, slot 0's body) and `ClearFields` (the inlined constructor's field
  stores, shared by CBaseManager, CTransformObject and CHoover). Implementation moved from
  object.cpp / base_manager*.cpp into the new `engine_object.cpp`.
- `base_manager.h` `CBaseManager`: the whole layout named (no byte blob left): `objects_` +
  `maxId_`, `objectList_` (an `mfc::CTypedPtrList<CEngineObject>`, vtable 0073B8D4),
  `positions_` + `maxPositionId_`, `spares_` + `maxSpareType_`, `spareCounts_` +
  `maxSpareCountType_`, `idLow_`, `idHigh_`, `unknown_A0` / `unknown_A4` (-1 when constructed,
  0 / 300 after Create, not read by the manager itself), `lastId_`, `context_` (was `owner_AC`).
  Every table is followed by the highest key stored in it. Slots 13-24 declared as virtuals in
  vtable order (Init, NewObject, DestroyAll, the seven archive slots, CreateObject,
  ReleaseObject); the free `BaseManager*(self, void*, ...)` functions became members (virtual ones
  registered through `...Hook` shims, rule 4). `ObjectListIterator` moved here from object.h.
  `base_manager_core.cpp` was merged into `base_manager.cpp` (and the verify files likewise).
- `resource_manager.h` `CResourceManager : public CBaseManager` (was `void* vtable_` plus 0xAC
  bytes); slots 3 and 16-22 are overrides registered through shims; `DestroyManager` is now
  `Destroy`. Its constructor/destructor bodies moved from resource_stream.cpp to
  resource_manager.cpp, and call `CBaseManager::ConstructBase` / `Destroy` / `DestructBody`
  directly instead of through forwarders to their original addresses (same code in the release
  build; the verify build no longer runs those three wrappers for this one caller).
  `CloseFile` returns `bool` like the original (both originals set only AL).
- `core_streams.h` `CStream` (vtable 0073BDB0): slots 0-3 (Read returns the bytes read), `ok_`
  (+5), `version_` (+8, 2000000000 when constructed: the save format version of the save
  stream). `StreamReadString` (00461190) became `CStream::ReadString`, moved to core_streams.cpp.
  CMemoryStream (object.h) is left to ticket C2b; it should derive from CStream.
- `core_refcount.h` `CRefObject`: slots 0-3 declared (destructor, Slot1, OnShutdown, Clone)
  instead of `void* vtable_`.
- `core_transform.h` `CTransformObject::kVtableAddress`; its constructor body moved from object.cpp
  to core_transform.cpp. `CHoover::kVtableAddress`.

### Helpers merged

- `ei::SetVtable` (include/ei/vcall.h) replaces the file-local copies in my files.
- `stlp::hash_map` gained `ClearWith(deleteNode)` (STLport's clear with the game's own node
  deleter) and access to the pieces the manager's inlined code constructs by hand (`Functors`,
  `BucketVector`, `CountProxy`). The manager still calls every original STLport / MFC
  instantiation it called before; only the raw offsets went.
- The id choice of CreateObject and RegisterInstant is one helper (`NextFreeId`); the inlined
  CPtrList::AddTail (three copies) is `AddTail`; `GameRand` is `gamecrt::rand`.
- object_diplomacy.cpp's local hash_map layout (PartyMembers, FirstNode, NextNode) is
  `stlp::hash_map<std::uint32_t, CUnit*, stlp::IntHash>` with `First` / `After` (same walk).
- VCalls replaced by virtual calls: manager slots 14 / 23 / 24, engine object slot 4 and 12, the
  streams' Read / Write (base manager, diplomacy, named float table), CRefObject slots 0, 2, 3,
  the aligned object array's slot 4 (Release).

### Left as they are (not this family, or another layer)

- `FreeSpares`: the spare list's own deleting destructor is still `ei::VCall(list, 1, 1)` (MFC
  CObject's slot 1; mfc::CTypedPtrList has no C++ virtuals).
- `CTransformObject::SaveTransform` / `LoadTransform`: MFC CFile slots 13 / 14 through VCall
  (library layer).
- object.cpp's unit and figure getters and object_diplomacy.cpp's unit fields use `FieldAt` on
  CUnit / CFigure offsets: unit.h has a CUnit layout whose names disagree with object.h's
  `unitfield` (0x24C "party" vs "player", 0x250 "player" vs "side"); left for the units ticket.
- object_tables.cpp `FindCommandByKey` reads key_bindings.cpp's tables (0079BF70, 007857B0) by
  address; it belongs with key_bindings.cpp and a shared declaration of those tables.
- texture.cpp still calls the manager's GetFile / CloseFile through `VCall(m, 19 / 20)`; it can now
  call `m->GetFile(name, nullptr)` / `m->CloseFile(data)`.
- Other files still forward to this family's rewritten functions (00403F40, 00404920/60, 00455930,
  00455940, 00455950, 00404410, ...); that is ticket C3's merge.
- The ~25 other file-local `SetVtable` copies and the vtable constants of CEngineObject in
  camera_object.cpp, lightning_start.cpp, map_object_lifetime.cpp can switch to `ei::SetVtable` /
  `CEngineObject::kVtableAddress` with their own tickets.

### Verification

Same wrappers as before (tests/verify/verify_base_manager.cpp now holds both former files);
wrappers of virtual slots call the qualified member (`self->CBaseManager::Init(...)`, ...) so
they never dispatch through the vtable back into themselves. Checked in the built objects that
the virtual calls use the original slots (Create +8, Destroy +0xC, Release +0x10, Load +0x24,
Save +0x28, NewObject +0x38, CreateObject +0x5C, ReleaseObject +0x60; stream Read +4, Write +8).

## Ticket C2b: ChunkArchive, chunk streams, CMemoryStream (2026-10-05)

Behaviour unchanged; every rewritten address keeps its hook and its verify wrapper (the wrappers'
checks are the same, only the names they call changed).

### Declarations

- `core_streams.h` `CStream` (vtable 0073BDB0): all eight slots (0 destructor, 1 Read, 2 Write,
  3 Seek, 4 Size, 5 Position, 6 Clear (memory streams empty, file streams rewind), 7 CopyFrom
  004A4D40); `reading_` (+4, the direction byte: Rewind sets it, CopyFrom clears it); the inlined
  `Rewind()` (ok, Seek(0), reading) the archive uses; `ConstructBase` / `DestructBase` /
  `DeletingDestruct` (0047A200 / 004174E0 / 00417400) are members instead of `void* self`
  functions; `kVtableAddress`.
- `CMemoryStream : public CStream` moved here from object.h (vtable 0073BD90): slots 1-7 declared
  as overrides (Read 004A5380, Write 004A53F0, Seek 004A5490, Size 00417390, Position 004173A0,
  Clear 00417350, CopyFrom 004A55D0); the fields named from what Read / Write / Seek / Resize /
  SetSize do with them: `bits_` (+0xC), `bitMask_` (+0x10, was misnamed `growBy_`: it is the bit
  writer's mask, 0x80 = start a new byte), `bitsIndex_` (+0x14, -1 = no pending byte),
  `capacity_` (+0x1C); `version_` and `ok_` come from CStream. Members `Construct`, `Destruct`,
  `DeletingDestruct`, `Reset` (moved from object.cpp with its verify wrapper), and the forwarders
  `Resize` (004A54D0, keeps the contents, grows to 1.5x + 0x10) and `SetSize` (004A5570, exact,
  drops the contents). The slot 6 body is `CMemoryStream::Clear`, hooked through
  `MemoryStreamClearHook` (rule 4).
- Both are `__declspec(novtable)` with empty inline destructors: CMemoryStream is used by value
  (locals in world_game.cpp, world_save.cpp, world_sync.cpp, net_link.cpp; members of
  netlink::QueueNode / FramedLink; ChunkArchive's two streams), and those objects get the game's
  vtables from the constructor bodies. novtable keeps C++ from storing a vtable of its own in
  the implicit constructors and destructors, so a local is plain storage as before and no C++
  vtable is ever emitted (checked: no `??_7CStream` / `??_7CMemoryStream` / `??_7Chunk` symbol in
  any object file, and the not-rewritten virtuals are declared only, so a devirtualised call
  would not link). tools/check_thunks.py and progress.py now read `class __declspec(x) Name`.
- `chunk_stream.h` `Chunk` (vtable 0073BE94): slots 0 `OnChunk` (0042BEE0, empty; ForEachChild
  calls it virtually) and 1 `File` (0042BF40, rewritten; hooked through `ChunkFileHook`) instead
  of `void* vtable_` + VCall; `kVtableAddress`. `RootChunk : public Chunk` (vtable 0073BE9C):
  `file_` (the mfc::CFile at +0x18), `File` override (0042D260, not rewritten), `Open` and
  `Destruct` (0042D160 / 0042D270; were `ChunkRootOpen` / `ChunkRootDestruct` taking a Chunk*).
  The string-array readers take the array as `void*` and use console::CStringArray inside (the
  map specs pass their own copy of that layout, mapspec::StringArray).
- New `chunk_archive.h` (included by model.h, so its users are unchanged): `ChunkArchive` laid
  out whole: `indexStream_` / `dataStream_` (two CMemoryStreams, were `streams_[0x50]`),
  `stack_` (STLport list of `StackNode {next, prev, Ref}`, was `ChunkNode` with a byte blob),
  `version_` (+0x54, was `unknown_54`, 1 when constructed), `loading_` (a byte: Open stores the low
  byte of its argument), `objects_` (+0x5C, `stlp::hash_map<uint32, CRefObject*>`, was raw +0x5C /
  +0x60 / +0x64 reads), `saved_` (+0x70, list of `SavedNode`); size 0x74 (constructor 0053A7B0).
  `Ref::Reset` (00409E80, was `ChunkRefReset`), `Current()`, `Data()`, `PopChunk()` (the stack's
  erase(--end()) LeaveChunk does). The class registry at 0079B270 is declared once as
  `ChunkArchive::ClassRegistry {classIds (vtable -> class id), factories (+0x14)}` /
  `Registry()` (was two literal addresses). Open / Close take a `CStream*`; SaveObjectRef /
  ObjectRef take and return `CRefObject*`.
- `core_refcount.h` `CRefObject` slot 1 is `Serialize(ChunkArchive*)` (was `Slot1(int)`): the
  archive's Open / Close call it on the objects they create and save.
- `model.h` `PrintRecord` / `PrintRecord2`: slot 0 `Serialize` is virtual (the vector serialiser
  called it through the vtable by hand); hooked through `PrintSerializeHook` /
  `Print2SerializeHook`; the verify wrappers call `Record::Serialize` qualified.
- `stlp::hash_map` gained `Pair` and `Iterator` (STLport's value_type and iterator layout) for
  owners that call the game's own instantiations.

### Helpers merged

- The inlined child searches in Field, EnterChunk and the field readers are calls to FindChild
  (the same loop); Open's two inlined chunk searches are FindStreamChunk, which itself calls
  ReadStreamChunk instead of a copy of it; the inlined Ref resets in Open / Close are
  `Ref::Reset`; Close's two inlined resizes are ResizeToCurrent.
- ReadStringArray constructs its child through `Chunk::Construct` (was a copy of its stores).
- Removed duplicates: the RawString layout (stlp::string), the raw CStringArray accessors and
  their forwarders (console::CStringArray), the node-free forwarder (stlp::deallocate, same null
  check), the archive's stream VCall helpers (virtual calls), the verify file's own stack-erase
  forwarder (PopChunk) and Top() (Current()).
- Every STLport / MFC instantiation the archive called before is still called; only their
  parameter types changed.

### Other files (mechanical)

- world_game.cpp, world_save.cpp, world_sync.cpp, net_link.cpp: `x.vtable_ = ...` became
  `ei::SetVtable(&x, ...)`; `unknown_8` / `unknown_1C` / `growBy_` / `unknown_14` of their
  memory streams are `version_` / `capacity_` / `bitMask_` / `bitsIndex_`;
  world_save.cpp and net_session_pump.cpp call `ConstructBase()` instead of `StreamConstructBase`.
- world_zone.cpp: the .mob root's vtable store through `ei::SetVtable`.
- unit_db.cpp: `ar->Current()->occurrence` instead of `ar->stack_->prev->occurrence`.
- net_link.h includes core_streams.h (for CMemoryStream) instead of object.h.
- verify_world_save.cpp, verify_world_zone.cpp, verify_net_link.cpp, verify_net_link_driver.cpp:
  the same renames / include.

### Left as they are

- The MFC CFile calls of the chunk streams (slots 3, 8, 10, 13, 14) are still VCalls: mfc::CFile
  declares no virtuals (library layer).
- `FpuCopyBits` (ReadVector3 copies x and z through the FPU, quieting a signalling NaN) stays until
  the x87 cleanup.
- Other subsystems' copies of these layouts: net_delta.h `BitStream` (the same layout as
  CMemoryStream, with its own non-virtual slot forwarders), netlink::FileStream (a CStream),
  script.h `ScriptStream` and world.h `WorldStream` (CStream views with their own pure slots),
  mapspec::StringArray, the CMemoryStream forwarders in net_link.cpp (004A5490, 004A54D0,
  004A5570) and world_game.cpp (004A5490, 00468C20), world_zone.cpp's forwarders to 0042D160 /
  0042D270, base_manager.cpp's MapIterator / ObjectPair (can become stlp::hash_map::Iterator / Pair),
  hash_tables_400.cpp / atexit_400.cpp's raw view of the class registry, the ~40 literal uses of
  the empty string 0079B298, and the vtable constants of CStream / CMemoryStream defined again in
  map_object_lifetime.cpp and net_message_classes.cpp. The global `kStreamVtable` /
  `kMemoryStreamVtable` names stay for the world_* / net_* code (aliases of the class constants).
- TweenArray (model_db.cpp) still reads and writes through ScriptStream.

### Verification

Same wrappers as before (CMemoryStream::Reset's moved from verify_object.cpp to
verify_core_streams.cpp). Wrappers of virtual slots call the qualified member
(`self->Chunk::File()`, `self->CMemoryStream::Clear()`, `self->Record::Serialize(ar)`). Checked in
chunk_archive.obj (release) that every stream call goes through the vtable (Clear [+0x18], Size
[+0x10], Position [+0x14], Read [+4], Write [+8], Seek [+0xC]) and the objects' Serialize [+4].
Scenarios: the archive and the prints.db / acks.db serialisers run at every startup (database.res);
the chunk streams when a zone's .mob loads (newgame); the memory streams in every save, load and
network message.
