# Cleanup phase 1: interface screens (cleanup plan, ticket C2ui batches 1 and 2)

How the camp, hero screen, item description, save / load, global map, objectives, menu layouts and interface frame
code reads its objects after the cleanup (`ui_camp.cpp/.h`, `ui_hero_screen*.cpp`, `ui_item_info.cpp`,
`ui_saveload.cpp`, `ui_global_map.cpp`, `ui_interface_frame.cpp`, `ui_objectives.cpp`, `ui_layouts.cpp`). The screens
themselves are in `ui.md`. Every rewritten address kept its hook and verify wrapper; calls to rewritten code still go
through the game address.

## Declarations

- `CCampPages : CItemScreen`; its parties, panels and hero list are typed (`mfc::CArray<CUnit*> heroes_`, `HeroAt`),
  the controller slots are called on `CUnit::Controller()`. `CMpHeroScreen` has the item screens' two
  `ItemObjectCache`s (`models_`, `textures_`), `CUiEditField*` edit windows and slot 52 `VirtualChooseFigure`.
- `CItemInfoPanel`'s text arrays are `mfc::CStringArray`; the descriptions read `itemdb` records (item heads,
  materials, weapons, armour, spells) and `UnitStats` slots.
- Sprites are `CUiMesh` (`texture_`, `plainColour_`, `flags_`, `position_`, `orientation_`, `tint_`, `uOffset_`,
  `vOffset_`, `visible_`); window vtables are `CUiWindow::vtable_` / `inputVtable_`; texture memory is
  `CTexture::surface_` (`CDXSurface`) locked through `IDirectDrawSurface7`.
- `CInterface3D` names `pollTop_` (+0x81) and `disconnected_` (+0x88); the frame calls the windows through
  `CUiWindow`'s slot helpers (27 `VirtualMessageResult`, 34 `VirtualShowBriefing`, 35 `VirtualReadyToStopPolling`).
- The map database is read as `MapRecord` / `MapExit` (the objectives picture is `picture2`), quests as `QuestRecord`,
  the global map's exit distances as `stlp::map<int, float>`, the renderer's fog colour as `CSceneRenderer::fogColour_`.
- Duplicate forwarders went to the shared ones (`NamedTableConstructOriginal`, `GameSetWeatherOriginal`,
  `InterfaceTextExtentOriginal`, `TradePanelSettleOriginal`, `CFileFindPathOriginal`).

## Counts (batch 1 files, before / after)

Duplicate forwarders 9 / 0, At / Field 40 / 0, VCall 4 / 0, byte offset arithmetic 35 / 0, *At views 33 / 0, per-file
containers 4 / 2.

## Batch 2 (the rest of `src/game/ui*`)

Every `ui*.cpp` / `ui*.h` with a count in `config/cleanup_baseline.csv` (the battle screen and HUD, the window stack,
the help / message windows, the console, the hero preview, surfaces and text, person texts and briefings, the
objectives window, the global map's constructor and destructor).

- `CInterface3D` names its backdrops (`backdrop_`, `secondBackdrop_`, `backdropMode_`), `serverFlag_`, the modifier
  flags (`ctrlDown_`, `shiftDown_`, `altDown_`, `doubleClick_`, `rightDown_`), `messages_`, `helpTopics_`
  (`stlp::map<CString, int>`), the saved HUD state (`hudPanelsIn_[3]`, `hudBodyPart_`, `hudTopPage_`), `fastGame_`.
- The Direct3D device is `UiD3DDevice` with its stdcall slots (10 `Clear`, 20 / 21 `Set` / `GetRenderState`, 36 / 37
  `Get` / `SetTextureStageState`); `UiRenderer` names `device_`, `caps_`, `stencil_`, `result_`. `CSceneRenderer`'s
  slots 7 `SetColour` and 19 `DrawRectangle` (model.h) draw the selection box.
- Surfaces are `CUiSurface` (`flags_`, `holder_`, `kept_`, `keptSize_`, `width_`, `height_`) over `CDXSurface`.
- The HUD panels built inline by the battle screen are `CHudUnitPanel` / `CHudMap` / `CHudClock` / `CHudMovePanel` /
  `CHudTopPanel`; the panels' text arrays (`CHeroStatsPanel`, `CSpellMakerPage`, `CTradePanel`, `CHudUnitPanel`,
  `CCampPages::pageTitles_`) are `mfc::CStringArray`.
- Containers are the shared templates: `PersonTextMap = stlp::map<int, mfc::CString>` (`LoadPersonTexts` a free
  function), `IdVector = stlp::vector<int>`, `MpStringVector` / `MpVoiceVector`, `UnitSoundBank::entries`;
  `CObjectivesWindow` has `zones_` (`stlp::vector<CString>`), `exits_` (`mfc::CArray<ObjectiveExit>`), `quests_` /
  `objectiveTexts_` (`mfc::CStringArray`), `questPlaces_` (`stlp::vector<UiPointF>`); `CGlobalMap::Construct` and
  `GlobalMapDestroy` go through `zones_` / `links_` / `quests_`, `path_` and `arrivals_` (`Count`, `Compare`,
  `ResetToEmpty`).
- New forwarder: `ConsoleDrawInputOriginal` (005E4A10, originals_ui.h).

Counts (`src/game/ui*`, before / after): duplicate forwarders 0 / 0, At / Field 69 / 0, VCall 15 / 0, byte offset
arithmetic 48 / 0, *At views 7 / 0, per-file containers 6 / 0.

Fix (own commit d4ad9cc4): the blueprint description's first number read the item record's +0x38 through `FloatOf`,
i.e. a float's bits as an integer; the original loads it as a float and truncates (FLD [rec+0x38]; _ftol, 005F06A3).

## Left

Raw access the counter does not see (a byte pointer kept in a local, `Get` / `Set` / `SetDword` helpers):

- `ui_hero_preview.cpp`: the model's slots 22 / 25 and its light at +0x94 through `VirtualCall` / `Get` (the model
  class's vtable is not known up to those slots).
- `ui_create_screen.cpp`: screens 8 / 9 and the escape menu built inline with `SetDword` (their classes are not
  declared); `ui_item_screen.cpp`: screen 9's destructor (CString vector at +0x1AC) and the display's +0x14 / +0x8.
- `ui_window.cpp` Construct, `ui_item_view.cpp`, `ui_hero_screen*.cpp`, `ui_hero_figure.cpp`, `ui_camp.cpp`,
  `ui_camp_deals.cpp`, `ui_battle_setup.cpp` (player +0x30), `ui_trade_panel.cpp` (stats +0x700): byte locals.
- Item fields are still read with `Read<>(item, offset)` in ui_camp.cpp, ui_hero_panels.cpp and ui_item_info.cpp until
  the item classes are declared.

Declarations:

- `CBattleScreen` (ui.h) and `CBattleScreenSetup` (ui_camp.h) declare the same object twice; `CCampScreen`
  (ui_screens.h) repeats `CCampPages`; `CHudQuickItems::entries_` / `labels_` are raw arrays.
- Each file keeps its own `SetRenderState` helper; `WindowFromInput` / `FromInput` are repeated per file.
- `AllodView` repeats `AllodRecord`; `MapScreen` forwards CGlobalMap's own members through their addresses;
  `CCreditsScreen::archive_` is bytes (a CMappedResource).
- `MapRecord::kind`: object_tables.h says 1 edge / 2 brief, the global map treats 2 as a way between zones; check.
- `SpellRecord::unknown_1C` (power, radius, level, duration) and `ItemRecordHead::unknown_4C` can be named in item_db.h.
- Behaviour note from batch 1: the hero list's rename writes the hero party's name (record +0x30 points at it), as the
  original does; no scenario renames a hero.

## Unknown fields (ticket F3)

Every `unknown_XXX` member of the interface (ui.h, ui_camp.h, ui_screens.h, ui_briefing.h, the other ui_*.h and the
ui_*.cpp views, 132 in all) has a name now; layout unchanged. Alignment padding and blocks nothing in the rebuild
touches are `unused_XXX` (the main menu's +0x168 / +0x174, the view screen's +0x164, the trade and statistics panels'
+0x148, the briefing's +0x258 and the interface's +0x10 were also checked in the original's slots: nothing reads
them). The duplicate views keep their blocks as `unused_` with a pointer to the view that names them (CBattleScreen's
to CBattleScreenSetup, CUiMesh's to CFigureMesh, whose layout it repeats up to +0x220). Named:

- `CBattleScreenSetup`: `viewPressed_` (+0x1C0), `boxSelecting_` (+0x1C1), `questSound_` (+0x1FC), `partyCamera_`
  (+0x210, the camera id between the two party id vectors, as `PartyIds::camera`); `spare_` (+0x164) is only
  cleared by Create (00635250+9C4), no slot of the screen reads it.
- `BattleTargetQuery` is laid out as a `LockKey` (unit.h): `kind` the mask (1), then `id` and `level` (0).
- `CHudQuickItems::shownCount_` (+0x14C, the sprites added), `CHudMap::northTime_` (+0x1B0),
  `UiMeshVertex::normal` (+2, MeshCorner's second index; the builders write 0).
- Written, never read in the original (named `*Spare`): `CInterface3D::fpsSpare_` (+0x24, Create with the frame rate
  fields; ShowFps / DrawOverlays read only +0x20 / +0x28 / +0x2C), `CUiMesh::tintSpare_` (+0x230, CreateSprite;
  Draw 005E6D90 reads tint_ only), `CCursor::imageSpare_` (+0x14, Init and Shutdown 005E62E0; the frame step
  005E52E0 reads image_'s +0x14, not the cursor's), `CMpConnectScreen::spare_` (+0x254, Create only),
  `BriefingSpeaker::spareFlag` (+0x08, false when added; the vector's erase / insert copies it, nothing tests it).
- `CUiWindow::heldObject_` (+0xF4): 0 from the constructor, released by the destructor with the two materials; no
  writer identified among the window classes.
- `TradeRecord::padAfterPerks` / `padAfterBought`: alignment bytes the trade panel's verify copies before comparing
  whole records.

How the readers were looked for: a capstone index of every `[reg+disp]` operand per function of the vanilla exe
(config/functions.csv sizes), queried per class over its vtable slots and their direct callees in the class's
address range. Readers through another object's pointer outside that range are not excluded by it.
