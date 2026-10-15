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

## F2: unknown fields of the units, AI and items files (2026-10-14)

Every `unknown_XXX` member of unit.h, unit_db.h (+ unit_db_destroy.cpp), unit_ai.h, ai_map.h, item_db.h,
unit_looks.h, item.h, unit_client_models.h, class_templates.h, unit_client_sounds.cpp, party*.h, item_stock.cpp,
item_db_text.h, ai_shape_cache.h, ai_refresh.cpp and unit_ai.cpp (172) is named or `unused_XXX`. Method: all of
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

## C11 batch 1: shims to member functions (2026-10-14)

63 `__fastcall Fn(T* self, void*, ...)` shims became `T::Fn(...)` registered with EI_REPLACE_METHOD (counted by
tools/progress.py: 1,274 before, 1,211 after; the ticket's "497" does not match that metric). Release objects
differ only in the registration thunks (EI_REPLACE_METHOD's form) and the removed `xor edx, edx` at the callers.
- Class A (body moved into the member, header declaration added): AIPath StandAndTurn / StandAndTrack /
  StandAndTurnUntil / StandStill / LoadPath / SavePath, FootprintCellSet AlongDirection / DiscAt,
  UnitShapeCache::Get, MotionWaypoint::Construct, CTextManager::PeekWord, CLightning::DrawGlow, CCamera
  GetRange / OpenFile / RecordFrame, NamedPartyRecords Find / Add / Remove, RefPtr FromRawCounted /
  AssignHolder / AssignObject, SpeechFilter Replaced / Finished, CUnitClientSpecific::GetAttackParams,
  Behaviour Read / Write, ArmourSpell::Save, CEngineObject SetFlagBits / ClearFlagBits, CUnit::CarriedItem.
- Class B (the shim only forwarded to an existing non-virtual member, so it is deleted and the member
  registered): CTransformObject WorldMatrix / TiltTowards / TurnTowards / RollTowards / Rotate /
  TurnUpTowards / SetHeading, PlayerMode UpdateCombat / AddPartyTargets / CollectParty / StaysWithParty,
  AIViewer::Prepare, AIAttention IsUsable / Choose, ObjectListIterator::First, the 16 lib::AlignedArray<T>
  SetSize / Add / SetAtGrow / Copy instances in aligned_arrays.cpp, ObjectSet SetSize / SetSizeDiscardStep.
- Skipped: text_reader Open / ReadLine / ReadToHash / Split / FindLine, ClassTemplate::SetLevels and
  RefPtrShareReturn (callers in ui_* / net_*: wait for W2 / F5); CLightning DrawLightning (virtual `Draw` of the
  same name, C2535); UnitPrototypeAt (another file has a different forwarder of the same name); FindFigureAnimation
  (map_object_figures.h, F4a); QuaternionRotate (src/lib/vecmath.h, F5); shims on aliases of lib / mfc
  templates (AICallArray, AIReportArray, AILookArray, PatrolPointArray, CorpseList, TextSourceList, TweenArray,
  SmoothEdgeArray, PathCellList, SearchGrid<T>, ClassTemplateVector: no member can be added); shims of virtual
  slots (rule 4: a vtable thunk cannot be registered by member pointer); shims with a void* / uint8_t* self.

## C11 batch 2: shims to member functions (2026-10-14)

76 more shims became members (tools/progress.py: 1,211 before, 1,135 after). Release objects differ as in batch 1 (thunk form,
no `xor edx, edx` at the callers). The batch has fewer than the 100 asked for because the convertible pool is used up:
of the 1,135 left, about 430 are in ui_* / net_* / widescreen / renderer / PF1a files or in the originals_*.h headers, about 190 have a `void*` self,
and nearly all of the rest are call-throughs to originals (`XxxOriginal(a0, nullptr, a2)` in the world_*, combat_*,
item_*, script_* helper blocks; progress.py counts them but there is no body to move), shims of virtual slots
(`*Hook`, Delete / Ignore / Draw slots, LinkMesh, CItem*, AIMode, CUnitController) and aliases of lib / mfc templates.
- Scripts (script.h, script_*.cpp): CScript FindArea / FindBlock / DeleteArea / NewCommandList / NewTempObject /
  RunCommands / CreatePointLight / MovePointLight / DeletePointLight / LoadState / SaveState; ScriptIfThen Load / Run;
  ScriptDefinition Save / BindCall; SSCCmd Load / DestroyElement; ScriptSubCall Load / DestroyElement; ScriptDecl::Save;
  ScriptRun::Save; ScriptVars Load / AppendFrom; and the four ScriptNamedValues<T> instances (float, mfc::CString,
  ScriptObjectRef, ScriptGroup) of Load / Save / Append / CopyConstruct / FindName (FindName is one template body
  registered at four addresses; the others are explicit specializations declared in script.h).
  The savegame CScript::Load / Save became LoadState / SaveState: EI_REPLACE_METHOD cannot take an overloaded name.
- AI: CUnitLayer SetSmall / SetLarge / MergeSmall / MergeLarge; SearchGrid<T> Resize / CopyFrom / Construct (explicit
  specializations for the element types the game uses; SearchGrid is a real struct template, unlike the aliases of
  batch 1).
- Figures and models: CFigureManager FindModel / LoadFigureBones / LoadFigureBounds / LoadSkeletonLinks,
  FigureModel::FindAnimation, MeshFace::ComputeNormal, NamedTexture::Copy, CLinkFigureLoader::Construct,
  CBaseManager::FindFigureAnimation, CMapObject::SetScriptPosition, CMaterial SetDiffuse / SetAmbient / SetSpecular,
  CWorldClient::BuildUnitSkins.
- Small: hash400::Table (nine registrations: ObjectsFind / Lookup / RemoveKey / SetAt, PositionAt, PositionsRemoveKey,
  SpareCountAt), UnitPrototypeTable::At, BufferCursor::Start, Vector3 ConstructScene / Assign.
- Skipped again, with the reason: ClassTemplate::SetLevels and RefPtrShareReturn (callers in ui_camp_create.cpp /
  net_session_base.cpp: wait for W2 / F5); Quaternion::Rotate (defined inside namespace mapspec, a member cannot be);
  CFigureMesh::Rasterise (figure_mesh.h would need texture.h, which pulls in windows.h and renames GetClassName);
  CScript NewWorldScript / RunWorldScript and ScriptDefinition::AddParameter (script_parse_load.cpp already declares
  members of those names with other parameter types, so the definitions would change a signature); Chunk
  ReadMobChunks (MobRecords is not declared in chunk_stream.h) and FakeUnitScene::LoadScene (namespace fakeunit vs
  scene); ChunkArchive speech fields (namespace acks); DeviceRecord (app_device_list.cpp, the lever-record rename
  ticket); UnitModelAt / TerrainTypeAt / ObjectHolderAssign (the self is a pointer to a pointer, not a class).
- Fixed on the way: ScriptNamedValues<T> declared Append / CopyConstruct for ScriptGroup only; the declarations now
  take T. Parameters that hide members (C4458) were renamed (`x_`, `first`) in Vector3::ConstructScene and
  BufferCursor::Start; a local that hid a member was renamed in script_destroy.cpp and script_exec_run.cpp.
- Left: 1,135, almost all of them the categories listed at the top of this section. The next real gain is the ui_* / net_*
  callers after W2 / F5 (text_reader, ClassTemplate::SetLevels, RefPtrShareReturn), then the call-through blocks as
  their originals are ported.

## C11 batch 3: shims to member functions (2026-10-15)

100 more shims became members (tools/progress.py: 1,134 before, 1,034 after). W2 and F5 are done, so the ui_* / net_*
callers skipped in batches 1 and 2 are converted. Release objects differ only in the registration thunks and the removed
`xor edx, edx` at the callers, as before.
- Class B (the shim only forwarded to an existing non-virtual member: deleted, member registered), 64 + 3: CInterface3D
  Activate / PushWindow / CreateScreen / ShowMessage / OpenHelp / OnKeyDown / MessageText / LoadState; CBattleScreenSetup
  OnUnitSpeech / UnitName / Create / OnQuestVariable; CBriefingScreen Create / Update / MovePerson; CCampPages DoAction /
  UndoAction; CConstructorPage SpellMakerItemAt / ItemMakerItemAt / DrawSpellMaker / DrawItemMaker; CCreditsScreen and
  CExchangeScreen Draw; CCursor::Blit; CHeroEquipPanel ShowWeapon / ShowArmour / ItemAt; CHeroQuickStrip ShowEntry / ItemAt /
  Draw; CGlobalMap Create / Update; CMpHeroScreen Draw / Update / Close / MakePreview / ShowHero / ChooseFigure;
  CHeroStatsPanel Build / Update / DrawRow / DrawPart; CTradeGrid / CRepairGrid ItemAt; CRepairGrid::Draw; CHudSpellColumn
  Click / ClickAlternate; CHudActionColumn Select; CHudQuickItems Pick / UseNow; CItemInfoPanel Build / Draw; CUiItemStrip
  Draw / DrawFigures; CUiWindow::CopyImage; CUiMeshBuilder Framed / Disc / Ring; CUiSurface FrameRect / Greyscale
  (and the already-declared SwapContents / CaptureScreen / Rgb565Copy, whose three shims sat in one namespace block);
  CTradePanel Settle / SelectHero; FearMode Evaluate / Execute; CLightning::Start; GameInfoRecord Write / Read.
  Candidates were picked by tools-side scripts: the shim body is one call of `self->Member(args)` with the same
  parameter types as the member's declaration, and tools/check_thunks.py's virtual test says the member is not virtual.
- Class A (body moved into the member), 33: CTextManager Open / ReadLine / ReadToHash / Split / FindLine (text_reader.cpp;
  the two forwarder members of item_db_text.cpp are gone, the callers in ui_* call the members), ClassTemplate::SetLevels,
  RefPtr::ShareReturn (nine addresses), CDXSurface Construct / ConstructFromDesc / ConstructFromPicture / LoadPictureData /
  Fill / Blt, CTextureDevice BoundsOfVertices / BoundsOfProjected / DrawIndexed / DrawTwoTextures / DrawMeshBlocks /
  DrawMeshBlocks2 / DrawFlatBlocks / SetMaterial / DrawIndexedDirect (renderer_sprites.cpp, 00503680) / DrawTlTriangles,
  FigureMeshDrawer DrawBounds / DrawLit / DrawPlain, CBinkVideo::Frame, CCursor::SaveBackground, CUiSurface::PixelAt,
  CHudUnitPanel::DrawUnitInfo, ClientSession RecordTrade / StartParty, MasterLink::Construct.
  Rules the conversion followed: `self->x` becomes `x`, or `this->x` when the function also has an unqualified `x`
  (a local or parameter), so the meaning cannot change; parameters that hide a data member got an `In` suffix
  (`deviceIn`, `widthIn`, ...) and the locals that did became `binkHandle`, `surfaceHolder`, `newSurface`, `owner`.
  The device draws' members keep their file-local helper names free (`DrawMeshBlocks`, `DrawFlatBlocks`: the helpers
  `DrawMesh` / `DrawFlat` in the same files would have been shadowed by members of those names).
- Verify wrappers: the prototypes of the removed shims were deleted from the tests and the calls became member calls;
  verify_figure_part_bounds.cpp and verify_device_bounds.cpp, which took the shim's address, got a small
  `...Ours` function that calls the member. Added includes of texture.h / link_figure.h where a test only
  forward-declared the class.
- Skipped, with the reason: every shim of a virtual slot (the CSceneRenderer draws, Hook shims whose target is virtual
  by check_thunks, GameBuffer / BufferedStream slots, deleting destructors); ClassTemplatesBuild / Find, ReportsAdd,
  CallsHear, the TextSources* shims and the other shims on aliases of lib / mfc / stlp templates (no member can be added);
  ScaleRectToScreenEntry and the four other CInterface3D entries (they test `!self` before calling: ECX 0 is reachable
  with Rebirth's code); EquipBuild / QuickStripBuild (the shim takes `int`, the member `bool`: a signature change);
  AIViewer Refresh / See / Update (`uint8_t*` memory against `AIBrain*`); ClientBufferRatio (unnamed self; the buffer is
  the object); DeviceHolderAssign / TexturePointerAssign and the other pointer-to-pointer selfs; the shims with a
  `void*` self (about 190 of the rest); the call-through blocks (`XxxOriginal(a0, nullptr, a2)`) that have no body.
- Left: 1,034, mostly the categories above. What remains of the convertible pool is small: the virtual-slot shims need a
  different approach than member registration (a registration that bypasses the vtable thunk), and the call-through
  blocks go as their originals are ported.
