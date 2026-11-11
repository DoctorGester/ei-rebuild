# User interface

Screens, windows and the battle HUD (`src/game/ui*.cpp`). Layouts and per-function behaviour are in the headers and the
code; this note holds how the pieces fit, which scenarios reach them and the facts the code does not say. `ui.h` keeps
the core only: the interface manager (`Interface3D`), the window base (`UiWindow`, `UiContainer`), `UiSurface`,
`UiRegion` with its tooltip, labels and models, the event and the screen fade. Every other window family has its own
header and source (the table below): a family's classes are declared in its header and their functions defined in its
source file. How the interface's pixels are made (the surfaces drawn each frame, text, the glyph atlas) is ui-native.md.

## 1. Conventions shared by every screen

**Layout.** Layouts are authored for 800x600; `screen_layout::ToScreen` / `FromScreen` (a point or a rectangle,
returned by value; the `NoMargin` forms for sizes and offsets) convert with `x * width / 800`, `y * height / 600`
(64-bit products, truncating division). The WideScreen layout (rebirth.md, "WideScreen"; `screen_layout.h`) keeps the
800x600 middle and moves the edge panels by `EdgeShift`.

**Loops.** Loops over a window's lists are range-for. An index loop that reads the size again each pass stays where the
body can add to or remove from the list (a child's draw, a constructor that builds into the slots, `TakeOff`); its
comment says so ("By index: ...").

**Screen classes.** Each screen is a `UiWindow` subclass. Every input event reaches a window through one virtual,
`OnEvent(UiEvent&)`: its kind (LeftDown, LeftUp, MouseMove, RightDown / RightUp, LeftDouble / RightDouble, KeyDown,
Char, KeyUp, Wheel), the mouse in screen pixels (`at`), the key, scan code and repeat, the typed code point
(`character`), the wheel's turn in notches (`delta`: a float, positive away from the player; the original's wheel
constants are per Windows unit, `kWheelUnits` 120 a notch, and its handlers multiply by it, exact for whole notches) and
`handled`. A window's OnEvent is a switch to its named handlers (`LeftDown(UiEvent&)` ...); what it does not take goes
to its base class's OnEvent (a container's: the dispatch below). A window without a double-click handler of its own or
in a base takes a double click as a press (the original's default), a container's goes to its children as a double
click. `Deactivate()` is a virtual of its own (no event, no flag). The key tables are indexed by the scan code (0x100
extended). A button-down handler that takes the mouse stores its window in `Interface3D::mouseCapture`; the button-up
handler acts only when the capture is its own and clears it; a handler sets `event.handled` when it used the event,
which stops the container passing it on. A handler that hands the event to another window (the battle screen's wheel to
its panels, the drop-down list's press to its scroll bar) passes the same event, and its point unchanged: handlers that
scale the point scale a copy.

**How the screens open.** `Interface3D::CreateScreen` is called by the client's message 7 handler with the id the server
sends: 1 battle screen, 3 briefing / base, 4 global map, 5 camp, 7 view screen, 9 exchange (0 does nothing). The client asks
for a screen with message 4 (the client player's state becomes 3); the server accepts it only in state 1 (in the world): it
removes the party from the world, moves to state 2 and answers with message 7. Message 5 (needs client state 3; the camp's
page 7) brings the party back (state 2 -> 1). So the global map's camp button works only where the map is the world's own
screen (a "brief" zone). `LeaveToZone(player, zone, exit)` (server) sends message 8; the client sets game request kind 4
and the main loop loads the zone directly; its type in map.txt decides the mode: `game` (mode 1, battle screen), `brief`
(mode 3, a base such as bz1g), `edge` (mode 4). The global map is not shown on the way. The harness step `screen <id>` does
what the global map's buttons do (for 5 it first sets "constr.current" to 0) from the battle screen of zone1.

**Keyboard.** Key down goes to `Interface3D::OnKeyDown` (the console toggle on scan 0x29, the modifier flags Ctrl / Shift /
Alt, a screenshot "shot%.4d.bmp" on scan 0x46), then to the top window's KeyDown. In-game hotkeys are looked up by scan code
in the key table (`config\keyboard.ini`, `key_bindings.cpp`); a zone has no inventory or map-screen key (`I` is quick item
3, `M` the minimap).

| Files | Screen |
|---|---|
| `ui_briefing*.cpp`, `ui_briefing.h` | briefing / base screen (id 3, `BriefingScreen`) |
| `ui_camp.cpp`, `ui_camp.h` | camp (id 5, `CampPages`), its pages and panels' class declarations |
| `ui_hero_screen*.cpp`, `ui_hero_preview.cpp` | multiplayer hero screen (`MpHeroScreen`, texts `lmp_*`) |
| `ui_connect_*.cpp` | multiplayer connection screen (`MpConnectScreen`, `config\addrbook.ini`) |
| `ui_item_strip.cpp`, `ui_hero_panels.cpp`, `ui_trade_panel.cpp`, `ui/ui_hero_panels.cpp`, `ui_stats.cpp`, `ui_item_info.h` | item strips and the hero panels shared by camp and hero screen (`UiItemPanel` is in `ui_item_info.h`); camp pages: trade, repair, spell and item constructors |
| `ui_layouts.cpp`, `ui_menu_pieces.h`, `ui_credits.cpp`, `ui_exchange.cpp` | escape menu and main menu layouts, credits, exchange between players; the pieces the escape menu's windows share (a region, a frame and a shade sprite, the stone model, the slide's ease) |
| `ui_global_map*.cpp` | global map (id 4) |
| `ui_main_menu.cpp`, `ui_message_box.cpp`, `ui_message_box.h` | main menu, new-game dialog, message box |
| `ui_battle.cpp`, `ui_battle.h` | battle screen (`battle00`): creation, update, input, selection and orders |
| `ui_hud.h`, `ui_hud_slots.cpp`, `ui_hud_corner.cpp`, `ui_hud_side.cpp`, `ui_hud_portraits.cpp`, `ui_hud_pieces.h` | the HUD panels the battle screen builds: the slots (weapon bar, quick items, spell and action columns), the corner panels (clock, movement and top panels), the side panels (map, unit panel, `PanelSlide`) and the portraits, one source each; `ui_hud_pieces.h` holds what the sources share (click sound, sling arrow, tint, pause) |
| `ui_objectives.cpp`, `ui_objectives.h` | objectives window |
| `ui_options.cpp`, `ui_options.h`, `key_bindings.cpp`, `key_bindings.h` | options window and the key actions table (`kActionDefinitions`, defined in `ui_options.cpp`); the bindings file |
| `ui_saveload.cpp`, `ui_saveload.h` | save / load window and the quick saves |
| `ui_help_window.cpp`, `ui_help_window.h` | tutorial / help window |
| `ui_chat_overlay.cpp`, `ui_chat_overlay.h` | the in-game chat overlay |
| `ui_scrollbar.cpp`, `ui_spin.cpp`, `ui_edit_field.cpp`, `ui_scroll_text.cpp`, `ui_drop_list.cpp`, `ui_tooltip.cpp`, `ui_scrolling_surface.cpp`, each with its `.h` where it has a class of its own | controls: scroll bar and slider, spin, edit field, scrolling text, drop-down list, tooltip, the titles' scrolling text |
| `ui_mesh.cpp`, `ui_mesh.h` | the interface sprite (`UiMesh`) and the sizes the dialog pieces share |

## 2. Interface manager and window base (`ui_interface.cpp`, `ui_window.cpp`)

`Interface3D` (`g_interface`, the only one: windows, surfaces, regions and inputs name it, they keep no pointer to it)
owns the active windows in a vector (`unique_ptr`; `PushWindow` and `Activate` take the new window's), the top one
receives input; it owns its backdrops, screen copy, frame rate and status surfaces the same way. A `UiWindow` holds five
arrays: referenced engine objects and scene objects (counted: released in ~UiWindow), and the surfaces, regions and
models it owns (`unique_ptr`; `AddSurface` takes the owner and returns the surface); a container owns its children and
extra inputs the same way (`AddChild`). A window destroyed while it holds the mouse lets it go. `UiRegion` is a
rectangle or a convex polygon (from 3 vertices) tested with integer cross products; a region owns its tooltip
(`UiTooltip`, a plain struct holding its surface while it shows); tooltip states 0 idle, 1 hovered, 2 appearing, 3
disappearing, 4 shown (0 and 4 stay, 1 -> 0 or 2, 2 -> 4, 3 -> 0). A hovered tooltip goes back to idle (its timer 0) as
soon as the mouse is outside its region: the original's kept counting there and showed for one frame at the mouse, over
whatever region the mouse was on, when the count reached the delay (scenario `ui_tooltip_switch`). `UiRegion::SetTip`
formats "tip %d" up to '#'; a missing text shows the key. A window keeps the regions and sprites it uses again in named
members (`okButton`, `rows[n]`, `exitMarkers`) and compares the region under the mouse with them (`RegionUnder`; a slot
list's index `RegionIndex(list, region)`); a window that clears or trims its regions or sprites resets or rebuilds those
members with them (the help page, the briefing's topic rows, the objectives' exits and places). The creation order still
decides the hit test (the first region holding the point), the drawing order and the harness's `region <n>` steps, so
the scenario notes keep region numbers. The camp's and the hero screen's curtains are a `screen_layout::Curtains` pair.
`SetScreenPosition` computes `(x * 0.0025f - 1) * 0.48157f * depth` with each step stored as float.

News only one screen hears (items picked up, speech and quest variables for the battle screen; "briefing <name>" and
the players changing for the briefing; an exchange's end; the console's "give <player> assortment" for the camp) goes
through the interface's typed pointer to that screen (`battle`, `briefing`, `exchange`, `camp`: each set in the screen's
CreateScreen and cleared in its CloseScreen), and only while it is the top window (`OnTop`), as the original's virtuals
reached the top window and did nothing on the others. The item panels likewise keep their owner as `ItemScreen*` for the
slots every item screen answers (TakeItem, CanEquip, RefreshStats, HeroRecord) and as `camp` / `heroScreen` (null on the
other screens; the constructor overload the creating screen's `this` picks sets them) for one screen's own: help topics,
prices, heroes and the spell maker's party skill (camp), the weight and the figure choice (hero screen).

`CreateScreen` (`ui_create_screen.cpp`) builds screens 1, 2, 3 and 7 inline. `PushWindow` sets the backdrops, the server
flag (kept for the first window), five render states, runs the window's Create and pumps messages. `UiModel::Draw` animates
by timeGetTime / 67; model part flag 0x80 = hidden. `FrameTime` is the display's frame milliseconds * 0.001f, left
unrounded. `UiContainer::DrawSurfaces` / `DrawTips` do nothing unless the interface is drawing. `MasterRequestBox` is a message
box plus a master server link.

Input: `OnKeyDown` / `OnChar` / `OnKeyUp` (`ui_interface.cpp`; a character goes to the console, a client's open chat line,
then the top window); mouse input (`ui_interface_input.cpp`) to the capture, else the top window, with a double-click flag
and a right-button flag on the interface.

### Container input dispatch (`ui_container.cpp`; scenario `ui_container_input`)

The window's events go to the window that took the mouse, else the top window, as UiEvents (`Interface3D::OnMouse`:
left down clears the double-click flag, a double click sets it, left up clears it after the call; right down and a
right double click set the right-button flag, right up clears it; key down, key up, and a char unless the console or a
multiplayer chat takes it). `UiContainer::OnEvent` is one loop: the event's handled flag cleared; each extra input
(`UiInput`: the camera's keys, the view screen's unit picking) gets it, and one that uses it ends it; then each enabled
child (mouse and wheel events also need the point inside it) the same, by index (a handler may add children; none
removes one during input). Deactivate goes to every extra input and child and touches no flag. Ours: while a container
has a `popup` (an open drop-down list, ui-native.md "The drop-down list"), every event goes to it alone and the
container counts as handled. The original's flag lived on each window and was read stale by the interface's help-key
check (`OnKeyDown`: an unused ';' opens help) after a screen's early return; only windows with a help topic (global
map, camp, objectives, options) open one, and their key handlers write the flag on every path, so the event's flag
reads the same.

## 3. Interface base: frame update, text, console window, cursor, chat

- `Interface3D::Update`: a pending value pops the top window (Deactivate, CloseScreen, delete); 0x990001 sets close-all
  and leaves the game; then close-all pops every window, the mouse position is copied, a network client that lost the
  server gets the "disconnect" box (0x990000; its text is `disconnectReason` when the client left on its own, objects.md
  "Leaving the server"), else the top window's Update runs, last the root's. `ScreenFade::Draw`: alpha step `ms * rate +
  alpha` stored as float, clamped at 255, below 0 the fade ends. `Shutdown` writes `config\tutorial.ini` from
  `helpTopics`.
- The interface's root window is the `ChatOverlay`. `UiItemPanel` is the base of the description panel, hero equipment,
  quick strip, trade / repair grids, constructor pages and the blank panel.
- Glue (`ui_interface_input.cpp`): `PlayInterfaceSound` (Play2D, priority 1000, volume 100), `OnQuestVariable` (at 1 the
  name's third '.' part becomes savedText), news for the top window, the bottom window's save, the chat overlay's AddLine
  (multiplayer only).
- Cursor (`ui_cursor*.cpp`): `Cursor::Init` / `AddShape`, `SetShape`, `Start`, `UpdatePosition` (frames every 125 ms). The
  cursor is the system's ("Cursor in the frame" below).
- Flying hit points (`ui_flying_hit_points.cpp`): `Add` with flag 8 starts 0.5 s later; numbers need option 17. Their
  sizes (8x12 digits, 64x10 label rows, the 32x10 marker), offsets and drift are 800x600 layout pixels scaled by
  max(1, height43 / 1200) (the original's pixels up to a 1600x1200 screen, grown above it; 1.8 at 4K; height43 / 600 was
  twice too large at 4K for the user); the 8x12 texture is magnified, so at 4K the digits are soft.
- Chat overlay (`ui_chat_overlay.cpp`; multiplayer only, no scenario): `Update` erases lines older than 20 s, `AddLine`
  wraps at 780 px, "^<digit>" colours, at most 12 lines; Enter sends a `net::SayMessage` kept 60 s.
- Camera input (`ui_camera_input.cpp`, `ui/ui_camera_input.h`): `RightDown` / `RightUp` turn with the cursor held,
  `Deactivate`, `Wheel` zooms. Keys map through the scan code to characters: 5-10 set a direction, 13-16 restore camera
  views 0-3 (saved with Ctrl or Alt held); while a direction is held the camera pans (with a modifier turns and tilts) at
  the option speeds.
- Console window (`console/console_window.cpp`): `PaintInput`, `PaintLines`, `OnKeyDown`, `OnChar` (Rebirth's ImproveConsole behind
  its flag); harness key names `grave`, `backspace`, `delete`, `home`, `end`, `pgup`, `pgdn`, `enterchar` (Enter plus its
  character, which the console needs); scenario `console.txt`. **Size** (`console_window.cpp`): every size the window draws
  with (the 9-point Courier New, the 15-pixel line pitch, the 8-pixel padding, the 31-pixel input strip, the caret's 2
  pixels, the minimum window, the 24-pixel top margin) is a layout pixel times `scale` = `height43` / 600 (`ConsoleWindow::Px`),
  like the interface's fonts (`TitlesDpi`): the same in proportion at 1280x720, 3840x2160 and on a Retina Mac (the game
  sees physical pixels, a 2x display of 1280x720 points is 2560x1440: scale 2.4). A resolution change runs `OnResize` (the
  rectangle asked for, scale, font and visible lines again, the surface made anew). The log's "Console window:" line gives
  the metrics; scenario `ui_console_size` captures 1280x720, 3840x2160 and 2560x1440 (line pitch 18, 54 and 36 pixels).
- Multiplayer-only windows (no scenario): `ChatWindow` (the base screen's chat, `ui_chat_window.cpp`: a vector of counted
  `PlayerMessage`s, new ones in front), the exchange (`ui_exchange.cpp`), `MpConnectScreen` (`ui_connect_screen.cpp`),
  `MasterRequestBox` (`net::MasterLink` in net/net_session.h).

`DrawWrappedText` walks the text plus "\n" and measures with our own font (`ui_font.h`, `TextExtent`), scaled to 800x600 by
integer maths; a word that did not fit before a '\n' goes on a line of its own (even an empty one).

## 4. Main menu, dialogs, escape menu, credits

Scenarios `ui_main_menu` (regions 3, 1, 0, 4), `ui_container_input` (Esc on the menu, Esc on the quit box), `ui_screens`
(main menu region 5, the hero screen's list keys and rotate buttons, Esc back; then a new game and the escape menu: hover,
resume, Esc, the quit box cancelled), `ui_view_screen`. The harness's `newgame` bypasses the difficulty question.

- **Main menu** (`ui_main_menu.cpp`, a UiContainer; regions from the `MainMenu` layout in `menus.reg`): 0 new game (the
  difficulty question, "option_difficulty"), 1 load, 2 exit ("Movies\Exit.bik", then quit), 3 options, 4 credits, 5 the
  multiplayer hero screen. `Update`: rain at 0 / 7 o'clock, night lightning 1 in 41 per 100 ms. The signpost is the
  stones, unit 2 of the menu's world ("unmoco2"): a hovered button plays its Briefing animation, none the rest pose
  (kind 0x80590000), and `CreateScreen` plays the rest pose too: `UnitClientSpecific::RunTick` sets flag 8 (no shadow:
  `ShadowCastStrength`) on a unit playing no animation, so without it the signpost would cast no shadow until the first
  hover. Scenario `render_menu_shadow`.
- **Escape menu** (Esc in the menu resumes): rectangles from `menus.reg` in `Res\menus.res`: 0 save, 1 load, 2 resume, 3
  exit to the main menu (asks `quit_msg`; Enter = 0x10001 leaves the game, Esc = 0x10002), 4 options. The click builds the
  save / load and options windows and pushes them; save opens in save mode, load without; both slide in.
- **Message box** (flag 4 adds an edit field): OK + cancel at 263 / 463, OK alone or cancel alone at 363 (cancel alone adds
  an empty region 0 first, so cancel is region 1 in both layouts). The difficulty question (buttons 0 OK, 1 cancel, 2 / 3
  the choice) stores the difficulty on OK.
- **Credits** (`ui_credits.h`, `.cpp`): from a game it steps the loading screen and stops the music, from the menu it clears the
  server flag at the end; plays the movies under "Titlesfin", draws the header unsmoothed (ui-native.md), parses
  "credits.scr" from "<data>outro.res" (one TitleLine per line) and starts "Movies\Titles.bik". `Draw` takes the next line
  once the time passes `nextLineAt` and the text stopped (`speed` 0 or NaN).
- **View screen** (`ui_view_screen.cpp`): screen 7, a debug view: `UnitPickInput` (picked ids: `LeftDown`
  picks / walks, Ctrl attacks, Space toggles the server pause flag), camera keys; focus (30, 30), rotation premultiplied by
  225 degrees about x. No unit can be picked in ui_view_screen: the party leaves the world with the request.

`UiWindow::AddSurface` returns its argument (the titles' Create freezes the surface through it). A hero record keeps a
clone of a weapon put on, so checks compare counts, not identity.

## 5. Camp, global map, hero screens and the camp's panels

Scenarios `ui_screens`, `ui_camp`, `ui_camp_deals`, `ui_camp_make`, `ui_describe`, `ui_hero_screen`, `ui_hero_stats`,
`ui_hero_delete`, `ui_trade_hold` (Rebirth), `ui_gallery_deals`.

**Reaching them.** `screen 5` opens the camp as the global map's camp button does ("constr.current" 0, no constructor);
regions 5-10 and 4 ask for pages 1-6 and 0 (2-6 not offered: fall back), regions 1 / 0 the hero arrows, 2 / 3 turn the hero;
Esc (page 7) leaves; `screen 4` opens the global map (regions 0 / 1 turn it, Esc the escape menu). Constructor pages need a
constructor record, which only a base's dialogue ("constrN" answers -> screen 5) or `screen 5 <N>` provides; setting
"i.onspellconstr" / "i.onitemconstr" without one crashes in `RebuildList` (the original too: the lists stay null). In
ui_gallery_deals vanilla buys only with "give 0 exp" typed while the camp is open (given in the battle
it did not show in the camp). FindPaths: ui_gallery_deals sets "z.gz1g" / "z.gz2g" to 2 before `screen 4`, then a click on
gz2g's area at (140, 277) opens the objectives window with the ways. Not reached: a link click (regions 3 and up: a
"z.<way>" = 2 way of kind 2 with a figure; it travels at once), the repair grid with an item in it (only a worn item is
offered, and no script command or key wears one). The map's camp button (region 2) after `screen 4` makes the server fail
("Serv error during parsing command prevID=ff curID=4", the original too), so scenarios end on the map.

**Global map** (`ui_global_map*.cpp`). Titles go through `PlaceTitle`, positions from the map database's records
(`MapRecord::position`, `FindQuest`; (0, 0, 0) for a missing entry). `SetWeather` gives the three layers, the display
colour and the clouds' ramp their final values once. The zone under the mouse is `ZoneAt`: each zone marker's triangles
projected as its draw projects them (the screen matrix the window's draw starts from, recorded by
`GlobalMap::DrawSprites`, times the marker's placement and scale), the faces the clockwise cull keeps, the nearest depth
there and of equal ones the later marker (the depth test's LessEqual), at the mouse's pixel (Direct3D 7's pixel
centres). The original drew the markers alone in grey levels into a half-size picture and read the clicked pixel back; a
scratch run (960x720, 12 zones) found ZoneAt equal to that picture at every one of its pixels, so the picks differ only
by the picture's half resolution (it read the even pixel), within a pixel of a marker's edge. `RefreshSelection` keeps
what that step left: the tip hidden, the title, every scene object but the zone markers shown (the glow layer and a
hidden camp button too) but the tip's two sprites, nothing selected; a zone whose state is not 0-2 keeps its own texture
(the original left it in its pick grey). `FindPaths` is a recursive way search; `arrivals` maps exit -> the shortest
"#passtime" sum (`ui_global_map_paths.cpp`). Right button drags; `KeyDown`: Esc menu, '5' / '6' quick save / load
"saveq".
- Positions are `centre + turn(local)` (centre = layout point (400, 280) at depth 10, each step stored; `turn` = the spin
  about z times a tilt of 0.8 pi about x, `TurnProduct` with its own operand order). Markers come from the player's
  variables ("z.<zone>", "q.<zone>.<quest>" = 1 by bit compare with 1.0f, ways out whose "z.<way>" equals 2, unordered
  counting as equal). Arriving from anything but the camp autosaves to "saveb" when the autosave option is set. Update
  draws the weather into texture 4 on the GPU (`render::DrawClouds`, renderer.md section 7). Create calls SelectZone
  before it creates the weather object.

**Camp pages** (`ui_camp_*.cpp`). Input in `ui_camp.cpp`; `MessageResult` 0x10001 takes the first weapon back. Trade
pages stock the constructor's class template's lists with the sold records (cost off except page 1). `DeconstructSpell`
gives "Prototype" and a "Rune" per modifier; `DeconstructItem` "Instruction Weapon / Armor / Quick Item" and
"material.<key>" (`ui_camp.cpp`). Constructor pages 3 and 5 pick through `SpellMakerPage::ItemAt` /
`ConstructorPage::ItemAt` (`ui_hero_panels.cpp`).
- An unoffered page shows the current one again (recursively: the tail runs twice, as in the original); constructor pages
  need the spell / item constructor flag, their second pages also not "i.noconstr"; money below the cost disables an
  action button.
- MoveItem asks CanEquip first (refused = the cancel sound) and reads Shift itself; items made by a constructor (record
  flag +0x10) go back to the constructor's parts list and strip, others to the local player and the item strip. With
  `limit` > 0 (and at least that many items in the target) it moves only an item the same as one of the target's first
  `limit`, and then gives no turning state. Leaving (page 7) without a constructor still calls ReturnAll with null
  constructor lists as `to`; ReturnAll reads `to` only when `from` holds something. ScrollToItem does not update `count`.
- Hero skills are stored as float then truncated; the mana maximum is stored as float, divided by the period (unrounded)
  and truncated.

**Hero panels, trade panel, strips** (`ui_stats.cpp`, `ui/ui_hero_panels.cpp`, `ui_panel_draws.cpp`, `ui_hero_panels.cpp`,
`ui_trade_panel.cpp`, `ui_item_strip.cpp`):
- `HeroStatsPanel::SetHero` stores height * 100 truncated. The panel's page 10 -/+ buttons (its `rows` 4-11, regions
  6-13): a hold starts at -500 ms with the capture, a step every frame from 250 ms (whole milliseconds,
  `UiContainer::FrameTicks`); regions 0 / 1 previous / next hero. `DrawPart`'s armour "%.1f" is the largest of the first
  six damage types' three-layer sums; a NaN keeps the old maximum. `HeroEquipPanel::RightDown` does not mark the event
  used.
- `RepairGrid::DrawSprites` puts models at depth 2^(3 - row) * 6; the constructor pages' highlight p = (cos(5 t) + 1) / 2 stored as
  float, colours truncated and clamped. Lists are `UiItemList` = `std::vector<CItem*>`.
- Trade panel (`ui_trade_panel.cpp`): `SnapshotRecord` copies floats as dwords, `Settle` (the all-branch tests the low byte
  of `keep`), `RestoreRecord` (HealSlots by old hp / old max * new max - Reduce(new max), the maxima stored as float).
  `LeftDown`: regions 0-3 scroll, 4-9 a school level, 10 science, 11-33 a learnable perk, 34 up a held perk; a held school or
  science row repeats as the stats panel's -/+. `FormatPrice` is "%d" / "%.1fk" / "%dk" / "%.1fm" / "%dm" / "%.1fg", negative
  as 0x7FFFFFFF. Perk text keys are the record's name without trailing "123" plus "0"; held perks are a level byte per
  group of three skill records.
- Item strip (`UiItemStrip`, the forwarding strip of camp and exchange): a held arrow's repeat counts whole milliseconds
  (`scrollTicks`: one slot at once, then one per 100 ms past the first millisecond). The click (`SlotAt`, both
  `ForwardingStrip::ItemAt` and `FigureStrip::LeftDown`) is integer: `(x - 50 + floor(position * 100)) / 100`, where the
  original truncated `(x - 50) * 0.01f + position`; only a pixel exactly on a cell edge can differ. `UiItemPanel::ItemAt`
  returns `ItemUnder`: the item and the help topic of the place ("camphelp <topic>"); the camp, exchange and hero
  screen hover show the first panel's item, else its topic. The forwarding strip's topic is its mode's (equip 0x16,
  spell parts 0x18, item parts 0x17) over every cell, empty or not; the original wrote it into the topic out-parameter
  as a cursor value, so an empty strip cell shows that help text, kept. `SetTab` settles the old
  tab at the truncated `pending + position`; releasing an arrow keeps the fraction of the pending scroll and adds or removes
  a whole slot when the fraction is under half. Filter 0 puts quick items in tab 2 with weapons, armour and spell
  containers; tab 3 holds class 0x3007, so clicking tab 3 never equips quick items.

**Multiplayer hero screen** (`MpHeroScreen`). Modes: 9 the hero list (rows 9-18; 1 continue, 5 new hero, 6 rename, 7 delete
"lmp_deletepers", 8 the hero's inventory = the camp screen with `fromHeroScreen`), 10 a new hero (1 create, rows 9-19 the
voice), 11 its equipment (1 next). `FinishRename`: the name before " | " plus " | " and the typed text. `PlayVoice`: the
prototype's sound set, a random entry. `Continue` builds `MpConnectScreen`. `CreateHero` needs the attribute sum equal (or
unordered) to the allowed one and a name. `Draw` previews at depth 553 / 400; `Update` turns the preview and tints the
create button. A hero record is a Party whose first member's text 0 is the name. The rotation state is `rotating`, beside
the turn. Per-function detail: the `MpHeroScreen::*` sections below.

**Exchange** (screen 9; multiplayer only, no scenario): `ExchangeScreen : ItemScreen` with a scratch party, the client's
two offers, a strip, a figure strip, the trade grid and a money field (`ui_exchange.cpp`); `CanEquip` is always true.

**Battle screen Create** reads its part of a save (from version 0x47 an int and a string appended to the top panel's text
and, on page 0, the message log; from 0x62 two more fields), builds the unit panel, map, clock, movement and top panels and
puts the "pers <name>" texts into a map.

### Briefing / base screen (`ui_briefing*.cpp`, `ui_briefing.h`, `ui/ui_briefing.cpp`)

Screen 3 of a "brief" zone. States: 0 walking, 1 topic choice, 2 dialogue starting, 3 phrases, 4 rewards. Scenario
`ui_base_hide` (Smith's topic M6): the leader walks to Smith, `clickunit Smith`, region 0 (the first topic), clicks
through the phrases, Esc, a second Esc takes the rewards. A person
can talk when its server controller's command is 0 or 0xB and the next one 9 (or it has none).

`ShowBriefing` is "briefing <name>"; `SaveState` writes one zero dword; `OpenTopics` makes five rows 210..570 x 440.. with
"goodbye" appended; `FindTopics`: "b.<person>.<topic>" = 1 by bit compare, a network client sorts them, the last "constr..."
topic last; `TopicTitle` maps "constr..." to "briefing constr"; the "constr" topic's number is `Mid(7)`. `OnUnitJoined`: the
leader's "b.<zone>" topic starts at once. `PersonTextMap::Load` serves the battle screen too. The rewards are the quest
database's `QuestBriefingRecord` (quest_records.h). Per-function detail: section 11.

`MarkPeople` (each Update in states 0 and 1) marks the people with a topic open (`WorldClient::MarkPerson`: the
figure lit, MapObjectFlag::BoxMarked; a selected one is left alone) on the local view directly, only where the game is
served (player 0 has a server half); the original sent each mark as message 0x19 kind 1 / 2 through the server's player
0, which reached the view a tick or two after the screen's first frame (the people lit up late).

Meshes: 0 / 1 the topics' backdrop and frame (shown in state 1 only), 2 / 3 the text's backdrop and frame, then the people's
slots. Update hides them all each frame, then state 1 shows 0 and 1 and states 3 / 4 show 2 onwards (showing every mesh drew
the topics' frame inside the text's).

## 6. Battle screen: update, selection, orders, pending actions

Files `ui_battle.cpp`, `ui_hud_slots.cpp`; scenarios `ui_hud`, `ui_battle_commands`,
`ui_battle_spells`, `ui_quick_items`, `ui_quest_notice`. The key handler runs the key table's actions (1
pause, 2 / 3 game speed, 4 objectives, 0x17-0x1A movement modes, ...; Esc cancels a targeting mode or opens the escape
menu); the full list is in section 11, "BattleScreen::KeyDown". `UnitJoined` places a unit by the local player's record
order. `KeyUp` releases the order key of actions 0x11..0x16; `Wheel` offers the event to the map, unit panel, message log and
top panel. `UseNow` (`ui_hud_slots.cpp`) casts at once on the unit, or for an attack spell on the nearest unit it sees (none:
"nomagic").

- **Selection.** `RefreshSelection` is the hub: a single living selected member (flag 0x10) goes to the four `SetUnit`s,
  else they get null; the action column's second argument is "anyone selected". UnitJoined / ItemsPicked / UnitLeft,
  ToggleMember, a box drag and a network party change end with it.
- **Button release**: without a press on the view (set when no panel took the press) the container's handler; a drag (the
  mouse moved past the threshold option; 0 or less = no boxes) selects the party units inside the box; otherwise the unit
  at the point (kept only with flag 0x400000) goes to SelectUnit.
- **Pending action** (`BattleScreen::SetPendingAction(mode, slot, item)`; a cancel is (PendingAction::None, -1, -1)): every
  highlight off, then the slot's button highlighted in the mode's panel. Modes: 1 / 2 a spell on a unit / at a point (spell
  data flag 0x10000000 or not), 3 / 4 a quick item's spell likewise, 6 a player's item, 7 follow, 8 an order on an object
  (science). A highlighted button clicked again cancels with a sound; only a spell item with its data sets the action; only
  wand (5) and potion (8) quick items go on. SelectUnit passes the Ctrl byte as `quick` for a spell and 1 for a quick item;
  Rebirth's FixSpellSelfCasting passes 0 for the spell. `UseAtPoint` moves the point by -AxisY(unit) * 0.1 for the member
  that is `unit`, and the moved point stays for later members (the original changed its own argument). Orders nod the
  members with a non-positive delay.
- Spell kinds come from `spells.sdb`: 'u' (0x10000000, mode 1: arrow, lightning, acid_ray, the protections, healing, charm,
  ...), 't' (0x20000000, mode 2: fireball, inv_lit, acid_column, firewall, litnwall, acid_fog, fireworks, clairvoyence,
  teleport, campfire), 's' shapechange. **Open:** ui_battle_spells' firewall and fireball should give mode 2, yet its runs
  show `UseOnUnit` twice and `UseAtPoint` never (`SetUnit` leaves the last button highlighted, so the first click on it
  cancels; which click cancelled is unknown).
- Spell buttons: the newest spell's region at (760, 470)-(800, 510), older ones 40 up; action column regions 0..40 x (470 -
  40 i)..(510 - 40 i); with a selected unit SetUnit adds "skill0011" (action -1) first, then science (action 0).

Regions without a button get SetTip(0), an empty tooltip. FCOMP-style compares treat NaN as equal (damage "Unknown" when all
weights are zero; quest values 2 and 3).

Not exercised: the battle screen's drag (no drag step in the harness), mode 1 orders, the exit flag, RefreshSelection with
several units, the attack kinds 0..5 and the level-gap speech, the quest notice's sub-objective path, the mouse move's
portrait pick branch (multiplayer).

## 7. Battle HUD panels

Files `ui_hud_slots.cpp`, `ui_hud_corner.cpp`, `ui_hud_side.cpp`, `ui_hud_portraits.cpp` (declarations in `ui_hud.h`);
scenario `ui_hud` (new game, mouse moves over every panel, clicks on weapon slots, top panel pages and sling, unit panel
body parts and sling, map views, movement panel, a portrait; the escape menu: Options -> group -> Cancel, Load -> row ->
Cancel, Save -> Esc, Resume; the hero must take damage; the wheel over the map's sling button (in, then out past the
limit) and the map; TAB opens the objectives window. Spell, action and quick item buttons get only mouse moves: a click
would start a targeting mode the following Esc cancels).

Panel rectangles (800x600 layout; WideScreen moves the edge panels): weapon bar 80..240 x 520..600 (4 slots of 40), quick
items 520..720 x 500..600 (8 slots 50x50), spell column 760..800 x 190..510, action column 0..40 x 250..510 (6 buttons from y
470 up), portraits 250..550 x 440..600, map 615..800 x 0..165 (region 0 = the sling button 780..800 x 0..30), unit panel
0..185 x 0..225 (body parts 1-4 at 0..20 x 100..220), clock 720..800 x 520..600, movement panel 0..80 x 520..600 (0 inner
quarter circle radius 34, 1-4 ring sectors 35..80), top panel 180..620 x 0..100 (sling 385..415 x 0..20, page buttons
185..215 / 215..245).

Classes: `HudWeaponBar`, `HudQuickItems`, `HudSpellColumn`, `HudActionColumn` (`ui_hud_slots.cpp`), `HudPortraits`
(`ui_hud_portraits.cpp`; Rebuild every new game, ShowFace for low mana and hits), `PanelSlide`, `HudMap`, `HudUnitPanel`
(`ui_hud_side.cpp`, `PaintUnitInfo`), `HudClock`, `HudMovePanel` (`ToggleFlag` orders through the local view), `HudTopPanel`
(`ui_hud_corner.cpp`). A family source defines its classes' functions grouped by class, its file-local helpers in one
anonymous namespace at the top; names the sources share live in `ui_hud_pieces.h`, so a file-local name means one thing.

A panel's buttons go through `UiWindow::PressRegion` (button down: the region, the event used, the mouse taken),
`PressedRegion` (button up: only with the mouse taken here) and `HitRegion` (move, wheel), over its first `count` regions
(weapon bar, spell and action columns) or all of them; each returns the `UiRegion*` (null for none). A panel keeps the
regions and sprites it uses again in named members (`HudMap::slingButton`, `viewButtons[2]`, `viewIcons[2]`, ...; a
portrait's sprites in its `HudPortraitSlot`) and compares the region it gets with them; the slot lists (weapons, quick
items, spells, actions, portraits) stay indexed, their slot `RegionIndex(regions, region)`. The creation order still decides
the hit test (the first region holding the point; a panel's whole-area region comes last), the drawing order and the
harness's `region <n>` steps. Each action is one function the click and its key both call: the
weapon `HudWeaponBar::Choose`, the slings `PanelSlide::Toggle`, "camera north" `HudMap::FaceNorth`, the clock's
`TogglePause` / `SetSpeed` / `OpenObjectives`, the movement panel's `SetMode` / `ToggleFlag`, the spell column's `Click` /
`ClickAlternate`, the quick items' `Pick` / `UseNow`, the action column's `Select`. A held view button's release on the map
is not marked used.

- Quick items: a point outside every slot finds no entry (`RegionItem`; the original read the entry before its table,
  zero in practice, and counted a hit; the slots cover the panel's rectangle); the weapon bar's button-up orders
  `LocalView()` without a null check; the minimap wheel zooms only over region 0 (zoom 2.5^(notches/10)); hidden minimap
  entries keep stale positions; unit 0xA2C2A counts as hostile in gz20g.
- Minimap sprites are `UiMesh` quads; `UpdateView` clips the outline in place against the world rectangle, pads to a
  multiple of four with its first point and fans it; the needle angle is atan2(-x, -y) of the facing.
- The clock's x centre is the layout constant 800 that WideScreen moves. Time of day = (ticks * rate + start) mod 24 (as
  `WorldTimeOfDay` in script_eval.cpp).
- Portraits (`HudPortraitSlot`): a face model replaced for a new build takes the old one's place in the sprite list and in
  its slot (`face`); three
  quaternion products each keep their own operand order; the random scales are the exe's doubles, not exact powers of two.
  The orders' nods and the speech shakes are `HudPortraits::Wobble(index, kind)`. Kept: the sex letter ("m" / "f") keeps the
  previous portrait's value for a figure that is neither "unhuma" nor "unhufe"; AddPortrait indexes the column's frame by
  count % 6.
- Unit info: "consider_more<d>" / "consider_less<d>" are looked up in place (one string as key and result); a damage type of
  7 or more keeps the move text. `ShowParts` reads six body part records with fixed label positions. ToggleFlag's third
  frame value 0.48046875 is unreachable.
- Top panel: `ShowItems` builds page 1: "format_money" with the local party's money when above 0, then the local player's
  item list merged by kind into the client world's copies, one FormatItemName line each; into the log when page 1 is up.

## 8. Objectives, save/load, options, help windows and the controls

Rectangles from `menus.reg`: save/load OK 163..237, Cancel 563..637, Delete 363..437 (y 526..574), list rows 108..370 x 108..
in 24 steps; options OK 213..287, Cancel 513..587, groups 100..250 x 156.. in 24 steps, rows x 250..520.

Scenarios: save / load (`SaveLoadWindow`, `ui_saveload.cpp`) ui_hud, ui_saveload, ui_saveload_delete; options
(`OptionsWindow`, `ui_options.cpp`) ui_hud (also clicks group 2), ui_main_menu (OK = Apply), key remapping
ui_options_remap (Enter, `dblclickregion`, a taken key's box, OK, then the keys used in the game, the hero's moveMode in
the digest), the graphics rows ui_options_graphics; objectives ui_hud's TAB (Rebirth), ui_base_objectives.
`UiEditField::KeyDown` has no scenario that types into one. The titles' RTF text is drawn as glyph runs
(`UiScrollingSurface::RenderText`, render_rich_text; ui_main_menu).

- Save entries' titles: `ZoneTitle` / `AllodTitle` ("zone " / "allod " with the space; else "save_camp" / "Unknown").
  `QuickLoad` loads newgame's savea. A save window edits the name; the first edit of an entry other than "saveq" / "savea" /
  "saveb" marks it changed.
- Key handlers run only when the window is not sliding in, pass the key to the container first and mark the event used
  for the keys they use. Options: while a binding row waits, the next scan code is looked up in
  the key names (88 entries), a key bound to another row asks "key_already_mapped", else CommitBinding and RefreshRows. OK
  only copies the window's table into the key table (Apply); `config\keyboard.ini` ("<key name> <action>" lines) is written
  on the interface's shutdown, so a game that ends without it keeps the remap for that session only. Settings go to the
  settings file (standalone.md, "Settings file").
- Slide-in eases the model from y 700 to its place (550 / 620 save/load, 480 options): `e = 2s^2` below s = 0.5, else `1 -
  2(1-s)^2`, then `(a - 700) * e + 700` stored once as float; `Update` adds `FrameTime() / 0.5` and compares the unrounded sum
  with 1; `UiScrollbar::Update` adds `FrameTime() * speed * 16` (negated for "up") to a float accumulator (NaN loops forever
  in both).
- `UiScrollText` (the battle message log, the quest log, the briefing's text) is a window with a `UiScrollbar` `bar`
  (its position the pixels scrolled; the window passes it its input and update and draws its sprites); it tokenises
  every text added into `TextToken` values: format tokens (runs of "\n", BEL 'C' r g b, BEL 'F' digit) and word
  tokens (a string and its width).
- **Options data**: rows are {OptionBinding*, OptionSetting*}; 27 option definitions (id, name, kind, maximum, group,
  row; 0-20 the original's, 21 our resolution row, a drop-down list: renderer.md, "Resolution changes"; 22-26 our
  graphics rows in the display group's free rows 8 and 10-13, kind 3 a spin control over a list of choices: renderer.md,
  "The graphics rows"; their labels and tips are added texts, Russian where the game's are), 59 key actions; 14 rows, 11
  groups. The slider (`UiSlider : UiScrollbar`): `upButton` (left, bottom - 10, left + 12, bottom), `downButton` (right
  - 12, bottom - 10, right, bottom), `trackArea` (left, top, right, bottom - 10); the scroll bar's depth is 3.0. The
  edit field's keys: Backspace, End, Home, Left, Right, Delete. Key names are followed by "  |  ".
- **Objectives window**: TAB (keyboard.ini `obj`) opens it, Esc or Enter closes it; OK asks for the zone at the nearest exit.
  Build shows the current zone's page, or the zones of the allod in which the player holds quests ("q.<zone>.<quest>" = 1;
  any allod in multiplayer). ShowZone's exits come from the map database record, not the world's ZoneExit; the player's
  variables are walked in the hash map's order. `LoadQuestPlaces`: the map database quest's places (map.txt
  "#quest <name>"), or the quest record's floats for a client. RefreshQuests trims sprites and regions to 21 + exits and
  9 + exits and adds one of each per quest place. The zone list and the distance table (from the global map) are not
  reached. Update blinks the markers with cos.
- Help window (`ui_help_window.cpp`): Build "tutorial <topic>", "#screen" per page, "tutor <key>" texts, the pulse, clicks
  and keys; the page setup is section 11, "HelpWindow::ShowPage".

## 9. Item descriptions, item view, sprite shapes, surface helpers

Scenario `ui_describe` (new game, GiveItem of a steel axe, an axe with a firewall spell, a potion and a rune, `screen 5`; the
mouse rests on the statistics rows, body slots, the item strip's "all" tab (region 7 at 785, 580) with `key ctrl` over the
first slots, then page 1; Esc). Blueprint, material, spell record and modifier descriptions are reached by none.

Pieces: `ItemInfoPanel` (`ui_item_info.cpp`: `DescribeStats`, `DescribeItem`, `DescribeBlueprint`, `DescribeCarriedSpell`,
`DescribeMaterial`, `DescribeSpellRecord`, `DescribeQuickItem`, `DescribeModifierRecord`, `ShowHelp`, `KeyDown` / `KeyUp` for
Ctrl), `ItemDamageLists`, `ItemScreen::DrawItem`; `UiMesh` shapes `Framed`, `Ring`, `Disc`, `ResetColours` (`ui_mesh.cpp`);
surface helpers (`ui_surface.cpp`, the sections below); `DrawHeroFace`, `ItemFigureTexture` (256x256 from "<name>.mmp",
drawn on the GPU with `render::DrawIntoTexture`), `UiWindow::DrawHeroPreview` (`ui_item_view.cpp`); `ItemScreen::CreateBase`,
`ItemIconWindow::Build`, `TrimItemModels` / `TrimItemTextures`.

The camp creates the description panel at (600, 100) and the help panel at (0, 100); the panel's owner is the item screen
and it holds a copy of the described item. `DescribeItem` dispatch: kind 0x3007 by its subkind 1 material, 2 blueprint, 3
spell record, 4 modifier record; quick item category 8 its own page; a carried spell while Ctrl is down, or a spell container
(0x3008), the spell page. The item screens keep model and figure texture caches (name -> {object, last use}, limit 30, or 20
with the low-memory flag); `ItemScreen::models` hides `UiWindow::models` (the window's own models) in every screen derived
from it, and the hero screen's mode switch reads `UiWindow::models`. Each item has a turning state whose glow and angle wrap
keep unrounded values.

## 10. Harness steps and scenario lessons for the interface

The steps added for UI work (`screen`, `clickregion` / `moveregion`, `wheel`, `dblclick`, `clickunit`, `hudstate`,
`campputon`, `zone` / `waitzone`, `ldown` / `lup`, key names `ctrl`, `grave`, ... `enterchar`) are documented in the header of
`tests/verify/harness.cpp`. Facts behind them:
- **Picking** (`clickunit`): the client world keeps a list of the objects drawn this frame with their screen rectangles
  (world.md, "World client frame"); a button release gathers the entries whose rectangle holds the point (then the
  object's finer test), sorts them by depth and takes the first unit, which must have flag 0x400000. The step tries a
  9x9 grid inside the unit's rectangle until the game's own pick answers with that unit, so the click cannot land on a
  unit in front; it fails when the unit is off screen or hidden. Modifier words (`ctrl`, `shift`, `alt`) after a key or
  mouse step hold those keys around it (Ctrl + a unit orders an attack), and `hold` / `release` keep them across steps
  (`camera_limits`). They are posted as key events, the only way the game learns them; the harness cannot drag.
- **Zone steps**: `zone` calls `LeaveToZone` on the local player's connection, as the zone exit handler and the script
  command do; it needs state 1, and in campaign mode 1 or 3 a dead or missing leader loses the game instead. `waitzone`
  waits for no pending game request, campaign mode not 2 (menu), the server world's zone name and state 1; the party appears
  a little later (Redeploy, world.md "Party between zones"), so follow it with `waitfor ... HP(GetLeader())`. The exit index
  counts `#exit` blocks from 0 (map.txt's `#exit n` minus 1); an index outside the zone's exits leaves the party at (0, 0).
  Zones: game `gz1g`..`gz21g`, brief `bz1g`..`bz18h`, edge zones `<a>_<b>` (no `#res`, mode 4; `waitzone` does not recognise
  them). One-way entrances (`none 0`): gz1g's #exit 1, gz4g's 5, bz4g's and bz13h's 1, gz11k's 4, gz15h's 2..4, gz20g's;
  `gz21g` names zonemichel.mpr / .mob, which no install has, so it goes to the main menu (`zone_gz20g_gz21g`; world.md,
  "Party between zones"); `gz6g` loads its frozen map when `z.gz6g.frozen` is nonzero; no zone needs quest state to
  load. Not tried: edge zones, `endofgame`.
  Scenarios `zone_gz2g_traps` (party at (67, 15), zone2's traps 1324..1330, MTF4 armed with a wolf), `zone_base_bz1g` (mode
  3, party at (59.25, 54.25), Smith), `zone_gz3g` (party at (23, 138.5), the Pyramid trap 4515).

Scenario lessons:
- **Race with a zone's intro script.** The server drops a hero blocked by script (BlockUnit / IsUnitBlocked, flag
  0x800000), and gz1g's intro script blocks the hero for about 37 s with its first tick unordered against the hero's
  creation: a scenario that orders the hero waits for the block (`waitfor 15 IsUnitBlocked(GetLeader())`) before lifting
  it and checks the hero is not blocked before each click (scripted moves ignore the flag).
- **Armed traps.** gz2g starts its magic traps armed (its scripts disarm them from triggers), so MTF4 strikes a placed wolf
  at the next world update: the scenario disarms it before placing the wolf, checks the wolf alive, arms it and waits for it
  to die.
- **Quick items.** No script command puts an item in a quick slot (GiveItem adds to the player's list); use `campputon 3006
  2` (at most four go on the hero); "wand 1".."wand N" are category 5 prototypes (e.g. `wand 3.iron[lightning]`; potions are
  category 8); the quick item panel's regions 0-3 are the bottom row from the right (x 670..720 down to 520..570, y
  550..600), 4-7 the row above; place a target where it is drawn on screen (`QuickWolf3` at (hx, hy - 3) was culled, (hx +
  3, hy + 3) works).

## MpHeroScreen::LeftDown

ui_hero_screen_click.cpp. Nothing while a hero is being renamed; after the base handler, by mode: the hero list (9: back to
the main menu, continue, the rotate arrows, new hero, rename, delete (a question), the hero's inventory (the camp screen
with fromHeroScreen), list rows), the new hero (10: back, create, the arrows, defaults, voice rows) and its statistics page
(11: back, next, take the equipment back).

## MpHeroScreen::KeyDown

ui_hero_screen_click.cpp. After the container's handler, unless a child used the key: Enter ends a renaming
(FinishRename), else continues (mode 9), creates the hero (10) or goes on to the next page (11); Esc ends a renaming without
keeping it (the list redrawn), else goes back to the main menu (9, as region 0), to the hero list (10) or to the new hero
(11, as region 0); PgUp / PgDn / Up / Down move the hero selection by 10 / 1 (mode 9) or the voice by 11 / 1 (mode 10, the
voice played). A key used is marked (other modes leave the paging keys unused).

## MpHeroScreen::MakePreview

ui_hero_screen.cpp. The preview cleared; the local player (given a "Hero" of "Human Hero" when it has no record) hands its
first record to the client world; the preview becomes the client unit with that record's id, its prototype index stored;
when there is one: skill animation 0x21 (SkillAnimation::Special14) (if any) set to start at a random frame (timeGetTime at
15 frames a second minus random % 51), the figure shows no weapon, refreshed, its animation restarted at the current frame
- 1, the record's statistics set and the controller's hit handler called.

## MpHeroScreen::ChooseFigure

ui_hero_preview.cpp. The new hero takes figure (unit prototype) `prototype`: the local player's first record handed to the
client world and its strength, dexterity and intelligence and height kept (25, 25, 25, 0.7 without one); the party cleared
and given a "Hero" of that prototype with its starting equipment; the attributes (base and current) reset to 25 and the
height to 0.7 on the hero list (mode 9), else the kept ones; each figure strip selects the prototype when it lists it (-1
otherwise); the equipment panels show the first weapon, quick item and spell; the preview, the statistics panel's and the
trade panel's hero rebuilt; text = prototype; the voice list of the strip that lists it (voiceLists[1] for names, else [0])
from row 0, its first file the hero's voice (text 6, read even when the list is empty); the voice scroll bar's range and
position 0; on the new hero page the voices drawn.

## MpHeroScreen::AddNewHero

ui_hero_screen_click.cpp. Region 1 / Enter of the statistics page: the new hero keeps only the chosen weapon, quick item and
spell container (the others removed from its lists; the shown weapon 0, or -1 without one), and a spell container whose
spell the hero cannot use (HeroCanUseSpell) goes back to the local player's items; a new hero record is appended and
selected, its file named "<the highest number among the folder's *.mp titles + 1>.mp", given the local party and saved; the
list's scroll range, the list drawn, mode 9.

## MpHeroScreen::SetMode

ui_hero_screen.cpp. The screen laid out for `mode`. First everything off: the menu model's parts, the nine child windows,
every sprite and surface, the regions after the ninth removed and the others' tips emptied. 9, the hero list: the model's
"but03" and "button03_multiplayer", the list's scroll bar, all sprites but 7-9, the four frame quarters (sprites 3-6), the
buttons' regions and tips, surfaces 0 and 1, the name edit off, the selection frame (sprite 2), ten list rows as regions and
the selected hero shown. 10, the new hero: the voices' scroll bar, sprites 3-9, the quarters, the bars at 300, the
statistics panel and both figure strips, the regions and tips, the name edit on and editing, surface 2, the selection frame,
eleven voice rows and the voices drawn. 11, its statistics: sprites 7-9, the bars at 400, the equipment, spell, trade and
statistics panels, two regions and three tips, surface 2, the name edit off, the trade panel's hero 0. Last, for every mode:
the description panel emptied, the statistics panel's page `mode`, mode. With the WideScreen layout the curtains (sprites 19
and 20, when there are 21) are then shown in modes 10 and 11 and hidden in 9.

## MpHeroScreen::CreateScreen

ui_hero_screen_create.cpp. The game's resources taken for a network game; the menu model with its multiplayer button; nine
regions; the item screen's base; the sprites (0, 1 the buttons, 2 the selection frame, 3-6 the quarters "campm1".."campm4"
around the figure, 7-9 the new hero's buttons, 10-12 frames, 13, 14 backgrounds, 15-18 the commands' buttons); the
description, statistics, equipment, quick item and trade panels; the list's scroll bar; the figures a new hero can take:
every creature record whose unit prototype's figure model is "unhuma" (defaults, voiceLists[0]) or "unhufe" (names,
voiceLists[1]), a voice "<lmp_male_voice / lmp_female_voice> <n>" only when the record's flag is set; both name lists sorted,
the women's then reversed; the two figure strips; the rename and name edit windows; the voices' scroll bar; four surfaces
(the list, its commands with the four "lmp_pers_..." texts, the voices, the name being changed, hidden); the saved heroes
reloaded from <data>"mp\"; the list's scroll range, the list drawn, the first hero selected (-1 without one) and mode 9; with
the WideScreen layout then the two curtains over the screen's margins, hidden (screen_layout::AddCurtains; sprites 19 and
20). As in the original, a creature whose unit prototype has no figure is not checked for.

## MpConnectScreen::CreateScreen

The screen over 800 x 600: the "saveload" and "scrollbar" textures, the stone menu model ("unmoco2" with "MainMenu00labels",
parts "but03" and "button03_multiplayer") at (400, 350, 8) turned about x by pi / 2 * 1.2; the regions with tips 0x9E98,
0x9E99, 0x9EA2 and 0x9EA3 on the first four; sprites 0 / 1 the two buttons, 2 the selection bar (plain 0xC8A06800), 3 the
sort arrow (the scroll bar's texture, hidden), 4 / 5 the unlisted and password switches, 6-9 the game type tabs (their names
"lmp_allod_name_%d" into allodNames, the first selected), 10 / 11 two bars, three frames and three dark backgrounds (the
details' frame and background reach y 425 / 420, the master and address buttons' start at 429 / 434: 10 and 4 lower than the
original's, for the pause row); fifteen row regions, then region 29 the pause mode (440, 396)-(690, 418), the mode the
original's; surface 0 (100, 100)-(700, 500); the scroll bar, the player slider (to `net::kMostPlayers` - 1 = 13, the
original's 5; position 5, maxPlayers 6) and the name edit
(the first hero's name to its '|', at most 10 characters, trimmed; not focused) as children; the 22 "lmp_connection_%d"
texts; the client started; the queries read from config\addrbook.ini (a chunk archive, chunk 1; an extra word of 0 becomes a
random one xor the tick count) and given to the client when the file opens; nothing selected, the list rebuilt and drawn,
the help topic, and surface 1 with text 8 ("connecting").

## MpConnectScreen::DrawListVanilla

ui_connect_list.cpp (WideScreen, and Rebirth's ExtendedServerName behind its flag). The list painted on surface 0 each
frame (`PaintList`); `DrawListVanilla` sets the controls, sprites and tips and leaves g_connectTextColour as the drawing
did (the titles are drawn in the colour from before the call: `titleColour`). With row 0
(the new game) selected its controls show: the player slider and the name edit enabled (and editing), sprites 4 and 5 and
their regions' tips (0x9EA4 / 0x9EA5, set once: newGameTips; the pause region's tip is ours); otherwise they are off and
the tips empty. Texts 10 and 11 by
the two buttons, the column titles (texts 12-15), the sort arrow (sprite 3: turned for an odd key, at its column while a key
is set) and a line under the titles in the text colour; 15 rows from scrollTop in the colour of the entry's state
(g_connectTextColour: 1 0x31B3FF, 2 0x31E3EE, else 0xA7D7E4): the new game row "<text 7> <the name typed>", the others name,
players "n/m", the game type's allod name ("?" for 4) and the ping; the selection frame (sprite 2) on the selected row. Then
the selected entry's details at the right in 0xA7D7E4: for the new game the game type tabs (sprites 6-9) with their names,
texts 0, 2 (with maxPlayers), 3, 16 and 17, then the pause mode's row (ours, surface (340, 296): "Pause: as the original" /
"the leader, announced" / "any player" / "off", in Russian where the game's texts are, `GameTextsInRussian`; a click on
region 29 with the new game selected steps to the next mode, passed to StartServer as `net::PauseMode`) and, ours, the
companions switch on the game type's title row (surface 460; region 30 at (560, 214)-(700, 237): "Companions: off" /
"two", network.md, "Companions"); for a server its name, players, game type, the map's title wrapped over up to
three lines, address, ping and, with a password, text 21; ours then, once the server's info answer was read
(`net::ServerEntry::modRead`), its mod ("Mod: <title>" / "no mod" / "the original game") and, for a host this game
cannot join for the mods (`ServerEntry::ModAgrees`), why ("Cannot join: the mods differ" / "another version of the mod")
in 0x808080, the colour that host's row is drawn in too (`modidentity::Listed`; network.md, "Mods"). The quirk's colour
left for the titles stays the state's.

## Companions window

Ours (`ui_companions.cpp`; network.md, "Companions"). The list of companions is picked only in the multiplayer base,
the briefing screen of a multiplayer game (a local zone: no server link, no HUD). `BriefingScreen::CreateScreen` adds
`CompanionButton` next to the chat window when `CompanionsOffered` (a multiplayer client; a host only with its
companions option on): a framed "+" on a shade, 30 x 30 at (762, 562) in the 800 x 600 layout (bottom right), tip
"Companions (F3)"; F3 (`BriefingScreen::KeyDown`) opens the same window. A joined game does not know its host's option
in the base (it learns the limit with the first network zone), so it always offers; `SavedParties::companions` past the
host's limit are dropped when a zone asks for them. The battle screen has no button and no F3.
The window is a shaded box over (200, 150)-(600, 412): the title, one row per picked companion ("(with you)") and then
per saved hero neither played nor picked (at most 8 in all), the picked one framed (the first at first), below them what
OK does ("OK: add", "OK: send away", or why nothing); regions 0 OK (the OK button's picture), 1 Close (cancel's), 2 on
the rows. OK (or Enter) takes the picked companion off `SavedParties::companions` or puts the picked hero on it while
there is room (the host's limit, `net::kMostCompanions` for a joined game), and closes; nothing is sent. Close or Esc
closes. It leaves a value no screen takes.
The battle screen's client party puts the hero's companions (its party's records after the first, with a unit) right after
the camera's hero (`companionPortraits`), so the HUD shows their portraits with health bars beside the other players'
faces (AddPortrait centres the hero, its companions and the players); ToggleMember (portrait clicks, the member keys) then
works on a network client for the hero and its companions only.

## ExchangeScreen::CreateScreen

ui_exchange.cpp. Builds the window: texture "campinfo", regions 0 accept, 1 take back, 2 leave (tips 0xA028, 0xA029,
0xA02B), their four button sprites (the third hidden, the leave button shaded 0.5), the item screens' base with the icon
window's bars at y 300, a scratch party (made), our offer (the client's, cleared) and the partner's, then the children: the
description panel at (600, 100), the local party's item strip (forwarding, action 1, items), the players' figure strip (top,
no arrow tips), the trade grid over both offers (mode 5) and the money field (0..200 x 0x15E..0x16E, empty), and four
200-wide surfaces.

## Container draws

Each in its window's file:
- the container windows' base draw (UiContainer, ui_container.cpp): stage 0 set to `kAlphaPassStage`, then the window's own
  sprites and models and its enabled children;
- the briefing screen's and the main menu's draws (`DrawScreen`, ui_container.cpp; ui_briefing.cpp, ui_main_menu.cpp): the
  client world's frame, (the briefing screen) a mouse move to the window itself, a depth clear, then the children;
- the hero equipment panel's draw (HeroEquipPanel, ui_hero_panels.cpp): the two highlight frames placed (the shown
  weapon, the selected quick item), the weapons' (left) and quick items' (from the right) item models, and with the body
  (the camp) the seven equipment slots' models inside depth clears (all to 0, the slots' rectangles to 1, all to 1
  afterwards), as the hero preview and the trade grid do;
- the alpha reference set to the window's value.

Every state set goes to render::g_renderState; every depth clear to render::ClearDepth through `ClearUiDepth` (ui.h; ui_interface.cpp).

## Cursor in the frame

The frame function draws the software cursor after the scene and the interface, before the present, when the display
option's software cursor bit is set. It hands the image's current frame, its mask, its hot spot and the hold to the renderer
(render::Cursor), whose present shows it as the system's cursor (renderer.md section 9). The system moves the cursor; a frame
that hands none over (the movie players and the loading screens hide the image) hides it.

## Camera mouse turn

With the right button held (CameraInput::MouseMove, turning) the cursor's drag sets the camera's turn and tilt target
rates: the drag in layout units (screen_layout::FromScreenNoMargin, integer: it truncates) times the speed options, turn
`speed * dx / 400 * 10`, tilt `speed * dy / 300 * 500`. Camera::SetTurnRate / SetTiltRate jump the rate up to a larger
target; a smaller one (0 too) is approached in the camera's updates at response 1/3 a camera second (55 ms), and each
update turns by dt * rate. The interface's mouse-move events reach the camera input as well as the frame's own call
(BattleScreen::DrawSprites): the first call after the cursor's poll (once a frame, Cursor::UpdatePosition, after the interface's
draw) takes the drag, later ones get (0, 0) and set the targets to 0, so the rate eases off between takes; the original's
order, kept.

- The drag is the mouse's speed, not the frame's motion (Cursor::TakeDrag): the motion of the polls in the last 1000 / 60
  ms (or the last poll's interval, when longer), the oldest poll counted for its part in that window, scaled from the
  window to 1000 / 60 ms. At 60 Hz it is the frame's motion (frames of 16 / 17 ms: within a count); at other rates the
  same mouse speed gives the same drag, so the turn per second does not go with the frame's length (`camera_drag_rate`:
  3.80 to 3.85 rad at 4, 8, 16 and 32 ms frames, tilts within 5%).
- Taking hold (Cursor::SetHeld) polls the mouse and drops its motion so far, so motion from before the press does not turn
  the camera in the first frame, and keeps the pointer's position (the global map's drag too).
- The mouse counts are the pointer's motion on every system: SDL's relative motion with
  SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE (platform_sdl.cpp), on Windows raw input through SDL's copy of the system's pointer
  speed and acceleration, on X11 XInput2's accelerated valuators, on Wayland the compositor's accelerated relative motion
  times the window's pointer_scale (1 for the game's window, which is never exclusive full screen). So the desktop's
  pointer settings set the camera's speed too.
- The turn per count is a fixed amount, the same at every screen size: the drag in counts times 800 / 2880 layout units
  (screen_layout::kLayoutPerCount, kept as a fraction, not truncated), then the same rates. 2880 is the 4:3 part's width
  of a 3840x2160 screen (2160 * 4 / 3): the user's 4K monitor at 200% Windows scaling, whose borderless full screen the
  game sees in physical pixels (SDL's coordinates are pixels on Windows, PixelsPerPoint 1). The pan with the modifier
  held and the global map's drag (GlobalMap::MouseMove: the angle turns `counts * kLayoutPerCount / 300`) share the
  scaling. Positions (pointing, clicking, hit tests, a scroll bar's thumb following the cursor) still scale to the
  window; every response per count or wheel step is the same at any size. Already size independent: the camera's wheel
  zoom, the scroll bars' and minimap's wheel steps and the item strip's (wheel delta only), the edge pan (a fixed speed
  over the options' margin in pixels), the village and briefing cameras (the same CameraInput), the scroll bar and
  slider thumbs (placed by the cursor's position), and the briefing's map click (a position). `EI_CAMERA_TRACE=1` logs
  the global map's drag taken (`global map drag dx dy (layout) at ms: angle`).
- Harness: `mousedrag dx dy` adds (dx, dy) to each frame's poll; with `frametime` that is a mouse speed of dx / frametime
  counts a ms (`camera_spin`, `camera_drag_rate`).
- `EI_CAMERA_TRACE=1` traces it into ei-rebuild.log (src/game/ui/camera_trace.cpp): a line a second (`camera trace: N frames
  in M ms (a to b ms apart), motion x y`: the poll rate and its spread), a line a poll while held or moving (interval, the
  device's motion, the drag given, the drags taken since the last poll, heading, pitch, turn and tilt rate and target),
  and a line a drag taken (in counts and layout units, the targets it set).

## Battle HUD corner panels

The game clock (HudClock) and the movement panel (HudMovePanel): their setups make the panels' sprites and regions about
the layout's bottom corners. The WideScreen layout (screen_layout.h) puts the clock at the screen's right edge and the
movement panel at its left (each window's origin, `UiWindow::originX`, at +EdgeShift / -EdgeShift). See ui_hud.h and
rebirth.md ("WideScreen").

## Battle HUD sliding panels

The two sliding side panels, the map (HudMap) and the unit panel (HudUnitPanel): their layouts at the current slide offset,
their per-frame updates and their setups. The WideScreen layout (screen_layout.h) puts the map at the screen's right edge
and the unit panel at its left (each window's origin, `UiWindow::originX`, at +EdgeShift / -EdgeShift). See ui_hud.h
and rebirth.md ("WideScreen").

## Battle HUD unit panel draw

HudUnitPanel's draw. With the WideScreen layout the height bar and the model are placed from the window's origin
(left by EdgeShift), the depth buffer's rectangles scaled without the margin. It clears the renderer's depth
(`ClearUiDepth`) and sets render::g_renderState, as the hero preview does (ui_hero_preview.cpp).

## Item description drawing

The item description panel: what the camp, the multiplayer hero screen and the exchange window write about the item or
statistic under the mouse (ui_item_info.h; section 9). Each function writes text lines into the panel's labels (`labels`,
which surface 0's paint draws each frame): single lines, and wrapped ones for the title and description. Values of the statistics
functions (UnitStats, WeaponStat84 / 88, WeaponPrototypeSkillBonus) are truncated as they come back, unrounded (those
functions return double), so no rounding happens in between.

## Save / load window pictures

The save / load window's saving, its entry list and the selected save's picture (ui_saveload.h, SaveLoadWindow; renderer.md sections
6 and 7).

**shot.sav** (`save_picture.h`). The original's file is 256x192 5:6:5 pixels (0x18000 bytes, rows from the top, no header),
and the original game reads exactly those bytes. Ours writes them first, then a trailer the original never reads: "EIPC",
version 1, width, height, the length of a QOI file (`src/lib/qoi.h`, 8 bits a channel, 3 channels) and the file
(numbers little-endian 32 bits). Our loader takes the trailer's picture when its magic, version and QOI file are whole,
else the 5:6:5 one (an old save's, the original game's); a short file's missing rows are black. So one file serves both
games and old saves keep their pictures. The trailer at 4K is about 1 MB (the free-space check's 1 MB margin is not
grown for it).

**Sizes.** A picture is kept at the size the window's picture rectangle (`SaveLoadWindow::kPictureRect`, 290x220 layout
units) has on the screen it was made on: 290x220 at 800x600, 1044x792 at 3840x2160. The 5:6:5 picture is the copy's pixel
under each 256x192 centre ((2x + 1) * size / 512, each channel's top bits: the original's); the trailer's is the copy
shrunk to the rectangle, each pixel the average of the copy's pixels it covers. Both come from one copy of the screen:
the escape menu's (`Interface3D::screenCopy`: the whole window, as the backdrop) for the save window, the 800x600
layout's rectangle for QuickSave (the screen with the interface, as the original's).

**Showing.** ShowPicture reads the selected save's shot.sav, shrinks a picture larger than the rectangle is here to fit it
(a 4K save shown at 800x600) and makes a texture of the picture's own size (`picture`; a 5:6:5 picture is 256x192); the
sprite shows the whole texture (`ShowTexture`: the texels half a texel in, as SetRectangle's) over the rectangle, linear
filtered by the stages' filters, so a smaller picture is stretched smoothly. The save window's new save is `newPicture`
(the shot.sav bytes FillEntries makes from the escape menu's copy), shown as `dialog`; SaveGame writes those bytes.
Checked by ui_saveload, ui_quicksave (checksave compares both pictures with the frame; `oldshot` cuts savea's file to
the original's and the Load window shows it) and the unit checks (tests/unit/save_picture.cpp).

## Surface blit

A window surface's draw (UiSurface::Blit; called by UiWindow's surface pass and the interface's draw for the backdrop and
the frame rate surface): when the surface is shown, its frame picture and what its window's paint draws are drawn into the
frame at its rectangle, clipped to it and the screen; a keyed
surface (SurfaceBlend::Keyed) leaves out what is in its background colour.
Every surface is opaque or keyed; the original's stretch bit was never set. See ui-native.md section 3.

## Surface construction

The window surface's constructors and destructor (UiSurface): one taking (interface, layout left, top, right, bottom, blend,
scale flag) and one taking (interface, layout rectangle, blend; always scaled). Both scale the rectangle to the screen
(where WideScreen sits) and set the shown flag; an empty rectangle ends the game ("Can't create DDSurface", as the
original's holder did). A new surface draws nothing until it has a frame picture or a paint (the original's was black:
every opaque surface's paint fills its background).

## Surface pixel work

The interface surfaces' pixel work (UiSurface, ui.h): copying the screen into a surface, a 16-bit 5:6:5 copy of a surface at
a given size (the save game's picture) and greying a surface in place ((2r + 4g + b) / 16; the escape menu's background).
The escape menu's drawing runs all three when the menu opens. All three run on the GPU: the copy is a frame picture of the
surface's size, the greying a shader option when it is drawn, and the 5:6:5 copy is drawn at its size and read back
(renderer.md section 6, "Screen copies on the GPU").

## 11. Behaviour moved out of header comments

Long header comments of ui.h, ui_camp.h, ui_battle.h and the other UI headers; the header keeps the gist.

### BattleScreen::LeftUp

Input (ui_battle.cpp). The left button released: nothing while a portrait click picks a player; without a press on the
view the container's handler; a press without a drag, or on a network client, is a click on the unit under the point (units
with flag 0x400000 only) through SelectUnit; a drag selects the party units inside the box: the world's selection cleared,
the box marks and portraits' markers reset, each unit inside controlled with its portrait marked and its face turned (about
-x by kHudQuarterTurn / 7), then the speech event with the first one's id; then RefreshSelection.

### BattleScreen::MouseMove

Input (ui_battle.cpp). The mouse moved (also every frame from Draw, at the current position): while a portrait
click picks a player the cursor shape 0xD and the portraits' own mouse move; otherwise shape 0, then with a press on the
view, a positive BoxSelectThreshold and not a network client: a move farther than the threshold from the press point in x or
y starts the selection box, which empties the unit panel's hover unit and spans the press point and (x, y); before that the
unit under the point is hovered at once. Without a press the container's handler first, and when no panel took the move the
unit under the point (any flags) goes to HoverUnit.

### BattleScreen::KeyDown

A key pressed (ui_battle.cpp; Rebirth's MPSpeedControl behind its flag). While a portrait click picks a player, Esc
ends that and nothing else happens. On a network client Enter / Backspace go to the chat line and the key is used.
Otherwise the container's handler first; a key nobody used and not repeating: Esc drops the pending action with every
panel's highlight, or without one opens the escape menu; any other key by its action (KeyActions, by the scan code): 1
pause (the clock's sound), 2 / 3 normal / fast speed (not while paused or on a network client: the world's update period 55
/ 27 ms), 4 the objectives window of the player's zone, 0xB the camera follows the first selected member (or nobody), 0xC
the camera faces north, 0x11..0x16 the order keys (orderKeys), 0x17..0x1A the selected members' movement mode 0..3 and 0x1B
their flag (the movement panel), 0x1C / 0x1D the action buttons "follow" / "science", 0x1E..0x25 spells 0..7 and 0x2A..0x2D
quick items 0..3 (cast or used at once with Ctrl or Alt), 0x26..0x29 weapons 0..3, 0x2E..0x31 the unit panel's body parts
and 0x32 / 0x33 the top panel's pages (a panel slid away comes back, the choice already shown slides it away), 0x34 the map
slides, 0x35 / 0x36 quick save / load ("saveq"), 0x37..0x39 party members 0..2 (ToggleMember; with Ctrl or Alt the camera
follows the member), 0x3A every member selected (not on a network client), 0x3B the message window opens. Pause, the
objectives, the movement keys, the message window and the escape menu mark the key used.

### OnQuestVariable

A "q." player variable changed while the screen is on top (the quest log passes it on). `name` split at '.': three parts
(q.<zone>.<quest>) a quest line, "combat_get_quest" / "_complete_" (value 2) / "_failed_" (3), then the quest's title (text
"quest <quest>"); four parts (a sub-objective) the same with "_subobj" and the "#subobj <n>" line, skipped on a single player
whose variable "<a>.<b>.<c>" is 0. The line (any part count) goes to the top panel's text and, unless suppressed, the message
log; the clock pulses, and the pending quest sound becomes at least the value (1 for a failure, 3 for a completed quest).

### RefreshSelection

Called when a unit joins or leaves the party, after items are picked up, by the member toggle, a box drag and a network
client's party change. With exactly one living party unit selected the weapon bar, quick items, spell and action columns
show it, otherwise they are emptied (the action column keeps its extra button while anyone is selected); the unit panel
shows the first selected unit (null with nobody selected) when it changed; the pending action and every panel highlight are
cleared.

### ToggleMember

Party member `index` clicked (its portrait or body, SelectUnit; the member keys): nothing on a network client or for an
index past the party. The selection is the members' flag 0x10; with Shift the member flips, else it alone is chosen. The
world's selection is cleared; each portrait's marker gets the chosen value and its face's rest orientation the turn about -x
by kHudQuarterTurn / 7 (or the identity); each chosen member is controlled and the clicked one gets a speech event (id, 0,
0). In single player the local player's unit records follow flag 0x10; then RefreshSelection.

### HelpWindow::ShowPage

The page setup (ui_help_window.cpp). Empties the two lists, the surfaces, regions and scene objects, opens "tutorial <text>"
(without it the pending value is set to 0) and goes to the page's "#screen" line. The page's rectangle is (200, 50)-(600,
560), or "#position left top right [rows]" (at least 300 wide, rows x 16 + 110 high, at most 600). Scene objects 0 the plain
background, 1 the frame, 2 / 3 / 4 the previous, OK and next buttons; then one pulsing frame per "#rectangle left top right
bottom" line (with the WideScreen layout some zone tutorial pages' frames move by the layout's extra units, as the battle
screen's panels) and one entry per "#text [picture]" line (the next line's first word is the text's key). Surfaces: 0 the
page's title and "title (page/pages)", 1 the entries' wrapped "tutor <key>" texts (laid out twice when no rows are given: the
first pass makes the pictures, "tutorial_<picture>" copied into referenced object 1's 64-pixel cells, and gives the height),
2 the buttons' texts with the help key's name. Regions 0-2 the buttons.

### GlobalMap::MouseMove

The mouse moved (WideScreen: the drag scaled without the margin). While dragging: the drag
motion turns the map: angle += dx / 300 (layout units), taken mod kGlobalMapFullTurn, and turn = the tilt (pi * 0.8 about x)
times the spin about z; the motion is cleared. Otherwise the zone or link under the mouse is selected (a link's region 3..;
or, off every region, the zone whose marker is under the point (`ZoneAt`), on gipat its link when it has one; nothing
while a turn button is held) and its tip shown: the first surface with the title and the titles of the zone's quests, the
last two sprites behind it; a zone in state 0 or nothing hides the tip. A new name plays "buttons\globalmap\zone.wav".
Regions past the links do nothing.

### GlobalMap::LeftDown

The left button pressed (nothing while dragging; ui_global_map_paths.cpp). Regions 0 / 1, the turn buttons: turning -1 /
+1 and "buttons\globalmap\move.wav". Region 2, the camp (unless noCamp is 1): "buttons\globalmap\transit.wav", the
constructor 0 and screen 5 asked for. A link (regions 3..), or on gipat a zone that has one: "buttons\globalmap\ok.wav",
the ways to the link's zone searched from the local player's zone and, when there is one, the time of arrival exit 0
passed and the zone asked for at entrance 0. Any other zone under the mouse (`ZoneAt`; its state not 0): the sound, the
search, and the objectives window opened over the map with the zone, the arrival times, two buttons and no zone list.

### CampPages::BuildPages

Create's body. Builds the camp: the server flag set; the constructor's class template from the player's "constr.current"
(none found: back to the world, nothing built) with its flags, price weights and part lists; the player's "i.noconstr" /
"i.off..." / "i.on..." switches; texture "campinfo", twelve regions (the two action buttons, the two hero turn areas, the
eight page buttons) and ten button sprites; the item screen's base; two surfaces; seven item lists; the panels
(description, statistics, help, the item strip, equipment, quick items, trade, and each offered constructor's strip and
pages); the party's heroes handed to the client world as units (records of class 0x32, ids raised by 100000000 meanwhile),
each idling, armed from its record and wearing its armour; the first hero shown the page titles; the first page (4 item
constructor, 2 spell constructor, else 0); a constructor's stock made on its first visit (restockDue) from the party's
lowest and highest skill per spell school; the allod's constructor music; antialiasing (render state 2 = 2) when the device
sorts independently.

### BriefingScreen::MouseMove

With the chat window active, cursor 0xD and the event to it; otherwise cursor 0, the container's handler, then by state: 1
the hovered topic row (redrawn when it changes), 3 cursor 0xE, 4 cursor 0. Walking (state 0, right button up): over a person
who can talk (as LeftDown; on a network client not the leader) its topics are looked up (cursor 0xE when it has any) and its
"pers" text, when it has one, is shown in surface 1 at the mouse (-125, -20); over a zone exit, cursor 0xF when the zone of
exit (not of the exit found: kept) is not "none" and its "z.<zone>" variable is not 1.

### BriefingScreen::KeyDown

With the chat window active only Esc, which gives the input back. Otherwise the container's handler, then Backspace / Enter
go to a network client's chat line; Enter (single player) and Esc skip the phrases (with 1 until the state changes) or take
the rewards; Esc also opens the escape menu (state 0), ends the start or the topic choice (1); Space the next phrase. Other
keys while walking by their action: 4 the objectives window when a quest of this allod ("q.<quest>.<zone>.." = 1) is
active, 0xB the camera to the leader, '5' / '6' quick save / load "saveq". Every handled key sets handled.

### BriefingScreen::StartDialogue

Dialogue topic starts: "goodbye" (or no leader) back to state 0; a "constr..." topic opens the camp (screen 5) with
constructor N (the topic from its eighth character) when that class template exists. Otherwise the "#show" lines of the
text "briefing <topic>" name the people: the first one not the leader is person, a second third; without one, state 0. The
places: the person's (or, with noWalk, the leader's) position, the other 2.5 + viewDistance along its facing, the third one
beside the middle; their facings across, flat and normalised. With `remember` the people's positions are kept first
(personHome, thirdPlace). A mover of the third person (or of the person, noWalk) is dropped, its target kept unless it was
waited for; then the leader (or, noWalk, the person) and the third walk to their places (waited, `immediate`). `immediate`
starts the phrases at once (StartPhrases), else state 2.

### BriefingScreen::FinishDialogue

The dialogue ends (the rewards taken, or skipped): the voice stops, the music fades in, the shown items go, every speaker
leaves (the server hears event 2, its blink stops, its server unit's flag 0x40000000 (HelmetHidden) back, height 0, action
dropped) and the list is emptied; the leader and the person back to height 0, the third speaker walks back to thirdPlace
(and is forgotten), with noWalk the person to personHome. The first "b.*.<topic>" variable (three parts) completes (for
player 0). State 0, the server hears event 0 with the leader; a dialogue that started by itself (briefingStarted) puts the
camera on the leader (message 0x04) and frees it, otherwise it goes back to savedCamera / lookAt.

### BriefingScreen::ShowItem

"#show <name>": a person `name` joins the speakers (moved to the end; the server hears event 1; the leader's server unit
gets flag 0x40000000 (HelmetHidden: no helmet shown); its controller's actionDone the speakers' callback). Anything else with
`index` >= 0 is a loot item ("_" read as " ", lowercased, the loot table's name index): an item of that name shown already
goes, then slot `index` % 7 (x 300, 500, 200, 400, 600, 75, 725; y 300 or 500) gets a backdrop and frame for the first seven,
the item's figure "initqu<n>item" (tinted from index 8) with its image "quitem<n>" in a new texture, turned over about x; it
goes first in shown. The third argument is not read.

### BriefingScreen::NextPhrase

The next phrase of the current text: its lines up to the next "#phrase <speaker> [voice]" are carried out ("#nolips",
"#show", "#hide", "#animation n", "#camera n [angle distance height]"); the phrase (up to the next '#') goes, headed by the
speaker's name, into dialogueText and (unless `skip`) the text window, with its voice "<t|s>\briefing\<topic>\<n>.mp3" and
phraseTime. After the last phrase: state 4 and the briefing record's rewards as lines (experience, gold, items given and
lost, quests). The camera then views the phrase's speaker, who talks (an animation, blinking unless "#nolips") while the
others listen; skipping to the end shows the whole text at once.
