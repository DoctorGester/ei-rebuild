# World effects: missiles, visibility, sound sequences, moshka, tornado (0x00672000-0x00680000)

## Missiles and visibility (world_effect_missile.cpp, world_effect_visibility.cpp)

CEffectMissile (abstract, vtable 00743754, 0x158): +0x14C arrived, +0x150 target id, +0x154 "from caster".
CEffectArrow (0x4E, vtable 007436F0, 0x6C0) embeds a CEffectParticle (+0x158), CEffectPointLight (+0x2D8) and
CEffectSound (+0x43C), then the sound name CString +0x6A0, light colour / range, particle type / size, caster id
+0x6BC. Steer (0067AAB0, shared with class 0x5B, which doubles the speed 0.6667) aims at the target's
+0x1C position with z + above.z / 2 + center.z; arrived when the planar distance is below one step (or NaN).
On a client the missile is kept 1 above the world's slot 28 height. Tick moves, and on a client moves the parts
(slot 24, Start(1) each). Start copies the settings into the parts (light velocity (0.0001, 0, 0), flags 0x840,
sound radius 30, looping) and starts them. Scenario spells (arrow, acid ray). CSpellEffect::Update calls Start /
UpdateMissile directly and the factory constructs effects directly, so those wrappers (and constructor wrappers) never
count; Tick / Pack / Unpack go through the vtables. CWorld has slots 28 (RawSurfaceHeightAt) and 40 (ObjectWithId);
CMapObject +0x144 is the extent.

Rewritten (grep the address): `Steer` 0067AAB0, SetTarget 0067AC70, Tick 0067AC90, Reset 0067AD10, MoveParts 0067B450, Start 0067B4E0, Unpack 0067B690,
Pack 0067B800, SaveState 0067B9A0, LoadState 0067B8E0, Construct 0067AE30, Destruct 0067B1C0, DeletingDestruct 0067B1A0; CEffectSoundSeq Start
00676C00 and Unpack 00676FD0 (+ ten pickers 00676DD0..00676F90; world_effect_sequence.cpp); CEffectAmbientSound Construct 00677510, Destruct
006776E0 (Reset inline, then again), DeletingDestruct 006776C0; effect factory EffectNewObject 0067E170 (manager slot 14, RET 4;
world_effect_lifetime.cpp). Also 0067AA00 (CEffectMissile::ResetMissile, the abstract vtable's slot 18; Arrow and ArrowFigure Reset call it),
00675FB0 / 0067B390 (CEffect::DestructEffect, also the unwind funclets') with 00676910 / 0067B430 (slot 0 of the two abstract vtables), 0067B050
(CEffect::Construct out of line), 00676900 (TickDefault: 1), 006773A0 (ReadSavedStrings; callers keep the forwarder). Nothing but unwinding
reaches the first five.
Kept: Load and Unpack do not recompute the moving flag; Start copies the sound name unbounded.

Sound sequences: the first name (when not empty) and the others go into a stack CStringArray handed to
CSoundManager::AddSequence with a chooser for their count; an update moves the sequence through SetSoundPosition with
the sequence id. The chooser takes the number of sounds started so far in EDX (`SoundSequence::PlayNext`, sound_manager.cpp,
passes it).

The factory constructs class 0x5B, Moshka (100) and Tornado (0x65) through their constructor addresses.

CEffectShowVisibility (0x5E, vtable 0074381C, 0x170): +0x14C observer, +0x150 seer (SetUnits 0067C640, from vision fog
0x17 with caster and target), +0x154 counted fog object (client: made by the fog manager *007AF75C, 004E28D0), +0x158
FogGrid (cells / rows allocated by the constructor). The server's Tick follows the seer, recomputes the grid (005CD210)
and marks the effect changed; Pack / Unpack send the grid through a stack memory stream (FogGrid Write / Read 005C8CC0 /
005C8BF0); Start reveals it in the fog (`CVisibilityFog::Reveal`). Reset does not reset the base. Not rewritten: 0067C5D0 (FogGrid free), 0067C450 / 0067C460 (class name / id). Script casts
have no caster, so only `spells_hero.txt` reaches it (see below).

CEffectArrowFigure (0x5B, vtable 007437B8, 0x19C): +0x158 the figure object (class 0x38 from the object manager with the
next client id 007AF63C, counted; model "efar1", texture "arrow01" swapped for "", filed with CWorldClient::AddObject; the
original reads the object's +0x94 figure before making it), +0x15C the shooter, +0x160 the DamageInfo DeliverAttack
(0054F450) copies in (multiplier 1 after Reset). Tick: on the server, once arrived, the target's slot 31
(CMapObjectVirtuals::ReceiveHit) gets (hit, shooter, shooter); the shooter is looked up before the target is tested. Slot
24 moves the figure in the client's object grid (CWorldClient +0x120) and rolls it along the velocity. The factory
constructs it directly. Scenario `effects_bow` (the bow is selected on the weapon
bar only in vanilla: Rebirth's layout moves the bar). Forwarders 00516DA0, 00518840, 00518860, 0051C970, 0051C250 live in
originals_units.h (world_client_maps.cpp uses them).

## Moshka (world_effect_moshka.cpp)

CEffectMoshka (100, vtable 0074387C, 0x16C): the client world's bird (+0x1B0, made by 00566C20 with the constructor,
told to follow the session's leader by 00565380 / 00566C20). +0x14C figure (class 0x38, "unmobi" / "bird00", filed with
AddObject, lit, animation 0 played), +0x150 particle source 0x203C (scale 0.5, attached), +0x154 followed id, +0x158 last
time, +0x15C timer, +0x160 circle angle, +0x164 perch height, +0x168 state (0 perched 200 frames, 1 circling within 5 of the
leader for 100, 2 flying to the perch at +0x1C for 150, then Reset). Update (0067CEF0, from the draw pass with the
animation time) steps at most 5 frames, keeps the bird 0.5 above the surface (slot 28, platforms), moves it at
sqrt(sqrt(distance)) * 0.03 + 0.3 per frame (sector grid Move, RollTowards) and advances its animation. FindPerch
(0067CD90) draws ISAAC angles until the AI map's line of sight is above 0.5. Kept: an unknown state leaves
Update's target unset (never happens: Follow sets 0). Not rewritten: 0067CAB0 / 0067CAC0, 0067D650 / 0067D660 (3-byte
Pack / Start stubs). 007C8430 (pi/2, set at startup) is a constant here.

## Tornado (world_effect_tornado.cpp)

CEffectTornado (0x65, vtable 007438F8, 0x15C): +0x14C particle source 0x200A (client), +0x150 looping sound
"nature\tornado\circle.wav" (near 10, far 40), +0x154 ticks left, +0x158 launch tick. Launch (0067DA50, from the server
weather 0058CCD0's helper 0058CEA9) aims at a random speed 0.096..0.216 per tick, ticks = lrint(distance / speed). Tick
moves it (source and sound follow), stops the source for the last 110 ticks, and on the server every 8th tick above 20
hands each player-owned unit within 5 (CSectorMap::FindUnitsInRadius) a hit of 5 (weight 1 on type 6) through its
controller's slot 20 (CUnitServerVirtuals::TakeHitSlot, map_object.h). Start catches a new source up by at most 29
ticks. Not rewritten: 0067D7C0 / 0067D7D0 (class name / id), 0067E0D0 (counted-pointer
release, unwind only). No scenario starts weather with a tornado yet.

## Scenario notes (spells_hero)

- The server drops a blocked unit's orders, casts included (0066FFF0), so `spells_hero` waits for the intro script's
  `BlockUnit` (flag 0x800000), lifts it and waits for the arrow to hurt either wolf; every newgame scenario shows 3
  CastSpell (point lights). `ui_battle_spells` blocks the hero the same way (only checks that UseOnUnit is reached).
- A party unit's spells come from its player's party record (GetSpell 005319A0), not CUnit::AddSpell (00531820), so
  GiveUnitSpell never reaches the hero's column. The scenario gives "spell container[...]" items (GiveItem) and puts
  them on in the camp's magic page (page 1; CanEquip 006100E0 wants the school skill, hence GiveSkill). "Or" is a Rebirth
  addon function (vanilla: "Parameter of type float expected"). hudstate logs the column unit's spell slots.
  `ui_battle_spells` still uses GiveUnitSpell, so its column holds only the hero's two spells.

## Next candidates

The Moshka and the tornado need a followed object / tornado weather that no scenario provides.
