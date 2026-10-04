# User interface (005F0000-00612000)

Files: `src/game/ui.h`, `src/game/ui_interface.cpp`, `src/game/ui_window.cpp`,
`tests/verify/verify_ui.cpp`. No shared header was changed (`ui.h` includes `core_transform.h`,
`engine_object.h` and `item_db.h` as they are).

## What the area is

Most of the range is the interface screens (inventory, trade, spell book, map, dialogs, briefing,
multiplayer lobby) built on one window base class, plus a large amount of STLport/MFC template
code (red-black tree inserts, node-pool allocation and deallocation, CString list nodes, CArchive
serialisation of CString-pair arrays). Drawing goes through DirectDraw surfaces and GDI DCs
(`CDC::FillSolidRect`, `GetTextExtentPoint32`). The rewrite covers the layout, hit-testing and
container logic underneath all screens; drawing code was left alone.

### Classes (names provisional, set in Ghidra)

- **CInterface3D**, the interface manager (`CInterface3D::Create` 005F4990). `+0x00/+0x04` screen
  width/height, `+0x34..+0x3C` STLport vector used as the stack of active windows (the top one
  receives input; activation code at 005F7350/005F71C0), `+0x48/+0x4C` a pending value and its
  flag, `+0x68/+0x6C` mouse position in screen pixels, `+0x7C..+0x81` key-modifier flags.
  Interface layouts are authored for 800x600; the four scale helpers convert between that and the
  real screen (`x * width / 800`, `y * height / 600` and back) with 32-bit wrapping products and
  truncating division.
- **CUiWindow**, the base of every interface window (ctor 005FE700, dtor 005FE930, vtables
  00740F60 and 00740F2C at `+0x94`). It starts with a CTransformObject (0x54 bytes) kept opaque.
  `+0x98` the CInterface3D, `+0xD8` window RECT in layout coordinates, `+0xE8` enabled flag, then
  five aligned pointer arrays (see below): `+0xF8` referenced engine objects, `+0x108` scene
  objects (both released through CEngineObject slot 4), `+0x118` CUiSurface, `+0x128` CUiRegion,
  `+0x138` CUiModel. Slot 19 (006002C0 in the base, 00601AC0 in the 00741008 subclass) shows or
  hides surfaces/children when the enabled flag changes.
- **UiPtrArray<T>**: `{T** data; int size, capacity, growBy}` with storage aligned to 32 bytes
  (operator new of `n*4 + 0x24`, the raw pointer stored at `data[-1]`; 00600A80 allocates it).
  Growth by `growBy`, or by `size / 8` clamped to [4, 1024]. The original inlines Append/Remove at
  every call site and has five identical out-of-line copies of SetSize (one per window array).
- **CUiRegion** (0x34 bytes, created by `CUiWindow::AddRegion`): owner interface, an aligned
  vertex array (8-byte points, allocated by 004F65B0), bounds RECT at `+0x14`, `+0x24` rectangle
  only, `+0x25` enabled, `+0x28/+0x2C` offset subtracted before testing, `+0x30` tooltip. A region
  is a rectangle (SetRect) or a convex polygon (AddPoint; enabled from 3 vertices) tested edge by
  edge with integer cross products.
- **CUiTooltip** (0x184 bytes, 0065E320; outside this range, not rewritten): `+0x180` state
  0 idle, 1 hovered (timer), 2 appearing, 3 disappearing, 4 shown. `0065E580` advances it.
- **CUiSurface** (0x38 bytes, vtable 00740F28, ctors 005FCC00/005FCDD0, dtor body 005FD120): an
  off-screen DirectDraw surface; `+0x04` interface, `+0x24` screen RECT.
- **CUiModel** (4 bytes): pointer to a scene object shown in a window (inventory item, portrait).
  005FC1F0 creates it, 005FC1D0 releases it.
- **UiRecordTable**: the spell book's view of the spell (stride 0x84) and spell-modifier (0x24)
  databases, the same bounds-checked lookup as `RecordTable::At` in `item_db.h`.

## Rewritten

| Address | Name | Verify |
|---|---|---|
| 005F7CA0 | `CInterface3D::ScaleRectToScreen` | pure, original on a scratch rect (result and EAX) |
| 005F7D20 | `CInterface3D::ScalePointToScreen` | same |
| 005F7DA0 | `CInterface3D::ScaleRectFromScreen` | same |
| 005F7E00 | `CInterface3D::ScalePointFromScreen` | same |
| 005F7E30 | `CInterface3D::ScalePointFromScreenCopy` (identical copy) | same |
| 005F7590 | `CInterface3D::GetActiveWindow` | pure |
| 005F74E0 | `CInterface3D::SetPendingValue` | original on a 0x50-byte copy |
| 005F2EA0 | `UiRecordTable::SpellAt` | pure |
| 005F2EE0 | `UiRecordTable::ModifierAt` | pure |
| 005FD3A0 | `CUiSurface::SetRect` | original on a copy of the surface |
| 005FE400 | `CUiSurface::MoveTo` | original on a copy of the surface |
| 005FC450 | `CUiModel::SetScreenPosition` | original on a copy of the object's first 0x28 bytes |
| 005FC8F0 | `CUiRegion::HitTest` | pure |
| 005FC660 | `CUiRegion::HasActiveTip` | pure |
| 005FC9E0 | `CUiRegion::SetRect` | ours only; fields checked |
| 005FCA40 | `CUiRegion::ClearShape` | ours only; fields checked |
| 005FCA70 | `CUiRegion::AddPoint` | ours only; bounds, vertices, capacity rule, enable rule |
| 005FC880 | `CUiRegion::OnMouseMove` | ours only; tooltip state transition (see below) |
| 005FFCC0 | `CUiWindow::RegionAt` | pure |
| 005FFD30 | `CUiWindow::ContainsPoint` | pure |
| 005FEBF0 | `CUiWindow::UpdateRegionTips` | ours only; every tooltip's transition |
| 006002A0 | `CUiWindow::SetEnabled` | ours only; flag |
| 005FEDD0 | `CUiWindow::AddObject` | ours only; array append and reference count |
| 005FF110 | `CUiWindow::RemoveObject` | ours only; array contents |
| 005FF180 | `CUiWindow::RemoveObjectAt` | ours only; array contents |
| 005FF700 | `CUiWindow::RemoveSceneObject` | ours only; result and array |
| 005FF770 | `CUiWindow::RemoveSceneObjectAt` | ours only; array |
| 005FF7D0 | `CUiWindow::ClearSceneObjects` | ours only; empty array |
| 005FFA90 | `CUiWindow::AddSurface` | ours only; array append |
| 005FFBF0 | `CUiWindow::RemoveSurfaceAt` | ours only; array |
| 005FFC50 | `CUiWindow::ClearSurfaces` | ours only; empty array |
| 005FF830 | `CUiWindow::AddRegion` | ours only; append and the new region's fields |
| 005FF9C0 | `CUiWindow::RemoveRegionAt` | ours only; array |
| 005FFA20 | `CUiWindow::ClearRegions` | ours only; empty array |
| 005FFD80 | `CUiWindow::AddModel` | ours only; append, empty model |
| 005FFF40 | `CUiWindow::ClearModels` | ours only; empty array |
| 006003D0, 006004E0, 00600730, 00600840, 00600950 | `UiPtrArray<T>::SetSize` (five identical copies) | ours only; size, capacity rule, kept prefix, alignment |

40 addresses (36 distinct functions plus the four extra SetSize copies).

### Calling conventions and return values

- The scale helpers are `__thiscall` with one stack pointer argument (`RET 4`). The two "to
  screen" ones leave `0x1B4E81B5 * product` (the division-by-600 magic multiply) in EAX; the
  "from screen" ones leave the last result. Both are returned so EAX is identical.
- `HasActiveTip` returns `XOR AL,AL` when there is no tooltip; EAX was already 0 there, so it is a
  full BOOL.
- `RemoveSceneObject` returns 1/0 in EAX; `AddRegion`/`AddModel` return the new object (AddModel 0
  when allocation fails, and then nothing is appended).
- `UpdateRegionTips` pops one unused stack argument (`RET 4`).

### Floating point

`CUiModel::SetScreenPosition` computes `(x * 0.0025f - 1) * 0.48157f * depth` (and 0.75 for y),
each stored once as float. Under `/fp:fast` MSVC folded the two constants into one product, so the
function is compiled with `#pragma float_control(precise, on)`; the generated code was checked to
keep the original order (multiply, subtract, multiply, multiply, FSTP float).

## Forwarders added

- 005FC600 `CUiRegion::ReleaseContents` (releases the tooltip via its slot 0 and the vertex array).
- 005FD120 `CUiSurface::ReleaseContents` (surface destructor body; DirectDraw).
- 005FC1D0 `CUiModel::ReleaseObject`.
- 0065E580 `CUiTooltip::OnMouseMove` (tooltip state machine, outside this range).

The standalone build must also provide `gameapi::PtInRect` and `gamecrt::operator_new/delete`
(already in `src/lib`). Not renamed in Ghidra because they are outside this range: 0065E580,
004F65B0 (the 8-byte aligned allocator).

## Original bugs kept on purpose

- `CUiWindow::AddRegion` leaves the new region's bounds uninitialised.
- The array append code has a dead branch for `size == -1` that frees the storage and then stores
  through the null array; kept as is.
- Removing by index does not check the index. With an index at or past the end the original's
  `REP MOVSD` count goes negative and it faults; ours skips the move (only differs where the
  original crashes).

## Verify wrappers

- Pure functions run the original through the trampoline and ours, compared bit for bit.
- Output-only functions run the original into a scratch copy (the rect/point, the surface, the
  first 0x50 bytes of CInterface3D, a copy of the model's object) and ours on the real data.
- Array mutators run ours only. The wrapper snapshots the array (pointers, size, capacity, growth,
  whether storage existed) and checks the result against the original's rules: append keeps the
  prefix, adds the item last and sets the capacity the original computes; removal shifts the tail
  down by one without touching the capacity; clears leave a null array with size and capacity 0;
  SetSize keeps the old storage when it fits and otherwise grows to `max(newSize, capacity + step)`.
- Region/tooltip: the tooltip handler (0065E580) runs its own state machine (0 and 4 stay, 1 may go
  to 0 or 2, 2 to 4, 3 to 0). The wrapper computes the state our transition hands over from our
  HitTest and checks the final state is reachable from it.

## Offline testing (not committed)

A scratch harness mapped game.exe's sections at their VAs inside a 32-bit test process (the range
reserved in a suspended child before its loader runs), pointed the PtInRect import at user32 and
redirected MFC operator new/delete (007005AF/007005D8) to a recording allocator. Every rewritten
function, including the stateful ones, then ran side by side with the original on random inputs
(about 2.6 million comparisons: results, output objects, array contents, allocation sizes,
release calls, x87 precision control 24/53/64 bits for the float function). No mismatches; a
deliberately broken build was caught. A second harness ran the verify wrappers themselves against
the original code (trampolines pointing at the original addresses) on random states: no false
mismatches.

## Skipped

- STLport and MFC template instantiations (tree/list nodes, node-pool `_M_allocate`, CString pair
  arrays and their `Serialize`, `ConstructElements`): library code.
- Drawing: CDC/DirectDraw helpers (005FDC00/005FDC60 DC acquire/release, 005FDE20/005FDFA0 text
  output, 005FE2F0 fill, 005FD240 pixel read, 005FD7A0 surface save/restore, 005FCC00/005FCDD0
  surface constructors) and the screen-specific `Draw`/`Update` methods.
- 005F8340 text extent (GDI), 005F78C0/005F7B40 keyboard dispatch, 005F7350 window activation,
  005F8070 script command parsing: large, many side effects into the game; good later targets once
  the window classes are better known.
- 005FC730 / 005FC6B0 tooltip creation ("tip_%d" text lookup, allocate 0x184 bytes): depend on
  the tooltip class.

## Rebirth patches the scaling functions

Rebirth's addon patches all five CInterface3D scaling functions (005F7CA0, 005F7D20, 005F7DA0,
005F7E00, 005F7E30; widescreen support), so under Rebirth those hooks are skipped. Rewritten callers
(RegionAt, ContainsPoint, UpdateRegionTips, CUiSurface::SetRect/MoveTo) therefore call them through
the original addresses (`CInterface3D::CallScale*`); calling our versions directly made RegionAt
disagree with the patched original in the Rebirth smoke run.

# Screens (00610000-00630000)

Files: `src/game/ui_screens.h`, `src/game/ui_screens.cpp`, `tests/verify/verify_ui_screens.cpp`,
scenario `tests/scenarios/ui_screens.txt`. `ui.h` was not changed (the screens are new classes that
hold a `CUiWindow` as their first member).

## What the range holds

Mostly screen classes built on `CUiWindow`, each with a vtable pair (the input sub-object's vtable
at +0x94, then the main vtable 0x34 bytes after it). Main-vtable slots every screen uses: 19
show/hide children, 20 per-frame draw, 24 update, 25 Create (regions and models, often from a named
layout resource), 26 on activate, 27 message-box result, 42 and up screen-specific. Input slots: 2
left down, 3 left up, 4 mouse move, 5/6 right down/up, 9 key down `(vk, repeat, scan|flags)`, 10
char, 12 wheel `(keys, delta, x, y)`. The screen base class (the 00741008 subclass of `CUiWindow`)
passes input on to child windows (+0x148 array) and controls (+0xC4 list) after clearing the
handled flag (+0x9C) (00601110, 006011F0, 006012D0, 00601820).

| Range | What | Vtables / notes |
|---|---|---|
| 00610020-00612CF0 | Camp screen methods (party skill, equip, buy/sell, level up, item and spell constructor descriptions: `camp_*`, `spell container`, `Prototype`, `Instruction_*`) | camp 007408E8/0074091C (0x260 bytes, Create 0060C870, Rebirth-patched) |
| 00613400-00613DF0 | STLport/MFC template code (vector destructors, node-pool frees, string hash_map lookups, CArray<DWORD> Serialize) and 16-byte destructor thunks | library |
| 00613EA0-006195B0 | Multiplayer hero screen (`lmp_*`, `MainMenu00`, `button03_multiplayer`): hero list, new hero (portrait, name, voice), statistics | 00742118/0074214C (0x220 bytes); Create 00613EA0 and the mode switch 00615D30 are Rebirth-patched |
| 00619630-0061B220 | STLport tree/hash containers (address book), catch blocks | library |
| 0061B2C0-0061E2F0 | Multiplayer connection screen (`config\addrbook.ini`, `lmp_connection %d`, server full, wrong password/protocol) | 00741C9C/00741CD0; 0061B2C0 and 0061D780 are Rebirth-patched |
| 0061E2F0-00620300 | Another lobby window (00741DA4/00741DD8) | not mapped further |
| 00620380-00621F30 | Camp item strip (tabs, arrows, wheel, filtered list) | 007419E8/00741A1C; forwarding subclass 00741948/0074197C |
| 00622000-00626C50 | Hero panels shared by the camp and the hero screen: equipment (`camp1-4`), statistics (`Strength` ... `lmp_points_left`, `magic_skills`, `infounit_*`), perks and skills | 00741504 group |
| 006270A0-00629AF0 | Camp pages: trade (`trade1-4`), repair, spell constructor (`constrspell1-4`), item constructor (`constritem1-4`), `InventoryM01` | 00741504 group |
| 0062A830-0062B300 | In-game escape menu (`escMenu00`, `quit_msg`) | 00741270/007412A4 |
| 0062B7D0-0062C090 | Credits (`Titles`, scrolling text) | 00740C08/00740C3C |
| 0062C250-0062D330 | Exchange between players (`campinfo`, `xch_*`) | Create 0062C250 |
| 0062D350-0062FFC0 | Global map (`global_map`, `gtime`, `save_day`, `buttons\globalmap\*.wav`) | 00740B08/00740B3C (0x294 bytes, screen factory 005F5BA0 id 4); 0062F950 Rebirth-patched |

### How the screens open

- The interface manager's screen factory 005F5BA0 is driven by the server (client message handler at
  00669D50): id 1 main menu, 3 in-game HUD, 4 global map, 5 camp (005F6240), and 2, 7, 8, 9. The
  global map and the camp are reached in single player only through a zone exit (the world script
  `LeaveToZone` puts the player in state 3 and messages the server); no key opens them.
- Main menu (left-click handler 00632D70; its regions come from the `MainMenu` layout resource):
  region 0 new game (difficulty box), 1 load, 2 exit, 3 options, 4 credits, 5 multiplayer.
- Multiplayer hero screen, mode 9 (hero list): regions 0 back (also Esc), 1 continue to the
  connection screen (needs a hero, else `lmp_no_pers`), 2/3 rotate the preview while held, 5 new hero
  (mode 10), 6 rename, 7 delete (`lmp_deletepers`), 8 the hero's inventory (the camp screen,
  vtable 0074091C), 9-18 list rows; keys up/down/PgUp/PgDn move the selection. Mode 10: the name edit
  is active (clicks are ignored while it is; Esc ends editing), voice rows are regions 9-19, up/down
  move the voice. Creating a hero writes `<n>.mp` files, so the scenario does not.
- In a game, Esc opens the escape menu (it is created by the key-down handlers 006034A0, 00630580
  and 0063A560). Regions (from the `EscMenu` layout, 0062AB20): 0 save, 1 load, 2 resume, 3 quit
  (message box: Enter = 0x10001 leaves the game, Esc = 0x10002), 4 options. Esc in the menu resumes.
- Keyboard: the interface reads keys from window messages, not DirectInput. WM_KEYDOWN goes to
  005F78C0 (console toggle, modifier flags, screenshot on scan 0x46), then to the top window's input
  slot 9. In-game hotkeys are looked up by scan code in the table at 0079BF70, loaded from
  `config\keyboard.ini` (00462C10). A zone has no inventory or map-screen key: `I` is quick item 3
  and `M` the minimap (`w_minimap`), which is why the earlier probe saw nothing open.
  DirectInputCreateEx is imported, but the interface does not use it.
# Interface screens (00630000-00660000)

Files: `src/game/ui_hud.cpp` (battle HUD panels), `src/game/ui_menus.cpp` (save/load and options
windows, scroll bar, tooltip slots), declarations appended to `src/game/ui.h`;
`tests/verify/verify_ui_hud.cpp`, `tests/verify/verify_ui_menus.cpp`; scenario
`tests/scenarios/ui_hud.txt`.

## What the range holds

- 00630000-00632000: global map / zone screens (`map.txt`, `quest`, `zone`, `allod`).
- 00632000-00635250: the main menu (00632940 New Game / Exit / intro movie, 006326B0 its regions from
  `menus.reg`, 00632D70 difficulty / titles), the new-game dialog (vtables 0074225C / 00742228,
  6337B0) and the message box (constructor 00634650, vtables 00742348 / 00742314; `quit_msg`, the
  exit movie).
- 00635250-0064BE80: the battle screen. 00635250 sets it up (`battle00`) and creates the HUD
  panels below; 0063A560 is its key handler (Rebirth-patched): action table 0079BF70 maps keys to
  actions (1 pause, 2/3 game speed, 4 objectives, 0x17-0x1A movement modes, ...), Esc cancels a
  targeting mode (+0x17C) or opens the Esc menu (vtables 007412A4 / 00741270, code at 0062AB20 /
  0062AE90 outside this range). 006369D0 / 00638280 quest and zone messages, 006407E0 / 00640060
  portraits (`infa%s%s%dface`, `face%s%02d`), 00647040 unit info texts (`consider_more%d`),
  0064A4E0 `combat_picked_up`.
- 0064BE80-0064F000: objectives window (constructor 0063B5D0, vtables 007411BC / 00741188;
  `obj_%d`, `buttons\objectives\*.wav`).
- 0064F000-00653000: options window (`escMenu00`, `option_group_`, key bindings).
- 00653000-00658000: save/load window (`saveload`, `saveq`/`savea`/`saveb`, pictures `shot.sav`).
- 00658000-0065A500: tutorial / help window (`tutor`, `tutorial`; created by 005F86F0 for a
  window's help topic, vtables 00740E28 / 00740DF4).
- 0065A500-00660000: controls: scroll bar (0065AEA0), spin and edit controls (0065BC30, 0065BCD0,
  0065C010), scrolling text (0065CC00; the battle screen's message log), tooltip (0065E320),
  DirectInput mouse (0065EAD0), RichEdit (0065ED30), party bar (0065F2D0).

Much of the drawing (`CDC`, DirectDraw surfaces, text) and the large screen builders were left alone.

### Input sides

Every window has a second vtable at +0x94 (the input side: `{vtable, CInterface3D*, bool handled}`).
Its handlers get `this` = window + 0x94 and (buttons or key, x, y) in screen pixels: slot 2 button
down, 3 button up, 4 mouse move, 5/6 right button, 7 double click, 9 key down, 11 key up, 12 wheel
(buttons, delta, x, y; returns BOOL). A button-down handler that takes the mouse stores its input
side in `CInterface3D +0x0C` (`mouseCapture_`); the button-up handler acts only when the capture is
its own and clears it. The handlers set `+0x9C` (`inputHandled_`) when they used the event, which
stops the container (00601110 etc.) passing it on. The rewrites are members of the panel classes
and are registered through shims that subtract 0x94.

### Battle HUD panels (800x600 layout of the original; Rebirth moves some)

| Panel (set up by) | Rectangle | Regions |
|---|---|---|
| weapon bar `CHudWeaponBar` (0063BAB0) | 80..240 x 520..600 | 4 slots, 40 wide |
| quick items `CHudQuickItems` (0063C800) | 520..720 x 500..600 | 8 slots 50x50 |
| spell column `CHudSpellColumn` (0063E2A0) | 760..800 x 190..510 | built per unit |
| action column `CHudActionColumn` (0063F300) | 0..40 x 250..510 | 6 buttons 40x40 from y 470 up |
| portraits `CHudPortraits` (0063FE70) | 250..550 x 440..600 | one per party member, y 510..600, centred on x 400 |
| map `CHudMap` (00641F20) | 615..800 x 0..165 | 0 sling (780..800, 0..30), 1 round map, 2 map area, 3/4 view buttons (780..800, 100..130 / 130..160), 5, 6 whole |
| unit panel `CHudUnitPanel` (006456E0) | 0..185 x 0..225 | 0 sling (0..20, 0..30), 1-4 body parts (0..20, 100..220 in 30 steps), 5 whole |
| clock `CHudClock` (00648520) | 720..800 x 520..600 | polygons (pause / speed) |
| movement panel `CHudMovePanel` (00649190) | 0..80 x 520..600 | 0 inner quarter circle (radius 34), 1-4 ring sectors 35..80 (centres about (56,589), (48,568), (32,552), (11,544)) |
| top panel `CHudTopPanel` (00649AF0) | 180..620 x 0..100 | 0 sling (385..415, 0..20), 1/2 page buttons (185..215 / 215..245, 0..20), 3 whole |

Esc menu rectangles come from `menus.reg` in `Res\menus.res` (loaded by 005ACE80 into 007C8048):
Save 295..495 x 79..160 (region 0), Load 295..495 x 160..215 (1), Resume 275..510 x 312..355 (2),
Exit to main menu 240..550 x 380..444 (3, asks `quit_msg`), Options 295..495 x 220..300 (4). Save/load
window: OK 163..237, Cancel 563..637, Delete 363..437 (all y 526..574), list rows 108..370 x 108.. in
24 steps. Options window: OK 213..287, Cancel 513..587 (y 526..574), groups 100..250 x 156.. in 24
steps, rows at x 250..520.

## Rewritten

| Address | Name | Verify |
|---|---|---|
| 00620960 | `CUiItemStrip::SetTab` | original, restore, ours: strip fields 0x150-0x17F and the six tab buttons' shades |
| 00620F10 | `CUiItemStrip::OnMouseMove` (input slot 4) | original, restore, ours: scroll fields and the interface's capture |
| 00621010 | `CUiItemStrip::OnLButtonUp` (input slot 3) | same |
| 00620E10 | `CUiItemStrip::OnLButtonDown` (input slot 2) | ours: handled flag, arrow capture and direction, Shift jumps, tab |
| 006210F0 | `CUiItemStrip::OnWheel` (input slot 12) | ours: pending scroll grows by delta/120 over the strip; returns 1 |
| 006212A0 | `CUiItemStrip::OnLButtonDownForward` | ours: handled flag |
| 00621420 | `CUiItemStrip::SetFilter` | ours: positions, pending, velocity, filter |
| 00621510 / 00621530 | `CUiItemStrip::SelectTab` / `CycleTab` | ours: resulting tab |
| 006218F0 / 006219A0 / 00621A80 | `CUiItemStrip::ItemTab` / `SpellTab` / `ArmorTab` | pure |
| 006212F0 | `CUiItemStrip::RebuildList` (slot 26) | ours: the list is the source items whose tab (by the original classifiers) is the current one |
| 00621F30 | `CUiItemStrip::ItemAt` (slot 25) | side by side (both rebuild the list): item and cursor |
| 00610020 | `CCampScreen::BestPartySkill` (slot 45) | pure, original into scratch outputs |
| 006110F0 | `CCampScreen::ShowPageTabs` (slot 44) | ours: the three strips' tabs |
| 00618FF0 | `CMpHeroScreen::ClampHeroSelection` | ours: index in the list, row visible |
| 00619090 | `CMpHeroScreen::ClampVoiceSelection` | ours: index, row visible, the hero's voice file |
| 006193C0 | `CMpHeroScreen::EquipmentWeight` (slot 53) | pure (double in ST0) |
| 006188E0 | `CMpHeroScreen::CanEquip` (slot 43) | pure (AL) |
| 00618980 | `CMpHeroScreen::OnItemChosen` (slot 42) | ours: equipped slot |
| 00616900 | `CMpHeroScreen::GetHero` (slot 51) | pure |
| 006177D0 | `CMpHeroScreen::OnLButtonUp` | ours: rotation stopped, capture released |
| 0062AD10 | `CEscMenu::OnMouseMove` | ours: hovered region |
| 0062B300 | `CEscMenu::OnKeyDown` | ours: Esc sets handled and pending value 0 |
| 0062ACC0 | `CEscMenu::OnMessageResult` (slot 27) | ours: pending value / leave-game flag |
| 0062BFE0 / 0062C010 | `CCreditsScreen::OnLButtonDown` / `OnKeyDown` | ours: closed (pending 0xCCCCCCCC or the leave-game flag) |
| 0062D090 | `CExchangeScreen::OnKeyDown` | ours: handled for Backspace/Enter/Esc |

29 functions. Input handlers are registered as `__fastcall` shims that take the input sub-object
(window + 0x94) in ECX and call the window method.

### Details

- Item strip fields (+0x148): action mode (1-3; cursor 0x16/0x18/0x17), owner screen (+0x14C; item
  clicks go to its slot 42 with the action mode, and the item predicates are called on it), tab,
  scroll direction, timer, per-tab positions in slots (6 floats at +0x15C), pending scroll (+0x174),
  velocity, strip y (500 bottom, 0 top), the visible list (CPtrArray at +0x198, `SetSize` 0062A390)
  and the source list (+0x1AC). The strip is 100 px per slot from x 50: `ItemAt` truncates
  `(x - 50) * 0.01f + position` with the game's `_ftol` (the sum is not rounded to float first).
- `SetTab` settles the old tab at `_ftol(pending + position)` (unrounded sum) and shades the tab
  buttons (scene objects 2-7, colour at +0x224) 1.0 or 0.5.
- Releasing an arrow (`SettleScroll`, inlined twice by the original) keeps the fraction of the
  pending scroll and adds or removes a whole slot when the fraction is under half. The fraction is
  exact in float, so comparing the unrounded x87 value gives the same result.
- Item tabs. Filter 0 (items): parts and kind-1 items 0, spells and modifiers 1, weapons, 0x3005,
  armour and spell containers 2, quick items 3, 0x3009 4, others 5. Filter 1 (spells): the spell
  record's +0x10 (0-2, else 5); modifiers 3, or 4 when the record's +0xC > 5. Filter 2 (armour): parts
  by sub-kind (0 -> 0; 1 -> 1 if the armour record's +0xC > 2 unsigned, else 2; 2 -> 3), armour 3,
  kind 1 4, others 5.
- Hero skills (`HeroSchoolSkill`, `HeroManaUnits`): 00527EB0's result is stored as float, then
  FISTP (round to nearest); the mana maximum (00523F50) is stored as float, divided by the period
  (00523CD0, unrounded) and truncated. Under `/fp:fast` the compiler dropped the float store, so it
  goes through `StoreFloat`.
- The sound system (005F7E60) gets the game's own string pointers, not copies.

## Forwarders added (standalone build must replace)

Screen base input handlers 00601110/006011F0/006012D0/00601820 (`UiInput`), interface sound 005F7E60,
leave game 00581660, chat keys 005F7C60/005F7C80, cancel exchange 0041E3E0, region tooltip 005FC730,
scroll bar position 0065B090 (and the bar's virtual slot 25), equipment panels
006229E0/00622A30/00622BC0, item predicates 005EE050/005EE080/005EE0D0/005EE110, CPtrArray `SetSize`
0062A390, model animation 005FC3B0/005FC3D0, hero screen 00616910 (show hero) and 00616560 (draw
voices), unit statistics 00527EB0/00523F50/00523CD0 and `CSpell::GetPrototype` 0068BA20 (called
through their addresses), the game's `_ftol` 006E853C. Window virtual slots 25, 26, 42, 47 and 51 are
called through the vtable, as the original does.

## Original bugs kept on purpose

- `SpellTab` / `ArmorTab` read the record through null when the item's record index is out of range.
- `ClampVoiceSelection` with an empty voice list reads the entry before the vector.

## Skipped

- Layout builders (Create, slot 25) and mode switches: 00613EA0, 00615D30, 0061B2C0, 0060C870 and
  0062F950 are Rebirth-patched (widescreen); the others (00620380, 0062AB20, 0062C250 ...) call
  005E70E0 (Rebirth-patched sprite placement) and the drawing helpers.
- Drawing and text output (00616560, 006167C0, 00622DB0, 00623880, 006242E0, 00624CB0, 00625880 ...),
  the global map (0062D350, 6.7 KB) and the click handlers that create windows (00617050, 0062AE90,
  0062CF70): large, with many side effects. Good later targets now that the screen classes are named.
- 00620A50 (the strip's per-frame scroll animation): float-heavy (SQRT, branch-free selects); left for
  a run that can open the camp.
- Template code (00613400-00613DF0, 00619420-0061B220, 0062A390 ...).

## Scenario

`tests/scenarios/ui_screens.txt` (about 40 s): main menu region 5 (multiplayer hero screen), the list
keys and the rotate buttons, Esc back to the menu; then a new game and the escape menu (hover, resume,
Esc, the quit box cancelled with Esc). It uses the harness's new `clickregion`/`moveregion` steps,
which click the centre of a region of the top window wherever the layout put it. The camp, the
global map and the item strips are not reached (the scenario takes no zone exit), so their wrappers
only run when a game reaches the camp.
| 0063C6D0 / 0063C7D0 | `CHudWeaponBar::OnMouseDown` / `OnMouseMove` | side by side (handled flag, capture) |
| 0063C710 | `CHudWeaponBar::OnMouseUp` | ours only: capture released, handled flag, selected slot |
| 0063C600 | `CHudWeaponBar::Update` (every frame) | ours only: selection follows the unit's +0x270 |
| 0063C4F0 | `CHudWeaponBar::Draw` | ours only: returns 1, alpha states restored, specular off |
| 0063DAA0 / 0063DAE0 / 0063E270 | `CHudQuickItems::OnMouseDown` / `OnDoubleClick` / `OnMouseMove` | side by side |
| 0063D9D0 | `CHudQuickItems::Update` (every frame; tooltip texts) | ours only: panel state unchanged |
| 0063CDC0, 0063F190, 0063FD50 | `Draw` of quick items, spell column, action column (identical) | ours only: returns 1, specular off |
| 0063F1E0 / 0063F220 / 0063F2D0 | `CHudSpellColumn::OnMouseDown` / `OnDoubleClick` / `OnMouseMove` | side by side |
| 0063F260 | `CHudSpellColumn::OnMouseUp` | ours only: capture, handled flag |
| 0063FDA0 / 0063FE40 | `CHudActionColumn::OnMouseDown` / `OnMouseMove` | side by side |
| 0063FDE0 | `CHudActionColumn::OnMouseUp` | ours only: capture, handled flag |
| 00641CE0 / 00641E20 / 00641EA0 | `CHudPortraits::OnMouseDown` / `OnMouseUp` / `OnMouseMove` | ours only: capture, handled flag, portrait mode reset |
| 00645060 | `CHudMap::OnMouseDown` | ours only: capture, handled, view and button tints |
| 00645610 | `CHudMap::OnMouseMove` | side by side |
| 00648330 / 006484C0 / 006484F0 | `CHudUnitPanel::OnMouseDown` / `OnMouseMove` / `OnWheel` | side by side (wheel: also the result) |
| 00648370 | `CHudUnitPanel::OnMouseUp` | ours only: capture, sling (flag, direction sign), body part and tints |
| 00648D30 / 00649160 | `CHudClock::OnMouseDown` / `OnMouseMove` | side by side |
| 00649630 / 00649750 | `CHudMovePanel::OnMouseDown` / `OnMouseMove` | side by side |
| 00649670 | `CHudMovePanel::OnMouseUp` | ours only: capture, handled flag |
| 00649550 | `CHudMovePanel::Update` (every frame) | ours only: common flag recomputed, indicator value |
| 0064A1C0 / 0064A2C0 / 0064A2F0 | `CHudTopPanel::OnMouseDown` / `OnMouseMove` / `OnWheel` | side by side |
| 0064A200 | `CHudTopPanel::OnMouseUp` | ours only: capture, sling, page |
| 00654660 / 006547B0 | `CSaveLoadWindow::OnMouseDown` / `OnDoubleClick` | ours only: selection, pending close value |
| 006544A0 | `CSaveLoadWindow::Draw` (slide-in) | ours only: returns 1, surface frozen exactly when resting |
| 006545E0 | `CSaveLoadWindow::Update` | ours only: slide grows, ends past 1 |
| 00654430 / 00654450 | `CSaveLoadWindow::CloseDialog` / `OnDialogResult` | ours only: dialog cleared / pending value |
| 00650C20 / 00650D70 | `COptionsWindow::OnMouseDown` / `OnDoubleClick` | ours only: pending close, selected row, key wait flag |
| 00650920 / 00650A20 | `COptionsWindow::Draw` / `Redraw` | ours only (as the save/load draw) |
| 006508E0 | `COptionsWindow::OnDialogResult` | ours only (call counted) |
| 00650900 | `COptionsWindow::HelpTopic` | pure, side by side |
| 0065B090 | `CUiScrollbar::TakePosition` | side by side (result, position, flag) |
| 0065B360 | `CUiScrollbar::Update` (every frame, also the battle screen's message log) | ours only: leftover within (-1, 1) |
| 0065B240 / 0065B340 | `CUiScrollbar::OnMouseDown` / `OnMouseUp` | side by side (capture, handled, state, grab, accumulator) |
| 0065EA50 / 0065EA80 | `CUiTooltip::Draw` / `Redraw` (every frame per tooltip) | ours only: result, state |

56 functions. "Side by side": the original runs first through the trampoline, its handled flag and
capture (and any other field it may change) are recorded and put back, then ours runs and the two
are compared. These originals change nothing else (RegionAt, the scaling and PtInRect are pure).

### Calling conventions and values

- Input handlers: `RET 0xC` (wheel `RET 0x10`, returns 1 in EAX on every path, including when the
  point is outside the panel). The shims are `__fastcall (input, edx, ...)`.
- The capture value the originals store is `window ? input : 0` (`NEG/SBB/AND` on window = input -
  0x94); the window is never null there, so ours stores the input side.
- Draw slots return 1 (`RET 4`); the update slots take no arguments.
- `CSaveLoadWindow::OnDialogResult` (slot 27): the caller (005F590D) ignores EAX.

### Floating point

- The slide-in drawing eases the window's model from y 700 to its place (550/620 save/load,
  480 options): `e = 2s^2` below s = 0.5, else `1 - 2(1-s)^2`, then `(a - 700) * e + 700` stored once
  as the float argument. Done in doubles; MSVC rewrites `s*s + s*s` as `(s+s)*s` and
  `-80*e + 700` as `700 - 80*e`, which round identically (scaling by 2 and negation are exact).
- `CSaveLoadWindow::Update` adds `FrameTime() / 0.5` to the slide (compiled as a doubling, exact)
  and compares the unrounded sum with 1 like the original.
- `CUiScrollbar::Update`: `FrameTime() * speed * 16` (same order), negated for "up", added to the
  float accumulator; whole steps come off with exact +-1 operations, so keeping them in a register
  (ours) or reloading the stored float (original) gives the same values. A NaN accumulator takes
  the negative branch and loops forever in both.
- `FrameTime` (00600270) leaves `ticks * 0.001f` unrounded in ST0, so its forwarder returns double.

## Forwarders added (standalone build must replace)

`CInterface3D::PlaySound` 005F7E60, `ShowHelp` 005F86F0; `CUiWindow::DrawBase` 005FEB90,
`RedrawBase` 005FECC0, `FrameTime` 00600270; `CUiContainer::UpdateBase` 006010E0, `DrawContainer`
00600EF0, `RedrawContainer` 00601040; container input defaults 00601110 / 00601570;
`CUiSurface::Freeze` 005FD760; `CUiModel::Draw` 005FC2F0; `CUiRegion::SetTipText` 005FC690;
`UiRenderer::FlushBatchA/B` 004E7840 / 004E7A40; `CBattleScreen::SelectUnit` 00638EF0
(Rebirth-patched), `HoverUnit` 00639EF0; `CHudWeaponBar::Highlight` 0063C050 (Rebirth-patched);
`CHudQuickItems::ItemTip` 0063CEE0; `CHudSpellColumn::Click` / `ClickAlternate` 0063EC90 / 0063EF60;
`CHudActionColumn::Select` 0063FB50; `CHudMovePanel::ToggleFlag` 00649810, `ShowMode` 00649910
(Rebirth-patched); `CHudTopPanel::ShowPage` 0064A320; local view orders 0066AE70 (weapon) /
0066AED0 (movement mode); cursor shape 005E5200 (global 007C7BD8); server command 0041D260
(session at 0079B5D8); save/load: 00655290, 00655380, 00655550, 00655B20, 006567A0
(Rebirth-patched), 00656E10; options: 00651350, 00651D70, 00652000, 006524A0; the cancel's sound
setters 006B7740 / 006B7A00 / 006B78A0 (global 007AF784) and display settings 00500960 (007AF060,
slot 22); spin value 0065BEB0 / 0065BEE0. Globals read by address: 0079BCC4..0079BD04 (settings
the options window saved), 007C7BD8, 0079B5D8, 00794848 (the string "options", returned as is).

## Original bugs kept on purpose

- Quick items: the slot lookup uses RegionAt's result unchecked, so a point inside the panel but
  outside every slot (-1) reads the dword before the table (+0x168, zero in practice) and is taken
  as a hit (`RegionItem`).
- Weapon bar button-up: the order goes to `LocalView()` without checking for a missing local view.
- Options: rows are indexed from the region number without a bound (regions past the table would
  read the following fields).

## Skipped

- Rebirth-patched functions in the range (00630010, 00636020, 00638EF0, 0063A560, 0063BAB0,
  0063BCC0, 0063C050, 0063C800, 0063CAE0, 0063E2A0, 0063E4B0, 0063F300, 0063F500, 00641F20,
  00642930, 00642B40, 00645210, 006456E0, 00645F80, 006462A0, 00647FB0, 00648520, 00648D70,
  00649190, 00649910, 006567A0, 00659420, 0065C2E0, 0065E580): only hookable in the vanilla
  install; their callers here go through the original addresses.
- The main menu (00632940 etc.): its New Game writes the game state request at 007AFE20 that the
  harness relies on.
- Float-heavy per-frame updates: clock (006488E0: fsin/fcos/atan2), top panel slide (00649FF0),
  tutorial pulse (0065A5E0), wheel zoom (00645640, `_CIpow` compared unrounded), options update
  (00650A30). They need inline-x87 care.
- Screen builders, key handlers (0063A560, 006548A0, 00650E40, 0064E5A0, 006592C0) and drawing.

## Scenario `tests/scenarios/ui_hud.txt`

New game, then mouse moves over every panel; clicks: weapon slots 0/1/0 (100,560), (140,560); top
panel pages (200,10), (230,10), (200,10) and its sling twice (400,10); unit panel body parts
(10,115)..(10,205) and its sling twice (10,15); map views (790,145), (790,115); movement panel inner
button twice (10,590) and mode 1 (48,568); the hero's portrait (400,560). Then Esc opens the menu:
Options (395,260) -> group 2 (175,192) -> Cancel (550,550); Load (395,187) -> first row (240,120) ->
Cancel (600,550); Save (395,120) -> Esc; Resume (392,333); finally the hero must take damage (the
world runs again). Spell, action and quick-item buttons get only mouse moves: a click there starts
a targeting mode, and the Esc that follows would cancel it instead of opening the menu. Nothing
saves, loads, quits or starts a movie.

# Large interface functions (second pass over 005E0000-00660000)

Files: `src/game/ui_hud_map.cpp` (minimap), `src/game/ui_hud_frame.cpp` (clock and top panel),
`src/game/ui_hud_portraits.cpp` (portraits), `src/game/ui_layouts.cpp` (Esc menu, save/load and
options windows), `src/game/ui_objectives.cpp` (objectives window), `COptionsWindow::Update` in
`ui_menus.cpp`; wrappers in `tests/verify/verify_ui_hud_map.cpp`, `verify_ui_hud_frame.cpp`,
`verify_ui_hud_portraits.cpp`, `verify_ui_objectives.cpp`, `verify_ui_menus.cpp`,
`verify_ui_screens.cpp`. Declarations were added to `ui.h` and `ui_screens.h`.

## Rewritten

| Address | Size | Name | Verify |
|---|---|---|---|
| 00643DC0 | 4142 | `CHudMap::BuildMarkers` (every frame) | side by side: mesh arrays and renderer fields, byte for byte |
| 0064BE80 | 3459 | `CObjectivesWindow::Build` (slot 25) | ours: 3 objective texts, the scroll bar, and the zone page's structure (below) |
| 0064CC10 | 3412 | `CObjectivesWindow::ShowZone` | ours: 9 + n regions, 21 + n sprites, n exits = the zone's exits that lead somewhere; the shown zone's name |
| 00642E80 | 3897 | `CHudMap::UpdateView` (every frame) | side by side: centre/facing (put back between the runs), needle rotation, clipped polygon, area mesh, bounds quad |
| 00641080 | 3162 | `CHudPortraits::Update` (every frame) | side by side, slots and the game RNG put back between the runs: slots, RNG, face rotations, bars, flash, marker |
| 00653DD0 | 1629 | `CSaveLoadWindow::Build` (slot 25) | ours: counts of what is added, selection and offered name |
| 006503C0 | 1291 | `COptionsWindow::Build` (slot 25) | ours: counts, group titles, flags |
| 006488E0 | 1095 | `CHudClock::Update` (every frame) | side by side (phases put back): phases, interface field, sprite rotations/positions/tints, hand flag |
| 0062AE90 | 694 | `CEscMenu::OnLButtonDown` | ours: the window opened on top (vtable, save flag), resume's pending value |
| 00649FF0 | 453 | `CHudTopPanel::Update` (every frame) | side by side (slide state put back): slide, offset, sling sprite, interface fields |
| 00650A30 | 446 | `COptionsWindow::Update` | ours: slide rule; settings take the moved bars' positions |
| 0062AB20 | 361 | `CEscMenu::Build` (slot 25) | ours: one model and five regions added |
| 00645640 | 159 | `CHudMap::OnWheel` (input slot 12) | side by side: zoom, handled flag, result |

13 functions, about 24.2 KB.

### Objectives window (CObjectivesWindow, 0x1F0 bytes)

- TAB (keyboard.ini `obj`) creates it for the current zone (0063A560 -> constructor 0063B5D0, then
  0064BE40 sets the zone name +0x170 and +0x174/+0x178/+0x17C). Esc or Enter (with one button)
  close it.
- Build: alpha reference render state 1 (the old value kept at +0x1EC), the texts obj_0..obj_2,
  the quest log (0065CC00) and the list's scroll bar as children, textures "saveload" and
  "cross", three surfaces, scene objects 0-20 (buttons, highlight, picture placeholder 3, shades,
  frames, the zone arrows 14/15, five row markers) and regions 0-8 (buttons, arrows, rows). Then
  the zone page for the zone, or with +0x17C the zones of the zone's allod in which the player
  holds quests (variables "q.<zone>.<quest>" = 1; in multiplayer any allod), starting at the zone
  of the interface's current quest (+0xAC); several zones show the arrows.
- ShowZone drops the previous page's objects (beyond 2 textures, 21 sprites, 9 regions), puts the
  zone picture in a 300x300 square around (580, 190), adds a triangle marker (005E99D0, Rebirth-
  patched) and a region per exit that leads somewhere, turned and pushed 10 pixels towards the
  side it lies on, with the distances from +0x178 (a map<int, float> the global map passes) or
  the zone variable "z.<target>" otherwise; then the quests: the variables "q.<zone>.<quest>" not
  0, sorted, listed, and the one named by the global CString 007C8178 (or the interface's +0xAC)
  selected; then the zone's title and description ("zone <name>") on surface 1.
- The player's variables are copied into a local hash_map<CString, float> (00609C10 constructor,
  00578B70 copy by prefix) and walked in STLport order (buckets, chains; the next bucket from the
  key's hash 00460BA0). The rewrite frees it the way the original's inlined destructor does
  (keys released, nodes to the 16-byte free list bucket by bucket, then the bucket storage).
- Exits come from the map database record (+0x4C, 0x3C bytes, rectangle at +4..+0x10, target at
  +0x28), not the world's ZoneExit layout.
- RefreshQuests (0064E8B0 -> 0064DC60, the selected quest's description) trims the sprites and
  regions to 21 + exits and 9 + exits and then adds one sprite and one region per place of the
  quest (8-byte points at +0x1CC..+0x1D0); it returns before that when there is no such quest.
  The first verify wrapper expected exactly 21 + exits and 9 + exits and so reported a quest
  with a place (gz1g, Rebirth run) as a mismatch; the wrappers now allow those places.

### Minimap (CHudMap)

- Fields: +0x14C/+0x150 the camera's ground point (0079B388), +0x154/+0x158 facing, +0x15C/+0x160
  world size, +0x164 world units per map texture, +0x168 scale, +0x16C zoom (1 .. +0x170), +0x178/
  +0x17C the round map's outline (map units), +0x188 the outline clipped to the world (8-byte point
  array, SetSize 00453100). Scene objects: 0 world-bounds quad, 1 visible-area fan, 2 markers, 10
  compass needle.
- The interface sprites are meshes (`CUiMesh`): quads as blocks of four x then four y floats
  (+0x1C0), 6-byte vertex records (+0x1E0: vertex number, frame), 16-bit indices (+0x200), texture
  coordinates (+0x210), visible flag (+0x220); 005EA150 hands the arrays to the renderer.
- Markers: party members and listed units (local view +0x50D8) as arrows (frame 0 party, 1
  neutral, 2 hostile by both relation masks; unit 0xA2C2A counts as hostile in zone gz20g),
  marked objects (the manager at 007AF748, list +0xD4) as squares (frame 3), zone exits that lead
  somewhere as squares (frame 1), other players in multiplayer as diamonds (frame 0). Units with a
  squared map distance over 0.56 are hidden; other markers use the zoomed place inside 0.6 and
  otherwise the unzoomed one when it lies on the rim (squared distance at least 0.68, both
  coordinates within 0.97); hidden entries keep their old positions and get cleared vertex records.
- UpdateView clips the outline against x >= 0, y >= 0, x <= width, y <= height in place (insert
  the crossing before the edge's end, then remove the outside points), pads it to a multiple of
  four with its first point and fans it. The needle angle is FPATAN(-x, -y) of the facing.
- The wheel zooms by 2.5^(delta/1200) (_CIpow) only over region 0 (the sling button,
  780..800 x 0..30): RegionAt returns the first region containing the point. Kept.

### Clock, top panel, portraits

- Clock: +0x14C blink phase (wraps at 1), +0x150 dial angle (wraps at pi/6), +0x154 fast speed
  (copied to CInterface3D +0x94), +0x158 pause highlight. The time of day is (ticks 007AF61C *
  007AF5D8 + 007AF5D4) mod 24 through _CIfmod (inline asm, as WorldTimeOfDay in script_eval.cpp).
- Top panel: +0x150 offset (ftol of travel * eased slide), +0x154 travel, +0x158 slide; the sling
  sprite (object 2) turns over while sliding out. Writes CInterface3D +0x8A (shown) and +0x90 (page).
- Portraits (`HudPortraitSlot`, 0x78 bytes, now named in ui.h): bars (objects 5i+2/5i+3) by health
  and mana, the hit flash (5i+5, alpha in +0xD4), the face picture state (00640670: 0 normal, 2 hit
  for 2 s, 1 for 4 s, 3 mana under a quarter), the face model by the unit's build +0xC8 ("th" under
  0.2, "fa" over 0.8, else "me"; 005FF460 replaces the scene object, so the rewrite re-reads the
  object array after it), the face easing (Slerp by 2*dt towards restA*restB) and a random wobble
  started with probability dt/3 per frame (RNG 007A9D40), then the marker over the selected unit
  (0079B3C4). Three quaternion products are inlined, each with its own operand order; the rewrite
  has one function per product. The RNG's double scales (0073C468, 00742760, 0073C508) are not
  exact powers of two and are read from the exe. The random words are loaded like the original's
  FILD qword (exact): MSVC's unsigned-to-double (FILD dword, then FADD 2^32 for words of 2^31 and
  up) rounds in that FADD at the game's 24-bit precision, which changed the wobble delay
  (slot +0x3C) in 4 of 5304 calls before the fix.

### Builders and the Esc menu click

- Save/load (0x194 bytes) and options (0x418) windows are constructed inline by the Esc menu
  click (container constructor 00600B20, member vectors 0062B550/0062B570/0062B5E0, CStringArray,
  vtables 00741388/00741354 and 00741FAC/00741F78) and pushed with 005F7350. The quit box is
  00634650 ("quit_msg"). Save opens with +0x178 set, load without; both slide in.
- Builders: textures 005FEF20/005FF0B0, sprites 005FF3C0 + 005E70E0 (rectangle) / 005E7D60
  (frame), regions with tooltips 005FC730 (by id) / 005FC6B0 (by text), surface 005FCC00, scroll
  bar 0065AEA0 (RECT by value), model 005FC1F0 ("unmoco1", "escMenu00", "escMenu00labels") turned
  about x by a global angle (007C81C8 save/load, 007C81A0 options, 007C7FB0 Esc menu).
- The Esc menu takes its five rectangles from 007C80A8.. (menus.reg) through CopyRect and 0062AC90
  (SetRect with a RECT by value); the rewrite calls SetRect's original address directly.

## Rebirth

Rebirth's addon.dll also rewrites data the original reads at run time: it stores its widescreen
layout width into 00742B08 (the clock's x centre, 800 in the exe). The first CHudClock::Update
compiled that constant in and mismatched under Rebirth; it now reads all its constants from
memory as the original does. A scan of addon.dll and EI_Plugin.asi for the other constant
addresses and code ranges of this pass found no further writes.

CUiRegion::SetRect (005FC9E0) and AddPoint (005FCA70) are Rebirth-patched, as are 005E70E0,
005E7D60 and 005E6FA0 (sprite placement): every new caller goes through the original addresses
(`CUiRegion::CallSetRect`, `CUiMesh::SetRectangle/SetFrame/Place` forwarders).

## Forwarders added (standalone build must replace)

Meshes 005EA210/005EA600/005EA4C0/005EA380/005EA150, 8-byte point array SetSize 00453100,
sprite placement 005E6FA0/005E70E0/005E7D60, sprite position 005E7010, frame time 00600270 (as a
free function), panel layout 00649E20, face picture 00640670, scene object replacement 005FF460,
textures 005FEF20/005FF0B0, sprites 005FF3C0, region tooltips 005FC730/005FC6B0, SetRect
005FC9E0 (original address), surface and scroll bar constructors 005FCC00/0065AEA0, container
constructor 00600B20, AddChild 00600D30, member vector constructors 0062B550/0062B570/0062B5E0,
message box 00634650, window activation 005F7350, model load and parts 005FC1F0/005FC4F0/005FC4C0,
save list 00655D40, options tables 00651940/00652200/00651600, text lookup 004DEFC0, the game's
_ftol 006E853C, _CIfmod 006E95AA and _CIpow 006E81A0 (inline asm), FPATAN (naked). Objectives
window: hash_map constructor 00609C10, variable copy by prefix 00578B70, string hash 00460BA0,
Split 004DBE40, vector<CString> insert overflow 0051F790, sort pieces 0064F310/0064F9A0/0064FBD0,
exit array 0064EFD0/0064F160, rb-tree increment 0043DBE0, surface text 005FDC00/005FE2F0/
005FDE20 (Rebirth)/005FDC60, triangle sprite 005E99D0 (Rebirth), sprite scale 005E70B0, scroll bar
range 0065AFE0, text log constructor 0065CC00, exit selection 0064E990, quest list 0064E8B0.

## Original bugs and quirks kept

- The Esc menu click stores the save/options flags through the new window without checking the
  allocation, and activates a null quit box (FatalError) when that allocation fails.
- BuildMarkers reads a zone exit's target through a copy of the exit record (CString copy and
  release); the rewrite reads the target directly (same string).
- The options window's Build appends the 11 group titles without clearing the array first.
- Hidden minimap entries keep stale positions; the wheel zooms only over the sling button.
- Copies the original makes through FLD/FSTP (bar y values written back unchanged) are skipped.

## Harness and scenario

New harness step `wheel <x> <y> <delta>` (WM_MOUSEWHEEL with screen coordinates after a mouse
move). `tests/scenarios/ui_hud.txt` turns the wheel over the map's sling button (in, then out
past the limit) and over the map. The every-frame functions (markers, view, clock, portraits, top
panel) run throughout ui_hud and ui_screens; the Esc menu, save/load and options builders and the
Esc menu click run in ui_hud's menu walk, the Esc menu build also in ui_screens. ui_hud also
opens the objectives window with TAB (its Create and zone page), moves the quest selection and
closes it with Esc. The objectives window's +0x17C zone list and the global map's distance table
(+0x178) are not reached by a scenario (they need the global map or the camp).

## Mapped, not rewritten

- 0062D350 (6.7 KB) is the global map's Create (slot 25 of 00740B3C): zone and allod pictures,
  the paths and quests between zones from the player's variables (a hash map copy 00578B70 walked
  with 004614C0/005E0DB0), exits, the clock text (`save_day`, gtime), weather by island. Only
  reached through a zone exit, which no scenario takes.
- 00604C50 (4.5 KB): a briefing/dialogue script step (`#show`, `#hide`, `#phrase`, `#camera`,
  `#animation`, reward texts `brief_get_*`).
- 005F1590 (4.9 KB): the camp's skill/item description text drawing.
- The objectives window's other methods: key handler 0064E5A0, quest list 0064E8B0, exit
  selection 0064E990, clicks 0064E330, update 0064E760.
- 00635250 (2.6 KB): the battle screen's setup (creates the HUD panels; reads the save stream when
  loading).

# Interface base (005E0000-00610000, third pass)

Files: `src/game/ui_console_window.cpp` (console window drawing and keys), `src/game/ui_interface_frame.cpp`
(CInterface3D's frame update), `src/game/ui_text.cpp` (word-wrapped text), `src/game/ui_gdi.h` (the game's MFC
CDC/CPen as the interface uses them); wrappers in `tests/verify/verify_ui_base.cpp`. Shared headers: `ui.h`
(declarations of `CInterface3D::Update` and `CUiSurface::DrawWrappedText`), `console.h` (the console window's
fields +0x08..+0x2C and +0x78/+0x79 named, four method declarations).

## What the range holds

- 005E0B20-005E1580: STLport map/tree templates of the console command maps (library code).
- 005E18E0 (2959): `CFlyingHitPoints::Draw` (damage numbers over units; called from 00569E40, Rebirth-patched),
  float-heavy vertex building, left for later. 005E25B0 removes an entry.
- 005E26F0-005E3930: the chat/message window (005E2A30, 2045 bytes: MBCS word wrap of a message with the
  text extent 005F8340; 005E3230 its key handler (vtable 007400CC); 005E3700 drawing, Rebirth-patched).
- 005E3CE0-005E4D25: the console window (below). 005E44A0 (character input) is Rebirth-patched.
- 005E4E00-005E6990: the mouse cursor (005E5200 SetShape on 007C7BD8; 005E5690/005E5C40 draw and blit it into
  locked surfaces), sprite loading 005E63F0 (49 callers).
- 005E6D90-005EA850: interface sprites/meshes (`CUiMesh`): 005E6FA0/005E70E0/005E7680/005E7D60/005E99D0
  Rebirth-patched; 005E8450/005E8C60/005E91F0 further rectangle/frame builders (same shape, not patched).
- 005EADD0-005EBFB0: camera and pick input of the battle view (vtables 0074021x).
- 005EC020-005F3000: item, spell and unit preview and description drawing for the camp and hero screens
  (005ECA10, 005ED7B0, 005EE760 description dispatcher with 005EF300/005EF5D0/005EFD90/005F0370/005F0B90/
  005F1130, 005F1590 the camp's skill/stat description): CString formatting into 005FDE20/005FDFA0.
- 005F3000-005F4990 and 005F9810-005FC010: STLport/MFC containers (layout maps, CArchive serialisation
  005FB120/005FB380/005FB5F0).
- 005F4990-005F9000: CInterface3D: Create 005F4990 (Rebirth), destroy 005F4F90, Draw 005F5320 (Rebirth),
  Update 005F57C0, CreateScreen 005F5BA0, window activation 005F71C0/005F7350, keys 005F78C0/005F7B40,
  scaling, script commands 005F8070 (Rebirth), text extent 005F8340 (Rebirth), help 005F86F0, message
  boxes 005F8A80.
- 005FC1D0-00601B70: CUiModel, CUiRegion, CUiSurface (DC 005FDC00/005FDC60, text 005FDE20 (Rebirth), wrapped
  text 005FDFA0, fill 005FE2F0, save/restore 005FD7A0), CUiWindow and the container base.
- 00601B70-00610000: briefing / dialogue screen (vtables 00740580..: 006024B0 Create, 006030F0 click,
  006034A0 keys, 00604C50 the script step `#show/#hide/#phrase/#camera/#animation`, 006064E0, 00608450) and
  the camp screen's first methods (0060C870 Create, Rebirth).

## Rewritten

| Address | Size | Name | Verify |
|---|---|---|---|
| 005F57C0 | 748 | `CInterface3D::Update` (every frame) | ours only: the mouse copy (+0x68/+0x6C from 007C7C50) |
| 005FDFA0 | 838 | `CUiSurface::DrawWrappedText` | side by side: line count and the surface's pixels through GDI (the caller holds the DC): recorded, the original's result recorded and the old pixels put back, ours compared |
| 005E4A10 | 789 | `ConsoleWindow::DrawInput` | side by side: caret/anchor clamp and the surface's pixels (DirectDraw lock); before ours the pixels are put back with the input strip's rows inverted, so anything ours leaves undrawn shows |
| 005E4180 | 709 | `ConsoleWindow::OnKeyDown` | side by side: input text, caret, anchor, command index, scroll, Ctrl/Shift (restored between the runs) |
| 005E4800 | 524 | `ConsoleWindow::DrawLines` | as DrawInput (rows above the input strip); runs on every console message (without a surface: the scroll clamp only) |
| 005E3FD0 | 182 | `ConsoleWindow::Show` | ours only: flag, surface exactly while shown |

6 functions, 3790 bytes.

### Details

- Update: while a value is pending (+0x4C) the top window gets input slot 1 (cancel), slot 26 and its
  deleting destructor and is popped; messages are pumped (00458220); backdrop state 4 swaps in +0x58
  (005FD7A0 saves it); with one window left the backdrop and +0x5C are freed and +0x60 goes to the world
  server's +0x6C (00578D10, not in a network client); the new top gets slots 19(1) and 27(value); 0x990001
  sets close-all and leaves the game (0057ECA0, 00581660); the capture +0x0C is cleared. Then +0x81 polls
  slot 35, close-all (+0x44) pops every window and calls 005F8C50, a queued window (+0x40) is activated
  (005F71C0), the mouse is copied, a network client that lost the server (+0x88) gets the "disconnect"
  message box (00634650, 0x990000), else the top window's slot 24 runs; last the root (+0x1C) slot 24.
- Console window fields: +0x08 font, +0x0C CStringArray of entered commands, +0x20 index into it, +0x24 input,
  +0x28 caret, +0x2C anchor, +0x78 Ctrl, +0x79 Shift. Keys: PgUp/PgDn a line (Ctrl: a page), Home/End the
  caret (Ctrl: first/last page), Left/Right, Up/Down through the commands, Delete. Drawing: background
  0x282828, lines 0xB4B4B4 bottom up 15 px apart (DT_NOPREFIX), "+" marks (0x787BD8) at the right, the input
  after "> " (0x792DE0) in white, selection 0x505050 clipped at width - 16, a 2-pixel caret pen.
- DrawWrappedText: the text plus "\n" is walked with the game's `_ismbblead`/`isleadbyte`/`_mbsinc`; a word
  ends at ' ' or '\n'; when line + word is wider than the rectangle (005F8340 with the font argument) the
  line is stored and the word starts the next; '\n' stores the line (and the word on its own line when it
  did not fit). Lines are drawn with 005FDE20, each one rect height lower; the full line count is returned.
- 005F8340 takes a CString by value; a naked forwarder cannot declare one (C3068), so the data pointer is
  handed over and the local detached (`TextMetricsAt::TextExtent` in ui_text.cpp).

## Forwarders added (standalone build must replace)

MFC CDC FillSolidRect 0070BB04, SetBkMode 007090C2, SelectObject(CPen*) 00708F5A, MoveTo 00709613, LineTo
0070965F, CPen constructor 0070A0E2, CGdiObject::DeleteObject 0070A0CC, CString::Left 006FDAE9; CDC virtual
slots 10 (SelectObject(CFont*)), 12 (SetTextColor), 26 (DrawText). Surface GetDC/ReleaseDC 005FDC00/005FDC60,
surface constructor 005FCDD0 and destructor body 005FD120, save 005FD7A0, text 005FDE20 and extent 005F8340
(both Rebirth-patched), cursor 005E5200, message pump 00458220, session 0057F440/0057ECA0/00581660, world
server flag 00578D10, activation 005F71C0/005F7350, close-all 005F8C50, message box 00634650, `_ismbblead`/
`isleadbyte`/`_mbsinc`.

## Original quirks kept

- The console's Up key increases the command index; Down stops at -1. Delete with no selection at the end
  of the input does nothing (no redraw).
- DrawWrappedText stores a word that did not fit before a '\n' on a line of its own (even an empty one).

## Skipped

- Rebirth-patched: 005E3700, 005E44A0, 005E6FA0, 005E70E0, 005E7680, 005E7D60, 005E99D0, 005EB210, 005F4990,
  005F5320, 005F8070, 005F8340, 005FDE20, 0060C870, 0060FAB0 (and the scaling/region functions above).
- Not reached by any scenario (camp, hero screen descriptions, briefing dialogue): 005F1590, 00604C50,
  005ECA10, 005EE760 and its parts, 006064E0, 00604130. Mapped above; good targets once a scenario reaches
  the camp or a dialogue.
- 005E18E0 flying hit points (float vertex building), 005E5690/005E5C40 cursor blits (surface locking),
  005E2A30 chat word wrap: left for a later pass.
- Template code (005E0B20-005E1580, 005F3000-005F4990, 005F9810-005FC010, 0060A320-0060BD70).

## Harness and scenario

New key names `grave` (the console key, scan 0x29), `backspace`, `delete`, `home`, `end`, `pgup`, `pgdn`,
and `enterchar` (Enter plus its WM_CHAR, which the console's character input needs).
`tests/scenarios/console.txt` now opens the console window in game, types "lastxfps", moves the caret,
deletes the x, runs it with Enter (expects "Current FPS:"), walks the command history, scrolls and closes
the window. CInterface3D::Update runs every frame in every scenario.

# Camp, global map and battle screen setup (00600000-00660000, fourth pass)

Files: `src/game/ui_camp.h` (new; the classes below), `src/game/ui_battle_setup.cpp`,
`src/game/ui_global_map.cpp`, `src/game/ui_camp.cpp`, `src/game/ui_hero_panels.cpp`; wrappers in
`tests/verify/verify_ui_camp.cpp`; scenario `tests/scenarios/ui_camp.txt`. Shared files changed:
`tests/verify/harness.cpp` (new `screen <id>` step, two forwarders) and one line in `README.md`. No
shared header was changed (`ui_camp.h` includes `ui.h`, `ui_screens.h` and `object_tables.h`).

## How the screens open (corrections to "Screens" above)

- `CInterface3D::CreateScreen` (005F5BA0) is called by the client's message 7 handler (00669D50,
  slot 7 of the table set up by 00667230) with the id the server sends: 1 the battle screen
  (vtables 00740764 / 00740730, Create 00635250), 2 (00740688), 3 the briefing / base screen
  (007405AC), 4 the global map, 5 the camp, 7, 8, 9.
- The client asks for a screen with message 4 (`00578B00(id)` -> 0066B280; it also sets the client
  player's +0x20 to 3). The server's handler (00670CE0, slot 4 of the table at 0066CBE7) accepts it
  only in state 1 (in the world): it removes the party from the world (00590A40), moves to state 2
  and answers with message 7. Message 5 (`00578B20` -> 0066B2C0, needs client state 3; the camp's
  page 7) brings the party back (00670DA0: state 2 -> 1). So the global map's camp button works
  only where the map is the world's own screen (a "brief" zone).
- `LeaveToZone(player, zone, exit)` (0066F310, server side) sends message 8; the client (00669EB0)
  sets the game request kind 4 and the main loop loads `zone` directly (0057C460). The zone's type in
  map.txt decides the mode: `game` 0 (mode 1, the battle screen), `brief` 1 (mode 4, a base such as
  bz1g, the briefing screen), `edge` 2 (mode 3). The global map is not shown on the way.
- The harness's new `screen <id>` step does what the global map's buttons do (for 5 it first sets
  "constr.current" to 0, as 0062FE60 does) from the battle screen of zone1.

## Rewritten

| Address | Size | Name | Verify | Scenario |
|---|---|---|---|---|
| 0062D350 | 6721 | `CGlobalMap::Create` (slot 25) | ours: marker counts and arrays, 13 + markers sprites, 3 + links regions, 2 surfaces + backdrop, links point at zones, fixed fields | ui_camp (`screen 4`) |
| 00635250 | 2596 | `CBattleScreenSetup::Create` (slot 25) | ours: 11 panels appended in order with their vtables and owner, 2 input handlers, textures, reset fields, hero id | every scenario |
| 0062F050 | 2302 | `CGlobalMap::Update` (slot 24) | ours: bob time = stored FrameTime + old, selection refresh done, w >= 0 after a turn | ui_camp |
| 00611930 | 2361 | `CCampPages::UpdateAction` | side by side (original first; ours releases what it made): cost, flags, made-or-not, button shades | ui_camp (every camp frame) |
| 006100E0 | 2078 | `CCampPages::CanEquip` (slot 43) | side by side, AL | ui_camp (strips) |
| 00623880 | 2645 | `CHeroStatsPanel::DrawStats` | ours: state untouched (GDI text only) | ui_camp, ui_screens (hero screen) |
| 0060E9E0 | 1609 | `CCampPages::ShowPage` | ours: the shown page's panels exactly enabled, strip slots, button shades; page 7 leaves | ui_camp |
| 006233C0 | 1207 | `CHeroStatsPanel::ShowStats` | ours: 2 + 9 / 13 / 8 regions, page, objects 4.. shown on page 10 only | ui_camp |
| 00622DB0 | 1307 | `CHeroStatsPanel::Build` (slot 18) | ours: counts, 8 texts, fields | ui_camp, ui_screens |
| 0060F050 | 998 | `CCampPages::Draw` (slot 20) | ours: returns 1, page/hero unchanged | ui_camp |
| 00612600 | 946 | `CCampPages::CanMakeItem` | ours: result iff a new item is held (with its reference) | ui_camp (page 5) |
| 00612290 | 622 | `CCampPages::CanMakeSpell` | ours: result implies a held container | ui_camp (page 3) |
| 00622000 | 1041 | `CHeroEquipPanel::Build` | ours: counts by `body`, rectangle, selection | ui_camp, ui_screens |
| 006280C0 | 980 | `CConstructorPage::BuildSpellMaker` | ours: counts, texts, fields | ui_camp |
| 006270A0 | 744 | `CTradeGrid::Build` | ours: 16 regions, 32 sprites, textures, surface, kinds | ui_camp |
| 00628CC0 | 696 | `CConstructorPage::BuildItemMaker` | ours: counts, fields | ui_camp |
| 006279E0 | 682 | `CRepairGrid::Build` | ours: as the trade grid | ui_camp |
| 00622A80 | 312 | `CHeroQuickStrip::Build` | ours: 8 regions, 2 sprites, y | ui_camp |

18 functions, about 32.5 KB. All are hooked as one function each (helpers such as the cell grid of
006270A0 / 006279E0 are shared C++ code). None is Rebirth-patched; every Rebirth-patched callee
(005E70E0, 005E7680, 005E6FA0, 005E7D60, 005FDE20, 005FC9E0, the panel builders 0063BAB0 ...
00648520, the price slot 54 0060FAB0) is called through its original address or the vtable.

### Details

- Battle screen Create: when a game is loaded it reads the screen's part of the save (007AFDF8:
  hero id count and ids into +0x1DC, the map zoom, the server flag byte, from version 0x47 an int
  and a string that is appended to the top panel's text and, on page 0, to the message log; from
  0x62 +0x18C and +0x188), else remembers the local player's first hero. Panels: weapon bar,
  quick items, spells, actions, portraits by their constructors; unit panel, map, clock, movement
  panel, top panel built inline (window constructor 005FE700, owner +0x148, vtables) and set up by
  their builders with the interface's saved settings (+0x89/+0x8C, +0x8B, +0x94, +0x8A/+0x90);
  the message log 0065CC00. The "pers <name>" texts go into the map<int, CString> at +0x1F0 by
  the name's text key (insert with hint 00609E70).
- Global map: positions on the island model are `centre + turn(local)` where the centre is the
  layout point (400, 280) at depth 10 in camera space (`(x * 0.0025 - 1) * 0.48157 * depth`, each
  stored) and `turn` the quaternion at +0x1EC (spin about z by +0x1FC times a tilt of 0.8 pi about
  x; the product is inlined with its own operand order, TurnProduct). Markers: zones of the allod
  with a figure (state = "z.<zone>"), the quests "q.<zone>.<quest>" = 1 (bit compare with 1.0f),
  the ways out whose "z.<way>" equals 2 (FCOMP: unordered counts as equal) with a region each
  (20 either side, 25 above, 10 below the sprite's layout position, truncated with _ftol). The
  clock text is "<save_day>  <day>,  <h>:<mm>" from gtime * 60 minutes. Fog colours by allod
  (ingos, suslanger), +0x278 for gipat. Arrived from anything but the camp (screen 5) it autosaves
  to "saveb" when 0079BE7C is set.
- Global map Update: while turning (+0x174) or dragging (+0x264) the angle grows by
  `FrameTime * dir * pi * k + angle` (stored), wrapped with _CIfmod by 007C8020; the markers and
  the link regions follow. Always: the arrow bobs (`0.5 - cos(t * k) * a + z`, t unrounded), the
  three layers' texture frames take the fraction of `t * rate`, the weather (00499F10) is drawn
  into texture 4's locked surface under the interface's critical section 007AEE78.
- Camp pages (CCampPages = the camp screen's fields): page buttons are regions 4-11 (page 7
  leaves); constructor pages need +0x195 (spell) / +0x196 (item), their second pages also
  `!+0x254` ("i.noconstr"). ShowPage disables every page's panels, enables the page's, sets the
  item strip's slot count (9; 1 on page 2; 5 on page 4), draws the page title (surface 1) and
  shades the page buttons; an unoffered page shows the current one again (recursively, the tail
  then runs twice, as in the original).
- UpdateAction (every frame from Draw): page 0 repair-all of the hero's weapons (+0x730) and armour
  (+0x750) where the item constructor works; page 1 the level-up (trade panel 00625510/
  00625560/00625530, experience shown instead of money); pages 2/4 trade (price slot 54 times count
  +0x48, bought minus sold); pages 3/5 take apart a finished container / blueprint or make (CanMake*),
  the help panel describing it; page 6 repair. Money below the cost disables the button.
- CanEquip: the strips' filter per page (see the code): weapons and armour on page 0 (worn-out ones
  not to a hero: FISTP of +0x20 <= 0), spell containers a hero can cast on page 1 (HeroCanUseSpell),
  the spell constructor's container / one spell / up to eight modifiers fitting the spell (kind
  flags at +0x78) within the party's best skill and mana (slot 45; mana summed as stored floats),
  the item constructor's blueprint, parts by blueprint (name compare) and containers whose spell
  fits the item made (+0x40 / +0x2C), repairs where +0x24 > +0x20.
- Statistics panel: the new hero page shows strength, dexterity, intelligence (FTOL of +0xC/+0x14/
  +0x1C), points left `ftol(75 - (float)sum)` and -/+ buttons coloured by `value > 15` and
  `stored < 35 && points > 0`; other pages the values (+0x10, +0x18, +0x20, hit points 00523E80,
  mana 00523F50, +0x30, experience +0x700 + +4 unrounded) and the encumbrance (00660840, or the hero
  screen's slot 53 on page 11) against +0x54, red when over; page 0 the armour by body part and the
  weapon's figures (unit +0x240 slot 50).

## Forwarders added (standalone build must replace)

Panel constructors and builders 0063BAB0, 0063C800, 0063E2A0, 0063F300, 0063FE70, 0065CC00,
006456E0, 00641F20, 00648520, 00649AF0, 0064A7A0, 0065CF30, 005EACB0, window constructor 005FE700,
container inputs 00600CC0, DWORD array SetSize 0059C660, party array SetSize 005EBDB0, save stream
004A5490/004A54D0/00461190 (vtable slot 1 Read), game helpers 00578D10/005786A0/00578950, text
tables 004DEFA0/004DEFC0/004DF8D0, map insert 00609E70. Global map: 00631110, 00630E00, 00631030,
00630F50, 00630CC0, 00630860, 0062EE90, 00630670, arrays 00631CC0/00631E70/00631FF0 and element
assignment 00632490/006324E0, rotation 0046A4B0, sprite scale/position 005E7060/005E7010, hash map
00581790/004614C0/005E0DB0/00578B70, Split 004DBE40, 00499DC0/00499EE0/00499F10 weather,
006B4F70, autosave 006570E0, FatalDirectX 004800E0, cursor 005E5200, _CIfmod 006E95AA (inline asm).
Camp: help panel 005F2910/005EE6C0, trade panel 006255A0/00625860/00625510/00625560/00625530/
00625690, stats panel 006233C0 (rewritten, called through its address)/00624CB0/006242E0/00624590,
description 005F1590, hero preview 005EC300, item predicates 005EE050/005EE080/005EE0B0/005EE0D0/
005EE110/005EE130, item moves 005EE2C0/005EE1B0/00621570, back to the world 00578B20, spell
constructor 006841A0/006841E0/006842A0/00684250, item constructor 006A5C00/006A5C50/006A5CC0/
006A5CD0/006A5CE0/006A5CF0, spell clone 0068B410, cast flag 0068B7C0, RefPtr assign 0052C690,
container record 00613820, item factory 006AD000/006A04D0, carried weight 00660840, sprites
005E8450/005E7680. Harness: 00578B00 (screen request), 00578560 (constr.current).

## Original quirks kept

- Global map: the marker arrays' templates have uninitialised numbers in the original (they are
  overwritten right after); the rewrite's are 0.
- Camp Draw: the hover info passed to 005F1590 has uninitialised bytes +4..+0x13 unless a panel
  fills them (005F1590 returns before reading them when the kind is -1); the rewrite zeroes them.
- CanMakeSpell keeps the modifiers in an 8-entry stack array without a bound (CanEquip allows at
  most eight); the rewrite's array is larger, so more than eight would differ only where the
  original overwrites its frame.
- CanEquip on page 1 reads the container's spell (+0x44) without a null check; CanMakeItem's item
  type stays unset without a part (never used then).
- The ShowPage fallback for an unoffered page runs the whole tail twice.

## Not rewritten (mapped)

- The credits (0062B7D0 Create: parses outro.res credits.scr, a RichEdit window 0065ED30 and a
  bink movie 005DBA50): the scenario must not play movies, so no scenario reaches it.
- 00612CF0 (camp slot 39, 1776 bytes): fills the constructors with every blueprint and part
  ("Instruction_Weapon", "Instruction_Armor"; a debug fill). No caller found besides the vtable.
- 00625880 / 006261A0 (the trade panel's skill and perk lists, STL vectors of 0x18-byte entries),
  00624F10 (trade panel build), 00626920 (its click), the drawing slots 20 of the panels (00622420,
  00622C10, 00627390, 006284A0, 00628F80, 00627C90), 00624A60 / 00624790 (stats update and click),
  00610AD0 (camp slot 42, item chosen), 00611220 / 00611760 (the action buttons), 0060E6B0 /
  0060E3E0 / 0060FCA0 / 0060FE40 (camp slots 26 / 47 ...). Good next targets: all are reached by
  ui_camp.
- The exchange window (0062C250 Create, 0062C9C0) and the multiplayer lobby screens (0061C430,
  0061C700, 0061D470, 0061E2F0 ..., 00617050 the hero screen's click, 00618A10, 00617A50,
  00616AC0, 006159E0): need a network session; no scenario.
- The briefing screen's functions (00604C50, 00604130, 006064E0, 006024B0, 006034A0 ...) belong to
  the base screen (CreateScreen 3); not in this pass.

## Scenario `tests/scenarios/ui_camp.txt`

New game; `screen 5` opens the camp as the global map's camp button does ("constr.current" 0, no
constructor: Create, Draw, UpdateAction, the stats/equipment/quick-strip Builds and ShowStats);
regions 5-10 and 4 ask for pages 1-6 and 0 (2-6 are not offered and fall back), regions 1/0 the hero
arrows, 2/3 turn the hero; Esc (page 7) leaves the camp and the game resumes (the battle screen's
Create runs again); `screen 4` opens the global map (Create, Update every frame), regions 0/1 turn
it, Esc opens and closes the escape menu over it. About 40 s. The battle screen Create runs in
every scenario.

The first version also set "i.onspellconstr" / "i.onitemconstr" to offer the constructor pages.
That crashed the original too (00621377 in CUiItemStrip::RebuildList, reading 0x44): with
"constr.current" 0 the camp's Create (0060C870) has no constructor record (+0x198 = 0), yet with
those flags it creates the spell and part strips and gives them `+0x1C8 + 0x40` /
`+0x1B4 + 0x40` as item lists (00621160); those fields are the record's +0xAC / +0x3C and stay 0
without a record, so the lists are at address 0x40. The flags are only valid with a
constructor, which the camp gets from a base's dialogue ("constrN" answers, 006064E0 -> 00578560(N)
-> screen 5) in a "brief" zone; that path needs the briefing screen's dialogue and is not scripted.

Not reached by a scenario: the constructor pages' Builds (006270A0, 006280C0, 00628CC0, 006279E0),
CanMakeSpell / CanMakeItem, and probably CanEquip (slot 43; asked when an item is picked or dropped).

## Unit info texts (00647040)

Files: `src/game/ui_hud_unit_info.cpp`, `tests/verify/verify_ui_hud_unit_info.cpp`.

| Address | Name | Verify |
|---|---|---|
| 00647040 | `DrawUnitInfo` (battle HUD info panel thiscall, RET 4) | ours only: the unit's health / mana words and the panel's surface and label table unchanged |

Called by 006462A0 (Rebirth-patched) for the unit under the cursor. The panel: +0x118 points at
the surface (its text draw 005FDE20 is Rebirth-patched, called through its address), +0x1A0 the
label table (CStrings), +0x148 the group (+0x16C units, +0x170 count). Lines, from (5, 0x23) to
(0x9B, 0x32), 15 pixels apart, colour 0xA7D7E4:
1. label 0 "%s %d/%d": health (+0x17E, negative shown as 0) / maximum (+0x180).
2. label 1: mana +0x182 / +0x184 (unsigned words).
3. label 2 "%s %.1f %s": the speed, (00742408 * speed) * 00742AEC on the FPU, the speed from
   0052FAF0 mode 3 for a unit outside the group, else 0052FA20; the suffix is the text of
   "move_<n>" for the parameters' +0xF above 1 (n = value - 1), else empty.
4. label 6 "%s %d%s" label 13: the vision range at the time of day (0055FC50 rounded to float,
   005271E0, rounded to float, FISTP).
5. labels 3 and 4: attack and defence from the controller (+0x240, slot 50, a 0x20-byte record).
6. label 9 "%s %d-%d %s": damage minimum / maximum (truncated) and the text of "dmg_<type>" for a
   type below 7; for another type the suffix keeps the move text (original behaviour).
7. label 15: the parameters' +0x15 byte.
8. The description (005270E0 into a 0x1C-byte record, 005F13A0 (Rebirth-patched) into lines):
   label 10 + line 0 always; outside the group label 11 + line 1 when not empty, label 5 when the
   prototype's vector at +0x100 is not empty, and with a group "consider_more<d>" /
   "consider_less<d>" for the level difference to the first group unit, clamped to 25 (looked up
   in place: the same CString is key and result), drawn after label 16; inside the group label 12
   + line 2 when not empty.
9. A multiplayer client, for a group unit: "camp_current_exp" and the parameters' +8.

The damage values go through the modern `__ftol2_sse` instead of the game's `_ftol` (both
truncate).

## Objectives window: the selected quest (0064DC60)

Files: `src/game/ui_objectives.cpp` (`CObjectivesWindow::ShowQuest`), `tests/verify/verify_ui_objectives.cpp`.
`ui.h`: `unknown_1A4` renamed `shownQuest_`, `ShowQuest` declared.

| Address | Name | Verify |
|---|---|---|
| 0064DC60 | `CObjectivesWindow::ShowQuest` (`__fastcall`, called by RefreshQuests 0064E8B0) | ours only: the shown quest is the selected one; extra sprites / regions are the quest places' or none |

Only when the selected row differs from the one shown (+0x1A4): the log (first child) is cleared
(0065D450); for a valid row the global quest name 007C8178 is set, and when "quest <name>" is in
the texts the log gets: colour code 007946F8 + the text up to '#', "\n", then (when the quest
database's index 007C87B4 has the quest; its operator[] 0064EE90 adds an empty slot otherwise)
colour 007946EC and "obj_cost_exp %d\n" / "obj_cost_money %d\n" for rewards above 0 (truncated).
Then each line of the text: "#quest" ends the loop; "#subobj <id>" with the player's variable
q.<zone>.<quest>.<id> (truncated) not 0 adds colour 0079349C, the next line, " ", for 2 colour
007946B8 + "obj_completed", for 3 colour 007946C0 + "obj_failed", then 007946AC, the text up to '#'
and 007946A4. The log text is set (0065CFE0). With the zone in the map database: 0064EA80 fills the
quest places, the sprites / regions after the exits' (21 + exits, 9 + exits) are removed, and each
place gets a 20x20 marker (cross texture, colour (1, 0, 0) at +0x224) at its position on the zone
picture (as the exits: 300 / larger world side, centre (580, 190)) and a region with tip 0x138F0.
