# Cleanup phase 1: world effects and spells (cleanup plan, ticket C2e)

How the world effects (`world_effect*.cpp / .h`), the lightning (`lightning*.cpp`), the particle callbacks and the
spells (`spell*.cpp / .h`) reach other classes after the cleanup, and what is left. The classes themselves are in
`objects.md` and `items_spells.md`. Every rewritten address kept its hook and verify wrapper. The object code was
checked with a disassembly diff for each group. The only differences are register allocation, inlined accessors,
`call [reg+slot*4]` in place of the out-of-line VCall, and the order of two pure loads in
`CParticleSource::UpdateRegeneration`.

## Declarations

- World effects: `CEffectVirtuals` (world_effect.h) declares slots 21-23 (`Unpack`, `Pack`, `Start`) on top of
  `CTimedObjectVirtuals` (18-20); `EffectSlots(this)` and `TimedSlots(this)` reach them.
  - `CEffectPointLight::light_` is a `CLight*`, `CEffectParticle::source_` a `CParticleSource*`, and
    `CEffectLightning::bolt_` a `CLightning*`.
  - `CEffectSoundSeq::name_` is an `mfc::CString`.
  - world_effect_internal.h has `WorldOf` / `ContextWorld` (a `CWorld*`); the stream helpers go through `CStream`.
- Spells:
  - `CSpellEffect::world` is a `CWorldServer*` (`AIMap()`, `Sectors()`, `timeWheel_`, `tick_`).
  - The buff record a unit gets is a `MapEffect` (`power`, `ending`), built by 00688510 through its address.
  - Units go through `ObjectSlots` (`EyePoint`, `AddEffect`, `IsLargeUnit`, `IsDead`, `ReceiveHit`).
  - Controllers are `CUnitServer` (`Brain()->memory / visible / unknown_12C`, `unit->player`).
  - The possessing player's server is `CPlayer::connection_` as `CPlayerServer` (`group50F8_`, `unknown24_`).
- Spell records go through `SpellRecord` / `SpellModifierRecord`.
- `CManagerContext` slot 17 (item.h) returns the `CSpellConstructor*`.
- `CSceneRenderer` slot 16 (model.h) is named `DrawStrip`. The lightning reaches slots 11 / 16 / 17 and
  `pipeline_` through it, and the manager walks `CBaseManager::objectList_`.
- Particle callbacks:
  - element values are read through `ParticleElement::value`;
  - carriers are `CObject3D` (`orientation`, `radius`, `position`, `above`);
  - `moveMode` is read from `CUnit`.

## Counts (these files, before / after)

| | Before | After |
|---|---|---|
| Offset access (At / Field) | 14 | 0 |
| VCall | 56 | 1 |
| Byte offset arithmetic | 31 | 0 |
| *At views | 0 | 0 |
| Per-file containers | 0 | 0 |
| Duplicate forwarders touching these files | 3 | 3 |

## Left

- spell_effect_start.cpp `StartHealing`: the controller's slot 21 (`Heal(float)`) stays an `ei::VCall` with the
  amount's bits. The original pushes the bits (`MOV` / `PUSH`), and a float argument would go through the FPU
  (`FLD` / `FSTP`).
- The duplicate forwarders are left to the forwarder ticket:
  - 006B8600 (Play3D: world_effect.cpp and map_object_companion.cpp);
  - 006FE11F / 006FE216 (CString TrimRight / TrimLeft: spell_text.cpp, ini_file.cpp, item_constructor.cpp,
    originals_library.h).
- Hidden offsets the counters do not see:
  - spell_effect_internal.h: the prototype reads `Prototype(e) + 0x2C / 0x64 / 0x70`;
  - spell_cast.cpp: `DwordAt` / `FloatAt` on the prototype;
  - particle_common.h: `ElementInt` / `ParticleFloat`.
- `CSpellEffect` and world.h's `Projectile` describe the same class (0xA8 bytes, 0067E510). They should become one
  declaration.
- The naked x87 helpers in spell_effect_internal.h still call the world's height slots 27 / 28 by number.
