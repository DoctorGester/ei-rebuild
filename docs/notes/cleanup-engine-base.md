# Cleanup phase 1: engine base (cleanup plan subsystem 1)

How the engine-base classes are declared after the cleanup (headers: `engine_object.h`, `base_manager.h`, `resource_manager.h`,
`core_streams.h`, `chunk_stream.h`, `chunk_archive.h`, `core_refcount.h`, `core_transform.h`, `camera.h`) and what is still left. The
classes themselves (layouts, slots, behaviour) are in `core.md`. Behaviour was unchanged by every step: each rewritten address kept its
hook and verify wrapper.

## Conventions used (follow them for the next families)

- Classes whose objects the game builds (or that are used by value as locals) are `__declspec(novtable)` with empty inline destructors: C++
  never stores a vtable of its own in them, the game's constructor bodies set the real vtables, `kVtableAddress` names the address
  (`ei::SetVtable` stores it; one shared helper replaces the file-local copies), and the not-rewritten virtuals are declared only so a
  devirtualised call would not link. Checked in the objects: no `??_7CStream` / `??_7CMemoryStream` / `??_7Chunk` symbol is emitted and
  every stream / manager call goes through the original slot (manager Create +8, Destroy +0xC, Release +0x10, Load +0x24, Save +0x28, NewObject
  +0x38, CreateObject +0x5C, ReleaseObject +0x60; stream Read +4, Write +8, Seek +0xC, Size +0x10, Position +0x14, Clear +0x18; Serialize +4).
  `tools/check_thunks.py` and `progress.py` read `class __declspec(x) Name`.
- Virtual slots are declared in vtable order and registered through `...Hook` shims (rule 4); the verify wrappers of virtual slots call the
  qualified member (`self->CBaseManager::Init(...)`, `self->Chunk::File()`, `self->CMemoryStream::Clear()`, `self->Record::Serialize(ar)`) so
  they never dispatch back into themselves. Non-virtual members are registered with `EI_REPLACE_METHOD`.
- Calls that went through forwarders to rewritten functions are direct calls; the release build runs the same code, the verify build no longer
  runs those wrappers for those callers (a deliberate trade, noted per ticket).
- `stlp::hash_map` grew `ClearWith(deleteNode)`, `Functors` / `BucketVector` / `CountProxy` (the pieces the manager's inlined code constructs
  by hand), `Pair`, `Iterator`, `First` / `After` (STLport's begin() and iterator ++); the game's own STLport / MFC instantiations are still
  called, only the raw offsets went.
- Duplicate helpers merged: `NextFreeId` (CreateObject and RegisterInstant), `AddTail` (three inlined copies), `gamecrt::rand`, FindChild /
  FindStreamChunk / ReadStreamChunk / `Ref::Reset` / ResizeToCurrent inside the archive, `Chunk::Construct` in the string-array readers,
  `game::EmptyText()` / `kEmptyTextAddress` for 0079B298 (the ~40 literal uses and per-file copies are gone), `camera::` names for the camera's
  globals and `CAM_*` macros for its exe constants.

## Declarations

- `CEngineObject`: slots 2, 3, 11, 12 pure (`_purecall` in 0073B8E0), `owner_` a `CBaseManager*`, non-virtual `DeletingDestruct` (00402D20) and
  `ClearFields` (the inlined constructor's stores, shared by CBaseManager, CTransformObject and CHoover).
- `CBaseManager`: the whole layout named (tables each followed by their highest key), slots 13-24 virtual in order; `ObjectListIterator` lives
  here. `CResourceManager : CBaseManager` (slots 3 and 16-22 overrides; `CloseFile` returns `bool`).
- `CStream` (all eight slots; `reading_`, `ok_`, `version_`; `Rewind()`), `CMemoryStream : CStream` (slots 1-7 overrides; `bits_`, `bitMask_`
  (was misnamed `growBy_`: the bit writer's mask), `bitsIndex_`, `capacity_`; `Resize` / `SetSize` / `Seek` forwarders), `BitStream` = `CMemoryStream`,
  `ScriptStream` and `WorldStream` = `CStream`. `Chunk` (slot 0 `OnChunk`, slot 1 `File`), `RootChunk` (`file_`, `Open`, `Destruct`),
  `ChunkArchive` laid out whole (`indexStream_`, `dataStream_`, `stack_`, `version_`, `loading_`, `objects_`, `saved_`; `ClassRegistry` at 0079B270 declared
  once). `CRefObject` slot 1 is `Serialize(ChunkArchive*)`; `PrintRecord` slot 0 `Serialize` is virtual. `CCamera` (`camera.h`) has named members for
  what was raw offsets (view stack, near / far / fov, file name, flags), `LoadState` / `SaveState` take a `CStream*`.

## Left

- `FreeSpares`: the spare list's own deleting destructor is `ei::VCall(list, 1, 1)` (MFC CObject slot 1; `mfc::CTypedPtrList` has no C++ virtuals). The
  MFC `CFile` slots (3, 8, 10, 13, 14) in the chunk streams, `SaveTransform` / `LoadTransform` and the file streams stay `VCall`s (library layer).
- `texture.cpp` still calls the manager's GetFile / CloseFile through `VCall(m, 19 / 20)`; it can call the members now.
- `unit.h` and `object.h` disagree on the unit's names at +0x24C (`party` / `player`) and +0x250 (`player` / `side`); units ticket.
  `object_tables.cpp` `FindCommandByKey` reads the key-binding tables (0079BF70, 007857B0) by address (belongs with `key_bindings.cpp`).
- File-local `SetVtable` copies remain in 13 files (camera_object, fake_unit, item_constructor, lightning_start, magic_trap, map_object_lifetime,
  map_spec*, net_coder, particle_create, resource_stream, ui_layouts); `FpuCopyBits` (ReadVector3's FPU copy of a signalling NaN) stays until the x87
  cleanup.
- Other layouts of these streams: `UnitStream` (`unit.h`), the `SaveStream` structs in `ui_battle_setup.cpp` / `ui_global_map.cpp`, the unit_ai stream,
  `verify_magic_trap`'s wrapper, the remaining per-file `Text(0x...)` helpers, `TweenArray` (`model_db.cpp`, reads and writes through `ScriptStream`),
  `hash_tables_400` / `atexit_400` raw views of the registry.
- Duplicate forwarders seen: 00411850 (`ui_battle_update.cpp` `UnitListConstruct`), 006E6653 (`ui_saveload.cpp`, `world_game.cpp`), 0070133F
  (`texture_compose.cpp`, `ui_saveload.cpp`), 00700AA9 / 00700970 (`net_*.cpp`), `CIacos` (`core_transform_math.cpp`, `quaternion.cpp`), `CIpow`
  (`ui_hud_map.cpp`, `ui_item_info.cpp` inline asm), `NodeFreeList` (4 files; `stlp::SmallNodeFreeList` is the same address); 007AF738 is the
  terrain here but `LightManager()` elsewhere. (006FDA6D is `Right(nCount)`, 006FD9B4 `Mid(nFirst)`: core.md "MFC CString: Mid and Right".)
