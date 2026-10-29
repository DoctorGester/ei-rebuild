# User interface (005E0000-00670000)

What is rewritten, scenario preconditions, hazards, original bugs kept, what is left. Layouts and per-function behaviour
are in the headers and code (`ui.h`, `ui_camp.h`, `ui_screens.h`, `ui_item_info.h`, `ui_cursor.h`,
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
delta, x, y)`; handlers `RET 0xC` (wheel `RET 0x10`) and return 1 on every path;
no caller reads it, and ours (handlers, Event, the draw) return void. A button-down handler that takes the
mouse stores its input side in `CInterface3D +0x0C` (`mouseCapture_`); the button-up handler acts only when the capture
is its own and clears it; a handler sets `+0x9C` (`handled_`) when it used the event, which stops the container
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
| 0065A500-00660000 | controls: scroll bar 0065AEA0, spin / edit 0065BC30 / 0065BCD0 / 0065C010, scrolling text 0065CC00, tooltip 0065E320, DirectInput mouse 0065EAD0, titles' scrolling text 0065ED30, party bar 0065F2D0 |

**Rebirth** (WideScreen, rebirth.md) patches the five scaling helpers, the sprite placement (005E6FA0, 005E70E0, 005E7D60,
005E7680, 005E99D0), CUiRegion::SetRect / AddPoint, 005FDE20 (text), 005F8340 (text extent), many screen builders and key
handlers, and the constant 00742B08 (the clock's x centre, 800). Our code reproduces the patches behind their flags
(rewrite guide rule 9; rebirth.md, config/rebirth_patches.csv).

## 2. Interface manager and window base (`ui_interface.cpp`, `ui_window.cpp`)

`CInterface3D` (Create 005F4990; the original's constructor 005F46A0 ran from a static initialiser; ours, `g_interface`, and
the cursor `g_cursor` are plain globals that construct themselves) keeps the active windows in an
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

**Deleting destructors** (O): slot 0 of all 50 windows in 005E0000-00640000 and the input sides'
37 thunks (`SUB ECX,0x94; JMP`); ours are the compiler's; the chat line's (its surface) is in `ui_chat_overlay.cpp`. The plain strip's 0060E280 frees nothing of its own; `CObjectivesWindow` 00603B90 does not free the exit distances map +0x178. `CMasterRequestBox` (00741DD8) is a message box
plus a master server link at +0x180; `MpAddressEntry` is 0x34 bytes; 0062EDA0 is 00499E30's code.

### Window stack, screen construction, window base methods (O unless noted)

`OnKeyDown` / `OnChar` / `OnKeyUp` (`ui_interface_keys.cpp`: char goes to the console, a client's open chat line, then
the top window); mouse input (`ui_interface_input.cpp`: to the capture, else the top window; +0x7F double click, +0x80
right button; S on stand-in vtables). `CUiWindow::CopyImage` (005FFFB0, S), `SaveState`/`LoadState` (005F85C0 S through
a stream recorder, every save), `MessageText` (S), `OpenHelp` (005F86F0), the battle camera's key input
(`ui_camera_keys.cpp`, S). `CUiContainer::Redraw` / `RedrawTips` do nothing unless the interface is drawing (`drawing_`).

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
- `CInterface3D` declares +0x40 (queued window), the fonts +0x70, `messages_` +0x84 (STLport list of CString),
  `helpTopics_` +0x98 (STLport map<CString, int>); its root window +0x1C is the `CChatOverlay` (`chatOpen_` +0x14C,
  `caret_` +0x154). `CUiItemPanel` (+0x148 `mode_`, +0x14C `owner_`) is the base of the description panel, hero
  equipment, quick strip, trade / repair grids, constructor pages and the blank panel 00741B58. The hero stats panel has
  its own deleting destructors 0060E340 / 00613DD0.
- Glue (`ui_interface_glue.cpp`): `PlaySound` (Play2D, priority 1000, volume 100; the sound system gets the game's own
  string pointers), `OnQuestVariable` (at 1 the name's third '.' part becomes savedText_), news for the top window
  (slots 28-32, 36, 37 as `CUiWindow::Virtual*`), the bottom window's slot 40 (save game), the chat overlay's AddLine
  (005F82D0 / 005F82F0 / 005F8310 multiplayer only; S via a slot recorder, removed in C19 batch 20).
- Cursor (`ui_cursor*.cpp`): `CCursor::Init` / `AddShape`, `SetShape`,
  `TakeLock` / `ReleaseLock` (mode 2 only), `Start` (thread 005E52A0 unless mode 1), `UpdatePosition` (frames every
  125 ms; +0x08 the DirectInput mouse, +0x98 the thread). Frame draw, thread and image construct (the blend 005E5C40 is not ported):
  renderer.md (R9c2).
- Flying hit points (`ui_flying_hit_points.cpp`): `Draw` (S, timeGetTime held, stand-in renderers replayed), `Add`
  (flag 8 starts 0.5 s later); numbers need option 17 (0079BE74).
- Chat overlay (`ui_chat_overlay.cpp`; multiplayer only, no scenario): `Update` erases lines older than 20 s, `AddLine`
  wraps at 780 px, "^<digit>" colours, at most 12 lines; Enter sends a `net::SayMessage` kept 60 s.
- Camera input (`ui_camera_input.cpp`; S; ui_hud, ui_screens): `RightDown` / `RightUp` (turning with the cursor
  held), `Deactivate`, `Wheel`.
- Console window: `DrawInput`, `DrawLines`, `OnKeyDown`, `OnChar` (Rebirth's ImproveConsole behind its flag), pixel
  checks S (before ours the input strip's rows are put back inverted so anything left undrawn shows); harness key names
  `grave`, `backspace`, `delete`, `home`, `end`, `pgup`, `pgdn`, `enterchar` (Enter plus its WM_CHAR, which the console
  needs); scenario `console.txt`.
- `CUiItemPanel::LeftDown` (005EE310, input 2 of every item panel; S, ui_camp_deals), `ItemModel` (or a counted 100 x
  100 stand-in sprite).
- Small and multiplayer-only functions (no scenario unless named; `verify_ui_small_slots.cpp`, `verify_ui_small_members.cpp`
  list the scenario per address): `UiInputSide::LeftDouble` / `RightDouble` (slot 7 / 8 of most input sides;
  `dblclick`), `CCampPages::DebugFill` (slot 39, only the vtable refers to it), `CChatWindow` (base screen,
  `ui_chat_window.cpp`: players vector +0x150 of counted `PlayerMessage`s, front insert = the inlined `insert(begin(),
  x)`, overflow 0060A120), exchange `Create` / `Draw` (children constructed inline; `ui_exchange.cpp`),
  `CMpConnectScreen` (`ui_connect_screen.cpp`, layout in ui_screens.h; helpers call each other by address; slots 26
  0061C2D0 / 41 0061C420 too short to hook), `CMasterRequestBox` (`net::MasterLink` in net_classes.h; the address list's
  `vector<MpAddressEntry>` push / insert default, erase, entry copy / assignment / destructor).

`DrawWrappedText` (005FDFA0) walks text + "\n" with the game's `_ismbblead` / `isleadbyte` / `_mbsinc`; 005F8340 takes a
CString by value, which a naked forwarder cannot declare (C3068): the data pointer is handed over and the local detached
(`TextMetricsAt::TextExtent`). The original measured with GDI on a screen DC (GetDC(0079BC28)) and the font CInterface3D
+0x70 + font*4; ours measures with our own font (ui_font.h, `TextExtent`; P8, docs/notes/ui-native.md "P8 done"), scaled to
800x600 by integer maths as before; a word that did not fit before a '\n' goes on a line of its own (even an empty one).

**A-I22 (2026-10): 005E0000-00640000 finished.** Every hookable game function of the range is ported, in library.csv or in
dead_code.csv; what is left are 31 stubs of 1-3 bytes (plain RET / RET n, too short to hook). Groups verified O with the
state read back, by `verify_ui_small_members.cpp` and friends: the unpatched small members (default slot answers, setters,
constructors, handlers, exchange slots 42 / 43: net_pair_zone reaches both), the text and sprite draws (005FDE20 surface DrawText, 005F8340, 005E3700,
005E6FA0 sprite Place, the shapes 005E70E0 / 005E7680 / 005E7D60 / 005E99D0; 005F8340 and 005E6FA0 S), `CInterface3D::Create`
and `DrawOverlays`, the battle camera / global map mouse moves (005EB210 S, 00630010 S, 0062F950 O), the battle panel builders
(0063BAB0 .. 0063F500, 0063A560 key-down; 0063C050 S), `CCampPages::Create` (0060C870), the hero screen (00613EA0 Create,
00615D30 SetMode) and the connection screen (0061B2C0 Create, 0061D780 list draw; ui_mp_host reaches the list draw).

CSV: library.csv 12 rows (the chat line list, help topic / cache map nodes, `CFont` / `CRect` inlines, EH-only
destructors), dead_code.csv 0063C010, 0063F6E0. Forwarded on purpose: the Rebirth-patched callees (`CUiMesh::SetRectangle` /
`SetFrame`, `CUiRegion::SetRect`, `SurfaceDrawText`, `CHudWeaponBar::Highlight` 0063C050, `CHudMovePanel::ShowMode`
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
  the music, from the menu (1) it clears the server flag at the end; plays the movies under "Titlesfin" (00458270) (the original then switches
  font smoothing off for the session; ours draws the header unsmoothed instead, ui-native.md "Font smoothing"), parses "credits.scr" from "<0079BC94>outro.res" (one TitleLine per line) and starts
  "Movies\Titles.bik". `Draw` (0062BD80) takes the next line once the time passes `nextLineAt_` and the text stopped
  (`speed_` 0 or NaN).
- **View screen** (`ui_view_screen.cpp`): screen 7, a debug view (0x168, 00740840 / 0074080C): inert input 00741154,
  `UnitPickInput` (00740278, 0x20: a CDWordArray of picked ids: `LeftDown` pick / walk, Ctrl attack (how 6), Space
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
0x44: the lists at `+0x1C8 + 0x40` / `+0x1B4 + 0x40` stay 0). Reached with goldens (reach.txt): CanMakeSpell / CanMakeItem,
DeconstructItem, the trade / repair grid and maker Builds (ui_camp_make, ui_camp_deals, ui_gallery_deals), the repair
grid's draw (page 6; ui_camp_deals, ui_gallery_deals), the trade panel's click and RestoreRecord (ui_trade_hold,
ui_trade_restore, ui_gallery_deals: vanilla buys only with "give 0 exp" typed while the camp is open; given in the battle
it did not show in the camp), FindPaths (ui_gallery_deals: "z.gz1g" / "z.gz2g" set to 2 before `screen 4`,
then a click on gz2g's area at (140, 277) opens the objectives window with the ways), CGlobalMap::CloseScreen (ui_camp). Not
reached: a link click (regions 3 and up: a "z.<way>" = 2 way of kind 2 with a figure; it travels at once), the repair
grid with an item in it (only a worn item is offered, and no script command or key wears one). The map's camp button
(region 2) after `screen 4` makes the server fail ("Serv error during parsing command prevID=ff curID=4", the original
too), so scenarios end on the map.

Global map (`ui_global_map*.cpp`): `Create` (0062D350, 6.7 KB), `Update` (0062F050), titles (the original's `AllodTitle`,
`ZoneTitle`, `QuestTitle`: ours `PlaceTitle` in `ui_global_map_places.cpp`), positions (`ZonePosition`, `QuestPosition`:
ours `MapPosition`, (0, 0, 0) for a missing entry where the original kept uninitialised locals), `SelectZone` (RET 8, flag
is a byte). `SetWeather` (S) applied an 18-int table one index at a time from Create, every index of a group writing the
same colour: ours gives the three layers, the display colour and the clouds' ramp their final values once (the layers'
first colours and the display's 0xFF3C005A were overwritten before the first frame). `RefreshSelection` (00630670) draws the zone markers alone in colours 0xFF181818 + 0x101010 i through a
frame step 00458010(0, 0) into the backdrop; the click reads the zone back from the pixel. `FindPaths` (006312C0, S, RET 4)
is a recursive way search; `arrivals_` = STLport map<int, float> exit -> shortest "#passtime" sum
(`ui_global_map_paths.cpp`). game.exe's `GlobalMapZonesSetAtGrow` is CArray<GlobalMapZone>'s (elements
005FBF50 / 005FBD30); ours are std::vectors (push_back). `RightDown` / `RightUp` (S) drag (motion 007C7C60), `KeyDown` (Esc menu inline, '5' / '6'
quick save / load "saveq"). `CBattleScreen::Create` (00635250) builds 11 panels and 2 input handlers.

Camp pages (`ui_camp_*.cpp`): `LeftDown` / `LeftUp` / `KeyDown`, `MessageResult` (0x10001 takes the first
weapon back), `HelpTopic` (`ui_camp_input.cpp`; `turning_` +0x25C); `UpdateAction`, `CanEquip` (slot 43, AL), `CanMakeItem`
/ `CanMakeSpell`; trade pages: `DoAction` / `UndoAction` (trade pages stock the constructor's class template (+0x198)
lists 0069908x-006992xx with the sold records; cost_ off except page 1), `DeconstructSpell` ("Prototype" + a "Rune" per
modifier), `DeconstructItem` ("Instruction Weapon / Armor / Quick Item", "material.<key>" built with one
operator+ instead of two, a "spell container" through 00613820 instead of the inlined map insert) (`ui_camp_deals.cpp`); `PutOn`,
`TakeOff`, `TakeItem` (42, RET 8), `PutOnFromStrip` (RET 4), `Update` (turn via `_CIfmod`), `BestPartySkill` (45, P),
`TopicClicked` (44). Constructor slots `CSpellMakerPage::ItemAt` / `CConstructorPage::ItemAt` (`ui_constructor_slots.cpp`; pages 3 and 5).

Hero panels, trade panel, strips (`ui_camp_stats.cpp`, `ui_stats_buttons.cpp`, `ui_stats_rows.cpp`, `ui_equip_panels.cpp`,
`ui_panel_draws.cpp`, `ui_hero_panels.cpp`, `ui_trade_panel*.cpp`, `ui_item_strip*.cpp`):
- `CHeroStatsPanel::SetHero` stores +0x170 = height * 100 truncated (an edge height may differ at another precision).
  The panel's page 10 -/+ regions 6-13: a hold starts at -0.5 s with the capture, repeat after 0.25 s (ui_hero_stats,
  coordinates unconfirmed); regions 0 / 1 previous / next hero. `DrawPart` armour "%.1f" = the largest of the first six
  damage types' three-layer sums; a NaN keeps the old maximum. `CHeroEquipPanel::RightDown` does not mark the event
  used.
- Draw details: `CRepairGrid::Draw` puts models at depth 2^(3 - row) * 6; the constructor pages' highlight p = (cos(5 t) +
  1) / 2 stored as float, colours truncated and clamped (an integer edge may differ at another precision; ui_camp_deals
  pages 3, 5, 6). Lists are `UiItemList` = `std::vector<CItem*>`.
- Trade panel (`ui_trade_panel.cpp`; ui_trade_restore): `SnapshotRecord` copies floats as dwords, `Settle` (the all-branch
  tests the low byte of `keep`), `RestoreRecord` (HealSlots by old hp / old max * new max - Reduce(new max), the maxima
  stored as float). `LeftDown` (`ui_trade_panel_input.cpp`; ui_trade_hold with `ldown` / `lup`, purchases only in
  the Rebirth run): regions 0-3 scroll, 4-9 a school level, 10 science, 11-33 a learnable perk, 34 up a held perk.
  `FormatPrice` is "%d" / "%.1fk" / "%dk" / "%.1fm" / "%dm" / "%.1fg", negative as 0x7FFFFFFF.
- Item strip (`CUiItemStrip`, the forwarding strip of camp and exchange): `Update` scroll physics compared to 4 ULPs;
  `SetupFigures` (name list shares +0x194 with `filter_`); figure strips (00741BF4) `DrawFigures`, `CountFigures`,
  `CFigureStrip::LeftDown` (`ui_item_strip_setup.cpp`; ui_hero_screen mode 10, unconfirmed that its clicks land).
  the item predicates `Is*` (ui_screens.h; 005EE050-005EE130, P), `CItemScreen::MoveItem` (RET 0x18) and `ReturnAll` (RET 8;
  `ui_item_screen.cpp`), `ScrollToItem` (RET 4).

Multiplayer hero screen (`CMpHeroScreen`, `ui_hero_screen*.cpp`, `ui_hero_figure.cpp`; O unless marked). Modes: 9 hero list
(rows 9-18; 1 continue 006183B0, 5 new hero, 6 rename, 7 delete "lmp_deletepers", 8 the hero's inventory = the camp screen
built inline with `fromHeroScreen_` 1), 10 new hero (1 create 006186F0, rows 9-19 the voice), 11 equipment (1 next 00617A50).
Scenarios ui_hero_screen, ui_hero_stats (RefreshStats), ui_hero_delete (AddNewHero, DeleteHero, record life cycle; unconfirmed).
`Weight` (53, double in ST0), `CanEquip` (43, AL), `TakeItem` (42), `HeroRecord` (51), `RefreshStats` (47),
`ChooseFigure` (52: +0x1BC / +0x1C0 are two item strips whose `selected_` (+0x17C) is the chosen figure; the strip listing
the prototype picks the voice list; the voice (text 6) is the list's first file, read even when the list is empty).
`KeyDown` `FinishRename`: the name before " | " plus " | " and the typed text; paging by 10 / 1 heroes or 11 / 1 voices;
`Rename` uses Mid (006FD9B4). `PlayVoice`: prototype -> sound set map 007C8A6C -> a random entry, flags 0x2282 (ours counts
the entries instead of building the temporary vector). `Continue` builds `CMpConnectScreen` (00741CD0) inline. `CreateHero`:
attribute sum vs 00741C98 equal or unordered, a name. `Draw` previews at depth 553 / 400; `Update` turns by fmod 007C7F10 and
tints the create button; `Destroy` is the destructor body (vtable 0074214C slot 0 00633240 calls it). `AddNewHero` writes file
"<highest *.mp title + 1>.mp" (00619920 when full; `RefreshHeroScroll` shared with DeleteHero); `MessageResult` 0x10001
deletes in mode 9. `DrawHeroList`: a hero record is a CParty whose first member's text 0 is the name. `MakePreview` (RET 4;
skill animation 0x21 from a random frame, controller slot 14 `RestartAnimation(float)`). The original's forwarders
`RenameAt` / `PlayVoiceAt` / `ContinueAt` / `CreateHeroAt` are the member calls.

Exchange (screen 9; multiplayer only, no scenario): `CExchangeScreen : CItemScreen` (+0x190 scratch party, +0x194 / +0x198 the
client's parties_[1] / [0], strip, figure strip, trade grid, money field, figure names); `ui_exchange.cpp`: `ExchangeDone`
0062D210 (37), `CloseScreen` 0062C8F0, `Update` 0062C930, `LeftDown` 0062CF70, `TakeItem` 0062D100 (O); the empty
`MessageResult` 0062C920 is the container's default and `CanEquip` (0062D200) always true. The client's trade
calls go through their addresses (originals_ui.h `Client*Original`).

Hazards and facts:
- **Item strip**: `ItemAt` truncates `(x - 50) * 0.01f + position` with `_ftol` (the sum not rounded to float first), `SetTab`
  settles the old tab at `_ftol(pending + position)`, releasing an arrow (`SettleScroll`) keeps the fraction of the pending scroll
  and adds or removes a whole slot when the fraction is under half; `Update`'s branch-free FNSTSW selects pick the speed on NaN.
  Filter 0 puts quick items (0x3006) in tab 2 with weapons, armour and spell containers; tab 3 holds class 0x3007, so clicking tab
  3 never equips quick items.
- **Battle screen Create** reads its part of a save (007AFDF8; from 0x47 an int and a string appended to the top panel's text and,
  on page 0, the message log; from 0x62 +0x18C and +0x188), builds the unit panel, map, clock, movement and top panels inline
  (owner +0x148; each panel a `new` of its class with the interface and the screen)
  and puts the "pers <name>" texts into the map<int, CString> at +0x1F0.
- **Global map**: positions are `centre + turn(local)` (centre = layout point (400, 280) at depth 10, each step stored; `turn` =
  spin about z by +0x1FC times a tilt of 0.8 pi about x, inlined with its own operand order: `TurnProduct`); markers come from the
  player's variables ("z.<zone>", "q.<zone>.<quest>" = 1 by bit compare with 1.0f, ways out whose "z.<way>" equals 2 by FCOMP:
  unordered counts as equal); arriving from anything but the camp autosaves to "saveb" when 0079BE7C is set; Update draws the
  weather (00499F10) into texture 4 (the original: its locked surface under the critical section 007AEE78; ours on the GPU,
  `render::DrawClouds`, renderer.md section 7). Create calls SelectZone before it creates
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

Behaviour worth knowing: `MessageResult` has the load window inline; `ShowBriefing` (34) is "briefing <name>"; `SaveState`
(40) writes one zero dword; `OpenTopics` makes five rows 210..570 x 440.. with "goodbye" appended; the mouse move sets cursors
0xD / 0xE / 0xF; `KeyDown` (`ui_briefing_keys.cpp`): action 4 opens the objectives window when a "q.<zone>.<quest>" = 1 lies
in this allod, 0xB the camera to the leader, '5' / '6' "saveq". `FindTopics` (S, `ui_briefing_topics.cpp`): "b.<person>.<topic>"
= 1 by bit compare; a network client sorts them; the last "constr..." topic last; `TopicTitle` maps "constr..." to "briefing
constr". `StartDialogue(immediate, remember)`, `StartPhrases` (camera saved to +0x1F8, look-at +0x204, 0066F840(3, 0) to the
server), `PlaySpeakerAnimation` (the controller's +0x978 callback 006023F0), `ViewSpeaker` (message 0x0F), `ViewPreset`
("#camera n"), `HideSpeaker` ("#hide"), `ShowItem` ("#show"), `OnUnitJoined` (29: the leader's "b.<zone>" topic starts at once),
`PersonTextMap::Load` (also the battle screen's setup), `FinishDialogue` (completes "b.*.<topic>" through 00592090's
address), `MovePerson` (RET 0x28).

Meshes: 0 / 1 the topics' backdrop and frame (shown in state 1 only), 2 / 3 the text's backdrop and frame, then the people's
slots. Update hides them all each frame, then state 1 shows 0 and 1 and states 3 / 4 show 2 onwards (not 0 and 1: showing every
mesh drew the topics' frame inside the text's, W5; ui_widescreen_dialogue's phrase frame and ui_gallery_base show it).

Layout: speakers +0x1A4 hero, +0x1A8 person, +0x1AC third; places_ +0x1B0 / directions_ +0x1D4 ([0] person, [1] leader, [2]
third), thirdPlace_ / thirdDirection_ +0x210 / +0x21C, personHome_ / personHomeDirection_ +0x228 / +0x234, firstPhrase_ +0x244,
dialogueText_ +0x248, speaking_ +0x254, shown_ +0x278 (BriefingShownItem list), immediate_ +0x27C, autosave_ +0x27E, people_
+0x284 (STLport vector<int>), markPhase_ +0x290, speakers_ +0x294 (12-byte BriefingSpeaker), text_ a CUiScrollText;
the rewards are the quest database's `QuestBriefingRecord` (quest_records.h, 0x6C: exp, money, the lists by
`ListKind`, specials +0x68). The "constr" topic's number is `Mid(7)` (006FD9B4, not
Right). Helpers: `BriefingVariables` (00578C40 into a 100-bucket table, 00581790; walked with 005E0DB0), `LocalPlayerZone`
(00578A90). player_messages.h and ui_briefing.h are kept apart by `player_out_mark.h`. Inlined in the original, not functions: 00609A80, 0060B490 / 0060AC00, 00609A40 (pool
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
(S); `MouseMove` (00639D60 S), `UnitName` (0063B920, RET 8), `LeftUp` / `LeftDown` (O; ui_hud, world_client,
ui_describe); the message slots in `ui_battle_slots.cpp` (`SaveState` 40 S through a recorder, `CloseScreen` 26,
`ReadyToStopPolling` 35 AL, `ItemsPicked` 28, `UnitLeft` 30, `UnitJoined` 29: placed by the local player's record order, selected by the record's
+0x778, `MessageResult` 27, `GameOver` 31; 27 / 28 / 30 / 31 not reached by a scenario); orders `OrderAt` (RET 0x14),
`ToggleMember` (RET 4), `AttackUnit`, `OrderDeadUnit` (RET 8), `UseOnUnit` (RET 0xC), `UseAtPoint` (RET 0x14); `Speech` (32),
`QuestVariable` (33); `LeftDouble`, `KeyUp` (the order key of actions 0x11..0x16, `orderKeys_` +0x18D in the jump
table's order 0, 1, 5, 4, 3, 2), `Wheel` (map, unit panel, message log, top panel); the columns' `Highlight` /
`ClearHighlights` (every drop of the pending action is `SetPendingAction(kPendingNone, -1, -1)`: the original's inlined
copies of it do the same stores), `CUiMesh::SetScale`; `CHudSpellColumn::Click`,
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
  a sound; only a spell item with its data (+0x44) sets the action; only wand (QuickItemCategory::Wand, 5) and potion (QuickItemCategory::Potion, 8) quick items go on.
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

**Tooltips for regions without a button** get SetTip(0), a fresh 0x184-byte tooltip with the empty text 0079B298 whose constructor 0065E320 writes the arguments (+0x164 / +0x168) and size (+0x174 / +0x178) only for a non-empty text: leftover heap bytes, compared only for tips with lines. FCOMP / `TEST AH,0x40` compares treat NaN as equal (damage "Unknown" when all weights are zero; quest values 2 and 3).

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
`CHudPortraits::Update`, `CHudClock::Update`, `CHudTopPanel::Update`. The panels' destructors are the implicit ones;
`PartyIds::Equals` 006381E0 (P, every frame).

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
  rounding changed the wobble delay in 4 of 5304 calls). The orders' nods and the speech shakes, inlined at each site in the
  original, are `CHudPortraits::Wobble(index, kind)` (the same draw and store). Kept: the sex letter ("m" / "f") keeps the previous portrait's value
  for a figure that is neither "unhuma" nor "unhufe"; AddPortrait's count % 4 switch leaves the texel offsets uninitialised for a
  negative count (ours 0) and indexes the column's frame and restB_ by count % 6. The multiplayer branch (no scenario) calls the
  name maps' find_or_insert directly (00514880 / 0053D340).
- Unit info: "consider_more<d>" / "consider_less<d>" are looked up in place (one CString as key and result); a damage type of 7 or
  more keeps the move text; damage values go through `__ftol2_sse` instead of `_ftol` (both truncate). `ShowParts` reads six body
  part records (+0x374, 0x1C bytes) with label positions from 00742434 / 0074244C. ToggleFlag's third frame value 0.48046875 is
  unreachable. Texts go through DrawText 005FDE20 and Place 005E6FA0 by address.
- Top panel: `ShowItems` builds page 1: "format_money" with the local party's +0x50 when above 0, then the local player's item
  list (CPlayer::items_ +0xE0) merged by kind (slot 14) into the client world's copies (0055E940), one FormatItemName line each, the
  copies released; into the log when page 1 is up. The original builds lines with CString operator+ temporaries; ours appends to
  one CString.

## 8. Objectives, save/load, options, help windows and the controls

Rectangles from `menus.reg`: save/load OK 163..237, Cancel 563..637, Delete 363..437 (y 526..574), list rows 108..370 x 108.. in
24 steps; options OK 213..287, Cancel 513..587, groups 100..250 x 156.. in 24 steps, rows x 250..520. All O unless marked.

Scenarios by window: save/load (`CSaveLoadWindow`: input, `Draw`, `Update`, `KeyDown` Esc 0x100001 / 0x110001, `IsReservedSave`
P) ui_hud, ui_saveload; `Confirm`, `AskDelete`, `LoadSelected`, `DeleteSave` (looked up through the game's GetFileAttributesA
import, which harness runs redirect to harness_saves; called by address `Call*`) ui_saveload, ui_saveload_delete; `MessageResult`
(the caller 005F590D ignores EAX); `SaveEntry::MakeTitle`, `ZoneTitle` / `AllodTitle` ("zone " / "allod " with the space,
007941F8 / 00794200; else "save_camp" / "Unknown"; S; `ui_saveload.cpp`); `QuickSave` (`__fastcall` slot, DL frame step) /
`QuickLoad` (newgame's savea); `Build` (00653DD0 save/load, 006503C0 options; `bindings_` / `settings_` are vectors,
`groupNames_` a `std::vector<std::string>`; `DestroyOptions` 0062B180); options (`COptionsWindow`: input, `Draw`, `Redraw`, `HelpTopic`
P, `Update`, `KeyDown`; ui_hud; `RefreshRows`, `ShowGroup` (RET 4), `BuildSettings`, `BuildBindings`, `LoadSettings` (S; sets
every control twice), `Apply` (`ui_options.cpp`; `Call*` forms by address); Build runs all but Apply (ui_hud also clicks group
2); Apply: ui_main_menu's OK; key remapping: ui_options_bind (Enter, a taken key's box, cancelled), ui_options_remap (SV8:
Enter, `dblclickregion`, a taken key's box, OK, then the keys used in the game, the hero's moveMode in the digest) and
ui_options_remap_load (the keyboard.ini ui_options_remap saves, written by `tools/make_keyboard_ini.py` into the overlay,
loaded at start: the restart half)); controls (`CUiScrollbar` `TakePosition` S, `Update` also the message log, `CUiSlider::SetRect`
slot 26 RET 4: every frame; `CUiTooltip`; `CUiScrollText::Tokenize` / `DrawLines` RET 4 (`ui_scroll_text.cpp`; the message log);
`CUiEditField::KeyDown` S (`ui_edit_field.cpp`; no scenario types into one); `CUiScrollingSurface::RenderText` 0065F020: the
titles' RTF text as glyph runs (render_rich_text), ui_main_menu, render_titles_present, ui_titles_scroll); `CObjectivesWindow` (`Build`, `ShowZone`,
`ShowQuest` `__fastcall`, `DrawQuestList`, `OnLButtonDown` in `ui_objectives_input.cpp`: OK requests the zone at the nearest
exit, travel time 00578700 forwarded; `LoadQuestPlaces` 0064EA80 S: map.txt "#quest <name>" pairs, or the quest record's
floats for a client; the forwarder is `FillQuestPlaces`; ui_hud's TAB, ui_objectives); help window (`ui_help_window.cpp`:
Build "tutorial <topic>", "#screen" per page, "tutor <key>" texts, Close, the pulse, clicks, keys; the page draw 00659420 is
Rebirth-patched; ui_help with F1).

Key handlers run only when the window is not sliding in, pass the key to 00601820 first and set the input side's handled flag
(+8) for the keys they use. A save window edits the name (+0x188, caret +0x190; the first edit of an entry other than "saveq" /
"savea" / "saveb" sets +0x18C); options: while a binding row waits (+0x3F4) the next scan code (flags & 0x1FF) is looked up in
the key names 007857B0 (88 x 0x48), a key bound to another row asks "key_already_mapped", else CommitBinding (00652000) and
RefreshRows. OK only copies the window's table into 0079BF70 (Apply); config\keyboard.ini ("<key name> <action>"
lines, 00462DF0) is written on the interface's shutdown (005F4F90), so a game that ends without it keeps the remap for
that session only; settings go to the registry on exit (harness runs skip RegSetValueExA, slot 0073B018). Slide-in eases the model from y 700 to its place (550 / 620 save/load, 480 options): `e = 2s^2` below s = 0.5, else
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
`Draw`, `Describe`, `KeyDown` / `KeyUp` (`key ctrl`), `Build` (O); `ItemDamageLists` 005F13A0 (EI_Plugin patches a JMP at
+0x61: ours runs in vanilla only; S); `CItemScreen::DrawItem` 005ECA10 (S; drawn twice per frame in the verify build);
`CUiMesh` `Framed`, `Ring` (RET 0x2C), `Disc` (RET 0x24), `ResetColours` (`ui_mesh_build.cpp`; Commit's +0xC4 compared as
"points at the current indices"; S with arrays poisoned 0xCD; every scenario); surface helpers `CaptureScreen`, `Rgb565Copy`
(the save picture), `Greyscale` ((2r + 4g + b) / 16), `SwapContents` (O), `PixelAt` (`ui_surface_pixels.cpp`; S; ui_screens),
constructors / destructor body (`ui_surface_create.cpp`; O; the picture window is dead); `DrawHeroFace` 005ED7B0 (EI_Plugin's
fix at +0xD9 keeps the original under the plugin), `ItemFigureTexture` (256x256 from "<name>.mmp"; the original
unpacked it via converter 007AF06C slot 5 and packed 1:5:5:5 under the device lock; R28: drawn on the GPU,
`render::DrawIntoTexture`, as CopyImage is), `CUiWindow::DrawHeroPreview` 005EC300 (O) (`ui_item_view.cpp`;
S on pixels); `CItemScreen::CreateBase`, `CItemIconWindow::Build`, `TrimItemModels` / `TrimItemTextures` (O).

Facts: the camp creates the description panel (+0x164 at 600, 100) and the help panel (+0x1DC at 0, 100); the owner (+0x14C) is the
item screen and +0x150 an item list holding a copy of the described item (00661DB0). `DescribeItem` dispatch: kind 0x3007 with +0x4C
1 material, 2 with +0x50 0-2 blueprint, 3 spell record, 4 modifier record; quick item category 8 -> 005EF300; a carried spell (+0x44)
while Ctrl is down, or a spell container (0x3008) -> 005EF5D0. The statistics (UnitStats methods, WeaponStat84 / 88,
WeaponPrototypeSkillBonus) are called by name and truncated with the game's `_ftol` as they come back in ST0 (unrounded: those
rewrites return double). The item screens keep model (+0x170) and figure texture (+0x180) caches (STLport
map<string, {object, last use}>, limit 30, or 20 with 00784754; `CItemScreen::models_` hides `CUiWindow::models_`, the window's
UI models, in every screen derived from it: the hero screen's mode switch reads `CUiWindow::models_`); each item has a turning state (vtable 007403F8, 0x18 bytes, at the
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
`CUiRegion::SetRect`, `CUiMesh::SetRectangle / SetFrame / Place`, 005FDE20, 005F8340 and the panel builders by address;
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
- The item screens' slots 42-54 (what the strips and panels call on their owner: TakeItem, CanEquip, TopicClicked,
  BestPartySkill, RefreshWeapons / RefreshStats, Next / PreviousHero, HeroUnit, HeroRecord, ChooseFigure, Weight, Price) go
  through `CItemScreen::Virtual*` (ui_item_info.h; ItemScreenClass names the same slots); a panel's Build (slot 18) through
  `CUiWindow::VirtualBuild`. The save window's entries (+0x164), the HUD portraits' slots (+0x158) and the quick items'
  entries (+0x158) are typed containers (`std::vector<SaveEntry>`, `std::vector<HudPortraitSlot>`,
  `std::vector<Entry>`).

## CMpHeroScreen::LeftDown

ui_hero_screen_click.cpp (input click). Nothing while a hero is being renamed; after the base handler, by
mode: the hero list (9: back to the main menu, continue, the rotate arrows, new hero, rename, delete (a
question), the hero's inventory (the camp screen with fromHeroScreen_), list rows), the new hero (10: back,
create, the arrows, defaults, voice rows) and its statistics page (11: back, next, take the equipment back).

## CMpHeroScreen::KeyDown

ui_hero_screen_click.cpp (input key). After the container's handler, unless a child used the key: Enter
ends a renaming (FinishRename), else continues (mode 9), creates the hero (10) or goes on to the next page
(11); Esc ends a renaming without keeping it (the list redrawn), else goes back to the main menu (9, as
region 0), to the hero list (10) or to the new hero (11, as region 0); PgUp / PgDn / Up / Down move the hero
selection by 10 / 1 (mode 9) or the voice by 11 / 1 (mode 10, the voice played). A key used is marked (other
modes leave the paging keys unused).

## CMpHeroScreen::MakePreview

ui_hero_screen.cpp. The preview cleared; the local player (given a "Hero" of "Human Hero" when it has no
record) hands its first record to the client world; the preview becomes the client unit with that record's
id, its prototype index stored; when there is one: skill animation 0x21 (if any) set to start at a random
frame (timeGetTime at 15 frames a second minus random % 51), the figure shows no weapon, refreshed, its
animation restarted at the current frame - 1, the record's statistics set and the controller's hit virtual
called.

## CMpHeroScreen::ChooseFigure

ui_hero_figure.cpp. The new hero takes figure (unit prototype) `prototype`: the local player's first record
handed to the client world and its strength, dexterity and intelligence and height kept (25, 25, 25, 0.7
without one); the party cleared and given a "Hero" of that prototype with its starting equipment; the
attributes (base and current) reset to 25 and the height to 0.7 on the hero list (mode 9), else the kept
ones; each figure strip selects the prototype when it lists it (-1 otherwise); the equipment panels show the
first weapon, quick item and spell; the preview, the statistics panel's and the trade panel's hero rebuilt;
text_ = prototype; the voice list of the strip that lists it (voiceLists_[1] for names_, else [0]) from row
0, its first file the hero's voice (text 6); the voice scroll bar's range and position 0; on the new hero
page the voices drawn.

## CMpHeroScreen::AddNewHero

ui_hero_screen_click.cpp. Region 1 / Enter of the statistics page: the new hero keeps only the chosen
weapon, quick item and spell container (the others removed from its lists; the shown weapon 0, or -1
without one), and a spell container whose spell the hero cannot use (HeroCanUseSpell) goes back to the
local player's items; a new hero record is appended (the vector's own growth when full) and selected, its
file named "<the highest number among the folder's *.mp titles + 1>.mp", given the local party and saved;
the list's scroll range, the list drawn, mode 9.

## CMpHeroScreen::SetMode

ui_hero_screen_mode.cpp (Rebirth-patched, reproducing WideScreen, so hooked in both launches). The screen
laid out for `mode`. First everything off: the menu model's parts, the nine child windows, every sprite and
surface, the regions after the ninth removed and the others' tips emptied. 9, the hero list: the model's
"but03" and "button03_multiplayer", the list's scroll bar, all sprites but 7-9, the four frame quarters
(sprites 3-6), the buttons' regions and tips, surfaces 0 and 1, the name edit off, the selection frame
(sprite 2), ten list rows as regions and the selected hero shown. 10, the new hero: the voices' scroll bar,
sprites 3-9, the quarters, the bars at 300, the statistics panel and both figure strips, the regions and
tips, the name edit on and editing, surface 2, the selection frame, eleven voice rows and the voices drawn.
11, its statistics: sprites 7-9, the bars at 400, the equipment, spell, trade and statistics panels, two
regions and three tips, surface 2, the name edit off, the trade panel's hero 0. Last, for every mode: the
description panel emptied, the statistics panel's page `mode`, mode_. With Rebirth's WideScreen the curtains
(sprites 19 and 20, when there are 21) are then shown in modes 10 and 11 and hidden in 9.

## CMpHeroScreen::CreateScreen

ui_hero_screen_create.cpp (Create; Rebirth-patched, reproducing WideScreen; hooked in the vanilla launch
only, as EI_Plugin.asi patches it too in a Rebirth launch). The game's resources taken for a network game;
the menu model with its multiplayer button; nine regions; the item screen's base; the sprites (0, 1 the
buttons, 2 the selection frame, 3-6 the quarters "campm1".."campm4" around the figure, 7-9 the new hero's
buttons, 10-12 frames, 13, 14 backgrounds, 15-18 the commands' buttons); the description, statistics,
equipment, quick item and trade panels; the list's scroll bar; the figures a new hero can take: every
creature record whose unit prototype's figure model is "unhuma" (defaults_, voiceLists_[0]) or "unhufe"
(names_, voiceLists_[1]), a voice "<lmp_male_voice / lmp_female_voice> <n>" only when the record's flag is
set; both name lists sorted, the women's then reversed; the two figure strips; the rename and name edit
windows; the voices' scroll bar; four surfaces (the list, its commands with the four "lmp_pers_..." texts,
the voices, the name being changed, hidden); the saved heroes reloaded from <data>"mp\"; the list's scroll
range, the list drawn, the first hero selected (-1 without one) and mode 9; with Rebirth's WideScreen then
the two curtains over the screen's margins, hidden (widescreen::AddCurtains; sprites 19 and 20).

## CMpConnectScreen::CreateScreen

Create (Rebirth-patched; this is the vanilla body, hooked in the vanilla launch only). The screen over
800 x 600: the "saveload" and "scrollbar" textures, the stone menu model ("unmoco2" with
"MainMenu00labels", parts "but03" and "button03_multiplayer") at (400, 350, 8) turned about x by pi / 2 *
1.2; the regions (above) with tips 0x9E98, 0x9E99, 0x9EA2 and 0x9EA3 on the first four; sprites 0 / 1 the
two buttons, 2 the selection bar (plain 0xC8A06800), 3 the sort arrow (the scroll bar's texture, hidden), 4
/ 5 the unlisted and password switches, 6-9 the game type tabs (their names "lmp_allod_name_%d" into
allodNames_, the first selected), 10 / 11 two bars, three frames and three dark backgrounds; fifteen row
regions; surface 0 (100, 100)-(700, 500); the scroll bar, the player slider (position 5, maxPlayers_ 6) and
the name edit (the first hero's name to its '|', at most 10 characters, trimmed; not focused) as children;
the 22 "lmp_connection_%d" texts; the client started; the queries read from config\addrbook.ini (a chunk
archive, chunk 1; an extra word of 0 becomes a random one xor the tick count) and given to the client when
the file opens; nothing selected, the list rebuilt and drawn, the help topic, and surface 1 with text 8
("connecting").

## CMpConnectScreen::DrawListVanilla

ui_connect_list.cpp (Rebirth-patched: WideScreen and ExtendedServerName reproduced behind their flags; the
screen's other members call it through its address). The list drawn on surface 0. With row 0 (the new game)
selected its controls show: the player slider and the name edit enabled (and editing), sprites 4 and 5 and
their regions' tips (0x9EA4 / 0x9EA5, set once: newGameTips_); otherwise they are off and the tips empty.
Texts 10 and 11 by the two buttons, the column titles (texts 12-15), the sort arrow (sprite 3: turned for an
odd key, at its column while a key is set) and a line under the titles (a pen of the text colour); 15 rows
from scrollTop_ in the colour of the entry's state (g_connectTextColour: 1 0x31B3FF, 2 0x31E3EE, else
0xA7D7E4): the new game row "<text 7> <the name typed>", the others name, players "n/m", the game type's
allod name ("?" for 4) and the ping; the selection frame (sprite 2) on the selected row. Then the selected
entry's details at the right in 0xA7D7E4: for the new game the game type tabs (sprites 6-9) with their
names, texts 0, 2 (with maxPlayers_), 3, 16 and 17; for a server its name, players, game type, the map's
title wrapped over up to three lines, address, ping and, with a password, text 21.

## CExchangeScreen::CreateScreen

ui_exchange.cpp (Create). Builds the window: texture "campinfo", regions 0 accept, 1 take back, 2 leave
(tips 0xA028, 0xA029, 0xA02B), their four button sprites (the third hidden, the leave button shaded 0.5),
the item screens' base with the icon window's bars at y 300, a scratch party (made), our offer (the
client's, cleared) and the partner's, then the children: the description panel at (600, 100), the local
party's item strip (forwarding, action 1, items), the players' figure strip (top, no arrow tips), the trade
grid over both offers (mode 5) and the money field (0..200 x 0x15E..0x16E, empty), and four 200-wide
surfaces.

## Container draws

Interface draws that still talked to the old Direct3D device without handing anything to the new
renderer (ticket R5, docs/notes/renderer.md sections 5 and 8):
- the container windows' base draw (CUiContainer): stage 0 set to
  `kAlphaPassStage`, then the window's own sprites and models and its enabled children;
- the draws of interface screens 3 the base / briefing screen,
and 2: the menu frame step, (screen 3) a mouse move to the input
  side, a depth clear, then the children;
- the hero equipment panel's draw (CHeroEquipPanel): the two
  highlight frames placed (the shown weapon, the selected quick item), the weapons' (left) and quick items' (from the
  right) item models, and with the body (the camp) the seven
  equipment slots' models inside depth clears (all to 0, the slots' rectangles to 1, all to 1
  afterwards), as the hero preview and the trade grid do;
- the alpha reference set to the window's +0x1EC.
Every state set goes to render::g_renderState; every depth clear to render::ClearDepth (through
`ClearUiDepth`, ui.h; there is no old device, R27c). The item models go through
and the mesh draws, which hand over already.

## Cursor in the frame

The software mouse cursor in the frame (thiscall on the cursor object; called
by the frame function after the scene and the interface, before the present, when the
display option's software cursor bit (the renderer's +4 bit 0) is set). The DirectDraw version copies the
cursor's screen rectangle (updated) from the render target into the background
surface, combines the cursor image (the part `from_` of it) with it in the composition surface
((background & mask) | image) and copies that back to the render target; `drawn_`
keeps the rectangle drawn. Ours hands the image and its mask for the same rectangle to the new
renderer (render::Cursor), which draws it in every frame it presents. See docs/notes/ui.md
("Cursor") and docs/notes/renderer.md.
Also the cursor's draws straight to the primary surface under DirectDraw: the cursor
thread (redraws the cursor on the primary every 41 ms between frames) and the background
save. Ours has neither the background nor the composition surface (005E60B0 made them: nothing drew into them
since R11a, and R30a deleted them); the background saves and
the composition's holder blits are left out (the two
ports, which nothing of ours calls), and the cursor thread moves the cursor between frames by
presenting the last frame again (render::CursorBetweenFrames);
not while the image is hidden (the movie players and the loading screen hide it).

## Battle HUD corner panels

The battle HUD's two corner panels, the game clock (CHudClock) and the movement panel
(CHudMovePanel): their setups, which make the panels' sprites and their
regions about the layout's bottom corners.
Rebirth patches both for its WideScreen layout, reproduced behind the flag (ui_widescreen.h):
the clock at the screen's right edge, the movement panel at its left (the panel offset while
they run, their rectangles moved by EdgeShift). The callees Rebirth patches too (the sprites'
Place and SetTriangle, the regions' AddPoint, the panel's ShowMode)
are called through their addresses. See ui.h ("Battle screen and its HUD panels"),
docs/notes/items_spells.md and docs/notes/rebirth.md ("WideScreen").

## Battle HUD sliding panels

The battle HUD's two sliding side panels, the map (CHudMap) and the unit panel (CHudUnitPanel):
their layouts at the current slide offset, their per-frame updates
(Update) and their setups.
Rebirth patches all of them for its WideScreen layout, reproduced behind the flag (
ui_widescreen.h): the map at the screen's right edge, the unit panel at its left (the panel
offset while the setups and layouts run, their layout constants moved by EdgeShift). The callees
Rebirth patches too (the sprites' Place and shapes, the regions' SetRect and AddPoint) are called
through their addresses. See ui.h ("Battle screen and its HUD panels"), docs/notes/items_spells.md
and docs/notes/rebirth.md ("WideScreen").

## Battle HUD unit panel draw

The battle HUD's unit panel: its draw (CHudUnitPanel).
Rebirth patches it for its WideScreen layout, reproduced behind the flag (ui_widescreen.h):
the panel offset while it draws (until the window's base draw), the model's x moved left by
EdgeShift, the depth buffer's rectangles scaled without the margin. The callees Rebirth patches
too (the sprites' Place, the layout scaling) are called through their
addresses. The function drives
the original's Direct3D device itself (IDirect3DDevice7::Clear, Get / SetTextureStageState); ours clears the new
renderer's depth (`ClearUiDepth`) and sets render::g_renderState, as the hero preview does (ui_hero_preview.cpp).
See ui.h (CHudUnitPanel) and docs/notes/items_spells.md.

## Item description drawing

The item description panel: what the camp, the multiplayer hero screen and
the exchange window write about the item or statistic under the mouse. See ui_item_info.h and
docs/notes/ui.md ("Item descriptions").
Each function draws text lines into the panel's surface 0 between Begin and End Text
(Rebirth-patched, called through its address) for single lines and DrawWrappedText for the wrapped
title and description. Values the game truncates with its _ftol are converted the same way; values
of the statistics functions (UnitStats, WeaponStat84 / 88, WeaponPrototypeSkillBonus) are truncated
as they come back in ST0: the ones the original leaves unrounded are declared double there, so no
rounding happens in between. Compiled with precise floating point so that products keep the
original's order.

## Save / load window pictures

The save / load window's saving, its entry list and the selected save's picture. See ui.h
(CSaveLoadWindow) and docs/notes/renderer.md (sections 6 and 7). The window shows two 256x256 5:6:5 textures (a
CTexture with its holder): objects_[1], into which ShowPicture draws the selected save's shot.sav (256x192
pixels), and dialog_, which FillEntries draws from the escape menu's 256x192 copy of the
screen for the new save of a save window. Both are drawn into on the GPU (render::DrawIntoTexture, the 0x18000
bytes as stored: 5:6:5 rows of 512 bytes, at the texture's top; the rows a short shot.sav lacks keep the last
picture's). The window keeps the copy (newPicture_), which SaveGame writes to the new save's shot.sav, so the
file's bytes are the original's.

## Surface blit

A window surface's draw (CUiSurface::Blit; called by CUiWindow's surface pass and the interface's draw for
the backdrop and the frame rate surface): when the surface exists and is shown (shown_), the part of
its screen rectangle (rect_) inside the screen (the interface's width_ and height_) is blitted from the
surface (holder_) onto the screen with the matching source rectangle, with the source colour key background_
when its blend is SurfaceBlend::Keyed. The original's format word had a stretch bit (2) too, which no surface
set (every surface is format 4, opaque, or 5, keyed; bit 4 is the shown flag). The new renderer (src/render)
draws the surface's canvas there (render::ui::DrawCanvas); the holder has no DirectDraw surface and no memory
(the original's Blt is gone with the DirectDraw fallback). See docs/notes/renderer.md section 6 and
docs/notes/ui-native.md, "P8 done".

## Surface construction

The window surface's constructors and destructor (CUiSurface): one for a window showing an .mmp
picture, the destructor, one taking (interface, layout left, top, right, bottom, blend, scale flag) and one
taking (interface, layout rectangle, blend; always scaled). Both scale the rectangle to the screen
(Rebirth's wide-screen patch sits there), make a surface holder of its size, add height * pitch to the
surface-bytes total, clear the surface to 0 with a colour-fill Blt and set the shown flag. The holder has no
DirectDraw surface and no memory (surface.cpp): the surface's pixels are its canvas on the GPU, made black with
it (CUiSurface::MakeCanvas, P8), so the colour fill is left out. Making a window asks DirectDraw for nothing. (The picture constructor
has no caller in game.exe or the mods: config/dead_code.csv.) The original's unwinding when the holder's
allocation throws (freeing the holder's memory) is not reproduced: that throw ends the game.

## Surface pixel work

The interface surfaces' pixel work (CUiSurface, ui.h): copying the screen into a surface, a 16-bit 5:6:5
copy of a surface at a given size (the save game's picture) and greying a surface in place (the escape
menu's background). The escape menu's drawing (Rebirth-patched) runs all three when the menu opens. See
docs/notes/ui.md, "Item descriptions, item view". All three run on the GPU (R28a): the copy is drawn into the
surface's canvas (since P8 every window surface's only copy), the greying is a pass on it, and the 5:6:5 copy is
drawn at its size and read back (docs/notes/renderer.md section 6, "Screen copies on the GPU"). PixelAt
reads the surface's pixel from the GPU and SwapContents puts its canvas aside on the GPU. A surface without a
holder is left alone.

## 12. Behaviour moved out of header comments

Long header comments of ui.h, ui_camp.h and the other UI headers, moved here by the comment cleanup. The header keeps the gist.

### CBattleScreen::LeftUp

Input side `this` is the window (ui_battle_update.cpp). The left button released: nothing while a portrait
click picks a player; without a press on the view the container's handler; a press without a drag, or on a
network client, is a click on the unit under the point (units with flag 0x400000 only) through SelectUnit; a
drag selects the party units inside the box : the world's selection cleared, the box marks and portraits'
markers reset, each unit inside controlled with its portrait marked and its face turned (about -x by
kHudQuarterTurn / 7), then the speech event with the first one's id; then RefreshSelection.

### CBattleScreen::MouseMove

Input side `this` is the window (ui_battle_mouse.cpp). The mouse moved (also every frame from Draw, at the
current position): while a portrait click picks a player the cursor shape 0xD and the portraits' own mouse
move; otherwise shape 0, then with a press on the view, a positive BoxSelectThreshold and not a network
client: a move farther than the threshold from the press point in x or y starts the selection box, which
empties the unit panel's hover unit and spans the press point and (x, y); before that the unit under the point
is hovered at once. Without a press the container's handler first, and when no panel took the move the unit
under the point (any flags) goes to HoverUnit.

### CBattleScreen::KeyDown

A key pressed (ui_battle_keys.cpp; Rebirth's MPSpeedControl reproduced behind its flag; input side `this` is
the window). While a portrait click picks a player, Esc ends that and nothing else happens. On a network
client Enter / Backspace go to the chat line and the key is used. Otherwise the container's handler first; a
key nobody used and not repeating: Esc drops the pending action with every panel's highlight, or without one
opens the escape menu (built inline: 0x168 bytes); any other key by its action (KeyActions, by `flags &
0x1FF`): 1 pause (the clock's sound), 2 / 3 normal / fast speed (not while paused or on a network client: the
clock's +0x154, the world's update period 55 / 27 ms, the interface's +0x94), 4 the objectives window of the
player's zone, 0xB the camera follows the first selected member (or nobody), 0xC the camera faces north (the
map's +0x1B0 0.5), 0x11..0x16 the order keys (orderKeys_), 0x17..0x1A the selected members' movement mode 0..3
and 0x1B their flag (the movement panel), 0x1C / 0x1D the action buttons "follow" / "science", 0x1E..0x25
spells 0..7 and 0x2A..0x2D quick items 0..3 (cast or used at once with Ctrl or Alt), 0x26..0x29 weapons 0..3,
0x2E..0x31 the unit panel's body parts and 0x32 / 0x33 the top panel's pages (a panel slid away comes back,
the choice already shown slides it away), 0x34 the map slides, 0x35 / 0x36 quick save / load ("saveq"),
0x37..0x39 party members 0..2 (ToggleMember; with Ctrl or Alt the camera follows the member), 0x3A every
member selected (not on a network client), 0x3B the message window opens). Pause, the objectives, the movement
keys, the message window and the escape menu mark the key used.

### OnQuestVariable

A "q." player variable changed while the screen is on top (the quest log passes it on). `name` split at '.':
three parts (q.<zone>.<quest>) a quest line, "combat_get_quest" / "_complete_" (value 2) / "_failed_" (3),
then the quest's title (text "quest <quest>"); four parts (a sub-objective) the same with "_subobj" and the
"#subobj <n>" line, skipped on a single player whose variable "<a>.<b>.<c>" is 0. The line (any part count)
goes to the top panel's text and, unless +0x160, the message log; the clock pulses, +0x1FC becomes at least
the value (1 for a failure, 3 for a completed quest).

### RefreshSelection

Called when a unit joins or leaves the party, after items are picked up, by the member toggle, a box drag and
a network client's party change). The selection changed: with exactly one living party unit selected the
weapon bar, quick items, spell and action columns show it, otherwise they are emptied (the action column keeps
its extra button while anyone is selected); the unit panel (+0x14C, +0x15C / +0x154 from the prototype's +0x20
/ +0x24) shows the first selected unit (null with nobody selected) when it changed; the pending action (+0x17C
0, +0x180 / +0x184 -1) and every panel highlight are cleared.

### ToggleMember

Party member `index` clicked (its portrait or body, SelectUnit; the member keys): nothing on a network client
or for an index past the party. The selection is the members' flag 0x10 (as 0x10 or 0); with the interface's
+0x7D the member flips (to 1 / 0), else it alone is chosen (1). The world's selection is cleared; each
portrait's marker gets the chosen value and its face's rest orientation the turn about -x by kHudQuarterTurn /
7 (or the identity); each chosen member is controlled and the clicked one gets a speech event (id, 0, 0). In
single player the local player's unit records follow flag 0x10; then

### CHelpWindow::ShowPage

Body of the Rebirth-patched page setup (ui_help_page.cpp; reproducing Rebirth's WideScreen, so hooked in both
launches). Empties the two lists, the surfaces, regions and scene objects, opens "tutorial <text_>" (without
it the pending value is set to 0) and goes to the page's "#screen" line. The page's rectangle is (200,
50)-(600, 560), or "#position left top right [rows]" (at least 300 wide, rows x 16 + 110 high, at most 600).
Scene objects 0 the plain background, 1 the frame, 2 / 3 / 4 the previous, OK and next buttons; then one
pulsing frame per "#rectangle left top right bottom" line (with Rebirth's WideScreen some zone tutorial pages'
frames move by the layout's extra units, as the battle screen's panels) and one entry per "#text [picture]"
line (the next line's first word is the text's key). Surfaces: 0 the page's title and "title (page/pages)", 1
the entries' wrapped "tutor <key>" texts (laid out twice when no rows are given: the first pass makes the
pictures, "tutorial_<picture>" copied into referenced object 1's 64-pixel cells, and gives the height), 2 the
buttons' texts with the help key's name. Regions 0-2 the buttons.

### CGlobalMap::MouseMove

(input slot 4; Rebirth-patched; reproducing WideScreen, so hooked in both launches: the drag scaled without
the margin, the pick picture's x without it). The mouse moved. While dragging: the drag motion (printed to the
stream) turns the map: angle_ += dx / 300 (layout units), taken mod kGlobalMapFullTurn, and turn_ = the tilt
(pi * 0.8 about x) times the spin about z; the motion is cleared. Otherwise the zone or link under the mouse
is selected (a link's region 3..; or, off every region, the zone read from the backdrop's pixel at half the
point, on gipat its link when it has one; nothing while a turn button is held) and its tip shown: the first
surface with the title and the titles of the zone's quests, the last two sprites behind it; a zone in state 0
or nothing hides the tip. A new name plays "buttons\globalmap\zone.wav". Regions past the links do nothing.

### CGlobalMap::LeftDown

(Input slot 2. Rebirth's WideScreen patches it and this reproduces the patch: the pick picture's x is read
without the margin; ui_global_map_paths.cpp.) The left button pressed (nothing while dragging). Regions 0 / 1,
the turn buttons: turning_ -1 / +1 and "buttons\globalmap\move.wav". Region 2, the camp (unless noCamp_ is 1):
"buttons\globalmap\transit.wav", the constructor 0 and screen 5 asked for. A link (regions 3..), or on gipat a
zone that has one: "buttons\globalmap\ok.wav", the ways to the link's zone searched from the local player's
zone and, when there is one, the time of arrival exit 0 passed and the zone asked for at entrance 0. Any other
zone under the mouse (read from the backdrop's pixel at half the point; its state not 0): the sound, the
search, and the objectives window (built inline) opened over the map with the zone, the arrival times, two
buttons and no zone list.

### CCampPages::BuildPages

Create's body. Builds the camp: the server flag set; the constructor's class template from the player's
"constr.current" (none found: back to the world, nothing built) with its flags, price weights and part lists;
the player's "i.noconstr" / "i.off..." / "i.on..." switches; texture "campinfo", twelve regions (the two
action buttons, the two hero turn areas, the eight page buttons) and ten button sprites; the item screen's
base; two surfaces; seven item lists; the panels (description, statistics, help, the item strip, equipment,
quick items, trade, and each offered constructor's strip and pages); the party's heroes handed to the client
world as units (records of class 0x32, ids raised by 100000000 meanwhile), each idling, armed from its record
and wearing its armour; the first hero shown the page titles; the first page (4 item constructor, 2 spell
constructor, else 0); a constructor's stock made on its first visit (restockDue) from the party's lowest and
highest skill per spell school; the allod's constructor music; antialiasing (render state 2 = 2) when the
device sorts independently.

### CBriefingScreen::MouseMove

Mouse move: with the chat window active, cursor 0xD and the event to it; otherwise cursor 0, the container's
handler, then by state: 1 the hovered topic row (redrawn when it changes), 3 cursor 0xE, 4 cursor 0. Walking
(state 0, right button up): over a person who can talk (as LeftDown; on a network client not the leader) its
topics are looked up (cursor 0xE when it has any) and its "pers" text, when it has one, is shown in surface 1
at the mouse (-125, -20); over a zone exit, cursor 0xF when the zone of exit_ (not of the exit found: kept) is
not "none" and its "z.<zone>" variable is not 1.

### CBriefingScreen::KeyDown

Keys: with the chat window active only Esc, which gives the input back. Otherwise the container's handler,
then Backspace / Enter go to a network client's chat line; Enter (single player) and Esc skip the phrases
(with 1 until the state changes) or take the rewards; Esc also opens the escape menu (state 0), ends the start
or the topic choice (1); Space the next phrase. Other keys while walking by their action: 4 the objectives
window when a quest of this allod ("q.<quest>.<zone>.." = 1) is active, 0xB the camera to the leader, '5' /
'6' quick save / load "saveq". Every handled key sets handled_.

### CBriefingScreen::StartDialogue

Dialogue topic_ starts: "goodbye" (or no leader) back to state 0; a "constr..." topic opens the camp (screen
5) with constructor N (the topic from its eighth character) when that class template exists. Otherwise the
"#show" lines of the text "briefing <topic>" name the people: the first one not the leader is person_, a
second third_; without one, state 0. The places: the person's (or, with noWalk_, the leader's) position, the
other 2.5 + viewDistance along its facing, the third one beside the middle; their facings across, flat and
normalised. With `remember` the people's positions are kept first (personHome_, thirdPlace_). A mover of the
third person (or of the person, noWalk_) is dropped, its target kept unless it was waited for; then the leader
(or, noWalk_, the person) and the third walk to their places (waited, `immediate`). `immediate` starts the
phrases at once (StartPhrases), else state 2.

### CBriefingScreen::FinishDialogue

The dialogue ends (the rewards taken, or skipped): the voice stops, the music fades in, the shown items go,
every speaker leaves (the server hears event 2, its blink stops, its server unit's flag 0x40000000 (HelmetHidden) back,
height 0, action dropped) and the list is emptied; the leader and the person back to height 0, the third
speaker walks back to thirdPlace_ (and is forgotten), with noWalk_ the person to personHome_. The first
"b.*.<topic>" variable (three parts) completes (for player 0). State 0, the server hears event 0 with the
leader; a dialogue that started by itself (briefingStarted_) puts the camera on the leader (message 0x04) and
frees it, otherwise it goes back to savedCamera_ / lookAt_.

### CBriefingScreen::ShowItem

"#show <name>": a person `name` joins the speakers (moved to the end; the server hears event 1; the leader's
server unit gets flag 0x40000000 (HelmetHidden: no helmet shown); its controller's actionDone the speakers' callback). Anything else with
`index` >= 0 is a loot item ("_" read as " ", lowercased, the loot table's name index): an item of that name
shown already goes, then slot `index` % 7 (x 300, 500, 200, 400, 600, 75, 725; y 300 or 500) gets a backdrop
and frame for the first seven, the item's figure "initqu<n>item" (tinted from index 8) with its image
"quitem<n>" in a new texture, turned over about x; it goes first in shown_. The third argument is not read.

### CBriefingScreen::NextPhrase

The next phrase of the current text: its lines up to the next "#phrase <speaker> [voice]" are carried out
("#nolips", "#show", "#hide", "#animation n", "#camera n [angle distance height]"); the phrase (up to the next
'#') goes, headed by the speaker's name, into dialogueText_ and (unless `skip`) the text window, with its
voice "<t|s>\briefing\<topic>\<n>.mp3" and phraseTime_. After the last phrase: state 4 and the briefing
record's rewards as lines (experience, gold, items given and lost, quests). The camera then views the phrase's
speaker, who talks (an animation, blinking unless "#nolips") while the others listen; skipping to the end
shows the whole text at once.
