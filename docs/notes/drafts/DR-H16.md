# DR-H16: options / keyboard screens, widgets, client message handlers

Branch draft-dr-h16. Builds: `build.cmd verify` and `build.cmd release` with no errors or warnings,
check_thunks / check_returns / check_forwarders / check_cleanup (run by build.cmd), check_stolen,
check_x87 and check_signatures at 0 problems (main merged in first). Not run in the game yet: the
scenarios below should reach every function.

## Functions

| Address | Function | Status | File |
|---|---|---|---|
| 00651350 | COptionsWindow::RefreshRows | built, wrapper ours only | src/game/ui_options.cpp |
| 00651D70 | COptionsWindow::ShowGroup (RET 4) | built, wrapper ours only | ui_options.cpp |
| 00651940 | COptionsWindow::BuildSettings | built, wrapper ours only | ui_options.cpp |
| 00651600 | COptionsWindow::BuildBindings | built, wrapper ours only | ui_options.cpp |
| 00652200 | COptionsWindow::LoadSettings | built, wrapper side by side (5 stolen bytes) | ui_options.cpp |
| 006524A0 | COptionsWindow::Apply | built, wrapper ours only | ui_options.cpp |
| 0065B8B0 | CUiSlider::SetRect (slider vtable 00742CBC slot 26, RET 4) | built, wrapper ours only | ui_options.cpp |
| 0065D150 | CUiScrollText::DrawLines (RET 4) | built, wrapper ours only | src/game/ui_scroll_text.cpp |
| 0065C610 | CUiEditField::OnKeyDown (input side slot 9, RET 0xC) | built, wrapper side by side (6 stolen bytes) | src/game/ui_edit_field.cpp |

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

## Checked at build time (against the exe's bytes, capstone)

- All nine functions read instruction by instruction against the rewrites: field offsets, call
  arguments and their order, RET counts (ShowGroup / SetRect / DrawLines RET 4, OnKeyDown RET 0xC,
  the rest plain RET), loop bounds (26 settings x 0x14, 59 actions x 0x10, 88 key names x 0x48,
  14 rows, 11 groups). The edit field's jump table (0065C8EC / 0065C908): 08 Backspace, 23 End,
  24 Home, 25 Left, 27 Right, 2E Delete, everything else nothing.
- 00652930 (bindings' _M_insert_overflow): thiscall, pushes 1, &temp, finish, no caller cleanup:
  (pos, value, count), RET 0xC as declared.
- The slider's regions (0065BBB7..0065BC03): region 0 (left, bottom - 10, left + 12, bottom),
  1 (right - 12, bottom - 10, right, bottom), 2 (left, top, right, bottom - 10), as drafted. Its
  four sprites' SetRectangle / Place arguments match too.
- CUiScrollbar +0x17C: the constructor 0065AEA0 stores its first float there (and the second at
  +0x14C, the int at +0x148), so the slider's depth is BuildSettings' 3.0; CUiSlider::Construct's
  parameters renamed (depth, maximum, speed).
- Strings: 007948C4 "OFF", 007948B4 "ON", 00794830 "  |  " (after each key name), 0079482C " |"
  (TrimRight set), 00787BCC "W" (DrawLines' font sample).
- Float constants (all single precision, used by FMUL / FADD / FSUB / FCOMP dword): 0073B84C 1.0,
  0074023C 50.0, 00742B80 0.02, 0073BAA0 4.0, 0073C3B0 0.25, 0073B86C 0.5, 0073B850 0.1. 007C8210
  (the sprites' quarter turn) is in .bss, set at start-up. LoadSettings' products of two floats are
  exact in double; Apply's int * 0.02 (- 1) products are exact in double for the settings' ranges,
  so the one float rounding matches the x87 one.
- 0057F440 is IsMultiplayerClient (Apply tests AL), 00578E20 returns [007AF788] (GameTexts()).
- check_cleanup: the option globals, strings and constants are named accessors in ui_options.cpp
  (and the font sample in ui_scroll_text.cpp); both files at their baseline.
- check_forwarders did not object to the third 006FD9B4 forwarder (mfc::CString::Mid; the address
  was already in config/forwarder_duplicates.txt), so console.cpp / ui_console_window.cpp were left.

## function_sizes.inc

Ghidra answered, but `tools/gen_function_sizes.py` would have shrunk 98 existing entries (none grew;
e.g. 0040D710 555 -> 60, 004B6760 13477 -> 8792): the open Ghidra database currently reports
truncated function bodies (its listing of 0040D710 stops after an FMUL). That output was not used;
only the 22 handler rows from config/extra_function_sizes.csv were added to the committed file.
Regenerate it once the Ghidra database's function bodies are whole again.

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

## Where it stands

All nine functions built with wrappers; the handler research below is in config/functions.csv,
config/extra_function_sizes.csv and src/hook/function_sizes.inc. Not done: any rewrite of the
handlers, and an in-game run of the scenarios. LoadSettings' side-by-side wrapper sets every
control twice (the original's values, then ours: the same values again).

## Client message handlers missing from config/functions.csv

CPlayerClient's handler table (+0x24, 0x100 entries, filled by 00667230; player_client.cpp lists it)
points at 23 handlers Ghidra has no function for (code only reached through the table). Their
extents were read with capstone from the exe (linear sweep to the last RET past every forward
branch); 00669D50 (already in extra_function_sizes.csv as 340) came out the same. In
config/functions.csv each is a row with the gap to the next function (that file's convention; the
eleven rows they split were shortened to their own gap); config/extra_function_sizes.csv has the
code sizes (for the installer's body compare once function_sizes.inc is regenerated). 00669010
(0x02) was already a function.

Every handler is thiscall on the CPlayerClient (+0x1C the player, +0x34 the received stream read
through its slot 1 Read(buffer, n)), no stack arguments, returns 1 (0 on a failed lookup or read
where noted).

| Type | Address | Size | What it does |
|---|---|---|---|
| 0x00 | 00667DA0 | 52 | login accepted: a dword read, state +0x20 = 2, 0066C1E0(player, value) on the player's +0x14 |
| 0x01 | 00667DE0 | 14 | the player's 00665A10 |
| 0x04 | 00667C20 | 228 | camera placement: a point (12 bytes) and two dwords; client world 005662B0(point); unless the player's +0xD8 string equals 007900F4 the camera (0079B2A8) 00411470 with the point, 23.0 and an angle (00743148 * 0073BA30 * 0073DF70), else 004108D0(6 if the second dword is 3, else 7); then 004109E0(first dword). Returns the last Read's result |
| 0x07 | 00669D50 | 340 | game state (known: CreateScreen 005F5BA0, see ui.md) |
| 0x09 | 00669F30 | 16 | the interface's (007C7D50) 005F7FA0 |
| 0x0B | 00667DF0 | 48 | two dwords to the interface's 005F82A0 (a numbered message with a value) |
| 0x0C | 006696A0 | 103 | (id, float) pairs until id -1: object manager 007AF60C's object (00403F40) gets the float at +0x28; 0 when the client's +0x39 flag is clear |
| 0x0F | 00667D10 | 97 | two points and a byte: the camera's 00411060(a, b, 0) and 00410E50(byte) |
| 0x12 | 00667E20 | 52 | a byte and a dword: the player's world (00455940) 00566C10(dword) |
| 0x14 | 0066A420 | 94 | (id, float): the party's (+0x70) slot 18 object table, its object (00403F40) gets the float at +0x28 and goes to the player's 00665BE0 |
| 0x16 | 00669400 | 133 | two dwords (unit id, index): the party's member 006621B0 takes record index of its +0x730 list (00660A50); the client world unit's brain (+0x240) slot 25 with -1 |
| 0x19 | 0066A620 | 225 | byte kind and a unit id: 0 the client world controls the unit (00566030), 1 / 2 set / clear unit flag 0x100 (not for flag 0x10) and its +0x94 object's slot 26 with 1 / 0, 3 clears the selection (00566100(0)); other kinds return 0. Jump table 0066A704 after the code |
| 0x41 | 006687B0 | 160 | map object changed: the id in the player's +0x28 hash map; its delta read (0050EAB0(1, stream), 0 on failure); kinds 0x54 / 0x5D also go to the world's 005651A0 |
| 0x42 | 00668850 | 237 | map object removed: a unit of the player's +0x40 map gets 005300D0(0); the +0x28 entry erased (0059BCB0 / 0059BC70 / 00598E40 / 0059B750), the object flagged 1 and released (slot 4), the client world's 00564BE0(its id); 0 when unknown |
| 0x43 | 0066A3B0 | 74 | an id: the object manager's (007AF5F8) object of kind 0x50 gets its +0x240 slot 48 |
| 0x50 | 00668940 | 238 | timed object added: (kind, id); the world's factory (slot 14) makes it (slot 23), it reads itself (00675BA0(0, stream)), goes into the player's +0x58 hash map (highest id kept) with one more reference |
| 0x51 | 00668A30 | 105 | timed object changed: the +0x58 entry reads its delta (00675BA0(1, stream)); 0 when unknown or the read fails |
| 0x52 | 00668AA0 | 157 | timed object removed: the +0x58 entry erased, the object flagged 1 and released (slot 4); 0 when unknown |
| 0x60 | 00668C50 | 951 | party state: the previous party ids (+0x50D8) lose unit flag 0x400000 (and their 007C8370 entries); a byte to the world's 00566350; a dword; the new id list (+0x50D8, grown like an aligned array) whose units get 0x400000 and, in single player (+0x100 == 1) without +0x24C and with 007AFCA4 >= 1, their brain's slot 43; 007C8370 0066BA70; a dword to the world's +0x6E4; a second id list into +0x50E8 |
| 0x70 | 00669760 | 70 | a byte and a 16-bit value: 007AFCAC's 004E3FC0(byte, (float)value) |
| 0x71 | 006697B0 | 68 | two dwords: client world 0055FB50(second), 0055FB30(first), 00566360() |
| 0x72 | 00669800 | 66 | a point and a dword: client world 00564190(point, dword) |
| 0x73 | 0066A400 | 20 | the client world's 005683B0(stream) reads a block |

Not looked at: CPlayerServer's handler table (+0x28 / +0x2C, objects.md) may have the same kind of
table-only handlers; its entries were not checked against functions.csv.
