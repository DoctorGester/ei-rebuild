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
- Field names: `scriptName` is a `std::string`; +0x254..+0x260 `animationEntry`, `secondEntry`, `actionSound`,
  `animationStart`, `motionMode`, `pathTimeShift` (setters SetAnimationEntries / SetMotionMode / SetPathTimeShift);
  +0x270 `pendingWeapon`; +0x328 `unitRadius`; +0x4B0 `actionSpeeds[6]` (from `UnitModelRecord::actionSpeeds`).
- 005327D0 is `CUnit::ActionSpeed(const FigureAnimation*)`: it reads the entry's key (bits 18-21, the action kind), not
  a hit's damage type, so it is no longer ResistanceTo.
- `SkillLevelRecord::Value()` (the exact FILD load of values[7]; three copies merged) and `WorthExponent()` (values[6]).
- `unitrecord::UnitRecord` (unit_record.h) is the tail of a .mob unit record (`mapspec::UnitSpec::record`, +0x210,
  0x114 bytes): `UnitRecordStats` (chunk BBBB0004, 0xAC bytes, typed fields), the five item name lists and
  `applyStats` (chunk BBBB000A). ApplyTo (00477B00) writes the server companion's `vitals` and the controller's
  `stats` by name; 0046C2F0 is `UnitRecordDestruct`. The record's +0x54 copies 7 bytes into `UnitStats::damageWeights`
  (as the original does).
- Duplicates merged: unit_save's ChangePlayer is `CUnit::SetPlayer`; the level value loads above.

## Left

- `CUnit::player` is still `char*` (CPlayer, about 145 uses, with `PartyRecords` at +0x70 reached by arithmetic).
- `UnitStream` duplicates `CStream` (about 110 uses); unit_save converts with `AsStream`.
- `UnitParams params` is CUnit's member at +0x330 and `UnitMotion motion` at +0x27C (unit_motion.h, which unit.h
  includes; ai_motion.h keeps OrderOf). The unit panel's "body parts" (+0x374) are the params' slots' kind / current /
  maximum (the maximum is unsigned: the panel, the wounds and the network fraction read it so).
- `carried` (+0x49C..+0x4AF) and PartyUnitRecord's four item lists are `std::vector`s.
- `UnequipArmour` / `ReleaseAllItems` call controller slot 27 as `ArmourTakenOff`: the original reads the vtable before
  the item's GetType, the virtual call after it; GetType reads the item only, so nothing can tell.
- Item fields used as floats through the item family's names: condition `CItem::condition_` / `conditionMax_` are floats
  (unit_hits' WearItem and ApplyHit / DamageSlot take the `CItem*`), count `count_`, spell cost `CSpell::unknown_40`; `UnitPrototypeRecord::unknown_24` is
  a height (compared with 1.5 for size class 0). They belong to their families' tickets.
- `CMapObjectManager` (unit_create.h) reads its world at +0xAC by hand.
- Unknowns still unnamed in CUnit: `unknown_438` (copied to the party record's +0x780), `unknown_43C` (cell flag 0x80),
  `unknown_278` (slot 54, "large unit").
- `CObject3D : CTransformObject` was left (the shared fields have other names: a large rename).

## C2ms: map specs, unit stats, the fake unit, class templates

Files: unit_create_stats, unit_vitals, unit_stats_pack, unit_skins, map_spec, map_spec_unit, map_spec_effect,
fake_unit, class_templates (and class_template_stock), hoover_loot, zone_records.

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
- `ClassTemplate::ItemsParty()` / `SpellsParty()` (the parties are `PartyStorage`, party_storage.h: the template's Construct / Destruct build them).
- `DiplomacyConstruct` takes `ZoneDiplomacy*`.
- unit_skins' `TextureSet` (the texture cache's composition request, not an STL set) is `SkinComposition`.
- New members shadowed locals named `damage` / `combat` in UnitStats methods (C4458): unit_hits' DamageSlot and
  ApplyHit use `typeDamage`; UnitStats::Reset writes the members.

Left:
- combat.h's `CombatStats` overlay repeats UnitStats +0x64..+0x10F (`Combat()` reinterprets the stats); its users
  include script_addon_commands.cpp, so the merge waits for that file's owner.
- unit_create_stats keeps its `__asm` (FpuCopy2, GameFtol) and raw addresses (the record tables, 0x007846F0 "none").
- `SceneHelper` / `CFigureMesh` declare no virtuals; `ClassTemplate` does not derive from CRefCounted (the delete
  goes through a reinterpret_cast); party_manager.cpp still reinterpret_casts the template parties.
- The effect spec's world object class is not known (CTransformObject / CEngineObject fields used).

## F2: unknown fields of the units, AI and items files (2026-10-14)

Every `unknown_XXX` member of unit.h, unit_db.h (+ unit_db_destroy.cpp), unit_ai.h, ai_map.h, item_db.h,
unit_looks.h, item.h, unit_client_models.h, class_templates.h, unit_client_sounds.cpp, party*.h, item_stock.cpp,
text_manager.h, ai_shape_cache.h, ai_refresh.cpp and unit_ai.cpp (172) is named or `unused_XXX`. Method: all of
them renamed to unused_ and the verify build run with `-k 0`; each compile error is a use, named from its code.
- Meanings worth knowing: CUnit +0x438 `fightAtWill` (the move panel's flag; PlayerMode attacks only at 1; the
  party record keeps it at +0x780, SetUnknown438 / 0052E9B0 sets it); +0x278 `obstacle` (sector map, slot 54);
  +0x43C `altPassCosts` (set from the cell's flag 0x80 before a path search). UnitStats +0x60 `movementKind`
  (model field 5; 2 = on the water), +0x44 `sightCone`, +0x48 `peripheralRange` (prototype field 0x18). The
  prototype's +0x24 (field 0x23) is the unit's `height` (panel view, reach checks); +0x54 `experienceValue`.
  AIBrain +0xF4 `nearby`, +0x12C `seenCorpses`, +0x36 `alarmSlot`, +0x37 `alwaysUpdated`, +0x44/+0x48
  `nearX`/`nearY`. Stream +0x04 is CStream::reading_ (`reading`).
- Archive fields nothing reads keep the archive's field number as their name (`field4`, `field1F`, ...), the
  item records' partly used byte gaps are split the same way (WeaponRecord `field16` / `field1B`, ArmorRecord
  `field19` / `field1A`, QuickItemRecord `field17` / `field18` / `field1B`).
- `refsHigh` is the high word of the reference dword the original zeroes with the count (AIMode, CItemLink,
  ClassTemplate, PartyRecord). `spare*`, `sideCone`, `lightStateA/B`, `otherSound`, `spareHandle`, `zeroed`
  are only ever written (zero / -1).
- unit_db_destroy.cpp's local `SkillLevelRecord` (0x18) / `SkillRecord` (0x44) are unit_db.h's SkillRecord /
  SkillLevelRecord (the names are swapped there; the layouts and the destructor order are right).
- AIEvent's creator 005C9630 has no callers in the Ghidra database (+0x08 is x, y again; +0x14 only saved).

## C11: shims to members, constants with homes (conventions that came out of 14 batches)

The batches (2026-10-14/15) moved `__fastcall Fn(T* self, void*, ...)` shims into members and replaced file-local address
constants by typed homes; all spelling only (git log has the batch commits). What a worker needs:

- **Members.** A shim's body moved into a member. The `<Slot>Impl` members (the body of a virtual slot as a distinct
  non-virtual member, the virtual declared and never defined) exist because a virtual could not be registered (rewrite
  guide rule 4); C28 folds them into real virtuals. Deleting destructors are `DeleteScalar` (slot 0, RET 4,
  `flags` bit 0 frees; the precedent is `CMapObject::DeleteMapObject`); empty slots have distinct names (`IgnoreArgument`,
  `ReturnZero`, `NothingMore`, ...). Overloaded names cannot be registered (CScript::Load / Save became LoadState /
  SaveState). Slots that return a string build it in the caller's result: a member returning it by value is the same
  hidden-pointer ABI. Where a shim took a pointer the original dereferenced, the member takes a reference.
- **Moving a body into a member.** `self->x` becomes `x`, or `this->x` when a local or parameter of that name exists;
  parameters and locals that hide a data member (C4458) are renamed (`In` suffix, `newName`); a member of a class in a named
  namespace needs qualified helper names, and a helper with the same name as a member needs `::`. The qualified-call rename
  does not see dependent names (`self->Record::Serialize` in a template) or calls on value objects: the linker names them.
- **What stays a shim**: shims on aliases of library templates (no member can be added); `void*` / `uint8_t*` selfs
  and pointer-to-pointer selfs; the `!self` tests of the CInterface3D scale entries (ECX 0 is reachable with Rebirth's
  code); `EquipBuild` / `QuickStripBuild` (`int` against `bool`); shims whose return value differs from the slot's
  (CMemoryStream::Write returns `position_`, DriverLink::SendBytes `bool ? 1 : 0`); Rebirth-patched input handlers
  (0063A560, 00630010, 0062F950: check_patched_calls resolves them by symbol name); the `XxxOriginal(a0, nullptr, a2)`
  call-through blocks (no body to move). UI input shims are registered as `InputForward<&Class::Method>::Call` and
  `UiInput::DeleteWindow<&Class::Delete>` (ui_screens.h: the self is the input sub-object, the window found with
  `WindowFromInput`). Classes added for the factories: `CClientSpecificManager`, `CEffectManager` (base_manager.h).
- **Container Abi types are gone.** The src/lib `*Abi` wrappers and the serialiser templates went with the MFC and STLport
  containers (C14); check_cleanup forbids new `...Array` / `...List` / `...Map` struct names in game files, so a
  `using OldName = New;` follows a renamed type.
- **Constants with homes.** Vtables are the compiler's (C19, C28). Texts are accessors in `game_strings.h`
  (`game::mapwords`, `questwords`, `savewords`, `briefingwords`, `zonetexts`, `equipmenttexts`, `menutexts`, `nettexts`,
  `menukeytexts`, `consoletexts`, `weathertexts`; they return `Text(address)` so ei_vectors reads the stand-in data); floats
  are `worlddata::One()` / `Zero()` / `Half()` and the `const float&` (`ExeFloatRef`) effect constants in world_data.h;
  globals are accessors (`worlddata::InterfaceManager()`, `WorldServer()`, ...) or named (`g_gameFlow`, `g_skillDatabase`). unit_vitals'
  constants are the `hpVal`, `mpVal` and `damageLevel*` members of `LogicSettings` at 007C53F8. `RecordTable::At` is
  `RecordAt`, `CMap::At` is `ElementAt`; `ei::OffsetPtr(base, offset)` (game_data.h) replaces own `At()` helpers.
- **Callbacks by function.** Function-local statics register their exit functions with `std::atexit` themselves
  (at_exit_functions.h went in C20r: nothing used its declarations); the eh-vector iterators take `void*` and the
  functions themselves.
- **Left raw**: no `constexpr uintptr_t kX = 0x00...` in src (tools/progress.py 0, C11 batch 33). game.exe's text
  bounds are `ei::kGameTextBegin / kGameTextEnd` (include/ei/hook.h; game.exe's WinMain is hook.cpp's, for the harness);
  `game::EmptyText()` returns the named `game::kEmptyText` (0079B298, C18).
- **Library forwarder wrappers.** One-line file-local wrappers over the `originals_library.h` forwarders were inlined; kept:
  wrappers with a typed result (`(Node*)ListNewNode...Original`), integer-to-pointer casts, `const_cast` / `static_cast`
  arguments, wrappers used as function pointers (hash table `FindOrInsert(..., ObjectsResize, ObjectsNewNode)`), and every
  wrapper over a forwarder of another header (the party / player / world / combat / ui names are shorter and more readable).
- Last measured: `__fastcall` shims with a `void*` dummy 632 (from 1,274); method registrations 4,774; golden runs all
  PASS, differing only in the script `#gobject` field (heap layout, SV7's) on a few.
