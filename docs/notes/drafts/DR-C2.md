# DR-C2: names for the placeholder virtual slots

Placeholder `SlotNN()` declarations in map_object.h, unit.h, world.h, map_object_figures.h and
party.h renamed after what the slot does. Vtable order and signatures unchanged (a renamed
`void Name()` still only holds the slot's place: give it its real signature when a caller needs
it). Evidence: the vtable entries read in Ghidra (xrefs from each entry), the slot functions
disassembled, existing names in src and docs/notes. Confidence: H high (named function or clear
body in several classes), M medium (one class shows the meaning, others are stubs), L low (best
guess, see the note).

Built after merging main (see Status).

Name clashes avoided: CWorld's slots must not take the names of free helpers that CWorldServer /
CWorldClient member bodies call unqualified (`World()`, `ItemManager()` in world_party.cpp /
world_quests.cpp, `LightManager()`, `ParticleManager()`, `TerrainHeightAt()`), nor of
CWorldServer's own `CopyItem(CEngineObject*)` (it would hide the slot). Hence ThisWorld,
ItemFactory, ClientLightManager, ClientParticleManager, GridHeightAt, DuplicateItem.

## map_object.h

### CMapObjectVirtuals (CMapObject 0073DAA0, CUnit 0073EA8C; also traps 0073E548, levers 0073E740)

| Slot | CMapObject / CUnit | Old | New | Evidence | Conf |
|---|---|---|---|---|---|
| 13 | 00401490 | Slot13 | CopyTransform | CTransformObject::CopyTransform (core_transform.h) | H |
| 14 | 00516370 | Slot14 | CombineRotation | CObject3D::CombineRotation3D, CTransformObject slot 14 | H |
| 15 | 0040CC30 | Slot15 | UpdateWorld | CTransformObject::UpdateWorld | H |
| 16 | 00516350 | Slot16 | SaveTransform | calls 00401510 SaveTransform | H |
| 17 | 00516360 | Slot17 | LoadTransform | calls 00401540 LoadTransform | H |
| 19 | 0050E890 / 0052E870 | Slot19 | AttachObject | CMapObject::AttachObject, CUnit::AttachCompanion | H |
| 20 | 005161E0 | Slot20 | CopyFigure | CObject3D::CopyFigure | H |
| 22 | 00516460 | Slot22 | DrawFigure | CObject3D::DrawFigure | H |
| 23 | 00516420 | Slot23 | DrawCollectDirty | CObject3D::DrawCollectDirty | H |
| 24 | 00516490 | Slot24 | DrawFigureShadow | CObject3D::DrawFigureShadow | H |
| 25 | 0050E410 (RET 4) / 0052FC40 | Slot25 | AdvanceTime | CUnit::AdvanceUnitTime; world_client kSlotAnimate | H |
| 26 | 00517500 | Slot26 | LoadFigures | CObject3D::LoadFigures (units 0052E910 LoadUnitFigures) | H |
| 27 | 0050F2D0 / 00532A80 | Slot27 | RecomputeScale | mapslot::kRecomputeScale | H |
| 28 | 0050E420 (RET) / 00532AB0 | Slot28 | EffectsChanged | mapslot::kEffectsChanged, CUnit::EffectsChanged | H |
| 29 | 0050E430 / 004467E0 (1) | Slot29 | IsSynchronised | flags_ 0x20000 (set by the zone loader for records with +0x6C or a script name); units, levers, traps, torches 1. CWorldServer::SendFreshObjects moves the objects answering 0 out of the map-object maps (world_map.cpp kSlotObjectStatic) | L |
| 32 | 0050E970 / 0052EB50 | Slot32 | StatsChanged | PackVitals, CUnit::StatsChanged | H |
| 33 | 0050E960 / 0052EB30 | Slot33 | ScaleEffectChanged | mapslot::kScaleEffectChanged | H |
| 35 | 0050E480 (RET 4) / 00530A20 | Slot35 | SetStats | CUnit::SetServerStats(stats) | M |
| 36 | 0050F050 | Slot36 | SetServerData | CMapObject::SetServerData | H |
| 37 | 0050E490 (RET) / 00530A50 | Slot37 | UpdateSizeClasses | CUnit::UpdateSizeClasses | H |
| 38 | 0050F0C0 (0) | Slot38 | IsUsable | traps 1 (00529AF0), levers NeedsKey (0052BB00); ui_hud kSlotCanTarget: use / item / spell orders need it | M |
| 39 | 0050F0D0 (0) | Slot39 | IsDirectUseBlocked | levers: byte +0x260 (record +0x14); ui_hud: a plain click uses the object only when 38 is set and 39 is 0 | L |
| 40 | 0050F0E0 (-123) | Slot40 | KeyId | levers: the lock's id when its mask has bit 3 (0052BBC0) | M |
| 41 | 0050F0F0 | Slot41 | ApplyScience | CMapObject::ApplyScience; traps Disarm, levers ApplyKey; combat_actions kSlotStudy | H |
| 42 | 0050F110 (0, RET 4) | Slot42 | AcceptsKey | levers KeyOpens, traps KeyFits; ui_hud kSlotAcceptsSpell with the screen's +0x220 | M |
| 43 | 004467B0 (AL 1) | Slot43 | KeepsMapFlag | levers: byte +0x261 (keepsMapFlag, the AI map flag kept while it moves) | L |
| 44 | 0050F120 | Slot44 | UseSpots | CMapObject::FreeArray (no spots), levers UseSpots 0052BBE0 | H |
| 45 | 0050F1E0 | Slot45 | AddEffect | CMapObject::AddEffect; spell_effect kSlotApplyBuff | H |
| 46 | 0050E4A0 / 0052F7F0 | Slot46 | SetStunned | mapslot::kSetStunned, CUnit::SetFlag1000000 | M |
| 49 | 0050EC30 / 0052EF90 | Slot49 | ReadNetState | CMapObject::ReadNetState | H |
| 50 | 0050EAF0 / 0052EC70 | Slot50 | WriteNetState | CMapObject::WriteNetState | H |
| 51 | 0050F020 / 0052F740 | Slot51 | PlaceOnGround | CMapObject::PlaceOnGround | H |
| 54 | 0050E4F0 (0) / 00446890 | Slot54 | IsLargeUnit | units' +0x278 (object.h kFlag278 "AI map: large unit"); spell_effect kSlotBusyA refuses a link | M |
| 56 | 0050E510 / 00446850 | Slot56 | ForgetObject | units: controller slot 42; world_server kSlotForget, units.md ForgetObject | H |
| 57 | 0050E520 / 00446870 | Slot57 | UpdateMoveFactors | units: controller slot 53 = CUnitServer 00552F30 -> UpdateMoveFactors | H |
| 58 | 0050E530 / 00446810 | Slot58 | RefreshWounds | units: controller slot 31 (client RefreshWounds 00537360) | M |
| 59 | 0050E540 (ST0 0) / 004468B0 | Slot59 | MoveSpeed | units: GetMoveSpeeds' forward speed; combat.cpp kUnitSlotSpeed | H |

### CMapObjectCompanion (server 0073DD6C, client 0073DBEC)

| Slot | Server / client | Old | New | Evidence | Conf |
|---|---|---|---|---|---|
| 13 | 005106D0 (RET 4) / 005117D0 | Slot13 | EffectsChanged | client EffectsChanged (map_objects.md) | H |
| 14 | 00515810 / 005104B0 | Slot14 | Unused14 | empty in both | L |

### CUnitServerVirtuals (CUnitServer 0073EF38): named as unit.h's CUnitController

| Slot | Server | Old | New | Conf |
|---|---|---|---|---|
| 13 | 00533B90 (base) | Slot13 | FiguresLoaded | H |
| 14 | | Slot14 | RestartAnimation | H |
| 15 | 00533BB0 (RET) | Slot15 | Unused15 | L |
| 16 | 00546090 | Slot16 | Path | H |
| 17 | 005460B0 | Slot17 | Unused17 | L |
| 18 | 00533BC0 | Slot18 | PlayerChanged | H |
| 19 | 005533B0 | Slot19 | RefreshVitals | H |

Callers changed: none (no code called these placeholders).

## unit.h: CUnitController (server 0073EF38, client 0073ECD8, base 0073EDBC)

| Slot | Server / client | Old | New | Evidence | Conf |
|---|---|---|---|---|---|
| 13 | 00533B90 / 005344E0 | Slot13 | FiguresLoaded | client FiguresLoaded; unit_update kControllerFiguresLoaded | H |
| 15 | 00533BB0 / 00535580 | Slot15 | Unused15 | empty in base, server and client; no caller found | L |
| 17 | 005460B0 / 00534130 | Slot17 | Unused17 | empty in both | L |
| 19 | 005533B0 / 00533BD0 | Slot19 | RefreshVitals | CUnitServer::RefreshVitals | H |
| 20 | 00553F40 / 00533BE0 | Slot20 | TakeHit | CUnitServer::TakeHit | H |
| 21 | 005543B0 | Slot21 | Heal | CUnitServer::Heal; kControllerSlotHeal | H |
| 22 | 00553500 | Slot22 | RepackStats | CUnitServer::RepackStats; kControllerStatsChanged | H |
| 23 | 00552FB0 | Slot23 | UpdateSizeClasses | CUnitServer::UpdateSizeClasses | H |
| 24 | 00556760 / 00539160 | Slot24 | Level | both Level | H |
| 28 | 005535E0 / 00533C20 | Slot28 | ClearOuterArmourLayers | CUnitServer | H |
| 29 | 005535F0 / 00536100 | Slot29 | RefreshEquipment | server RefreshEquipment, client RefreshSkins ("equipment changed") | H |
| 31 | 00533C50 (0) / 00537360 | Slot31 | RefreshWounds | client RefreshWounds; the hero screen calls it after SetFromStats | H |
| 32 | 00533C60 / 00538140 | Slot32 | SetArmourModels | client | H |
| 33 | 00533C70 / 00538860 | Slot33 | SetWeaponModels | client | H |
| 35 | 00533C90 / 00538920 | Slot35 | RemoveWeaponModel | client | H |
| 36 | 005567A0 / 005391B0 | Slot36 | HasOtherArmour | server: armour slots 3..6; client: any weapon model (differs) | M |
| 37 | 005567E0 / 005391E0 | Slot37 | HasMainArmour | both: body / trousers / helmet | H |

Callers changed: ui_hero_screen.cpp, ui_hero_screen_click.cpp (`Slot31()` -> `RefreshWounds()`).

## unit.h: UnitStream (the CStream interface)

Slot 0 Slot0 -> DeletingDestructor (CStream 00417400, core_streams.h), H. Overrides renamed in
tests/verify/unit_capture_stream.h, verify_magic_trap.cpp and verify_map_object_save.cpp
(unit_ai.h's AIStream::Slot0 is the same slot but outside this ticket; left).

## world.h: CWorld (server 0073F928, client 0073F41C)

| Slot | Server / client | Old | New | Evidence | Conf |
|---|---|---|---|---|---|
| 13 | 0055EB40 both | Slot13 | ThisWorld | returns this; the manager context's slot 13 is "the world" (CMapObject::World 00455950) | H |
| 14 | 0055EB90 both | Slot14 | TimedObjectManager | +0x48 (world.h: timed objects; combat calls it the object factory) | M |
| 16 | 0055EBB0 | Slot16 | ItemConstructor | +0x58; item_constructor "slot 16: the constructor"; CParty +0x20 itemConstructor_ | H |
| 17 | 0055EBC0 | Slot17 | SpellConstructor | +0x50; spell_cast kContextSlotSpellConstructor; CParty +0x28 | H |
| 18 | 004BA000 | Slot18 | ItemFactory | +0x54 itemManager_ (CParty's slot 18 is ItemManager) | H |
| 20 | 0055EB50 | Slot20 | PlayerList | +0x38 players_ | H |
| 21 | 0055EB80 | Slot21 | SpecificManager | +0x44; the client's is a CClientSpecificManager (0042D690, vtable 0073BECC) | M |
| 24 | 00594CE0 / 0055E940 | Slot24 | DuplicateItem | CWorldServer::CopyItem, CParty::CopyItem | H |
| 25 | 0055EB20 (0) / 00561890 | Slot25 | TexturePath | client CString +0x224 = data path + "textures\" (Setup) | M |
| 27 | 00594220 / 00563F70 | Slot27 | GridHeightAt | CWorldServer::GridHeight, CWorldClient::ClampedGroundHeight | H |
| 35 | 0055EC00 (0) / 005619A0 | Slot35 | ClientLightManager | +0x190 built by 0068D850, GetClassName "CLightManager" | H |
| 36 | 0055EC10 (0) / 005619B0 | Slot36 | ClientParticleManager | +0x19C vtable 0073F644, "CParticleManager" | H |
| 37 | 0055EC20 (0) / 005619C0 | Slot37 | ClientManager194 | +0x194, not set in Setup; not identified | L |
| 38 | 0055EC30 (0) / 005619D0 | Slot38 | ClientManager198 | +0x198, not identified | L |
| 39 | 0055EC40 (0) both | Slot39 | Unused39 | returns 0 in both | L |

Callers changed: none. Note world_client.cpp's kSlotUnits (14, "+0x38 units list") disagrees with
the vtable (slot 14 returns +0x48 in both worlds); not touched.

## party.h: CParty (vtable 00743000)

The manager context's slots as CWorld's: 13 ThisWorld (0065F4B0, null), 14 TimedObjectManager
(0065F530, null), 15 FigureManager (0065F4C0, null), 16 ItemConstructor (0065F540, +0x20), 17
SpellConstructor (0065F550, +0x28), 19 MapObjectManager (0065F4D0, null), 20 PlayerList
(0065F4E0, null), 21 SpecificManager (0065F4F0, null), 23 FigureCache (0065F500, null). H (the
bodies are getters; names follow the world's). Callers changed: none.

## map_object_figures.h

### CObject3DSpecific (client 0073DE90, server 0073DFBC)

| Slot | Client / server | Old | New | Evidence | Conf |
|---|---|---|---|---|---|
| 14 | 0051B210 / 0051F290 | Slot14 | RemoveFromPartSets | finds the part by name (slot 16), removes it from the part sets +0x20 / +0x24, releases, returns it | M |
| 16 | 0051B2E0 / 0051F2B0 | Slot16 | FindPart | 005169A0 | H |
| 17 | 0051B2D0 / 0051F2A0 | Slot17 | FindPartById | 00516920 | H |
| 21 | 0051CB30 / 0051AC80 | Slot21 | ApplyMaterials | LoadFigures calls it last; material manager (slot 33), skin 0051C250 | M |
| 22 | 0051C130 / 0051AC90 | Slot22 | BoundsContainPoint | PtInRect on the object's dirtyRect; renderer.md: cursor picking | M |
| 23 | 0051C1D0 / 0051ACB0 | Slot23 | PartsIntersectRect | IntersectRect with the parts' screen rects (rubber band) | M |
| 24 | 0051C160 / 0051ACA0 | Slot24 | PartsContainPoint | PtInRect on the parts' screen rects | M |
| 25 | 0051C9D0 / 0051ACC0 | Slot25 | Unused25 | empty in both | L |

### CFigureInstance (= CLinkMesh, 0073D790) slots 1-3: UpdateScreenRect, HasPart, CollectPartNames (H,
link_figure.h). Its slot 4, declared HasPiece, is CLinkMesh::LoadPart (004F30D0 -> 004F7480);
the name was left (not a placeholder).

### CFigureCache (= CLinkFigureLoader, 0073D808) slots 0-2: DeleteThis, CacheSize, CacheBytes (H,
model.h).

Callers changed: none.

## Status

All 102 placeholders in the five headers renamed; the two callers updated. Merged main (A-I16,
A-H15, A-S9, A-U6, A-U7, A-W7, A-W8): one conflict, verify_map_object_save.cpp, whose ReadRecorder
main moved to tests/verify/unit_capture_stream.h (its slot 0 renamed there to DeletingDestructor).

Real signatures set (checked against the slot bodies in Ghidra; no caller converted yet):
- CWorld 18 `CEngineObject* ItemFactory()` (004BA000 returns +0x54), 20 `CEngineObject*
  PlayerList()` (0055EB50 returns +0x38).
- CParty 16 `CItemConstructor* ItemConstructor()` (0065F540 returns +0x20).
- CUnitController / CUnitServerVirtuals 19 `int RefreshVitals(char withHitPoints)` (server 005533B0
  RET 4 returns 1, client 00533BD0 returns 0). CUnitServer derives from CEngineObject, so its plain
  RefreshVitals member does not override it.
- CMapObjectVirtuals 38 `int IsUsable()`, 39 `int IsDirectUseBlocked()` (all bodies set full EAX:
  0050F0C0 / 0050F0D0 0, traps 1, levers 0052BB00 SETNZ, 0052B2D0 MOVZX byte), 42 `int
  AcceptsKey(const LockKey* key)` (0050F110 RET 4; levers KeyOpens, traps KeyFits take a LockKey).

Builds: `build.cmd verify` and `release` with no errors or warnings, all build checks 0,
check_stolen / check_x87 / check_signatures 0. check_cleanup --update: placeholder virtuals now 16
(none left in these five headers; the rest are in engine_object.h, map_spec.h, resource.h,
ui_camp.h, unit_ai.h).
