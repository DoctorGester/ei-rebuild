# User interface (005E0000-00670000)

What is rewritten, scenario preconditions, hazards, original bugs kept, what is left. Layouts and per-function behaviour
are in the headers and code (`ui.h`, `ui_camp.h`, `ui_screens.h`, `ui_item_info.h`, `ui_mesh_build.h`, `ui_cursor.h`,
`ui_gdi.h`, `console.h`); every rewritten function carries its address in the source (grep the address), so this file
no longer lists them one by one. Files are in `src/game/`, wrappers in `tests/verify/verify_<file>.cpp`. Verify: **S**
side by side (the original runs first, its state recorded and put back, then ours), **O** ours only (the original
creates, draws or sends; invariants checked), **P** pure. Old section titles in code comments ("Sixteenth pass" etc.)
map to: input and commands -> section 6, camp -> section 5, items -> section 9. The original's quirks (unchecked
allocations, null reads, uninitialised stack bytes that ours zero) are not listed here: the verify wrappers and the
functions' comments carry them, and a side-by-side mismatch on one is the original behaving oddly, not a port bug.

## 1. Conventions shared by every screen

**Layout of the range:** 005E0000-00610000 interface base (manager, windows, surfaces, sprites, cursor, text, console
window, item descriptions); 00610000-00630000 camp / hero / lobby screens; 00630000-00660000 main menu, battle screen and
HUD, objectives, options, save/load, tutorial, controls. Layouts are authored for 800x600; the scale helpers convert with
`x * width / 800`, `y * height / 600` (32-bit wrapping products, truncating division).

**Screen classes.** Each screen is a `CUiWindow` subclass with a vtable pair: the input side at `+0x94` (`{vtable,
CInterface3D*, handled}`; handlers get `this` = window + 0x94 and (buttons or key, x, y) in screen pixels; the rewrites
are window methods registered through `__fastcall` shims that subtract 0x94) and the main vtable 0x34 bytes after it.
Main slots: 19 show/hide children, 20 per-frame draw (returns 1, RET 4), 24 update, 25 Create, 26 on activate, 27
message-box result, 42 and up screen-specific. Input slots: 1 deactivate, 2 left down, 3 left up, 4 mouse move, 5 / 6
right down / up, 7 / 8 double clicks, 9 key down `(vk, repeat, scan|flags)`, 10 char, 11 key up, 12 wheel `(keys,
delta, x, y)`; handlers `RET 0xC` (wheel `RET 0x10`) and return 1 on every path. A button-down handler that takes the
mouse stores its input side in `CInterface3D +0x0C` (`mouseCapture_`); the button-up handler acts only when the capture
is its own and clears it; a handler sets `+0x9C` (`inputHandled_`) when it used the event, which stops the container
passing it on.

**How the screens open.** `CInterface3D::CreateScreen` (005F5BA0) is called by the client's message 7 handler (00669D50)
with the id the server sends: 1 battle screen (Create 00635250), 2 (00740688), 3 briefing / base (007405AC), 4 global
map, 5 camp (005F6240), 7, 8, 9 (id 0 does nothing; 6 and ids above 9 reach the activation with the id as the window
pointer). The client asks for a screen with message 4 (`00578B00(id)` -> 0066B280; sets the client player's +0x20 to
3); the server's handler (00670CE0) accepts it only in state 1 (in the world): it removes the party from the world
(00590A40), moves to state 2 and answers with message 7. Message 5 (`00578B20` -> 0066B2C0, needs client state 3; the
camp's page 7) brings the party back (00670DA0: state 2 -> 1). So the global map's camp button works only where the map
is the world's own screen (a "brief" zone). `LeaveToZone(player, zone, exit)` (0066F310, server) sends message 8; the
client (00669EB0) sets game request kind 4 and the main loop loads `zone` directly (0057C460); the zone's type in map.txt
decides the mode: `game` kind 0 (mode 1, battle screen), `brief` kind 2 (mode 3, a base such as bz1g), `edge` kind 1 (mode
4) (map loader 0045EA20); the global map is not shown on the way. The harness step `screen <id>` does what the global
map's buttons do (for 5 it first sets "constr.current" to 0, as 0062FE60 does) from the battle screen of zone1.

**Keyboard.** Keys come from window messages, not DirectInput. WM_KEYDOWN goes to 005F78C0 (console toggle on scan 0x29,
modifier flags at CInterface3D +0x7C Ctrl / +0x7D Shift / +0x7E Alt, screenshot "shot%.4d.bmp" on scan 0x46), then to the
top window's input slot 9. In-game hotkeys are looked up by scan code in the table at 0079BF70 (`config\keyboard.ini`,
00462C10); a zone has no inventory or map-screen key (`I` is quick item 3, `M` the minimap).

| Range | What (vtable pairs in ui.h) |
|---|---|
| 00601B70-00610000 | briefing / base screen (id 3, `CBriefingScreen`) |
| 00610020-00612CF0 | camp screen (007408E8 / 0074091C, 0x260 bytes, Create 0060C870 Rebirth-patched) |
| 00613EA0-006195B0 | multiplayer hero screen (`lmp_*`, 00742118 / 0074214C, 0x220 bytes; 00613EA0 and 00615D30 Rebirth-patched) |
| 0061B2C0-00620300 | multiplayer connection screen (`config\addrbook.ini`; 00741C9C / 00741CD0; 0061B2C0, 0061D780 Rebirth-patched), another lobby window from 0061E2F0 |
| 00620380-00621F30 | camp item strip (007419E8 / 00741A1C; forwarding subclass 00741948 / 0074197C) |
| 00622000-00629AF0 | hero panels shared by camp and hero screen, camp pages: trade, repair, spell and item constructors (00741504 group) |
| 0062A830-0062D330 | escape menu (00741270 / 007412A4), credits 0062B7D0 (00740C08 / 00740C3C), exchange between players from 0062C250 |
| 0062D350-00632000 | global map (00740B08 / 00740B3C, 0x294 bytes; 0062F950 Rebirth-patched), zone screens from 00630000 |
| 00632000-00635250 | main menu (00632940, 006326B0, 00632D70), new-game dialog (006337B0), message box (00634650) |
| 00635250-0064BE80 | battle screen (`battle00`; keys 0063A560 Rebirth-patched), panels, quest / zone messages, portraits, unit info |
| 0064BE80-0065A500 | objectives window (0063B5D0), options, save/load; tutorial / help window from 00658000 (created by 005F86F0) |
| 0065A500-00660000 | controls: scroll bar 0065AEA0, spin / edit 0065BC30 / 0065BCD0 / 0065C010, scrolling text 0065CC00, tooltip 0065E320, DirectInput mouse 0065EAD0, RichEdit 0065ED30, party bar 0065F2D0 |

**Rebirth** (WideScreen, rebirth.md) patches the five scaling helpers, the sprite placement (005E6FA0, 005E70E0, 005E7D60,
005E7680, 005E99D0), CUiRegion::SetRect / AddPoint, 005FDE20 (text), 005F8340 (text extent), many screen builders and key
handlers, and the constant 00742B08 (the clock's x centre, 800): callers go through the original addresses (rule 9).
The Rebirth-patched functions are the VANILLA bodies in our code: the installer hooks them in the vanilla launch only,
rewritten callers keep their `EI_ORIGINAL` forwarders, and none of Rebirth's patches is reproduced.

## 2. Interface manager and window base (`ui_interface.cpp`, `ui_window.cpp`)

`CInterface3D` (Create 005F4990; constructor 005F46A0 is a static initialiser, not ported) keeps the active windows in an
STLport vector (top receives input). `CUiWindow` (ctor 005FE700, dtor 005FE930, vtables 00740F60 / 00740F2C) starts with a
0x54-byte CTransformObject kept opaque and holds five aligned pointer arrays (+0xF8 referenced engine objects, +0x108
scene objects (both released through slot 4), +0x118 surfaces, +0x128 regions, +0x138 models). `UiPtrArray<T>`: storage
aligned to 32 bytes (operator new of `n*4 + 0x24`, raw pointer at `data[-1]`; 00600A80), growth by `growBy` or `size / 8`
clamped to [4, 1024]; append keeps the prefix, removal shifts the tail (capacity kept), clears leave a null array, SetSize
keeps old storage when it fits, else `max(newSize, capacity + step)`; the original inlines Append / Remove everywhere and
has six identical SetSize copies. `CUiRegion` is a rectangle or a convex polygon (from 3 vertices) tested with integer
cross products; tooltip states 0 idle, 1 hovered, 2 appearing, 3 disappearing, 4 shown (0065E580, Rebirth-patched: 0 and 4
stay, 1 -> 0 or 2, 2 -> 4, 3 -> 0). The scale helpers (`__thiscall`, RET 4) leave `0x1B4E81B5 * product` (the
division-by-600 magic multiply) in EAX ("to screen") or the last result ("from screen"); ours return the same.
`SetScreenPosition` computes `(x * 0.0025f - 1) * 0.48157f * depth` with each step stored as float.

Window stack and construction: `CreateScreen` (`ui_create_screen.cpp`) inlines the constructors of screens 1, 2, 3, 7;
`PushWindow` (005F7350; stack full: 005FA9A0) sets backdrops +0x54 / +0x58, +0x60 the server flag kept for the first
window, five render states, slot 25 and the message pump. `CloseAllPending` uses +0x44 and the screen flag 00784750.
`CUiWindow::LoadTexture` calls the texture manager's loads 006922A0 / 006926F0 by address; `CreateTexture` uses flag
0x40000. `CUiModel::Draw` animates by timeGetTime / 67; part flag 0x80 = hidden; `Load` takes map object class 0x33 /
id 0x88CA6C00 (ui_layouts.cpp calls `CallLoad`). `FrameTime` is display slot 6 ms * 0.001f, left unrounded (forwarders
return `double`). `CUiRegion::SetTip` formats "tip %d" up to '#'; a missing text shows the key. The per-frame slots'
wrappers (`verify_ui_window_slots.cpp`) use stand-in vtables that record the calls. The STLport containers' allocator
bytes in the screen constructors/destructors are stack bytes; ours passes the same value where a constructor keeps them.

**Deleting destructors** (`ui_window_delete.cpp`, O): slot 0 of all 50 windows in 005E0000-00640000 and the input sides'
37 thunks (`SUB ECX,0x94; JMP`; ours return the window). The plain strip's 0060E280 frees nothing of its own; `CObjectivesWindow` 00603B90 does not free the exit distances map +0x178. `CMasterRequestBox` (00741DD8) is a message box
plus a master server link at +0x180; `MpAddressEntry` is 0x34 bytes; 0062EDA0 is 00499E30's code.

### Window stack, screen construction, window base methods (O unless noted)

`OnKeyDown` / `OnChar` / `OnKeyUp` (`ui_interface_keys.cpp`: char goes to the console, a client's open chat line, then
the top window); mouse input (`ui_interface_input.cpp`: to the capture, else the top window; +0x7F double click, +0x80
right button; S on stand-in vtables). `CUiWindow::CopyImage` (005FFFB0, S), `SaveState`/`LoadState` (005F85C0 S through
a stream recorder, every save), `MessageText` (S), `OpenHelp` (005F86F0), the battle camera's key input
(`ui_camera_keys.cpp`, S). `CUiContainer::RedrawContainer` / `RedrawChildTips` do nothing unless CInterface3D +0x30.

### Container input dispatch (`ui_container.cpp`; scenario `ui_container_input`)

The window procedure (00457970) maps messages to the top window's input slots (WM_LBUTTONDOWN 2, WM_RBUTTONDOWN 5 sets
+0x80, WM_LBUTTONDBLCLK 7 sets +0x7F, WM_KEYDOWN 9, WM_CHAR 10 unless the console or a multiplayer chat takes it, WM_KEYUP
11, WM_MOUSEWHEEL 12). The container's dispatchers (input vtable 00740FD4: 00601110 (2), 006011F0 (3), 006012D0 (4),
006013B0 (5), 00601490 (6), 00601570 (7), 00601650 (8), 00601820 (9), 006018E0 (10), 006019A0 (11), 00601730 (12, RET
0x10, returns 1), 00601A60 (1)) are identical apart from the slot; no return value except the wheel's. Dispatch: the
container's handled flag cleared; each extra input side (+0x158..+0x15C) gets its flag cleared and the event, one that sets
its flag ends it; then each enabled child (+0x148; mouse and wheel events also need 005FFD30) the same, the child and list
end re-read after each call; slot 1 calls every side and child and touches no flag. Verify (O): flags preset to 0x5A must
stay on the skipped ones.

## 3. Interface base: frame update, text, console window, cursor, chat (005E0000-00610000)

Every scenario reaches the frame, draw, cursor and shutdown code. Not rewritten here: 005E3700 (the chat line's draw) and
005F8340 (text extent), both Rebirth-patched.

- `CInterface3D::Update` (005F57C0): a pending value (+0x4C) pops the top window (input slot 1, slot 26, deleting
  destructor); 0x990001 sets close-all and leaves the game (0057ECA0, 00581660); then +0x81 polls slot 35, close-all pops
  every window (005F8C50), a queued window (+0x40) is activated, the mouse is copied (+0x68 / +0x6C from 007C7C50), a
  network client that lost the server (+0x88) gets the "disconnect" box (00634650, 0x990000), else the top window's slot
  24 runs, last the root's. `CScreenFade::Draw` (005E69C0): bars of height * [00740150]; alpha step `(ms as qword) *
  rate + alpha` stored as float, clamped at 255, below 0 the fade ends. `Shutdown` writes config\tutorial.ini from
  `helpTopics_`.
- `CInterface3D` declares +0x40 (queued window), the fonts +0x70, `messages_` +0x84 (stlp::list of CString),
  `helpTopics_` +0x98 (stlp::map<CString, int>); its root window +0x1C is the `CChatOverlay` (`chatOpen_` +0x14C,
  `caret_` +0x154). `CUiItemPanel` (+0x148 `mode_`, +0x14C `owner_`) is the base of the description panel, hero
  equipment, quick strip, trade / repair grids, constructor pages and the blank panel 00741B58. The hero stats panel has
  its own deleting destructors 0060E340 / 00613DD0.
- Glue (`ui_interface_glue.cpp`): `PlaySound` (Play2D, priority 1000, volume 100; the sound system gets the game's own
  string pointers), `OnQuestVariable` (at 1 the name's third '.' part becomes savedText_), news for the top window
  (slots 28-32, 36, 37 as `CUiWindow::Virtual*`), the bottom window's slot 40 (save game), the chat overlay's AddLine
  (005F82D0 / 005F82F0 / 005F8310 multiplayer only; S via `slot_recorder.h`).
- Cursor (`ui_cursor*.cpp`): `CCursor::Init` / `AddShape`, `ChangeShape` (callers keep `SetShape` through the address),
  `TakeLock` / `ReleaseLock` (mode 2 only), `Start` (thread 005E52A0 unless mode 1), `UpdatePosition` (frames every
  125 ms; +0x08 the DirectInput mouse, +0x98 the thread). Frame draw, thread and image construct (the blend 005E5C40 is not ported):
  renderer.md (R9c2).
- Flying hit points (`ui_flying_hit_points.cpp`): `Draw` (S, timeGetTime held, stand-in renderers replayed), `Add`
  (flag 8 starts 0.5 s later); numbers need option 17 (0079BE74).
- Chat overlay (`ui_chat_overlay.cpp`; multiplayer only, no scenario): `Update` erases lines older than 20 s, `AddLine`
  wraps at 780 px, "^<digit>" colours, at most 12 lines; Enter sends a `net::SayMessage` kept 60 s.
- Camera input (`ui_camera_input.cpp`; S; ui_hud, ui_screens): `OnRightDown` / `OnRightUp` (turning with the cursor
  held), `OnDeactivate`, `OnWheel`.
- Console window: `DrawInput`, `DrawLines`, `OnKeyDown`, `OnChar` (Rebirth's ImproveConsole behind its flag), pixel
  checks S (before ours the input strip's rows are put back inverted so anything left undrawn shows); harness key names
  `grave`, `backspace`, `delete`, `home`, `end`, `pgup`, `pgdn`, `enterchar` (Enter plus its WM_CHAR, which the console
  needs); scenario `console.txt`.
- `CUiItemPanel::OnLButtonDown` (005EE310, input 2 of every item panel; S, ui_camp_deals), `ItemModel` (or a counted 100 x
  100 stand-in sprite).
- Small and multiplayer-only functions (no scenario unless named; `verify_ui_small_slots.cpp`, `verify_ui_small_members.cpp`
  list the scenario per address): `UiInputSide::LeftDoubleAsDown` / `RightDoubleAsDown` (slot 7 / 8 of most input sides;
  `dblclick`), `CCampPages::DebugFill` (slot 39, only the vtable refers to it), `CChatWindow` (base screen,
  `ui_chat_window.cpp`: players vector +0x150 of counted `PlayerMessage`s, front insert = the inlined `insert(begin(),
  x)`, overflow 0060A120), exchange `Create` / `Draw` (children constructed inline; `ui_exchange.cpp`),
  `CMpConnectScreen` (`ui_connect_screen.cpp`, layout in ui_screens.h; helpers call each other by address; slots 26
  0061C2D0 / 41 0061C420 too short to hook), `CMasterRequestBox` (`net::MasterLink` in net_classes.h; the address list's
  `vector<MpAddressEntry>` push / insert default, erase, entry copy / assignment / destructor).

`DrawWrappedText` (005FDFA0) walks text + "\n" with the game's `_ismbblead` / `isleadbyte` / `_mbsinc`; 005F8340 takes a
CString by value, which a naked forwarder cannot declare (C3068): the data pointer is handed over and the local detached
(`TextMetricsAt::TextExtent`). Measuring uses a screen DC GetDC(0079BC28) and the font CInterface3D +0x70 + font*4, scaled to
800x600 by integer maths; a word that did not fit before a '\n' goes on a line of its own (even an empty one).

**Rule (GDI text pixel checks).** Other programs on the shared dgVoodoo desktop can disturb a draw: the check redraws the
original until two draws agree (`ei::verify::SettleOriginal`, `tests/verify/pixel_settle.h`: up to 5 tries, 150 ms) and
compares ours with that (never settled: not compared). Used by DrawWrappedText, the console, the item panel's
`CompareDrawing` and `CTradePanel::DrawLists`, the only wrappers that compare surface pixels.

**A-I22 (2026-10): 005E0000-00640000 finished.** Every hookable game function of the range is ported, in library.csv or in
dead_code.csv; what is left are 31 stubs of 1-3 bytes (plain RET / RET n, too short to hook). Groups verified O with the
state read back, by `verify_ui_small_members.cpp` and friends: the unpatched small members (default slot answers, setters,
constructors, handlers, exchange slots 42 / 43), the text and sprite draws (005FDE20 surface DrawText, 005F8340, 005E3700,
005E6FA0 sprite Place, the shapes 005E70E0 / 005E7680 / 005E7D60 / 005E99D0; 005F8340 and 005E6FA0 S), `CInterface3D::Create`
and `DrawOverlays`, the battle camera / global map mouse moves (005EB210 S, 00630010 S, 0062F950 O), the battle panel builders
(0063BAB0 .. 0063F500, 0063A560 key-down; 0063C050 S), `CCampPages::Create` (0060C870), the hero screen (00613EA0 Create,
00615D30 SetModeVanilla) and the connection screen (0061B2C0 Create, 0061D780 list draw; ui_mp_host reaches the list draw).

CSV: library.csv 12 rows (the chat line list, help topic / cache map nodes, `CFont` / `CRect` inlines, EH-only
destructors), dead_code.csv 0063C010, 0063F6E0. Forwarded on purpose: the Rebirth-patched callees (`CUiMesh::SetRectangle` /
`SetFrame`, `CUiRegion::CallSetRect`, `SurfaceDrawText`, `CHudWeaponBar::Highlight` 0063C050, `CHudMovePanel::ShowMode`
00649910, the list draw 0061D780, `HeroScreenSetModeOriginal` 00615D30, the saved heroes' reload 00662FB0), the library's
sort pieces (0061A240 / 0061A300 over `vector<CString>`, 0061EF60 / 0061F6F0 / 0061FB00 over the address list), 00578D30 /
00578D50 (pause), 006481D0 (unit panel part). Shared: `gamecrt::fmod` (006E95A0), `gamecrt::sscanf` (006EAEEC), originals_ui.h
forwarders 00444CD0, 00462C10, 00444A70, 005F9810, 00578700, 0065C2D0, 005A6C80, 005F7E30, 005FD240, 00615D30. Kept as the
original: the hero screen's Create does not null-check a creature's unit prototype's figure (an unknown model name reads +0xE8 of
null) nor a failed child allocation; its figure lists are sorted with the library's own introsort and the figure prototype is
looked up with the out-of-line `operator[]` 0053C780 (the original inlines it over 0053D340).

## 4. Main menu, dialogs, escape menu, credits

Scenarios `ui_main_menu` (regions 3, 1, 0, 4), `ui_container_input` (Esc on the menu, Esc on the quit box), `ui_screens`
(main menu region 5, the hero screen's list keys and rotate buttons, Esc back; then a new game and the escape menu: hover,
resume, Esc, the quit box cancelled), `ui_view_screen`. The harness's `newgame` bypasses the difficulty question.

- **Main menu** (click 00632D70, regions from the `MainMenu` layout in `menus.reg`; `ui_main_menu.cpp`, a CUiContainer):
  0 new game (difficulty question: message box 0x184 bytes, "option_difficulty"), 1 load, 2 exit ("Movies\Exit.bik"
  through 005DB3F0, then 00455F20(0)), 3 options, 4 credits, 5 multiplayer hero screen. `Update` (00632B10): rain at 0 /
  7 o'clock, night lightning 1 in 41 per 100 ms.
- **Esc menu** (key-down handlers 006034A0, 00630580, 0063A560; Esc in the menu resumes): rectangles from `menus.reg` in
  `Res\menus.res` (loaded by 005ACE80 into 007C8048): 0 save, 1 load, 2 resume, 3 exit to main menu (asks `quit_msg`;
  Enter = 0x10001 leaves the game, Esc = 0x10002), 4 options. The click constructs the save/load (0x194, vtables 00741388 /
  00741354) and options (0x418, 00741FAC / 00741F78) windows inline and pushes them; save opens with +0x178 set, load
  without; both slide in.
- **Message box** (00634650; flag 4 adds an edit field, 0065C010): OK + cancel at 263 / 463, OK alone or cancel alone at 363
  (cancel alone adds an empty region 0 first, so cancel is region 1 in both layouts). The difficulty question (0074225C /
  00742228; Create 006337B0, buttons 00633DD0: 0 OK, 1 cancel, 2 / 3 the choice) copies +0x180 to 0079BEC0 on OK.
- **Credits** (0062B7D0, slot 25 of 00740C3C): from a game (+0x1F4 0) it steps the loading screen (006B71C0 with 1) and stops
  the music, from the menu (1) it clears the server flag at the end; plays the movies under "Titlesfin" (00458270), switches
  font smoothing off for the session, parses "credits.scr" from "<0079BC94>outro.res" (one TitleLine per line) and starts
  "Movies\Titles.bik". `Draw` (0062BD80) takes the next line once the time passes `nextLineAt_` and the text stopped
  (`speed_` 0 or NaN).
- **View screen** (`ui_view_screen.cpp`): screen 7, a debug view (0x168, 00740840 / 0074080C): inert input 00741154,
  `UnitPickInput` (00740278, 0x20: a CDWordArray of picked ids: `OnLeftDown` pick / walk, Ctrl attack (how 6), Space
  toggles the server pause flag), camera keys; focus (30, 30), rotation premultiplied by 225 degrees about x. No unit
  can be picked in ui_view_screen: the party leaves the world with the request.

Lessons:
`CUiWindow::AddSurface` (005FFA90) must return its argument (the titles' Create freezes the surface through it: a crash in
005FD760 reading 0000000D); a hero record keeps a clone of a weapon put on (006608D0 copies the item), so checks compare
counts, not identity.

## 5. Camp, global map, hero screens and the camp's panels

Scenarios `ui_screens`, `ui_camp`, `ui_camp_deals`, `ui_describe`, `ui_hero_screen`, `ui_hero_stats`, `ui_hero_delete`,
`ui_trade_restore`, `ui_trade_hold`. Rebirth-patched here: 00613EA0, 00615D30, 0061B2C0, 0061D780, 0060C870, 0062F950
(callees 005E70E0, 005E7680, 005E6FA0, 005E7D60, 005FDE20, 005FC9E0, the panel builders 0063BAB0 ... 00648520 and the price
slot 54 0060FAB0 go through their original address or the vtable).

**Reaching them.** `screen 5` opens the camp as the global map's camp button does ("constr.current" 0, no constructor);
regions 5-10 and 4 ask for pages 1-6 and 0 (2-6 not offered: fall back), regions 1 / 0 the hero arrows, 2 / 3 turn the hero;
Esc (page 7) leaves; `screen 4` opens the global map (regions 0 / 1 turn it, Esc the escape menu). Constructor pages need a
constructor record, which only a base's dialogue ("constrN" answers, 006064E0 -> 00578560(N) -> screen 5) or `screen 5 <N>`
provides; setting "i.onspellconstr" / "i.onitemconstr" without one crashes the original too (00621377 in `RebuildList` reads
0x44: the lists at `+0x1C8 + 0x40` / `+0x1B4 + 0x40` stay 0). Not reached: CanMakeSpell / CanMakeItem, probably CanEquip,
the constructor / grid Builds, the repair grid's draw, the trade panel's click, DeconstructItem (needs a made item),
FindPaths (needs a zone / link click), CGlobalMap::Close.

Global map (`ui_global_map*.cpp`): `Create` (0062D350, 6.7 KB), `Update` (0062F050), titles (`AllodTitle`, `ZoneTitle`,
`QuestTitle` in `ui_global_map_places.cpp`), `ZonePosition`, `QuestPosition`, `SelectZone` (RET 8, flag is a byte),
`SetWeather` (S). `RefreshSelection` (00630670) draws the zone markers alone in colours 0xFF181818 + 0x101010 i through a
frame step 00458010(0, 0) into the backdrop; the click reads the zone back from the pixel. `FindPaths` (006312C0, S, RET 4)
is a recursive way search; `arrivals_` = stlp::map<int, float> exit -> shortest "#passtime" sum
(`ui_global_map_paths.cpp`). `GlobalMapZonesSetAtGrow` is `CArray<GlobalMapZone, GlobalMapZoneElements>` (elements
005FBF50 / 005FBD30). `OnRightDown` / `OnRightUp` (S) drag (motion 007C7C60), `OnKeyDown` (Esc menu inline, '5' / '6'
quick save / load "saveq"). `CBattleScreen::Create` (00635250) builds 11 panels and 2 input handlers.

Camp pages (`ui_camp_*.cpp`): `OnLButtonDown` / `OnLButtonUp` / `OnKeyDown`, `OnMessageResult` (0x10001 takes the first
weapon back), `HelpTopic` (`ui_camp_input.cpp`; `turning_` +0x25C); `UpdateAction`, `CanEquip` (slot 43, AL), `CanMakeItem`
/ `CanMakeSpell`; trade pages: `DoAction` / `UndoAction` (trade pages stock the constructor's class template (+0x198)
lists 0069908x-006992xx with the sold records; cost_ off except page 1), `DeconstructSpell` ("Prototype" + a "Rune" per
modifier), `DeconstructItem` ("Instruction Weapon / Armor / Quick Item", "material.<key>" built with += instead of two
operator+, a "spell container" through 00613820 instead of the inlined map insert) (`ui_camp_deals.cpp`); `PutOn`,
`TakeOff`, `MoveItem` (42, RET 8), `PutOnFromStrip` (RET 4), `Update` (turn via `_CIfmod`), `BestPartySkill` (45, P),
`ShowPageTabs` (44). Constructor slots `SpellMakerItemAt` / `ItemMakerItemAt` (`ui_constructor_slots.cpp`; pages 3 and 5).

Hero panels, trade panel, strips (`ui_camp_stats.cpp`, `ui_stats_buttons.cpp`, `ui_stats_rows.cpp`, `ui_equip_panels.cpp`,
`ui_panel_draws.cpp`, `ui_hero_panels.cpp`, `ui_trade_panel*.cpp`, `ui_item_strip*.cpp`):
- `CHeroStatsPanel::SetHero` stores +0x170 = height * 100 truncated (an edge height may differ at another precision).
  The panel's page 10 -/+ regions 6-13: a hold starts at -0.5 s with the capture, repeat after 0.25 s (ui_hero_stats,
  coordinates unconfirmed); regions 0 / 1 previous / next hero. `DrawPart` armour "%.1f" = the largest of the first six
  damage types' three-layer sums; a NaN keeps the old maximum. `CHeroEquipPanel::OnRightDown` does not mark the event
  used.
- Draw details: `CRepairGrid::Draw` puts models at depth 2^(3 - row) * 6; the constructor pages' highlight p = (cos(5 t) +
  1) / 2 stored as float, colours truncated and clamped (an integer edge may differ at another precision; ui_camp_deals
  pages 3, 5, 6). Lists are `UiItemList` = `lib::AlignedArray<CItem*>`.
- Trade panel (`ui_trade_panel.cpp`; ui_trade_restore): `SnapshotRecord` copies floats as dwords, `Settle` (the all-branch
  tests the low byte of `keep`), `RestoreRecord` (HealSlots by old hp / old max * new max - Reduce(new max), the maxima
  stored as float). `OnLButtonDown` (`ui_trade_panel_input.cpp`; ui_trade_hold with `ldown` / `lup`, purchases only in
  the Rebirth run): regions 0-3 scroll, 4-9 a school level, 10 science, 11-33 a learnable perk, 34 up a held perk.
  `FormatPrice` is "%d" / "%.1fk" / "%dk" / "%.1fm" / "%dm" / "%.1fg", negative as 0x7FFFFFFF.
- Item strip (`CUiItemStrip`, the forwarding strip of camp and exchange): `Update` scroll physics compared to 4 ULPs;
  `SetupFigures` (name list shares +0x194 with `filter_`); figure strips (00741BF4) `DrawFigures`, `CountFigures`,
  `OnFigureClick` (`ui_item_strip_setup.cpp`; ui_hero_screen mode 10, unconfirmed that its clicks land).
  `UiItemKinds::Is*` (005EE050-005EE130, P), `CItemScreen::MoveItem` (RET 0x18) and `ReturnAll` (RET 8;
  `ui_item_screen.cpp`), `ScrollToItem` (RET 4).

Multiplayer hero screen (`CMpHeroScreen`, `ui_hero_screen*.cpp`, `ui_hero_figure.cpp`; O unless marked). Modes: 9 hero list
(rows 9-18; 1 continue 006183B0, 5 new hero, 6 rename, 7 delete "lmp_deletepers", 8 the hero's inventory = the camp screen
built inline with `fromHeroScreen_` 1), 10 new hero (1 create 006186F0, rows 9-19 the voice), 11 equipment (1 next 00617A50).
Scenarios ui_hero_screen, ui_hero_stats (RefreshHero), ui_hero_delete (AddNewHero, DeleteHero, record life cycle; unconfirmed).
`EquipmentWeight` (53, double in ST0), `CanEquip` (43, AL), `OnItemChosen` (42), `GetHero` (51), `RefreshHero` (47),
`ChooseFigure` (52: +0x1BC / +0x1C0 are two item strips whose `selected_` (+0x17C) is the chosen figure; the strip listing
the prototype picks the voice list; the voice (text 6) is the list's first file, read even when the list is empty).
`OnKeyDown` `FinishRename`: the name before " | " plus " | " and the typed text; paging by 10 / 1 heroes or 11 / 1 voices;
`Rename` uses Mid (006FD9B4). `PlayVoice`: prototype -> sound set map 007C8A6C -> a random entry, flags 0x2282 (ours counts
the entries instead of building the temporary vector). `Continue` builds `CMpConnectScreen` (00741CD0) inline. `CreateHero`:
attribute sum vs 00741C98 equal or unordered, a name. `Draw` previews at depth 553 / 400; `Update` turns by fmod 007C7F10 and
tints the create button; `Destroy` is the destructor body (vtable 0074214C slot 0 00633240 calls it). `AddNewHero` writes file
"<highest *.mp title + 1>.mp" (00619920 when full; `RefreshHeroScroll` shared with DeleteHero); `OnMessageResult` 0x10001
deletes in mode 9. `DrawHeroList`: a hero record is a CParty whose first member's text 0 is the name. `MakePreview` (RET 4;
skill animation 0x21 from a random frame, controller slot 14 `RestartAnimation(float)`). Forwarders `RenameAt` /
`PlayVoiceAt` / `ContinueAt` / `CreateHeroAt`.

Exchange (screen 9; multiplayer only, no scenario): `CExchangeScreen : CItemScreen` (+0x190 scratch party, +0x194 / +0x198 the
client's parties_[1] / [0], strip, figure strip, trade grid, money field, figure names); `ui_exchange.cpp`: `OnExchangeDone`
0062D210 (37), `Close` 0062C8F0, `OnMessageResult` 0062C920, `Update` 0062C930, `OnLButtonDown` 0062CF70 (O). The client's trade
calls go through their addresses (originals_ui.h `Client*Original`).

Hazards and facts:
- **Item strip**: `ItemAt` truncates `(x - 50) * 0.01f + position` with `_ftol` (the sum not rounded to float first), `SetTab`
  settles the old tab at `_ftol(pending + position)`, releasing an arrow (`SettleScroll`) keeps the fraction of the pending scroll
  and adds or removes a whole slot when the fraction is under half; `Update`'s branch-free FNSTSW selects pick the speed on NaN.
  Filter 0 puts quick items (0x3006) in tab 2 with weapons, armour and spell containers; tab 3 holds class 0x3007, so clicking tab
  3 never equips quick items.
- **Battle screen Create** reads its part of a save (007AFDF8; from 0x47 an int and a string appended to the top panel's text and,
  on page 0, the message log; from 0x62 +0x18C and +0x188), builds the unit panel, map, clock, movement and top panels inline
  (owner +0x148) and puts the "pers <name>" texts into the map<int, CString> at +0x1F0.
- **Global map**: positions are `centre + turn(local)` (centre = layout point (400, 280) at depth 10, each step stored; `turn` =
  spin about z by +0x1FC times a tilt of 0.8 pi about x, inlined with its own operand order: `TurnProduct`); markers come from the
  player's variables ("z.<zone>", "q.<zone>.<quest>" = 1 by bit compare with 1.0f, ways out whose "z.<way>" equals 2 by FCOMP:
  unordered counts as equal); arriving from anything but the camp autosaves to "saveb" when 0079BE7C is set; Update draws the
  weather (00499F10) into texture 4's locked surface under the critical section 007AEE78. Create calls SelectZone before it creates
  the weather object (+0x214 still heap garbage): a wrapper snapshot through it crashed ui_camp.
- **Camp**: an unoffered page shows the current one again (recursively: the tail runs twice, as in the original); constructor pages
  need +0x195 (spell) / +0x196 (item), their second pages also `!+0x254` ("i.noconstr"); money below the cost disables an action
  button. MoveItem: slot 43 first, refused = the cancel sound; moves go through 005EE1B0 (it reads Shift +0x7D itself); items whose
  record +0x10 flag is set (made by a constructor) go back to the constructor's parts list and strip (+0x1EC / +0x1F0), others to
  the local player (00578BA0) and the item strip (+0x1E0). Leaving (page 7) without a constructor still calls ReturnAll with the
  null constructor lists +0x1C8 / +0x1B4 as `to` and empty `from` lists; ReturnAll reads `to` only when `from` holds something,
  and so must its wrapper. Hero skills: 00527EB0's result is stored as float then FISTP; the mana
  maximum (00523F50) is stored as float, divided by the period (00523CD0, unrounded) and truncated (`StoreFloat`).
- **MoveItem**: with `limit` > 0 (and at least that many items in the target) it moves only an item the same as one of the
  target's first `limit` items, and then gives no turning state; ScrollToItem (the camp's "AddItem" call sites) does not update
  `count_`; its second loop inlines the array's SetSize.
- **Trade panel** (vtable 007417A8; the camp's +0x1F4, the hero screen's +0x1C4): rows of 0x18 bytes (STLport vectors); perk text
  keys are the record's +4 name without trailing "123" plus "0"; held perks are a level byte per group of three skill records
  (007AF268, 0x44 bytes). **The hero screen's rotation state is window +0x1E4 (the turn +0x1E0), not +0x150.**

The trade grid draw 00627390 is in `ui_trade_grid_draw.cpp` (renderer.md); exchange Create / Draw and the connection
screen are in section 3.

### Briefing / base screen (`ui_briefing*.cpp`, `ui_briefing.h`, `ui_person_texts.cpp`)

Screen 3 of a "brief" zone (zone_base_bz1g: Create 006024B0, Update 00604130, Draw 00602BF0). States +0x16C: 0 walking, 1 topic
choice, 2 dialogue starting, 3 phrases, 4 rewards. Scenario `ui_base_dialogue` (sets "b.Smith.M4" first: Smith has no topic after a new game): the leader walks to Smith
(MoveToObject), `clickunit Smith`, region 0 (the first topic), waits 4 s, clicks through the phrases, Esc, a second Esc takes the rewards. A
person can talk when its server controller's command is 0 or 0xB and the next one 9 (or it has none). All O unless marked.

Behaviour worth knowing: `OnMessageResult` has the load window inline; `OnBriefing` (34) is "briefing <name>"; `SaveState`
(40) writes one zero dword; `OpenTopics` makes five rows 210..570 x 440.. with "goodbye" appended; the mouse move sets cursors
0xD / 0xE / 0xF; `OnKeyDown` (`ui_briefing_keys.cpp`): action 4 opens the objectives window when a "q.<zone>.<quest>" = 1 lies
in this allod, 0xB the camera to the leader, '5' / '6' "saveq". `FindTopics` (S, `ui_briefing_topics.cpp`): "b.<person>.<topic>"
= 1 by bit compare; a network client sorts them; the last "constr..." topic last; `TopicTitle` maps "constr..." to "briefing
constr". `StartDialogue(immediate, remember)`, `StartPhrases` (camera saved to +0x1F8, look-at +0x204, 0066F840(3, 0) to the
server), `PlaySpeakerAnimation` (the controller's +0x978 callback 006023F0), `ViewSpeaker` (message 0x0F), `ViewPreset`
("#camera n"), `HideSpeaker` ("#hide"), `ShowItem` ("#show"), `OnUnitJoined` (29: the leader's "b.<zone>" topic starts at once),
`PersonTextMap::Load` (also the battle screen's setup), `FinishDialogue` (completes "b.*.<topic>" through 00592090's
address), `MovePerson` (RET 0x28).

Layout: speakers +0x1A4 hero, +0x1A8 person, +0x1AC third; places_ +0x1B0 / directions_ +0x1D4 ([0] person, [1] leader, [2]
third), thirdPlace_ / thirdDirection_ +0x210 / +0x21C, personHome_ / personHomeDirection_ +0x228 / +0x234, firstPhrase_ +0x244,
dialogueText_ +0x248, speaking_ +0x254, shown_ +0x278 (BriefingShownItem list), immediate_ +0x27C, autosave_ +0x27E, people_
+0x284 (stlp::vector<int>), markPhase_ +0x290, speakers_ +0x294 (12-byte BriefingSpeaker), text_ a CUiScrollText;
`BriefingRecord` 0x6C (exp, gold, four name lists, specials +0x68). The "constr" topic's number is `Mid(7)` (006FD9B4, not
Right). Helpers: `BriefingVariables` (00578C40 into a 100-bucket table, 00581790; walked with 005E0DB0), `BriefingPlayerName`
(00578A90). player_messages.h cannot be included next to ui_briefing.h (three MfcArray templates), hence the forwarders
(originals_ui.h) and `player_out_mark.h`. Inlined in the original, not functions: 00609A80, 0060B490 / 0060AC00, 00609A40 (pool
0x30). 00608D10 (slot 35, `MOV AL,1; RET`) is too short to hook; 006023F0 is no function in Ghidra or function_sizes.inc.
Left out as unused: Update's sort / unique of the marked
ids, the unused party searches (00578BA0) in HideSpeaker / FinishDialogue, OnUnitJoined's server-world lookup.

## 6. Battle screen: update, selection, orders, pending actions (00635250-0064BE80)

Files `ui_battle_*.cpp`, `ui_hud.cpp`, `ui_hud_pick.cpp`, `ui_hud_tips.cpp`, `ui_hud_highlight.cpp`, `ui_spell_column.cpp`;
scenarios `ui_hud`, `ui_battle_commands`, `ui_battle_spells`, `ui_order_move`, `ui_quick_items`, `ui_quest_notice`. The key
handler 0063A560 is Rebirth-patched (the action table 0079BF70: 1 pause, 2 / 3 game speed, 4 objectives, 0x17-0x1A movement
modes, ...; Esc cancels a targeting mode (+0x17C) or opens the Esc menu). The Rebirth-patched callees 005E70E0, 005E7680,
005E7D60, 005FDE20, 005F8340, 005FC9E0, 0063BCC0, 0063C050, 0063E4B0, 0063F500, 00638EF0 are called through their addresses,
and so are the rewritten ones with wrappers (00637DC0, 006379E0, 00637190, 006373A0, 00637500, 00637750, 0063CEE0, 00639EF0,
0063EC90, 0063FB50, 0063DD70).

Entry points: `CBattleScreen::Update` (00638280, every frame, O) and `RefreshSelection` (00637DC0); the panels' `SetUnit`s
(S); `OnMouseMove` (00639D60 S), `UnitName` (0063B920, RET 8), `OnLButtonUp` / `OnLButtonDown` (O; ui_hud, world_client,
ui_describe); the message slots in `ui_battle_slots.cpp` (`SaveState` 40 S through a recorder, `Close` 26, `PollShown` 35 AL,
`OnItemsPicked` 28, `OnUnitLeft` 30, `OnUnitJoined` 29: placed by the local player's record order, selected by the record's
+0x778, `OnMessageResult` 27, `OnGameOver` 31; 27 / 28 / 30 / 31 not reached by a scenario); orders `OrderAt` (RET 0x14),
`ToggleMember` (RET 4), `AttackUnit`, `OrderDeadUnit` (RET 8), `UseOnUnit` (RET 0xC), `UseAtPoint` (RET 0x14); `OnUnitSpeech` (32),
`OnQuestVariable` (33); `OnLeftDouble`, `OnKeyUp` (the order key of actions 0x11..0x16, `orderKeys_` +0x18D in the jump
table's order 0, 1, 5, 4, 3, 2), `OnWheel` (map, unit panel, message log, top panel); the columns' `Highlight` /
`ClearHighlights`, `CUiMesh::SetScale` (callers use `HighlightAt` / `SpriteSetScaleOriginal`); `CHudSpellColumn::Click`,
`CHudActionColumn::Select`, `CHudQuickItems::Pick` (S; 0063A560's spell keys call them by address); `ClickAlternate`,
`UseNow` (`ui_hud_pick.cpp`: an immediate cast on the unit, or for an attack spell (0068B770) on the nearest unit it sees;
none: "nomagic"; `dblclick` in ui_quick_items).

How they chain:
- **Selection.** `RefreshSelection` is the hub: a single living selected member (flag 0x10; slot 55 = dead) goes to the four
  `SetUnit`s, else they get null; the action column's second argument is "anyone selected" (the original's empty case pushes EDX
  after SETG DL: only the low byte means anything). The original stores the first selected unit before testing (00637EE5). Slots
  28 / 29 / 30, ToggleMember, a box drag and a network party change end with it.
- **Button release** (`this` = input side): without a press on the view (+0x1C0, set by 00638A40 when no panel took it) the
  container's handler 006011F0; a drag (+0x1C1, set by the mouse move past the threshold option 0079BDB0; 0 or less = no boxes)
  with the units in the box (+0x1CC, 005656A0) matched against the party clears the selection (00566100(0)), resets markers and
  faces and re-marks and controls (00566030) each unit inside, then slot 32; otherwise 00565E40 gives the unit at the point (kept
  only with flag 0x400000) and SelectUnit (00638EF0) gets it.
- **Pending action** (`CBattleScreen::SetPendingAction`): +0x180 slot, +0x17C mode, +0x184 item; every highlight off and the
  panels' +0x150 -1, then the slot's button highlighted in the mode's panel (1, 2 spells; 3-6 quick items; 7, 8 actions); a
  cancel is (0, -1, -1). Modes: 1 / 2 a spell on a unit / at a point (spell data flag 0x10000000 or not), 3 / 4 a quick item's
  spell likewise, 6 a player's item, 7 follow, 8 an order on an object (science). A highlighted button clicked again cancels with
  a sound; only a spell item with its data (+0x44) sets the action; only wand (category 5) and scroll (8) quick items go on.
  SelectUnit passes the Ctrl byte as `quick` for a spell and 1 for a quick item; Rebirth's FixSpellSelfCasting passes 0 for the
  spell. `UseAtPoint` moves the point by -AxisY(unit +0x34) * 0.1 for the member that is `unit` (the original changes its own
  argument slots, so the moved point stays for later members). Orders nod the members (+0x38 1, or 2) with a non-positive delay;
  the exit flag is +0x18C / +0x188.
- Spell kinds come from `spells.sdb` (tag 0x0B at prototype +0x30): 'u' (0x10000000, mode 1: arrow, lightning, acid_ray, the
  protections, healing, charm, ...), 't' (0x20000000, mode 2: fireball, inv_lit, acid_column, firewall, litnwall, acid_fog,
  fireworks, clairvoyence, teleport, campfire), 's' shapechange. **Open:** ui_battle_spells' firewall and fireball should give
  mode 2, yet its runs show `UseOnUnit` twice and `UseAtPoint` never (`SetUnit` leaves the last button highlighted, so the first
  click on it cancels; which click cancelled is unknown).
- Spell buttons: 0063E4B0 puts the newest spell's region at (760, 470)-(800, 510), older ones 40 up; action column regions are
  0..40 x (470 - 40 i)..(510 - 40 i); with a selected unit SetUnit adds "skill0011" (action -1) first, then science (action 0).

**Tooltips for regions without a button** get SetTip(0, 1, 0), a fresh 0x184-byte tooltip with the empty text 0079B298 whose constructor 0065E320 writes the arguments (+0x164 / +0x168) and size (+0x174 / +0x178) only for a non-empty text: leftover heap bytes, compared only for tips with lines. FCOMP / `TEST AH,0x40` compares treat NaN as equal (damage "Unknown" when all weights are zero; quest values 2 and 3).

Not exercised: the battle screen's drag (no drag step in the harness), mode 1 orders and the toggle with Shift (Shift during a click: the harness's `shift` word), the exit flag, RefreshSelection with several units, the attack kinds 0..5 and the level-gap speech,
the quest notice's sub-objective path, the mouse move's portrait pick branch (multiplayer).

## 7. Battle HUD panels

Files `ui_hud*.cpp` (declarations in `ui.h`, "Battle screen and its HUD panels"); scenario `ui_hud` (new game, mouse moves over
every panel, clicks on weapon slots, top panel pages and sling, unit panel body parts and sling, map views, movement panel, a
portrait; the Esc menu: Options -> group -> Cancel, Load -> row -> Cancel, Save -> Esc, Resume; the hero must take damage; the
wheel over the map's sling button (in, then out past the limit) and the map; TAB opens the objectives window. Spell, action and
quick item buttons get only mouse moves: a click would start a targeting mode the following Esc cancels).

Panel rectangles (800x600 layout; Rebirth moves some): weapon bar 80..240 x 520..600 (4 slots of 40), quick items 520..720 x
500..600 (8 slots 50x50), spell column 760..800 x 190..510, action column 0..40 x 250..510 (6 buttons from y 470 up), portraits
250..550 x 440..600, map 615..800 x 0..165 (region 0 = the sling button 780..800 x 0..30), unit panel 0..185 x 0..225 (body parts
1-4 at 0..20 x 100..220), clock 720..800 x 520..600, movement panel 0..80 x 520..600 (0 inner quarter circle radius 34, 1-4 ring
sectors 35..80), top panel 180..620 x 0..100 (sling 385..415 x 0..20, page buttons 185..215 / 215..245).

Panel classes and their pieces (all in `ui_hud_*.cpp`; mouse handlers S where the code says so): `CHudWeaponBar`,
`CHudQuickItems` (Draw 0063CDC0 = 0063F190, 0063FD50 of the columns), `CHudSpellColumn`, `CHudActionColumn`, `CHudPortraits`
(Rebuild 006407E0 every newgame, AddPortrait 00640060 RET 0x10 through its address, ShowFace 00640670: spells_cast low
mana, hits; `ui_hud_portrait_setup.cpp`), `CHudMap`, `CHudUnitPanel` (ShowHealth, ShowParts 4 ULPs; `ui_hud_panels.cpp`;
`DrawUnitInfo` 00647040 from 006462A0, Rebirth-patched), `CHudClock`, `CHudMovePanel` (`ToggleFlag` 00649810: orders through
the local view 0066AF30), `CHudTopPanel` (`ui_hud_top_panel.cpp`: `ShowPage` S, `Layout` S, `Build` 00649AF0 disabled in
tests/verify/disabled.txt (X17), `AddPickedUp`, `AddMoney` (no scenario), `ShowItems` 0064A7A0 with `this` the top panel in
ECX, which Ghidra mislabels). Per frame (S, state put back): `CHudMap::BuildMarkers` 00643DC0, `UpdateView` 00642E80,
`CHudPortraits::Update`, `CHudClock::Update`, `CHudTopPanel::Update`. Destructor bodies are in `ui_hud_destroy.cpp` (inlined MFC
array destructors write the array's vtable, then delete); `PartyIds::Equals` 006381E0 (P, every frame).

Rebirth-patched (vanilla-only hooks; callers use the original addresses): 00630010, 00636020, 00638EF0, 0063A560, 0063BAB0,
0063BCC0, 0063C050, 0063C800, 0063CAE0, 0063E2A0, 0063E4B0, 0063F300, 0063F500, 00641F20, 00642930, 00642B40, 00645210, 006456E0,
00645F80, 006462A0, 00647FB0, 00648520, 00648D70, 00649190, 00649910, 006567A0, 00659420, 0065C2E0, 0065E580 (the last sixteen: A-H26 in items_spells.md; 006462A0, which clears the Z-buffer through the Direct3D device
itself, by A-E2). 006407E0 holds
EI_Plugin Fixes sites (rebirth.md), so the installer keeps the original under Rebirth. Dead: 0063C010 / 0063F6E0 (a panel reset,
no references). Library: 0064AB20 / 0064AD40 / 0064B3A0 / 0064ACD0 (the portrait slot vector).

Hazards and facts:
- Quick items' slot lookup uses RegionAt's result unchecked: a point inside the panel but outside every slot (-1) reads the dword
  before the table (+0x168, zero in practice) and counts as a hit (`RegionItem`); the weapon bar's button-up orders `LocalView()`
  without a null check; the minimap wheel zooms only over region 0 (zoom 2.5^(delta/1200) with `_CIpow`, compared unrounded);
  hidden minimap entries keep stale positions; the original's FLD / FSTP copies of unchanged bar y values are skipped;
  BuildMarkers reads a zone exit's target through a copy of the exit record (ours directly); unit 0xA2C2A counts as hostile in
  gz20g.
- Minimap sprites are `CUiMesh` quads (005EA150 hands the arrays to the renderer); `UpdateView` clips the outline in place against
  the world rectangle, pads to a multiple of four with its first point and fans it; the needle angle is FPATAN(-x, -y) of the facing.
- **Clock reads its constants from memory** (00742B08, overwritten by Rebirth). Time of day = (ticks 007AF61C * 007AF5D8 +
  007AF5D4) mod 24 through `_CIfmod` (inline asm, as `WorldTimeOfDay` in script_eval.cpp).
- Portraits (`HudPortraitSlot`, 0x78 bytes): 005FF460 replaces the scene object, so the rewrite re-reads the object array after
  it; three inlined quaternion products each have their own operand order; the RNG's double scales (0073C468, 00742760, 0073C508)
  are not exact powers of two and are read from the exe; random words are loaded like FILD qword (MSVC's unsigned-to-double
  rounding changed the wobble delay in 4 of 5304 calls). Kept: the sex letter ("m" / "f") keeps the previous portrait's value
  for a figure that is neither "unhuma" nor "unhufe"; AddPortrait's count % 4 switch leaves the texel offsets uninitialised for a
  negative count (ours 0) and indexes the column's frame and restB_ by count % 6. The multiplayer branch (no scenario) calls the
  name maps' find_or_insert directly (00514880 / 0053D340).
- Unit info: "consider_more<d>" / "consider_less<d>" are looked up in place (one CString as key and result); a damage type of 7 or
  more keeps the move text; damage values go through `__ftol2_sse` instead of `_ftol` (both truncate). `ShowParts` reads six body
  part records (+0x374, 0x1C bytes) with label positions from 00742434 / 0074244C. ToggleFlag's third frame value 0.48046875 is
  unreachable. Texts go through DrawText 005FDE20 and Place 005E6FA0 by address.
- Top panel: `ShowItems` builds page 1: "format_money" with the local party's +0x50 when above 0, then the local player's item
  list (WorldPlayer +0xE0) merged by kind (slot 14) into the client world's copies (0055E940), one FormatItemName line each, the
  copies released; into the log when page 1 is up. The original builds lines with CString operator+ temporaries; ours appends to
  one CString.

## 8. Objectives, save/load, options, help windows and the controls

Rectangles from `menus.reg`: save/load OK 163..237, Cancel 563..637, Delete 363..437 (y 526..574), list rows 108..370 x 108.. in
24 steps; options OK 213..287, Cancel 513..587, groups 100..250 x 156.. in 24 steps, rows x 250..520. All O unless marked.

Scenarios by window: save/load (`CSaveLoadWindow`: input, `Draw`, `Update`, `OnKeyDown` Esc 0x100001 / 0x110001, `IsReservedSave`
P) ui_hud, ui_saveload; `Confirm`, `AskDelete`, `LoadSelected`, `DeleteSave` (looked up through the game's GetFileAttributesA
import, which harness runs redirect to harness_saves; called by address `Call*`) ui_saveload, ui_saveload_delete; `OnDialogResult`
(the caller 005F590D ignores EAX); `SaveEntry::MakeTitle`, `ZoneTitle` / `AllodTitle` ("zone " / "allod " with the space,
007941F8 / 00794200; else "save_camp" / "Unknown"; S; `ui_saveload.cpp`); `QuickSave` (`__fastcall` slot, DL frame step) /
`QuickLoad` (newgame's savea); `Build` (00653DD0 save/load, 006503C0 options; `bindings_` / `settings_` are stlp::vectors,
`groupNames_` an mfc::CStringArray; `DestroyOptions` 0062B180); options (`COptionsWindow`: input, `Draw`, `Redraw`, `HelpTopic`
P, `Update`, `OnKeyDown`; ui_hud; `RefreshRows`, `ShowGroup` (RET 4), `BuildSettings`, `BuildBindings`, `LoadSettings` (S; sets
every control twice), `Apply` (`ui_options.cpp`; `Call*` forms by address); Build runs all but Apply (ui_hud also clicks group
2); Apply: ui_main_menu's OK); controls (`CUiScrollbar` `TakePosition` S, `Update` also the message log, `CUiSlider::SetRect`
slot 26 RET 4: every frame; `CUiTooltip`; `CUiScrollText::Tokenize` / `DrawLines` RET 4 (`ui_scroll_text.cpp`; the message log);
`CUiEditField::OnKeyDown` S (`ui_edit_field.cpp`; no scenario types into one); `CUiScrollingSurface::RenderText` 0065F020: the
titles' RichEdit text via EM_FORMATRANGE, ui_main_menu, render_titles_present); `CObjectivesWindow` (`Build`, `ShowZone`,
`ShowQuest` `__fastcall`, `DrawQuestList`, `OnLButtonDown` in `ui_objectives_input.cpp`: OK requests the zone at the nearest
exit, travel time 00578700 forwarded; `LoadQuestPlaces` 0064EA80 S: map.txt "#quest <name>" pairs, or the quest record's
floats for a client; the forwarder is `FillQuestPlaces`; ui_hud's TAB, ui_objectives); help window (`ui_help_window.cpp`:
Build "tutorial <topic>", "#screen" per page, "tutor <key>" texts, Close, the pulse, clicks, keys; the page draw 00659420 is
Rebirth-patched; ui_help with F1).

Key handlers run only when the window is not sliding in, pass the key to 00601820 first and set the input side's handled flag
(+8) for the keys they use. A save window edits the name (+0x188, caret +0x190; the first edit of an entry other than "saveq" /
"savea" / "saveb" sets +0x18C); options: while a binding row waits (+0x3F4) the next scan code (flags & 0x1FF) is looked up in
the key names 007857B0 (88 x 0x48), a key bound to another row asks "key_already_mapped", else CommitBinding (00652000) and
RefreshRows. Slide-in eases the model from y 700 to its place (550 / 620 save/load, 480 options): `e = 2s^2` below s = 0.5, else
`1 - 2(1-s)^2`, then `(a - 700) * e + 700` stored once as float; `Update` adds `FrameTime() / 0.5` and compares the unrounded sum
with 1; `CUiScrollbar::Update` adds `FrameTime() * speed * 16` (negated for "up") to a float accumulator (NaN loops forever in
both). `CUiScrollText` (constructor 0065CC00; the battle message log and the quest log; `textRect_` +0x18C, `lines_` +0x1B0,
`align_` +0x1C4) tokenises every text added through 0065CF30 / 0065CFE0 / 0065CC77 into format tokens (0x14 bytes, vtable
00742F28: runs of "\n", BEL 'C' r g b, BEL 'F' digit) and word tokens (0x10 bytes, 00742F20: CString and width from 005F8340) in
the CArray at +0x19C (next token 0065D970; ours calls 0065E010 SetSize for both cases).

**Options data**: rows are {OptionBinding*, OptionSetting*}; OptionSetting::definition_ points into 00787430 (26 x 0x18: id,
name, kind, maximum, group, row), OptionBinding::action_ into 00787080 (59 x 0x10); 14 rows, 11 groups. `CUiSpin : CUiWindow`
0x174. The slider (`CUiSlider : CUiScrollbar`, ctor 0065BC30): regions 0 (left, bottom - 10, left + 12, bottom), 1 (right - 12,
bottom - 10, right, bottom), 2 (left, top, right, bottom - 10); CUiScrollbar +0x17C is the depth (0065AEA0 stores its first float
there: 3.0). The edit field's jump table (0065C8EC / 0065C908): 08 Backspace, 23 End, 24 Home, 25 Left, 27 Right, 2E Delete.
Strings 007948C4 "OFF", 007948B4 "ON", 00794830 "  |  " after each key name, 0079482C " |", 00787BCC "W" (DrawLines' font
sample). 0057F440 is IsMultiplayerClient (Apply tests AL).

**Objectives window** (0x1F0; vtables 007411BC / 00741188, ctor 0063B5D0): TAB (keyboard.ini `obj`) creates it via 0063A560, Esc
or Enter closes it. Build shows the current zone's page, or with +0x17C the zones of the allod in which the player holds quests
("q.<zone>.<quest>" = 1; any allod in multiplayer). ShowZone's exits come from the map database record (+0x4C, 0x3C bytes), not
the world's ZoneExit; the player's variables are copied into a local hash_map<CString, float> (00609C10, 00578B70) walked in
STLport order. RefreshQuests (0064E8B0 -> 0064DC60) trims sprites and regions to 21 + exits and 9 + exits and adds one of each
per quest place, which the wrapper must allow. The +0x17C zone list and the distance table (+0x178, a map<int, float> from
the global map) are not reached. `RefreshQuests` 0064E8B0, `SelectExit` 0064E990 and `Update` 0064E760 (slot 24: the markers'
blink alphas through std::cos and _ftol, a rounding edge may differ by one) are in ui_objectives_input.cpp (O; callers go through
CallSelectExit / CallRefreshQuests so the wrappers run); +0x1C0 is firstRow_, +0x1C8 the float blinkTime_.

Shared forwarders: 006EAF20 `_access`, 00578D00 (ServerFlagOriginal), 00458010 (FrameStepOriginal), 005F8340
(InterfaceTextExtentOriginal). Library: SaveEntry's copy / assignment / destructor and its vector helpers 006578F0..00658020, the
0x14 / 0x18 record helpers 00653100..006535D0, the save list sort 00658030-00658C90, 0065A770, 006537E0 / 00653800 / 006518A0 /
00651CE0 (key records' destructors), the quest list sort 0064F310 .. 006500B0 (its comparison inlines 0064F550, a quest's order,
game code). Not rewritten: RefreshList 006567A0 (Rebirth-patched), 00654490 (3 bytes, too short).

Cleanup leftovers (DR-C5): ui.h has `GameDeviceLock()` (007AEE78) while about twenty files keep a local `DeviceLock()`;
`kUnitPtrArrayVtable` (007402AC) is still spelled out in ui_battle_commands.cpp, ui_camp.cpp, ui_create_screen.cpp,
ui_hero_screen_click.cpp, ui_item_screen.cpp and player_server_handlers.cpp; ui_hud.cpp's portrait server command calls
`net::GameClient()->SendCommand` (0041D260) directly, so its wrapper does not see those calls.

## 9. Item descriptions, item view, sprite shapes, surface helpers (005E0000-00610000)

Scenario `ui_describe` (new game, GiveItem of a steel axe, an axe with a firewall spell, a potion and a rune, `screen 5`; the
mouse rests on the statistics rows, body slots, the item strip's "all" tab (region 7 at 785, 580) with `key ctrl` over the first
slots, then page 1; Esc; vanilla coordinates, under Rebirth some rows may be missed).

Pieces: `CItemInfoPanel` (`ui_item_info.cpp`; S on pixels with nested wrappers; ui_camp, ui_describe; blueprint, material, spell
record, modifier descriptions reached by none): `DescribeStats` (005F1590, 4.9 KB), `DescribeItem`, `DescribeBlueprint`,
`DescribeCarriedSpell`, `DescribeMaterial`, `DescribeSpellRecord`, `DescribeQuickItem`, `DescribeModifierRecord`, `ShowHelp`,
`Draw`, `Describe`, `OnKeyDown` / `OnKeyUp` (`key ctrl`), `Build` (O); `ItemDamageLists` 005F13A0 (EI_Plugin patches a JMP at
+0x61: ours runs in vanilla only; S); `CItemScreen::DrawItem` 005ECA10 (S; drawn twice per frame in the verify build);
`CUiMeshBuilder` `Framed`, `Ring` (RET 0x2C), `Disc` (RET 0x24), `ResetColours` (`ui_mesh_build.cpp`; Commit's +0xC4 compared as
"points at the current indices"; S with arrays poisoned 0xCD; every scenario); surface helpers `CaptureScreen`, `Rgb565Copy`
(the save picture), `Greyscale` ((2r + 4g + b) / 16), `SwapContents` (O), `PixelAt` (`ui_surface_pixels.cpp`; S; ui_screens),
constructors / destructor body (`ui_surface_create.cpp`; O; the picture window is dead); `DrawHeroFace` 005ED7B0 (EI_Plugin's
fix at +0xD9 keeps the original under the plugin), `ItemFigureTexture` (256x256 from "<name>.mmp" via converter 007AF06C slot 5;
locks without DDLOCK_WAIT, left held if the error report returns 0), `CUiWindow::DrawHeroPreview` 005EC300 (O) (`ui_item_view.cpp`;
S on pixels); `CItemScreen::CreateBase`, `CItemIconWindow::Build`, `TrimItemModels` / `TrimItemTextures` (O).

Facts: the camp creates the description panel (+0x164 at 600, 100) and the help panel (+0x1DC at 0, 100); the owner (+0x14C) is the
item screen and +0x150 an item list holding a copy of the described item (00661DB0). `DescribeItem` dispatch: kind 0x3007 with +0x4C
1 material, 2 with +0x50 0-2 blueprint, 3 spell record, 4 modifier record; quick item category 8 -> 005EF300; a carried spell (+0x44)
while Ctrl is down, or a spell container (0x3008) -> 005EF5D0. Values computed by original functions are truncated straight off the
FPU stack (inline asm, the game's `_ftol`). The item screens keep model (+0x170) and figure texture (+0x180) caches (STLport
map<string, {object, last use}>, limit 30, or 20 with 00784754); each item has a turning state (vtable 007403F8, 0x18 bytes, at the
item's +0x1C) whose glow and angle wrap keep unrounded x87 values (inline asm). The sprite builders use the original's FPU sequence
for circle points and call the out-of-line array copies. `00700EFE` is `CString::MakeLower`. In the switch-over the surface
functions work on game-owned memory (renderer.md, "Interface windows").

## 10. Harness steps and scenario lessons for the interface

The steps added for UI work (`screen`, `clickregion` / `moveregion`, `wheel`, `dblclick`, `clickunit`, `hudstate`, `campputon`,
`zone` / `waitzone`, `ldown` / `lup`, key names `ctrl`, `grave`, ... `enterchar`) are documented in the header of
`tests/verify/harness.cpp`. Facts behind them:
- **Picking** (`clickunit`): the client world (007AF5B8) keeps a list at +0xF0 (node: next +0, value +8 of 0x18 bytes with the
  object id at +0xC and a screen RECT at +0x10) of the objects drawn this frame (renderer.md section 3); 00565E40 (thiscall, RET 4;
  the button release 00638B30 calls it with the message's x, y) gathers entries whose rectangle holds the point (005718B0:
  PtInRect, then the object's +0x94 slot 24 finer test), sorts by depth (005659D0) and returns the first unit (type 0x50); the
  release then wants flag 0x400000. The step tries a 9x9 grid inside the unit's rectangle until the game's own pick answers with
  that unit, so the click cannot land on a unit in front; it fails when the unit is off screen or hidden. Modifier words (`ctrl`, `shift`, `alt`) after any key or mouse step hold those keys around it (SelectUnit's
  +0x7C branch with Ctrl orders an attack, 00637190), and `hold` / `release` keep them across steps (`ui_modifier_keys`:
  Shift + portrait = ToggleMember's toggle, Ctrl / Alt + spell key 1 / quick item keys = 0063EF60 / 0063E020). They are posted as
  key messages (Ctrl, Shift WM_KEYDOWN; Alt WM_SYSKEYDOWN with the context bit), the only way the game learns them; the harness
  cannot drag.
- **Zone steps**: `zone` calls `LeaveToZone` (0066F310) on the local player's connection (server world 007AFF60, `GetPlayer`
  00594180 with player 0) as the zone exit handler (00670C20) and script command 0x8A do; it needs state 1, and in campaign mode 1
  or 3 a dead or missing leader loses the game instead. `waitzone` waits for no pending game request, campaign mode (007C2CC4) not
  2 (menu), the server world's zone name (+0x120, set by NewZone 00595480) and state 1; the party appears a little later
  (Redeploy, world.md "Party between zones"), so follow it with `waitfor ... HP(GetLeader())`. The exit index (world player
  +0xDC, zone +0xD8) counts `#exit` blocks from 0 (map.txt's `#exit n` minus 1); the deploy rectangle is exit +0x04..+0x10, an
  index outside the zone's exits leaves the party at (0, 0). Zones: game `gz1g`..`gz21g`, brief `bz1g`..`bz18h`, edge zones
  `<a>_<b>` (no `#res`, mode 4; `waitzone` does not recognise them). One-way entrances (`none 0`): gz1g's #exit 1, gz4g's 5, bz4g's
  and bz13h's 1, gz11k's 4, gz15h's 2..4, gz20g's; `gz21g` names zonemichel.mpr / .mob which the install lacks (NewZone fails;
  untested); `gz6g` loads its frozen map when `z.gz6g.frozen` is nonzero; no zone needs quest state to load. Not tried: edge zones,
  `endofgame`, leaving a zone other than zone1. Scenarios `zone_gz2g_traps` (party at (67, 15), zone2's traps 1324..1330, MTF4
  armed with a wolf), `zone_base_bz1g` (mode 3, party at (59.25, 54.25), Smith), `zone_gz3g` (party at (23, 138.5), the Pyramid
  trap 4515).

Scenario lessons:
- **Race with a zone's intro script.** The server drops a hero with flag 0x800000 (script BlockUnit / IsUnitBlocked) in 0066FFF0
  before FindPlayerMode 005D5E60 / CommandWalk 005D39E0, and gz1g's intro script (SendText0D, SetVariableMax "q.gz1g...",
  PlayJingle) blocks the hero for about 37 s with its first tick unordered against the hero's creation: `ui_order_move` waits for
  the block (`waitfor 15 IsUnitBlocked(GetLeader())`) before lifting it and checks the hero is not blocked before each click
  (scripted moves ignore the flag). The order path (OrderAt 00636F10 -> 0066A7D0 writes 0x30, count, ids, ctrl, point into
  CPlayerClient +0x5C; 00667810 compresses and sends it on the loopback link; the server's 0066D270 decompresses it into 007C83D0
  and dispatches by the first byte through the table built in 0066C9A0, 0x30 -> 0066FFF0) has a wrapper at every rewritten step.
- **Armed traps.** gz2g starts its magic traps armed (its scripts disarm them from triggers), so MTF4 strikes a placed wolf at the
  next world update: the scenario disarms it before placing the wolf, checks the wolf alive, arms it and waits for it to die. The
  "command 8" mismatches there were a verify bug: 004C7C90 adds a script instance at the head of the list (script.md, "Runtime").
- **Quick items.** No script command puts an item in a quick slot (GiveItem adds to the player's list); use `campputon 3006 2` (at
  most four go on the hero: MoveItem -> PutOnFromStrip -> PutOn 0060FCA0); "wand 1".."wand N" are category 5 prototypes (items.idb
  tag 0x15, e.g. `wand 3.iron[lightning]`; potions are category 8); the quick item panel's regions 0-3 are the bottom row from the
  right (x 670..720 down to 520..570, y 550..600), 4-7 the row above; place a target where it is drawn on screen (`QuickWolf3` at
  (hx, hy - 3) was culled, (hx + 3, hy + 3) works).

## 11. Rules and lessons for interface work

The rewrite guide's rules 2, 5, 7 and 9 apply throughout (Rebirth-patched callees such as `CInterface3D::CallScale*`,
`CUiRegion::CallSetRect`, `CUiMesh::SetRectangle / SetFrame / Place`, 005FDE20, 005F8340 and the panel builders by address;
nested wrappers through `Call*` / `...At`; 00742B08 read from memory). Interface-specific:
- Float-heavy per-frame code (clock, top panel, strip scroll, minimap) keeps the original's order: FPU values `double`, float
  stores through `StoreFloat`, `_ftol` truncations, unrounded sums stay unrounded (`ItemAt`, `SetTab`).
- COM device methods are stdcall with the interface pushed first: call them through a stdcall helper (a thiscall helper put
  the interface in ECX and crashed the hero preview).
- Side-by-side checks of stateful handlers record the fields the original may change (handled flag, capture, panel state),
  put them back, run ours and compare; pixel checks use the settle rule (section 3). A wrapper's snapshot must not follow
  pointers a Create sets later (the global map's weather object).
- A wrongly placed field hides bugs for long (`CMpHeroScreen::rotating_`): when a state seems stuck, compare the offset in
  the handler that sets it with the one that clears it.
