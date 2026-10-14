# Cleanup phase 1: units (cleanup plan, ticket C2u)

How the unit family is declared after the cleanup (`unit*.h` / `unit_*.cpp`, without `unit_create_stats.cpp`) and what
is still left. The units themselves are in `units.md`; the conventions are those of `cleanup-engine-base.md` and
`cleanup-map-objects.md`. Every rewritten address kept its hook and verify wrapper.

## Declarations

- `CUnit : CMapObject` (novtable, `kVtableAddress` 0073EA8C). The layouts agreed at every offset, so CUnit's copies of
  +0x000..+0x237 are gone:
  - the prototype name is `CObject3D::name`, the figure's build `scale.y`, the old `animationCC` `scale.z`, the model
    height `defaultScale`, `animation1B4` `baseScale.z`;
  - hit points and mana are `CMapObject::packedVitals` (`UnitVitalsPacked` moved to map_object.h), the effects are
    `MapEffect` records;
  - `MarkChanged` and `HasEffect` are CMapObject's (rewritten in map_object.cpp); `MapObject()` is the upcast.
- CUnit's bodies of slots 18-51 are overrides (AdvanceTime, SetStunned, AcceptsAnimation, RecomputeScale,
  EffectsChanged, ApplyBaseScale, LoadFigures, AttachObject, StatsChanged, ScaleEffectChanged, SetStats,
  UpdateSizeClasses, PlaceOnGround, TakePrototype), each registered through a `...Hook` shim; calls that meant the body
  are qualified. `StandPoint` is not an override (it takes `float*`, the slot `Vector3*`).
- `CUnitController` slots 36 / 37 return `bool` (AL); slot 33 (`SetWeaponModels`) returns EAX, which SendEquipment
  passes on.
- Field names: `scriptName` is an `mfc::CString`; +0x254..+0x260 `animationEntry`, `secondEntry`, `actionSound`,
  `animationStart`, `motionMode`, `pathTimeShift` (setters SetAnimationEntries / SetMotionMode / SetPathTimeShift);
  +0x270 `pendingWeapon`; +0x328 `unitRadius`; +0x4B0 `actionSpeeds[6]` (from `UnitModelRecord::actionSpeeds`).
- 005327D0 is `CUnit::ActionSpeed(const FigureAnimation*)`: it reads the entry's key (bits 18-21, the action kind), not
  a hit's damage type, so it is no longer ResistanceTo.
- `SkillLevelRecord::Value()` (the exact FILD load of values[7]; three copies merged) and `WorthExponent()` (values[6]).
- Duplicates merged: unit_save's ChangePlayer is `CUnit::SetPlayer`; the level value loads above.

## Counts (unit family, before / after)

Offset access 44 / 0, VCall 22 / 0, *At views 0 / 0, `unknown_` names 400 / 340, byte offset arithmetic 23 / 15.

## Left

- `CUnit::player` is still `char*` (CPlayer, about 145 uses, with `PartyRecords` at +0x70 reached by arithmetic).
- `UnitStream` duplicates `CStream` (about 110 uses); unit_save converts with `AsStream`.
- `UnitParams` could be embedded at +0x330 (it is exactly 0x108 bytes up to +0x438); CUnit's `attackRate`,
  `movementKind`, `modeSpeeds`, `baseSpeed` repeat it and `bodyParts` is a view of its slots shifted by 0x14 (the
  maximum is unsigned there, signed in the slot), so the merge needs care.
- `carried` (+0x49C..+0x4AF) and PartyUnitRecord's four item lists could be `mfc::CArray`.
- `UnequipArmour` / `ReleaseAllItems` call controller slot 27 by hand: the original reads the vtable before evaluating
  the argument, which a virtual call would reorder.
- Item fields used as floats through the item family's names: condition `CItem::unknown_20` / `unknown_24` (unit_hits'
  `ItemFloat`, `IsBroken`), count `unknown_48`, spell cost `CSpell::unknown_40`; `UnitPrototypeRecord::unknown_24` is
  a height (compared with 1.5 for size class 0). They belong to their families' tickets.
- `CMapObjectManager` (unit_create.h) reads its world at +0xAC by hand.
- Unknowns still unnamed in CUnit: `unknown_438` (copied to the party record's +0x780), `unknown_43C` (cell flag 0x80),
  `unknown_278` (slot 54, "large unit").
- `CObject3D : CTransformObject` was left (the shared fields have other names: a large rename).

## C2ms: map specs, unit stats, the fake unit, class templates

Files: unit_create_stats, unit_vitals, unit_stats_pack, unit_skins, map_spec, map_spec_unit, map_spec_effect,
fake_unit, class_templates (and class_template_stock), hoover_loot, zone_records.

- Counts in these files (offset access / VCall / byte offset arithmetic / per-file vector-map-set struct), before
  31 / 25 / 21 / 1, after 0 / 0 / 0 / 0. Whole tree after: 95 / 35 / 18 / 6.
- Every new virtual call was checked with dumpbin: fake_unit slots 0, 1, 0x54, 0x5C; class_templates 0x48, 0x58 and
  the slot-0 delete; hoover_loot 0x48; map_spec 0x1C, 0x2C, 0x34, 0x38, 0x78; map_spec_unit 4, 8, 0x78;
  map_spec_effect 0x1C; unit_create_stats (ApplyPrototype) 0x78, 0x84, 0x8C, 0x90.
- `UnitStats` +0x064..+0x0AF are declared (`sizeClass`, `sizeClass20` / `60`, `combat[5]`, `damage`, `damageSpread`,
  `damageWeights[7]`, `naturalWeapon`); `unknown_04C` is a float (cos 90 degrees from the builders).
- `UnitPrototypeRecord`: `skillLevel`, `attributes01`, `attribute2` are floats; the gold rolls are `UnitGoldRoll gold`
  / `secondGold`. `UnitLevelRecord`: `slotType` (+0x04), `vitalMask` (+0x40). `SkillLevelRecord`: `learnable`
  (+0x40), `RequiredSkill()`, `RequiredSkillValue()`, `RequiredStat(stat)` (CanLearn).
- Class masks named in item_db.h: `SpellRecord::classMask`, `SpellModifierRecord::classMask`,
  `ItemRecordHead::classMask`, `MaterialRecordHead::classMask`.
- `ClassTemplate::ItemsParty()` / `SpellsParty()` (CParty is abstract, so the parties stay byte storage).
- `mfc::CObjectVirtuals` (GetRuntimeClass, the deleting destructor, Serialize) for MFC objects called through their
  vtable; `scene::FakeUnitScene` views the fake unit's scene arrays.
- `DiplomacyConstruct` takes `ZoneDiplomacy*`.
- unit_skins' `TextureSet` (the texture cache's composition request, not an STL set) is `SkinComposition`.
- New members shadowed locals named `damage` / `combat` in UnitStats methods (C4458): unit_hits' DamageSlot and
  ApplyHit use `typeDamage`; UnitStats::Reset writes the members.

Left:
- combat.h's `CombatStats` overlay repeats UnitStats +0x64..+0x10F (`Combat()` reinterprets the stats); its users
  include script_addon_commands.cpp, so the merge waits for that file's owner.
- map_spec_unit's `UnitStatsDestruct` (0046C2F0) reaches the five lists from UnitSpec +0x210 by arithmetic on a byte
  pointer (not counted by the check). Embedding a block struct in UnitSpec touches world_units.cpp.
- unit_create_stats keeps its `__asm` (FpuCopy2, GameFtol) and raw addresses (the record tables, 0x007846F0 "none").
- `SceneHelper` / `CFigureMesh` declare no virtuals; `ClassTemplate` does not derive from CRefCounted (the delete
  goes through a reinterpret_cast); party_manager.cpp still reinterpret_casts the template parties.
- The effect spec's world object class is not known (CTransformObject / CEngineObject fields used).
