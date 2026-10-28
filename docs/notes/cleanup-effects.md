# Cleanup phase 1: world effects and spells (cleanup plan, ticket C2e, F1)

How the world effects (`world_effect*.cpp / .h`), the lightning (`lightning*.cpp`), the particle callbacks and the spells (`spell*.cpp / .h`)
reach other classes after the cleanup, and what is left. The classes themselves are in `objects.md` and `items_spells.md`.

## Declarations

- World effects: `CEffect` (world_effect.h; a real class since C28f) declares slots 18-23 (`Reset`, `Radius`, `Position`, `Unpack`,
  `Pack`, `Start`). `CEffectPointLight::light_` is a `CLight*`, `CEffectParticle::source_` a
  `CParticleSource*`, `CEffectLightning::bolt_` a `CLightning*`, `CEffectSoundSeq::name_` a `std::string`. world_effect_internal.h has `WorldOf` /
  `ContextWorld` (a `CWorld*`); the stream helpers go through `CStream`.
- Spells: `CSpellEffect::world` is a `CWorldServer*` (`AIMap()`, `Sectors()`, `AIEvents()`, `tick_`). The buff record a unit gets is a `MapEffect`
  (`power`, `ending`), built by 00688510 through its address. Units go through `ObjectSlots` (`EyePoint`, `AddEffect`, `IsLargeUnit`, `IsDead`,
  `ReceiveHit`); controllers are `CUnitServer` (`Brain()->memory / visible / unknown_12C`, `unit->player`); the possessing player's server is
  `CPlayer::connection_` as `CPlayerServer` (`group50F8_`, `unknown24_`). Spell records go through `SpellRecord` / `SpellModifierRecord`.
  `CManagerContext` slot 17 (item.h) returns the `CSpellConstructor*`.
- `CSceneRenderer` slot 16 (model.h) is `DrawStrip`; the lightning reaches slots 11 / 16 / 17 and `pipeline_` through it, and the manager walks
  `CBaseManager::objectList_`. Particle callbacks read element values through `ParticleElement`'s per-type views (`fragment`, `flames`, `orbiter`,
  `funnel`, `star`, ...; effects.h asserts each offset), `value` / `word` only where a set-up clears a slot no view reads; carriers are `CObject3D` (`orientation`,
  `radius`, `position`, `above`); `moveMode` is read from `CUnit`.

## Left

- spell_effect_start.cpp `StartHealing`: the controller's slot 21 (`Heal(float)`) stays an `ei::VCall` with the amount's bits. The original pushes the
  bits (`MOV` / `PUSH`), and a float argument would go through the FPU (`FLD` / `FSTP`).
- Duplicate forwarders left to the forwarder ticket: 006B8600 (Play3D: world_effect.cpp and map_object_companion.cpp); 006FE11F / 006FE216 (CString
  TrimRight / TrimLeft: spell_text.cpp, ini_file.cpp, item_constructor.cpp, originals_library.h).
- The naked x87 helpers in spell_effect_internal.h call the world's height slots 27 / 28 by number. (The prototype's hidden offsets are typed
  fields now, and world.h's `Projectile` became `CSpellEffect` (C15).)
- `CSpellEffect::spell_` stays bytes with `Spell()` casting it: CSpell is abstract, so `lib::InPlace` cannot hold it. world_effect.h's embedded
  effects have the same problem.
- The bounds the renderer clips by are `ClipBounds` in CParticleSource (`clip`), CVisibilityFog and CLightning (`bounds`), so `ClipTest` takes them
  with no cast (C15).

## Meanings of formerly unknown fields (ticket F1)

- CSpell's parameters follow the prototype: +0x20 duration, +0x24 damage, +0x28 radius (the area made a radius), +0x30 speed, +0x34 range, +0x38
  target count, +0x40 mana cost, +0x44 complexity (+1 per modifier), +0x4C (prototype +0x3C, only loaded and saved), +0x50 skill needed.
- `CSpellEffect::targetCount` (+0x54) is the embedded spell's +0x20, which is the duration, so that existing name is probably wrong.
- `CParticleManager::quietStart` (+0xE4; `ParticleManagerClearQuietStart`) is never set non-zero in src.
- `CSkyObject` +0x94..+0x21F are named after `CFigureMesh`, whose code runs on it. `CTrapObject::savedValue_` (+0x238) is only loaded and saved;
  nothing sets it. Constructor-only fields stay `unused_`: CLight +0xE4 / +0xE8 / +0xFC, CLightFactory +0xB0..+0xD0.
- `CSpellEffect::target` / `caster` are `AIRefTo<CMapObject>` / `AIRefTo<CUnit>`, the missile's `figure_` and the moshka's `bird_` are
  `AIRefTo<CMapObject>`, the point light's `light_` and the particle effect's `source_` are `HeldObject<CLight>` / `HeldObject<CParticleSource>`.
