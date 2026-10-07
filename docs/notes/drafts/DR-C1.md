# DR-C1: cleanup draft of world_client_maps.cpp, world_units.cpp, world_game.cpp

Draft only: NOTHING HERE HAS BEEN COMPILED (the machine was short of memory; no build, no
python checks, no game). The next worker must build `build.cmd verify` / `release`, fix what
does not compile, run check_cleanup / check_thunks / check_signatures, then
`python tools/check_cleanup.py --update`.

Baseline counts (config/cleanup_baseline.csv) before: world_client_maps 160 At / 39 VCall / 27 raw;
world_game 155 / 3 / 70; world_units 155 / 29 / 30 (+ 4 asm, 2 float_control, 4 byte arithmetic).

## world_units.cpp (done, uncompiled)

Now: 0 At<>, 0 VCall, 0 byte-offset arithmetic, 1 raw address (inside the `__asm` of the CIacos
forwarder, left with the asm), asm 4 and float_control 2 unchanged (out of the ticket's scope).

The creation "message" is the .mob record (map_spec.h): `mapspec::UnitSpec` for CreateUnits,
`ObjectSpec` for UseQuickItem, and in CreateObject per kind: ObjectSpec (1, 5-9), UnitSpec (5),
FlameSpec (6), TrapSpec (7), LeverSpec (8), SoundSpec (3), LightSpec (2), ParticleSpec (4). The
party record is `PartyUnitRecord` (unit.h; texts[0] script name, [2] name, [3]/[4] the companion
texts, [5] prototype, [6] the 005308D0 text, unknown_01C id, unknown_77C leader, unknown_780,
unknown_770 -> moveMode2, weaponIndex -> CUnit::unknown_270). Objects are CMapObject / CUnit /
CTorchObject; timed objects CEffectSound / CEffectAmbientSound / CEffectPointLight /
CEffectParticle (world_effect.h); the brain is CUnitServer::brain (combat.h).

Header changes (all offsets from existing static_asserts or the disassembly noted):
- map_spec.h: EffectSpec, ParticleSpec, LightSpec, SoundSpec moved in from map_spec_effect.cpp,
  UnitSpec from map_spec_unit.cpp (unchanged layouts; static_assert on UnitSpec::stats 0x210 and
  SoundSpec::unknown_84 added). ObjectSpec::parentId (+0x50) is really the object's type for the
  world (CreateObject passes it to the manager's CreateObject); not renamed (map_spec.cpp uses it).
- map_object.h CMapObjectVirtuals: slot 29 IsDynamicSlot (bool; 0050E430 `flags >> 17 & 1`, units
  004467E0 `MOV AL,1`), 33 ScaleEffectSlot(int) (units 0052EB30), 35 SetStatsSlot(const UnitStats*)
  (units 00530A20 -> 00552F40), 37 UpdateSizeClassesSlot() (units 00530A50 -> controller slot 23,
  00552FB0). Vtables read from the exe: CUnit 0073EA8C, CMapObject 0073DAA0, CUnitServer 0073EF38.
  "Slot" suffix so the names do not collide with CUnit's plain members (ScaleEffectChanged,
  UpdateSizeClasses) or CUnitServer::SetStats.
- map_object.h: new `CTorchObject : CMapObject` with `spec` at +0x238 (sizeof 0x23C, from the
  TorchCreate comment). UNVERIFIED that the case-6 object's +0x238 is always a CTorchObjectSpec;
  the slot signatures match (16/17/18 RET 0xC/0xC/4).
- server_specific_manager.h: `CTorchObjectSpecSlots` (vtable 0073D558 read from the exe: 13-15
  empty RETs 004D6B20/30/40, 16 SetLightSlot, 17 SetFlameSlot, 18 SetNameSlot) and TorchSpecSlots().
- world.h CWorld: slot 18 ItemManager(), slot 24 CopyItemSlot(CEngineObject*) (server 00594CE0;
  not named CopyItem, CWorldServer::CopyItem is the hooked plain member).
- world.h CWorldServer: +0x11D loadedFresh_, +0x11E keepsState_ (world_map.cpp's LoadedFresh /
  KeepsState helpers still use byte offsets; convert them in a later pass).
- item.h: `ItemSourceRef` (12 bytes: vtable, refs dword, itemId), the record at CItem +0x1C.
- New world_data.h: named accessors for the strings, float constants (read from the exe), vtables
  and the prototype name map world_units.cpp used by address.

Behaviour risks to check when building / running:
- The torch's slot 16 now takes `float value` (flame->volume) where VCall pushed the dword: a
  signalling NaN would be quietened if MSVC moves it through the FPU. Slot 17's colour array is
  passed as const float* (same pointer).
- Unit record counts are uint32 in PartyUnitRecord; the loops cast them to int as the original
  compared signed (At<int>).
- Item charge copy (+0x28 = +0x2C) and spec scale/position zeroing use memcpy/memset to keep dword
  moves.
- PrototypeNamePair (local, 0x10 bytes) replaces the 16-byte buffer for the name map pair.
- `UnitSet18` forwarder now declared with `const char*` (same dword).

## world_client_maps.cpp, world_game.cpp

Not started.
