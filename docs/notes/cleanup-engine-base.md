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
