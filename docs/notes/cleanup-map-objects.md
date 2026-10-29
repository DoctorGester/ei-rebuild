# Cleanup phase 1: map objects (cleanup plan, ticket C2m)

How the map object family is declared after the cleanup (`map_object*.h` / `map_object*.cpp`, with the companions in `unit.h`) and what is
still left. The objects themselves (layouts, slots, behaviour) are in `map_objects.md`. The conventions are the ones in
`cleanup-engine-base.md`.

## Declarations

- `CMapObject : CObject3D` (novtable, `kVtableAddress` 0073DAA0). CObject3D holds the first 0x170 bytes (`pointOffset` is `center`, `extent` is
  `above`). Slots 13-61 are declared virtual in vtable order; a slot declared as `void Name()` keeps only its place. The slots the rewrite owns
  are the virtuals themselves, each registered through a `...Hook` shim: AttachObject 19, RecomputeScale 27, TakePrototype 34, SetServerData 36,
  ApplyScience 41, AddEffect 45, PlaceOnGround 51; `Lever::KeyId` (40) and `MagicTrap::ForgetObject` (56) are overrides, also registered through
  shims. `Lever::UseSpots` is `CollectUseSpots` (not a slot). `CMapObjectVirtuals` is an alias of CMapObject,
  `ObjectSlots()` returns `CMapObject*`; member +0x178 is `animationSpeed`, the same field as `CUnit::animationSpeed`.
- `CMapObjectCompanion` (novtable, 0073DC54; slots 13-19, `owner` at +0x1C) is the base of `CMapObjectServer` (0073DD6C) and
  `CMapObjectClientSpecific` (0073DBEC; reads the owner as a unit through `Unit()`). Both classes' IsDead / IsOverkilled (slots 17 / 18) are
  overrides with shims in `unit.cpp`. The base's Destruct / Delete / Create / Destroy / Update are members (`DestructCompanion`,
  `DeleteCompanion`, ...).
- `CObject3DClientSpecific : CObject3DSpecific` (map_object_shadow.h; `CObject3D::ClientSpecific()` is a static_cast). Its own bodies are
  named apart from the base's slots so none overrides one (rule 4): ApplyWorldMaterials, SetMaterialsLit, DirtyRectContains, PartRectsContain,
  PartRectsMeet; `FindPart` / `SetLit` on it are the base's slots 16 / 26.
- `CObject3DClientSpecific::Draw` / `DrawShadow` take the `CSceneRenderer*` (model.h). Draw uses the pipeline's matrix stacks (PushMatrix,
  PushMatrixProduct, PopMatrix on stacks 1-3); the shadow uses stack 10. `FigurePart` has `orientation` (+0x34) and `worldMatrix` (+0x54);
  `ObjectWorldMatrix` is shared by the draw and the shadow. `CTorchObject::Create` is a member (`map_object_create.cpp`).
  `CObject3D::figure` is a `Ref<CLinkMesh>` (`figure.Assign` is 00519580) and `DirtyRect()` replaces hand-made casts. `LightManager()` is one copy in map_object.h; the
  client file uses spell.h's `TheLightFactory()->litIds_`. The shadow's naked `Fpatan` is `x87::fpatan`.

## Behaviour notes

- Now that CMapObject's slots are virtual, a derived class that runs the map object's own body must call it qualified (`CTorchObject::
  AttachTorchObject` calls `CMapObject::AttachObject`). Called unqualified, the slot dispatches back to the override: this recursion once killed
  every scenario at the first torch.
- The client's slot 13 and 19 bodies keep names other than the virtuals: `OnEffectsChanged` takes a `bool`; `ApplyLightMode` sets no result,
  while the slot passes EAX on. Declaring them as the overrides would have changed the argument or the return value.

## Left

- Remaining VCalls: `map_object_figures.cpp` (the figure cache's slot 6, because slot 5 is unknown); `map_object_shadow.cpp` (the 2DintMMX
  device's slots 11 and 12, +0x2C / +0x30).
- `CObject3D : CTransformObject` (C15 batch 9): the transform fields are `localPosition_` (+0x1C), `worldPosition_` (+0x28, the
  position in view space after CWorldClient::CollectVisible), `localRotation_` (+0x34) and `worldRotation_` (+0x44); CObject3D overrides
  only slot 14 (CombineRotation, 00516370). CParticleSource derives from CTransformObject too. CMapObject's `SaveTransformSlot` /
  `LoadTransformSlot` (00516350 / 00516360) stay non-virtual bodies.
- The effect array is `CMapObject::effects`, a `std::vector<MapEffect>`. `CMapObject::updateCountdown` (+0x174, 5 after Create): world updates until the effects are next looked at. `MaterialStage::
  unknown_00` is unnamed (cleanup-figures.md). `CompanionOwner()` is still used by `map_object_client_effects.cpp`, whose locals cache the owner.
- `ShowHit` (slot 16) and the server's `TakeHit` (slot 20, past the declared slots) are not overrides. These stay free functions registered at
  several addresses: the shared stub bodies in `map_object_stub_slots.cpp` and the companion's empty slots (`CompanionIgnore`,
  `CompanionNothing`, `CompanionHit`).
- Forwarders that could become direct calls: `LightViewMatrix` / `LightRotation` (004F1440 / 004F15C0) in the shadow; `CallCreate3D` is kept
  deliberately so the verify wrapper runs.
