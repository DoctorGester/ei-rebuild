# DR-H16: options / keyboard screens, widgets, client message handlers (draft, not built)

Branch draft-dr-h16. Nothing here has been compiled, checked by the tools or run. The next worker
builds (`build.cmd verify`, `build.cmd release`), runs check_stolen / check_x87 /
check_signatures / check_forwarders / check_cleanup / check_thunks, fixes, and runs the scenarios
below.

## Functions

| Address | Function | Status | File |
|---|---|---|---|
| 00651350 | COptionsWindow::RefreshRows | drafted, wrapper drafted (ours only) | src/game/ui_options.cpp |
| 00651D70 | COptionsWindow::ShowGroup (RET 4) | drafted, wrapper drafted (ours only) | ui_options.cpp |
| 00651940 | COptionsWindow::BuildSettings | drafted, wrapper drafted (ours only) | ui_options.cpp |
| 00651600 | COptionsWindow::BuildBindings | drafted, wrapper drafted (ours only) | ui_options.cpp |
| 00652200 | COptionsWindow::LoadSettings | drafted, wrapper drafted (side by side, 5 stolen bytes) | ui_options.cpp |
| 006524A0 | COptionsWindow::Apply | drafted, wrapper drafted (ours only) | ui_options.cpp |
| 0065B8B0 | CUiSlider::SetRect (slider vtable 00742CBC slot 26, RET 4) | drafted, wrapper drafted (ours only) | ui_options.cpp |
| 0065D150 | CUiScrollText::DrawLines (RET 4) | drafted, wrapper drafted (ours only) | src/game/ui_scroll_text.cpp |
| 0065C610 | CUiEditField::OnKeyDown (input side slot 9, RET 0xC) | drafted, wrapper drafted (side by side, 6 stolen bytes) | src/game/ui_edit_field.cpp |

Wrappers: tests/verify/verify_ui_options.cpp.

Scenarios that should reach them: ui_hud, ui_main_menu, ui_container_input open the options window
(Build 006503C0 runs BuildSettings -> CUiSlider::SetRect per slider, LoadSettings, BuildBindings,
ShowGroup(0) -> RefreshRows; ui_hud also clicks the second group). Apply: only the OK button (no
scenario; add a step clicking OK at 213..287 x 526..574 after changing nothing). DrawLines: the
battle screen's message log (ui_hud). The edit field: a message box with flag 4 or the multiplayer
hero screen's name fields; no scenario types into one yet.

## Shared header changes (ui.h, mfc.h)

- `UiSpinValue` / `UiSpin` replaced by `CUiSpin : CUiWindow` (0x174 bytes; texts_ +0x148,
  value_ +0x15C, changed_ +0x160, layout_ +0x164). The options rows' second pointer is an
  `OptionSetting*` (the old `UiSpin` was the setting read with the wrong type: its bar_ +8 and
  value_ +0xC are OptionSetting's bar_ / spin_). `OptionRow` is now {OptionBinding* binding_,
  OptionSetting* setting_}; callers in ui_menus.cpp, ui_menu_keys.cpp and the two verify files
  were updated (spin_ -> setting_, `->value_` -> `->spin_`).
- `OptionSetting::definition_` is `const OptionDefinition*` (table 00787430, 26 x 0x18: id, name,
  kind, maximum, group, row); `OptionBinding` is {const ActionDefinition* action_ (table 00787080,
  59 x 0x10: id, name, group, row), stlp::vector<const UiKeyName*> keys_, CString name_, CString
  keysText_}. ui_menus.cpp's Update reads `definition_->id_`; ui_menu_keys.cpp compares
  `action_->id_`.
- New: `CUiSlider : CUiScrollbar` (0065BC30 constructor forwarder, SetRect); CUiScrollbar's
  +0x154 is `RECT track_`, +0x17C `float depth_` (named from the slider only), sizeof asserted
  0x18C. CUiScrollText: `textRect_` +0x18C, `lines_` (mfc::CArray<UiTextLine*>) +0x1B0, `align_`
  +0x1C4; `UiTextLine` {height_, firstToken_} (size unknown, only used through pointers).
  CUiEditField: caret_ +0x174, editing_ +0x178, focused_ +0x179, changed_ +0x17A, `Text()`,
  `DrawContents()` (0065C2E0, Rebirth-patched, forwarder in ui_edit_field.cpp).
- `KeyActionTable()` / `kKeyActionCount`, `UiKeyNames()` / `kUiKeyNameCount`,
  `OptionDefinitions()` / `ActionDefinitions()` accessors.
- COptionsWindow: RefreshRows / ShowGroup / Apply / BuildSettings / LoadSettings / BuildBindings
  are the rewrites; `Call*` forms go through the original addresses (the forwarders moved from
  ui_menus.cpp / ui_layouts.cpp to ui_options.cpp) and every caller (ui_menus.cpp,
  ui_menu_keys.cpp, ui_layouts.cpp's Build, ShowGroup itself) uses them so the wrappers run.
- mfc::CString::Mid (006FD9B4) added to mfc.h / mfc.cpp. 006FD9B4 is already forwarded from
  console.cpp and ui_console_window.cpp (listed in config/forwarder_duplicates.txt); if
  check_forwarders objects to a third, switch those two to mfc::CString::Mid.

## Not checked (fix at build time)

- Everything compiles at all: aggregate init `OptionBinding{}` / `OptionSetting{}` (stlp::vector
  with a deleted copy constructor inside OptionBinding: only placement / value init is used, never
  a copy; the vector template's push_back is used for OptionSetting and `const UiKeyName*` only).
- 00652930 (bindings' _M_insert_overflow) declared RET 0xC from its call site (three pushes, no
  caller cleanup); argument order (pos, value, count) from the pushes.
- The slider's region order (0, 1, 2) in 0065B8B0: the Ghidra listing was cut off after the first
  sprite, so the regions' ECX loads were not read; taken from the decompiler's order of the three
  SetRect calls and the scroll bar's region meaning (0 up/left, 1 down/right, 2 track). Check the
  raw bytes 0065BA..0065BC20.
- Strings 007948C4 / 007948B4 (the spin's fallback texts) and 00794830 / 0079482C (the keys'
  separator and trim set) are not defined in Ghidra; their contents were not read. Only their
  addresses are used, so the behaviour does not depend on it.
- Constants read from memory (0073B84C "one", 0074023C, 00742B80, 0073BAA0, 0073C3B0, 0073B86C,
  0073B850, the angle 007C8210): values inferred from use only.
- `IsMultiplayerClient()` (world.h, BOOL) is called directly (it is rewritten, world_rules.cpp);
  0056D7A0 is forwarded locally as ClientWorldRefreshObjectsOriginal with ClientWorldObject()
  (originals_world.h).
- check_cleanup: ui_options.cpp / ui_edit_field.cpp are new (start at zero): they use
  `reinterpret_cast<CUiMesh*>` of scene objects (as CDifficultyBox does) and reinterpret_cast of
  global addresses through named constexpr addresses; if the tool counts those, move the option
  globals into a header with accessors.

## Behaviour notes / differences kept on purpose

- BuildSettings / BuildBindings: the original push_back copies a stack temporary whose
  non-string fields were never written (all are overwritten afterwards for kinds 0 and 1; a
  definition of another kind would keep stack garbage in bar_ / spin_ in the original, null in
  ours; the table has none of another kind as far as read).
- DrawLines: a format token's font above 2 indexes the three measured heights out of range (the
  original reads its other stack slots); ours reads past the array likewise (undefined). Never seen
  with the game's texts (fonts 0-2).
- CUiEditField::OnKeyDown does not clamp the caret on Left / Right (the original does not either;
  0065C2E0 presumably does).
- ShowGroup draws the rows twice when a row is filled (once on selecting it, once at the end), as
  the original.

## Where I stopped

All nine functions drafted with wrappers. Next: the client message handlers below.

## Client message handlers missing from config/functions.csv

(see the section added below by the same ticket)
