# DR-C5: cleanup draft of ui_hud.cpp, ui_surface_create.cpp, world_client_draw.cpp

Branch draft-dr-c5. Written without building (draft pass): nothing here has been compiled or run.
The finishing worker builds `verify` and `release`, runs check_cleanup (then `--update`),
check_forwarders / check_thunks / check_signatures, and the scenarios that reach the three files:
- ui_surface_create.cpp: any scenario that opens a window (the surfaces are made for every screen;
  verify_dx_surface.cpp wraps all four functions);
- ui_hud.cpp: an in-game scenario with HUD clicks (SelectUnit 00638EF0, OrderAt 00636F10, the quick
  item release 0063DB20, the portrait clicks in a multiplayer client);
- world_client_draw.cpp: every in-game frame (Render 00569E40 / DrawPass 0056AFE0).

**Merge order: world_client_draw.cpp depends on DR-C1** (draft-dr-c1, head 86931a0 when this was
written): it uses DR-C1's CWorldClient / CWorld member names, which main's world.h does not have yet
(heroes_ +0xB4, screenEntries_ +0xF0, objectGrid_.rows_, rangeFirstX_..rangeY_ +0x138..+0x14C,
owned1AC_ / owned1B0_ / listManager_ +0x1A0 / arrayManager_ +0x1A4, list1B8_ / list1BC_ /
list1C0_, ribbons_ +0x6F4, list7BC_, manager40_ (CWorld +0x40), types SectorList, ScreenEntry).
Merge DR-C1 (and DR-C2 before it) first. One member is in neither: **add `CCamera* camera_;` at
CWorldClient +0x210** (split from DR-C1's `unknown_210[0x224 - 0x210]`: `CCamera* camera_;` then
`std::uint8_t unknown_214[0x224 - 0x214];`, static_assert 0x210; world.h then needs
`class CCamera;` forward declared). I did not edit CWorldClient's members to keep out of DR-C1's
hunks.

## Header changes (all additive: data members, accessors, constants; no slot renamed)

- ui.h:
  - `struct CDXSurface;` forward declaration; `class CHudWeaponBar; class CHudPortraits;` forward
    declarations before CBattleScreen.
  - CUiSurface: `kVtableAddress` (00740F28), `kFromPicture` (flag 1, 005FCF80), `holder_` is now
    `CDXSurface*` (was void*; its only other users test it for null: verify_ui_window_helpers.cpp),
    `unknown_34` is an `int` (was a byte array; the constructors store 0 there), static_asserts for
    interface_, width_, height_, keptSize_, unknown_34.
  - New accessors after CUiSurface: `UiSurfaceKeptBytes()` 007C7E74, `UiSurfaceBytes()` 007C7E78
    (from the old file's literal uses and the SwapContents comment), `GameDeviceLock()` 007AEE78
    (twenty other files keep a local `DeviceLock()` with the same address; they can switch).
  - CBattleScreen: `pendingExit_` +0x188, `exitPending_` +0x18C, `orderKeys_[6]` +0x18D,
    `unknown_193`, `weaponBar_` +0x194, `portraits_` +0x1A4 (CHudPortraits*), `itemUseKey_[3]`
    +0x220, all split from unknown_188 / unknown_1A4; static_asserts. Evidence: the old Field<>
    offsets in ui_hud.cpp and CBattleScreenSetup in ui_camp.h (same object: savedSelection_ /
    savedFlag_ / orderKeys_ / weaponBar_ / portraits_ at the same offsets; its unknown_220 is the
    12 bytes). Plus `VirtualUnitSpeech(id, event, extra)`: vtable slot 32 (006366F0,
    CBattleScreenSetup::OnUnitSpeech, RET 0xC).
  - `kUnitPtrArrayVtable` (007402AC, the game's CArray<CUnit*>; ui_battle_commands.cpp,
    player_server_handlers.cpp, ui_camp.cpp, ui_create_screen.cpp still spell it out).
- game_strings.h: CantGetSurfaceDescriptionText 0078E6C4, MmpExtensionText 0078FB4C,
  SoundOnOffText 007935BC, TerrainCoordsText 00794418, SpaceText 0078DA40, ZoneVariablePrefixText
  007900E8 (all from the old code's comments).
- console.h: `console::Out7C5760()`, the ostream at 007C5760 (its filter is not identified).
- originals_world.h: `ClientGlueObject()` 007AFEA5.
- originals_render.h: `SceneContext()` 007AF060 (the object DisplayObject() returns, without the call).
- map_object.h: CMapObject `attached` +0x94 (split from unknown_044 into unknown_044[0x94 - 0x44],
  attached, unknown_098[0xA4 - 0x98]; static_assert). Evidence: map_object.cpp's 00516D50 comment
  (type 0x46 objects at +0x94), map_object_figures.cpp, map_object_lifetime.cpp's At<>(owner, 0x94).
  No slot touched. DR-C1/DR-C2/DR-C3 do not change unknown_044 (checked their heads), so this
  should merge cleanly.
- terrain.h: Terrain `unknown_100` (void*, +0x100; split from unknown_000). Only DrawPass's null
  test is known.
- texture.h: CTextureDevice `zEnable_` +0x60 (render state 7 is put back from it, 00569E40),
  `unknown_158` +0x158 (0 around the sky and hit point draws), static_asserts; the class grew past
  +0xAC (it is never allocated by our code; no sizeof assert existed). `RenderDeviceObject()` 007AF080.
- model.h: `ScenePipeline()` 007AF068 (a CTransformPipeline: the old code's matrix stacks at +0x1C,
  +0x24, +0x3C, +0x5C are stacks_[1], [2], [5], [9]; world_client_view.cpp's local RenderDevice
  view of the same object can switch to it).
- world.h (not CWorldClient): `FrameObjectList`, `enum class FrameList`, `FrameObjects(list)`
  (007AF508 + 0x10 * list) and `FrameDeferredUnits()` (007AF5B4), after `WorldClient()`.
  world_client_view.cpp keeps its own constants (kTouchedModels... and a local DeferredUnits()):
  the new names were chosen not to clash with them.

## ui_surface_create.cpp (baseline At 62, VCall 2, raw 3, *At view struct 1)

All converted: At<> 0, VCall 0, raw 0, view struct 0 (ScaleAt is gone: CInterface3D::CallScaleRectToScreen,
the same 005F7CA0 address). Local forwarders dropped for shared ones: GameNew -> gamecrt::operator_new
(007005AF), ConstructHolder -> DXSurfaceConstructOriginal (004EF7B0). ComCall -> IDirectDrawSurface7
from <ddraw.h> (GetSurfaceDesc, Blt), the description a DDSURFACEDESC2 (dwHeight * lPitch as before).
The texture manager's slots 19 / 20 are CBaseManager::GetFile / CloseFile (base_manager.h; the
CloseFile result is ignored as before). The holder's release in the constructors is
CDXSurface::Release() (the same inlined body: count down, slot 0 with 1 at zero); the picture
constructor still calls 004E6BA0 through SurfaceHolderReleaseOriginal as before.
The exported functions keep their `char*` signatures (verify_dx_surface.cpp declares them so).

## ui_hud.cpp (baseline At 73, VCall 8, raw 32, byte arithmetic 2)

Converted: Field<> 73 -> 0 (the local template is gone), byte arithmetic 2 -> 0 (SetTint uses
CUiMesh::tint_, the move panel's flag CUiMesh::uOffset_), VCall 8 -> 6, raw addresses: every data
literal moved to a named accessor or header. What is left:
- 6 VCalls on map object slots 38 / 39 / 42 (placeholders in map_object.h; DR-C2 names them IsUsable,
  IsDirectUseBlocked, AcceptsKey but declares them void, while these calls read an int). Convert after
  DR-C2's merge with corrected return types.
- Raw-address count: the UI_INPUT_HANDLER / UI_WHEEL_HANDLER registrations (`UI_INPUT_HANDLER(0x0063C6D0, ...`)
  are counted by check_cleanup because the macro is not an EI_ macro; most of the baseline's 32. Either
  teach check_cleanup's REGISTRATION pattern the two macros or leave the count.
- Item slot 21 (the player's loot item's 12-byte use key) is still a vtable call (`ItemUseKey`, not a
  VCall): CItem declares only slots 13-15.
- 00742760 (PortraitDelayScale) and 0073B84C (GameOne) are file-local accessors.

Other changes:
- The session's player list walk uses net::GameClient()->players_ as stlp::list<net::PlayerMessage*>
  (id_ +0x2C the unit id, serial_ +0x30 the player id), as ui_hud_portrait_setup.cpp does.
- **Behaviour/verify note:** the portraits' server command now calls `net::GameClient()->SendCommand`
  (0041D260, rewritten in net_client_commands.cpp) directly instead of through its address; its verify
  wrapper (if any) no longer sees these two calls. Everything else keeps going through the same
  addresses as before (ViewSetWeapon / ViewSetMoveMode / ViewOrderFollow / ViewOrderMoveAlt / the screen's
  own commands / UnitQuickItem / ItemCategory); duplicates of shared forwarders were replaced by them:
  CameraSetTarget -> CameraSetTargetOriginal, OrderUnitsSetSize -> PartyArraySetSizeOriginal,
  GlueExitZone -> GameExitZoneOriginal, ClientView (005699F0) -> WorldClient()->LocalView() (rewritten,
  world_client.cpp), Cursor() -> GameCursor() (ui_cursor.h, the same 005E5200 forwarder).
- The console line of a ground click goes through console::GameOStream (operator<<, PutFloat, EndLine):
  the same calls (006FC31D, the +4 x_floatused store, 006FC7FA, 006FC0B0 '\n', 00434FE0 flush).
- OrderUnits is mfc::CArray<CUnit*> (brace-initialised with kUnitPtrArrayVtable; FreeData at the end).
- A unit's position for ScreenOrderAt / ScreenCastAt is copied from CUnit::positionX..Z with memcpy
  (three dwords, as Field<UiVec3> did).
- NodPortrait(screen, index) is the portrait nod the old code inlined three times (slots_[i].wobbleTime_
  the random delay, then wobbleKind_ 1; same order of reads and the random call).

Unverified: `ObjectSlots(u)->IsDead()` relies on CMapObjectVirtuals' declaration order (slot 55) as the
rest of the code does; the unit position fields are CUnit's own (unit.h), not CMapObject's.

## world_client_draw.cpp (baseline At 71, VCall 5, asm 6, raw 17, byte arithmetic 8)

Converted: Field<> 71 -> 0, byte arithmetic 8 -> 0, VCall 5 -> 4, the list-address constants and the
other data literals -> world.h FrameObjects / FrameDeferredUnits, texture.h RenderDeviceObject,
model.h ScenePipeline, originals_render.h SceneContext, camera::TheCamera() (0079B2A8; its
localRotation_ is the old 0079B2DC), and file-local named accessors (state block handles 007AEDD0 /
D8 / DC, 0073BAB0, 0073B84C, 0073F750, 0079BD50 / 4C / 54, 0079BE64, 007900E4). ObjectVector and
ListNode (per-file layouts) are gone: FrameObjectList (world.h) and stlp::list<T>::Node.
Matrix stacks: PushMatrix / PopMatrix take CTransformPipeline::stacks_[i] (MatrixStack), the top is
CTransformPipeline::Top(9). PushSceneMatrix and the flushes call the shared forwarders directly.
Left:
- the 6 `__asm` blocks (x87 helpers, out of this ticket's scope; rule 7 cleanup) and the raw addresses
  inside two of them (Ftol's 006E853C, AnimationTime's 007AF61C / 007AFCA8): 3 counted;
- 4 VCalls: CWorld slot 36 (the particle manager; DR-C2: ClientParticleManager, declared void),
  the drawn objects' slots 22 / 23 / 24 (DrawEach) and the attached object's slots 25 / 26.
Unverified: `CMapObject` is assumed for the selection list's objects (positionX..Z, orientation,
attached); `static_cast<CMapObject*>` of ManagerFindObject's result.

## Where I stopped

All three files drafted; notes complete. Nothing built. Next: merge DR-C2, DR-C1, add camera_, build,
fix, run the scenarios above, check_cleanup --update.

## Finished (build pass)

Main merged (DR-C1..C4 and the rest); the draft compiled as written against DR-C1's names. Changes on top:
- terrain.h conflict: main's Terrain layout, the draft's `unknown_100` pointer kept (+0x100).
- CWorldClient `camera_` (+0x210, `CCamera*`), `unknown_214`, static_assert; `class CCamera;` in world.h.
- world.h's `FrameObjectList` tripped check_cleanup's per-file container rule: FrameObjects() now
  returns `stlp::vector<CEngineObject*>&` (begin() / end(); `Finish()` where the old code stored
  the end pointer).
- ui_hud.cpp: slots 38 / 39 / 42 are `ObjectSlots(x)->IsUsable()` / `IsDirectUseBlocked()` /
  `AcceptsKey(&itemUseKey_)` (main's int signatures). CBattleScreen::itemUseKey_ is a `LockKey`
  (ui.h includes unit.h); the item slot 21 helper ItemUseKey returns `const LockKey*` and the copy is
  a struct assignment (12 bytes, as the memcpy was).
- world_client_draw.cpp: map object slots 22-24 declared `int DrawFigure / DrawCollectDirty /
  DrawFigureShadow(void* context)` (RET 4, from 00516460 / 00516420 / 00516490); DrawEach takes a
  lambda per slot. CWorld slot 36 declared `ClientManager19C* ClientParticleManager()` (005619B0
  returns +0x19C). The attached side half's slots 25 / 26 are CObject3DSpecific's `Unused25(0)`
  (0051C9D0, an empty RET 4) and `SetLit(0)`.
- Counts after (At, VCall, asm, pragma, raw, view struct, placeholder, byte arithmetic):
  ui_surface_create.cpp all 0 (was 62, 2, 0, 0, 3, 1, 0, 0); ui_hud.cpp all 0 (was 73, 8, ..., 2);
  world_client_draw.cpp 0, 0, 6, 0, 3, 0, 0, 0 (was 71, 5, 6, 0, 17, 0, 0, 8): the six x87 `__asm`
  helpers and the three raw addresses inside them remain. config/forwarder_duplicates.txt lost the two
  duplicates the draft dropped.
- verify and release build without warnings; check_thunks, check_returns, check_forwarders,
  check_cleanup, check_stolen, check_x87, check_signatures at 0. Not run in game (the suite is run
  centrally): the scenarios listed at the top still apply.
