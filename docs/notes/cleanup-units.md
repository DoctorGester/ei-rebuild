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

## C11 batch 4: shims to member functions (2026-10-15)

103 more shims became members (tools/progress.py: 1,234 before, 1,131 after). Spelling only; the release objects
differ in the registration thunks and the removed `xor edx, edx` at the callers, as before.
- Class B (forwarder deleted, existing non-virtual member registered), 22: CCameraInput OnKeyUp / OnRightDown /
  OnRightUp / OnMouseMove / OnWheel; CCampPages DeconstructItem / DeconstructSpell; CConstructorPage SpellMakerItemAt /
  ItemMakerItemAt; CCreditsScreen::Draw; CHudWeaponBar / QuickItems / SpellColumn / ActionColumn Draw; CSaveLoadWindow
  Draw / Update / CloseDialog / OnDialogResult; COptionsWindow Draw / Redraw / OnDialogResult / HelpTopic. The UI window
  classes declare these as non-virtual members, so their slot forwarders were plain class B.
- Class A (body moved into the member), 81:
  - Deleting destructors, all named `DeleteScalar` (slot 0 of the vtable, RET 4, `flags` bit 0 frees): AIRef,
    AICallRecord, ScriptPointLight, CFigureManager, mapspec::Behaviour, CTransformObject, CCamera, CHoover, CLightning,
    CDXSurface, GameError (also `DeleteScalarBase`), scene::FakeUnitScene, UnitSpec, CObject3DClientSpecific,
    CPlayerConnection, CSysInfo, CUnitController, CPlayerManager, CItemManager, CRenderPreset, LeverRecord, plus
    RefPtr::DeleteDestroyed / DeleteReleased. A slot shim becomes a non-virtual member with a distinct name (the
    precedent is CMapObject::DeleteMapObject), so the registration cannot hit a vtable thunk.
  - Slot implementations with distinct names: CUnitController IgnoreArgument / IgnoreTwoArguments / IgnoreThreeArguments /
    ReturnZeroOne / ReturnZeroThree / ReturnFalse; CMapObject IgnoreArgument / AcceptsNoKey / AcceptsAnyAnimation /
    ScaleEffectChangedDefault; CMapObjectCompanion IgnoreArgument / QueryNothing / HitNothing; CObject3DSpecific DrawNothing /
    ReturnZero / IgnoreArgument; CItem EmptyName / NoUse; GameBuffer NothingMore / WriteMoreGrowing / SeekGrowing /
    ReserveGrowing / DeleteGrowing / SeekView / DeleteView / DeleteViewBase; CRenderHelper ReleasePresetRecord /
    PresetBlockOf / PresetAt / PresetStatesOf / BuiltInStatesOf; BufferedStream ReserveReadWindow / ReserveWriteWindow /
    ReadMoreWindow / WriteMoreWindow / SeekWindow; BufferedFile ReadAtFile / WriteAtFile.
  - Others: RefPtr CopyShared / CopyPlain / AssignCounted; SessionBase Register{Request,Party,Member,Map,Variable,File,
    Chunk,Award,Trade}Handler (the callers in net_client.cpp / net_server.cpp pass `(channel, handler)` now);
    LeverRecord Archive / CopyConstruct / Assign; GameBuffer CopyOut / ReadBytes (the old BufferRead); CTradeGrid::DrawGrid;
    terrain::SectorData::DrawPage.
  Rules as in batch 3. Parameters or locals that hid a data member got new names (`deleteFlags`, `tileCount`,
  `vertexTotal`). A member defined for a class in `namespace net` / `app` / `scene` closes and reopens the surrounding
  namespace or is qualified; a member of the same name as a file-local helper hides it (`::Reallocate(this, grown)`,
  `::ApplyWaterLevels(this)`, `app::RecordDestruct`). app_device_list.cpp was restructured by hand around LeverRecord.
- Verify wrappers: the calls became member calls; verify_world_deleting_destructors.cpp has an `EI_DELETE_MEMBER_CHECK`
  next to `EI_DELETE_CHECK` (still used by ObjectHolderDelete); verify_net_handler_registrars.cpp uses member pointers;
  verify_render_helper.cpp and verify_game_buffer.cpp call the members directly. Added includes where a test only
  forward-declared the class.
- Skipped, with the reason: virtual-declared members (item name slots, Hook shims, Load / Save / IsSameAs / CopyTo /
  InitFromRecord, the CSceneRenderer draws, TakeHit / SetScriptName / SetVisibilityMode / MoveObject); aliases of mfc /
  stlp templates; `void*` / `uint8_t*` selfs; pointer-to-pointer selfs (TerrainTypeAt, UnitModelAt, ObjectHolderDelete and
  the pointer assigns); EquipBuild / QuickStripBuild (`int` against `bool`); AIViewer Refresh / See / Update;
  ScaleRect*Entry (the `!self` test); CScript NewWorldScript / RunWorldScript (member signature differs); ChunkArchive
  speech fields (namespace acks); FakeUnitScene::LoadScene; Quaternion rotate / axis; ClientBufferRatio; DrawLightning;
  CFigureMesh Rasterise; the aimode:: shims (namespace-level helpers); the deleting destructors with imprecise self types
  (CUiWindow, CBaseManager / EffectManager, CObject3DSpecific server / WoundsMaterial / CCloudNoise).
- Left: 1,131. The files S16a / S12f edit (lib/stlp_*, stl_sorts, library_*, world_sync) were not touched.

## C11 batch 5: virtual-slot shims to `<Slot>Impl` members (2026-10-15)

127 more shims became members (tools/progress.py: 1,132 before, 1,005 after). Spelling only: no signature, argument
order or float grouping changed; the release objects differ in the registration thunks and the removed hook shim, as before.
The pool this batch: the `*Hook` shims of virtual slots (`self->Class::Slot(args)`), where the virtual member already holds the
body and the shim exists only because a pointer to a virtual member is a vtable thunk (rule 4).
- Method: the body is renamed to a distinct non-virtual member `<Slot>Impl` (declared next to the virtual in the header,
  "body of the slot above"), the shim is deleted and the address is registered with `EI_REPLACE_METHOD(addr, Class::SlotImpl)`.
  The virtual declaration stays (the game's vtable dispatches to the hooked address; the C++ virtual is never defined now).
  Qualified base calls (`CItem::Load(stream)` in the subclasses' Load) and the verify wrappers call `...Impl`; a call on a
  value object (`file.stream.Write(...)` in world_map_files.cpp) is a static call and had to be renamed too (the linker finds
  every such miss). check_thunks passes: the registered names are not virtual.
- Converted (127): items: CItem and subclasses Load / Save (12), IsSameAs (6), InitFromRecord (6), CopyTo (7), GetUse (1),
  the 16 name slots (ModelName / FirstListName / FigureName / PictureName; the shim built the string in the caller's result
  with placement new, the member now returns it by value, which is the same hidden-pointer ABI); GroundPrint / Footprint /
  BloodPrint Tick / Load / Save (8); CLinkMesh slots (14) and CLinkFigureLoader (4); net: SessionBase (10), ClientBase /
  ServerBase (11), FileStream Read / Write, NetLink / LoopbackLink / ReplayLink / FramedLink Send / Receive (9); CMappedResource
  and CMMIOResource (8); CResourceManager (5); CBaseManager Load / Save / Init / archive and file slots / CreateObject /
  ReleaseObject (10).
- Skipped, with the reason: shims whose call passes `*name` for a pointer parameter (CTransformObject CopyTransform /
  CombineRotation / UpdateWorld, CEngineObject::CopyFrom, Terrain::CombineRotation: the member takes a reference, the shim a
  pointer); CMemoryStream::Write (the shim returns `position_`, the slot is void); void* / uint8_t* selfs and the
  `WindowFromInput` shims of ui_window_delete.cpp / ui_control_delete.cpp (the self is the input base subobject, not the
  window); the deleting destructors with imprecise self types (CUiWindow, CBaseManager / EffectManager, CObject3DSpecific
  server / WoundsMaterial / CCloudNoise: no precise class to hold the member yet); LightningManagerInit / VisFogManagerInit /
  SessionTakeNew (the argument differs from the parameter); the call-through blocks (`XxxOriginal(a0, nullptr, a2)`).
- Verify wrappers: tests that called `Class::Slot` qualified call `Class::SlotImpl` (verify_item_*, verify_party_items,
  verify_ground_print, verify_link_*, verify_net_*, verify_resource*, verify_base_manager).
- Left: 1,005. What remains is mostly the call-through blocks, `void*` selfs, pointer-to-pointer selfs, aliases of mfc / stlp
  templates and the files S16a / S12g own.

## C11 batch 6: more virtual-slot shims to `<Slot>Impl` members (2026-10-15)

64 more shims became members (tools/progress.py: 1,005 before, 941 after). Same method as batch 5 (spelling only: no signature,
argument order or float grouping changed): the body is renamed to a distinct non-virtual `<Slot>Impl` member declared next to the
virtual ("body of the slot above"), the `*Hook` shim is deleted, the address is registered with `EI_REPLACE_METHOD(addr,
Class::SlotImpl)`, and the qualified base calls and verify wrappers call `...Impl`. The batch was done with a small script that
reads the shim, checks that the shim's return type and parameter types equal the member's declaration, and does the rename, the
declaration and the registration; it skipped what did not match and the linker found the qualified calls it missed.
- Converted (64): CMemoryStream Read / Seek / CopyFrom and CStream::CopyFrom; CTransformObject SaveTransform / LoadTransform;
  CCloudNoise MakeLayer / Generate; the manager factories CItemManager / CLightFactory (NewObject, CreateObject) / CMaterialManager
  / CSpellManager / CServerSpecificManager NewObject and CTextureManager NewObject / CreateObject / ReleaseObject; MagicTrap::
  ForgetObject; CMapObject AddEffect / PlaceOnGround / SetServerData / AttachObject / TakePrototype / ApplyScience; CUnit
  SetStunned / AcceptsAnimation / TakePrototype / AttachObject / ScaleEffectChanged / SetStats / PlaceOnGround / AdvanceTime;
  PrintRecord / PrintRecord2 Serialize; ClientSession and ServerSession slots (CollectReplicated, Tick, CreateEntry, Describe,
  OnMaster, Admit, OnClientLost, OnClientLeft); CWorldHandle / CClientHandle / netlink::DriverSession TakeNew; CRenderHelper Record
  and ReleaseBlock; CSceneRenderer DeleteRenderer / SetTexture / SetColour / DrawLineList / DrawFigure / DrawFigureLit; Terrain
  RunTick / Create / Destroy; CWorld and CWorldServer SetTime / SetTimeSpeed.
- The lightning and vis-fog managers' Init (0x00562D70, 0x00562DA0) are registered straight to `CBaseManager::InitImpl` (the same
  member at three addresses); their two shims, their declarations in world_class_slots.h and the verify calls are gone.
- Two verify details: verify_models.cpp constructs PrintRecord / PrintRecord2 scratch copies, which emits their vtables, so it
  defines the two virtual `Serialize` forwarders to `SerializeImpl` (the verify build only). The qualified-call rename does not see
  dependent names (`self->Record::Serialize` in a template) or calls on a value object (`pending.Seek(...)`, `text.s.Seek(...)`):
  those were renamed by hand after the linker named them.
- Skipped, with the reason: shims that dereference a pointer parameter (CTransformObject CopyTransform / CombineRotation /
  UpdateWorld, CEngineObject::CopyFrom, Terrain::CombineRotation); CMemoryStream::Write (the shim returns `position_`, the slot is
  void); DriverLink::SendBytes (the shim returns `bool ? 1 : 0` as int); the CInterface3D Scale*ToScreen / FromScreen entries (a
  null-self widescreen branch before the call); the UI input shims (`WindowFromInput<T>(input)->OnX(...)`: the self is the input
  base subobject); deleting destructors; the `XxxOriginal` call-through blocks; free `__fastcall` bodies of virtual slots that have
  no member to hold them yet (LightningNewObject, VisFogManagerNewObject / Draw, WeatherDraw, EffectNewObject, ManagerNewObject,
  RendererSetFogColour / SetBlend, UiContainerDraw ...: these are class-A conversions, a member to add and the body to move).
- Left: 941. The virtual-slot forwarders are used up; the rest is call-through blocks, UI input and `void*` selfs, mfc / stlp
  aliases, and free bodies that need a member added first.

## C11 batch 7: free bodies of virtual slots to members (2026-10-15)

47 more shims became members (tools/progress.py: 952 before, 905 after on this branch; 908 with main's merged in). Spelling only: no signature, argument order or float
grouping changed. This batch took the free `__fastcall Fn(T* self, void*, ...)` bodies of slots that had no member yet: the body
moved into a non-virtual member (`<Slot>Impl`, or the slot's own name where the class has no virtual of that name), declared next
to the virtual it implements ("body of the slot above") or at the top of the class, registered with `EI_REPLACE_METHOD`, and the
verify wrappers call the member. A script did the move (the `self->x` of the body became `x`, or `this->x` where a parameter or
local of the same name exists; comments and strings untouched; a function inside a `namespace` closes and reopens it around the
member); the builds found the rest. A member of a named namespace needs qualified helper names (`misc400::New`, `unitdb::Destroy...`),
a helper of the same name as a member needs `::` (`::DrawStrip`, `::DrawRain`, `::UpdateRegionTips`), and a parameter or local that
hides a data member is a C4458 warning here (renamed: `newName`, `parameterName`, `currentWorld`, `currentMode`).
- Converted (47): CSceneRenderer SetFogColourState / SetBlend / CreateRenderer / DrawRectangle / DrawSprites / DrawStrip (the ribbon
  draw) / DrawTriangles / Polyline; CLightningManager NewObject and CLightning Draw; ClientArrayManager (vis fog) NewObject / Draw;
  CRainSnow Draw; CUiContainer DrawContainer (00600EF0) and DrawChildren (00600F70), CBriefingScreen, CMainMenu and CHeroEquipPanel
  Draw; CUnit TakeHit / ObjectDeleted / SetScriptName and CMapObject SetVisibilityMode; CWorldClient MoveObject / MoveObject2;
  UnitLevelRecord and UnitPrototypeRecord DeleteScalar; FiguredUnitRecord / DressedUnitRecord CopyConstruct; RefPtr DeleteScalar;
  GameBuffer ReserveView (the fixed view's empty reserve, two addresses); AIMode Priority50 / EnterIdle / ClearAction /
  LookAroundExecute / PlayerPriority / TamedPriority (the shared mode slots, out of `namespace aimode`); ScriptDefinition
  AddParameterImpl; ClientSession ReadServerListImpl and BufferRatioImpl (a double returned in ST0, as before). The pointer
  parameters dereferenced by the old shims (CTransformObject CopyTransform / CombineRotation / UpdateWorld, Terrain
  CombineRotation, CEngineObject CopyFrom): the member takes a reference where the shim took a pointer, which is the same
  argument; the members were renamed to `...Impl`, the shims deleted, the qualified base calls (`CTransformObject::CombineRotation
  (parent)` in Terrain and CObject3D) and verify wrappers call `...Impl`.
- New classes: `CClientSpecificManager` and `CEffectManager` (base_manager.h, both empty subclasses of CBaseManager) hold the
  object factory 0042D700 and 0067E170 and, for the effect manager, its deleting destructor 0055F380. Nothing else about their
  vtables is declared; the other slots of those managers stay free functions.
- Skipped, with the reason: RasteriseFigureMesh (a parameter type, `IntPoint`, is local to the .cpp); FakeUnitLoadScene (the
  helpers are in `namespace fakeunit` behind a typedef of the scene); WindowDrawBlended / WindowDrawAlphaRef (the class of
  0062EFC0 / 0065A6D0 is not known); EquipBuild / QuickStripBuild (`int` to `bool` glue); AIViewer / AIAttention hooks (the shim
  converts a memory pointer to the brain); CMemoryStream::Write and DriverLink::SendBytes (the shim returns a value for a void
  slot / `bool ? 1 : 0`); the deleting destructors of imprecise classes (CUiWindow trade panel, CObject3DSpecific server,
  WoundsMaterial, CCloudNoise base); string_map.cpp's panel (the class is file-local, `void*` selfs); the UI input shims and the
  other `void*` selfs; ReadMobChunks / LoadMobFile (free functions over the generic `Chunk` reader); the acks / ChunkArchive field
  readers; the rest are `XxxOriginal` call-through blocks and aliases of mfc / stlp templates.
- Left: 905. What is left is almost only call-through blocks, UI input and `void*` selfs and template aliases: the pool of free
  bodies with a known class is used up.
- Verification: build verify and release with no errors or warnings, every check at 0 (check_stolen, check_x87, check_signatures,
  check_thunks, check_registrations: 4,774 method registrations). `work.py test C11 --golden --jobs 4` on the merged tree: 128 runs,
  all PASS; of the 56 goldens one differs, render_movie_present (vanilla), only in the script `#gobject` field (a heap-layout
  field, SV7's); not chased.

## C11 batch 8: constexpr address constants and own At() helpers (2026-10-15)

`constexpr uintptr_t kX = 0x00...` constants: 369 before, 249 after (tools/progress.py); files defining their own `At()`: 4 before, 2
after; reinterpret_cast 6,145 before, 6,083 after. Spelling only: no signature, argument order or float grouping changed. A script
(`rep.py`, scratch) deleted the constant and replaced its uses by the home it stands for; the builds found the rest.
- Vtables (about 60): the file-local constants of script_copy / script_destroy / script_blocks (`scriptvtable::k...` of script.h and
  `mfc::impl::*Class::Vtable()`), fake_unit, magic_trap, map_spec, map_spec_unit, ai_join, ai_plan, ai_refresh, base_manager,
  world_spells, world_loot, world_zone, unit_create, net_client_awards, particle_create, lightning_start, and `kLookArrayVtable` /
  `kLookArrayBaseVtable` of unit_ai.h (six users and one verify file). The helpers that took the address as `uintptr_t` now take
  `void*`; `ei::SetVtable(void*, const void*)` (vcall.h) and `CArray::Construct(const void*)` (mfc.h) take a vtable pointer, so
  no cast is added. Two script argument-array classes (`ArgObjectsClass`, `ArgGroupsClass`, mfc_slots.h) were the only new homes of
  this family. fake_unit, map_spec, map_spec_unit and particle_create lost their local `SetVtable` copies.
- Strings: `game_strings.h` gained `QuestionMarkText`, `ZoneSuffix1/2/3Text` and six interface sound names (cancel, ok, select,
  put on, put off, rotate) for the net_server_* / net_client_commands / ui_camp / ui_hero_screen_click / ui_menu_keys files; others
  use the existing `NoneText`, `SpaceText`, `flowdata::kDot`, `kQuestVariablePrefix`, `kSlotFolder`.
- Floats: `worlddata::One()` / `Zero()` and a new `Half()` (ai_footprint_shapes, ai_line_step, net_server_tick, ui_mesh_build).
- unit_vitals: its fifteen constants are the `hpVal`, `mpVal` and `damageLevel*` members of `LogicSettings` at 007C53F8.
- Globals: `TheGameFlow()`, `worlddata::InterfaceManager()` / `QuestBriefings()` / `FlushManipulator()`, `UiClientWorld()`,
  `SkillDb()` / `unitdb::SkillLevels()`, `WorldServer()`, `gameapi::RegQueryValueExA` (hook/paths.cpp), and the
  `BehaviourConstruct` / `BehaviourDestruct` functions called directly by the eh-vector iterators in map_spec_unit.
- Own `At()`: atexit_400 uses `&ei::GameData<char>(address)`; ui_create_screen uses the new `ei::OffsetPtr(base, offset)`
  (game_data.h). The two left are members, not raw-offset helpers: `RecordTable::At` (item_db.h, about 110 call sites) and
  `CMap::At` (mfc_list.h); renaming them to `RecordAt` / `ElementAt` is a later batch.
- Local renames: `near` / `far` locals in particle_create and unit_ai_modes (the Windows macros come in with mfc_array.h).
- Left, by family (249): class `kVtableAddress` statics (about 70, already the class's home); vtables with no home (map_spec*,
  ui_layouts, ui_scroll_text, resource_stream, misc_400, net_coder / net_link / core_streams / console shared constants);
  strings (map_database 24, quest_database 10, ui_saveload, menu_rects, world_briefing, unit_equipment, world_zone, the
  net_session_tick / net_connection log texts, ui_console_window / ui_text newlines); float constants in the effect headers
  (spell_effect_internal.h, world_effect_internal.h), spell_effect_update, options, ui_mesh_build, ai_refresh tables; function
  addresses (at-exit destructors, eh-vector callbacks that are members or live in src/standalone); image addresses in hook/ and
  standalone/; globals with no accessor reachable from the file (unit_ai.cpp's world and game time: world.h clashes with its local
  `GameTime()`, console stream, the candidates list, the script queue, net_server_tick's master host).
- Verification: build verify and release with no errors or warnings on the merged tree, every check at 0 (check_stolen, check_x87,
  check_signatures, check_thunks, check_registrations: 4,774 method registrations, check_cleanup, ei_vectors).
  `work.py test C11 --golden --jobs 4`: 128 runs, all PASS; the 56 goldens are all the same.

## C11 batch 9: the last own At() members and string constants with typed homes (2026-10-15)

Files defining their own `At()`: 2 before, 0 after (tools/progress.py). `constexpr uintptr_t kX = 0x00...`: 249 before, 187 after.
Spelling only: no signature, argument order or float grouping changed.
- `RecordTable::At` (item_db.h) is now `RecordAt` (125 call sites in src/ and tests/, including the unqualified calls of
  `UiRecordTable`, which derives from it; `UnitPrototypeTable::At` and `CMapPtrToPtr::At` are other classes and keep their name).
  `CMap::At` (mfc_list.h, the hash map's `operator[]`) is now `ElementAt`, with `Abi::ElementAt` in mfc_slots.h and the mfc_list /
  mfc_serialize verify tests. The verify-test files with their own `At<T>(object, offset)` helpers are not in the count (tests/).
- String constants of five files moved into `game_strings.h` as accessors in a namespace each (they return `Text(address)`, so
  ei_vectors still reads the stand-in data): `game::mapwords` (map_database, 23 words; "none" uses `NoneText()`),
  `game::questwords` (quest_database, 10 names plus `ListKey(i)` for the six briefing list keys), `game::savewords` (ui_saveload,
  12), `game::briefingwords` (world_briefing, 8), `game::zonetexts` (world_zone, 4 error texts), `game::equipmenttexts`
  (unit_equipment, 4). The helpers that took the string's address as `uintptr_t` (map_database `Is`, quest_database
  `ReadBriefingHeader` / `ReadListInline`) take `const char*`. `ListKey` is a switch, not an array of addresses, so
  check_cleanup's raw-address count stays down (834 from 902 at the start of the batch).
- Left (187): the families of batch 8 minus the strings above; menu_rects (its `Text(file ? kCantParse : kCantOpen)` needs a
  small rewrite), net_session_tick / net_connection log texts, net_messages.h (17), spell_effect_internal.h and
  world_effect_internal.h floats, class `kVtableAddress` statics, function addresses.
- Verification: build verify and release with no errors or warnings, every check at 0 (check_stolen, check_x87, check_signatures,
  check_thunks, check_registrations, check_cleanup, ei_vectors) on the tree merged with main. `work.py test C11 --golden --jobs 4`: 128 runs, all PASS; the 56 goldens are all the same.

## C11 batch 10: network and menu texts, effect and AI float constants (2026-10-15)

`constexpr uintptr_t kX = 0x00...`: 187 before, 118 after (tools/progress.py); check_cleanup raw addresses 834 before, 765 after.
Spelling only: no signature, argument order or float grouping changed.
- Strings in `game_strings.h`: `game::menutexts` (menu_rects: `menus.reg` and its two errors; the `Text(file ? kCantParse : kCantOpen)`
  became `file ? CantParse() : CantOpen()` passed to `FatalError`), `game::nettexts` (17 log lines of net_session_tick and
  net_connection, including the six literal "Can't handle ..." texts; `LogAddress` / `LogText` / `LogConnection` / `LogLine` take
  `const char*`, the connection's "after" text is `nullptr` when absent), `game::menukeytexts` (ui_menu_keys), `game::consoletexts`,
  `game::weathertexts` (the two thunderstorm script formats), and `NewLineText()` / `PathSuffixText()` (ui_console_window, ui_text,
  net_server_map).
- Floats in `world_data.h` (`worlddata`): 22 effect constants of spell_effect_internal.h, spell_effect_update.cpp and
  world_effect_internal.h (`OneRef`, `AcidRayScale`, `StrikeHeight`, `ScorchAlpha`, ...) return `const float&` (`ExeFloatRef`), so
  `ConstAt` / `ConstPtr` of both effect headers take a `const float&`; `PowerScaled` / `LightRiseScaled` too. ui_mesh_build,
  ai_line_step, ai_footprint_shapes lost their local `GameFloat`; `AnnouncePeriodRef` and `QueueWaitLimitRef` keep the memcpy / pointer
  argument; the ai_refresh tables are `LevelHeights()`, `OccluderCosts()`, `OccluderDamping()`, `MarkTable()`, `DampScale()`. The unused
  `kPi` of ui_mesh_build was dropped.
- Left (118): class `kVtableAddress` statics (about 75, net_messages.h's 17 among them), vtables with no home (map_spec*, ui_layouts,
  ui_scroll_text, resource_stream, magic_trap, lightning_start, misc_400, fake_unit), function addresses (at-exit destructors,
  eh-vector callbacks), globals (script queue, candidates, master host, state block alpha), options' two float factors (they feed
  naked `FtolProduct` / `StoreUnsignedProduct` that take an address), tests/ and hook/standalone files.
- Verification: build verify and release with no errors or warnings, every check at 0 (check_stolen, check_x87, check_signatures,
  check_thunks, check_registrations, check_cleanup, ei_vectors) on the tree merged with main. See the golden result below.
- `work.py test C11 --golden --jobs 4` on the merged tree: 128 runs, ALL PASS; 54 goldens the same, 2 differ (levers, ui_saveload_delete) only in script #gobject (heap layout, SV7's).
