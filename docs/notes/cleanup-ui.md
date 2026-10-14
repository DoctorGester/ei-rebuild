# Cleanup phase 1: interface screens (cleanup plan, ticket C2ui batch 1)

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

## Counts (these files, before / after)

Duplicate forwarders 9 / 0, At / Field 40 / 0, VCall 4 / 0, byte offset arithmetic 35 / 0, *At views 33 / 0, per-file
containers 4 / 2.

## Left

- `ui_camp.h`: `PersonTextMap` (an `stlp::map<int, mfc::CString>` with only its header declared) and
  `CBattleScreenSetup::IdVector` (`stlp::vector<int>`); replacing them reaches the battle, briefing, person text and
  create-screen files.
- `CObjectivesWindow` keeps its split fields; `Zones()`, `Exits()`, `Quests()`, `ObjectiveTexts()` and `QuestPlaces()`
  give the containers. Retyping them reaches `ui_objectives_input.cpp`, `ui_screen_constructors.cpp`,
  `ui_window_delete.cpp` and their verify files.
- `CGlobalMap::Construct` and `GlobalMapDestroy` still write by offset (`stlp::map` keeps its count and compare
  private); `AllodView` repeats `AllodRecord` (object_tables.h names only `name`); `MapScreen` forwards CGlobalMap's
  own members through their addresses.
- `MapRecord::kind`: object_tables.h says 1 edge / 2 brief, the global map treats 2 as a way between zones; check.
- `CCampScreen` (ui_screens.h) repeats `CCampPages`; `CCreditsScreen::archive_` is bytes (a CMappedResource).
- Item fields are still read with `Read<>(item, offset)` in ui_camp.cpp and ui_item_info.cpp until the item classes
  are declared; `SpellRecord::unknown_1C` (power, radius, level, duration) and `ItemRecordHead::unknown_4C` /
  `value38` can be named in item_db.h.
- Behaviour fix on the way: the hero list's rename writes the hero party's name (record +0x30 points at it), as the
  original does; no scenario renames a hero.
