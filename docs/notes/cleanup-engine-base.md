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

- `unit.h` and `object.h` disagree on the unit's names at +0x24C (`party` / `player`) and +0x250 (`player` / `side`); units ticket.
  `object_tables.cpp` `FindCommandByKey` reads the key-binding tables (0079BF70, 007857B0) by address (belongs with `key_bindings.cpp`).
- File-local `SetVtable` copies remain in 18 files (grep `void SetVtable`: ai_unit_shape, fake_unit, figure_factory, lever_object, lightning_start,
  map_spec*, net_coder, particle_create, resource_stream, script_runtime_helpers, torch_object, ui_camp_create, ui_create_screen, ui_exchange,
  ui_hero_screen_create, ui_layouts); `FpuCopyBits` (ReadVector3's FPU copy of a signalling NaN) stays until the x87
  cleanup.
- C5 (folded): `UnitStream` and the unit_ai `AIStream` are `CStream` (members `reading_` / `version_`; the verify capture, recording and replay
  streams derive from `FakeStream`); the `SaveStream` structs of `ui_battle_setup.cpp` / `ui_global_map.cpp` are `CMemoryStream` at 007AFDF8;
  `verify_magic_trap`'s `ReadRecorder` is `unitverify::ReadRecorder`; the 101 per-file string helpers (`Text`, `GameText`, `GameString`,
  `TextAt`, `Letter`, and `console::GameText` / `device::Text` / script_parse's / spell_effect_internal's) are `game::Text` (`game_strings.h`).
- Left: `TweenArray` (`model_db.cpp`, reads and writes through `ScriptStream`), `hash_tables_400` / `atexit_400` raw views of the registry,
  the two `LocalFileStream` RAII wrappers (`ui_connect_screen.cpp`, `ui_saveload.cpp`; different interfaces over `netlink::FileStream`), the
  per-file accessors of the save stream 007AFDF8 (`SaveGameStream` in `ui_battle_slots.cpp` / `ui_briefing.cpp`, `SaveStream` in
  `ui_briefing_create.cpp`, the same in tests/verify) and the identical `SaveRecorder` / `RecordingStream` test classes.
- Duplicate forwarders: `config/forwarder_duplicates.txt` and docs/originals-report.md. 007AF738 is the terrain in this layer
  but `LightManager()` elsewhere.

## C2z: the app files and the scattered rest of the gate

Offset access, `VCall`, byte offset arithmetic, per-file vector struct and `SlotNN` are 0 in every file under `src/` except `src/render/`
(4 byte arithmetic, renderer ticket). Duplicate forwarders stay 0. Every new virtual call must be checked in the release objects (dumpbin):
none may be devirtualised.

- `app_frame`: the device object is `CTextureDevice` (`RenderDeviceObject()`: `primary_`, `renderTarget_` width / height, `screen_`,
  `lastLoadResult_`); the screen clear calls `IDirectDrawSurface7::Blt` with a `DDBLTFX`; the captured `CDib` is deleted through
  `mfc::CObjectVirtuals` slot 1 (`CDib` keeps its vtable as a field; its `Delete` is registered by address, so it is not a C++ virtual).
- `app_registry`: prototypes and counted pointers are `CRefCounted` (`delete` = slot 0 with 1, as `core_refcount.cpp`); WinMain's COM
  pointers are `IUnknown`. Signatures (`void*`) kept for the verify wrappers.
- CFile slots through `mfc::CFileVirtuals` (`chunk_stream`, `core_transform`'s Save / LoadTransform, `camera_object`, which also uses
  `ei::SetVtable`); `FreeSpares` deletes the spare list through `mfc::CObjectVirtuals`.
- `refptr_copies`: `RefPtrRelease` deletes on `CRefCounted`; the object holders take `CEngineObject**` (`Release()` slot 4, `refCount_`),
  `world_deleting_destructors.cpp` and the verify wrappers follow. `party_records`: the record's party `Create()` (slot 2) and `delete` on the
  record as a `CRefCounted` (base vtable 0073BB68).
- Named slots: `CPixelConverter` 1 `SetChannelMasks`, 10 `DecodeDxt`, 11 / 12 `BlurShadow` / `BlurShadow2`; `CFigureCache` 5 `Clear` (004F7020), 6
  `ScaledFigure` (004F75E0) (now `CLinkFigureLoader`, cleanup-figures.md); `CUnitController` 21 `Heal(amountBits)` returning int (the amount is
  pushed as bits, as the original does); `CMappedResource` 4 `Flush` (hook `MmioFlushHook`); `GlobalMapWeather` 0 `NoiseOctave` / 1 `BuildStep`;
  `AIStream` 0 `DeletingDestruct` (the verify streams' overrides renamed).
- `menu_rects` calls the owner's `CBaseManager::GetFile` (slot 19), `text_reader` the archive's `CMappedResource::GetFile` (slot 5),
  `effects_weather` the world's `SurfaceHeightAt` (slot 30; the double result plus 0.5 rounded once, as the original adds in ST0 and stores).
- `CParticleSource::orientation` (+0x34, the identity after Reset); the emitters read the attachment's `CObject3D::orientation`; the sound
  manager's `emitters[index]`; `app_device_list` reads `ChunkArchive::loading_` and `Current()->occurrence`.
- `LeverRecord` (lever.h) is the only declaration of the lever table's record (CString members, spots as floats). bink_video.cpp's
  `SoundSystem` view is `CSoundManager` (`waveDriver`, +0x1C); `LocalViewData::partyTaken` is `sendParty` (CPlayerClient::sendParty_, the same
  byte).