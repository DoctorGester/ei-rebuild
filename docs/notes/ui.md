# User interface (005E0000-00670000)

Current-state reference for the interface code: what is rewritten (tables: address, name, how it is verified), call
chains and scenario preconditions, hazards, original bugs kept, what is left. Layouts, field offsets and per-function
behaviour are in the headers and code (`ui.h`, `ui_camp.h`, `ui_screens.h`, `ui_item_info.h`, `ui_mesh_build.h`,
`ui_cursor.h`, `ui_gdi.h`, `console.h`) and are not repeated here. Files are in `src/game/`, wrappers in
`tests/verify/verify_<name>.cpp`. "Side by side" = the original runs first (state recorded and put back), then ours, and
the results are compared; "ours only" = the original cannot run twice (it creates, draws, sends) and the wrapper checks
invariants of the result. Names are provisional. Old per-pass section titles that code comments still mention
("Sixteenth pass" etc.) map to: input and commands -> section 6, camp -> section 5, items -> section 9.

## 1. Conventions shared by every screen

**Layout of the range:** 005E0000-00610000 interface base (manager, windows, surfaces, sprites, cursor, text, console
window, item description drawing); 00610000-00630000 camp / hero / lobby screens; 00630000-00660000 main menu, battle
screen and HUD, objectives, options, save/load, tutorial, controls. Layouts are authored for 800x600; the four scale
helpers convert to and from the real screen (`x * width / 800`, `y * height / 600`, 32-bit wrapping products,
truncating division).

**Screen classes.** Each screen is a `CUiWindow` subclass with a vtable pair: the input side at `+0x94` (`{vtable,
CInterface3D*, handled}`; handlers get `this` = window + 0x94 and (buttons or key, x, y) in screen pixels; the rewrites
are window methods registered through `__fastcall` shims that subtract 0x94) and the main vtable 0x34 bytes after it.
Main slots: 19 show/hide children, 20 per-frame draw (returns 1, RET 4), 24 update, 25 Create, 26 on activate, 27
message-box result, 42 and up screen-specific. Input slots: 1 deactivate, 2 left down, 3 left up, 4 mouse move, 5 / 6
right down / up, 7 / 8 double clicks, 9 key down `(vk, repeat, scan|flags)`, 10 char, 11 key up, 12 wheel `(keys,
delta, x, y)`; handlers `RET 0xC` (wheel `RET 0x10`) and return 1 on every path. A button-down handler that takes the
mouse stores its input side in `CInterface3D +0x0C` (`mouseCapture_`; the originals store `window ? input : 0`, the
window is never null); the button-up handler acts only when the capture is its own and clears it; a handler sets `+0x9C`
(`inputHandled_`) when it used the event, which stops the container passing it on.

**How the screens open.** `CInterface3D::CreateScreen` (005F5BA0) is called by the client's message 7 handler (00669D50,
slot 7 of the table set up by 00667230) with the id the server sends: 1 battle screen (Create 00635250), 2 (00740688),
3 briefing / base (007405AC), 4 global map, 5 camp (005F6240), 7, 8, 9. The client asks for a screen with message 4
(`00578B00(id)` -> 0066B280; it also sets the client player's +0x20 to 3); the server's handler (00670CE0, slot 4 of
the table at 0066CBE7) accepts it only in state 1 (in the world): it removes the party from the world (00590A40), moves
to state 2 and answers with message 7. Message 5 (`00578B20` -> 0066B2C0, needs client state 3; the camp's page 7)
brings the party back (00670DA0: state 2 -> 1). So the global map's camp button works only where the map is the world's
own screen (a "brief" zone). `LeaveToZone(player, zone, exit)` (0066F310, server) sends message 8; the client (00669EB0)
sets the game request kind 4 and the main loop loads `zone` directly (0057C460); the zone's type in map.txt decides the
mode: `game` kind 0 (mode 1, battle screen), `brief` kind 2 (mode 3, a base such as bz1g), `edge` kind 1 (mode 4) (map
loader 0045EA20); the global map is not shown on the way. The harness step `screen <id>` does what the global map's
buttons do (for 5 it first sets "constr.current" to 0, as 0062FE60 does) from the battle screen of zone1.

**Keyboard.** The interface reads keys from window messages, not DirectInput. WM_KEYDOWN goes to 005F78C0 (console toggle
on scan 0x29, modifier flags at CInterface3D +0x7C Ctrl / +0x7D Shift / +0x7E Alt, screenshot "shot%.4d.bmp" on scan
0x46), then to the top window's input slot 9. In-game hotkeys are looked up by scan code in the table at 0079BF70
(`config\keyboard.ini`, 00462C10); a zone has no inventory or map-screen key (`I` is quick item 3, `M` the minimap).

**Screens by address range** (vtable pairs in ui.h / the headers):

| Range | What |
|---|---|
| 00601B70-00610000 | briefing / dialogue screen (id 3): 006024B0 Create, 006030F0 click, 006034A0 keys, 00604C50 script step, 006064E0, 00608450; not rewritten, reached only by a dialogue |
| 00610020-00612CF0 | camp screen (007408E8 / 0074091C, 0x260 bytes, Create 0060C870 Rebirth-patched) |
| 00613EA0-006195B0 | multiplayer hero screen (`lmp_*`, 00742118 / 0074214C, 0x220 bytes; Create 00613EA0 and mode switch 00615D30 Rebirth-patched) |
| 0061B2C0-0061E2F0 | multiplayer connection screen (`config\addrbook.ini`; 00741C9C / 00741CD0; 0061B2C0 and 0061D780 Rebirth-patched), another lobby window 0061E2F0-00620300 |
| 00620380-00621F30 | camp item strip (007419E8 / 00741A1C; forwarding subclass 00741948 / 0074197C) |
| 00622000-00629AF0 | hero panels shared by the camp and hero screen, camp pages: trade, repair, spell and item constructors (00741504 group) |
| 0062A830-0062B300 | escape menu (00741270 / 007412A4); 0062B7D0-0062C090 credits (00740C08 / 00740C3C); 0062C250-0062D330 exchange between players |
| 0062D350-0062FFC0 | global map (00740B08 / 00740B3C, 0x294 bytes; 0062F950 Rebirth-patched); 00630000-00632000 zone screens |
| 00632000-00635250 | main menu (00632940, 006326B0, 00632D70), new-game dialog (006337B0), message box (00634650) |
| 00635250-0064BE80 | battle screen (`battle00`; 00635250 setup, 0063A560 keys Rebirth-patched), panels, quest / zone messages, portraits, unit info |
| 0064BE80-00658000 | objectives window (0063B5D0), options window, save/load window; 00658000-0065A500 tutorial / help window (created by 005F86F0) |
| 0065A500-00660000 | controls: scroll bar 0065AEA0, spin / edit 0065BC30 / 0065BCD0 / 0065C010, scrolling text 0065CC00 (battle message log), tooltip 0065E320, DirectInput mouse 0065EAD0, RichEdit 0065ED30, party bar 0065F2D0 |

**Rebirth.** Rebirth's addon patches the five scaling helpers, the UI sprite placement (005E6FA0, 005E70E0, 005E7D60,
005E7680, 005E99D0), CUiRegion::SetRect / AddPoint, 005FDE20 (text), 005F8340 (text extent) and many screen builders and key
handlers (`docs/rebirth-patched-functions.txt`). Rewritten callers go through the original addresses
(`CInterface3D::CallScale*`, `CUiRegion::CallSetRect`, `CUiMesh::SetRectangle / SetFrame / Place`): calling our scaling
versions directly made `RegionAt` disagree with the patched original. Rebirth also overwrites data the original reads at
run time (00742B08, the clock's x centre, 800 in the exe, becomes its widescreen layout width): read such constants from
memory. Rebirth-patched functions can only be hooked in the vanilla install; the Rebirth smoke run is the check.

## 2. Interface manager and window base (`ui_interface.cpp`, `ui_window.cpp`; `verify_ui.cpp`)

Facts the headers do not give: `CInterface3D` (Create 005F4990) keeps the stack of active windows in an STLport vector (top receives
input; activation 005F7350 / 005F71C0); `CUiWindow` (ctor 005FE700, dtor 005FE930, vtables 00740F60 / 00740F2C at +0x94) starts with a
0x54-byte CTransformObject kept opaque and holds five aligned pointer arrays (+0xF8 referenced engine objects, +0x108 scene objects (both
released through CEngineObject slot 4), +0x118 surfaces, +0x128 regions, +0x138 models). `UiPtrArray<T>`: storage aligned to 32 bytes
(operator new of `n*4 + 0x24`, raw pointer at `data[-1]`; 00600A80), growth by `growBy` or `size / 8` clamped to [4, 1024]; the original
inlines Append / Remove at every call site and has five identical out-of-line SetSize copies. `CUiRegion` is a rectangle or a convex
polygon (enabled from 3 vertices) tested edge by edge with integer cross products; tooltip states 0 idle, 1 hovered, 2 appearing, 3
disappearing, 4 shown (0065E580, Rebirth-patched, advances them). The scale helpers (`__thiscall`, RET 4) leave `0x1B4E81B5 * product`
(the division-by-600 magic multiply) in EAX ("to screen") or the last result ("from screen"); ours return the same.

| Address | Name | Verify |
|---|---|---|
| 005F7CA0, 005F7D20, 005F7DA0, 005F7E00, 005F7E30 | `CInterface3D::ScaleRectToScreen`, `ScalePointToScreen`, `ScaleRectFromScreen`, `ScalePointFromScreen` (+Copy) | pure, original on a scratch rect |
| 005F7590, 005F74E0, 005F2EA0, 005F2EE0 | `GetActiveWindow`, `SetPendingValue`, `UiRecordTable::SpellAt` / `ModifierAt` | pure; SetPendingValue on a 0x50-byte copy |
| 005FD3A0, 005FE400, 005FC450 | `CUiSurface::SetRect`, `MoveTo`, `CUiModel::SetScreenPosition` | original on a copy |
| 005FC8F0, 005FC660, 005FFCC0, 005FFD30 | `CUiRegion::HitTest`, `HasActiveTip`, `CUiWindow::RegionAt`, `ContainsPoint` | pure |
| 005FC9E0, 005FCA40, 005FCA70, 005FC880, 005FEBF0, 006002A0 | `CUiRegion::SetRect`, `ClearShape`, `AddPoint`, `OnMouseMove`, `CUiWindow::UpdateRegionTips` (RET 4), `SetEnabled` | ours only, fields |
| 005FEDD0-005FF7D0, 005FFA90-005FFC50, 005FF830-005FFA20, 005FFD80, 005FFF40 | `AddObject`, `RemoveObject(At)`, `RemoveSceneObject(At)` (returns 1/0), `ClearSceneObjects`, `AddSurface` (returns its argument), `RemoveSurfaceAt`, `ClearSurfaces`, `AddRegion`, `RemoveRegionAt`, `ClearRegions`, `AddModel` (0 on allocation failure), `ClearModels` | ours only, array contents |
| 006003D0, 006004E0, 00600730, 00600840, 00600950 | `UiPtrArray<T>::SetSize` (five identical copies) | ours only, size / capacity rule / kept prefix |

Verify style: array mutators snapshot the array and check the original's rules (append keeps the prefix and sets the capacity the original
computes; removal shifts the tail down without touching the capacity; clears leave a null array with size and capacity 0; SetSize keeps old
storage when it fits, else `max(newSize, capacity + step)`); region / tooltip: 0065E580 runs its own state machine (0 and 4 stay, 1 may go
to 0 or 2, 2 to 4, 3 to 0), so the wrapper checks the final state is reachable from ours. `SetScreenPosition` computes `(x * 0.0025f - 1) *
0.48157f * depth` with each step stored as float (`float_control(precise, on)`: /fp:fast folds the two constants). Original bugs kept:
`AddRegion` leaves the new region's bounds uninitialised; the array append has a dead branch for `size == -1` that frees the storage and stores
through the null array; removing by index does not check it (past the end the original's `REP MOVSD` count goes negative and faults; ours
skips the move). Not rewritten: STLport / MFC template code (005F3000-005F4990, 005F9810-005FC010, 0060A320-0060BD70, 00613400-00613DF0,
00619420-0061B220, 0062A390, ...), region tooltip creation 005FC730 / 005FC6B0 ("tip_%d" text lookup, 0x184 bytes).

### Window stack, screen construction, window base methods (all ours only unless noted)

| Address | Name | Notes |
|---|---|---|
| 005F5BA0 | `CInterface3D::CreateScreen` (`ui_create_screen.cpp`) | the original inlines the constructors of screens 1, 2, 3, 7; id 0 does nothing, 6 and ids above 9 reach the activation with the id as the window pointer |
| 005F71C0, 005F7350 | `Activate`, `PushWindow` | window stack (005FA9A0 when full), backdrops +0x54 / +0x58, +0x50, +0x60 (the server flag kept for the first window), modifier flags, five render states (COM SetRenderState), slot 25 Create, message pump |
| 005F78C0 | `CInterface3D::OnKeyDown` (`ui_interface_keys.cpp`) | scans 0x1D / 0x11D Ctrl, 0x2A / 0x36 Shift, 0x38 / 0x138 Alt |
| 005FE700, 005FE930, 005FF460, 005FFFB0, 00600D30, 006005F0 | `CUiWindow::Construct`, `DestroyBase` (frees the arrays with SetSize(0), so the array destructors after it find nothing), `ReplaceSceneObject`, `CopyImage` (side by side on the written rectangle), `CUiContainer::AddChild`, `UiPtrArray::SetAtGrow` | |
| 005F67A0 / 005F6920, 005F6240, 005F6470 / 005F6620, 005F6F30 / 005F7010, 005F6D50 / 005F6E40 | constructor / destructor body of `CGlobalMap`, `CCampPages::Construct`, item screens' and the camp's destructor bodies, screens 9 and 8 | the STLport containers' empty compare / allocator objects are passed by address in the original (argument slots, stack bytes): where a constructor keeps the byte, ours passes the same value |
| 005F86F0, 005F8A80, 005F8C90, 005F8420, 005EADD0 | `OpenHelp`, `ShowMessage`, `MessageText` (side by side on the string), `LoadState`, the battle camera's key input (`ui_camera_keys.cpp`; side by side on the camera's motion, view and saved-view bytes and the input object) | |

Not rewritten: 005F6BA0 (briefing screen's destructor body), 005E3230.

### Container input dispatch (`ui_container.cpp`; `verify_ui_container.cpp`; scenario `ui_container_input`)

The window procedure (00457970) maps messages to the top window's input slots (WM_LBUTTONDOWN 2 (005F76A0), WM_RBUTTONDOWN 5 sets +0x80,
WM_LBUTTONDBLCLK 7 sets +0x7F, WM_KEYDOWN 9 (005F78C0), WM_CHAR 10 (005F7B40, unless the console or a multiplayer chat takes it), WM_KEYUP 11
(005F7BE0), WM_MOUSEWHEEL 12 (005F7880)). The container's dispatchers (`UiContainerInput`, input vtable 00740FD4): 00601110 (slot 2), 006011F0
(3), 006012D0 (4), 006013B0 (5), 00601490 (6), 00601570 (7), 00601650 (8), 00601820 (9), 006018E0 (10), 006019A0 (11), 00601730 (12, RET 0x10,
returns 1), 00601A60 (1, plain RET): identical apart from the slot; the original returns no value except the wheel's `MOV EAX,1`. Dispatch: the
container's handled flag cleared; each extra input side (+0x158..+0x15C) gets its flag cleared and the event, and one that sets its flag ends
it; then each enabled child (+0x148; mouse and wheel events also need 005FFD30 to hold the point) the same; the child and list end are re-read
after each call; slot 1 calls every side and child and touches no flag. Verify (`DispatchCheck`, ours only): every flag is set to a marker 0x5A
before the call and must be left exactly on those the dispatcher skipped; a handler that changed the lists makes the check skip.

## 3. Interface base: frame update, text layout, console window, cursor (005E0000-00610000)

Files: `ui_interface_frame.cpp`, `ui_text.cpp`, `ui_console_window.cpp`, `ui_cursor*.cpp`, `ui_gdi.h` (the game's MFC CDC /
CPen as the interface uses them); wrappers `verify_ui_base.cpp`. Not rewritten in this range: 005E18E0
`CFlyingHitPoints::Draw` (float-heavy vertex building; cannot be compared without faking the clock and the device),
005E2A30 / 005E3230 / 005E3700 (chat window; multiplayer only, 005E3700 Rebirth-patched), 005F8340 (text extent,
Rebirth-patched), 005F7B40, the sprite draw 005E6D90 (the sprite shape builders 005E8450 / 005E8C60 / 005E91F0 are done: section 9).

| Address | Name | Verify |
|---|---|---|
| 005F57C0 | `CInterface3D::Update` (every frame) | ours only: the mouse copy (+0x68 / +0x6C from 007C7C50) |
| 005FDFA0 | `CUiSurface::DrawWrappedText` | side by side: line count and the surface's pixels through GDI (rule below) |
| 005E4A10, 005E4800 | `ConsoleWindow::DrawInput`, `DrawLines` | side by side: caret / anchor clamp and the surface's pixels (DirectDraw lock; before ours the pixels are put back with the input strip's rows inverted so anything ours leaves undrawn shows); DrawLines runs on every console message |
| 005E4180, 005E3FD0, 005E44A0 | `ConsoleWindow::OnKeyDown`, `Show`, `OnChar` (Rebirth's ImproveConsole behind its flag) | side by side: input text, caret, anchor, command index, scroll, Ctrl / Shift; ours only: flag, surface exactly while shown |
| 005E54A0, 005E5690, 005E5C40, 005E4EB0 | cursor frame draw, cursor thread, `CCursor::Blit`, image construct | renderer.md (cursor thread and switch-over); Blit side by side on the target rect holding the device lock |

`Update`: a pending value (+0x4C) pops the top window (input slot 1, slot 26, deleting destructor); 0x990001 sets close-all
and leaves the game (0057ECA0, 00581660); then +0x81 polls slot 35, close-all pops every window (005F8C50), a queued
window (+0x40) is activated (005F71C0), the mouse is copied, a network client that lost the server (+0x88) gets the
"disconnect" message box (00634650, 0x990000), else the top window's slot 24 runs, last the root's (+0x1C). Console window
fields are in console.md (+0x78 Ctrl, +0x79 Shift).

`DrawWrappedText` walks text + "\n" with the game's `_ismbblead` / `isleadbyte` / `_mbsinc`; 005F8340 takes a CString by
value, which a naked forwarder cannot declare (C3068): the data pointer is handed over and the local detached
(`TextMetricsAt::TextExtent`). The original keeps no state between calls (measuring uses a screen DC GetDC(0079BC28) and
the font from CInterface3D +0x70 + font*4, scaled to 800x600 by integer maths; 005FDE20 scales the rect by 005F7CA0 and
the height back by 005F7E30). It stores a word that did not fit before a '\n' on a line of its own (even an empty one).

**Rule (GDI text pixel checks).** A rare `DrawWrappedText` pixel mismatch (a short Cyrillic word, two runs at the same
millisecond) was an outside event on the shared full-screen dgVoodoo desktop, not an input. The check counts differing
pixels, logs their bounding box and on a difference redraws the original from the recorded pixels until two draws in a
row agree (`ei::verify::SettleOriginal`, `tests/verify/pixel_settle.h`: up to 5 tries, 10 ms * try apart, 150 ms total),
then compares ours with that settled draw; if the original never settles the pixels are not compared. The item panel's
`CompareDrawing` uses the same helper; these are the only wrappers that compare surface pixels.

Quirks kept: the console's Up key increases the command index, Down stops at -1; Delete with no selection at the end of
the input does nothing. Harness: key names `grave`, `backspace`, `delete`, `home`, `end`, `pgup`, `pgdn`, `enterchar` (Enter
plus its WM_CHAR, which the console's character input needs); scenario `console.txt` drives the console window in game.

## 4. Main menu, dialogs, escape menu, credits

Files: `ui_layouts.cpp`, `ui_menus.cpp`, `ui_screens.cpp`; wrappers `verify_ui_screens.cpp`, `verify_ui_menus.cpp`;
scenarios `ui_main_menu` (regions 3, 1, 0, 4) and `ui_screens` (main menu region 5, the hero screen's list keys and
rotate buttons, Esc back; then a new game and the escape menu: hover, resume, Esc, the quit box cancelled with Esc; uses the
harness steps `clickregion` / `moveregion`). The harness's `newgame` bypasses the difficulty question.

- **Main menu** (click 00632D70, regions from the `MainMenu` layout in `menus.reg`): 0 new game (difficulty question: message
  box 0x184 bytes, "option_difficulty"), 1 load (the save / load window as a load window), 2 exit ("Movies\Exit.bik" through
  005DB3F0, then 00455F20(0)), 3 options, 4 credits, 5 multiplayer hero screen. The original passes uninitialised stack
  bytes as the containers' allocators; ours pass 0.
- **Esc menu**: opened by the key-down handlers 006034A0, 00630580, 0063A560 (Esc in the menu resumes); rectangles from
  `menus.reg` in `Res\menus.res` (loaded by 005ACE80 into 007C8048): 0 save, 1 load, 2 resume, 3 exit to main menu (asks
  `quit_msg`; Enter = 0x10001 leaves the game, Esc = 0x10002), 4 options. The click constructs the save/load (0x194, vtables
  00741388 / 00741354) and options (0x418, 00741FAC / 00741F78) windows inline (container constructor 00600B20) and pushes
  them with 005F7350; save opens with +0x178 set, load without; both slide in. Builders: textures 005FEF20 / 005FF0B0, sprites
  005FF3C0 + 005E70E0 / 005E7D60, regions with tooltips 005FC730 (by id) / 005FC6B0 (by text), surface 005FCC00, scroll bar
  0065AEA0, model 005FC1F0.
- **Message box** (constructor 00634650; flag 4 adds an edit field, 0065C010): OK + cancel at 263 / 463, OK alone or cancel
  alone at 363 (cancel alone adds an empty region 0 first, so cancel is region 1 in both layouts). The difficulty question
  (vtables 0074225C / 00742228) has its own Create 006337B0 and button handler 00633DD0 (0 OK, 1 cancel, 2 / 3 the choice;
  copies +0x180 to 0079BEC0 on OK).
- **Titles / credits** (0062B7D0, slot 25 of 00740C3C): from a game (+0x1F4 0) it steps the loading screen (006B71C0 with 1)
  and stops the music, from the menu (1) it clears the server flag at the end; plays the movies under "Titlesfin"
  (00458270), switches font smoothing off for the session (the original passes the address of +0x16C as uiParam with a null
  pvParam, so nothing is read and smoothing is not restored: kept), parses "credits.scr" from "<0079BC94>outro.res" (one
  TitleLine per line) and starts "Movies\Titles.bik".

| Address | Name | Verify |
|---|---|---|
| 00632D70 | `CMainMenu::OnLButtonDown` (slot 2) | ours only: the window the region opens is on top with its vtable (0074225C, 00741388, 00741FAC, 00740C3C, 0074214C); other points open nothing; region 2 not checked |
| 00634790, 006337B0, 0062B7D0 | `CMessageBox::Build`, `CDifficultyBox::Build`, `CCreditsScreen::Build` (slot 25) | ours only: surface / sprite / region counts, the edit field as a child with flag 4; easy_ 0; +2 surfaces kept in +0x164 / +0x168, the script's lines appended |
| 0062AB20, 0062AE90 | `CEscMenu::Build` (slot 25), `OnLButtonDown` | ours: one model and five regions added; the window opened on top (vtable, save flag), resume's pending value |
| 0062AD10, 0062B300, 0062ACC0 | `CEscMenu::OnMouseMove`, `OnKeyDown`, `OnMessageResult` (slot 27) | ours: hovered region; Esc sets handled and pending value 0; pending value / leave-game flag |
| 0062BFE0, 0062C010, 0062D090 | `CCreditsScreen::OnLButtonDown` / `OnKeyDown`, `CExchangeScreen::OnKeyDown` | ours: closed (pending 0xCCCCCCCC or the leave-game flag); handled for Backspace / Enter / Esc |

Quirks kept: the Esc menu click stores the save / options flags through the new window without checking the allocation and
activates a null quit box (FatalError) when that allocation fails; the main menu's load and options windows and the message
box's edit field are written without checking the allocation; the message box with neither button flag shows a cancel button
behind an empty region 0; the difficulty question writes its tip text even when "tip option_difficulty" is missing. Lessons:
`CUiWindow::AddSurface` (005FFA90) must return its argument (the titles' Create freezes the new surface through that result:
a crash in 005FD760 reading 0000000D); a hero record keeps a clone of a weapon put on (006608D0 copies the item), so checks
must compare counts, not identity.

## 5. Camp, global map, hero screens and the camp's panels

Files: `ui_screens.h/.cpp`, `ui_camp.h` / `ui_camp.cpp`, `ui_global_map.cpp`, `ui_hero_panels.cpp`, `ui_item_strip.cpp`, `ui_trade_panel.cpp`,
`ui_hero_screen_click.cpp`; wrappers `verify_ui_screens.cpp`, `verify_ui_camp.cpp`, `verify_ui_item_strip.cpp`, `verify_ui_trade_panel.cpp`,
`verify_ui_hero_screen_click.cpp`; scenarios `ui_screens`, `ui_camp`, `ui_describe`, `ui_hero_screen`. Rebirth-patched here: 00613EA0, 00615D30,
0061B2C0, 0061D780, 0060C870, 0062F950 (callees 005E70E0, 005E7680, 005E6FA0, 005E7D60, 005FDE20, 005FC9E0, the panel builders 0063BAB0 ...
00648520 and the price slot 54 0060FAB0 are called through their original address or the vtable).

**Reaching them.** `screen 5` opens the camp as the global map's camp button does ("constr.current" 0, no constructor); regions 5-10 and 4 ask for
pages 1-6 and 0 (2-6 are not offered and fall back), regions 1 / 0 the hero arrows, 2 / 3 turn the hero; Esc (page 7) leaves; `screen 4` opens the
global map (regions 0 / 1 turn it, Esc opens the escape menu over it). Constructor pages need a constructor record, which only a base's dialogue
("constrN" answers, 006064E0 -> 00578560(N) -> screen 5) or `screen 5 <N>` provides; setting "i.onspellconstr" / "i.onitemconstr" without one
crashes the original too (00621377 in `RebuildList` reads 0x44: the lists are at `+0x1C8 + 0x40` / `+0x1B4 + 0x40`, the record's +0xAC / +0x3C,
which stay 0). Not reached by a scenario: the constructor pages' Builds (006270A0, 006280C0, 00628CC0, 006279E0), CanMakeSpell / CanMakeItem,
probably CanEquip, hero creation (00617A50).

| Address | Name | Verify |
|---|---|---|
| 0062D350, 0062F050 | `CGlobalMap::Create` (slot 25, 6.7 KB), `Update` | ours: marker / sprite / region counts, links point at zones; bob time, w >= 0 after a turn |
| 00631110, 00631030, 00630F50 | `CGlobalMap::AllodTitle`, `ZoneTitle`, `QuestTitle` (`ui_global_map_places.cpp`) | side by side: the text (scratch string) |
| 00630E00, 00630CC0 | `CGlobalMap::ZonePosition`, `QuestPosition` | side by side when found (CheckNear); the original leaves uninitialised stack in `out` when map.txt has no entry, ours leaves `out` |
| 00630860, 0062EE90 | `CGlobalMap::SelectZone` (RET 8, flag is a byte), `SetWeather` | side by side: markers' texture, flags, tint, plain colour; display +0x14; weather +4..+0xC |
| 00635250 | `CBattleScreenSetup::Create` (every scenario) | ours: 11 panels in order with vtables and owner, 2 input handlers, hero id |
| 00611930, 006100E0 | `CCampPages::UpdateAction`, `CanEquip` (slot 43, AL) | side by side (ours releases what it made) |
| 0060E9E0, 0060F050, 00612600, 00612290 | `ShowPage`, `Draw` (slot 20), `CanMakeItem`, `CanMakeSpell` | ours: the page's panels exactly enabled, strip slots; Draw returns 1; result iff an item / container is held |
| 0060E6B0, 0060FE40, 0060E3E0, 0060FCA0, 0060F9E0, 0060F980, 0060F6B0, 0060F710 / 0060F7A0, 0060F830 | `Close` (slot 26), `TakeOff`, the trade panel's destructor body, `PutOn`, `RefreshWeapons` (46), `RefreshStats` (47), `Update` (24; the hero preview's turn through `_CIfmod`), `NextHero` / `PreviousHero` (48 / 49), `ShowHero` | ours only |
| 00610AD0, 00610920 | `MoveItem` (slot 42, RET 8), `PutOnFromStrip` (RET 4) | ours only: refused leaves both counts, mode 0 takes off, mode 1 puts a weapon on a hero with room |
| 00623880, 006233C0, 00622DB0, 00622000, 00622A80 | `CHeroStatsPanel::DrawStats`, `ShowStats`, `Build` (slot 18), `CHeroEquipPanel::Build`, `CHeroQuickStrip::Build` | ours: state untouched (GDI text only); region / sprite / text counts |
| 006270A0, 006279E0, 006280C0, 00628CC0 | `CTradeGrid::Build`, `CRepairGrid::Build`, `CConstructorPage::BuildSpellMaker`, `BuildItemMaker` | ours: counts, textures, surface |
| 00625880, 006261A0, 00624F10 | `CTradePanel::BuildLists`, `DrawLists`, `Build` (slot 18) | side by side: the four lists, experience, scroll positions; surface pixels, price sprites' quads; Build ours only |
| 00610020, 006110F0 | `CCampScreen::BestPartySkill` (slot 45), `ShowPageTabs` (slot 44) | pure; ours: the three strips' tabs (only the strips the page uses) |
| 00618FF0, 00619090, 006193C0, 006188E0, 00618980, 00616900, 006177D0, 00617050 | `CMpHeroScreen::ClampHeroSelection`, `ClampVoiceSelection`, `EquipmentWeight` (slot 53, double in ST0), `CanEquip` (43, AL), `OnItemChosen` (42), `GetHero` (51), `OnLButtonUp`, `OnLButtonDown` (slot 2) | ours / pure; OnLButtonDown per region (arrows, mode changes, the inventory's camp on top, voice row, equipment taken back) |
| 00620380, 00620960, 00620F10, 00621010, 00620E10, 006210F0, 006212A0, 00621420, 00621510, 00621530 | `CUiItemStrip::Build` (RET 4), `SetTab`, `OnMouseMove`, `OnLButtonUp`, `OnLButtonDown`, `OnWheel`, `OnLButtonDownForward`, `SetFilter`, `SelectTab`, `CycleTab` | Build side by side with its regions and sprites recorded and removed again; SetTab / MouseMove / LButtonUp original, restore, ours; the rest ours only |
| 006218F0, 006219A0, 00621A80, 006212F0, 00621F30, 00620A50, 00621B50 | `ItemTab`, `SpellTab`, `ArmorTab`, `RebuildList` (slot 26), `ItemAt` (25), `Update` (slot 24: scroll physics), `Draw` (slot 20 of the forwarding strip 0074197C; the camp and the exchange window use it) | pure; ours: the list is the source items of the current tab; ItemAt side by side; Update side by side (scroll fields +0x150..+0x17C put back, floats within 4 ULPs); Draw ours only |

Hazards and facts (the code has the rest):
- **Item strip**: `ItemAt` truncates `(x - 50) * 0.01f + position` with `_ftol` (the sum is not rounded to float first), `SetTab` settles the old tab at
  `_ftol(pending + position)`, releasing an arrow (`SettleScroll`) keeps the fraction of the pending scroll and adds or removes a whole slot when
  the fraction is under half; `Update`'s branch-free FNSTSW selects pick the speed on NaN. Filter 0 puts quick items (0x3006) in tab 2 with weapons,
  armour and spell containers; tab 3 holds class 0x3007, so clicking tab 3 never equips quick items (harness section).
- **Battle screen Create** reads its part of a save (007AFDF8; from 0x47 an int and a string appended to the top panel's text and, on page 0, to the
  message log; from 0x62 +0x18C and +0x188), builds the unit panel, map, clock, movement and top panels inline (window constructor 005FE700, owner
  +0x148) and puts the "pers <name>" texts into the map<int, CString> at +0x1F0.
- **Global map**: positions are `centre + turn(local)` (centre = layout point (400, 280) at depth 10, `(x * 0.0025 - 1) * 0.48157 * depth`, each
  stored; `turn` = spin about z by +0x1FC times a tilt of 0.8 pi about x, the product inlined with its own operand order: `TurnProduct`); markers come
  from the player's variables ("z.<zone>", "q.<zone>.<quest>" = 1 by bit compare with 1.0f, ways out whose "z.<way>" equals 2 by FCOMP: unordered
  counts as equal); arriving from anything but the camp autosaves to "saveb" when 0079BE7C is set; Update draws the weather (00499F10) into texture 4's
  locked surface under the interface's critical section 007AEE78.
- **Camp**: an unoffered page shows the current one again (recursively: the tail runs twice, as in the original); constructor pages need +0x195 (spell)
  / +0x196 (item), their second pages also `!+0x254` ("i.noconstr"); money below the cost disables an action button. MoveItem: slot 43 (CanEquip) first,
  refused = the cancel sound; moves go through 005EE1B0 (it reads the interface's Shift flag +0x7D itself); items whose record +0x10 flag is set (made by a
  constructor) go back to the constructor's parts list and strip (+0x1EC / +0x1F0), the others to the local player (00578BA0) and the item strip (+0x1E0).
  A hero record keeps a clone of a weapon put on (006608D0 copies the item): compare counts, not identity. Hero skills (`HeroSchoolSkill`,
  `HeroManaUnits`): 00527EB0's result is stored as float then FISTP; the mana maximum (00523F50) is stored as float, divided by the period (00523CD0,
  unrounded) and truncated (`StoreFloat`: under /fp:fast the compiler drops the float store). The sound system (005F7E60) gets the game's own string
  pointers, not copies.
- **Trade panel** (vtable 007417A8; the camp's +0x1F4, the hero screen's +0x1C4): rows of 0x18 bytes (STLport vectors); perk text keys are the record's
  +4 name without trailing "123" plus "0"; held perks are a level byte per group of three skill records (007AF268, 0x44 bytes). **The hero screen's
  rotation state is window +0x1E4 (the turn +0x1E0), not +0x150**: `rotating_` was once declared at +0x150, so the button-up left the real state set and
  the preview kept turning (fixed). Hero screen modes: 9 hero list (rows 9-18; 1 continue 006183B0, 5 new hero, 6 rename, 7 delete "lmp_deletepers", 8 the
  hero's inventory = the camp screen built inline with `fromHeroScreen_` 1), 10 new hero (1 create 006186F0, rows 9-19 the voice), 11 equipment (1 next
  00617A50).

Original quirks kept: the global map's marker array templates have uninitialised numbers (overwritten right after; ours 0); the camp Draw passes hover info
to 005F1590 with uninitialised bytes +4..+0x13 unless a panel fills them (ours zeroes); `CanMakeSpell` keeps the modifiers in an 8-entry stack array
without a bound; `CanEquip` on page 1 reads the container's spell (+0x44) without a null check; `CanMakeItem`'s item type stays unset without a part;
`SpellTab` / `ArmorTab` read the record through null when the record index is out of range; `ClampVoiceSelection` with an empty voice list reads the entry
before the vector; the trade panel's BuildLists pushes copies of a row whose price, level and flag are uninitialised stack bytes (ours 0; a held perk at
the last record reads the next record's name through null), `DrawLists` leaves the third school's row empty, 00626F10 leaves +0x2B and +0x39..+0x3B of
each record unwritten.

Not rewritten: 00612CF0 (camp slot 39, a debug fill; no caller but the vtable), 00626920 (trade panel click), drawing slots 20 of the panels (00622C10,
006284A0, 00628F80, 00627C90; the trade grid draw 00627390 is in `ui_trade_grid_draw.cpp`, renderer.md), 00624A60 / 00624790 (stats update and click),
00611220 / 00611760 (the camp's action buttons; the deal button needs a hero who can level up or the item constructor), 00616560 / 006167C0 (hero screen
voice drawing), 006242E0 / 00624CB0, 0062CF70, 006312C0 (the global map's path search, reached only from the click 0062F950), 00617A50 (creates the hero:
.mp files, records array 007C82A0..007C82AC); the exchange window (0062C250, 0062C9C0) and the lobby screens (0061C430, 0061C700, 0061D470, 0061E2F0 ...,
00618A10, 00616AC0, 006159E0) need a network session, no scenario.

## 6. Battle screen: update, selection, orders, pending actions (00635250-0064BE80)

Files: `ui_battle_setup.cpp` (Create, Update), `ui_battle_update.cpp`, `ui_battle_hover.cpp`, `ui_battle_commands.cpp`,
`ui_battle_mouse.cpp`, `ui_hud.cpp`, `ui_hud_pick.cpp`, `ui_hud_tips.cpp`, `ui_spell_column.cpp`; wrappers
`verify_ui_battle_*.cpp`, `verify_ui_hud_pick.cpp`, `verify_ui_hud_tips.cpp`, `verify_ui_spell_column.cpp`; scenarios `ui_hud`,
`ui_battle_commands`, `ui_battle_spells`, `ui_order_move`, `ui_quick_items`, `ui_quest_notice`. The key handler 0063A560 is
Rebirth-patched (the action table 0079BF70 maps keys to actions: 1 pause, 2 / 3 game speed, 4 objectives, 0x17-0x1A movement
modes, ...; Esc cancels a targeting mode (+0x17C) or opens the Esc menu). None of the functions below is Rebirth-patched;
the Rebirth-patched callees 005E70E0, 005E7680, 005E7D60, 005FDE20, 005F8340, 005FC9E0, 0063BCC0, 0063C050, 0063E4B0, 0063F500,
00638EF0 are called through their original addresses, and so are the rewritten functions covered by wrappers (00637DC0,
006379E0, 00637190, 006373A0, 00637500, 00637750, 0063CEE0, 00639EF0, 0063EC90, 0063FB50, 0063DD70).

| Address | Name | Verify | Scenario |
|---|---|---|---|
| 00638280 | `CBattleScreenSetup::Update` (slot 24, every frame) | ours: quest sound consumed, exit window, selection box marks | every game scenario |
| 0063D2B0, 0063E820, 0063C180, 0063F800 | `CHudQuickItems::SetUnit`, `CHudSpellColumn::SetUnit`, `CHudWeaponBar::SetUnit`, `CHudActionColumn::SetUnit` (RET 8) | side by side: fields, entries, sprites, regions, tooltip lines | every game scenario (via 00637DC0) |
| 0063CEE0, 00639EF0 | `CHudQuickItems::ItemTip` (RET 8), `CBattleScreen::HoverUnit` (RET 0xC) | side by side (ItemTip into a copy of the caller's CString; HoverUnit: cursor shape and hover unit) | every game scenario (the draw sends a mouse move every frame) |
| 00639D60, 0063B920, 00638B30 | `OnMouseMove` (slot 4, RET 0xC), `UnitName` (RET 8), `OnLButtonUp` (slot 3, RET 0xC) | side by side (cursor shape, handled flag, +0x1C1, box +0x1CC, hover unit; UnitName into a scratch CString); ours only: press on the view consumed | every game scenario; ui_hud, world_client, ui_describe |
| 00637DC0 | `RefreshSelection` | ours only: the panels' unit +0x154 and the unit panel's +0x14C (the original stores the first selected unit before testing: 00637EE5), pending action cleared | every game scenario (slots 28 / 29 / 30, ToggleMember, a box drag, a network party change end with it) |
| 00636F10, 006379E0, 00637190, 006373A0 | `OrderAt` (RET 0x14), `ToggleMember` (RET 4), `AttackUnit`, `OrderDeadUnit` (RET 8) | ours only: members nod (+0x38 1, or 2) with a non-positive delay; exit flag +0x18C / +0x188; markers and flag 0x10 for the toggle | ui_order_move, ui_hud, ui_battle_commands |
| 006366F0, 006369D0 | `OnUnitSpeech` (slot 32, RET 0xC), `OnQuestVariable` (slot 33, RET 8) | ours only: the top panel's text only grows; faces / shake by event; quest line, clock pulse | any unit speaking; ui_quest_notice |
| 00637500, 00637750 | `UseOnUnit` (RET 0xC), `UseAtPoint` (RET 0x14) | ours only: each member the unit's own check allows (CanCastSpell 0052F820 / CanUseQuickItem 0052F8B0) nods; nothing pending afterwards | ui_battle_spells, ui_quick_items |
| 0063DB20, 0063EC90, 0063FB50, 0063DD70 | `CHudQuickItems::OnMouseUp` (RET 0xC), `CHudSpellColumn::Click`, `CHudActionColumn::Select`, `CHudQuickItems::Pick` (RET 4) | OnMouseUp ours only (capture, handled flag, pending mode); the rest side by side: pending action, panels' +0x150, button sprites | ui_battle_spells, ui_quick_items; 0063A560's spell keys call them by address |

How they chain (the code has the details):
- **Selection.** `RefreshSelection` is the hub: a single living selected member (flag 0x10; slot 55 = dead) goes to the weapon
  bar / quick items / spells / actions `SetUnit`s, else they get null; the action column's second argument is "anyone selected"
  (the original's empty case pushes EDX after SETG DL, so only the low byte is meaningful).
- **Button release** (`this` = input side): without a press on the view (+0x1C0, set by 00638A40 when no panel took the press)
  the container's handler 006011F0; a drag (+0x1C1, set by the mouse move 00639D60 past the threshold option 0079BDB0; 0 or
  less = no boxes) with the units in the box (+0x1CC, 005656A0) matched against the party clears the world's selection
  (00566100(0)), resets markers and faces and re-marks and controls (00566030) each unit inside, then slot 32; otherwise
  00565E40 (world, the point) gives the unit there (kept only with flag 0x400000) and SelectUnit (00638EF0) gets it.
  Every drag case ends with 00637DC0.
- **Pending action** (every HUD button that starts or drops one does the same, `CBattleScreen::SetPendingAction`): +0x180 slot,
  +0x17C mode, +0x184 item, the quick item panel's eight highlights off (0063CD20) and +0x150 -1, the spell and action columns'
  buttons off (0063E750 / 0063F730) and +0x150 -1, then the slot's button highlighted in the panel of the mode (1, 2 spells; 3-6
  quick items; 7, 8 actions); a cancel is (0, -1, -1). Modes: 1 / 2 a spell on a unit / at a point (spell data flag 0x10000000 or
  not), 3 / 4 a quick item's spell likewise, 6 a player's item, 7 follow, 8 an order on an object (the science button). A
  highlighted button clicked again cancels with a sound; only a spell item with its data (+0x44) sets the action; only wand
  (category 5) and scroll (8) quick items go on. SelectUnit passes the Ctrl byte as `quick` for a spell (mode 1) and 1 for a quick
  item (mode 3); Rebirth's FixSpellSelfCasting passes 0 for the spell. `UseAtPoint` moves the point by -AxisY(unit +0x34) * 0.1 for
  the member that is `unit` (the original changes its own argument slots, so the moved point stays for later members).
- Spell kinds come from `spells.sdb` in `Res\database.res` (tag 0x0B at prototype +0x30): 'u' (flag 0x10000000, mode 1, a unit
  target: arrow, lightning, acid_ray, the protections, healing, charm, ...), 't' (0x20000000, mode 2, a point: fireball, inv_lit,
  acid_column, firewall, litnwall, acid_fog, fireworks, clairvoyence, teleport, campfire), 's' shapechange. **Open:**
  ui_battle_spells' firewall and fireball should give mode 2, yet its runs show `UseOnUnit` twice and `UseAtPoint` never (the
  column's button order or which click cancelled is unknown: `SetUnit` leaves the last button highlighted, so the first click on
  it cancels).
- **Hazards kept**: a wand or scroll without a spell becomes the pending slot with mode 0 and no highlight; `UnitName` does not
  test the prototype for null, the mouse move not the unit panel; the weapon bar keeps one tip CString for all weapons
  (004E0150 overwrites it), the quick item tip passes one CString as description and kind; the spell column sets the tooltip of
  region `index` for spell slot `index` (a gap shifts them); the action column's builder is a loop of one pass over three buttons;
  HoverUnit passes a dead unit with flag 0x400000 on to the action checks; Update passes the portraits a dword whose low byte only
  is set; AttackUnit's order kind is the index of the first set byte of +0x18D..+0x192 (6 for none); ToggleMember's markers get 0x10
  (not 1) for members that stay selected with +0x7D and a negative index reaches its array write unchecked; the quest notice posts a
  bare colour-code line for "q." names that are neither three nor four parts. **Tooltips for regions without a button** get
  SetTip(0, 1, 0), a fresh 0x184-byte tooltip with the empty text 0079B298 whose constructor 0065E320 writes the arguments
  (+0x164 / +0x168) and size (+0x174 / +0x178) only for a non-empty text: those bytes are leftover heap, so the wrappers compare
  them only for tips with lines. Values compared by FCOMP / `TEST AH,0x40` treat NaN as equal (damage "Unknown" when all weights
  are zero; quest values 2 and 3).
- Spell buttons: 0063E4B0 puts the newest spell's region at (760, 470)-(800, 510) and moves older ones 40 up;
  `CHudSpellColumn::Click` (0063EC90) sets +0x180 the index and +0x17C 1 when the spell data's +0x3C has 0x10000000, else 2. Action
  column regions are 0..40 x (470 - 40 i)..(510 - 40 i); with a selected unit SetUnit adds "skill0011" (action -1) first, then
  science (action 0).

Not exercised by the scenarios: the battle screen's drag (no drag step in the harness), the order's mode 1 and the toggle with
Shift (the harness cannot hold Shift during a click), the exit flag (no scenario clicks an exit), RefreshSelection with several
units selected, the attack kinds 0..5 and the level-gap speech, the quest notice's sub-objective path, the mouse move's portrait
pick branch (a multiplayer server command). Not rewritten: 0063EF60 (`ClickAlternate`) and 0063E020 (the quick item used at once):
they need the interface's key flag +0x7F during a click, or Ctrl / Alt held during a spell or quick item key; both are
SetPendingAction plus an immediate 00637500 / 00637750 and `CSpell::IsOffensive` (0068B770, Rebirth-patched: call through its
address).

## 7. Battle HUD panels

Files: `ui_hud.cpp` (panel input and per-frame slots), `ui_hud_map.cpp`, `ui_hud_frame.cpp` (clock, top panel),
`ui_hud_portraits.cpp`, `ui_hud_unit_info.cpp`, `ui_hud_panels.cpp` (items_spells.md); declarations in `ui.h` ("Battle screen
and its HUD panels"); wrappers `verify_ui_hud*.cpp`; scenario `ui_hud` (new game, mouse moves over every panel, clicks on weapon
slots, top panel pages and sling, unit panel body parts and sling, map views, movement panel, a portrait; then the Esc menu:
Options -> group -> Cancel, Load -> row -> Cancel, Save -> Esc, Resume; finally the hero must take damage; the wheel over the
map's sling button (in, then out past the limit) and the map; TAB opens the objectives window. Spell, action and quick item
buttons get only mouse moves: a click starts a targeting mode and the Esc that follows would cancel it instead of opening the
menu; nothing saves, loads, quits or plays a movie).

Panel rectangles (800x600 layout; Rebirth moves some): weapon bar 80..240 x 520..600 (4 slots of 40), quick items 520..720 x
500..600 (8 slots 50x50), spell column 760..800 x 190..510, action column 0..40 x 250..510 (6 buttons from y 470 up), portraits
250..550 x 440..600, map 615..800 x 0..165 (region 0 = the sling button 780..800 x 0..30), unit panel 0..185 x 0..225 (body parts
1-4 at 0..20 x 100..220), clock 720..800 x 520..600, movement panel 0..80 x 520..600 (0 inner quarter circle radius 34, 1-4 ring
sectors 35..80), top panel 180..620 x 0..100 (sling 385..415 x 0..20, page buttons 185..215 / 215..245).

| Panel | Handlers (address) | Verify |
|---|---|---|
| `CHudWeaponBar` | OnMouseDown 0063C6D0, OnMouseMove 0063C7D0; OnMouseUp 0063C710, Update 0063C600 (every frame), Draw 0063C4F0 | side by side; ours only (Draw: returns 1, alpha states restored) |
| `CHudQuickItems` | OnMouseDown 0063DAA0, OnDoubleClick 0063DAE0, OnMouseMove 0063E270; Update 0063D9D0 | side by side; ours only |
| quick items / spell column / action column Draw | 0063CDC0, 0063F190, 0063FD50 (identical) | ours only: returns 1 |
| `CHudSpellColumn`, `CHudActionColumn` | OnMouseDown 0063F1E0 / 0063FDA0, OnDoubleClick 0063F220, OnMouseMove 0063F2D0 / 0063FE40; OnMouseUp 0063F260 / 0063FDE0 | side by side; ours only |
| `CHudPortraits` | OnMouseDown 00641CE0, OnMouseUp 00641E20, OnMouseMove 00641EA0 | ours only |
| `CHudMap` | OnMouseDown 00645060; OnMouseMove 00645610, OnWheel 00645640 | ours only; side by side |
| `CHudUnitPanel` | OnMouseDown 00648330, OnMouseMove 006484C0, OnWheel 006484F0; OnMouseUp 00648370 | side by side (wheel also the result); ours only |
| `CHudClock`, `CHudMovePanel`, `CHudTopPanel` | OnMouseDown 00648D30 / 00649630 / 0064A1C0, OnMouseMove 00649160 / 00649750 / 0064A2C0, OnWheel 0064A2F0; OnMouseUp 00649670 / 0064A200, MovePanel Update 00649550 | side by side; ours only |
| per frame | `CHudMap::BuildMarkers` 00643DC0 (3.4 KB), `UpdateView` 00642E80 (3.9 KB), `CHudPortraits::Update` 00641080 (3.2 KB), `CHudClock::Update` 006488E0, `CHudTopPanel::Update` 00649FF0 | side by side with state put back (mesh arrays byte for byte; slots and the game RNG) |
| `DrawUnitInfo` | 00647040 (from 006462A0, Rebirth-patched; RET 4) | ours only: unit, surface and label table unchanged |

Rebirth-patched (hookable in the vanilla install only; callers here use the original addresses): 00630010, 00636020, 00638EF0,
0063A560, 0063BAB0, 0063BCC0, 0063C050, 0063C800, 0063CAE0, 0063E2A0, 0063E4B0, 0063F300, 0063F500, 00641F20, 00642930, 00642B40,
00645210, 006456E0, 00645F80, 006462A0, 00647FB0, 00648520, 00648D70, 00649190, 00649910, 006567A0, 00659420, 0065C2E0, 0065E580.

Hazards and facts:
- Quick items' slot lookup uses RegionAt's result unchecked, so a point inside the panel but outside every slot (-1) reads the
  dword before the table (+0x168, zero in practice) and counts as a hit (`RegionItem`); the weapon bar's button-up orders
  `LocalView()` without checking for a missing local view; the minimap wheel zooms only over region 0 (RegionAt returns the first
  region containing the point; zoom 2.5^(delta/1200) with `_CIpow`, compared unrounded); hidden minimap entries keep stale
  positions; copies the original makes through FLD / FSTP (bar y values written back unchanged) are skipped; BuildMarkers reads a
  zone exit's target through a copy of the exit record (ours directly: same string); unit 0xA2C2A counts as hostile in zone gz20g.
- Minimap sprites are `CUiMesh` quads (layout in ui.h; 005EA150 hands the arrays to the renderer); `UpdateView` clips the outline in
  place against the world rectangle, pads to a multiple of four with its first point and fans it; the needle angle is FPATAN(-x, -y)
  of the facing.
- **Clock reads its constants from memory**: 00742B08 (x centre, 800 in the exe) is overwritten by Rebirth's widescreen width;
  compiling it in mismatched under Rebirth. Time of day = (ticks 007AF61C * 007AF5D8 + 007AF5D4) mod 24 through `_CIfmod` (inline
  asm, as `WorldTimeOfDay` in script_eval.cpp).
- Portraits (`HudPortraitSlot`, 0x78 bytes): 005FF460 replaces the scene object, so the rewrite re-reads the object array after it;
  three inlined quaternion products each have their own operand order (one function per product); the RNG's double scales (0073C468,
  00742760, 0073C508) are not exact powers of two and are read from the exe; random words are loaded like FILD qword (MSVC's
  unsigned-to-double rounds in its FADD at 24-bit precision, which changed the wobble delay in 4 of 5304 calls).
- Unit info: "consider_more<d>" / "consider_less<d>" are looked up in place (one CString as key and result); a damage type of 7 or
  more keeps the move text (original behaviour); damage values go through `__ftol2_sse` instead of `_ftol` (both truncate).

## 8. Objectives, save/load, options windows and the controls

Files: `ui_objectives.cpp`, `ui_layouts.cpp` (Esc menu, save/load and options Build; also section 4), `ui_menus.cpp` (mouse, draw,
update, scroll bar, tooltip), `ui_menu_keys.cpp`, `ui_scroll_text.cpp`; wrappers `verify_ui_menus.cpp`, `verify_ui_objectives.cpp`,
`verify_ui_menu_keys.cpp`. Rectangles from `menus.reg`: save/load OK 163..237, Cancel 563..637, Delete 363..437 (all y 526..574),
list rows 108..370 x 108.. in 24 steps; options OK 213..287, Cancel 513..587, groups 100..250 x 156.. in 24 steps, rows x 250..520.

| Address | Name | Verify |
|---|---|---|
| 00654660, 006547B0, 006544A0, 006545E0, 00654430, 00654450 | `CSaveLoadWindow::OnMouseDown`, `OnDoubleClick`, `Draw` (slide-in), `Update`, `CloseDialog`, `OnDialogResult` (slot 27; the caller 005F590D ignores EAX) | ours only: selection and pending close value; Draw returns 1; slide ends past 1 |
| 00650C20, 00650D70, 00650920, 00650A20, 006508E0, 00650900, 00650A30 | `COptionsWindow::OnMouseDown`, `OnDoubleClick`, `Draw`, `Redraw`, `OnDialogResult`, `HelpTopic` (pure), `Update` | ours only: pending close, selected row, key wait flag |
| 00653DD0, 006503C0 | `CSaveLoadWindow::Build`, `COptionsWindow::Build` (slot 25) | ours: counts of what is added |
| 006548A0, 00655430, 00657820, 00650E40 | `CSaveLoadWindow::OnKeyDown`, `ShowSelection`, `IsReservedSave` (pure), `COptionsWindow::OnKeyDown` | ours only: Esc leaves pending 0x100001 / 0x110001; selection clamped; caret within the name |
| 0065B090, 0065B360, 0065B240, 0065B340 | `CUiScrollbar::TakePosition`, `Update` (every frame, also the battle screen's message log), `OnMouseDown`, `OnMouseUp` | side by side; Update ours only |
| 0065EA50, 0065EA80, 0065D4B0 | `CUiTooltip::Draw`, `Redraw` (every frame per tooltip), `CUiScrollText::Tokenize` | ours only; Tokenize: original on a stand-in (copy of the token array), appended tokens compared |
| 0064BE80, 0064CC10, 0064DC60 | `CObjectivesWindow::Build` (slot 25), `ShowZone`, `ShowQuest` (`__fastcall`, from RefreshQuests 0064E8B0) | ours: texts, scroll bar, page structure (9 + n regions, 21 + n sprites); the shown quest is the selected one |

Key handlers run only when the window is not sliding in, pass the key to the base handler (00601820) first and set the input
side's handled flag (+8) for the keys they use. In a save window Backspace / Delete / Home / End / Left / Right edit the name (+0x188,
caret +0x190; the first edit of an entry that is not "saveq" / "savea" / "saveb" sets +0x18C); options: while a binding row waits
(+0x3F4) the next key's scan code (flags & 0x1FF) is looked up in the key name table 007857B0 (88 entries of 0x48 bytes), a key
bound to another row asks "key_already_mapped", else CommitBinding (00652000) and RefreshRows. Slide-in drawing eases the window's
model from y 700 to its place (550 / 620 save/load, 480 options): `e = 2s^2` below s = 0.5, else `1 - 2(1-s)^2`, then `(a - 700) * e +
700` stored once as the float argument; `Update` adds `FrameTime() / 0.5` to the slide and compares the unrounded sum with 1;
`CUiScrollbar::Update` adds `FrameTime() * speed * 16` (negated for "up") to a float accumulator (a NaN accumulator loops forever in
both); `FrameTime` (00600270) leaves `ticks * 0.001f` unrounded in ST0, so its forwarder returns `double`. `CUiScrollText` (a
CUiScrollbar subclass; constructor 0065CC00; the battle screen's message log and the quest log) tokenises every text added through
0065CF30 / 0065CFE0 / 0065CC77 into format tokens (0x14 bytes, vtable 00742F28: runs of "\n", BEL 'C' r g b, BEL 'F' digit) and word
tokens (0x10 bytes, vtable 00742F20: the CString and its width from 005F8340) in the CArray at +0x19C (next token from 0065D970, not
rewritten; ours calls 0065E010 SetSize for both where the original inlines the word case).

**Objectives window** (0x1F0 bytes; vtables 007411BC / 00741188, constructor 0063B5D0): TAB (keyboard.ini `obj`) creates it via
0063A560, Esc or Enter closes it. Build shows the zone page for the current zone, or with +0x17C the zones of the zone's allod in
which the player holds quests (variables "q.<zone>.<quest>" = 1; any allod in multiplayer). ShowZone's exits come from the map
database record (+0x4C, 0x3C bytes), not the world's ZoneExit; the player's variables are copied into a local hash_map<CString,
float> (00609C10, 00578B70) and walked in STLport order, freed like the original's inlined destructor. RefreshQuests (0064E8B0 ->
0064DC60) trims sprites and regions to 21 + exits and 9 + exits and adds one of each per quest place, which the wrapper must allow
(a quest with a place, gz1g in a Rebirth run, once mismatched). Scenarios reach Build and the zone page (ui_hud's TAB); the +0x17C
zone list and the distance table (+0x178, a map<int, float> the global map passes) are not reached.

Original quirks kept: options rows are indexed from the region number without a bound; save/load Left can take the caret below 0 and
Right past the name (RefreshList clamps it); options Left / Right do not set the handled flag, a key missing from the key name table
keeps the binding waiting, a failed allocation of the message box (or of a token) is written through as a null pointer; the options
Build appends the 11 group titles without clearing the array first; Tokenize's format token constructor colour (0x1C8DC) and font are
overwritten at once.

Not rewritten: the tutorial / help window (00658000-0065A500, `tutor`, created by 005F86F0; 0065A5E0 pulse needs inline x87), the
controls' constructors and the edit control (0065AEA0, 0065E320, 0065BC30, 0065BCD0, 0065C010, keys 0065C610: no scenario types into
one), the objectives window's key handler 0064E5A0, quest list 0064E8B0, exit selection 0064E990, clicks 0064E330, update 0064E760,
0065F9B0 (CParty load: only when a save is loaded), 00651940 / 0065B8B0 / 00649AF0 (window builders and layouts), 006570E0 (the
autosave: the picture from a window surface, renderer.md), 006592C0, 0065D150, 0065C930, 00667EC0 (multiplayer object creation),
0067CEF0 (CEffectMoshka's per-frame flight: float maths, no scenario creates the birds), 006407E0 / 00640060 (portraits: EI_Plugin patches
them at runtime, Fixes and NPlayers, rebirth.md).

## 9. Item descriptions, item view, sprite shapes, surface helpers (005E0000-00610000)

Files: `ui_item_info.h/.cpp` (description panel), `ui_item_view.cpp` (model view, panel drawing), `ui_item_screen.cpp`,
`ui_mesh_build.h/.cpp` (sprite shapes), `ui_surface_pixels.cpp`, `ui_surface_create.cpp`; wrappers `verify_ui_item_info.cpp`,
`verify_ui_mesh_build.cpp`, `verify_ui_item_strip.cpp`; scenario `ui_describe` (new game, GiveItem of a steel axe, an axe with a
firewall spell, a potion and a rune, `screen 5`; the mouse rests on the statistics rows, body slots, the item strip's "all" tab
(region 7 at 785, 580) with `key ctrl` over the first slots, then page 1; Esc; vanilla 800x600 coordinates, under Rebirth some
rows may be missed, harmless).

| Address | Name | Verify | Scenario |
|---|---|---|---|
| 005F1590, 005EE760, 005F0370, 005EF5D0, 005EFD90, 005F0B90, 005EF300, 005F1130, 005F2910 | `CItemInfoPanel::DescribeStats` (4.9 KB, RET 4), `DescribeItem`, `DescribeBlueprint`, `DescribeCarriedSpell`, `DescribeMaterial`, `DescribeSpellRecord`, `DescribeQuickItem` (category 8), `DescribeModifierRecord`, `ShowHelp` | side by side on the panel surface's pixels (DirectDraw lock outside the functions' own GetDC / ReleaseDC; sub-descriptions run their wrappers nested; rule in section 3) | ui_camp, ui_describe (blueprint, material, spell record, modifier: none) |
| 005ECA10 | `CItemScreen::DrawItem` (RET 8) | side by side: model fields and turning state (an item is drawn twice per frame in the verify build) | ui_camp, ui_screens, ui_describe |
| 005E8450, 005E91F0, 005E8C60, 005E9F30 | `CUiMeshBuilder::Framed`, `Ring` (RET 0x2C), `Disc` (RET 0x24), `ResetColours` | side by side, sprite arrays poisoned with 0xCD and rewritten identically; Commit's +0xC4 (index array pointer) compared as "points at the current indices" (the builders free it with SetSize(0)) | every scenario |
| 005EE5C0, 005EE6C0, 005EE640, 005EE680, 005EE6B0, 005EE440 | `CItemInfoPanel::Draw` (slot 20), `Describe`, `OnKeyDown` / `OnKeyUp`, slot 1, `Build` | ours: returns 1; the list holds one item | ui_describe (`key ctrl`) |
| 005F13A0 | `ItemDamageLists` (RET 4) | side by side on a scratch array; EI_Plugin patches a JMP at +0x61 (config/rebirth_patches.csv), so ours runs in vanilla only | |
| 005FD3D0, 005FD440, 005FE440, 005FD7A0, 005FD240 | `CUiSurface::CaptureScreen`, `Rgb565Copy` (the save game's picture), `Greyscale` (grey = (2r + 4g + b) / 16), `SwapContents` (Freeze's helper), `PixelAt` (RET 8) | side by side on the surface's pixels; SwapContents ours only | ui_screens (the escape menu's Draw runs CaptureScreen, Rgb565Copy(256, 192), Greyscale when it opens) |
| 005FCC00, 005FCDD0, 005FCF80, 005FD120 | `CUiSurface` constructors, picture window (dead), destructor body | ours only | |
| 005ED7B0, 005EC020, 005EC300 | `DrawHeroFace` (RET 8; EI_Plugin's fix at +0xD9 keeps the original under the plugin), `ItemFigureTexture` (256x256 from "<name>.mmp" via converter 007AF06C slot 5; lock without DDLOCK_WAIT, left held if the error report returns 0), `CUiWindow::DrawHeroPreview` (RET 0x14) | side by side on the face model / both textures' pixels; DrawHeroPreview ours only: position / orientation and flag 0x800 (other bits of +0x08 may change) | |
| 005EDD80, 005F2A90, 005F2F20, 005F2F90 | `CItemScreen::CreateBase`, `CItemIconWindow::Build`, `TrimItemModels` / `TrimItemTextures` | ours only | |

Facts and hazards: the camp creates two panels, the description panel (+0x164 at 600, 100) and the help panel (+0x1DC at 0, 100); the
owner (+0x14C) is the item screen and +0x150 an item list holding a copy of the described item (00661DB0). `DescribeItem` dispatch: kind
0x3007 with +0x4C 1 material, 2 with +0x50 0-2 blueprint, 3 spell record, 4 modifier record; quick item category 8 -> 005EF300; a carried
spell (+0x44) while Ctrl is down, or a spell container (0x3008) -> 005EF5D0. Values computed by original functions are truncated straight
off the FPU stack (inline asm with the game's `_ftol`). The item screens keep model (+0x170) and figure texture (+0x180) caches (STLport
map<string, {object, last use}>, limit 30, or 20 with 00784754); each item has a turning state (vtable 007403F8, 0x18 bytes, at the
item's +0x1C) whose glow and angle wrap keep unrounded x87 values (inline asm). The sprite builders use the original's FPU sequence for
circle points and call the out-of-line array copies (005EA380, 005EA600, 005EA210), which have the same growth rule as the inlined ones.
`00700EFE` is `CString::MakeLower` (`_mbslwr`). In the switch-over the surface functions work on game-owned memory without DirectDraw
(renderer.md, "Interface windows").

Quirks kept: `DescribeItem` reads the material record through null when the material index is out of range (and the prototype +0x4C
unchecked on the material line); `DescribeCarriedSpell` without an item or spell calls the surface's End without a Begin;
`DescribeBlueprint` for a kind outside 0-2 uses an uninitialised record (unreachable); `DrawItem` writes the new turning state's count
before checking the allocation; `Ring` writes two vertex records before the array when count is 0.

Not rewritten: the base / briefing screen (id 3, 0x2A4 bytes, vtables 00740578 / 007405AC: Update 00604130 (dialogue state +0x16C: 0 idle,
1 topic choice, 2 dialogue starting, 3 phrases, 4 rewards), Create 006024B0, slot 34 006030C0 (the interface command "BRIEFING <name>" of
005F8070, from client message handler 00667E60), click 006030F0, keys 006034A0, dialogue start 006064E0 ("constrN" opens the camp with
constructor N), script step 00604C50, rewards 00608450, destructor 005F6BA0): needs a "brief" zone (maps bz2g .. bz18h) or an interface
"briefing" command; no scenario does it, and they move NPCs and the camera and play voice files, so they could only be checked "ours
only" (left for a pass that adds a base scenario). 005E6D90 (sprite draw) and the STLport algorithm instantiations (0060BF20 / 0060C070 /
0060C0B0, 0060C390 / 0060C510, 0060A660).

## 10. Harness steps and scenario lessons for the interface

The steps added for UI work (`screen`, `clickregion` / `moveregion`, `wheel`, `dblclick`, `clickunit`, `hudstate`, `campputon`, `zone` /
`waitzone`, key names `ctrl`, `grave`, ... `enterchar`) are documented in the header of `tests/verify/harness.cpp`. Facts behind them:
- **Picking** (`clickunit`): the client world (007AF5B8) keeps a list at +0xF0 (node: next +0, value +8 of 0x18 bytes with the object id at +0xC
  and a screen RECT at +0x10) of the objects drawn this frame (rectangles from projected vertices, renderer.md section 3); 00565E40 (thiscall, RET
  4; the button release 00638B30 calls it with the message's own x, y) gathers entries whose rectangle holds the point (005718B0: PtInRect, then the
  object's +0x94 slot 24 finer test), sorts by depth (005659D0) and returns the first unit (type 0x50); the release then wants flag 0x400000. The step
  finds the unit's rectangle in this frame's list and tries a 9x9 grid inside it with 00565E40 until the game's own pick answers with that unit, so
  the click cannot land on a unit in front; it fails when the unit is off screen or hidden. `ctrl` holds Ctrl (scan 0x1D) around the click
  (SelectUnit's +0x7C branch then orders an attack on any living unit, 00637190); the harness cannot hold Shift or drag.
- **Zone steps**: `zone` calls `LeaveToZone` (0066F310) on the local player's connection (server world 007AFF60, `GetPlayer` 00594180 with player 0)
  as the zone exit handler (00670C20) and the script command 0x8A do; it needs state 1, and in campaign mode 1 or 3 a dead or missing leader loses
  the game instead. `waitzone` waits for no pending game request, campaign mode (007C2CC4) not 2 (menu), the server world's zone name (+0x120, set
  by NewZone 00595480) and state 1; the party appears a little later (Redeploy), so follow it with `waitfor ... HP(GetLeader())`. The exit index is
  stored in the world player's +0xDC (zone +0xD8); Redeploy (00590700, modes 1 and 3) puts the party at the centre of that exit's deploy rectangle
  (exit +0x04..+0x10), facing away from the map centre unless the exit has a `#deployangle` (an index outside the zone's exits leaves it at (0, 0));
  the index counts `#exit` blocks from 0, i.e. map.txt's `#exit n` minus 1. Zones: game `gz1g`..`gz21g`, brief `bz1g`..`bz18h`, edge zones
  `<a>_<b>` (no `#res`, mode 4, the global map's travel; `waitzone` does not recognise them). Some exits are one-way entrances (`none 0`: gz1g's
  #exit 1, gz4g's 5, bz4g's and bz13h's 1, gz11k's 4, gz15h's 2..4, gz20g's); `gz21g` names zonemichel.mpr / .mob which the install lacks (NewZone
  fails; untested); `gz6g` loads its frozen map when `z.gz6g.frozen` is nonzero; no zone needs quest state to load. Not tried: edge zones,
  `endofgame`, leaving a zone other than zone1. Scenarios `zone_gz2g_traps` (party at (67, 15), zone2's traps 1324..1330, MTF4 armed with a wolf),
  `zone_base_bz1g` (mode 3, party at (59.25, 54.25), the base's Smith), `zone_gz3g` (party at (23, 138.5), the Pyramid trap 4515): the first central
  run decides whether their position, trap and Smith checks hold.

Scenario lessons:
- **Race with a zone's intro script.** The server drops a hero with flag 0x800000 (script BlockUnit / IsUnitBlocked) in 0066FFF0 before FindPlayerMode
  005D5E60 / CommandWalk 005D39E0, and gz1g's intro script (SendText0D, SetVariableMax "q.gz1g...", PlayJingle) blocks the hero for about 37 s with its
  first tick unordered against the hero's creation: `ui_order_move` waits for the block (`waitfor 15 IsUnitBlocked(GetLeader())`) before lifting it
  and checks the hero is not blocked before each click (scripted moves ignore the flag). The order path (OrderAt 00636F10 -> 0066A7D0 writes 0x30,
  count, ids, ctrl, point into CPlayerClient +0x5C; 00667810 compresses it and sends it on the loopback link; the server's 0066D270 decompresses it
  into 007C83D0 and dispatches by the first byte through the table built in 0066C9A0, 0x30 -> 0066FFF0) has a wrapper at every rewritten step.
- **Armed traps.** gz2g starts its magic traps armed (its scripts disarm them from triggers), so MTF4 strikes a placed wolf at the next world update:
  the scenario disarms it before placing the wolf, checks the wolf alive, arms it and waits for it to die. The "command 8" mismatches there were a
  verify bug: 004C7C90 adds a script instance at the head of the list (script.md, "Runtime").
- **Quick items.** No script command puts an item in a quick slot (GiveItem adds to the player's list); use `campputon 3006 2` (at most four go on the
  hero: MoveItem -> PutOnFromStrip -> PutOn 0060FCA0); "wand 1".."wand N" are category 5 prototypes (items.idb tag 0x15, e.g. `wand 3.iron[lightning]`;
  potions are category 8); the quick item panel's regions 0-3 are the bottom row from the right (x 670..720 down to 520..570, y 550..600), 4-7 the row
  above; place a target where it is drawn on screen (`QuickWolf3` at (hx, hy - 3) was culled, (hx + 3, hy + 3) works).

## 11. Rules and lessons for interface work

- Rebirth-patched callees (list in `docs/rebirth-patched-functions.txt`) are called through their original address
  (`CInterface3D::CallScale*`, `CUiRegion::CallSetRect`, `CUiMesh::SetRectangle / SetFrame / Place`, 005FDE20 text,
  005F8340 extent, the panel builders, ...); rewritten functions that Rebirth patches itself are hookable in the vanilla
  install only. Read constants from the exe's memory (Rebirth overwrites 00742B08, the layout width).
- Calls from a rewritten function to another rewritten one that a verify wrapper covers go through the original address
  too (`Call*` members, `...At` forwarders), so nested wrappers run.
- Float-heavy per-frame code (clock, top panel, strip scroll, minimap) keeps the original's operation order: values the
  original keeps on the FPU stay `double`, stores to float go through `StoreFloat`, truncations are `_ftol` (the
  compiler may drop a float store under /fp:fast), sums that the original does not round are not rounded
  (`ItemAt`, `SetTab`), unsigned random words are loaded like FILD qword, NaN compares keep FCOMP / `TEST AH` behaviour.
- COM (Direct3D / DirectDraw) device methods are stdcall with the interface pushed first: call them through a stdcall
  helper (a thiscall helper once put the interface in ECX and crashed the camp's hero preview).
- Side-by-side checks of stateful handlers record the fields the original may change (handled flag, capture, panel
  state), put them back, run ours and compare; checks that read GDI / DirectDraw pixels use the settle-and-recompare rule
  (section 3).
- A function that returns its argument or `this` in EAX must too (`AddSurface`: the titles' Create used the result).
- Fields declared at a wrong offset in a header hide bugs for a long time (`CMpHeroScreen::rotating_`): when a state
  seems stuck, check the offset in the handler that sets it against the one that clears it.

