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
