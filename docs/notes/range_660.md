# Players, resources and effect objects (00660000-00680000)

Code: `src/game/world_effect.h`, `world_effect_internal.h`, `world_effect.cpp` (base and sounds),
`world_effect_visual.cpp` (light, particles, wall, lightning), `src/game/resource_stream.h/.cpp`
(the rest of the resource code). Verify: `tests/verify/verify_world_effect.cpp`,
`tests/verify/verify_resource_stream.cpp`. Names are provisional (class names are the game's
GetClassName strings). The earlier resource work (CMappedResource, CMMIOResource methods,
CResourceManager slots 16-22, CMappedFile) is in `resource.cpp` / `resource_manager.cpp`.

Shared files changed (additive only): `src/game/resource.h` (declarations of
`CMappedResource::ListFiles` and `CMMIOResource::Construct/Destroy/DeletingDestroy`),
`src/game/resource_manager.h` (`CResourceManager::Construct/Destruct/DeletingDestruct/
DestroyManager`), `src/lib/stlp.h` (`vector::FreeStorage`), `docs/standalone-cleanup.md`.

## Map

| Range | Contents |
|---|---|
| 0065F4B0-00661F30 | **CParty** (vtable 00743000, GetClassName 0065F560, 54 slots: a CEngineObject plus a second interface at +0x2C): constructor 00661050, destructor 006611D0, slots 2/3 006614F0/00661640, member list +0x40/+0x44 (00661DB0 add, 00661F30 remove), serialisation 00661300 / 006618C0 / 00661C10; 00660100-00661050 party record list helpers used by the hero screens (0058FD90, 00590C70, 0060FCA0, 00617A50) |
| 006620F0-00662AC0 | party records: 0x784-byte records of the party's units (items, stats), `PartyRecords::Find` 006621B0 (by unit id; units call it for their weapons), record read / write 006621F0 / 006625B0, 006628D0 |
| 00662AC0-00663000 | multiplayer map transfer: 00662AC0 writes/reads "temp.mp", 00662DD0, 00662FB0 ("\*.mp" list; patched by Rebirth) |
| 006632B0-00664D00 | STLport hash_map / vector instantiations of the player tables (6636D0 bucket init, ...) |
| 00664DE0-00666170 | **CPlayer** (vtable 00743104, 16 slots): constructor 00664E20, slots 2/3 00665010/006650E0, slot 14/15 00666170/00666050; items: 00665470 receive items, 00665640 HasItem (script), 00665780, 00665BE0 item changed; units: 00665A70 / 00665B50 remove; 00665DE0 per-tick (drops dead references from the list at +0x118, then the slot 21 of +0x20) |
| 00666EE0-0066C700 | **CPlayerClient** (vtable 00743180, 48 slots): slots 2/3/6 00667230/00667510/00667680 (per frame; "Very top secret"/"Tango" are the cipher keys of the session), 00667810 send ("client to server, size =", " squeezed size ="), 00667BA0 slot 14 (receive: packet dispatcher, "Wrong packet ID", "Pkt read error"), handlers 00667EC0-0066C1E0: 00667EC0 creates an object the server announced (unit prototype "unmogo" gets rh3.axeth00 / rh3.sword00 and rh3.pike00 or sling; "unmowi" gets particle 0x204D; humans' "hr.%.2d" model part from the party record; "mo%dp" / "mo%ds" model and skeleton names of non-unit objects by +0x0C), 00669A30 "party received on client", 0066AE70 / 0066AED0 local view orders (weapon, movement mode) |
| 0066C730-00671080 | **CPlayerServer** (vtable 00743280, 24 slots): slots 2/3 0066C9A0/0066CC50, 0066D270 (slot 23), 0066D6E0 message 0x0B to the client, 0066E130 per tick (rebuilds the "Heroes" group), 0066E430/0066E7D0 (slot 17: send, "server to client, size =", " squeezed to "), variables and state 0066EE10 (0x81 set variable) / 0066EE80 / 0066EF30 / 0066EB90 / 0066EF80 / 0066F310, 0066F450 "Resending party of player", 0066F4C0 slot 20 (receive: "Serv wrong packet ID", "SrvErrMsg", "WrongClientVersion"); command handlers 0066F840-00671060: 0066FFF0 walk, 00670190 attack, 00670310 group action, 00670C20 zone exit, 00670DA0, 00670E00 / 00670EB0 (variables), 00670F50 (party), 00671060 |
| 006710A0-00671280 | CArray / aligned-array helpers |
| 00671280-00675050 | **resources**: CMappedResource (vtable 007432E0) and CMMIOResource (00743300) (rewritten earlier, `resource.cpp`), their STLport hash_map instantiations (006713C0-00674070), the sound system's file reader (00743320, below), CResourceBase (00743324), FindEntry 00674850, the accelerator 006748F0 / 00674910, ListFiles 00674AC0 / WildcardMatch 00674CE0 |
| 00674D90-00675B40 | CResourceManager (vtable 00743338): constructor / destructor / slot 3 (this round), slots 16-22 (earlier); CMappedFile (earlier) |
| 00675B40-0067E000 | **effect objects** (below): CEffect base 00675B40-00676050, CEffectSound 00676050-00676930, CEffectSoundSeq -006776A0, CEffectAmbientSound -00677F70, CEffectPointLight -00678950, CEffectParticle -006799A0, CEffectWall -0067A070, CEffectLightning -0067B180, CEffectArrow (0x6C0 bytes, embeds a particle, a light and a sound; missiles) -0067BC90, CEffectArrowFigure ("arrow01", "efar1") -0067C450, CEffectShowVisibility -0067CAB0, CEffectMoshka (birds, "bird00", "unmobi") -0067D7C0, CEffectTornado ("nature\tornado\circle.wav") -0067E000 |
| 0067E170 | the effect factory: class id 0x48..0x5B to a new effect object |
| 0067E510-0067F870 | CSpellEffect (vtable 0074399C): constructor 0067E510, destructor 0067E5D0, save / load 0067E7D0 / 0067E960, `Update` 0067E6D0, and the helpers that create its effect objects (0067F170 arrow, 0067F260 particle or wall, 0067F410 lightning, 0067F580 sound, 0067F660 sound sequence, 0067F7C0 point light; 0067EE00-0067EF40 release them, 0067EFC0 hit by spell type). items_spells.md has the spell side |

### Effect objects

Every effect is a CEngineObject / CTransformObject (position +0x1C) with: +0x94 start tick, +0x98
velocity, +0xA4 "moving" (velocity not all zero; NaN components count as zero), +0xA8 the replicated
state (a `DeltaField`, net_delta.h), +0x148 change stamp (global 007AFFC8, -1 after Reset).
The server ticks them (slot 6; 0 removes the effect) and Packs (slot 22) their fields into the
DeltaField (SendFull / SendDelta, 00675B40 / 00675B70); the client Unpacks (slot 21, through
Receive 00675BA0) and then Starts (slot 23: 0 = create the sound / light / particle source, else
move it). Load / Save (slots 9 / 10) use the save stream's slot 1 / 2 and its +8 version (fields
added at versions 0x43, 0x58, 0x5C, 0x67, 0x6D, 100). Slot 19 is a radius (100 by default), slot
20 the position used for visibility.

| Class | Vtable / id | Own fields |
|---|---|---|
| CEffectSound | 007433EC / 0x48 | +0x14C name[256], +0x24C volume, +0x250 radius, +0x254 looping (0: ends after the first tick), +0x258 no-fade flag (plays with 1000), +0x25C sound handle, +0x260 fresh |
| CEffectSoundSeq | 0074344C / 99 | +0x14C CString, +0x150 CStringArray, +0x164 volume, +0x168 radius, +0x16C flag, +0x170 sequence handle |
| CEffectAmbientSound | 0074350C / 0x49 | +0x14C CStringArray, +0x160 volume, +0x164 radius, +0x168, +0x16C, +0x170 handle |
| CEffectPointLight | 0074356C / 0x4B | +0x14C colour, +0x158 range (negative: light flag 0x400), +0x15C CLight, +0x160 light flags |
| CEffectParticle | 007435D0 / 0x4A | +0x14C attached position, +0x158 particle type, +0x15C size, +0x160 / +0x164 copied to the source's +0x118 / +0x11C, +0x168 attached object, +0x16C target (moves towards it, keeping its height over the ground), +0x178 CParticleSource, +0x17C unit for the start position |
| CEffectWall | 00743630 / 0x5A | particle + +0x180 direction (to the source's +0x130); a fire wall (0x2010) leaves a scorch mark |
| CEffectLightning | 00743690 / 0x4D | +0x14C from, +0x158 to, +0x164 width, +0x168 bolt, +0x16C target object (+1 in z), +0x170 source unit, +0x174 flag |

Particle types 0x2002, 0x2006, 0x200B-0x200F, 0x2018, 0x2019, 0x201B-0x2026, 0x2030-0x2033,
0x203F-0x2043 are one-shot: the tick removes them 60 ticks after the start (0x2019: 120), and their
first spawn catches up all ticks since the start (other types at most 30). Types 0x2002 and 0x2030
leave a scorch mark (0056D4C0 on the world client 007AF5B8), 0x2030 also shakes the camera.

### The sound system's file reader (vtable 00743320, 0x34 bytes)

Created by 006B6040; CResourceBase fields plus +0x24 archive HMMIO, +0x28 entry offset (1 for a
loose file, 0 when nothing is open), +0x2C entry size, +0x30 loose file HMMIO. OpenFile tries a
readable loose file first (`_access`, `_findfirst` for the size, `mmioOpen`), then the archive's
entry. The archive part is dead code: its Open (00674310, which builds the accelerator with
00674910) and the by-number open 006745C0 have no callers, so +0x14 / +0x18 stay null.

### What "model / animation names" in this range are

The hr. / rh3. / mo%d strings are only used by 00667EC0, the client's handler for an object the
server announced (multiplayer): it builds the unit or object from the packet and picks the model
and weapon parts. The single-player model selection is elsewhere (00535CB0, 005381B0, 00536670,
00568800 use the same format strings). Not rewritten (3 KB, CString / CStringArray temporaries,
multiplayer only).

## Rewritten

| Address | Name | Verification |
|---|---|---|
| 00675B40 / 00675B70 | CEffect::SendFull / SendDelta | ours only (their Pack is checked itself) |
| 00675BA0 | CEffect::Receive | ours only: result 0 or 1 |
| 00675BE0 | CEffect::ResetBase (slot 18 of the base) | ours only: base fields cleared |
| 00675CA0 | CEffect::MarkChanged | ours, object restored, original; bytes compared |
| 00675D10 / 00675D40 | CEffect::CreateEffect / DestroyEffect (slots 2 / 3) | ours only (result 1) |
| 00675D50 | CEffect::SetVelocity | ours / restore / original, bytes compared |
| 00675FA0 | CEffect::Position (slot 20) | pure |
| 006768F0, 00676050, 00676930, 00677690, 00677F70 | Radius getters (slot 19) | pure, result bits |
| 00676200, 00676B10, 00677870, 006780C0, 00678A90, 00679AF0, 0067A1B0 | Reset (slot 18) of sound, sequence, ambient, light, particle, wall, lightning | ours only: the fields Reset sets |
| 006762F0, 00677250 | Tick of sound / sequence | ours / restore / original (moving a playing sound twice is harmless); object bytes and result |
| 006781B0 | CEffectPointLight::Tick | ours only: position advanced, light placed there |
| 006795C0 | CEffectParticle::Tick (also the wall's) | ours / restore / original: object and the source's position bytes, result |
| 0067A8F0 | CEffectLightning::Tick | ours only (result 1) |
| 006763A0, 00677950, 006782D0, 00678CF0, 0067A370 | Start (slot 23) | ours only; the light: flag 0x400 matches the range's sign |
| 00678EC0 | CEffectParticle::Spawn | ours only: a source exists afterwards |
| 00679350 | CEffectParticle::Attach | ours only |
| 00676430, 006779D0, 00678520, 00678D30, 00679C70, 0067A490 | Unpack (slot 21) | ours only (they consume the reader and start things): result 0 or 1 |
| 00676550, 00677180, 00677B20, 00678610, 006791A0, 00679DB0, 0067A600 | Pack (slot 22) | ours into the field, the four streams (headers and bytes) saved, restored, original; compared (skipped when ours grew a buffer) |
| 006766D0, 00677430, 00677C60, 00678740, 006794E0, 00679ED0, 0067A7F0 | SaveState (slot 10) | original against a recording stand-in, ours through a forwarding recorder: bytes written compared |
| 006765F0, 00678690, 006793E0, 00679EA0, 0067A770 | LoadState (slot 9) | original on a scratch copy against a stand-in that reads nothing, ours through a forwarding recorder: (offset in the object, size) of every read compared |
| 006772D0, 00677BF0 | LoadState of sequence / ambient | ours only (the string-array readers use more of the stream) |
| 00678BA0 | CEffectParticle::Position | ours / restore / original; bytes and returned pointer |
| 00679280, 006792B0, 006792E0, 00679310, 00679C30, 0067A890, 0067A8C0, 0067A6B0 | particle / wall / lightning setters (SetAttachedTo, SetSize, SetColors, SetTarget, SetDirection, SetObjects, SetFlag, SetEnds) | ours / restore / original, bytes compared |
| 00674790 / 00674810 / 006747C0 / 00674A90 | CResourceBase Construct / Destruct / deleting destructor / FreeAccelTable | Construct: original into scratch memory, bytes compared; others ours only with checks |
| 00674280 / 006742C0 / 006742A0 | CResourceStream constructor / destructor / deleting destructor | ours only (constructor: fields checked) |
| 00674430 / 006744C0 / 006746B0 / 006746F0 | CResourceStream Close / OpenFile / Read / Seek | ours only (file positions): fields consistent, Read not above the request |
| 00674CE0 | WildcardMatch | pure |
| 00674AC0 | CMappedResource::ListFiles | original into a scratch CStringArray (MFC constructor / destructor 006FE925 / 006FE958), names compared |
| 006721D0 / 006722F0 / 006722D0 | CMMIOResource constructor / destructor / deleting destructor | ours only (constructor: vtable, closed handles) |
| 00674D90 / 00674DF0 / 00674DD0 / 00674F40 | CResourceManager constructor / destructor / deleting destructor / slot 3 | ours only |

All renamed in Ghidra where they were functions there.

### x87 details

The ticks and starts follow the original's operation order; values the original keeps on the FPU
are not rounded in between. Where the compiler could not be trusted to keep them, small naked
helpers repeat the original instructions: `StepFraction` (the particle's speed / distance ratio),
`SetLightRange` (range, range^2 with FST, 1 / range^2 from the unrounded square),
`WallScorchShape` (FPATAN and the length sum), `RandomAngle` (FILD qword of the random value: a
dword FILD plus 2^32 would round at 24-bit precision). Floats the original copies with FLD / FSTP
go through `CopyThroughFpu`; scalar float bits passed on to callees are read with `Bits(const
float&)` (a by-value float would pass through the FPU). The compiled code of the particle tick,
the lightning midpoint and the wall's scorch was read back with dumpbin.

## Forwarders added (through game addresses)

Sound system (*007AF784): 006B8600 play, 006B92F0 move, 006B94A0 stop, 006BA5E0 / 006BA7C0
sequence move / stop, 006B9950 / 006B9A30 / 006B9AA0 ambient play / move / stop. Light manager
(*007AF738): 006CA720 remove, 006CA5D0 add, 006CA830 move; CLight 0068D010 colour and position;
lights from *007AF748 slot 23. Particle sources: 00498400 create (*007AF754), 00485480 ScaleSize,
004849F0 attach, 00485360 stop; 0056D4C0 scorch mark; 004113A0 camera shake (*007AF7C8);
004667F0 / 0049EE70 ISAAC (007A9D40); bolts from *007AF758 slot 23, 004680D0 start, 00467E10 move;
00511B90 / 0056F1A0 reference assignment; 00455950 World, 00455940 ManagerContext, 0055FB00
FindUnit, 00403F40 FindObject (*007AF5F8), 00532860 a unit's effect point, 004B3330 group
contains; net_delta methods (0047FA40, 0047FAA0, 0047FAF0, 004A54D0, 0047E3A0, 0047DCA0,
0047E2D0, 0047F1E0, 0047F4D0, 0047E930, 0047E000, 0047E340 (read a C string; not rewritten),
0047E310, 0047F3D0, 0047F7F0); MFC CStringArray SetSize 006FE9B8 / SetAtGrow 006FEBCE, the
save-stream string readers 006773A0 / 004E5840; the particle methods the wall calls (00678D30,
006793E0, 00678EC0, 00679350). Resources: `_access` 006EAF20, `_findfirst` 006EB832, `_splitpath`
006E8BEE, FindEntry 00674850, CBaseManager 00402B20 / 004031E0 / 00402DF0. Effect virtual slots
and the stream / world slots go through `ei::VCall`.

None of the callees is on the Rebirth list. `CMMIOResource::Destroy` calls our
`CMappedResource::Destroy` directly: the original has an inline copy of that destructor, not a
call to the Rebirth-patched 006714A0.

## Original bugs and oddities kept

- CEffectSound / SoundSeq Tick move the sound when the handle is not 0, so a stopped (-1) sound is
  moved too; Sound Reset stops a sound only when it loops.
- CEffectAmbientSound::Unpack reads the name count into the low byte of the stack slot of its
  `field` argument.
- CEffectParticle::Attach reads the source's position through a null source when there is none.
- CResourceStream::Seek: a loose file treats "from the current position" as "from the start";
  "from the end" of an archive entry uses +0x2C, which OpenFile never sets. OpenFile leaks the
  `_findfirst` handle.
- CMappedResource::ListFiles copies a name into a 1 KB buffer without checking its length.

## Skipped and why

- The player classes (CParty, CPlayer, CPlayerClient, CPlayerServer, 00660000-00671280): network
  packets, party records and their CString / CArray / hash_map members; mostly multiplayer. The
  single-player parts run through the per-tick 00665DE0 / 0066E130 and the script-facing player
  state calls (0066EE10..0066F310), which write into the client stream. Good next candidates:
  `PartyRecords::Find` 006621B0 (pure), 0066EE10 / 0066D6E0 (fixed message layouts).
- 00667EC0 (object from the server, the model / weapon names): multiplayer, 3 KB, see above.
- Effect constructors and destructors (00675DD0, 006760A0, 00676760, 00676980, 00677510,
  006776E0, 00677D30, 00677FC0, 006787B0, 00678990, 006797E0, 006799E0, 00679FC0, 0067A0B0), the
  factory 0067E170: object lifetime with EH frames; later together with the effect manager.
- CEffectSoundSeq::Start 00676C00 and Unpack 00676FD0 (CStringArray temporaries and the random
  picks 00676DD0..00676F90 passed as callbacks), CEffectArrow / ArrowFigure / ShowVisibility /
  Moshka / Tornado (0067AAB0-0067E000): next round.
- Dead code: 00674310 (archive open of the file reader), 00674910 (accelerator), 006748F0,
  006745C0, 00674670, 00675CC0 (UpdateMoving) and 0067A2C0 (UpdateEnds) have no callers; their
  logic exists inline in the rewritten callers.
- STLport hash_map / vector instantiations (006713C0-00674070, 00675560-00675690).

## Scenarios that reach this code

No new scenario. `effects.txt` (CreateParticleSource: CEffectParticle Unpack / Start / Spawn / Tick /
Pack / setters, DeleteParticleSource: Reset), `spells.txt` (casts create sounds, sound sequences,
lights, particles, the fire wall and lightning through 0067F170-0067F7C0; their Tick, Pack, Unpack,
Start, Reset), `sandbox_actions.txt` (its healing spell goes through the same casting code, so
probably its sounds and particles). `combat.txt`, `models.txt` and `units.txt` reach only the
resource code. Every scenario reaches the resource code (archives open at startup, the
sound file reader at every sound) and the CResourceManager constructors; destructors run at exit.
Save / Load of effects only runs when a game with live effects is saved or loaded (none of the
scenarios does that).
