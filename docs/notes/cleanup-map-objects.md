# Cleanup phase 1: map objects (cleanup plan, ticket C2m)

How the map object family is declared after the cleanup (`map_object*.h` / `map_object*.cpp`, with the companions in
`unit.h`) and what is still left. The objects themselves (layouts, slots, behaviour) are in `map_objects.md`. Every
rewritten address kept its hook and verify wrapper. The conventions are the ones in `cleanup-engine-base.md`.

## Declarations

- `CMapObject : CObject3D` (novtable, `kVtableAddress` 0073DAA0). CObject3D holds the first 0x170 bytes, so the old
  duplicate fields are gone: `pointOffset` is now `center` and `extent` is now `above`. `Object3D()` is just the upcast.
  - Slots 13-61 are declared virtual in vtable order. A slot declared as `void Name()` keeps only its place; its
    arguments are not declared yet.
  - The slots the rewrite owns are the virtuals themselves, each registered through a `...Hook` shim: AttachObject 19,
    RecomputeScale 27, TakePrototype 34, SetServerData 36, ApplyScience 41, AddEffect 45, PlaceOnGround 51.
  - `Lever::KeyId` (40) and `MagicTrap::ForgetObject` (56) are overrides, also registered through shims.
  - `Lever::UseSpots` became `CollectUseSpots`. It is not a slot, but `check_thunks` matches by name.
  - `CMapObjectVirtuals` is still an alias of CMapObject, and `ObjectSlots()` returns `CMapObject*`.
  - Member +0x178 is `animationSpeed`, the same field as `CUnit::animationSpeed`.
  - `mapslot` keeps only `kResetScale`, for the verify wrapper of RecomputeScale.
- `CMapObjectCompanion` (novtable, 0073DC54; slots 13-19, `owner` at +0x1C) is the base of two classes, each with its
  `kVtableAddress`:
  - `CMapObjectServer` (0073DD6C);
  - `CMapObjectClientSpecific` (0073DBEC). The client reads the owner as a unit through `Unit()`.
  - Both classes' IsDead / IsOverkilled (slots 17 and 18) are overrides with shims in `unit.cpp`.
  - The base's Destruct / Delete / Create / Destroy / Update are members: `DestructCompanion`, `DeleteCompanion`, and
    so on.
- `CObject3DClientSpecific::Draw` / `DrawShadow` take the `CSceneRenderer*` (model.h).
  - Draw uses the pipeline's matrix stacks: PushMatrix, PushMatrixProduct and PopMatrix on stacks 1-3.
  - The shadow uses stack 10.
  - `FigurePart` has its `orientation` (+0x34) and `worldMatrix` (+0x54).
  - `ObjectWorldMatrix` is shared by the draw and the shadow.
- `CTorchObject::CreateTorch` is a member, defined in `map_object_create.cpp`.
- `CObject3D::FigureRef()` (the counted figure) and `DirtyRect()` replace the hand-made casts.
- Duplicates merged:
  - the companion's EffectSound copy;
  - `AssignCounted`;
  - the vtable constants of CObject3D, CMapObject and the companions;
  - `LightManager()`, now one copy in map_object.h instead of four;
  - the client file's private `LightFactory` view, replaced by spell.h's `TheLightFactory()->litIds_`.
- The shadow's naked `Fpatan` is now `x87::fpatan`.

## Behaviour notes

- Now that CMapObject's slots are virtual, a derived class that runs the map object's own body must call it
  qualified. For example, `CTorchObject::AttachTorchObject` calls `CMapObject::AttachObject`. Called unqualified, the
  slot dispatches back to the override: once this recursion killed every scenario at the first torch.

- Calls that used to go through forwarders to rewritten functions are now direct, the convention's verify-build trade.
  The draw now calls PushMatrix, PushMatrixProduct and UpdatePose directly.
- The client's slot 13 and 19 bodies keep names other than the virtuals:
  - `OnEffectsChanged` takes a `bool`;
  - `ApplyLightMode` sets no result, while the slot passes EAX on.

  Declaring them as the overrides would have changed the argument or the return value.

## Left

- Remaining VCalls:
  - `map_object_figures.cpp`: the figure cache's slot 6, because slot 5 is unknown;
  - `map_object_shadow.cpp`: the 2DintMMX device's slots 11 and 12 (+0x2C / +0x30).
- `CObject3D : CTransformObject` is not declared yet; `Transform()` reinterprets the object. The names differ:
  - `position` is `localPosition_`, `viewPosition` is `worldPosition_`;
  - `orientation` is `localRotation_`, `worldOrientation` is `worldRotation_`.

  CMapObject also declares slots 13-17 as placeholders, while CTransformObject has the real signatures.
- CUnit (`unit.h`) repeats the 0x000-0x238 layout instead of deriving from CMapObject.
- The effect array (+0x194) is still five raw fields (`effectsVtable` ... `effectGrowBy`) instead of an `mfc::CArray`,
  with about 58 uses outside the family.
- `CMapObject::updateCountdown` (+0x174, 5 after Create): world updates until the effects are next looked at (F4).
- Other unknown fields: `MaterialStage::unknown_00` (F4 named or marked unused the rest; cleanup-figures.md).
- `CompanionOwner()` is still used by `map_object_client_effects.cpp`, whose locals cache the owner between calls.
- `ShowHit` (slot 16) and the server's `TakeHit` (slot 20, past the declared slots) are not overrides.
- These stay free functions registered at several addresses: the shared stub bodies in `map_object_stub_slots.cpp` and
  the companion's empty slots (`CompanionIgnore`, `CompanionNothing`, `CompanionQuery`, `CompanionHit`).
- Forwarders that could become direct calls:
  - `LightViewMatrix` / `LightRotation` (004F1440 / 004F15C0) in the shadow;
  - `CallCreate3D`, kept deliberately so the verify wrapper runs.
- `FigurePart` and `CFigureMesh` (figure_mesh.h) describe the same part: merge them.
- `ColourKey` in the shadow reads the colour key from `CTexture::block_` (+0x60) through a `bit_cast`; the field should
  get a name in texture.h.
