// Automated test harness (verify build only).
//
// tools/run_all.py starts a run with EI_HARNESS set to the scenario path. The
// scenario is a text file of steps, run on the game's main thread from its message loop:
//
//   wait <milliseconds>       let the game run (prefer a step that waits for the state wanted:
//                             waitmenu, waitwindow, waitfor, waitline, waitzone, frames)
//   waitmenu [seconds]        wait until the main menu is up and takes input (as "waitwindow
//                             <seconds> menu", default 30 s): the first step of a scenario, before
//                             its first console, newgame or menu click
//   waitwindow <seconds> <window>
//                             wait until <window> is the window on top of the interface and takes
//                             input: no game request pending (007AFE20), the save/load and options windows
//                             done sliding in (they ignore input meanwhile), on top for two frames;
//                             fails after <seconds>. <window>: menu, battle, briefing, map, camp,
//                             view, trade, escmenu, saveload, options, objectives, help, msgbox,
//                             difficulty, heroes, titles, mpconnect, masterbox (image/harness_windows_image.cpp)
//   ifwindow <window> <step>  the step only when <window> is on top now (the names of waitwindow), else skipped
//   waitdialogue <seconds> <state>
//                             wait until the base screen (briefing) is on top with its dialogue in <state>
//                             (BriefingScreen::state: 0 walking, 1 a person's topics open, 2 the dialogue
//                             starting, 3 phrases, 4 rewards), in a main loop pass (input steps follow)
//   talk <seconds>            click through the base screen's dialogue: a click at the layout's centre in
//                             states 3 and 4 (the next phrase, then the rewards taken), two frames apart,
//                             until it is back in state 0 (a topic without phrases at once) or the game
//                             has left the base for a battle zone
//   waitunit <seconds> <name> wait until the unit with script name <name> is drawn and the game's
//                             picking finds it, as "clickunit" needs (a unit just added to the
//                             server reaches the client and the screen a few frames later)
//   waitidle <seconds> [<frames>]
//                             wait until the local player's leader (player 0's first party member in the
//                             server world) has stood idle for <frames> frames in a row (default 10): no
//                             action (a walk that ended or that a fight stopped, a fight over) and no path
//   walkto <seconds> <x> <y> [<frames>]
//                             walk the leader to map point (x, y) as a player does: the camera brought over
//                             it (lookat, at most twice), a double click on the ground there (clickground
//                             dblclick), then as waitidle with <frames> (default 20); the leader standing
//                             further than 3 units from the point (a fight stopped the walk) is clicked
//                             there again, at most 8 clicks
//   waitdead <seconds> <id> ...
//                             wait until each of the server world's units <id> is dead or gone
//   waitaward <seconds> [served]
//                             wait until the leader's experience or its party's money changes (an award
//                             applied: ClientSession::ApplyAwards; net_pair_quest): from what they were at
//                             the last "awardfrom" step, or else at this step. "served": instead until the
//                             game server has queued awards and they are all gone again (each is kept and
//                             replicated for 60 s; net_pair_leave's final award changes neither)
//   awardfrom                 note the leader's experience and money for the next waitaward (an award that
//                             may come while the steps between still run)
//                             (these four read the server world after the frame's update, as the
//                             digest does: they end in the same frame every run)
//   hit <target> <attacker> <amount>
//                             the server world's unit <target> takes a blow of <amount> damage from unit
//                             <attacker> (none: no attacker, as a script's InflictDamage): Unit::ReceiveHit, so the
//                             attacker gives the hit a side (HitDirection) and the game's own rules decide what it
//                             does, also in a network game where "@" commands do nothing. A unit is "leader" (the
//                             local player's first party member), "companion" (server player 0's first companion),
//                             "enemy" (as a target: the living unit nearest the attacker, or the leader, that
//                             is hostile to it), "#<id>" or a script name (as GetObjectByName)
//   giveitem <player> <item>  the server world's player <player> (its id: 0 the host's own) is given a new item
//                             <item> (a database name, as the item manager's CreateItem takes it), as a script's
//                             GiveItem(player, item) does, also in a network game (net_pair_database)
//   frames <n>                let the game run <n> frames of its main loop (after the input posted
//                             so far has been handled): for what follows a posted key or mouse
//                             step on the next frames, whatever the frame rate
//   console <command line>    run a console command (the game's own console: exec, server, give,
//                             ...). After the cheat word ("console thingamabob"), "@<command>" runs a
//                             world script command and "#<expression>" prints a script expression.
//   newgame                   start a new single-player game, as the main menu does (no intro)
//   load <slot>               load a saved game (a folder in harness_saves, see below)
//   keydown <key>, keyup <key>
//                             only the key's press or release (as "key" names it; no WM_CHAR): a key
//                             held over frames, e.g. an arrow key's camera pan (camera_smooth). The
//                             navigation keys come extended, as from a keyboard (see PostKey)
//   key <key>                 press and release a key in the game window: a letter or digit,
//                             or esc, enter, space, tab, f1..f12, up, down, left, right, plus, minus,
//                             grave (the console key), backspace, delete, home, end, pgup, pgdn, ctrl,
//                             enterchar (Enter with its WM_CHAR, as text input such as the console needs).
//                             A letter or digit is typed once (its WM_CHAR, lower case); the game's
//                             TranslateMessage translates as the US layout with no key held, whatever the
//                             user's own keyboard does (FixedKeyChar).
//                             Input steps (key and the mouse steps below) are posted from the game's
//                             main loop only: the pump that window switches run drops waiting input
//                             (see BeforeEvents)
//   click <x> <y>, rclick <x> <y>, move <x> <y>
//                             left/right click or move the mouse at (x, y) in the 800x600 layout every
//                             interface screen is designed for (scaled to the window's client size)
//   dblclick <x> <y>, rdblclick <x> <y>
//                             a left / right double click there (down, up, WM_xBUTTONDBLCLK, up)
//   ldown <x> <y>, lup <x> <y>, rdown <x> <y>, rup <x> <y>
//                             only the left / right button's press or release there (a button held
//                             over frames, e.g. the camp trade panel's repeated school purchase, the
//                             global map turned by a right-button drag)
//   mousedrag <dx> <dy>       from the next frame on, after each frame the game's mouse moves by
//                             (dx, dy) counts and a WM_MOUSEMOVE at the harness's mouse follows: a drag at a
//                             steady speed, e.g. the camera turned with the right button held (rdown /
//                             rup; camera_spin); "mousedrag 0 0" ends it
//   wheel <x> <y> <delta>    turn the mouse wheel by <delta> (120 a notch; negative: towards the
//                             user) with the mouse at (x, y) in the 800x600 layout
//   clickregion <n>, rclickregion <n>, dblclickregion <n>, moveregion <n>
//                             the same at the centre of clickable region <n> of the window on top
//                             of the interface (its regions as the screen's code numbers them;
//                             placed with the game's own scaling, so Rebirth's widescreen layout too)
//   droplist open | pick <n> | expect <text> | isopen <0|1>
//                             the first drop-down list of the window on top (the options' resolution row):
//                             click its row; click entry <n> of the open list (scrolled into view first);
//                             fail unless the chosen entry reads <text> ("1280 x 720"); fail unless the
//                             list is open (1) or closed (0)
//   clickunit <name> [l|r|move|dbl] [ctrl]
//                             left click (default), right click, the second half of a double click
//                             (double click message and release: what follows a first click that
//                             missed the unit) or move the mouse on the unit with
//                             script name <name> (as GetObjectByName) where the game's own picking
//                             (00565E40) finds it in this frame; modifiers as below. Fails when the unit is not drawn or always hidden
//                             behind another object. "#<id>" names a map object by id instead (a
//                             lever, "clickunit #1889"): found by the battle screen's object pick
//                             (005659D0), as waitunit does
//   clickground <x> <y> [click|dblclick|rclick|move] [mods]
//                             that mouse step (default click) at the screen point where the game's ground
//                             pick (0056C350, what a walk order asks) finds map point (x, y), searched over
//                             the 800x600 layout; fails when no point comes within 1.5 units (off screen)
//   groundview                log the map points the ground pick finds at the layout's centre and corners
//   minimap <cx> <cy> <sx> <sy>, lookat <x> <y>
//                             the minimap's centre (layout) and map units per pixel; lookat clicks the
//                             minimap to bring the camera toward map point (x, y) (see LookAt)
//   camera <x> <y> [<heading> [<distance> [<pitch>]]]
//                             place the view camera to look at map point (x, y) at once, in WinMain's loop:
//                             no unit followed, no pan; the heading (degrees about z, 0 the camera's north,
//                             90 turned so that screen right is +y), distance and pitch (degrees) set when
//                             given, else kept. Logs the pose after it ("camera" alone only logs it). The
//                             game's limits still ease a distance outside them back (Camera::Update)
//   camrange pitch|distance|x|y|ground <least> <most>
//                             require the view camera's pitch (degrees down from level), distance, focus x / y or
//                             its near plane's lowest height above the ground (the drawn view's; image::CameraView)
//                             within [least, most] and log its pose (camera_limits.txt)
//   climate <n>               the server world's climate (0 none, 1 rain, 2 snow, 3 storms; a change of
//                             climate clears the weather): "climate 0" keeps rain from starting by chance
//                             (one roll in 101 every 64 ticks on zone1) while a scene is captured dry
//   weatherstate <weather> [<least density>]
//                             require the server world's weather (0 clear, 1 rain, 2 snow) and the client's
//                             rain / snow layer heading for it with at least <least density> (0..64); logs
//                             the weather's fade, its sight factor and the layer's mode (R57d)
//   hold <mods>, release <mods>
//                             press / release the modifier keys <mods> (ctrl, shift, alt; separated by
//                             spaces, "+" or ","), held over frames and further steps. The game takes
//                             them from the window messages (Interface3D +0x7C Ctrl / +0x7D Shift /
//                             +0x7E Alt), never the real keyboard, so the original game and the image
//                             see the same. Alt is sent as WM_SYSKEYDOWN / WM_SYSKEYUP, as Windows does
//   <mods> as the last words of key, click, rclick, move, dblclick, rdblclick, ldown, lup, rdown, rup,
//   wheel, clickregion, rclickregion, dblclickregion, moveregion, clickunit (e.g. "click 90 320 shift", "key 1 ctrl",
//   "clickunit Wolf0 l ctrl+shift"): the modifiers are pressed just before the step's input and
//   released just after it (a plain "key ctrl" is still the key ctrl)
//   hudstate [minquick]       log the battle screen's pending action, the party's selection and
//                             quick items, the quick item panel and the spell column; with
//                             <minquick>, fail when the quick item panel shows fewer of its unit's
//                             own quick items (ui_quick_items)
//   campputon <type> <count>  with the camp on top: put the first <count> of the player's items
//                             of class type <type> (hex, e.g. 3006 quick items) on the hero, as a
//                             click on them in the item strip does (the camp's slot 42, mode 1)
//   screen <id> [constr]      ask the server for interface screen <id> as the global map's buttons
//                             do (4 the global map, 5 the camp; the battle screen is left). For the
//                             camp, <constr> (default 0: none) is the constructor / trader class it
//                             opens with, as a base dialogue's "constrN" answer (006064E0) sets it
//                             (classes 1, 3, 4, 5 after a new game); fails when there is no such
//                             class template
//   chat 1|0                  open the chat line (the interface's root) as a multiplayer client's Enter
//                             does, so that its typed line and caret are drawn; or close it
//   typeunicode <text>        <text> typed, each character a Text event of the platform's (harness_utf8.h)
//   consoleline <n> <text>    require the console's input line to hold <text> in UTF-8 with the caret at the
//                             start of its character <n> (from 0)
//   utf8text                  Cyrillic case-insensitive compares, case changes and the player name filter by
//                             lib/text_case.h's table (harness_utf8.h)
//   chatwrap <text>           <text> added to the chat overlay breaks into lines of valid UTF-8 that join back
//                             to it (harness_utf8.h)
//   utf8files                 a folder and a file with Cyrillic names made, listed, read back and removed
//                             through the game's file calls (harness_utf8.h)
//   netcut                   in a pair's game: its network cable pulled from now on (harness_pair.h): nothing
//                             it sends arrives, what the other game sends it is lost; both sessions time out
//   hidefile <path>           an install file (relative to the game folder: maps\z6q1.mq) reads as missing in
//                             this game until the game writes it (then its overlay copy is read): a player
//                             who lacks a file the host has (the shared quest files, net_pair_leave)
//   samefile <path>           require that this game wrote its own copy of that install file (in the overlay)
//                             and that it is the install's, byte for byte (the received quest file)
//   masterhost <host[:port]>  the options' Master Server Name (0079BED8) for this run, not saved: the
//                             master the connection screen asks and a public game announces itself to
//                             (net_second_master: 127.0.0.1 only)
//   shadows 0|1               the options' Shadows for this run, not saved (the harness's settings have it on)
//   fakemaster <port> [list|badsize|short]
//                             in a pair's game: a master server on the pair's virtual port <port> answers
//                             this game's announcements and its master requests with the announced games'
//                             list (or a bad one: harness_pair.h; with "masterhost 127.0.0.1:<port>")
//   nolan                     in a pair's game: its broadcasts are lost (the lobby's ping finds nothing)
//   netstate                  the server's connections, the lobby client's join state and listed servers as
//                             console lines "netstate: ..." (harness_pair.h; a golden keeps them)
//   netmod                    the mods the lobby compares as console lines "netmod: ...": this game's, the
//                             client's join state, refusal and the host's mod, the server's refusals for a mod
//                             (image::NetMod; a mod by its title and whether its files are this game's)
//   originalnet               this game's lobby acts as the original's from now on: no mod mark sent or read, its
//                             server admits any mod (net::g_originalPeer)
//   badupdate                 in a pair's game: its next update to the other game carries a record of an
//                             unknown type (harness_pair.h): the other game drops its connection
//   peer <name>               run_all's: start a second game beside this one on tests/scenarios/peer/<name>.txt,
//                             the two in lockstep on a network of their own (harness_pair.h; net_pair_*.txt)
//   peersignal <tag>, peerwait <seconds> <tag>
//                             in a pair's game: mark <tag> as reached; wait until the other game has marked
//                             <tag> (fails after <seconds>)
//   zone <name> [exit]        leave the zone as its exits do: the server's LeaveToZone (0066F310) for
//                             the local player, to map.txt zone <name> (gz2g, bz1g, ...), the party
//                             arriving at that zone's exit <exit> (from 0: map.txt's "#exit n" minus
//                             1; default 0). Needs the hero alive and in the world; fails otherwise
//   waitzone <seconds> <name> wait until zone <name> is loaded and the local player is in its world
//                             again (game and brief zones; the party's units come a little later)
//   check <expression>        print the script expression through the console ("#<expression>")
//                             and require a nonzero result (waits up to 3 s for the output)
//   timeout <seconds>         the whole scenario's time limit (default 60 s); past it the run
//                             fails and the game is closed (the watchdog thread; if that cannot run,
//                             run_all reads this line too and ends the game 20 s later)
//   hang main|dump|all        TEST ONLY, for the time limit's own checks (tests/scenarios/watchdog_*.txt,
//                             "# expect: TIMEOUT"): block the main thread for good; "dump" also blocks
//                             the thread that logs the main thread's stack (a main thread that cannot
//                             be suspended does that), "all" stops the watchdog thread as well, so only
//                             run_all can end the game
//   modfile <archive> <file> <bytes>, modlist <archive> <pattern> <count>, modarchive figures|links|textures <file> <bytes>,
//   mapfile exists <name> yes|no, playstream <name>.mp3
//                             Rebirth's mod-folder patches asked directly (harness_starter.h; S19d)
//   modtext <name> <text>, modack <unit> <speech> <sound>, modweapon <weapon> <min> <max>,
//   modrecord ack|anim|track|blood|footprint|scorch <name> = <text>
//                             a mod's records as TOML, and an animation's track, seen by the game (harness_starter.h;
//                             MF3, MF5)
//   ground <x> <y> <low> <high>  the ground's height at (x, y) in the landscape and the AI map (harness_starter.h)
//   waitline <seconds> <text> wait until the console has printed <text> (at any time in the run)
//   read mod|game <path> yes|no, wrote mod|game <path> yes|no, waitread <seconds> mod|game <path>
//                             whether the game touched (read, listed, asked about) or wrote the file below the mod
//                             folder or the game folder (harness_modtrace.h; S19d)
//   waitexpect <seconds> <text>  like expect, but waits up to <seconds> for <text> to appear (for
//                             output that follows posted keys, which a busy machine handles later)
//   frametime <ms>            on the virtual clock: each main loop frame takes <ms> (default 0: five
//                             world updates, the most one pass runs); e.g. 8 for a 120 Hz screen's frames
//   camtrace start [log] | stop <name>, camsame / camsteadier <a> <b> <kind> <fraction>, camsmooth <name> <kind> [jerky]
//                             the camera's motion frame by frame and its checks (harness_camera.h, R18, R35a)
//   speed <ms>                run one world update per <ms> of real time (game default 55, its own
//                             speed-up key 27; smaller is faster); 0 leaves it to the game
//   checktick <ms>            require the world update period to be <ms> now (the speed keys' effect)
//   checklimit <n>            require the client session's player limit (ClientSession::PlayerLimit) to be <n>
//   checkpaused 0|1           require the server world's pause flag to be off / on (the host's pause mode)
//   companions <n>            require server player 0's party to hold <n> records (its hero and companions)
//   companionlist <n>         require the companions picked in the base (SavedParties::companions) to number <n>
//   askcompanion <name>       send the add of saved hero <name> from the local client as a mid-game add (the server
//                             refuses it: the list changes in the base only)
//   heroxp <name> [more]      log the experience of the saved hero <name>, its file read from the disk; with
//                             "more" require it above what the previous heroxp of <name> logged
//   waitfor <seconds> <expression>
//                             evaluate the expression every 250 ms until it is nonzero; fail if
//                             that takes longer than <seconds>
//   expect <text>             require <text> in the console output since the previous expect
//   reject <text>             fail if <text> appears in the console output from now on
//   allow <text>              lines containing <text> are exempt from reject (a known message, e.g. a mod's
//                             AddMob'd script the original drops too)
//   report                    write the verify totals to harness_report.txt
//   diskfull <text>           TEST ONLY (harness_disk_full.txt): writes to files whose path contains
//                             <text> fail with ERROR_DISK_FULL from now on (harness_crash.h)
//   expectcrash <text>        the run passes when the game crashes, or ends on a message box (a fatal error's),
//                             with a failure line containing <text> (or one of "<text> | <text> ..."), and
//                             fails at "quit" when it has not
//   quit                      write the report and exit the game through the console
//   realtime, deterministic   the game on real time, or (the default) on the virtual game clock
//                             with fixed seeds, where runs repeat exactly (see below)
//   clocktrace, randtrace     log every clock read and wait (ei-rebuild.log), every random draw
//                             (randtrace.txt; harness_clock.h)
//   vanilla <step>, rebirth <step>   run the step only in that install
//   layout <mouse step> <x> <y> ...  the mouse step (click, move, ...) at layout point (x, y) of the 800x600
//                             interface, placed in the window's centred 4:3 part (the other mouse steps spread
//                             their points over the whole window): the same place at any resolution
//   secondmaster <host[:port]>  NewMaster's second master server for this run, over the settings file's
//                             [network] SecondMaster (net_second_master.h). Read before the game starts.
//   resolution <W>x<H>|desktop, widescreen 0|1
//                             answer the game's VideoResolution and the starter's WideScreen setting
//                             for this run (game.exe's and addon.dll's registry reads; read before
//                             the game starts; harness_starter.h)
//   capture <name>            write the frame presented after the next two to frames\<name>.bmp in
//                             the output folder (the render target before the software cursor and
//                             the flip; harness_capture.cpp); fails after 10 s without a frame
//   capture <name> <count> [freeze] [after <text> | on <window>]
//                             a burst: the next <count> presented frames, one after another, to
//                             frames\<name>_1.bmp .. _<count>.bmp; with "after", from the first frame
//                             presented after the console prints a line holding <text> (a load's
//                             "Loaded <slot>": its first frames); with "on", from the first frame
//                             presented with <window> on top (waitwindow's names; a return to the main
//                             menu); with "freeze", the clocks frozen (as "freeze") once the first is
//                             written, so the later ones differ from it only by what is drawn late;
//                             fails after 30 s without them all
//   freeze, unfreeze          hold the game's clocks (timeGetTime, GetTickCount) and the camera for
//                             captures: no world, client or camera updates, no clock-driven
//                             animation, while the frames keep being drawn. Do not "check"/"waitfor"
//                             while frozen (script output needs world updates); a scenario that
//                             freezes also sees a fixed date (as in deterministic mode)
//   focus off|on              the focus lost (WM_ACTIVATE inactive, the window minimised) or given
//                             back (restored, WM_ACTIVATE click-active), as Windows does it
//   windowsize <w> <h>        resize the game window (its client area) to <w> x <h>
//   fullscreen 0|1            answer the game's FullScreen setting for this run (harness_starter.h)
//   music <volume>            answer the game's AudioStreaming setting (the streamed music's volume) for
//                             this run; 0: no streamed music, so the music system's random draws do not
//                             follow the stream (harness_starter.h)
//   expectlog <text>          require <text> in ei-rebuild.log so far (the code's own log lines, e.g. the WideScreen layout's)
//   logmark                   the log's end now: the "new" log steps below look only at what comes after the last mark
//   expectnewlog <text>       require <text> in ei-rebuild.log after the last mark (then the mark moves to the end)
//   expectnonewlog <text>     require <text> NOT in ei-rebuild.log after the last mark (then the mark moves to the end)
//   settingsfile writable|defaults
//                             the image's settings file (ei_settings.ini in the run folder) is not read only:
//                             "writable" starts from the snapshot's file with a probe section of odd values and lets
//                             the game's exit save rewrite it (.tmp, then moved); "defaults" starts without a file,
//                             which the first use writes with the defaults (for a 1280x720 desktop), as the
//                             product's first start does. run_all compares the file after the run with the
//                             scenario's "# settings-file:" line (harness_settings.h)
//   waitpresent <seconds> [gamma]
//                             wait until the switch-over has presented a frame on the game window
//                             (its own swap chain); with "gamma" also until the verify build has
//                             checked a presented frame against a gamma ramp that is not the
//                             identity (one to one only; fails on a difference). Fails after
//                             <seconds> or when the swap chain failed
//   cursormark                remember the presents' cursor counts (render::CursorState) and start keeping
//                             the shapes shown while the gamma ramp is the identity ("cursorcheck gamma")
//   cursorcheck <name>|- shown|hidden|held|animated|gamma [min]
//                             the system's cursor (renderer.md section 9) since "cursormark": "shown", at least
//                             <min> (default 1) presents showed it and it is shown now, not held: the game's image's
//                             frame at the window's scale (size and hot spot), with opaque and transparent pixels;
//                             with a name, the last frame with the cursor drawn in at the game's position goes to
//                             frames\<name>.bmp. "hidden": at least <min> presents hid it (a movie, a loading
//                             screen). "held": presents held it, and it is held now at the game's position.
//                             "animated": the shape stepped to another frame of the same picture <min> times.
//                             "gamma": the shape shown now is one of those kept since "cursormark" through the
//                             present's gamma ramp (same size, hot spot and alpha; each colour the ramp's entry for
//                             the kept one; some changed when the ramp is not the identity)
//   windowtitle               require the game window's title to be "Evil Islands" and its class "EIGAME"
//   render <spec>             the new renderer's options for a run of the image (src/render/render.h;
//                             the format of EI_RENDER: "checkall"). Read before the game starts.
//   samecapture <a> <b> [tolerance] [fraction]
//                             require frames <a> and <b> to match: at most <fraction> (default
//                             0.001) of the pixels may have a channel differing by more than
//                             <tolerance> (default 8), as tools/compare_frames.py counts
//   diffcapture <a> <b> <least> <most> [<tolerance> [<left> <top> <right> <bottom>]]
//                             require frames <a> and <b> to differ in a share of their pixels (by more than
//                             <tolerance>, default 8) from <least> to <most>; with a rectangle (right and bottom
//                             excluded) the share of that rectangle's pixels
//   printlight <with> <without> <least> <most>
//                             where frames <with> and <without> differ by more than 8 in a channel (what
//                             <with> draws over <without>: the ground prints with "decals on" and off),
//                             the mean brightness (r + g + b) there in <with> over that in <without> must lie
//                             within <least>..<most> (R52a: a print about as dark as the ground under it)
//   skin <unit> <first> [<second>]
//                             the client's unit <unit> (a script name, or "#<id>") takes the textures a map
//                             placement names (primary <first>, secondary <second>; without it the unit's own
//                             kept), as soon as it reaches the client: a unit made by AddUnitToServer has none
//                             and draws untextured, as in the original. Client only; fails at the report when
//                             the unit never came
//   groundprints [<least blood> <most blood> <least footprints> <most footprints> [<least scorch> <most scorch>]]
//                             logs the client world's ground prints (blood, footprints and how many of them are
//                             shown, scorch marks, their life left together); with ranges, requires the counts in
//                             them (a load gives back the save's, R57a)
//   unitskins <unit> <first> <second>
//                             the client's unit <unit> must have been given the texture names <first> and
//                             <second> (what a map placement or a party record names; party_join_skin)
//   unitcolour <frame> <a> <b> <least> <most>
//                             while frozen: the mean colours of the middle half of units <a>'s and <b>'s
//                             rectangles (this frame's screen list) in frames\<frame>.bmp; their largest channel
//                             difference must lie within <least>..<most>
//   terrainlevels on|point|off
//                             the image's ground pages draw the texture level their sampler reads
//                             (render::TerrainLevels: a grey per level, marked green / blue), through
//                             the shipped sampler (on) or the old point-mip one (point), or their
//                             colours (off)
//   levelsteps <frame> <least> <most>
//                             require the fraction of neighbouring level-map pixel pairs of
//                             frames\<frame>.bmp whose level jumps (capture::LevelSteps: the seams of
//                             R12) to lie within <least>..<most>
//   hues <frame> <least> <most> [red|green|blue|yellow]
//                             require each of red, green, blue and yellow (capture::HueShares: the quadrants of
//                             tools/make_loose_bmp.py's loose terrain textures, A-I23a), or only the one named (the
//                             example mod's tinted ground), to cover a fraction of frames\<frame>.bmp within
//                             <least>..<most>
//   mmplevels <most>          require the levels of every .mmp texture the image holds, decoded as drawn,
//                             to lie within a mean difference of <most> (0..255 per channel) from their
//                             level 0, both box-filtered to 4 x 4 (render::CheckMmpLevels: levels read
//                             from the wrong bytes drew bright green strips over the trees, R12b)
//   msaa <samples>            the image's render quality (R12a, R33; off in a run unless set): the world
//   anisotropy <level>        view multisampled with <samples> (1 off, 2, 4, 8: render::SetMsaa), its
//                             models sampled anisotropically at <level> (1 off .. 16:
//                             render::SetAnisotropy), from the next frame
//   smoothcheck <plain> <smooth> <least> <most>
//                             require the last frame drawn at the render quality's levels: with msaa
//                             above 1 its world multisampled (as many samples as asked, at most the
//                             device's, at least 2) and resolved, else not; with anisotropy above 1
//                             models in it sampled anisotropically, else none; and frames\<smooth>.bmp
//                             to differ from frames\<plain>.bmp (the same frozen moment with both off)
//                             in a fraction of its pixels (by more than 8) within <least>..<most>: the
//                             world's edges and textures, nothing black or stale
//   smoothedges <plain> <smooth> <tolerance> <most> [<left> <top> <right> <bottom>]
//                             require at most a fraction <most> of frames\<smooth>.bmp's pixels (the
//                             world multisampled; in the rectangle, right and bottom excluded, or the
//                             whole frame) to lie outside the range of their 3 x 3 neighbourhood
//                             in frames\<plain>.bmp (the same frozen moment without) by more than
//                             <tolerance> in a channel: an edge pixel mixes what its neighbours show,
//                             so one outside was shaded wrongly where a triangle covers part of it
//                             (R17c1: the ground at the shore drawn as water along its triangles' edges)
//   smoothorder game|before|split|check
//                             the image's multisampled passes in an order the game never draws
//                             (render::SetSmoothOrder, R12a1), from the next frame: an empty pass on
//                             the frame target before the world (before), or the world split in two at
//                             its middle command with an empty pass between (split): the world's later
//                             run starts from the copy of the frame target into every sample. check
//                             requires the last frame drawn so: msaa above 1, multisampled, and copies
//                             into the multisampled target (none for game; split also two depth copies
//                             back)
//   shadowcheck <casters>     require the last frame's shadow map made with at least <casters> objects
//                             casting into it, the ground and some meshes sampling it, and every object
//                             whose shadow the game draws and that was drawn casting into it, and every
//                             ground print sampling it (render::ShadowState)
//   cloudcheck <least> <most> [<least cover> <most cover>]
//                             require the last frame's cloud shadows to hide between <least> and <most> of
//                             the sun under a full cloud (0 0: none) and cover that share of the ground
//                             (render::ShadowState, R17d)
//   weathercheck <least> <most> [<least snow> <most snow>]
//                             require the last frame's ground to be between <least> and <most> wet and its
//                             snow cover in that range (render::ShadowState, R17c)
//   unitweather <unit> <least wet> <most wet> [<least snow> <most snow> [<least shelter> <most shelter>]]
//                             require the unit ("leader", #id or a name) drawn lately with its own wetness, snow and share
//                             under a roof in those ranges (render::UnitWeatherState, R17c3)
//   losedevice <point>        the renderer's Direct3D 12 device removed for real (as a hang or a driver's reset
//                             removes it) just before its next call at <point>: commands (a new command buffer),
//                             pass, uniforms, acquire (the swap chain's texture), submit, write or wait (a
//                             download's); gpu::LoseDeviceAt. With "expectcrash" (render_device_lost_*)
//   shadowview on|off         with the shadow map, the receivers show only the sun the map and the clouds hide
//                             (white lit, black fully shadowed; render::SetShadowView, R25), from the next frame
//   decals on|off             the ground prints (blood, footprints, scorch marks) drawn (the default) or left
//                             out (render::SetGroundDecals, R52), from the next frame
//   weatherparticles on|off   the rain and snow particles drawn (the default) or left out
//                             (render::SetWeatherParticles, R53), from the next frame: a frozen capture
//                             with and without, and diffcapture, give the share of the frame they cover
//   panelweather on|off       the HUD unit panel's figure with its unit's weather (the default) or dry
//                             (render::SetInterfaceUnitWeather, R53a), from the next frame: a frozen
//                             capture both ways, and diffcapture on the panel's rectangle, show the weather
//   roofs on|off|check <n>    the ground and the world's meshes under a roof kept dry (the default) or weathered
//                             as in the open (render::SetRoofShelter, R17c5), from the next frame; check requires
//                             the last prepared frame's roof map made with at least <n> roof meshes
//   shadowshift <texels>      with the shadow map, its origin moved by <texels> whole texels across and half
//                             as many along (render::SetShadowShift, R25; 0 the default): the same shadow,
//                             other range cells
//   groundreach on|off|<least>
//                             on: the renderer measures each frame's ground through the camera's clip planes
//                             (render::GroundReachState); <least>: the last presented frame's ground must reach at
//                             least <least> world units from the camera and no run of it may be cut by the view's
//                             far side (the hard terrain edge of R62)
//   occlusion on|off|view|check
//                             the image's ambient occlusion (render::SetOcclusion, R17b) on (the default), off,
//                             or shown alone by its receivers (white none, black all), from the next frame;
//                             check requires the last frame's made (the world's ground and models in its
//                             depth, the ground and some meshes taking it) and the ground's own worked out
//                             for some sectors
//   terraincache on|off       the image's zone terrain kept on the GPU (render::SetTerrainCache, R37e4b;
//                             the default) or every sector built and written again at each terrain pass
//                             (off), from the next pass
//   meshcache on|off|check    the image's figure meshes kept on the GPU (render::SetMeshCache, R16d; the
//                             default) or built and uploaded every frame (off), from the next mesh
//                             draw; check requires that no check of a kept mesh against a fresh build
//                             (every 16th frame, render::MeshCacheState) found it stale
//   renderstats <label>|-     log (and add to the report) what the image's renderer did per frame since
//                             the last renderstats (render::TakeFrameStats, R16: hand-over, GPU uploads,
//                             draws and state changes, images made, CPU time, morphed meshes; R16e: the
//                             ground decals' projections, flat draws and object shadow builds) and the
//                             main thread's CPU and wall time per frame; "-" only starts a new count
//   checkplayers              require the base screen on top, of a network client with a live binding, to
//                             list every player the client holds (its chat window's faces; UB1: the order
//                             of the players and the binding in an update follows the server's heap)
//   checkshot [recorder] <n> <frame> [tolerance] [fraction]
//                             require shot<n>.bmp (the game's Scroll Lock screenshot, or with
//                             "recorder" the frame recorder's capture\shot<n>.bmp; in the saves
//                             overlay) to exist and match frames\<frame>.bmp as
//                             samecapture does (16 bits lose the low bits: use tolerance 8 or more)
//   checksave <slot> <frame> [centre] [tolerance] [fraction]
//                             require the save <slot>'s pictures (shot.sav: the 256x192 5:6:5 one and
//                             the full-colour one after it, src/game/saves/save_picture.h) to match
//                             frames\<frame>.bmp, each as averages of 32x24 matching rectangles (the
//                             save's stretches sample differently), counted as samecapture does;
//                             catches a picture of nothing (a cleared frame, R13). "centre": against
//                             the frame's centred 4:3 part (the 800x600 layout's rectangle under
//                             Rebirth's WideScreen, which the screen copy takes)
//   oldshot <slot>            the save <slot>'s shot.sav cut to its 5:6:5 picture, as the original
//                             game writes it (its pictures then come from that part)
//   record on|off             the game's frame recorder (0079BC54: a "shotNNNN.bmp" of each presented
//                             frame, nothing in the shipped game sets it)
//   digest <N>|off, digest    a record of the game state (world, generators, players, every world
//                             object: harness_digest.h) every <N> world updates from now into
//                             digest.txt, none (off), or one now (no argument); EI_DIGEST=<N> takes
//                             them for the whole run. tools/golden.py compares two runs' records and
//                             names the first field that differs
//   digest water              (a line for the whole run) the records and dumps hold the water layer too:
//                             hashes of each cached sector's water vertices as the waves move them (R16c3)
//   dump <name>               the whole game state now into dump_<name>.txt (harness_digest.h)
//   recordinput [live] [cut <frames>]
//                             (a line for the whole run) write the run's input into recorded.txt in the run folder
//                             as a scenario that replays it (harness_record.h); "live": the player's own keyboard
//                             and mouse in a window shown and activated, the frames paced to real time
//                             (tools/recorded.py, harness.cmd record); "cut": a part is cut off once it is this many
//                             frames long (recorded_2.txt, ...: each part begins with a load of the quick save)
//   recordcut                 (a recording's step) cut the recording here, as "cut" does
//   origin                    (a recorded part's step) the "at" lines count their frames from here
//   recorded                  (a line for the whole run) a recorded scenario: its game may quit itself after the
//                             last step (the player's Exit), which passes
//   at <frame> <step>         run the step in the pass after frame <frame> of the main loop (a recorded
//                             scenario's lines; fails when the run is already past it); "at <frame> event ..." gives
//                             the game one input event, motion or mouse position (harness_record.h)
//   realinput key <name>|text <text>|move <x> <y>|click <x> <y>|rclick <x> <y>
//                             input made as SDL's own events and translated as real play's
//                             (platform/sdl/sdl_input.h): a key pressed and released (SDL's key name: A, Return,
//                             Escape, Left), typed text, the mouse moved to (x, y) of the 800x600 layout and
//                             clicked there; given to the game at the end of the pass, as a player's input is
//   # comment
//
// Everything the console prints is captured into the report. The report ends with "RESULT: PASS"
// (no failed checks or rejected output) or "RESULT: FAIL"; run_all reads it.
//
// Harness runs are hands-off and leave the install alone: no startup movies, the tutorial option
// off, saves and config\tutorial.ini kept in harness_saves in the run folder, every other write under
// the game's root (the composed-skin cache textures\*.mmp, config\keyboard.ini, ...)
// going to a copy-on-write overlay there too (any other write of the game's into an install is refused and fails
// the run: MapPath below), and a UDP port of its own for the multiplayer sessions instead of 8888, every socket on
// 127.0.0.1 only (harness_net.h). The harness answers the game's platform calls through its own backend
// (platform/: docs/notes/harness.md) and asks the system for its own needs through system/harness_system.h.
// A watchdog fails and ends a run that stops making progress (e.g. a message box), and run_all
// ends a game that hangs after "quit".
#include "harness_camera.h"
#include "harness_capture.h"
#include "harness_address_order.h"
#include "harness_clock.h"
#include "harness_coverage.h"
#include "harness_crash.h"
#include "harness_digest.h"
#include "harness_digest_read.h"   // the class ids
#include "harness_folder_profile.h"
#include "harness_modtrace.h"

#include "harness_savetrace.h"

#include "harness_watch.h"

#include "harness_settings.h"
#include "harness_starter.h"
#include "harness_windows.h"
#include "harness_input.h"
#include "platform/harness_platform.h"
#include "harness_net.h"
#include "harness_pair.h"
#include "harness_record.h"
#include "harness_timing.h"
#include "harness_utf8.h"

#include "platform/log.h"
#include "platform/path_layers.h"
#include "platform/seams.h"
#include "platform/platform.h"
#include "game/app/harness_hooks.h"
#include "game/net/net_second_master.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "harness_objects.h"
#include "image/image_side.h"
#include "system/harness_system.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ei::verify {

namespace {

// The harness's fixed path buffers.
constexpr int kPath = sys::kMaxPath;


// The console's command executor (console::Execute).
void ConsoleExecute(const char* text) { image::ConsoleExecute(text); }

// GameRequestGameState: the client asks the server for interface screen `id` (the server answers with message 7 ->
// Interface3D::CreateScreen: 1 battle, 3 briefing, 4 global map, 5 camp).
void RequestScreen(int id) { image::RequestGameState(id); }
// GameSetConstructionVariable: sets player 0's "constr_current" (the constructor the camp opens with).
void SetCurrentConstructor(float constructor) { image::SetConstructionVariable(constructor); }

// 0066F310 (RET 8): PlayerServer::LeaveToZone(zone, exit), what a zone exit (00670C20) and the
// LeaveToZone script command (0x8A) do. Only in state 1 (+0x20: in the world); in campaign mode 1 or 3
// a dead or missing leader loses the game instead. The party is stored, the world player's +0xD8 /
// +0xDC become the zone and the exit, the state 3, and message 8 goes to the client (00669EB0: game
// request 4, carried out by the main loop through 0057C460). `exit` indexes the target zone's exits
// in map.txt order from 0 (its "#exit n" minus 1, as the map database stores the exits' targets);
// Redeploy (00590700) puts the party at the centre of that exit's deploy rectangle, or at (0, 0)
// when the zone has no such exit.

// The single-player hero's player number in the server world: world players' +0x24 is 0 for the
// local player, and the world script commands that act on "the player" (GSSetVar(0, ...),
// LeaveToZone(0, ...); script_exec.cpp case 0x8A: TheWorld()->GetPlayer(int(f[0])) and its +0x20)
// use 0.
constexpr int kLocalPlayer = 0;

// The local world player (WorldServer::GetPlayer) or null.
void* LocalWorldPlayer() { return image::LocalWorldPlayer(kLocalPlayer); }
// Its server connection (the world player's connection, a PlayerServer in single player; StateOf in
// script_exec.cpp) or null, and the connection's state (1 in the world, 3 leaving the zone).
void* LocalConnection() {
    void* player = LocalWorldPlayer();
    if (!player) return nullptr;
    return image::PlayerConnection(player);
}
int ConnectionState(void* connection) { return image::ConnectionState(connection); }
// What the zone steps read, for the log and failure messages.
std::string DescribeLocalPlayer() {
    void* player = LocalWorldPlayer();
    void* connection = LocalConnection();
    char text[200];
    // The server world's +0x5C (worldHandle), which the flow passes to the client world's SetPlayer (00566CE0). It is
    // not the server's player number: the first X9 run found no player with it (logged by the steps).
    std::snprintf(text, sizeof(text), "player %d -> %p, connection %p, state %d (world +0x5C = %p)", kLocalPlayer, player,
                  connection, connection ? ConnectionState(connection) : -1, image::WorldHandle());
    return text;
}
// The server world's zone (+0x120, the map database name NewZone 00595480 stores), or "".
const char* ServerZoneName() {
    const char* name = image::ServerZoneName();

    return name ? name : "";
}

struct Step {
    std::string verb;
    std::string argument;
};

std::vector<Step> g_steps;
std::size_t g_next = 0;
// The scenario's lines read before the game starts (a recording's header, harness_record.h); "recorded": a recorded
// scenario (its game may quit itself after the last step); an "at" step came after its frame (said once).
std::vector<std::string> g_loadLines;
bool g_recordedScenario = false;
bool g_replayLate = false;
// A recording's cut is queued (InjectCut) until its origin step.
bool g_cutQueued = false;

// EI_TIME_SCALE (default 1, never below 1): the factor on the harness's real-time limits (a frames step's, the whole
// scenario's) for a runner slower than a desktop (CI's software rendering, docs/notes/linux.md). The world's clock and
// the waits' own lengths are not scaled; run_all.py reads the same variable for the limit it reports.
double TimeScale() {
    static const double scale = [] {
        const double value = std::atof(platform::EnvironmentVariable("EI_TIME_SCALE").c_str());
        return value >= 1.0 ? value : 1.0;
    }();
    return scale;
}

// When a wait ends: after `ms` on the harness's clock (virtual time in deterministic mode), or at a
// real-time safety limit, so that a game which stops drawing frames (the virtual clock stands
// still) still fails the wait instead of the whole run's time limit: four times the wait plus 10 s, times
// TimeScale() (a busy machine draws slowly, and the virtual clock is then slower than real time).
struct Deadline {
    std::uint32_t start = 0, at = 0;           // harness clock
    std::uint32_t realStart = 0, realAt = 0;   // the system's tick count
    void Set(std::uint32_t ms) {
        start = clock::Now();
        at = start + ms;
        realStart = sys::TickCount();
        realAt = realStart + (clock::Virtual() ? static_cast<std::uint32_t>(TimeScale() * (4.0 * ms + 10000)) : ms);
    }
    bool Passed() const {
        return static_cast<std::int32_t>(clock::Now() - at) >= 0 || static_cast<std::int32_t>(sys::TickCount() - realAt) >= 0;
    }
    // For a failure message: "" when the wait's own time passed, else what the clock did meanwhile.
    std::string Why() const {
        if (static_cast<std::int32_t>(clock::Now() - at) >= 0) return "";
        char text[120];
        std::snprintf(text, sizeof(text), " (real-time limit: the game's clock moved %u ms in %u ms)",
                      clock::Now() - start, sys::TickCount() - realStart);
        return text;
    }
};
Deadline g_waitUntil;   // "wait"
std::atomic<bool> g_quitting{false};
std::atomic<bool> g_watchdogStopped{false};   // "hang all": the in-game watchdog does nothing
std::atomic<bool> g_stackDumpHangs{false};    // "hang dump": the thread that logs the main thread's stack never returns
bool g_running = false;
bool g_inStep = false;
char g_reportPath[kPath];
// Whether the PeekMessageA call running the steps is one that hands input to the game (see
// BeforeEvents): "key", mouse and region steps wait for one.
bool g_inputLoop = false;
// Whether that call is WinMain's own loop: "quit" waits for one.
bool g_winMainLoop = false;

std::vector<std::string> g_console;          // every console line so far
std::size_t g_logMark = 0;                   // the log text's length at the last logmark / expectnewlog
// The log without the harness's own echoes of its steps (the log is open for writing).
std::string LogText() {
    std::string text;
    if (FILE* log = sys::OpenShared(ei::OutputPath("ei-rebuild.log"))) {
        char line[4096];
        while (std::fgets(line, sizeof(line), log))
            if (!std::strstr(line, "harness: ")) text += line;
        std::fclose(log);
    }
    return text;
}
std::size_t g_expectFrom = 0;                // first line "expect" looks at
std::vector<std::string> g_rejected;         // texts that must not appear
std::vector<std::string> g_allowed;          // lines containing one of these are exempt from g_rejected
std::vector<std::string> g_failures;
// A pending "check": the console line index its output starts at, and when to give up.
bool g_checking = false;
std::string g_checkExpr;
std::size_t g_checkFrom = 0;
Deadline g_checkDeadline;
// A pending "waitfor": the expression, when it was last evaluated (its output starts at
// g_waitForFrom), when to evaluate it next, and when to give up.
// A pending "waitline": the text to find anywhere in the console output so far, and when to give up.
bool g_waitingLine = false;
std::string g_waitLineText;
Deadline g_waitLineDeadline;
std::size_t g_waitLineFrom = 0;   // waitline: 0 (any time); waitexpect: since the last expect
bool g_waitLineIsExpect = false;
// A pending "waitread" (harness_modtrace.h): the folder and the file, and when to give up, in real seconds (a thread
// opens the file, and the virtual clock runs on much faster than it).
bool g_waitingRead = false;
bool g_waitingModSwitch = false;   // a "mod" step's switch asked for, not yet made
std::string g_waitReadWhere, g_waitReadPath;
std::uint32_t g_waitReadAt = 0;   // the system's tick count
// A pending "waitzone": the zone and when to give up.
bool g_waitingZone = false;
std::string g_waitZoneName;
Deadline g_waitZoneDeadline;
bool g_waitingFor = false;
bool g_waitForPending = false;
std::string g_waitForExpr;
std::size_t g_waitForFrom = 0;
std::uint32_t g_waitForNext = 0;
Deadline g_waitForDeadline;
// Frames drawn so far: passes of WinMain's loop through its after-frame PeekMessageA (see
// BeforeEvents). A pending "frames": the count it waits for.
unsigned g_frames = 0;
bool g_waitingFrames = false;
unsigned g_waitFramesUntil = 0;
Deadline g_waitFramesDeadline;   // real time only: 10 s plus 100 ms a frame, times TimeScale()
// A pending "waitpresent" (R9c): the game window's presents when it started, its deadline, whether it
// waits for a gamma ramp check too, and the ramp checks a previous one has seen.
bool g_waitingPresent = false;
bool g_waitPresentGamma = false;
std::uint32_t g_waitPresentFrom = 0;
Deadline g_waitPresentDeadline;
int g_rampChecksSeen = 0;
// A pending "waitwindow": the window's name, when it started and gives up, and the frame from which it has been
// ready (kNotReady: not ready now).
constexpr unsigned kNotReady = ~0u;
bool g_waitingWindow = false;
std::string g_waitWindowName;
Deadline g_waitWindowDeadline;
unsigned g_waitWindowReadyFrom = kNotReady;
// A pending "waitdialogue" / "talk" (H53): the state awaited, whether it clicks through, its clicks, the frame the
// next click may come, when it gives up.
bool g_waitingDialogue = false;
bool g_talking = false;
int g_dialogueState = 0;
int g_talkClicks = 0;
unsigned g_talkNextFrame = 0;
Deadline g_dialogueDeadline;
// The "skin" steps whose unit has not reached the client yet (SkinStep).
struct PendingSkin {
    int id;
    std::string unit, first, second;   // second "": the unit's own second name kept
};
std::vector<PendingSkin> g_pendingSkins;
// A pending "waitunit": the unit's script name, when it started and gives up.
bool g_waitingUnit = false;
std::string g_waitUnitName;
Deadline g_waitUnitDeadline;
// A pending "waitidle" / "walkto" / "waitdead" / "waitaward" (SV5d5): which, its deadline; waitidle's frames wanted
// and the frame from which the leader has stood idle; walkto's point, its clicks so far, the minimap clicks of this
// aim and the frame the next may come (H53); waitdead's unit ids; waitaward's experience and money at its start.
enum class WorldWait { None, Idle, Walk, Dead, Award };
WorldWait g_worldWait = WorldWait::None;
Deadline g_worldWaitDeadline;
unsigned g_idleFrames = 0;
unsigned g_idleFrom = kNotReady;
float g_walkX = 0, g_walkY = 0;
int g_walkClicks = 0, g_walkLooks = 0;
bool g_walking = false;   // the ground was clicked; false: aiming the camera
unsigned g_walkNextFrame = 0;
constexpr float kWalkReach = 3.0f;
constexpr int kWalkMostClicks = 8;
std::vector<int> g_deadIds;
float g_awardExperience = 0;
unsigned g_awardMoney = 0;
bool g_awardServed = false;      // "waitaward ... served": the server's award queue filled and emptied
bool g_awardQueueSeen = false;   // the server's award queue was seen with awards
bool g_awardFromSet = false;   // "awardfrom" took them (the next waitaward compares with those)
// What each "heroxp" step read, by hero.
std::map<std::string, float> g_heroExperience;
// A "hit" step's unit: "leader", "#<id>" or a script name; "none" (0) where `none` allows it; -1 when not found.
int HitUnitId(const std::string& name, bool none) {
    if (none && name == "none") return 0;
    if (name == "leader") return digest::LocalLeader();
    if (name == "companion") return image::CompanionUnit(0);
    if (!name.empty() && name[0] == '#') return std::atoi(name.c_str() + 1);
    const int id = image::TextKey(name.c_str());
    return id ? id : -1;
}
void AwardFrom() {
    const digest::UnitNow unit = digest::UnitNowOf(digest::LocalLeader(&g_awardMoney));
    g_awardExperience = unit.experience;
    g_awardQueueSeen = false;
}
// A pending "peerwait": the other game's signal and when to give up (harness_pair.h).
bool g_waitingPeer = false;
std::string g_waitPeerTag;
Deadline g_waitPeerDeadline;
// A pending "capture": its name and when to give up (real time); the frames written so far.
bool g_capturing = false;
std::string g_captureName;
std::uint32_t g_captureDeadline = 0;
std::vector<std::string> g_capturedFrames;
// What starts a burst (capture <name> <count> after <text> | on <window>): the console text, and whether it came; the
// window.
std::string g_captureAfter;
bool g_captureAfterSeen = false;
std::string g_captureOn;
bool CaptureStarts() {
    if (!g_captureOn.empty()) return WindowKind(TopWindow()) == g_captureOn;
    return g_captureAfterSeen;
}

void OnConsoleLine(std::string_view text) {
    if (!text.empty() && text.front() != '\0') {
        std::string line(text.substr(0, text.find('\0')));
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        Log("console: %s", line.c_str());
        if (!g_captureAfter.empty() && line.find(g_captureAfter) != std::string::npos) g_captureAfterSeen = true;
        bool allowed = false;
        for (const std::string& a : g_allowed) allowed = allowed || line.find(a) != std::string::npos;
        for (const std::string& r : g_rejected)
            if (!allowed && line.find(r) != std::string::npos) g_failures.push_back("rejected output: " + line);
        g_console.push_back(std::move(line));
    }
}


// Crash diagnostics: logs access violations and other fatal faults with the faulting address and the code addresses
// found on the stack (ours as ei-rebuild+offset, the graphics driver's as module+offset). First-chance where the
// system has it (Windows), so faults the game handles itself show up too; only the first few are logged.
sys::Image g_self;

void LogCodeAddress(const char* label, std::uintptr_t a) {
    if (a >= g_self.base && a < g_self.end) {
        Log("  %s ei-rebuild+%X", label, unsigned(a - g_self.base));
        return;
    }
    // Direct3D's, DXGI's and OpenGL's frames too (crashes inside their threads and the driver's).
    std::uintptr_t base = 0;
    const std::string module = sys::ModuleOf(a, &base);
    if (module.find("d3d") != std::string::npos || module.find("dxgi") != std::string::npos ||
        module.find("GL") != std::string::npos)
        Log("  %s %s+%X", label, module.c_str(), unsigned(a - base));
}

void LogModuleOf(std::uintptr_t a) {
    std::uintptr_t base = 0;
    std::string module = sys::ModuleOf(a, &base);
    if (module.empty()) module = "?";
    Log("  in %s+%X (thread %llu)", module.c_str(), unsigned(a - base), static_cast<unsigned long long>(sys::CurrentThread()));
}

// The code addresses among the first `words` words of the stack from `sp`. It reads raw stack words, the sanitizer's
// redzones among them, so the address sanitizer leaves it out.
#if defined(_MSC_VER) && !defined(__clang__)
__declspec(no_sanitize_address)
#else
__attribute__((no_sanitize_address))
#endif
void LogStack(std::uintptr_t sp, int words) {
    const std::uintptr_t end = sys::StackEnd(sp);
    const auto* stack = reinterpret_cast<const std::uintptr_t*>(sp);
    for (int i = 0; i < words && sp + (i + 1) * sizeof(std::uintptr_t) <= end; ++i) LogCodeAddress("stack", stack[i]);
}

void FirstChance(const sys::Fault& fault) {
    static std::atomic<int> logged{0};
    if (logged++ >= 5) return;
    Log("harness: %s at %p (address %p); %s", fault.what.c_str(), reinterpret_cast<void*>(fault.pc),
        reinterpret_cast<void*>(fault.address), fault.registers.c_str());
    LogModuleOf(fault.pc);
    LogCodeAddress("at", fault.pc);
    LogStack(fault.sp, 256);
}

// Harness runs stay out of the user's way: the game window never takes focus or comes to the front, and it
// plays in a small window (harness_starter.h). The game's own code still runs, so scenarios test the same paths.

// The game reads the mouse's position (edge scrolling among others). The user's cursor can be anywhere, e.g. on a
// second screen to the right, which the game took as panning right and which moved the camera under the scenario's
// clicks. The game sees the harness's own mouse instead (harness_platform.h, MousePosition): the last position a
// move/click step posted (client coordinates), the window's centre until then.
std::atomic<int> g_mouseX{-1}, g_mouseY{-1};
// The "mousedrag" step's motion a frame (DirectInput counts; 0, 0: none).
int g_dragX = 0, g_dragY = 0;
void* GameWindow();

// Saves. The game keeps them under <game root>saves\ and rewrites saves\current\ whenever it
// starts or loads a game, so a harness run would overwrite the player's own progress. In harness
// runs the game folder's saves\ is harness_saves\ in the run folder instead (with a mod, harness_saves\<mod>\: the
// run's path map, MapPath below).

char g_savesDir[kPath];
// Whether the harness answers the game's file and desktop calls (its platform backend's game calls,
// platform/game_answers.cpp): set once the overlay's folders are known at the harness's start; a game started without
// the harness gets the system's answers.
bool g_answering = false;

// config\tutorial.ini (which tutorial windows were shown) is rewritten on every exit; harness runs keep theirs in
// harness_saves\tutorial.ini.
bool TutorialFile(const std::string& path) {
    return path.size() >= 20 && text::CompareIgnoringCase(path.c_str() + path.size() - 20, "\\config\\tutorial.ini") == 0;
}

// The rest of the install (textures\ with the composed-skin cache, config\keyboard.ini,
// config\addrbook.ini, mp\, ...) gets a copy-on-write overlay in harness_saves\: a write to a file
// under the game's root goes to harness_saves\<relative path> (a file the write keeps the contents
// of is copied there first), and a read takes that copy when there is one, else the install's file.
// Nothing under the install is ever modified. Deleting or moving an install file only affects the
// overlay copy (a move of an install file becomes a copy). The overlay is the top layer of the run's path map
// (src/platform/path_layers.h; Layers below).
char g_rootDirs[2][kPath];   // the game folder (starter::GameFolder) and the settings' one

bool UnderRoot(const char* full, const char* root, const char** relative) {
    std::size_t n = std::strlen(root);
    if (n == 0 || text::CompareIgnoringCase(full, root, n) != 0) return false;
    if (root[n - 1] != '\\') {
        if (full[n] != '\\') return false;
        ++n;
    }
    if (full[n] == '\0') return false;
    *relative = full + n;
    return true;
}

void LogInstallWrite(const char* path, const char* shadow) {
    static int logged = 0;
    if (logged++ < 200) Log("harness: install write redirected: %s -> %s", path, shadow);
}

// "hidefile <path>": install files (relative to the game folder, e.g. maps\z6q1.mq) this game reads
// as missing until it writes them itself (the overlay copy is then read): a player who lacks a file
// another has (SV5d2: the multiplayer quest files the server shares). Set by steps on the main
// thread before the game looks for the files; read by the file hooks.
constexpr int kHiddenFiles = 8;
char g_hiddenFiles[kHiddenFiles][kPath];
std::atomic<int> g_hiddenCount{0};

// A hidden file's folder of the same name (without its extension) is hidden with it, and everything in it: the folder
// that stands for an archive (docs/notes/mods.md, "Loose files").
bool Hidden(const char* relative) {
    const std::string_view path = relative;
    for (int i = 0; i < g_hiddenCount; ++i) {
        const std::string_view hidden = g_hiddenFiles[i];
        if (text::CompareIgnoringCase(path, hidden) == 0) return true;
        const std::size_t dot = hidden.find_last_of('.');
        if (dot == std::string_view::npos || dot == 0) continue;
        const std::string_view folder = hidden.substr(0, dot);
        if (path.size() >= folder.size() && text::CompareIgnoringCase(path.substr(0, folder.size()), folder) == 0 &&
            (path.size() == folder.size() || path[folder.size()] == '\\'))
            return true;
    }
    return false;
}

// "samefile <path>": the game wrote its own copy of the install file <path> (relative to the game folder; in
// the overlay), byte for byte the install's (A-I23: the shared quest file a hidefile peer receives).
std::string SameFileStep(const std::string& relative) {
    auto read = [](const std::string& path, std::string& out) {
        FILE* f = sys::OpenFile(path, "rb");
        if (!f) return false;
        char block[4096];
        for (std::size_t n; (n = std::fread(block, 1, sizeof(block), f)) > 0;) out.append(block, n);
        std::fclose(f);
        return true;
    };
    const char* root = g_rootDirs[1][0] ? g_rootDirs[1] : g_rootDirs[0];
    std::string install = std::string(root) + (root[std::strlen(root) - 1] == '\\' ? "" : "\\") + relative;
    std::string written, original;
    if (!read(std::string(g_savesDir) + "\\" + relative, written)) return "samefile: the game wrote no " + relative;
    if (!read(install, original)) return "samefile: cannot read " + install;
    if (written != original)
        return "samefile: " + relative + " differs from the install's (" + std::to_string(written.size()) + " / " +
               std::to_string(original.size()) + " bytes)";
    Log("harness: samefile %s: %u bytes, the install's", relative.c_str(), unsigned(written.size()));
    return {};
}

// Install folders a run sees only in its overlay, never the install's own files: mp\ (the multiplayer
// heroes, mp\<n>.mp; SV5d3: the user's own heroes had been listed and loaded on the hero screen, so goldens
// followed the user's play). A scenario's hero comes from tools/make_mp_hero.py or the game's hero screen.
// `relative` is the path below the game folder.
bool OverlayOnly(const char* relative) {
    return text::CompareIgnoringCase(relative, "mp", 2) == 0 && (relative[2] == '\\' || relative[2] == '/' || relative[2] == '\0');
}

// Whether a write that keeps its file's contents gets the install's copy first: not a log (a run's log
// starts empty in the overlay; addon.dll appends to starter\addon.log, which grows with the user's play) and
// not a file over 16 MB.
bool CopiedForWrite(const char* original) {
    const std::size_t length = std::strlen(original);
    if (length >= 4 && text::CompareIgnoringCase(original + length - 4, ".log") == 0) return false;
    const long long size = sys::FileSize(original);
    if (size <= 16ll << 20) return true;   // also when it is not there
    Log("harness: %s not copied into the overlay for a write (over 16 MB); the write starts from an empty file", original);
    return false;
}

// The run's path map (src/platform/path_layers.h) over the game folder (and the settings' data folder): the overlay
// on top (a read takes its copy when there is one; mp\ and the "hidefile" files read from it alone, missing until the
// game writes them; every write goes there, a file the write keeps copied there first unless CopiedForWrite says
// no), the user's key bindings and address book below it (the committed copies, harness_settings.h), the mod's layer
// under those (the settings' Mod, platform::SwitchMod); the saves in harness_saves\ (with a mod in harness_saves\<mod>\).
platform::PathLayers MakeLayers(const platform::Mod* mod) {
    platform::PathLayers map;
    for (const char* root : g_rootDirs)
        if (*root) map.bases.push_back(platform::FullGamePath(root));
    platform::PathLayer overlay;
    overlay.folder = platform::FullGamePath(g_savesDir);
    overlay.owns = [](std::string_view relative) {
        const std::string name(relative);
        if (OverlayOnly(name.c_str())) return true;
        if (!g_hiddenCount || !Hidden(name.c_str())) return false;
        static std::atomic<int> logged = 0;
        if (logged++ < 20) Log("harness: hidden file read as missing: %s", name.c_str());
        return true;
    };
    platform::PathLayer pinned;
    pinned.place = [](std::string_view relative) {
        char file[kPath];
        return settings::PinnedFile(std::string(relative).c_str(), file) ? std::string(file) : std::string();
    };
    map.layers = {overlay, pinned};
    if (mod) map.layers.push_back(mod->layer);
    map.writesToTop = true;
    map.copiesUp = [](std::string_view relative, const std::string& from) {
        return !OverlayOnly(std::string(relative).c_str()) && CopiedForWrite(from.c_str());
    };
    map.saves = overlay.folder + (mod ? "\\" + mod->name : std::string());
    return map;
}

// The map for the run's state: the settings' data folder known (it may differ from the game folder) and the mod.
// Each is made once and kept for the run (the game's threads hold them); a switch of mods asks for another.
const platform::PathLayers& Layers() {
    const char* folder = image::DataFolder();
    if (folder && *folder && !g_rootDirs[1][0]) sys::CopyText(g_rootDirs[1], sys::FullPath(folder).c_str());
    const platform::Mod* mod = platform::CurrentMod();
    const bool second = g_rootDirs[1][0] != 0;
    // The last map asked for, else the one made before for the same folders and mod (a switch of mods makes a new Mod).
    struct Made {
        bool second;
        const platform::Mod* mod;
        platform::PathLayers map;
    };
    static std::atomic<const Made*> last = nullptr;
    static std::mutex lock;
    static std::vector<const Made*> made;
    const Made* found = last.load(std::memory_order_acquire);
    if (!found || found->second != second || found->mod != mod) {
        std::lock_guard<std::mutex> hold(lock);
        found = nullptr;
        for (const Made* each : made)
            if (each->second == second && each->mod == mod) found = each;
        if (!found) {
            found = new Made{second, mod, MakeLayers(mod)};
            made.push_back(found);
        }
        last.store(found, std::memory_order_release);
    }
    return found->map;
}
// No run writes into an install (WB17). The game's writes of install files go to the overlay (MapPath), so what is
// left under an install root (the game folder; EI_INSTALL_ROOTS: run_all's installs, the other one among them) is
// refused (MapPath gives an empty path, so the call fails as on a missing file) and fails the run: "install: write
// refused: <path>". The harness answers the game's own file calls only (they all take their paths from MapPath); what
// a library writes by itself is not seen here.
std::vector<std::string> g_installRoots;   // in full, no backslash at the end
std::mutex g_refusedLock;
std::vector<std::string> g_refused;

void FindInstallRoots() {
    std::vector<std::string> named = {starter::GameFolder()};
    const std::string listed = platform::EnvironmentVariable("EI_INSTALL_ROOTS");
#if defined(_WIN32)
    constexpr char kSeparator = ';';   // os.pathsep
#else
    constexpr char kSeparator = ':';
#endif
    for (std::size_t at = 0; at <= listed.size();) {
        const std::size_t end = std::min(listed.find(kSeparator, at), listed.size());
        if (end > at) named.push_back(listed.substr(at, end - at));
        at = end + 1;
    }
    std::string list;
    for (const std::string& name : named) {
        std::string full = sys::FullPath(name);
        while (full.size() > 3 && full.back() == '\\') full.pop_back();
        if (full.empty() || std::find(g_installRoots.begin(), g_installRoots.end(), full) != g_installRoots.end()) continue;
        g_installRoots.push_back(full);
        list += (list.empty() ? "" : "; ") + full;
    }
    Log("harness: install roots no run writes in: %s", list.c_str());
}

bool InInstall(const std::string& path) {
    const std::string full = sys::FullPath(path);
    for (const std::string& root : g_installRoots) {
        const char* relative = nullptr;
        if (full.size() > root.size() && UnderRoot(full.c_str(), root.c_str(), &relative)) return true;
    }
    return false;
}

void RefuseInstallWrite(const std::string& path, const std::string& mapped) {
    std::lock_guard<std::mutex> lock(g_refusedLock);
    if (std::find(g_refused.begin(), g_refused.end(), mapped) != g_refused.end()) return;
    g_refused.push_back(mapped);
    Log("harness: install: write refused: %s%s%s", path.c_str(), path == mapped ? "" : " -> ", path == mapped ? "" : mapped.c_str());
}

// The read trace (EI_READTRACE=1; H41's stub data, tools/stub_data.py): every install path the game reads, lists or
// asks about, once each, in harness_reads.txt beside the report ("+" there, "-" missing), so the files a suite needs
// can be listed from its runs (docs/notes/linux.md, "The stub data").
bool g_readTrace = false;
std::mutex g_readsLock;
std::vector<std::string> g_reads;

void TraceRead(const std::string& mapped) {
    if (!InInstall(mapped)) return;
    const std::string full = sys::FullPath(mapped);
    std::lock_guard<std::mutex> lock(g_readsLock);
    for (const std::string& seen : g_reads)
        if (text::CompareIgnoringCase(seen.c_str() + 2, full.c_str()) == 0) return;
    g_reads.push_back(std::string(sys::Exists(full.c_str()) ? "+ " : "- ") + full);
}

void WriteReads() {
    if (!g_readTrace) return;
    FILE* f = sys::OpenFile(ei::OutputPath("harness_reads.txt"), "w");
    if (!f) return;
    std::lock_guard<std::mutex> lock(g_readsLock);
    for (const std::string& path : g_reads) std::fprintf(f, "%s\n", path.c_str());
    std::fclose(f);
}

// The report's failure for the refused writes, "" without any.
std::string InstallFailure() {
    std::lock_guard<std::mutex> lock(g_refusedLock);
    std::string text;
    for (const std::string& path : g_refused) text += (text.empty() ? "install: write refused: " : "; ") + path;
    return text;
}

// Game speed. The main loop runs one world update per UpdatePeriod milliseconds of real time (55 by
// default; the game's own speed-up key, keypad +, sets 27). "speed <ms>" holds it at <ms> every
// frame, since loading a zone resets it, and again after every world update (HeldServerUpdate,
// HeldClientSegment): the battle clock's set-up (00648520) and BattleScreen::Close (00635E60,
// on leaving a screen such as the camp) set 55 inside the client's tick, and the main loop's client
// loop, which tested its backlog against the held 10 before the tick, subtracts the 55 it reads
// after it: the unsigned backlog (10..54) wraps to about 4.29e9 and the loop runs some 78 million
// client ticks (minutes). Real-time runs mostly got away with it by timing; on the virtual clock
// ui_camp_deals, ui_trade_hold, effects_bow, ... hung every time. 0 = leave it to the game.
unsigned& TickLength() { return image::UpdatePeriod(); }
unsigned g_heldTickLength = 0;

// The main loop's calls of the server's update and the client's tick (false stops the frame): the loop's hooks
// (app/main_loop.cpp).
void HeldServerUpdate(::WorldServer* server) {
    {
        timing::Scope scope(timing::Server);
        image::ServerUpdate(server);
    }
    if (g_heldTickLength) TickLength() = g_heldTickLength;
    {
        timing::Scope scope(timing::Digest);
        digest::AfterWorldUpdate(server);   // a state record when its period says so
    }
    watch::AfterWorldUpdate();          // EI_WATCH_CUNIT (harness_watch.h)
    watch::ScrubStack();                // EI_STACK_SCRUB (harness_watch.h)
}
bool HeldClientSegment(::WorldClient* client) {
    bool result;
    {
        timing::Scope scope(timing::Client);
        result = image::ClientSegment(client);
    }
    if (g_heldTickLength) TickLength() = g_heldTickLength;
    return result;
}

void WriteReport() {
    for (const PendingSkin& skin : g_pendingSkins)
        g_failures.push_back("skin " + skin.unit + ": the unit never reached the client");
    g_pendingSkins.clear();
    clock::FlushRandomTrace();
    digest::Flush();
    coverage::Write();
    FILE* f = sys::OpenFile(g_reportPath, "w");
    if (!f) return;
    if (!g_capturedFrames.empty()) std::fprintf(f, "\nframes captured:\n");
    for (const std::string& frame : g_capturedFrames) std::fprintf(f, "  %s\n", frame.c_str());
    if (std::string digest = digest::Summary(); !digest.empty()) std::fprintf(f, "\nstate digest: %s\n", digest.c_str());
    sys::WindowReport(f);
    netlocal::Report(f);
    pair::Report(f);
    input::Report(f);
    timing::Report(f);
    std::fprintf(f, "\nconsole output:\n");
    for (const std::string& line : g_console) std::fprintf(f, "  %s\n", line.c_str());
    std::vector<std::string> failures = g_failures;
    if (std::string write = crash::FailedWrite(); !write.empty()) failures.push_back(write);   // harness_crash.h
    if (std::string digested = digest::Failure(); !digested.empty()) failures.push_back(digested);   // harness_digest.h
    if (std::string paired = pair::Failure(); !paired.empty()) failures.push_back(paired);    // harness_pair.h
    if (std::string pinned = settings::Failure(); !pinned.empty()) failures.push_back(pinned);   // harness_settings.h
    if (std::string written = InstallFailure(); !written.empty()) failures.push_back(written);   // MapPath

    for (const std::string& failure : failures) std::fprintf(f, "FAILED: %s\n", failure.c_str());
    std::fprintf(f, "RESULT: %s\n", !failures.empty() ? "FAIL" : "PASS");
    std::fclose(f);
    WriteReads();
    Log("harness: report written (%u failures)", unsigned(failures.size()));
}

// Deterministic mode, the default (a "realtime" line anywhere in the scenario turns it off,
// "deterministic" back on; EI_HARNESS_CLOCK overrides both): the virtual clock (harness_clock.h;
// every frame of the main loop runs exactly five world updates, the main thread's sleeps and timed
// waits take no real time, sample ends come on virtual time), a fixed date and fixed random seeds,
// so two runs of the same scenario take the same course, and a run with and without hooks
// (tools/compare.py) must print the same console output. The harness's waits count virtual time
// too, each with a real-time safety limit. Repeatability: docs/notes/standalone-verification.md, section 5.
bool g_deterministic = true;
// "clocktrace": log the game's clock reads and waits (harness_clock.h).
bool g_clockTrace = false;
// "randtrace": log every random draw (harness_clock.h), installed at the first message loop pass.
bool g_randomTrace = false;

// Frozen clocks ("freeze" / "unfreeze", for frame captures). While frozen, the game's timeGetTime
// and GetTickCount return what they returned when frozen: the main loop (004571DC) sees no time
// pass, so no world, client or camera update runs, and what the game animates from these clocks
// (interface fades, the frame timer, tooltips) holds still, while every loop still draws and
// presents a frame. Unfreezing resumes both clocks from where they stopped (no jump). Installed
// only when the scenario uses "freeze" (in deterministic mode the virtual clock just stops). Such
// a scenario also gets the fixed date: the main menu's picture follows the time of day.
// The camera is held too: the main loop calls Camera::Update once per frame even when no time has
// passed (at 00457595, with dt 0), and parts of it move by a fixed fraction per call, not per
// unit of time (the focus drifts a fifth of the way to the tracked object, the focus height a
// tenth of the way to the ground, ApplyView lifts the camera over nearby objects). Measured on
// two frozen captures three frames apart: the whole scene moved about 1 pixel at 3840x2160.
// While frozen that call is skipped (HeldCameraUpdate).
// Limits: what already moves by itself between frames (other threads, other frame-counted
// effects) is not held; the harness's own waits use real time (in deterministic mode the virtual
// clock, which stops too: use "capture", not "wait", while frozen there).
bool g_freezeUsed = false;
// The scenario records the camera's motion (camtrace, harness_camera.h): the same call is taken over.
bool g_cameraTraceUsed = false;
// The scenario records an object's drawn motion (objtrace): the frame step is taken over (TracedFrameStep).
bool g_objectTraceUsed = false;

// The main loop's per-frame camera update (Camera::Update, one float), taken over when the scenario freezes or
// records the camera's motion (camtrace: sampled after the call).
void HeldCameraUpdate(::Camera* camera, float dt) {
    if (clock::Frozen()) return;
    image::CameraUpdate(camera, dt);
    camera_trace::Frame(camera, dt);
}

// A pass's frame step (CL draw, DL cursor): a pair's game ends its turn first, so that it draws while
// the other game takes its turn (harness_pair.h). Only the main loop's call; the frame steps of modal
// loops and screens stay inside the turn.
void HandOffThenFrameStep(bool draw) {
    pair::HandOff();
    image::FrameStep(draw);
}

// The main loop's frame step, then the object trace's sample of what it drew (objtrace).
void TracedFrameStep(bool draw) {
    image::FrameStep(draw);
    camera_trace::ObjectFrame();
}

// The ISAAC generator's seed: srand(timeGetTime()), then 0x400 bytes from a randomly chosen file under C:\ (!). In
// deterministic mode the results are a fixed sequence instead, and rand() gets a fixed seed; the generator then
// mixes them exactly as before. The image takes it through IsaacRandom's seed hook (image::SetFixedSeed).
void FillFixedSeed(std::uint32_t* results) {
    std::uint32_t x = 0x2001C0DE;
    for (int i = 0; i < 256; ++i) results[i] = x = x * 1664525u + 1013904223u;
}

// The clock the harness's waits and timeouts use.
std::uint32_t Now() { return clock::Now(); }

// Input for the interface screens: window messages to the game window (its window procedure 00457970 handles the
// mouse and key messages).
void* GameWindow() { return HarnessMainWindow(); }

// Modifier keys held around an input step ("hold", "release" and the <mods> words of the mouse and key
// steps). The game reads them from the key messages (Interface3D::OnKeyDown sets +0x7C / +0x7D / +0x7E
// by scan code, OnKeyUp clears them by virtual key), so these are posted like a keyboard's: Ctrl and
// Shift as WM_KEYDOWN / WM_KEYUP, Alt as the system messages Windows sends for it.
enum Modifier : unsigned { kModCtrl = 1, kModShift = 2, kModAlt = 4 };

// The modifier a word names (0: none).
unsigned ModifierWord(const std::string& word) {
    if (word == "ctrl") return kModCtrl;
    if (word == "shift") return kModShift;
    if (word == "alt") return kModAlt;
    return 0;
}

// Words of `text` separated by spaces, "+" or ","; `bad` gets the first word that is no modifier.
unsigned ParseModifiers(const std::string& text, std::string* bad) {
    unsigned mods = 0;
    std::string word;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        char c = i < text.size() ? text[i] : ' ';
        if (c == ' ' || c == '+' || c == ',' || c == '\t') {
            if (!word.empty()) {
                if (unsigned one = ModifierWord(word)) mods |= one;
                else if (bad && bad->empty()) *bad = word;
                word.clear();
            }
        } else {
            word += c;
        }
    }
    return mods;
}

using platform::MouseButton;

void PostModifiers(unsigned mods, bool down) {
    // Alt's own press and release hold Alt, as the backend's key events carry it.
    struct ModifierKey { unsigned mod; platform::Key key; unsigned scan; std::uint8_t modifiers; };
    static const ModifierKey keys[] = {{kModCtrl, platform::Key::Control, 0x1D, 0},
                                       {kModShift, platform::Key::Shift, 0x2A, 0},
                                       {kModAlt, platform::Key::Alt, 0x38, platform::kModifierAlt}};
    for (const ModifierKey& key : keys) {
        if (!(mods & key.mod)) continue;
        if (down) input::PressKey(key.key, key.scan, key.modifiers);
        else input::ReleaseKey(key.key, key.scan, key.modifiers);
    }
}

// The modifiers at the end of an input step's argument, split off it (`argument` keeps the rest) and
// held until this goes out of scope. Only words after at least one other word count, so "key ctrl"
// stays the key ctrl. The press and release go through the same message queue as the step's input.
struct HeldModifiers {
    explicit HeldModifiers(std::string* argument) {
        std::string rest = *argument;
        for (;;) {
            std::size_t end = rest.find_last_not_of(" \t+,");
            if (end == std::string::npos) break;
            std::size_t begin = rest.find_last_of(" \t+,", end);
            if (begin == std::string::npos) break;   // a single word is the argument itself
            unsigned one = ModifierWord(rest.substr(begin + 1, end - begin));
            if (!one) break;
            mods |= one;
            rest.erase(begin);
        }
        *argument = rest;
        PostModifiers(mods, true);
    }
    ~HeldModifiers() { PostModifiers(mods, false); }
    unsigned mods = 0;
};

// The character a key types in harness runs (TypedCharacter, which harness_platform.cpp asks for each key down, in
// place of the system's text input). Windows makes a WM_CHAR from a key message with the thread's keyboard layout and key state, which follow the user's own layout
// switches and held keys even though the harness's game never has the focus: in pairs a typed hero name came out
// "ppeEeErR" (Shift held) or "peerr" (a layout without Latin letters) as well as "ppeeeerr" (SV5d2). Harness runs
// translate as the US layout with no key held: letters and digits make no WM_CHAR here (the "key" step posts
// theirs, so a name comes out as typed), the other keys their fixed character (Enter 0x0D, Esc 0x1B, ...).

char FixedKeyChar(platform::Key key) {
    switch (key) {
    case platform::Key::Backspace: return 0x08;
    case platform::Key::Tab: return 0x09;
    case platform::Key::Return: return 0x0D;
    case platform::Key::Escape: return 0x1B;
    case platform::Key::Space: return ' ';
    case platform::Key::KeypadMultiply: return '*';
    case platform::Key::KeypadPlus: return '+';
    case platform::Key::KeypadMinus: return '-';
    case platform::Key::KeypadDivide: return '/';
    case platform::Key::Semicolon: return ';';
    case platform::Key::Equals: return '=';
    case platform::Key::Comma: return ',';
    case platform::Key::Minus: return '-';
    case platform::Key::Period: return '.';
    case platform::Key::Slash: return '/';
    case platform::Key::Grave: return '`';
    case platform::Key::LeftBracket: return '[';
    case platform::Key::Backslash: return '\\';
    case platform::Key::RightBracket: return ']';
    case platform::Key::Apostrophe: return '\'';
    default: return 0;
    }
}

// The character a key press types (platform/harness_platform.h): the US layout's, with no key held.
int TypedCharacter(platform::Key key) { return static_cast<unsigned char>(FixedKeyChar(key)); }

// Which messages PostKey posts: the press and release ("key"), or only one of them ("keydown" / "keyup").
enum class KeyPart { Both, Down, Up };

void PostKey(const std::string& name, KeyPart part = KeyPart::Both) {
    using platform::Key;
    Key vk = Key::None;
    if (name.size() == 1 && std::isalpha(static_cast<unsigned char>(name[0]))) vk = KeyAfter(Key::A, std::tolower(static_cast<unsigned char>(name[0])) - 'a');
    else if (name.size() == 1 && std::isdigit(static_cast<unsigned char>(name[0]))) vk = KeyAfter(Key::Digit0, name[0] - '0');
    else if (name == "esc") vk = platform::Key::Escape;
    else if (name == "enter" || name == "enterchar") vk = platform::Key::Return;
    else if (name == "space") vk = platform::Key::Space;
    else if (name == "tab") vk = platform::Key::Tab;
    else if (name == "up") vk = platform::Key::Up;
    else if (name == "down") vk = platform::Key::Down;
    else if (name == "left") vk = platform::Key::Left;
    else if (name == "right") vk = platform::Key::Right;
    else if (name == "plus") vk = platform::Key::KeypadPlus;
    else if (name == "minus") vk = platform::Key::KeypadMinus;
    else if (name == "grave") vk = platform::Key::Grave;   // the console key (scan code 0x29)
    else if (name == "backspace") vk = platform::Key::Backspace;
    else if (name == "delete") vk = platform::Key::Delete;
    else if (name == "home") vk = platform::Key::Home;
    else if (name == "end") vk = platform::Key::End;
    else if (name == "pgup") vk = platform::Key::PageUp;
    else if (name == "pgdn") vk = platform::Key::PageDown;
    else if (name == "scrolllock") vk = platform::Key::ScrollLock;   // scan 0x46: the screenshot key (005F78C0)
    else if (name == "ctrl") vk = platform::Key::Control;   // scan 0x1D: sets Interface3D +0x7C while down
    else if (name == "shift") vk = platform::Key::Shift;    // scan 0x2A: +0x7D
    else if (name.size() >= 2 && name[0] == 'f') vk = KeyAfter(Key::F1, std::atoi(name.c_str() + 1) - 1);
    if (vk == Key::None) {
        Log("harness: unknown key '%s'", name.c_str());
        return;
    }
    // The scan code a US keyboard gives (input::ScanCode, as Windows' MapVirtualKey on that layout).
    unsigned scan = input::ScanCode(vk);
    // keydown / keyup mark the navigation keys extended (lParam bit 24) as Windows does: the game's key
    // table (0079BF70) is indexed by the scan code with that bit, and the camera keys of keyboard.ini
    // (arrows, Page Up / Down, Home) are the extended ones. "key" posts them without it, as it always
    // has (its scenarios' goldens; the arrows then read as the numeric keypad's).
    const bool extended = vk == platform::Key::Up || vk == platform::Key::Down || vk == platform::Key::Left || vk == platform::Key::Right ||
                          vk == platform::Key::PageUp || vk == platform::Key::PageDown || vk == platform::Key::Home ||
                          vk == platform::Key::End || vk == platform::Key::Delete;
    if (part != KeyPart::Both && extended) scan |= 0x100;
    if (part != KeyPart::Up) {
        input::PressKey(vk, scan);
        if (part == KeyPart::Both && vk >= Key::Digit0 && vk <= Key::Z)   // a letter's or a digit's: the name's
            input::TypeCharacter(static_cast<unsigned>(std::tolower(static_cast<unsigned char>(name[0]))), scan);
        if (part == KeyPart::Both && name == "enterchar") input::TypeCharacter(0x0D, scan);
    }
    if (part != KeyPart::Down) input::ReleaseKey(vk, scan);
}

// The image's renderer's present counts.
image::WindowPresents HarnessWindowPresents() { return image::WindowPresentCounts(); }

// The client area mouse steps aim at: the game window's, or in the switch-over the frame's
// (render::FrameInWindow: the frame may be scaled into a larger window, R9c); a point of it reaches
// the window through ToWindow.
Rect GameClient() {
    if (const std::optional<Size> frame = image::FrameInWindow()) return Rect::Sized({}, *frame);
    int width = 0, height = 0;
    if (harnessplatform::ClientSize(&width, &height)) return Rect{0, 0, width, height};
    return Rect{0, 0, 0, 0};
}
Point ToWindow(int x, int y) {
    return image::FrameToWindow({x, y});
}

// "cursormark" / "cursorcheck": the presents' system cursor (R40).
image::CursorPresents g_cursorMark;
bool g_cursorCollecting = false;   // "cursormark" on: the shapes shown at the identity ramp are kept (SeenCursors)

// Whether the gamma ramp is the identity.
bool RampIsIdentity() {
    const std::uint8_t* ramp = image::GammaRampEntries();
    for (int i = 0; i < 256; ++i)
        for (int c = 0; c < 3; ++c)
            if (ramp[i * 4 + c] != i) return false;
    return true;
}

// "cursorcheck gamma": the shape shown now is one of those seen since "cursormark" (made at the identity ramp: the
// picture has several frames) through the ramp the present applies: same size, hot spot and alpha, each colour the
// ramp's entry for the seen one; where the ramp is not the identity some colours differ. A failure message, or "".
std::string GammaShapeCheck(const harnessplatform::ShownCursor& shown) {
    const std::vector<harnessplatform::ShownCursor>& seen = harnessplatform::SeenCursors();
    if (seen.empty()) return "cursorcheck gamma: no shape seen since cursormark at the identity ramp";
    if (shown.rgba.empty()) return "cursorcheck gamma: no cursor shown";
    const std::uint8_t* ramp = image::GammaRampEntries();
    const bool identity = RampIsIdentity();
    std::size_t fewest = shown.rgba.size();
    bool matched = false, changed = false;
    for (const harnessplatform::ShownCursor& before : seen) {
        if (shown.width != before.width || shown.height != before.height || shown.hotX != before.hotX ||
            shown.hotY != before.hotY)
            continue;
        std::size_t wrong = 0;
        bool differs = false;
        for (std::size_t i = 0; i < before.rgba.size(); i += 4) {
            wrong += shown.rgba[i + 3] != before.rgba[i + 3];
            for (int c = 0; c < 3; ++c) {
                wrong += shown.rgba[i + c] != ramp[before.rgba[i + c] * 4 + c];
                differs = differs || shown.rgba[i + c] != before.rgba[i + c];
            }
        }
        fewest = (std::min)(fewest, wrong);
        if (!wrong) {
            matched = true;
            changed = changed || differs;
        }
    }
    Log("harness: cursorcheck gamma: ramp %s, %zu shapes seen, %s (the closest differs in %zu values)",
        identity ? "identity" : "set", seen.size(),
        matched ? (changed ? "the shown one is a seen one through the ramp, changed" : "the shown one is a seen one, unchanged")
                : "none matches",
        fewest);
    if (!matched) return "cursorcheck gamma: the shape shown is none of those seen at the identity ramp through the present's ramp";
    if (!identity && !changed) return "cursorcheck gamma: the ramp is not the identity but the shape did not change";
    return {};
}

// The last frame with the shown cursor drawn in at the game's cursor position less its hot spot (each frame pixel takes
// the cursor's pixel under its top left corner, alpha blended) into frames\<name>.bmp.
std::string WriteCursorFrame(const char* name, const image::CursorState& game) {
    harnessplatform::ShownCursor cursor;
    const std::optional<Size> placed = image::FramePlaced();
    std::optional<image::FrameImage> frame = image::ReadFrame();
    if (!harnessplatform::CursorShown(&cursor) || !placed || !frame || frame->width <= 0 || frame->height <= 0)
        return "cursorcheck: no frame or cursor to write";
    const int fw = frame->width, fh = frame->height;
    std::vector<std::uint8_t>& bgr = frame->bgr;
    const int pw = placed->w, ph = placed->h;
    const int left = game.x - game.hotX, top = game.y - game.hotY;
    for (int y = (std::max)(0, top); y < fh; ++y) {
        const int cy = static_cast<int>(static_cast<long long>(y - top) * ph / fh);
        if (cy >= cursor.height) break;
        for (int x = (std::max)(0, left); x < fw; ++x) {
            const int cx = static_cast<int>(static_cast<long long>(x - left) * pw / fw);
            if (cx >= cursor.width) break;
            const std::uint8_t* c = &cursor.rgba[(static_cast<std::size_t>(cy) * cursor.width + cx) * 4];
            std::uint8_t* f = &bgr[(static_cast<std::size_t>(y) * fw + x) * 3];
            for (int k = 0; k < 3; ++k) f[k] = static_cast<std::uint8_t>((c[2 - k] * c[3] + f[k] * (255 - c[3]) + 127) / 255);
        }
    }
    std::string path;
    if (!capture::WriteFrame(name, fw, fh, bgr, &path)) return std::string("cursorcheck: cannot write ") + name;
    g_capturedFrames.push_back(path + " (the last frame with the system's cursor drawn in)");
    return {};
}

std::string CursorCheck(const std::string& argument) {
    char name[64] = "", what[16] = "";
    unsigned least = 1;
    if (std::sscanf(argument.c_str(), "%63s %15s %u", name, what, &least) < 2)
        return "cursorcheck needs <name>|- shown|hidden|held|animated|gamma [min]";
    const image::CursorPresents now = image::CursorPresentCounts();
    const image::CursorPresents& mark = g_cursorMark;
    const image::CursorState game = HarnessCursor();
    harnessplatform::ShownCursor shown;
    const bool isShown = harnessplatform::CursorShown(&shown);
    Point heldAt{};
    const bool isHeld = harnessplatform::CursorHeld(&heldAt);
    Log("harness: cursorcheck %s: since cursormark %u presents showed the cursor, %u held it, %u hid it; %u frame "
        "steps, %u shapes made in all (%u failed); shown %s %dx%d (hot spot %d, %d)%s; the game's image %dx%d (%d "
        "frames, hot spot %d, %d) shown %d, %s at %d, %d",
        what, now.presentsShown - mark.presentsShown, now.presentsHeld - mark.presentsHeld,
        now.presentsHidden - mark.presentsHidden, now.frameChanges - mark.frameChanges, now.made, now.failures,
        isShown ? "yes" : "no", shown.width, shown.height, shown.hotX, shown.hotY, isHeld ? ", held" : "", game.width,
        game.height, game.frames, game.hotX, game.hotY, game.imageShown, game.held ? "held" : "free", game.x, game.y);
    char failure[200] = "";
    const std::string kind = what;
    if (now.failures) {
        std::snprintf(failure, sizeof(failure), "cursorcheck: the system could not make %u shapes", now.failures);
    } else if (kind == "shown") {
        const Size frame = image::FrameInWindow().value_or(Size{});
        const Size placed = image::FramePlaced().value_or(Size{});
        const int fw = frame.w, fh = frame.h, pw = placed.w, ph = placed.h;
        const int width = fw > 0 ? (std::max)(1, (game.width * pw + fw / 2) / fw) : 0;
        const int height = fh > 0 ? (std::max)(1, (game.height * ph + fh / 2) / fh) : 0;
        int opaque = 0, clear = 0;
        for (std::size_t i = 3; i < shown.rgba.size(); i += 4) {
            opaque += shown.rgba[i] == 255;
            clear += shown.rgba[i] == 0;
        }
        if (now.presentsShown - mark.presentsShown < least || !isShown || isHeld)
            std::snprintf(failure, sizeof(failure), "cursorcheck shown: not shown (or held)");
        else if (!game.image || game.width <= 0 || game.height <= 0 || shown.width != width || shown.height != height ||
                 shown.width != now.width || shown.height != now.height)
            std::snprintf(failure, sizeof(failure), "cursorcheck shown: %dx%d, expected the image's %dx%d at the window's scale: %dx%d",
                          shown.width, shown.height, game.width, game.height, width, height);
        else if (shown.hotX != (std::min)(width - 1, (game.hotX * width + game.width / 2) / game.width) ||
                 shown.hotY != (std::min)(height - 1, (game.hotY * height + game.height / 2) / game.height))
            std::snprintf(failure, sizeof(failure), "cursorcheck shown: hot spot %d, %d, the image's %d, %d", shown.hotX,
                          shown.hotY, game.hotX, game.hotY);
        else if (!opaque || !clear)
            std::snprintf(failure, sizeof(failure), "cursorcheck shown: %d opaque and %d transparent pixels", opaque, clear);
        else if (std::strcmp(name, "-") != 0)
            return WriteCursorFrame(name, game);
    } else if (kind == "hidden") {
        if (now.presentsHidden - mark.presentsHidden < least)
            std::snprintf(failure, sizeof(failure), "cursorcheck hidden: %u presents hid the cursor, expected %u or more",
                          now.presentsHidden - mark.presentsHidden, least);
    } else if (kind == "held") {
        if (now.presentsHeld - mark.presentsHeld < least || !isShown || !isHeld || !game.held || heldAt.x != game.x ||
            heldAt.y != game.y)
            std::snprintf(failure, sizeof(failure), "cursorcheck held: held at %d, %d (%s), the game's cursor at %d, %d (%s)",
                          heldAt.x, heldAt.y, isHeld ? "held" : "not held", game.x, game.y, game.held ? "held" : "free");
    } else if (kind == "animated") {
        if (now.frameChanges - mark.frameChanges < least)
            std::snprintf(failure, sizeof(failure), "cursorcheck animated: %u frame steps, expected %u or more",
                          now.frameChanges - mark.frameChanges, least);
    } else if (kind == "gamma") {
        return GammaShapeCheck(shown);
    } else {
        return "cursorcheck needs <name>|- shown|hidden|held|animated|gamma [min]";
    }
    return failure;
}

// "windowtitle": the game window's title is "Evil Islands" and its class "EIGAME" (the strings game.exe's table held,
// literals in app/main_loop.cpp since C23). The window class has no icon (C24 adds one).
std::string WindowTitleStep() {
    const std::string title = harnessplatform::WindowTitle(), cls = harnessplatform::WindowClass();
    Log("harness: windowtitle: title \"%s\", class \"%s\"", title.c_str(), cls.c_str());
    // The harness adds " (harness)" to the title of the window it creates (harness_platform.cpp's CreateGameWindow).
    if (title != "Evil Islands" && title != "Evil Islands (harness)") return "windowtitle: the window's title is \"" + title + "\"";
    if (cls != "EIGAME") return "windowtitle: the window's class is \"" + cls + "\"";
    return {};
}

// The layout point (x, y) of the 800x600 interface as the mouse steps' share of a window of width x height: the layout
// fills the window's 4:3 part, the largest one, centred (src/game/ui/screen_layout.h).
void LayoutToWindowShare(int width, int height, int* x, int* y) {
    const double part43Width = width * 3 < height * 4 ? width : height * 4.0 / 3.0;
    const double part43Height = height * 4 < width * 3 ? height : width * 3.0 / 4.0;
    const double pixelX = (width - part43Width) / 2 + *x * part43Width / 800;
    const double pixelY = (height - part43Height) / 2 + *y * part43Height / 600;
    *x = static_cast<int>(pixelX * 800 / width + 0.5);
    *y = static_cast<int>(pixelY * 600 / height + 0.5);
}

void PostMouse(const std::string& verb, int x, int y) {
    Rect client = GameClient();
    Point window = ToWindow(x * (client.right - client.left) / 800, y * (client.bottom - client.top) / 600);
    const Point at = window;
    g_mouseX = window.x;
    g_mouseY = window.y;
    input::MoveMouse(at);
    if (verb == "click") {
        input::PressButton(MouseButton::Left, at);
        input::ReleaseButton(MouseButton::Left, at);
    } else if (verb == "rclick") {
        input::PressButton(MouseButton::Right, at);
        input::ReleaseButton(MouseButton::Right, at);
    } else if (verb == "dblclick") {
        // What Windows sends for a double click: down, up, double click, up.
        input::PressButton(MouseButton::Left, at);
        input::ReleaseButton(MouseButton::Left, at);
        input::PressButton(MouseButton::Left, at, 2);
        input::ReleaseButton(MouseButton::Left, at);
    } else if (verb == "ldown") {
        input::PressButton(MouseButton::Left, at);
    } else if (verb == "lup") {
        input::ReleaseButton(MouseButton::Left, at);
    } else if (verb == "rdown") {
        input::PressButton(MouseButton::Right, at);
    } else if (verb == "rup") {
        input::ReleaseButton(MouseButton::Right, at);
    } else if (verb == "rdblclick") {
        input::PressButton(MouseButton::Right, at);
        input::ReleaseButton(MouseButton::Right, at);
        input::PressButton(MouseButton::Right, at, 2);
        input::ReleaseButton(MouseButton::Right, at);
    }
}

// A wheel turn at (x, y) in the 800x600 layout.
void PostWheel(int x, int y, int delta) {
    Rect client = GameClient();
    Point point = ToWindow(x * (client.right - client.left) / 800, y * (client.bottom - client.top) / 600);
    input::MoveMouse(point);
    input::TurnWheel(delta, point);
}

// The mouse step `verb` (clickregion, rclickregion, dblclickregion or moveregion) at layout point `point`: through
// the game's layout-to-screen scaling (patched by Rebirth), then to the window's client size.
void PostLayoutMouse(const std::string& verb, Point point) {
    int width = 0, height = 0;
    image::ScaleToScreen(&point.x, &point.y);
    image::InterfaceSize(&width, &height);
    Rect client = GameClient();
    if (width > 0 && height > 0) {
        point.x = point.x * (client.right - client.left) / width;
        point.y = point.y * (client.bottom - client.top) / height;
    }
    point = ToWindow(point.x, point.y);
    const Point at = point;
    g_mouseX = at.x;   // the polled cursor follows, as in PostMouse: hovers and tooltips read it
    g_mouseY = at.y;
    input::MoveMouse(at);
    if (verb == "clickregion") {
        input::PressButton(MouseButton::Left, at);
        input::ReleaseButton(MouseButton::Left, at);
    } else if (verb == "rclickregion") {
        input::PressButton(MouseButton::Right, at);
        input::ReleaseButton(MouseButton::Right, at);
    } else if (verb == "dblclickregion") {
        // As "dblclick": down, up, double click, up.
        input::PressButton(MouseButton::Left, at);
        input::ReleaseButton(MouseButton::Left, at);
        input::PressButton(MouseButton::Left, at, 2);
        input::ReleaseButton(MouseButton::Left, at);
    }
}

// The centre of region `index` of the top interface window (its layout rectangle plus the offset its hit test
// subtracts), posted as a mouse event.
void PostRegionMouse(const std::string& verb, int index) {
    void* window = TopWindow();
    const int regions = window ? WindowRegionCount(window) : 0;
    image::RegionInfo region;
    if (!window || index < 0 || index >= regions || !WindowRegion(window, index, &region)) {
        Log("harness: %s %d: no such region (window %p, %d regions)", verb.c_str(), index, window, regions);
        return;
    }
    Point point{(region.left + region.right) / 2 + region.offsetX, (region.top + region.bottom) / 2 + region.offsetY};
    Log("harness: %s %d: window %p region (%d,%d)-(%d,%d) enabled %d", verb.c_str(), index, window, region.left, region.top,
        region.right, region.bottom, region.enabled);
    PostLayoutMouse(verb, point);
}

// "droplist open | pick <n> | expect <text> | isopen <0|1>" on the first drop-down list of the window on top (the
// options' resolution row): a click on its row; a click on entry <n> of the open list (scrolled into view first); the
// chosen entry's text is <text>; the list is open or not. Each logs the list's state. "" or the failure.
std::string DropListStep(const std::string& argument) {
    void* window = TopWindow();
    image::DropListInfo list;
    if (!window || !image::DropList(window, &list)) return "droplist " + argument + ": no drop-down list in the window on top";
    const std::size_t space = argument.find(' ');
    const std::string word = argument.substr(0, space);
    const std::string rest = space == std::string::npos ? std::string() : argument.substr(space + 1);
    Log("harness: droplist %s: chosen %d '%s', %s, %d entries, %d shown from %d, active %d", argument.c_str(), list.value,
        list.text.c_str(), list.open ? "open" : "closed", list.count, list.shown, list.first, list.active);
    if (word == "open") {
        if (list.open) return "droplist open: the list is open already";
        PostLayoutMouse("clickregion", Point{(list.row.left + list.row.right) / 2, (list.row.top + list.row.bottom) / 2});
        return "";
    }
    if (word == "pick") {
        const int index = std::atoi(rest.c_str());
        Point centre{};
        if (!list.open) return "droplist pick " + rest + ": the list is not open";
        if (!image::DropListEntry(window, index, &centre))
            return "droplist pick " + rest + ": no such entry (" + std::to_string(list.count) + ")";
        PostLayoutMouse("clickregion", centre);
        return "";
    }
    if (word == "expect") return list.text == rest ? "" : "droplist expect " + rest + ": the list shows '" + list.text + "'";
    if (word == "isopen") {
        const bool wanted = rest == "1";
        if (list.open == wanted) return "";
        return "droplist isopen " + rest + ": the list is " + (list.open ? "open" : "closed");
    }
    return "droplist needs open, pick <n>, expect <text> or isopen <0|1>: " + argument;
}

// --- clickunit <name> [l|r|move] [mods] (ticket I8; the modifiers are HeldModifiers') -------------------------------------------
// A click on a unit's body, placed where the game's own picking finds it. The client world
// (007AF5B8) keeps a list (screenEntries: the object's class id, its id and its screen rectangle) of
// the objects drawn this frame with the rectangles of their projected vertices (written by the object
// draw, 004E8900 / 005A7A70).
// 00565E40 (the world's thiscall, RET 4) is the unit at a point that the battle screen's left
// button release (00638B30) asks with the message's own coordinates: those rectangles that hold
// the point, the client object's finer test (+0x94 slot 24), the nearest unit (type 0x50). The
// step tries the rectangle's centre and then a grid inside it until 00565E40 answers with the
// unit, so a click there selects or orders that unit and no other in front of it.
// 00461360 (__fastcall) turns the script name into the id AddUnitToServer gave the unit.
constexpr std::uint32_t kPickableUnit = 0x400000;   // the flag 00638B30 requires of the unit

// Where a click finds the unit with script name `name` in this frame (client coordinates) and the
// unit the game's pick returns there; "" or why there is no such point. `what` names the step in
// the log and the error (null: nothing logged).
// WorldClient::Pick (by the parts) and UnitAt.
void* HarnessPickAt2(void* world, const Point* p) { return image::PickAt2(world, p->x, p->y); }
void* HarnessUnitAt(void* world, const Point* p) { return image::UnitAt(world, p->x, p->y); }
// The client world's rectangle of object `id` in this frame's screen list (the last entry for it): 1, 0 when it is
// not drawn, -1 without a list.
int ScreenRect(void* world, int id, Rect* rect) { return image::ScreenRect(world, id, rect); }
// A picked object's id and flags (the EngineObject's id and flags).
int PickedId(const void* object) { return image::ObjectId(object); }
std::uint32_t PickedFlags(const void* object) { return image::ObjectFlags(object); }
std::string FindUnitPoint(const char* name, const char* what, Point* point, void** unit) {
    std::string label = std::string(what ? what : "waitunit") + " " + name;
    const bool byId = name[0] == '#';   // a map object: the battle screen's object pick
    int id = byId ? std::atoi(name + 1) : image::TextKey(name);

    void* world = HarnessClientWorld();
    // The unit's rectangle in this frame's list (the last entry for the id).
    Rect bounds{};
    const Rect* rect = &bounds;
    int listed = ScreenRect(world, id, &bounds);
    if (listed < 0) return label + ": no pick list (not in a game)";
    if (listed == 0) return label + " (id " + std::to_string(id) + "): not drawn this frame (off screen?)";
    if (what)
        Log("harness: %s: id %d, rectangle (%d,%d)-(%d,%d)", label.c_str(), id, rect->left, rect->top, rect->right,
            rect->bottom);
    // Candidates: the centre, then a 9x9 grid inside the rectangle from the middle outwards.
    std::vector<Point> candidates;
    int cx = (rect->left + rect->right) / 2, cy = (rect->top + rect->bottom) / 2;
    candidates.push_back({cx, cy});
    constexpr int kSteps = 9;
    for (int ring = 1; ring <= kSteps / 2; ++ring) {
        for (int j = -ring; j <= ring; ++j) {
            for (int i = -ring; i <= ring; ++i) {
                if (i != -ring && i != ring && j != -ring && j != ring) continue;
                int x = cx + i * (rect->right - rect->left) / (kSteps + 1);
                int y = cy + j * (rect->bottom - rect->top) / (kSteps + 1);
                candidates.push_back({x, y});
            }
        }
    }
    for (const Point& p : candidates) {
        void* hit = byId ? HarnessPickAt2(world, &p) : HarnessUnitAt(world, &p);
        if (hit && PickedId(hit) == id) {
            *point = p;
            *unit = hit;
            return "";
        }
    }
    return label + ": the game's pick (00565E40) finds another object everywhere in its rectangle";
}

std::string PostUnitMouse(const std::string& argument) {
    char name[64] = "", button[8] = "l";
    if (std::sscanf(argument.c_str(), "%63s %7s", name, button) < 1)
        return "clickunit needs <name> [l|r|move|dbl] [ctrl|shift|alt]";
    Point at{};
    void* hit = nullptr;
    std::string error = FindUnitPoint(name, "clickunit", &at, &hit);
    if (!error.empty()) return error;
    std::uint32_t flags = PickedFlags(hit);
    int clientWidth = 0, clientHeight = 0;
    harnessplatform::ClientSize(&clientWidth, &clientHeight);
    Log("harness: clickunit %s: at (%d,%d) (client %dx%d), flags %08X%s", name, at.x, at.y, clientWidth, clientHeight, flags,
        (flags & kPickableUnit) ? "" : " (no 0x400000: the battle screen ignores it)");
    // The battle screen reads the message's coordinates as they are (the pick space is the client
    // area); the harness's cursor follows them.
    at = ToWindow(at.x, at.y);   // the frame's pixel in the window (R9c)
    g_mouseX = at.x;
    g_mouseY = at.y;
    const Point where = at;
    input::MoveMouse(where);
    if (std::strcmp(button, "l") == 0) {
        input::PressButton(MouseButton::Left, where);
        input::ReleaseButton(MouseButton::Left, where);
    } else if (std::strcmp(button, "r") == 0) {
        input::PressButton(MouseButton::Right, where);
        input::ReleaseButton(MouseButton::Right, where);
    } else if (std::strcmp(button, "dbl") == 0) {
        // The second half of a double click only: the release after it carries the interface's
        // double-click flag (+0x7F) into the battle screen's SelectUnit (rebirth_fixes_ui).
        input::PressButton(MouseButton::Left, where, 2);
        input::ReleaseButton(MouseButton::Left, where);
    }
    return "";
}

// --- clickground <x> <y> [click|dblclick|rclick|move] (SV5d1) -----------------------------------------
// A mouse step on the ground point (x, y) of the map: the 800x600 layout point whose ground, as the
// game's own pick finds it (WorldClient::PickSurface, what the briefing screen's walk and the
// battle screen's orders ask with the message's coordinates), lies nearest to (x, y). Searched on a
// 10-pixel grid, then pixel by pixel around the best; fails when no point comes within 1.5 units (the
// point is off screen or hidden). The pick only reads the view and the terrain (its pick stack entry
// is popped again), so the step is the plain mouse step at the point found.
void HarnessPickSurface(void* world, float* out, int x, int y) { image::PickSurface(world, out, x, y); }

// --- blink faces|open|close (R11d) ------------------------------------------------------------------
// faces: the client world loads its units' face textures (the game does it when a zone is entered between
// zones, not for a new game). open / close: every unit of the client world with a face texture shows its face's
// open or closed state: the game's own blink (forced), as the briefing's speakers do. Fails when no unit has a
// face.
void HarnessLoadUnitFaces(void* world) { image::LoadUnitFaces(world); }
void HarnessBlinkOpen(void* half) { image::BlinkOpen(half); }
void HarnessBlinkClose(void* half) { image::BlinkClose(half); }

std::string BlinkStep(const std::string& argument) {
    if (argument != "faces" && argument != "open" && argument != "close") return "blink needs faces, open or close";
    void* world = HarnessClientWorld();
    if (argument == "faces") HarnessLoadUnitFaces(world);
    std::vector<void*> halves;
    if (!image::UnitFaceHalves(&halves)) return "blink: no client world";

    int faces = 0;
    for (void* half : halves) {
        if (argument == "open") HarnessBlinkOpen(half);
        else if (argument == "close") HarnessBlinkClose(half);
        ++faces;
    }
    Log("harness: blink %s: %d units with a face", argument.c_str(), faces);
    return faces ? "" : "blink: no unit has a face texture";
}

// --- skin <unit> <first> [<second>], unitcolour <frame> <a> <b> <least> <most> (R49) ---------------------------
// A unit made by AddUnitToServer has no texture names (its party record's texts 3 and 4 are empty, as the original's
// were), so its figure draws untextured: the original's look, which only the harness's spawns reach. "skin" gives the
// client's unit the textures a map placement of its kind names (zone1's wolves: Wolf00 GoblinAxeBag00), as soon as
// the unit reaches the client (checked every frame, so the step takes no frame); the client draws it, nothing of
// the game's state changes. A unit is a script name (as GetObjectByName) or "#<id>".
int UnitIdOf(const std::string& unit) {
    return !unit.empty() && unit[0] == '#' ? std::atoi(unit.c_str() + 1) : image::TextKey(unit.c_str());
}

void ApplyPendingSkins() {
    std::erase_if(g_pendingSkins, [](const PendingSkin& skin) {
        if (!image::SetObjectSkins(skin.id, skin.first.c_str(), skin.second.empty() ? nullptr : skin.second.c_str()))
            return false;
        Log("harness: skin %s: %s %s", skin.unit.c_str(), skin.first.c_str(), skin.second.c_str());
        return true;
    });
}

std::string SkinStep(const std::string& argument) {
    char unit[64] = "", first[64] = "", second[64] = "";
    if (std::sscanf(argument.c_str(), "%63s %63s %63s", unit, first, second) < 2) return "skin needs <unit> <first> [<second>]";
    g_pendingSkins.push_back({UnitIdOf(unit), unit, first, second});
    ApplyPendingSkins();
    return "";
}

// groundprints [ranges]: the client world's ground prints logged, and their counts in the ranges given.
std::string GroundPrintsStep(const std::string& argument) {
    const image::GroundPrintCounts counts = image::CountGroundPrints();
    Log("harness: groundprints: blood %d, footprints %d (%d shown), scorch %d, life left %d", counts.blood,
        counts.footprints, counts.shownFootprints, counts.scorch, counts.lifeLeft);
    int range[6] = {0, 0, 0, 0, 0, 0};
    const int read = std::sscanf(argument.c_str(), "%d %d %d %d %d %d", &range[0], &range[1], &range[2], &range[3],
                                 &range[4], &range[5]);
    if (read <= 0) return "";
    if (read != 4 && read != 6) return "groundprints needs the least and the most blood and footprints [and scorch]";
    const int have[3] = {counts.blood, counts.footprints, counts.scorch};
    const char* names[3] = {"blood", "footprints", "scorch"};
    for (int i = 0; i < read / 2; ++i) {
        if (have[i] < range[2 * i] || have[i] > range[2 * i + 1])
            return std::string("groundprints: ") + names[i] + " " + std::to_string(have[i]) + ", wanted " +
                   std::to_string(range[2 * i]) + " .. " + std::to_string(range[2 * i + 1]);
    }
    return "";
}

// unitskins <unit> <first> <second>: the texture names the client's unit holds.
std::string UnitSkinsStep(const std::string& argument) {
    char unit[64] = "", first[64] = "", second[64] = "";
    if (std::sscanf(argument.c_str(), "%63s %63s %63s", unit, first, second) != 3) return "unitskins needs <unit> <first> <second>";
    std::string haveFirst, haveSecond;
    if (!image::ObjectSkins(UnitIdOf(unit), &haveFirst, &haveSecond)) return std::string("unitskins: ") + unit + " is not on the client";
    Log("harness: unitskins %s: '%s' '%s'", unit, haveFirst.c_str(), haveSecond.c_str());
    if (haveFirst == first && haveSecond == second) return "";
    return std::string("unitskins ") + unit + ": has '" + haveFirst + "' '" + haveSecond + "', wanted '" + first + "' '" + second + "'";
}

// The mean colour of the middle half (each way) of unit `a`'s and unit `b`'s rectangles in this frame's screen list
// (the parts drawn: frozen, the frame captured shows the same) in frames\<frame>.bmp; the largest channel difference
// of the two means must lie within <least>..<most>. A spawned wolf with Wolf00 against zone1's LoneWolf: the same
// colours (render_unit_skin).
std::string UnitColourStep(const std::string& argument) {
    char frame[64] = "", names[2][64] = {"", ""};
    double least = 0, most = 0;
    if (std::sscanf(argument.c_str(), "%63s %63s %63s %lf %lf", frame, names[0], names[1], &least, &most) < 5)
        return "unitcolour needs <frame> <a> <b> <least> <most>";
    double means[2][3] = {};
    Rect rects[2] = {};
    for (int k = 0; k < 2; ++k) {
        const int listed = ScreenRect(HarnessClientWorld(), UnitIdOf(names[k]), &rects[k]);
        if (listed <= 0) return std::string("unitcolour: ") + names[k] + (listed < 0 ? ": no screen list" : ": not drawn");
        const Rect& r = rects[k];
        const int qx = (r.right - r.left) / 4, qy = (r.bottom - r.top) / 4;
        const char* why = "";
        if (!capture::MeanColour(frame, r.left + qx, r.top + qy, r.right - qx, r.bottom - qy, means[k], &why))
            return std::string("unitcolour ") + frame + " " + names[k] + ": " + why;
    }
    double largest = 0;
    for (int c = 0; c < 3; ++c) largest = (std::max)(largest, std::abs(means[0][c] - means[1][c]));
    char text[320];
    std::snprintf(text, sizeof(text),
                  "unitcolour %s: %s (%d,%d)-(%d,%d) rgb %.0f %.0f %.0f, %s (%d,%d)-(%d,%d) rgb %.0f %.0f %.0f: largest "
                  "difference %.1f (allowed %.1f .. %.1f)",
                  frame, names[0], rects[0].left, rects[0].top, rects[0].right, rects[0].bottom, means[0][2], means[0][1],
                  means[0][0], names[1], rects[1].left, rects[1].top, rects[1].right, rects[1].bottom, means[1][2],
                  means[1][1], means[1][0], largest, least, most);
    Log("harness: %s", text);
    g_capturedFrames.push_back(text);
    return largest < least || largest > most ? text : "";
}

std::string PostGroundMouse(const std::string& argument) {
    float wx = 0.0f, wy = 0.0f;
    char verb[16] = "click";
    if (std::sscanf(argument.c_str(), "%f %f %15s", &wx, &wy, verb) < 2)
        return "clickground needs <x> <y> [click|dblclick|rclick|move]";
    const std::string kind = verb;
    if (kind != "click" && kind != "dblclick" && kind != "rclick" && kind != "move")
        return "clickground: '" + kind + "' is no mouse step (click, dblclick, rclick, move)";
    const Rect client = GameClient();
    const int cw = client.right - client.left, ch = client.bottom - client.top;
    auto distance = [&](int lx, int ly) {
        float out[3] = {};
        HarnessPickSurface(HarnessClientWorld(), out, lx * cw / 800, ly * ch / 600);
        const double dx = double(out[0]) - wx, dy = double(out[1]) - wy;
        return dx * dx + dy * dy;
    };
    int bestX = -1, bestY = -1;
    double best = 1e30;
    auto consider = [&](int lx, int ly) {
        if (lx < 0 || ly < 0 || lx >= 800 || ly >= 600) return;
        const double d = distance(lx, ly);
        if (d < best) best = d, bestX = lx, bestY = ly;
    };
    for (int ly = 5; ly < 600; ly += 10)
        for (int lx = 5; lx < 800; lx += 10) consider(lx, ly);
    const int cx = bestX, cy = bestY;
    for (int ly = cy - 10; ly <= cy + 10; ++ly)
        for (int lx = cx - 10; lx <= cx + 10; ++lx) consider(lx, ly);
    char text[200];
    std::snprintf(text, sizeof(text), "clickground %.2f %.2f: layout (%d,%d), %.2f away", wx, wy, bestX, bestY,
                  best < 1e29 ? std::sqrt(best) : -1.0);
    Log("harness: %s", text);
    if (!(best <= 1.5 * 1.5)) return std::string(text) + ": no screen point picks the ground there";
    PostMouse(kind, bestX, bestY);
    return "";
}

// --- groundview (SV5d3) ---------------------------------------------------------------------------
// Logs the map points the ground pick finds at the layout's centre and corners (where the camera looks;
// for writing a scenario's clickground steps).
void LogGroundView() {
    const Rect client = GameClient();
    const int cw = client.right - client.left, ch = client.bottom - client.top;
    char text[400] = "groundview:";
    for (const auto& at : {std::pair{400, 300}, std::pair{0, 0}, std::pair{799, 0}, std::pair{0, 599}, std::pair{799, 599}}) {
        float out[3] = {};
        HarnessPickSurface(HarnessClientWorld(), out, at.first * cw / 800, at.second * ch / 600);
        const std::size_t used = std::strlen(text);
        std::snprintf(text + used, sizeof(text) - used, " (%d,%d) %.1f %.1f;", at.first, at.second, out[0], out[1]);
    }
    Log("harness: %s", text);
}

// --- minimap <cx> <cy> <sx> <sy>, lookat <x> <y> (SV5d3) -----------------------------------------------
// The battle screen's minimap is centred on the camera: a click at layout (cx + dx, cy + dy) moves the
// camera by (dx * sx, -dy * sy) map units ("minimap" gives the install's numbers: its HUD differs). lookat
// clicks the minimap where the camera, as the ground pick at the layout's centre finds it, would look at
// map point (x, y), at most 50 pixels from the centre; nothing when it is within 3 units already. The
// game's camera stops at the map's edge, and it goes back to the hero on its own at times, so a walk
// repeats lookat before its clickground steps.
struct MinimapLayout { float cx = 0, cy = 0, sx = 0, sy = 0; } g_minimap;

std::string LookAt(const std::string& argument, bool* clicked = nullptr) {
    float wx = 0.0f, wy = 0.0f;
    if (std::sscanf(argument.c_str(), "%f %f", &wx, &wy) != 2) return "lookat needs <x> <y>";
    if (!(g_minimap.sx > 0.0f && g_minimap.sy > 0.0f)) return "lookat: no minimap step before it";
    const Rect client = GameClient();
    const int cw = client.right - client.left, ch = client.bottom - client.top;
    float out[3] = {};
    HarnessPickSurface(HarnessClientWorld(), out, 400 * cw / 800, 300 * ch / 600);
    const float dx = (wx - out[0]) / g_minimap.sx, dy = -(wy - out[1]) / g_minimap.sy;
    if (clicked) *clicked = false;
    if (std::fabs(wx - out[0]) < 3.0f && std::fabs(wy - out[1]) < 3.0f) {
        Log("harness: lookat %.1f %.1f: the camera looks at %.1f %.1f", wx, wy, out[0], out[1]);
        return "";
    }
    const float length = std::sqrt(dx * dx + dy * dy), clamp = length > 50.0f ? 50.0f / length : 1.0f;
    const int lx = int(std::lround(g_minimap.cx + dx * clamp)), ly = int(std::lround(g_minimap.cy + dy * clamp));
    Log("harness: lookat %.1f %.1f: the camera looks at %.1f %.1f, minimap click (%d,%d)", wx, wy, out[0], out[1], lx, ly);
    PostMouse("click", lx, ly);
    if (clicked) *clicked = true;
    return "";
}

// --- camera <x> <y> [<heading> [<distance> [<pitch>]]] (R17c4) -----------------------------------------------
// Places the view camera directly (image::PlaceCamera): a scene aimed by map point on any install, whatever its
// minimap's layout or the heading its zone starts with (lookat's minimap clicks did not hold on Rebirth's zone1 at
// 1440x1080). Without arguments it only logs the pose.
std::string PlaceCamera(const std::string& argument) {
    image::CameraPlacement placement;
    const int given = std::sscanf(argument.c_str(), "%f %f %f %f %f", &placement.x, &placement.y, &placement.heading,
                                  &placement.distance, &placement.pitch);
    image::CameraView view;
    if (given == EOF || given == 0) {
        view = image::ViewCamera();
    } else if (given == 1) {
        return "camera needs <x> <y> [<heading> [<distance> [<pitch>]]]";
    } else {
        placement.setHeading = given >= 3;
        placement.setDistance = given >= 4;
        placement.setPitch = given >= 5;
        view = image::PlaceCamera(placement);
    }
    Log("harness: camera: focus %.2f %.2f %.2f, heading %.1f, distance %.2f, pitch %.1f", view.focusX, view.focusY,
        view.focusZ, view.heading, view.distance, view.pitch);
    return "";
}

// --- walkto <seconds> <x> <y> [<frames>] (H53) --------------------------------------------------------
// One pass of a walk's input, in a main loop pass: while aiming, the camera brought toward the point (a minimap
// click, then two frames for it to move; at most two, as the camera stops at the map's edge), then the double
// click on the ground there. "" or the error.
std::string WalkInput() {
    char point[64];
    std::snprintf(point, sizeof(point), "%.2f %.2f", g_walkX, g_walkY);
    if (g_walkLooks < 2) {
        bool clicked = false;
        if (std::string error = LookAt(point, &clicked); !error.empty()) return error;
        if (clicked) {
            ++g_walkLooks;
            g_walkNextFrame = g_frames + 2;
            return "";
        }
    }
    if (std::string error = PostGroundMouse(std::string(point) + " dblclick"); !error.empty()) return error;
    ++g_walkClicks;
    g_walkLooks = 0;
    g_walking = true;
    g_idleFrom = kNotReady;
    return "";
}

// --- hudstate [minquick] (ticket I9) ----------------------------------------------------------
// Logs the battle screen's state (the window on top): the pending action (+0x17C, +0x180), each
// party member's selection (flags 0x10) and quick items (Unit::GetQuickItem 00531920, slots
// 0-3), and the quick item panel's (+0x198) unit, entries shown and region items, the spell
// column's (+0x19C) count and highlight. With <minquick>, fails when the quick item panel shows
// fewer than that many of its unit's own quick items (regions 0-3). The reading is
// image/harness_objects_image.cpp's.
std::string HudState(const std::string& argument) {
    void* screen = TopWindow();
    if (!screen) return "hudstate: no window on top";
    return image::HudState(screen, argument);
}

// --- campputon <type> <count> (ticket I9) -----------------------------------------------------
// With the camp on top (screen 5, page 0): the first <count> items of class type <type> (hex,
// item type: 0x3004 weapon, 0x3005 armour, 0x3006 quick item, 0x3008 spell container) in the
// item strip's source list (the camp's itemStrip, its source: the player's items) are put on the hero as
// a click on them in the strip does: the camp's virtual slot 42 (TakeItem: MoveItem 00610AD0) with mode 1
// (PutOnFromStrip -> PutOn 0060FCA0). The strip's tabs do not matter. Logs every source item's
// type; fails when fewer than <count> items of the type are found or the camp is not on top.
std::string CampPutOn(const std::string& argument) {
    unsigned type = 0;
    int count = 0;
    if (std::sscanf(argument.c_str(), "%x %d", &type, &count) != 2) return "campputon needs <type hex> <count>";
    void* camp = TopWindow();
    if (!camp || WindowKind(camp) != "camp")
        return "campputon: the camp is not on top";
    // The strip's source items and their types.
    int page = 0;
    std::vector<void*> items;
    std::vector<unsigned> types;
    if (!image::CampItems(camp, &page, &items, &types)) return "campputon: the camp has no item strip source";
    std::vector<void*> chosen;
    for (std::size_t i = 0; i < items.size(); ++i) {
        Log("harness: campputon: page %d, source item %d %p type %04X", page, static_cast<int>(i), items[i], types[i]);
        if (items[i] && types[i] == type && static_cast<int>(chosen.size()) < count) chosen.push_back(items[i]);
    }
    for (void* item : chosen) {
        image::CampTakeItem(camp, item, 1);
        Log("harness: campputon: %p put on (MoveItem mode 1)", item);
    }
    if (static_cast<int>(chosen.size()) < count)
        return "campputon: " + std::to_string(chosen.size()) + " items of the type, wanted " + std::to_string(count);
    return "";
}

// --- campstock <class> scroll|prototype <spell code> <least> ------------------------------------------------------
// The magic stock of trader class <class> (its class template's spells party, made when its camp opened):
// logs each item, fails when fewer than <least> are scrolls (or prototype loot items) of the spell <code>.
std::string CampStock(const std::string& argument) {
    int type = 0, least = 0;
    char kind[16] = {}, code[64] = {};
    if (std::sscanf(argument.c_str(), "%d %15s %63s %d", &type, kind, code, &least) != 4 ||
        (std::strcmp(kind, "scroll") != 0 && std::strcmp(kind, "prototype") != 0))
        return "campstock needs <class> scroll|prototype <spell code> <least>";
    std::vector<std::string> items;
    if (!image::TraderMagic(type, &items)) return "campstock: no trader class " + std::to_string(type);
    const std::string wanted = std::string(kind) + " " + code;
    int found = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        Log("harness: campstock: class %d item %d: %s", type, static_cast<int>(i), items[i].c_str());
        if (items[i] == wanted) ++found;
    }
    Log("harness: campstock: class %d: %d of %s (at least %d)", type, found, wanted.c_str(), least);
    if (found < least)
        return "campstock: class " + std::to_string(type) + " has " + std::to_string(found) + " of " + wanted +
               ", wanted " + std::to_string(least);
    return "";
}

// --- clientbolts <least> [<most>] ---------------------------------------------------------------------------------
// The client world's lightning bolts (the server's EffectLightning objects as this game's client has them, also a
// network peer's): logs each one's ends, fails when there are fewer than <least> or more than <most>
// (net_pair_mod_spell).
std::string ClientBoltsStep(const std::string& argument) {
    int least = 0, most = INT_MAX;
    if (std::sscanf(argument.c_str(), "%d %d", &least, &most) < 1) return "clientbolts needs <least> [<most>]";
    const std::vector<std::string> bolts = image::ClientBolts();
    const int count = static_cast<int>(bolts.size());
    for (const std::string& bolt : bolts) Log("harness: clientbolts: %s", bolt.c_str());
    Log("harness: clientbolts: %d (wanted %d to %d)", count, least, most);
    if (count < least || count > most)
        return "clientbolts: " + std::to_string(count) + " bolts, wanted " + std::to_string(least) + " to " +
               std::to_string(most);
    return "";
}

// --- clienthurt <least> [<most>] ----------------------------------------------------------------------------------
// The client world's living units below their maximum hit points (the vitals the server sent this game's client, also
// a network peer's): logs each one, fails when there are fewer than <least> or more than <most> (net_pair_mod_spell:
// the blow of the host's spell on the peer's client).
std::string ClientHurtStep(const std::string& argument) {
    int least = 0, most = INT_MAX;
    if (std::sscanf(argument.c_str(), "%d %d", &least, &most) < 1) return "clienthurt needs <least> [<most>]";
    const std::vector<std::string> hurt = image::ClientHurt();
    const int count = static_cast<int>(hurt.size());
    for (const std::string& unit : hurt) Log("harness: clienthurt: %s", unit.c_str());
    Log("harness: clienthurt: %d (wanted %d to %d)", count, least, most);
    if (count < least || count > most)
        return "clienthurt: " + std::to_string(count) + " hurt units, wanted " + std::to_string(least) + " to " +
               std::to_string(most);
    return "";
}

// --- unitpart <part> <count> ---------------------------------------------------------------------------------------
// The client units wearing the figure part <part> (carried by their skeleton's bone of its name: a worn weapon's or
// armour's part attached, docs/notes/mods.md "Worn models"): logs each one, fails unless there are <count>
// (mod_showcase_worn).
std::string UnitPartStep(const std::string& argument) {
    char part[64] = "";
    int count = 0;
    if (std::sscanf(argument.c_str(), "%63s %d", part, &count) != 2) return "unitpart needs <part> <count>";
    const std::vector<std::string> wearing = image::UnitsWearing(part);
    for (const std::string& unit : wearing) Log("harness: unitpart: %s", unit.c_str());
    Log("harness: unitpart: %s on %d units (wanted %d)", part, static_cast<int>(wearing.size()), count);
    if (static_cast<int>(wearing.size()) != count)
        return "unitpart: " + std::string(part) + " on " + std::to_string(wearing.size()) + " units, wanted " +
               std::to_string(count);
    return "";
}

// --- figurealone <model> <animation> <part> <frames> ---------------------------------------------------------------
// A figure alone (FigureInstance, MF15k3): the model built without a game object, the animation played and moved on,
// the part's point read at rest and after; fails without the figure, the animation or the part, or when the part did
// not move (figure_alone).
std::string FigureAloneStep(const std::string& argument) {
    char model[64] = "", animation[64] = "", part[64] = "";
    float frames = 0.0f;
    if (std::sscanf(argument.c_str(), "%63s %63s %63s %f", model, animation, part, &frames) != 4)
        return "figurealone needs <model> <animation> <part> <frames>";
    std::vector<std::string> lines;
    const std::string failure = image::FigureAlone(model, animation, part, frames, &lines);
    for (const std::string& line : lines) Log("%s", line.c_str());
    return failure;
}

// --- waitwindow <seconds> <window> (ticket T1) ----------------------------------------------
// The interface windows a scenario waits for, by name (WindowKind, harness_windows.h).

// Whether the awaited window is on top and takes input (see the step's description).
bool WindowReady(const std::string& name) {
    void* top = TopWindow();
    if (!top || HarnessFlowState() != 0) return false;
    const std::string kind = WindowKind(top);
    if (kind != name) return false;
    if (kind == "saveload") return !image::SaveLoadSliding(top);
    if (kind == "options") return !image::OptionsSliding(top);
    return true;
}

// A recording's cut (harness_record.h, "Parts"): the steps that quick-save, end the part, and load the save at once as
// the next part's replay will (it begins with the same load, waitwindow and origin), next in line.
void InjectCut() {
    if (g_cutQueued) return;
    g_cutQueued = true;
    const std::vector<Step> cut = {{"key", "f5"},       {"waitexpect", "15 Saved saveq"}, {"recordpart", ""},
                                   {"load", "saveq"},   {"waitwindow", "30 battle"},      {"origin", ""}};
    g_steps.insert(g_steps.begin() + static_cast<std::ptrdiff_t>(g_next), cut.begin(), cut.end());
}

// For failure messages: the window on top and what keeps it from counting.
std::string DescribeTopWindow() {
    void* top = TopWindow();
    char text[200];
    std::snprintf(text, sizeof(text), "top window %p (%s), request %d, campaign mode %d", top,
                  top ? WindowKind(top).c_str() : "none", HarnessFlowState(), HarnessCampaignMode());
    return text;
}

// The image's renderer's steps (image::RenderStep).
bool IsRenderStep(const std::string& verb) {
    return verb == "terrainlevels" || verb == "levelsteps" || verb == "hues" || verb == "mmplevels" || verb == "msaa" || verb == "anisotropy" || verb == "smoothcheck" || verb == "smoothedges" || verb == "smoothorder" || verb == "shadowcasters" || verb == "shadowview" || verb == "shadowshift" || verb == "groundreach" || verb == "decals" || verb == "weatherparticles" || verb == "panelweather" || verb == "roofs" || verb == "terraincache" || verb == "meshcache" || verb == "shadowcheck" || verb == "cloudcheck" || verb == "weathercheck" || verb == "unitweather" || verb == "occlusion" || verb == "renderstats" || verb == "losedevice" || verb == "gputiming";
}

// The steps that post input messages to the game window.
bool IsInputStep(const std::string& verb) {
    return verb == "key" || verb == "keydown" || verb == "keyup" || verb == "click" || verb == "rclick" || verb == "move" || verb == "wheel" ||
           verb == "dblclick" || verb == "rdblclick" || verb == "ldown" || verb == "lup" || verb == "rdown" || verb == "rup" ||
           verb == "clickregion" || verb == "rclickregion" || verb == "dblclickregion" || verb == "moveregion" ||
           verb == "clickunit" || verb == "droplist" ||
           verb == "clickground" || verb == "lookat" || verb == "hold" || verb == "release" || verb == "focus" || verb == "windowsize" ||
           verb == "typeunicode";
}

// "expectcrash <text>": the crash the scenario is about (its failure line contains <text>, or one of the texts
// between " | ": each system's error code, say).
std::string g_expectedCrash;

bool ExpectedCrash(const std::string& what) {
    for (std::size_t at = 0; at <= g_expectedCrash.size();) {
        std::size_t end = g_expectedCrash.find(" | ", at);
        if (end == std::string::npos) end = g_expectedCrash.size();
        const std::string text = g_expectedCrash.substr(at, end - at);
        if (!text.empty() && what.find(text) != std::string::npos) return true;
        at = end + 3;
    }
    return false;
}
// The crash's failure line gives the free space for a C++ throw (a write failing on a full disk throws
// a platform::FileError) or when it is below kLowDiskMb.
constexpr long long kLowDiskMb = 1024;

// A fault nothing handled ends the run with a failure (sys::InstallFaultHandlers: an unhandled exception or a fatal
// signal, an abort, a pure virtual call, an uncaught C++ exception; re-asserted every step on Windows, where a library
// may install its own filter). The failure names a C++ throw's type and contents and, when it is low, the run disk's
// free space (harness_crash.h); ei-rebuild.log gets the code addresses on the stack.
void Fatal(const sys::Fault& fault) {
    char head[96];
    std::snprintf(head, sizeof(head), "the game crashed: %s at %08llX", fault.what.c_str(),
                  static_cast<unsigned long long>(fault.pc));
    std::string what = head;
    if (!fault.thrown.empty()) what += ": " + fault.thrown;
    const long long freeMb = sys::FreeMegabytes(g_savesDir);
    if (freeMb >= 0 && (freeMb < kLowDiskMb || fault.cppThrow))
        what += "; " + std::to_string(freeMb) + " MB free on the run folder's disk";
    what += " (stack in ei-rebuild.log)";
    Log("harness: %s", what.c_str());
    Log("harness: %lld MB free on the run folder's disk", freeMb);
    LogModuleOf(fault.pc);
    LogStack(fault.sp, 1024);
    if (!g_expectedCrash.empty() && ExpectedCrash(what)) {
        Log("harness: the crash the scenario expects (expectcrash %s)", g_expectedCrash.c_str());
        g_expectedCrash.clear();
    } else {
        g_failures.push_back(what);
    }
    sys::RunBounded([] { WriteReport(); }, 3000);
    sys::Terminate(3);
}

// A message box in a harness run is a failure nobody is there to close: the game's own boxes come to the harness's
// backend (platform::ShowMessageBox: AnswerMessageBox below), and the runtime's (abort's "abnormal program
// termination", a pure virtual call's) never show (sys::InstallFaultHandlers makes them faults). The text and the stack
// go to ei-rebuild.log and the run ends at once. During the run that is a failure; after "quit" the report is already
// written, so an "EXIT:" line is added to it (run_all.py lists those runs).
void EndRunOnMessageBox(const std::string& caption, const std::string& text) {
    std::string what = "message box '" + caption + "': " + text;
    for (char& ch : what) if (ch == '\r' || ch == '\n') ch = ' ';
    Log("harness: %s", what.c_str());
    LogModuleOf(reinterpret_cast<std::uintptr_t>(EI_HARNESS_CALLER()));
    LogStack(sys::StackHere(), 256);
    if (!g_expectedCrash.empty() && ExpectedCrash(what)) {
        Log("harness: the end the scenario expects (expectcrash %s)", g_expectedCrash.c_str());
        g_expectedCrash.clear();
        WriteReport();
        sys::Terminate(4);
    }
    if (g_quitting) {
        if (FILE* f = sys::OpenFile(g_reportPath, "a")) {
            std::fprintf(f, "EXIT: %s (stack in ei-rebuild.log)\n", what.c_str());
            std::fclose(f);
        }
        sys::Terminate(0);
    }
    g_failures.push_back(what + " (stack in ei-rebuild.log)");
    WriteReport();
    sys::Terminate(4);
}

std::string FocusStep(const std::string& argument);
std::string WindowSizeStep(const std::string& argument);

std::string RealInputStep(const std::string& argument);

void RunSteps() {
    sys::ReassertFaultHandler();
    if (!g_pendingSkins.empty()) ApplyPendingSkins();
    // The "show tutorial" option (registry value ShowTutorial). With it off, tutorial windows open only where the game
    // forces them (005F86F0, 005F8A80). Cleared every frame, since the options are reloaded when a game starts.
    HarnessClearShowTutorial();
    if (g_heldTickLength) TickLength() = g_heldTickLength;
    if (g_checking) {
        // The script prints its value on a later frame; take the first new line.
        if (g_console.size() > g_checkFrom) {
            const std::string& out = g_console[g_checkFrom];
            char* end = nullptr;
            double value = std::strtod(out.c_str(), &end);
            bool ok = end != out.c_str() && value != 0;
            Log("harness: check %s -> %s (%s)", g_checkExpr.c_str(), out.c_str(), ok ? "ok" : "FAILED");
            if (!ok) g_failures.push_back("check " + g_checkExpr + " printed '" + out + "'");
            g_checking = false;
        } else if (g_checkDeadline.Passed()) {
            Log("harness: check %s printed nothing%s", g_checkExpr.c_str(), g_checkDeadline.Why().c_str());
            g_failures.push_back("check " + g_checkExpr + " printed nothing" + g_checkDeadline.Why());
            g_checking = false;
        } else {
            return;
        }
    }
    if (g_waitingFor) {
        std::uint32_t now = Now();
        if (g_waitForPending && g_console.size() > g_waitForFrom) {
            const std::string& out = g_console[g_waitForFrom];
            char* end = nullptr;
            double value = std::strtod(out.c_str(), &end);
            g_waitForPending = false;
            if (end != out.c_str() && value != 0) {
                Log("harness: waitfor %s done after %u ms", g_waitForExpr.c_str(), now - g_waitForDeadline.start);
                g_waitingFor = false;
            } else {
                g_waitForNext = now + 250;
            }
        }
        if (g_waitingFor && g_waitForDeadline.Passed()) {
            Log("harness: waitfor %s timed out%s", g_waitForExpr.c_str(), g_waitForDeadline.Why().c_str());
            g_failures.push_back("waitfor " + g_waitForExpr + " timed out" + g_waitForDeadline.Why());
            g_waitingFor = false;
        }
        if (g_waitingFor) {
            if (!g_waitForPending && now >= g_waitForNext) {
                g_waitForFrom = g_console.size();
                g_waitForPending = true;
                ConsoleExecute(("#" + g_waitForExpr).c_str());
            }
            return;
        }
    }
    if (g_capturing) {
        const char* detail = "";
        capture::State state = capture::Poll(&detail);
        if (state == capture::State::Pending && sys::TickCount() < g_captureDeadline) return;
        g_captureAfter.clear();
        g_captureOn.clear();
        if (state == capture::State::Done) {
            Log("harness: capture %s: %s", g_captureName.c_str(), detail);
            g_capturedFrames.push_back(g_captureName + ": " + detail);
        } else {
            std::string why = state == capture::State::Pending ? "not every frame presented in time" : detail;
            Log("harness: capture %s failed: %s", g_captureName.c_str(), why.c_str());
            g_failures.push_back("capture " + g_captureName + ": " + why);
        }
        g_capturing = false;
    }
    if (g_waitingLine) {
        bool found = false;
        for (std::size_t i = g_waitLineFrom; i < g_console.size() && !found; ++i)
            found = g_console[i].find(g_waitLineText) != std::string::npos;
        const char* verb = g_waitLineIsExpect ? "waitexpect" : "waitline";
        if (found) {
            Log("harness: %s '%s' found", verb, g_waitLineText.c_str());
            g_waitingLine = false;
            if (g_waitLineIsExpect) g_expectFrom = g_console.size();
        } else if (g_waitLineDeadline.Passed()) {
            Log("harness: %s '%s' timed out%s", verb, g_waitLineText.c_str(), g_waitLineDeadline.Why().c_str());
            g_failures.push_back((g_waitLineIsExpect ? "expected console output '" + g_waitLineText + "' (waited)"
                                                     : "waitline " + g_waitLineText + " timed out") +
                                 g_waitLineDeadline.Why());
            g_waitingLine = false;
            if (g_waitLineIsExpect) g_expectFrom = g_console.size();
        } else {
            return;
        }
    }
    if (g_waitingModSwitch) {
        if (image::ModSwitchPending()) return;
        Log("harness: mod: the switch made (frame %u)", g_frames);
        g_waitingModSwitch = false;
    }
    if (g_waitingRead) {
        std::string error;
        if (modtrace::Seen(g_waitReadWhere, g_waitReadPath, false, &error)) {
            Log("harness: waitread %s %s: touched", g_waitReadWhere.c_str(), g_waitReadPath.c_str());
            g_waitingRead = false;
        } else if (!error.empty() || static_cast<std::int32_t>(sys::TickCount() - g_waitReadAt) >= 0) {
            Log("harness: waitread %s %s timed out%s", g_waitReadWhere.c_str(), g_waitReadPath.c_str(), error.c_str());
            g_failures.push_back("waitread " + g_waitReadWhere + " " + g_waitReadPath + " timed out" +
                                 (error.empty() ? "" : ": " + error));
            g_waitingRead = false;
        } else {
            return;
        }
    }
    if (g_waitingZone) {
        // Done in a main loop pass only (steps also run in the pump that window switches call while
        // the zone loads): no request pending, not the main menu, the server world holds the zone
        // and the local player is back in the world (state 1; LeaveToZone left it at 3).
        bool ready = false;
        if (g_inputLoop && HarnessFlowState() == 0 && HarnessCampaignMode() != 2 &&
            text::CompareIgnoringCase(ServerZoneName(), g_waitZoneName.c_str()) == 0) {
            void* connection = LocalConnection();
            ready = connection && ConnectionState(connection) == 1;
        }
        if (ready) {
            Log("harness: waitzone %s done after %u ms (campaign mode %d)", g_waitZoneName.c_str(),
                Now() - g_waitZoneDeadline.start, HarnessCampaignMode());
            g_waitingZone = false;
        } else if (g_waitZoneDeadline.Passed()) {
            char what[500];
            std::snprintf(what, sizeof(what), "waitzone %s timed out (zone '%s', request %d, campaign mode %d, %s)%s",
                          g_waitZoneName.c_str(), ServerZoneName(), HarnessFlowState(), HarnessCampaignMode(),
                          DescribeLocalPlayer().c_str(), g_waitZoneDeadline.Why().c_str());
            Log("harness: %s", what);
            g_failures.push_back(what);
            g_waitingZone = false;
        } else {
            return;
        }
    }
    if (g_waitingFrames) {
        if (g_frames < g_waitFramesUntil && static_cast<std::int32_t>(sys::TickCount() - g_waitFramesDeadline.realAt) < 0) return;
        if (g_frames < g_waitFramesUntil) {
            std::string what = "frames: " + std::to_string(g_waitFramesUntil - g_frames) + " frames still to go after " +
                   std::to_string(sys::TickCount() - g_waitFramesDeadline.realStart) + " ms";
            Log("harness: %s", what.c_str());
            g_failures.push_back(what);
        }
        g_waitingFrames = false;
    }
    if (g_waitingPresent) {
        const image::WindowPresents now = HarnessWindowPresents();
        bool presented = now.presents > g_waitPresentFrom;
        bool checked = !g_waitPresentGamma ||
                       (now.rampChecks > g_rampChecksSeen && !now.rampCheckPending && !now.rampIdentity);
        if (presented && checked) {
            Log("harness: waitpresent: %u frames presented on the game window%s", now.presents - g_waitPresentFrom,
                g_waitPresentGamma ? " and the gamma ramp checked" : "");
            if (g_waitPresentGamma) {
                g_rampChecksSeen = now.rampChecks;
                if (now.rampDiffering) {
                    char what[160];
                    std::snprintf(what, sizeof(what), "waitpresent: %d of %d points of the presented frame differ from the gamma ramp",
                                  now.rampDiffering, now.rampPoints);
                    g_failures.push_back(what);
                }
            }
            g_waitingPresent = false;
        } else if (g_waitPresentDeadline.Passed()) {
            char what[200];
            std::snprintf(what, sizeof(what), "waitpresent: %s (%u presented, %d ramp checks, one pending %d, identity %d)",
                          "timed out", now.presents - g_waitPresentFrom,
                          now.rampChecks, now.rampCheckPending, now.rampIdentity);
            Log("harness: %s", what);
            g_failures.push_back(what);
            g_waitingPresent = false;
        } else {
            return;
        }
    }
    if (g_waitingWindow) {
        // Ready on two frames in a row, and done in an input loop pass (input steps follow).
        if (!WindowReady(g_waitWindowName)) g_waitWindowReadyFrom = kNotReady;
        else if (g_waitWindowReadyFrom == kNotReady) g_waitWindowReadyFrom = g_frames;
        if (g_waitWindowReadyFrom != kNotReady && g_frames >= g_waitWindowReadyFrom + 2 && g_inputLoop) {
            bool fadeRunning = false;
            float fadeAlpha = 0.0f;
            const bool fade = HarnessScreenFade(&fadeRunning, &fadeAlpha);
            Log("harness: waitwindow %s done after %u ms (frame %u; screen fade %s, alpha %.0f)", g_waitWindowName.c_str(),
                Now() - g_waitWindowDeadline.start, g_frames, fade && fadeRunning ? "running" : "off", fade ? fadeAlpha : 0.0f);
            g_waitingWindow = false;
        } else if (g_waitWindowDeadline.Passed()) {
            std::string what = "waitwindow " + g_waitWindowName + " timed out (" + DescribeTopWindow() + ")" +
                               g_waitWindowDeadline.Why();
            Log("harness: %s", what.c_str());
            g_failures.push_back(what);
            g_waitingWindow = false;
        } else {
            return;
        }
    }
    if (g_waitingPeer) {
        if (pair::Signaled(g_waitPeerTag)) {
            Log("harness: peerwait %s done after %u ms (frame %u)", g_waitPeerTag.c_str(), Now() - g_waitPeerDeadline.start,
                g_frames);
            g_waitingPeer = false;
        } else if (g_waitPeerDeadline.Passed()) {
            std::string what = "peerwait " + g_waitPeerTag + " timed out" + g_waitPeerDeadline.Why();
            Log("harness: %s", what.c_str());
            g_failures.push_back(what);
            g_waitingPeer = false;
        } else {
            return;
        }
    }
    if (g_waitingDialogue) {
        void* top = TopWindow();
        const int state = top ? image::DialogueState(top) : -1;
        bool done = false;
        if (!g_talking) {
            done = state == g_dialogueState && g_inputLoop;
        } else if (state == 0 || (state < 0 && HarnessCampaignMode() == 1)) {
            // Back in state 0, or the dialogue's end left the base for a battle zone (a cut-scene base's
            // LeaveToZone as its briefing completes: Lost in Astral's cz* bases).
            done = true;
        } else if ((state == 3 || state == 4) && g_inputLoop && g_frames >= g_talkNextFrame) {
            PostMouse("click", 400, 300);
            ++g_talkClicks;
            g_talkNextFrame = g_frames + 2;
        }
        const char* verb = g_talking ? "talk" : "waitdialogue";
        if (done) {
            Log("harness: %s done after %u ms (frame %u, state %d, %d clicks)", verb, Now() - g_dialogueDeadline.start, g_frames,
                state, g_talkClicks);
            g_waitingDialogue = false;
        } else if (g_dialogueDeadline.Passed()) {
            std::string what = std::string(verb) + " timed out (dialogue state " + std::to_string(state) + ", " +
                               std::to_string(g_talkClicks) + " clicks; " + DescribeTopWindow() + ")" + g_dialogueDeadline.Why();
            Log("harness: %s", what.c_str());
            g_failures.push_back(what);
            g_waitingDialogue = false;
        } else {
            return;
        }
    }
    if (g_waitingUnit) {
        // Checked in input loop passes: the pick list is the last frame's, and clickunit follows.
        Point at{};
        void* unit = nullptr;
        std::string why = g_inputLoop ? FindUnitPoint(g_waitUnitName.c_str(), nullptr, &at, &unit) : "not checked";
        if (why.empty()) {
            Log("harness: waitunit %s done after %u ms (at %d,%d)", g_waitUnitName.c_str(), Now() - g_waitUnitDeadline.start,
                at.x, at.y);
            g_waitingUnit = false;
        } else if (g_waitUnitDeadline.Passed()) {
            why += g_waitUnitDeadline.Why();
            Log("harness: waitunit %s timed out: %s", g_waitUnitName.c_str(), why.c_str());
            g_failures.push_back("waitunit " + g_waitUnitName + " timed out: " + why);
            g_waitingUnit = false;
        } else {
            return;
        }
    }
    if (g_worldWait != WorldWait::None) {
        // The server world as the last world update left it (steps run after the frame), so the wait
        // ends in the same frame every run.
        std::string what;
        bool done = false;
        if (g_worldWait == WorldWait::Idle) {
            const int leader = digest::LocalLeader();
            const digest::UnitNow unit = digest::UnitNowOf(leader);
            if (!unit.found || unit.dead || !unit.idle) g_idleFrom = kNotReady;
            else if (g_idleFrom == kNotReady) g_idleFrom = g_frames;
            done = g_idleFrom != kNotReady && g_frames >= g_idleFrom + g_idleFrames;
            what = "waitidle " + std::to_string(g_idleFrames) + " (leader " + std::to_string(leader) +
                   (unit.found ? unit.dead ? ": dead)" : unit.idle ? ": idle)" : ": busy)" : ": not in the world)");
        } else if (g_worldWait == WorldWait::Walk) {
            const int leader = digest::LocalLeader();
            const digest::UnitNow unit = digest::UnitNowOf(leader);
            if (!unit.found || unit.dead || !unit.idle) g_idleFrom = kNotReady;
            else if (g_idleFrom == kNotReady) g_idleFrom = g_frames;
            const float dx = unit.x - g_walkX, dy = unit.y - g_walkY;
            const bool there = dx * dx + dy * dy <= kWalkReach * kWalkReach;
            const bool stood = g_idleFrom != kNotReady && g_frames >= g_idleFrom + g_idleFrames;
            char text[200];
            std::snprintf(text, sizeof(text), "walkto %.1f %.1f (leader %d at %.1f %.1f, %s, %d clicks)", g_walkX, g_walkY,
                          leader, unit.x, unit.y, unit.found ? unit.dead ? "dead" : unit.idle ? "idle" : "busy" : "not in the world",
                          g_walkClicks);
            what = text;
            if (g_walking && stood) {
                done = there;
                g_walking = there;   // stopped short: clicked there again
            }
            if (!g_walking && !done && g_inputLoop && g_frames >= g_walkNextFrame) {
                std::string error = g_walkClicks >= kWalkMostClicks ? what + ": still away after the last click" : WalkInput();
                if (!error.empty()) {
                    Log("harness: walkto: %s", error.c_str());
                    g_failures.push_back("walkto: " + error);
                    g_worldWait = WorldWait::None;
                    return;
                }
            }
        } else if (g_worldWait == WorldWait::Dead) {
            std::string alive;
            for (int id : g_deadIds) {
                const digest::UnitNow unit = digest::UnitNowOf(id);
                if (unit.found && !unit.dead) alive += " " + std::to_string(id);
            }
            done = alive.empty();
            what = "waitdead (alive:" + alive + ")";
        } else {
            unsigned money = 0;
            const digest::UnitNow unit = digest::UnitNowOf(digest::LocalLeader(&money));
            const int queued = image::ServerAwards();
            if (queued > 0) g_awardQueueSeen = true;
            done = g_awardServed ? g_awardQueueSeen && queued == 0
                                 : unit.found && (unit.experience != g_awardExperience || money != g_awardMoney);
            char text[200];
            std::snprintf(text, sizeof(text), "waitaward (experience %.1f -> %.1f, money %u -> %u, server awards %d%s)",
                          g_awardExperience, unit.experience, g_awardMoney, money, queued,
                          g_awardQueueSeen ? ", seen queued" : "");
            what = text;
        }
        if (done) {
            Log("harness: %s done after %u ms (frame %u)", what.c_str(), Now() - g_worldWaitDeadline.start, g_frames);
            g_worldWait = WorldWait::None;
        } else if (g_worldWaitDeadline.Passed()) {
            what += " timed out" + g_worldWaitDeadline.Why();
            Log("harness: %s", what.c_str());
            g_failures.push_back(what);
            g_worldWait = WorldWait::None;
        } else {
            return;
        }
    }
    // A recording that is long enough is cut where the battle screen takes input (harness_record.h, "Parts").
    if (record::CutDue() && WindowReady("battle")) InjectCut();
    while (g_next < g_steps.size()) {
        if (!g_waitUntil.Passed()) return;
        if (std::string why = g_waitUntil.Why(); !why.empty()) {
            g_failures.push_back("wait" + why);
            g_waitUntil = Deadline{};
        }
        Step s = g_steps[g_next++];
        // "vanilla <step>" / "rebirth <step>": only in that install's data (its maps, scripts and resources differ).
        // Rebirth is recognised by its addon.dll or EI_Plugin.asi.
        if (s.verb == "vanilla" || s.verb == "rebirth") {
            const bool runs = (s.verb == "rebirth") == starter::RebirthData();
            if (!runs) {
                Log("harness: skipped (%s only): %s", s.verb.c_str(), s.argument.c_str());
                continue;
            }
            std::size_t space = s.argument.find(' ');
            s.verb = s.argument.substr(0, space);
            s.argument = space == std::string::npos ? "" : s.argument.substr(space + 1);
        }
        // "ifwindow <window> <step>": the step only while that window is on top (a box that closes by itself when a
        // connect is refused at once, as on Linux, is not there to cancel).
        if (s.verb == "ifwindow") {
            const std::size_t space = s.argument.find(' ');
            const std::string name = s.argument.substr(0, space);
            if (space == std::string::npos || !image::IsWindowName(name)) {
                g_failures.push_back("ifwindow: needs a window name and a step (got '" + s.argument + "')");
                continue;
            }
            void* top = TopWindow();
            if (!top || WindowKind(top) != name) {
                Log("harness: skipped (%s is not on top): %s", name.c_str(), s.argument.c_str() + space + 1);
                continue;
            }
            const std::string inner = s.argument.substr(space + 1);
            const std::size_t at = inner.find(' ');
            s.verb = inner.substr(0, at);
            s.argument = at == std::string::npos ? "" : inner.substr(at + 1);
        }
        // "at <frame> <step>": a recorded scenario's line (harness_record.h), run in the pass after that frame; its
        // "event ..." given to the game here.
        if (s.verb == "at") {
            char* rest = nullptr;
            const unsigned long frame = std::strtoul(s.argument.c_str(), &rest, 10);
            if (record::Frame() < frame) {
                --g_next;
                return;
            }
            if (record::Frame() > frame && !g_replayLate) {
                g_replayLate = true;
                const std::string what = "at " + std::to_string(frame) + ": the replay is at frame " + std::to_string(record::Frame()) +
                                         " already (it has parted from the recording)";
                Log("harness: %s", what.c_str());
                g_failures.push_back(what);
            }
            std::string line = rest ? rest : "";
            line.erase(0, line.find_first_not_of(' '));
            const std::size_t space = line.find(' ');
            s.verb = line.substr(0, space);
            s.argument = space == std::string::npos ? "" : line.substr(space + 1);
            if (s.verb == "event") {
                Log("harness: frame %u: at %lu event %s", g_frames, frame, s.argument.c_str());
                if (std::string error = record::Replay(s.argument); !error.empty()) {
                    Log("harness: %s", error.c_str());
                    g_failures.push_back(error);
                }
                continue;
            }
        }
        // "layout <mouse step> <x> <y> ...": the point is in the 800x600 layout, which sits in the window's 4:3 part
        // (W12), so the same step aims at the same interface place in a window of any shape.
        if (s.verb == "layout") {
            const std::size_t space = s.argument.find(' ');
            const std::string verb = s.argument.substr(0, space);
            int x = 0, y = 0, used = 0;
            if (space != std::string::npos && IsInputStep(verb) &&
                std::sscanf(s.argument.c_str() + space + 1, "%d %d%n", &x, &y, &used) >= 2) {
                const Rect client = GameClient();
                LayoutToWindowShare(client.right - client.left, client.bottom - client.top, &x, &y);
                s.argument = std::to_string(x) + " " + std::to_string(y) + s.argument.substr(space + 1 + used);
                s.verb = verb;
            } else {
                g_failures.push_back("layout needs <mouse step> <x> <y>: " + s.argument);
                continue;
            }
        }
        if (!g_inputLoop && IsInputStep(s.verb)) {
            --g_next;   // posted from the next main loop pass instead (see BeforeEvents)
            return;
        }
        // "quit" posts the quit request (platform::PostQuit) from the main loop's pass, where the loop ends on it at
        // once, not from a loading screen's message drain (app::PumpMessages), which leaves it for the main loop.
        // "camera" places the camera between the main loop's camera updates, never inside a loading screen's drain.
        if (!g_winMainLoop && (s.verb == "quit" || s.verb == "camera")) {
            --g_next;
            return;
        }
        Log("harness: frame %u: %s %s", g_frames, s.verb.c_str(), s.argument.c_str());
        clock::RandomNote("step %s %s", s.verb.c_str(), s.argument.c_str());
        if (record::On()) {
            const record::StepKind kind = record::Classify(s.verb);
            if (kind == record::StepKind::Kept) {
                record::Step(s.verb, s.argument);
                if (s.verb == "quit") record::End();   // the quit console command ends the process
            } else if (kind == record::StepKind::Refused) {
                const std::string what = "recordinput: a recording cannot replay the step '" + s.verb + "' (harness_record.h)";
                Log("harness: %s", what.c_str());
                g_failures.push_back(what);
            }
        }
        if (s.verb == "wait") {
            g_waitUntil.Set(std::strtoul(s.argument.c_str(), nullptr, 10));
        } else if (s.verb == "frames") {
            g_waitingFrames = true;
            g_waitFramesUntil = g_frames + std::strtoul(s.argument.c_str(), nullptr, 10);
            g_waitFramesDeadline.Set(0);
            g_waitFramesDeadline.realAt = g_waitFramesDeadline.realStart +
                static_cast<std::uint32_t>(TimeScale() * (10000 + 100 * (g_waitFramesUntil - g_frames)));
            return;
        } else if (s.verb == "waitwindow" || s.verb == "waitmenu") {
            unsigned long seconds = 30;
            std::string name = "menu";
            if (s.verb == "waitmenu") {
                if (!s.argument.empty()) seconds = std::strtoul(s.argument.c_str(), nullptr, 10);
            } else {
                char* rest = nullptr;
                seconds = std::strtoul(s.argument.c_str(), &rest, 10);
                while (rest && *rest == ' ') ++rest;
                name = rest ? rest : "";
            }
            if (!image::IsWindowName(name)) {
                g_failures.push_back("waitwindow: unknown window '" + name + "'");
                continue;
            }
            g_waitingWindow = true;
            g_waitWindowName = name;
            g_waitWindowReadyFrom = kNotReady;
            g_waitWindowDeadline.Set(seconds * 1000);
            return;
        } else if (s.verb == "waitdialogue" || s.verb == "talk") {
            char* rest = nullptr;
            const unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            g_talking = s.verb == "talk";
            g_dialogueState = g_talking ? 0 : std::atoi(rest ? rest : "0");
            g_talkClicks = 0;
            g_talkNextFrame = 0;
            g_waitingDialogue = true;
            g_dialogueDeadline.Set(seconds * 1000);
            return;
        } else if (s.verb == "waitunit") {
            char* rest = nullptr;
            unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            while (rest && *rest == ' ') ++rest;
            g_waitingUnit = true;
            g_waitUnitName = rest ? rest : "";
            g_waitUnitDeadline.Set(seconds * 1000);
            return;
        } else if (s.verb == "waitidle" || s.verb == "walkto" || s.verb == "waitdead" || s.verb == "waitaward") {
            char* rest = nullptr;
            const unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            if (s.verb == "waitidle") {
                g_worldWait = WorldWait::Idle;
                g_idleFrames = 10;
                if (rest && *rest) g_idleFrames = std::strtoul(rest, nullptr, 10);
                g_idleFrom = kNotReady;
            } else if (s.verb == "walkto") {
                unsigned frames = 20;
                if (!rest || std::sscanf(rest, "%f %f %u", &g_walkX, &g_walkY, &frames) < 2) {
                    g_failures.push_back("walkto needs <seconds> <x> <y> [<frames>]");
                    continue;
                }
                if (!(g_minimap.sx > 0.0f && g_minimap.sy > 0.0f)) {
                    g_failures.push_back("walkto: no minimap step before it");
                    continue;
                }
                g_worldWait = WorldWait::Walk;
                g_idleFrames = frames;
                g_idleFrom = kNotReady;
                g_walkClicks = 0;
                g_walkLooks = 0;
                g_walking = false;
                g_walkNextFrame = 0;
            } else if (s.verb == "waitdead") {
                g_worldWait = WorldWait::Dead;
                g_deadIds.clear();
                for (char* at = rest; at && *at;) {
                    char* end = nullptr;
                    const long id = std::strtol(at, &end, 10);
                    if (end == at) break;
                    g_deadIds.push_back(static_cast<int>(id));
                    at = end;
                }
            } else {
                g_worldWait = WorldWait::Award;
                g_awardServed = rest && std::strstr(rest, "served") != nullptr;
                if (!g_awardFromSet) AwardFrom();
                g_awardFromSet = false;
            }
            g_worldWaitDeadline.Set(seconds * 1000);
            return;
        } else if (s.verb == "awardfrom") {
            AwardFrom();
            g_awardFromSet = true;
        } else if (s.verb == "hit") {
            char target[96] = {}, attacker[96] = {};
            float amount = 0.0f;
            std::string error;
            if (std::sscanf(s.argument.c_str(), "%95s %95s %f", target, attacker, &amount) != 3) {
                error = "hit needs <target> <attacker> <amount>";
            } else {
                const int attackerId = HitUnitId(attacker, true);
                const int targetId = std::string(target) == "enemy"
                                         ? image::NearestEnemy(attackerId > 0 ? attackerId : digest::LocalLeader())
                                         : HitUnitId(target, false);
                if (targetId < 0 || attackerId < 0)
                    error = std::string("hit: no unit ") + (targetId < 0 ? target : attacker);
                else
                    error = image::HitUnit(targetId, attackerId, amount);
                Log("harness: hit %s (%d) by %s (%d), %g damage%s%s", target, targetId, attacker, attackerId, amount,
                    error.empty() ? "" : ": ", error.c_str());
            }
            if (!error.empty()) g_failures.push_back(error);
        } else if (s.verb == "giveitem") {
            int player = -1;
            char item[96] = {};
            const std::string error = std::sscanf(s.argument.c_str(), "%d %95s", &player, item) != 2
                                          ? "giveitem needs <player> <item>"
                                          : image::GiveItem(player, item);
            Log("harness: giveitem %s%s%s", s.argument.c_str(), error.empty() ? "" : ": ", error.c_str());
            if (!error.empty()) g_failures.push_back(error);
        } else if (s.verb == "serverscript") {
            const std::string error = image::ServerScript(s.argument);
            Log("harness: serverscript %s%s%s", s.argument.c_str(), error.empty() ? "" : ": ", error.c_str());
            if (!error.empty()) g_failures.push_back(error);
        } else if (s.verb == "console") {
            ConsoleExecute(s.argument.c_str());
        } else if (s.verb == "recordcut") {
            // A driven recording's cut where the scenario says (harness_record.h, "Parts").
            if (!record::On()) {
                g_failures.push_back("recordcut: this run is not a recording (recordinput)");
            } else {
                InjectCut();
            }
        } else if (s.verb == "recordpart") {
            if (std::string error = record::Part(); !error.empty()) g_failures.push_back(error);
        } else if (s.verb == "origin") {
            // The frames of a recorded scenario's "at" lines count from here (a part that begins with a load).
            record::Origin();
            g_cutQueued = false;
            // A save holds no game generator, and the client's kept one has drawn in the frames since the load: the
            // recording and its replay both begin from fresh ones.
            image::ReseedRandom();
        } else if (s.verb == "newgame") {
            // What the main menu's new-game button does after the intro movie (00632940).
            HarnessInterfaceHasScreen() = 0;
            HarnessFlowState() = 3;
        } else if (s.verb == "load") {
            HarnessSetLoadTarget(s.argument.c_str());
            HarnessFlowState() = 2;
        } else if (s.verb == "hold" || s.verb == "release") {
            std::string bad;
            unsigned mods = ParseModifiers(s.argument, &bad);
            if (!bad.empty() || !mods) {
                std::string error = s.verb + ": '" + (bad.empty() ? s.argument : bad) + "' is no modifier (ctrl, shift, alt)";
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            } else {
                PostModifiers(mods, s.verb == "hold");
            }
        } else if (s.verb == "key") {
            HeldModifiers held(&s.argument);
            PostKey(s.argument);
        } else if (s.verb == "keydown" || s.verb == "keyup") {
            PostKey(s.argument, s.verb == "keydown" ? KeyPart::Down : KeyPart::Up);
        } else if (s.verb == "click" || s.verb == "rclick" || s.verb == "move" || s.verb == "dblclick" ||
                   s.verb == "rdblclick" || s.verb == "ldown" || s.verb == "lup" || s.verb == "rdown" || s.verb == "rup") {
            int x = 0, y = 0;
            HeldModifiers held(&s.argument);
            if (std::sscanf(s.argument.c_str(), "%d %d", &x, &y) == 2) PostMouse(s.verb, x, y);
            else Log("harness: %s needs <x> <y>", s.verb.c_str());
        } else if (s.verb == "mousedrag") {
            if (std::sscanf(s.argument.c_str(), "%d %d", &g_dragX, &g_dragY) != 2) g_failures.push_back("mousedrag needs <dx> <dy>");
        } else if (s.verb == "wheel") {
            int x = 0, y = 0, delta = 0;
            HeldModifiers held(&s.argument);
            if (std::sscanf(s.argument.c_str(), "%d %d %d", &x, &y, &delta) == 3) PostWheel(x, y, delta);
            else Log("harness: wheel needs <x> <y> <delta>");
        } else if (s.verb == "clickregion" || s.verb == "rclickregion" || s.verb == "dblclickregion" ||
                   s.verb == "moveregion") {
            HeldModifiers held(&s.argument);
            PostRegionMouse(s.verb, std::atoi(s.argument.c_str()));
        } else if (s.verb == "droplist") {
            const std::string error = DropListStep(s.argument);
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            }
        } else if (s.verb == "hudstate" || s.verb == "campputon" || s.verb == "campstock" || s.verb == "clientbolts" || s.verb == "figurealone" ||
                   s.verb == "clienthurt" || s.verb == "unitpart" ||
                   s.verb == "modfile" || s.verb == "modlist" ||
                   s.verb == "modarchive" || s.verb == "mapfile" || s.verb == "playstream" ||
                   s.verb == "folderprofile" || s.verb == "modtext" || s.verb == "modack" || s.verb == "modweapon" ||
                   s.verb == "modrecord" || s.verb == "ground" || s.verb == "effect") {
            std::string error = s.verb == "hudstate"        ? HudState(s.argument)
                                : s.verb == "effect"        ? image::DataEffectStep(s.argument)
                                : s.verb == "modtext"       ? starter::ModTextStep(s.argument)
                                : s.verb == "modack"        ? starter::ModAckStep(s.argument)
                                : s.verb == "modweapon"     ? starter::ModWeaponStep(s.argument)
                                : s.verb == "modrecord"     ? starter::ModRecordStep(s.argument)
                                : s.verb == "ground"        ? starter::GroundStep(s.argument)
                                : s.verb == "campputon"     ? CampPutOn(s.argument)
                                : s.verb == "campstock"     ? CampStock(s.argument)
                                : s.verb == "clientbolts"   ? ClientBoltsStep(s.argument)
                                : s.verb == "clienthurt"    ? ClientHurtStep(s.argument)
                                : s.verb == "unitpart"      ? UnitPartStep(s.argument)
                                : s.verb == "figurealone"   ? FigureAloneStep(s.argument)
                                : s.verb == "folderprofile" ? folder_profile::Step()
                                : s.verb == "modlist"       ? starter::ModListStep(s.argument)
                                : s.verb == "modarchive"    ? starter::ModArchiveStep(s.argument)
                                : s.verb == "mapfile"       ? starter::MapFileStep(s.argument)
                                : s.verb == "playstream"    ? starter::PlayStreamStep(s.argument)
                                                            : starter::ModFileStep(s.argument);
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            }
        } else if (s.verb == "groundview") {
            LogGroundView();
        } else if (s.verb == "minimap") {
            MinimapLayout m;
            if (std::sscanf(s.argument.c_str(), "%f %f %f %f", &m.cx, &m.cy, &m.sx, &m.sy) != 4) {
                g_failures.push_back("minimap needs <cx> <cy> <sx> <sy>");
                Log("harness: %s", g_failures.back().c_str());
            } else {
                g_minimap = m;
            }
        } else if (s.verb == "camera") {
            const std::string error = PlaceCamera(s.argument);
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            }
        } else if (s.verb == "camrange") {
            char what[16] = {};
            float least = 0.0f, most = 0.0f;
            const image::CameraView view = image::ViewCamera();
            Log("harness: camrange: focus %.2f %.2f %.2f, heading %.1f, distance %.2f, pitch %.1f; eye %.2f %.2f %.2f, "
                "near plane %.3f above the ground", view.focusX, view.focusY, view.focusZ, view.heading, view.distance,
                view.pitch, view.x, view.y, view.z, view.nearGround);
            const bool read = std::sscanf(s.argument.c_str(), "%15s %f %f", what, &least, &most) == 3;
            const std::string_view kind = what;
            const float* value = kind == "pitch"    ? &view.pitch
                                 : kind == "distance" ? &view.distance
                                 : kind == "x"        ? &view.focusX
                                 : kind == "y"        ? &view.focusY
                                 : kind == "ground"   ? &view.nearGround
                                                      : nullptr;
            if (!read || !value) {
                g_failures.push_back("camrange needs pitch|distance|x|y|ground <least> <most>");
                Log("harness: %s", g_failures.back().c_str());
            } else if (!(*value >= least && *value <= most)) {
                char text[128];
                std::snprintf(text, sizeof(text), "camrange: %s %.2f outside %.2f .. %.2f", what, *value, least, most);
                g_failures.push_back(text);
                Log("harness: %s", text);
            }
        } else if (s.verb == "climate") {
            int climate = 0;
            if (std::sscanf(s.argument.c_str(), "%d", &climate) == 1 && climate >= 0 && climate <= 3) {
                image::SetClimate(climate);
            } else {
                g_failures.push_back("climate needs 0, 1, 2 or 3");
                Log("harness: %s", g_failures.back().c_str());
            }
        } else if (s.verb == "weatherstate") {
            int expected = 0, leastDensity = 0;
            if (std::sscanf(s.argument.c_str(), "%d %d", &expected, &leastDensity) < 1) {
                g_failures.push_back("weatherstate needs the weather");
                Log("harness: %s", g_failures.back().c_str());
            } else {
                const image::WeatherState w = image::Weather();
                char text[240];
                std::snprintf(text, sizeof(text),
                              "weatherstate: weather %d (want %d), fade %.0f, sight factor %.2f; the layer's mode %d, "
                              "heading for %d, density %d (at least %d)",
                              w.weather, expected, static_cast<double>(w.strength), static_cast<double>(w.factor),
                              w.layerMode, w.layerTarget, w.layerDensity, leastDensity);
                Log("harness: %s", text);
                if (w.weather != expected || w.layerTarget != expected || w.layerDensity < leastDensity)
                    g_failures.push_back(text);
            }
        } else if (s.verb == "lookat") {
            std::string error = LookAt(s.argument);
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            }
        } else if (s.verb == "clickunit" || s.verb == "clickground") {
            HeldModifiers held(&s.argument);
            std::string error = s.verb == "clickunit" ? PostUnitMouse(s.argument) : PostGroundMouse(s.argument);
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            }
        } else if (s.verb == "screen") {
            // What the global map's buttons do (0062F950): ask the server for interface screen <id>.
            // For the camp (5) the map's button first sets "constr_current" to 0; a base dialogue's
            // "constrN" answer (006064E0) sets N instead, when the class template exists.
            int id = 0, constr = 0;
            std::sscanf(s.argument.c_str(), "%d %d", &id, &constr);
            if (id == 5 && constr > 0 && !image::HasClassTemplate(constr)) {
                std::string error = "screen: no class template " + std::to_string(constr);
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            } else {
                if (id == 5) SetCurrentConstructor(static_cast<float>(constr));
                RequestScreen(id);
            }
        } else if (s.verb == "chat") {
            // The chat line (the interface's root, ChatOverlay) opened as a multiplayer client's Enter
            // opens it (SetOpen, which draws it), or closed.
            if (!image::HasChatOverlay()) {
                g_failures.push_back("chat: the interface has no chat overlay");
            } else if (std::atoi(s.argument.c_str()) != 0) {
                image::ChatKeyEnter();
            } else {
                image::CloseChat();
            }
        } else if (s.verb == "typeunicode") {
            if (std::string failure = utf8typing::Type(GameWindow(), s.argument); !failure.empty())
                g_failures.push_back(failure);
        } else if (s.verb == "consoleline") {
            if (std::string failure = utf8typing::CheckConsoleLine(s.argument); !failure.empty())
                g_failures.push_back(failure);
        } else if (s.verb == "utf8text") {
            if (std::string failure = utf8text::Check(); !failure.empty()) g_failures.push_back(failure);
        } else if (s.verb == "chatwrap") {
            if (std::string failure = utf8text::CheckChatWrap(s.argument); !failure.empty())
                g_failures.push_back(failure);
        } else if (s.verb == "utf8files") {
            if (std::string failure = utf8files::Check(g_savesDir); !failure.empty()) g_failures.push_back(failure);
        } else if (s.verb == "peersignal") {
            if (!pair::Active()) g_failures.push_back("peersignal: not a pair's game (a \"peer\" line starts the other)");
            pair::Signal(s.argument);
        } else if (s.verb == "peerwait") {
            char* rest = nullptr;
            unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            while (rest && *rest == ' ') ++rest;
            if (!pair::Active()) {
                g_failures.push_back("peerwait: not a pair's game (a \"peer\" line starts the other)");
                continue;
            }
            g_waitingPeer = true;
            g_waitPeerTag = rest ? rest : "";
            g_waitPeerDeadline.Set(seconds * 1000);
            return;
        } else if (s.verb == "netcut") {
            if (!pair::Active()) g_failures.push_back("netcut: not a pair's game (a \"peer\" line starts the other)");
            pair::Cut();
        } else if (s.verb == "hidefile") {
            if (g_hiddenCount >= kHiddenFiles || s.argument.empty() || s.argument.size() >= kPath) {
                g_failures.push_back("hidefile: no room for " + s.argument);
            } else {
                sys::CopyText(g_hiddenFiles[g_hiddenCount], s.argument.c_str());
                for (char* p = g_hiddenFiles[g_hiddenCount]; *p; ++p)
                    if (*p == '/') *p = '\\';
                ++g_hiddenCount;
                Log("harness: %s reads as missing until this game writes it", s.argument.c_str());
            }
        } else if (s.verb == "samefile") {
            std::string error = SameFileStep(s.argument);
            if (!error.empty()) g_failures.push_back(error);
        } else if (s.verb == "masterhost") {
            HarnessSetMasterHost(s.argument.c_str());
        } else if (s.verb == "shadows") {
            if (s.argument == "0" || s.argument == "1") HarnessSetShadows(s.argument == "1");
            else g_failures.push_back("shadows needs 0 or 1");
        } else if (s.verb == "fakemaster") {
            int port = 0;
            char reply[16] = "list";
            std::sscanf(s.argument.c_str(), "%d %15s", &port, reply);
            if (!pair::Active()) g_failures.push_back("fakemaster: not a pair's game (a \"peer\" line starts the other)");
            else if (port <= 0 || port > 0xFFFF) g_failures.push_back("fakemaster needs <port>");
            else if (!pair::FakeMaster(static_cast<std::uint16_t>(port), reply))
                g_failures.push_back("fakemaster: the reply is list, badsize or short");
        } else if (s.verb == "netstate" || s.verb == "netmod" || s.verb == "netlist") {
            for (std::string& line : s.verb == "netstate" ? pair::NetState()
                                     : s.verb == "netmod" ? image::NetMod()
                                                          : image::NetList()) {
                Log("harness: %s", line.c_str());
                g_console.push_back(std::move(line));
            }
        } else if (s.verb == "originalnet") {
            image::OriginalNet();
        } else if (s.verb == "nolan" || s.verb == "badupdate") {
            if (!pair::Active()) g_failures.push_back(s.verb + ": not a pair's game (a \"peer\" line starts the other)");
            else if (s.verb == "nolan") pair::NoLan();
            else pair::BadUpdate();
        } else if (s.verb == "zone") {
            // What a zone exit does (00670C20): the server's LeaveToZone for the local player.
            char name[64] = "";
            int exitIndex = 0;
            if (std::sscanf(s.argument.c_str(), "%63s %d", name, &exitIndex) < 1) {
                g_failures.push_back("zone needs <name> [exit]");
                continue;
            }
            // MapDatabase::FindMap on the map database.
            const auto* record = static_cast<const std::uint8_t*>(image::FindMap(name));
            void* connection = LocalConnection();
            Log("harness: zone %s: %s", name, DescribeLocalPlayer().c_str());
            if (!record) {
                // StartZone would log a warning and go to the main menu.
                g_failures.push_back(std::string("zone: no zone '") + name + "' in map.txt");
            } else if (!connection || ConnectionState(connection) != 1) {
                g_failures.push_back(std::string("zone ") + name + ": the local player is not in the world (" + DescribeLocalPlayer() + ")");
            } else {
                int kind = image::MapKind(record);
                Log("harness: zone %s (map.txt type %s), exit %d", name, kind == 0 ? "game" : kind == 2 ? "brief" : "edge", exitIndex);
                image::LeaveToZone(connection, name, exitIndex);

                if (ConnectionState(connection) != 3)
                    g_failures.push_back(std::string("zone ") + name + ": LeaveToZone did not leave (the leader dead or missing?; " +
                                         DescribeLocalPlayer() + ")");
            }
        } else if (s.verb == "waitzone") {
            char* rest = nullptr;
            unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            while (rest && *rest == ' ') ++rest;
            g_waitingZone = true;
            g_waitZoneName = rest ? rest : "";
            g_waitZoneDeadline.Set(seconds * 1000);
            return;
        } else if (s.verb == "check") {
            g_checking = true;
            g_checkExpr = s.argument;
            g_checkFrom = g_console.size();
            g_checkDeadline.Set(3000);
            ConsoleExecute(("#" + s.argument).c_str());
            return;
        } else if (s.verb == "frametime") {
            if (!clock::Virtual()) g_failures.push_back("frametime needs the virtual clock (deterministic)");
            else clock::SetFrameTime(std::strtoul(s.argument.c_str(), nullptr, 10));
        } else if (camera_trace::UsedBy(s.verb) || camera_trace::ObjectsUsedBy(s.verb)) {
            camera_trace::Step(s.verb, s.argument, &g_failures, &g_capturedFrames);
        } else if (s.verb == "checktick") {
            const unsigned want = std::strtoul(s.argument.c_str(), nullptr, 10);
            Log("harness: checktick: the update period is %u ms (wanted %u)", TickLength(), want);
            if (TickLength() != want)
                g_failures.push_back("checktick: the update period is " + std::to_string(TickLength()) + " ms, not " +
                                     std::to_string(want));
        } else if (s.verb == "graphthreads") {
            std::vector<std::string> zones;
            for (std::size_t at = 0; at < s.argument.size();) {
                const std::size_t space = std::min(s.argument.find(' ', at), s.argument.size());
                if (space > at) zones.push_back(s.argument.substr(at, space - at));
                at = space + 1;
            }
            for (std::string& line : image::GraphThreads(zones, &g_failures)) g_console.push_back(std::move(line));
        } else if (s.verb == "checklimit") {
            const int want = std::atoi(s.argument.c_str());
            const int limit = image::SessionPlayerLimit();
            Log("harness: checklimit: the session's player limit is %d (wanted %d)", limit, want);
            if (limit != want)
                g_failures.push_back("checklimit: the session's player limit is " + std::to_string(limit) + ", not " +
                                     std::to_string(want));
        } else if (s.verb == "companions") {
            const int want = std::atoi(s.argument.c_str());
            const int records = image::ServerPartyRecords(0);
            Log("harness: companions: server player 0's party has %d records (wanted %d)", records, want);
            if (records != want)
                g_failures.push_back("companions: server player 0's party has " + std::to_string(records) +
                                     " records, not " + std::to_string(want));
        } else if (s.verb == "companionlist") {
            const int want = std::atoi(s.argument.c_str());
            const int picked = image::CompanionList();
            Log("harness: companionlist: %d companions picked (wanted %d)", picked, want);
            if (picked != want)
                g_failures.push_back("companionlist: " + std::to_string(picked) + " companions picked, not " +
                                     std::to_string(want));
        } else if (s.verb == "askcompanion") {
            if (!image::AskCompanion(s.argument.c_str()))
                g_failures.push_back("askcompanion " + s.argument + ": nothing to send");
        } else if (s.verb == "heroxp") {
            char name[64] = {}, more[16] = {};
            std::sscanf(s.argument.c_str(), "%63s %15s", name, more);
            std::string file;
            float experience = 0.0f;
            if (!image::SavedHero(name, &file, &experience)) {
                g_failures.push_back(std::string("heroxp: no saved hero ") + name + " reads");
            } else {
                Log("harness: heroxp: %s (%s) has %.3f experience", name, file.c_str(), experience);
                auto previous = g_heroExperience.find(name);
                if (std::string(more) == "more" && (previous == g_heroExperience.end() || !(experience > previous->second)))
                    g_failures.push_back(std::string("heroxp: ") + name + " did not gain experience");
                g_heroExperience[name] = experience;
            }
        } else if (s.verb == "checkpaused") {
            const bool want = std::atoi(s.argument.c_str()) != 0;
            Log("harness: checkpaused: the server world is %s (wanted %s)", image::ServerPaused() ? "paused" : "running",
                want ? "paused" : "running");
            if (image::ServerPaused() != want)
                g_failures.push_back(std::string("checkpaused: the server world is ") +
                                     (image::ServerPaused() ? "paused" : "running"));
        } else if (s.verb == "speed") {
            g_heldTickLength = std::strtoul(s.argument.c_str(), nullptr, 10);
            if (g_heldTickLength) TickLength() = g_heldTickLength;
        } else if (s.verb == "mod") {
            // A switch of mods in the main menu (app::RequestModSwitch); the next step runs once it is made.
            const std::string folder = starter::ModStepPath(s.argument);
            if (!image::SwitchMod(folder.c_str())) {
                g_failures.push_back("mod " + s.argument + ": the switch refused (not in the main menu?)");
                continue;
            }
            Log("harness: mod: a switch to \"%s\" asked for", folder.c_str());
            modtrace::SetRoots(folder, starter::GameFolder(), g_savesDir);
            g_waitingModSwitch = true;
            return;
        } else if (s.verb == "read" || s.verb == "wrote") {
            const std::string error = modtrace::ReadStep(s.verb, s.argument);
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            }
        } else if (s.verb == "waitread") {
            unsigned long seconds = 0;
            if (!modtrace::ParseWaitRead(s.argument, &seconds, &g_waitReadWhere, &g_waitReadPath)) {
                g_failures.push_back("waitread needs <seconds> <mod|game> <path>");
                continue;
            }
            g_waitingRead = true;
            g_waitReadAt = sys::TickCount() + static_cast<std::uint32_t>(seconds * 1000);
            return;
        } else if (s.verb == "waitline" || s.verb == "waitexpect") {
            char* rest = nullptr;
            unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            while (rest && *rest == ' ') ++rest;
            g_waitingLine = true;
            g_waitLineIsExpect = s.verb == "waitexpect";
            g_waitLineFrom = g_waitLineIsExpect ? g_expectFrom : 0;
            g_waitLineText = rest ? rest : "";
            g_waitLineDeadline.Set(seconds * 1000);
            return;
        } else if (s.verb == "waitfor") {
            char* rest = nullptr;
            unsigned long seconds = std::strtoul(s.argument.c_str(), &rest, 10);
            while (rest && *rest == ' ') ++rest;
            g_waitingFor = true;
            g_waitForPending = false;
            g_waitForExpr = rest ? rest : "";
            g_waitForDeadline.Set(seconds * 1000);
            g_waitForNext = g_waitForDeadline.start;
            return;
        } else if (s.verb == "expect") {
            bool found = false;
            for (std::size_t i = g_expectFrom; i < g_console.size() && !found; ++i)
                found = g_console[i].find(s.argument) != std::string::npos;
            Log("harness: expect '%s': %s", s.argument.c_str(), found ? "found" : "MISSING");
            if (!found) g_failures.push_back("expected console output '" + s.argument + "'");
            g_expectFrom = g_console.size();
        } else if (s.verb == "expectlog") {
            const std::string text = LogText();
            const std::string& wanted = s.argument;
            const bool found = text.find(wanted) != std::string::npos;
            Log("harness: expectlog '%s': %s", wanted.c_str(), found ? "found" : "MISSING");
            if (!found) g_failures.push_back("expected ei-rebuild.log line '" + wanted + "'");
        } else if (s.verb == "logmark") {
            g_logMark = LogText().size();
        } else if (s.verb == "expectnewlog" || s.verb == "expectnonewlog") {
            const std::string text = LogText();
            const std::string& wanted = s.argument;
            const bool found = text.find(wanted, g_logMark) != std::string::npos;
            const bool wantFound = s.verb == "expectnewlog";
            g_logMark = text.size();
            Log("harness: %s '%s': %s", s.verb.c_str(), wanted.c_str(), found == wantFound ? "ok" : "WRONG");
            if (found != wantFound)
                g_failures.push_back(std::string(wantFound ? "expected" : "unexpected") + " new ei-rebuild.log line '" + wanted + "'");
        } else if (s.verb == "capture") {
            // <name> [<count> [freeze] [after <text> | on <window>]]
            const std::size_t space = s.argument.find(' ');
            const std::string name = s.argument.substr(0, space);
            const std::string rest = space == std::string::npos ? std::string() : s.argument.substr(space + 1);
            int count = 0;
            int used = 0;
            std::string after, on;
            bool freeze = false;
            if (!rest.empty()) {
                if (std::sscanf(rest.c_str(), "%d%n", &count, &used) < 1 || count < 1 || count > 200) count = -1;
                std::string options = count > 0 ? rest.substr(used) : std::string();
                while (!options.empty() && options.front() == ' ') options.erase(0, 1);
                if (options.compare(0, 6, "freeze") == 0) {
                    freeze = true;
                    options.erase(0, 6);
                    while (!options.empty() && options.front() == ' ') options.erase(0, 1);
                }
                if (options.compare(0, 6, "after ") == 0) after = options.substr(6);
                else if (options.compare(0, 3, "on ") == 0) on = options.substr(3);
                else if (!options.empty()) count = -1;
            }
            // The name becomes a file name: letters, digits, '_', '-' and '.' only.
            bool valid = !name.empty() && name.size() < 56 && count >= 0;
            for (char c : name)
                valid = valid && (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.');
            if (!valid) {
                g_failures.push_back("capture: bad step '" + s.argument + "'");
                continue;
            }
            if (count > 0) {
                g_captureAfter = after;
                g_captureAfterSeen = false;
                g_captureOn = on;
                capture::RequestBurst(name.c_str(), count, after.empty() && on.empty() ? nullptr : &CaptureStarts,
                                      freeze);
            } else {
                // Two frames go by first, so that what changed before (a freeze, the mouse) is drawn.
                capture::Request(name.c_str(), 2);
            }
            g_capturing = true;
            g_captureName = name;
            g_captureDeadline = sys::TickCount() + (count > 0 ? 30000 : 10000);
            return;
        } else if (s.verb == "samecapture") {
            // samecapture <a> <b> [tolerance [fraction [left top right bottom]]]: the rectangle as diffcapture's.
            char a[64] = "", b[64] = "";
            int tolerance = 8;
            double limit = 0.001;
            int rect[4] = {};
            const int read = std::sscanf(s.argument.c_str(), "%63s %63s %d %lf %d %d %d %d", a, b, &tolerance, &limit,
                                         &rect[0], &rect[1], &rect[2], &rect[3]);
            if (read < 2 || (read > 4 && read < 8)) {
                g_failures.push_back("samecapture needs two frame names (and a rectangle's four sides)");
                continue;
            }
            double differing = 0;
            int largest = 0;
            const char* why = "";
            if (!(read == 8 ? capture::CompareRect(a, b, tolerance, rect[0], rect[1], rect[2], rect[3], &differing,
                                                   &largest, &why)
                            : capture::Compare(a, b, tolerance, &differing, &largest, &why))) {
                g_failures.push_back(std::string("samecapture ") + a + " " + b + ": " + why);
                continue;
            }
            char text[160];
            std::snprintf(text, sizeof(text), "samecapture %s %s: %.3f%% of the pixels differ by more than %d (largest %d)", a,
                          b, 100.0 * differing, tolerance, largest);
            Log("harness: %s", text);
            g_capturedFrames.push_back(text);
            if (differing > limit) g_failures.push_back(text);
        } else if (s.verb == "diffcapture") {
            // Two captures that must differ in a share of their pixels from `least` to `most` (R11d).
            char a[64] = "", b[64] = "";
            double least = 0, most = 1;
            int tolerance = 8;
            int rect[4] = {};
            const int read = std::sscanf(s.argument.c_str(), "%63s %63s %lf %lf %d %d %d %d %d", a, b, &least, &most,
                                         &tolerance, &rect[0], &rect[1], &rect[2], &rect[3]);
            if (read < 4 || (read > 5 && read < 9)) {
                g_failures.push_back("diffcapture needs two frame names, a least and a most fraction");
                continue;
            }
            double differing = 0;
            int largest = 0;
            const char* why = "";
            if (!(read == 9 ? capture::CompareRect(a, b, tolerance, rect[0], rect[1], rect[2], rect[3], &differing,
                                                   &largest, &why)
                            : capture::Compare(a, b, tolerance, &differing, &largest, &why))) {
                g_failures.push_back(std::string("diffcapture ") + a + " " + b + ": " + why);
                continue;
            }
            char text[200];
            std::snprintf(text, sizeof(text),
                          "diffcapture %s %s: %.4f%% of the pixels differ by more than %d (largest %d; allowed %.4f%% .. %.4f%%)",
                          a, b, 100.0 * differing, tolerance, largest, 100.0 * least, 100.0 * most);
            Log("harness: %s", text);
            g_capturedFrames.push_back(text);
            if (differing < least || differing > most) g_failures.push_back(text);
        } else if (s.verb == "printlight") {
            // How bright what one capture draws over another is against what it covers (R52a).
            char with[64] = "", without[64] = "";
            double least = 0, most = 0;
            if (std::sscanf(s.argument.c_str(), "%63s %63s %lf %lf", with, without, &least, &most) < 4) {
                g_failures.push_back("printlight needs two frame names, a least and a most ratio");
                continue;
            }
            double over[3] = {}, under[3] = {}, share = 0;
            const char* why = "";
            if (!capture::DrawnOver(with, without, 8, over, under, &share, &why)) {
                g_failures.push_back(std::string("printlight ") + with + " " + without + ": " + why);
                continue;
            }
            const double ratio = (over[0] + over[1] + over[2]) / (std::max)(under[0] + under[1] + under[2], 1.0);
            char text[240];
            std::snprintf(text, sizeof(text),
                          "printlight %s %s: %.3f%% of the pixels, rgb %.0f %.0f %.0f over %.0f %.0f %.0f: ratio %.3f "
                          "(allowed %.3f .. %.3f)",
                          with, without, 100.0 * share, over[2], over[1], over[0], under[2], under[1], under[0], ratio,
                          least, most);
            Log("harness: %s", text);
            g_capturedFrames.push_back(text);
            if (ratio < least || ratio > most) g_failures.push_back(text);
        } else if (s.verb == "blink") {
            std::string error = BlinkStep(s.argument);
            if (!error.empty()) g_failures.push_back(error);
        } else if (s.verb == "skin" || s.verb == "unitcolour" || s.verb == "unitskins" || s.verb == "groundprints") {
            std::string error = s.verb == "skin"           ? SkinStep(s.argument)
                                : s.verb == "groundprints" ? GroundPrintsStep(s.argument)
                                : s.verb == "unitskins" ? UnitSkinsStep(s.argument)
                                                        : UnitColourStep(s.argument);
            if (!error.empty()) g_failures.push_back(error);
        } else if (IsRenderStep(s.verb)) {
            // The image's renderer (image/harness_render_image.cpp).
            image::RenderStep(s.verb, s.argument, &g_failures, &g_capturedFrames);
        } else if (s.verb == "checkplayers") {
            std::string why;
            if (!address_order::CheckPlayerList(&why)) g_failures.push_back("checkplayers: " + why);
        } else if (s.verb == "checkshot") {
            int index = -1, tolerance = 8;
            char frame[64] = "";
            double limit = 0.001;
            // "recorder" first: the frame recorder's file (the movie folder ".\capture\"), not Scroll Lock's.
            bool recorder = s.argument.compare(0, 9, "recorder ") == 0;
            if (std::sscanf(s.argument.c_str() + (recorder ? 9 : 0), "%d %63s %d %lf", &index, frame,
                         &tolerance, &limit) < 2 ||
                index < 0) {
                g_failures.push_back("checkshot needs [recorder] <n> <frame>");
                continue;
            }
            const std::string path = str::Format("%s\\%sshot%.4d.bmp", g_savesDir, recorder ? "capture\\" : "", index);
            double differing = 0;
            int largest = 0;
            std::string how;
            const char* why = "";
            if (!capture::CompareFile(path.c_str(), frame, tolerance, &differing, &largest, &how, &why)) {
                g_failures.push_back(std::string("checkshot ") + std::to_string(index) + " " + frame + ": " + why);
                Log("harness: checkshot %d %s: %s (%s)", index, frame, why, path.c_str());
                continue;
            }
            char text[240];
            std::snprintf(text, sizeof(text), "checkshot %s%d %s: %sshot%.4d.bmp %s; %.3f%% of the pixels differ by more than %d (largest %d)",
                          recorder ? "recorder " : "", index, frame, recorder ? "capture\\" : "", index, how.c_str(),
                          100.0 * differing, tolerance, largest);
            Log("harness: %s", text);
            g_capturedFrames.push_back(text);
            if (differing > limit) g_failures.push_back(text);
        } else if (s.verb == "checksave") {
            int tolerance = 8;
            char slot[64] = "", frame[64] = "";
            double limit = 0.001;
            int used = 0;
            if (std::sscanf(s.argument.c_str(), "%63s %63s%n", slot, frame,
                         &used) < 2) {
                g_failures.push_back("checksave needs <slot> <frame>");
                continue;
            }
            const char* rest = s.argument.c_str() + used;
            while (*rest == ' ') ++rest;
            bool centre = std::strncmp(rest, "centre", 6) == 0;
            if (centre) rest += 6;
            std::sscanf(rest, "%d %lf", &tolerance, &limit);
            const std::string path = str::Format("%s\\%s\\shot.sav", g_savesDir, slot);
            double differing = 0;
            int largest = 0;
            const char* why = "";
            if (!capture::CompareSavePicture(path.c_str(), frame, centre, tolerance, &differing, &largest, &why)) {
                g_failures.push_back(std::string("checksave ") + slot + " " + frame + ": " + why);
                Log("harness: checksave %s %s: %s (%s)", slot, frame, why, path.c_str());
                continue;
            }
            char text[200];
            std::snprintf(text, sizeof(text), "checksave %s %s: %.3f%% of the pixels differ by more than %d (largest %d)", slot,
                          frame, 100.0 * differing, tolerance, largest);
            Log("harness: %s", text);
            g_capturedFrames.push_back(text);
            if (differing > limit) g_failures.push_back(text);
        } else if (s.verb == "oldshot") {
            const std::string path = str::Format("%s\\%s\\shot.sav", g_savesDir, s.argument.c_str());
            if (!capture::CutSavePicture(path.c_str())) g_failures.push_back("oldshot " + s.argument + ": no picture to cut");
        } else if (s.verb == "record") {
            HarnessRecordingFrames() = s.argument == "on" ? 1 : 0;
        } else if (s.verb == "waitpresent") {
            int seconds = 10;
            char what[16] = "";
            std::sscanf(s.argument.c_str(), "%d %15s", &seconds, what);
            g_waitPresentFrom = HarnessWindowPresents().presents;
            g_waitPresentGamma = std::strcmp(what, "gamma") == 0;
            g_waitPresentDeadline.Set(static_cast<std::uint32_t>(seconds) * 1000);
            g_waitingPresent = true;
            return;
        } else if (s.verb == "cursormark") {
            g_cursorMark = image::CursorPresentCounts();
            g_cursorCollecting = true;
            harnessplatform::ForgetSeenCursors();
            Log("harness: cursormark");
        } else if (s.verb == "cursorcheck") {
            std::string error = CursorCheck(s.argument);
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            }
        } else if (s.verb == "windowtitle") {
            std::string error = WindowTitleStep();
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            }
        } else if (s.verb == "realinput") {
            if (std::string error = RealInputStep(s.argument); !error.empty()) g_failures.push_back(error);
        } else if (s.verb == "focus" || s.verb == "windowsize") {
            std::string error = s.verb == "focus" ? FocusStep(s.argument) : WindowSizeStep(s.argument);
            if (!error.empty()) g_failures.push_back(error);
        } else if (s.verb == "hang") {
            g_stackDumpHangs = s.argument == "dump" || s.argument == "all";
            g_watchdogStopped = s.argument == "all";
            Log("harness: hang %s (a test of the time limit): the main thread blocks for good", s.argument.c_str());
            sys::SleepForever();
        } else if (s.verb == "freeze" || s.verb == "unfreeze") {
            clock::Freeze(s.verb == "freeze");
        } else if (s.verb == "digest" || s.verb == "dump") {
            std::string error = s.verb == "digest" ? digest::Step(s.argument) : digest::Dump(s.argument);
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            }
        } else if (s.verb == "reject") {
            g_rejected.push_back(s.argument);
        } else if (s.verb == "allow") {
            g_allowed.push_back(s.argument);
        } else if (s.verb == "diskfull") {
            crash::FailWritesTo(s.argument.c_str());
        } else if (s.verb == "expectcrash") {
            g_expectedCrash = s.argument;
        } else if (s.verb == "report") {
            WriteReport();
        } else if (s.verb == "quit") {
            if (!g_expectedCrash.empty()) g_failures.push_back("the game did not crash (expectcrash " + g_expectedCrash + ")");
            WriteReport();
            // The game can hang on the way out (Rebirth did: harness_starter.cpp). The quit console command calls exit(),
            // which stops our watchdog thread too, so run_all ends the process when it sees
            // harness_quit and the game is still running 3 s later.
            if (FILE* f = sys::OpenFile(ei::OutputPath("harness_quit"), "w")) std::fclose(f);
            g_quitting = true;
            ConsoleExecute("quit");
            return;
        } else {
            Log("harness: unknown step '%s'", s.verb.c_str());
        }
    }
}

// Watchdog thread: makes sure every harness run ends without Task Manager. After "quit" the game
// gets 5 s to exit by itself. A scenario that has not
// finished within its time limit (60 s from game start unless it says "timeout <seconds>") fails:
// the report is written with the failure and the game is closed. Keep scenarios well inside it.
constexpr std::uint32_t kDefaultTimeLimitMs = 60000;
std::uint32_t g_budget = kDefaultTimeLimitMs;   // before TimeScale()
std::uint32_t Budget() { return static_cast<std::uint32_t>(g_budget * TimeScale()); }

// A timed-out run: where the main thread is (sys::LogMainThreadStack, ei-rebuild.log). The stack dump runs on a thread
// of its own and the watchdog waits 2 s for it: suspending the main thread and reading its context waits until the
// thread is actually suspended, which a thread stuck in a driver call (an audio device's open under load) never is.
// The watchdog itself must reach the process's end whatever the main thread is doing (SV1d).
void LogMainThreadStackBounded() {
    const bool done = sys::RunBounded([] {
        if (g_stackDumpHangs) sys::SleepForever();
        sys::LogMainThreadStack();
    }, 2000);
    if (!done) Log("harness: the main thread's stack could not be read in 2 s (it cannot be suspended)");
}

void Watchdog() {
    const std::uint32_t start = sys::TickCount();
    for (unsigned beat = 1;; ++beat) {
        sys::SleepMs(250);
        if (g_watchdogStopped) sys::SleepForever();
        if (beat % 4 == 0) netlocal::Scan();   // binds outside loopback, whoever made them (harness_net.h)
        if (g_quitting) {
            sys::SleepMs(5000);
            Log("harness: the game did not exit 5 s after quit; terminating it");
            sys::Terminate(0);
        }
        if (sys::TickCount() - start >= Budget()) {
            Log("harness: scenario timed out after %u s (step %u of %u); terminating the game",
                (sys::TickCount() - start) / 1000, unsigned(g_next), unsigned(g_steps.size()));
            char what[160];
            std::snprintf(what, sizeof(what), "scenario exceeded its %u s time limit at step %u of %u",
                          Budget() / 1000, unsigned(g_next), unsigned(g_steps.size()));
            g_failures.push_back(what);
            LogMainThreadStackBounded();
            // Write the report from another thread and end the game whether or not that finishes:
            // a crashed or deadlocked main thread may hold a lock the report needs.
            sys::RunBounded([] { WriteReport(); }, 3000);
            sys::Terminate(1);
        }
    }
}

// The game pauses itself when its window loses focus (it stops the cursor, the mouse and the sound). Parallel harness
// runs leave only one game in front, so the harness keeps every game active: its platform backend tells the game no
// activation change of the system's (platform/harness_platform.h); the focus steps tell it their own.
// The window is made without activation and shown without it (the backend's CreateGameWindow: a window that is never
// activated, out of sight), so the system never tells the game it was activated, which sets the game's active flag
// 0079BC5C and starts its frames. Once the device object exists and the game is not active, the harness sends the
// activation a foreground start would have given (tickets R9a2, SV1c).
void ActivateWithoutForeground() {
    static bool sent = false;
    if (sent || !HarnessSceneDevice()) return;
    sent = true;
    if (HarnessActive()) return;
    Log("harness: the game window activated (nothing activates it: harness games never take the foreground)");
    harnessplatform::Activate(true);
}

// focus off: what the system tells the game when another window takes the focus (deactivated; the platform's
// backend keeps the system's own changes from a harness's game): the game deactivates, and the step minimises the
// window as the user's switch would (activating nothing). focus on: the window restored (not activated) and the
// activation the user's return gives. Ticket R9c.
std::string FocusStep(const std::string& argument) {
    if (!GameWindow()) return "focus: no game window yet";
    if (argument == "off") {
        Log("harness: focus off (deactivated, the window minimised)");
        harnessplatform::Activate(false);
        harnessplatform::ShowMinimized(true);
        return "";
    }
    if (argument == "on") {
        Log("harness: focus on (the window restored, activated)");
        harnessplatform::ShowMinimized(false);
        harnessplatform::Activate(true);
        return "";
    }
    return "focus needs off or on";
}

// windowsize <width> <height>: the game window's client area resized, as the user or a display change could; the
// renderer scales the frame into it.
std::string WindowSizeStep(const std::string& argument) {
    int width = 0, height = 0;
    if (std::sscanf(argument.c_str(), "%d %d", &width, &height) != 2 || width <= 0 || height <= 0)
        return "windowsize needs <width> <height>";
    if (!GameWindow()) return "windowsize: no game window yet";
    Log("harness: windowsize %dx%d", width, height);
    harnessplatform::ResizeClient(width, height);
    int now = 0, high = 0;
    harnessplatform::ClientSize(&now, &high);
    Log("harness: windowsize: the window is %dx%d", now, high);
    return "";
}

// "realinput": input made as SDL's own events, translated as real play's (harnessplatform::InjectReal*), given to the
// game with the player's at the end of the pass (ReleaseRealInput).
std::string RealInputStep(const std::string& argument) {
    const std::size_t space = argument.find(' ');
    const std::string kind = argument.substr(0, space), rest = space == std::string::npos ? "" : argument.substr(space + 1);
    if (kind == "key" && !rest.empty()) {
        harnessplatform::InjectRealKey(rest.c_str());
        return "";
    }
    if (kind == "text" && !rest.empty()) {
        harnessplatform::InjectRealText(rest);
        return "";
    }
    int x = 0, y = 0;
    if ((kind == "move" || kind == "click" || kind == "rclick") && std::sscanf(rest.c_str(), "%d %d", &x, &y) == 2) {
        const Rect client = GameClient();
        const Point at = ToWindow(x * (client.right - client.left) / 800, y * (client.bottom - client.top) / 600);
        harnessplatform::InjectRealMouse(kind == "move"    ? harnessplatform::RealMouse::Move
                                         : kind == "click" ? harnessplatform::RealMouse::Click
                                                           : harnessplatform::RealMouse::RightClick,
                                         at);
        return "";
    }
    return "realinput needs key <name>, text <text>, move|click|rclick <x> <y>";
}

// Real play's input (the player's in a live recording, or the "realinput" step's) given to the game at the end of the
// pass after a frame, where a recorded scenario's "at" lines give it on replay: a frame's mouse moves merged into the
// last, the harness's mouse (CursorPosition) put where each mouse event is, the motion added.
void ReleaseRealInput() {
    std::deque<platform::Event> events;
    Point motion{};
    if (!harnessplatform::TakeReal(&events, &motion)) return;
    if (record::Cutting()) return;   // between a recording's cut and the next part's origin the player's input is dropped
    for (std::size_t i = 0; i < events.size(); ++i) {
        const platform::Event& e = events[i];
        const bool mouse = e.type == platform::EventType::MouseMove || e.type == platform::EventType::MouseDown ||
                           e.type == platform::EventType::MouseUp || e.type == platform::EventType::Wheel;
        if (e.type == platform::EventType::MouseMove && i + 1 < events.size() &&
            events[i + 1].type == platform::EventType::MouseMove)
            continue;
        if (mouse) {
            g_mouseX = e.x;
            g_mouseY = e.y;
        }
        harnessplatform::Queue(e, false);
    }
    if (motion.x || motion.y) input::AddMotion(motion.x, motion.y);
}

void KeepGameWindowActive() {
    void* window = GameWindow();
    if (!window) return;
    {
        timing::Scope scope(timing::WindowChecks);
        sys::WatchWindows(window);
    }
    static bool logged = false;
    if (!logged) {
        logged = true;
        int width = 0, height = 0;
        harnessplatform::ClientSize(&width, &height);
        Log("harness: the game window stays active when it loses focus; client %dx%d", width, height);
    }
    ActivateWithoutForeground();
}

// Posted input reaches the game only through its main loop (WinMain 00456440: PeekMessageA, then
// GetMessageA and the input dispatcher 00457970) or the movie player's loop (005DB3F0: Esc and
// Space). Every other PeekMessageA call comes from the game's pump 00458220, which interface window
// switches run (Interface3D::Activate 005F71C0 when the battle screen opens after a new game,
// PushWindow, closing windows) to discard the input waiting meanwhile: what it removes goes to the
// window procedure, which ignores keys and mouse. Steps can run inside it, so input steps posted
// there were lost (the console scenario's keys, when the battle screen opened after its waitfor);
// they wait for one of these calls instead (the rewritten WinMain, src/game/app/main_loop.cpp, names its two calls
// through ei::mainloop::g_site).
bool IsWinMainLoop() {
    using ei::mainloop::Site;
    return ei::mainloop::g_site == Site::PeekBeforeFrame || ei::mainloop::g_site == Site::PeekAfterFrame;
}
bool IsInputLoop() { return IsWinMainLoop(); }

// A pending close-all (Interface3D +0x44, set by a screen request 00578B00 / 00578B20 or a leave
// answer) is carried out by whichever comes first: the input dispatcher's first step
// (CloseAllPending 005F5AB0) for the next message the main loop takes, or the next pass (the
// server's answer through Activate, or Draw). Which comes first depends on whether a message waits,
// and the close stops the playing sounds, which Terrain::PlayAmbience's
// draws depend on (SV1j: ui_view_screen's `screen 7` closed in its own frame or the next). On the
// virtual clock the main loop's peeks report no message while one is pending, so the next pass
// always does it; after two frames (no pass took it up) messages go through again.
unsigned g_closeAllHeldFrom = kNotReady;
bool HoldMessagesForCloseAll() {
    if (!clock::Virtual() || !IsWinMainLoop() ||
        !HarnessCloseAllPending()) {
        g_closeAllHeldFrom = kNotReady;
        return false;
    }
    if (g_closeAllHeldFrom == kNotReady) {
        g_closeAllHeldFrom = g_frames;
        clock::RandomNote("close-all pending: messages held until the next pass");
    }
    return g_frames < g_closeAllHeldFrom + 2;
}

bool g_closeTaken = false;   // live play's window close, taken as a quit

// The client world's pause (the escape menu, a dialogue, ...) logged where it changes, with the frame, so a golden
// that differs from a pause on can be read against the steps' lines (H62b).
void LogPauseChange() {
    static bool paused = false;
    if (image::ClientPaused() == paused) return;
    paused = !paused;
    Log("harness: frame %u: the game %s", g_frames, paused ? "paused" : "resumed");
}

// Before the game looks for events (harness_platform.h): the frame count, the steps; false
// while the close-all holds the events.
bool BeforeEvents() {
    if (g_randomTrace) clock::InstallRandomTrace(&g_frames);
    KeepGameWindowActive();
    // A frame has been drawn: the rewritten loop's after-frame call.
    const bool afterFrame = ei::mainloop::g_site == ei::mainloop::Site::PeekAfterFrame;
    if (afterFrame) {
        ++g_frames;
        LogPauseChange();
        timing::FrameEnd();
        timing::Scope scope(timing::PairWait);
        pair::FrameDone(g_frames);   // a pair's games take turns a frame at a time (harness_pair.h)
        if (g_dragX || g_dragY) {
            input::AddMotion(g_dragX, g_dragY);
            input::MoveMouse(Point{g_mouseX, g_mouseY});
        }
    }
    // On the virtual clock the steps run once a frame, after it: how often the game peeks besides
    // depends on the events it gets, and a step that follows a console command's output would
    // otherwise run in this frame or the next.
    if (g_running && !g_inStep && (afterFrame || !clock::Virtual())) {
        g_inStep = true;   // console commands can pump messages themselves
        g_inputLoop = IsInputLoop();
        g_winMainLoop = IsWinMainLoop();
        timing::Scope scope(timing::Steps);
        RunSteps();
        if (afterFrame) {
            ReleaseRealInput();
            // Live play: the player closed the window; the recording ends with a quit in the next pass.
            if (record::Live() && harnessplatform::CloseRequested() && !g_closeTaken) {
                g_closeTaken = true;
                Log("harness: the window was closed: the recording ends");
                g_steps.resize(g_next);
                g_steps.push_back({"quit", ""});
            }
            record::EndPass();
        }
        g_inStep = false;
    }
    return !HoldMessagesForCloseAll();
}

// EI_RENDER_EXTRA: words added to the renderer's settings of every run (the scenario's "render" step, or
// EI_RENDER), e.g. a debugging word of docs/notes/renderer.md. Set it in the environment of `work.py test` (a batch
// keeps its EI_* variables).
std::string ExtraRenderWords() { return platform::EnvironmentVariable("EI_RENDER_EXTRA"); }

std::string WithExtraRenderWords(const std::string& spec) {
    const std::string extra = ExtraRenderWords();
    return extra.empty() ? spec : spec + "," + extra;
}

bool LoadScenario(const char* path) {
    bool rendered = false;   // a "render" step configured the renderer
    FILE* f = sys::OpenFile(path, "r");
    if (!f) return false;
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        std::string text(line);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
        std::size_t start = text.find_first_not_of(" \t");
        if (start != std::string::npos && text.compare(start, 10, "# prepare:") == 0) g_loadLines.push_back(text);
        if (start == std::string::npos || text[start] == '#') continue;
        text = text.substr(start);
        std::size_t space = text.find(' ');
        g_steps.push_back({text.substr(0, space), space == std::string::npos ? "" : text.substr(space + 1)});
        // A line read here, not a step: kept for a recording's header (harness_record.h), but for the run's own time
        // limit and peer.
        auto TakeLoadLine = [&] {
            const std::string& verb = g_steps.back().verb;
            if (verb != "timeout" && verb != "peer") g_loadLines.push_back(text);
            g_steps.pop_back();
        };
        if (g_steps.back().verb == "recordinput" || g_steps.back().verb == "recorded") {
            if (g_steps.back().verb == "recorded") {
                g_recordedScenario = true;
            } else {
                // recordinput [live] [cut <frames>]
                const std::string& argument = g_steps.back().argument;
                const std::size_t cut = argument.find("cut ");
                record::Ask(argument.compare(0, 4, "live") == 0,
                            cut == std::string::npos ? 0 : static_cast<unsigned>(std::strtoul(argument.c_str() + cut + 4, nullptr, 10)));
            }
            g_steps.pop_back();
            continue;
        }
        if (g_steps.back().verb == "randtrace") {
            g_randomTrace = true;
            TakeLoadLine();
            continue;
        }
        if (g_steps.back().verb == "deterministic" || g_steps.back().verb == "realtime" ||
            g_steps.back().verb == "clocktrace") {
            if (g_steps.back().verb == "clocktrace") g_clockTrace = true;
            else g_deterministic = g_steps.back().verb == "deterministic";
            TakeLoadLine();
            continue;
        }
        if (g_steps.back().verb == "secondmaster") {
            ::net::second_master::SetAddress(g_steps.back().argument);
            TakeLoadLine();
            continue;
        }
        // "vanilla <step>" / "rebirth <step>" for a window step, taken before the game starts (a window size for one
        // install's runs): applied when the data folder is that install's (starter::RebirthData), else dropped.
        if (g_steps.back().verb == "vanilla" || g_steps.back().verb == "rebirth") {
            const std::string& inner = g_steps.back().argument;
            const std::size_t at = inner.find(' ');
            const std::string verb = inner.substr(0, at), argument = at == std::string::npos ? "" : inner.substr(at + 1);
            if (verb == "resolution" || verb == "widescreen" || verb == "fullscreen") {
                if ((g_steps.back().verb == "rebirth") == ei::verify::starter::RebirthData())
                    ei::verify::starter::ParseStep(verb, argument);
                TakeLoadLine();
                continue;
            }
        }
        // A "mod" line after the game's first window wait is a switch of mods in the run (a step); before it, the
        // settings' Mod at the start.
        const bool modStep = g_steps.back().verb == "mod" &&
                             std::any_of(g_steps.begin(), g_steps.end() - 1, [](const Step& step) {
                                 return step.verb == "waitmenu" || step.verb == "waitwindow";
                             });
        if ((!modStep && ei::verify::starter::ParseStep(g_steps.back().verb, g_steps.back().argument)) ||
            settings::ParseStep(g_steps.back().verb, g_steps.back().argument)) {
            TakeLoadLine();
            continue;
        }
        if (g_steps.back().verb == "digest" && g_steps.back().argument == "water") {
            digest::WithWater();   // named in the digest's title, written before the main menu's first record
            TakeLoadLine();
            continue;
        }
        if (g_steps.back().verb == "render") {
            rendered = true;
            image::ConfigureRenderer(WithExtraRenderWords(g_steps.back().argument).c_str());
            TakeLoadLine();
            continue;
        }
        if (g_steps.back().verb == "peer") {   // run_all's: the other game of a pair (harness_pair.h)
            TakeLoadLine();
            continue;
        }
        if (g_steps.back().verb == "timeout") {
            g_budget = 1000 * std::strtoul(g_steps.back().argument.c_str(), nullptr, 10);
            TakeLoadLine();
            continue;
        }
        // The clocks are hooked from the start when the scenario freezes them (see HeldClock).
        const Step& last = g_steps.back();
        if (last.verb == "freeze" || ((last.verb == "vanilla" || last.verb == "rebirth") && last.argument == "freeze"))
            g_freezeUsed = true;
        if (last.verb == "capture" && last.argument.find(" freeze") != std::string::npos) g_freezeUsed = true;
        // The step's own verb, also after "vanilla" / "rebirth" (a trace on one install's data only).
        const bool installStep = last.verb == "vanilla" || last.verb == "rebirth";
        const std::string verb = installStep ? last.argument.substr(0, last.argument.find(' ')) : last.verb;
        if (camera_trace::UsedBy(verb)) g_cameraTraceUsed = true;
        {
            // "read", "wrote" and "waitread" (also after "vanilla" / "rebirth"): the reads are noted from the start.
            const std::string word = last.verb == "vanilla" || last.verb == "rebirth" ? last.argument : last.verb + " ";
            if (word.compare(0, 5, "read ") == 0 || word.compare(0, 6, "wrote ") == 0 || word.compare(0, 9, "waitread ") == 0)
                modtrace::Enable();
        }
        if (camera_trace::ObjectsUsedBy(verb)) g_objectTraceUsed = true;
    }
    std::fclose(f);
    // EI_RENDER_EXTRA's words also reach a scenario without a "render" step (on top of EI_RENDER).
    if (!rendered && !ExtraRenderWords().empty())
        image::ConfigureRenderer(WithExtraRenderWords(platform::EnvironmentVariable("EI_RENDER")).c_str());
    return true;
}

struct Harness {
    Harness() {
        const std::string scenario = platform::EnvironmentVariable("EI_HARNESS");
        if (scenario.empty() || scenario.size() >= kPath) return;
        const char* path = scenario.c_str();
        sys::RememberMainThread();   // the harness starts on the game's main thread, before main
        // Report next to ei-rebuild.log. It exists from the start ("RESULT: RUNNING"), so run_all can
        // tell a run whose harness never started from one that is still going.
        sys::CopyText(g_reportPath, ei::OutputPath("harness_report.txt").c_str());
        FILE* report = sys::OpenFile(g_reportPath, "w");
        if (!LoadScenario(path)) {
            Log("harness: cannot read scenario %s", path);
            if (report) {
                std::fprintf(report, "FAILED: cannot read scenario %s\nRESULT: FAIL\n", path);
                std::fclose(report);
            }
            sys::Terminate(2);   // never leave a game waiting in the menu
        }
        // EI_HARNESS_CLOCK (run_all.py / compare.py --clock): "virtual" or "real" overrides the
        // scenario's clock, "trace" adds the clock trace.
        const std::string clockMode = platform::EnvironmentVariable("EI_HARNESS_CLOCK");
        if (clockMode.find("virtual") != std::string::npos) g_deterministic = true;
        if (clockMode.find("real") != std::string::npos) g_deterministic = false;
        if (clockMode.find("clocktrace") != std::string::npos) g_clockTrace = true;
        if (clockMode.find("randtrace") != std::string::npos) g_randomTrace = true;

        if (report) {
            std::fputs("RESULT: RUNNING\n", report);
            std::fclose(report);
        }

        g_self = sys::SelfImage();
        // Faults end the run with the report written; no system or runtime box waits for a user (harness_system.h).
        sys::InstallFaultHandlers(&FirstChance, &Fatal);
        ei::InstallProfiler();   // EI_PROFILE=1: samples this thread from here on

        sys::CopyText(g_savesDir, sys::FullPath(ei::OutputPath("harness_saves")).c_str());
        sys::MakeFolder(g_savesDir);
        Log("harness: %lld MB free on the run folder's disk", sys::FreeMegabytes(g_savesDir));
        sys::CopyText(g_rootDirs[0], sys::FullPath(starter::GameFolder()).c_str());
        FindInstallRoots();
        g_readTrace = platform::EnvironmentVariable("EI_READTRACE") == "1";

        // A scenario's "resolution" / "widescreen" (or EI_RESOLUTION / EI_WIDESCREEN), the image's small
        // window otherwise: harness_starter.cpp.
        ei::verify::starter::Install(g_savesDir);
        modtrace::SetRoots(starter::ModPath(), starter::GameFolder(), g_savesDir);
        settings::Install();   // the registry snapshot and the pinned install files, never the user's (harness_settings.h)
        netlocal::Install();   // a UDP port of its own, sockets on 127.0.0.1 only (harness_net.h)
        digest::Init(&g_frames);   // EI_DIGEST
        record::Init(&g_frames, &g_mouseX, &g_mouseY);
        if (record::Asked()) {
            std::string error = !g_deterministic ? "recordinput needs the virtual clock (deterministic)"
                                : pair::Active() ? "recordinput cannot record a pair"
                                                 : record::Begin(g_loadLines, path);
            if (!error.empty()) {
                Log("harness: %s", error.c_str());
                g_failures.push_back(error);
            } else if (record::Live()) {
                harnessplatform::TakeRealInput();
            }
        }
        if (savetrace::Enabled()) savetrace::Install(g_self.base, g_self.end);   // EI_SAVETRACE (harness_savetrace.h)

        if (watch::Enabled()) watch::Install();   // EI_WATCH_CUNIT (harness_watch.h)

        image::ObserveConsoleLines(&OnConsoleLine);
        g_answering = true;   // the overlay, the other game window (platform/game_answers.cpp)
        if (g_deterministic) {
            clock::InstallVirtual(&TickLength());
            image::SetWaitForVsync(false);   // frames need not wait for the display
            image::SetPlayedMoviesPaced(false);   // nor a script's movie for its real length (H70)
            clock::FixDate();
            image::SetFixedSeed(&FillFixedSeed);
            Log("harness: deterministic mode (virtual clock)");
        } else if (g_freezeUsed) {
            clock::InstallHeld();
            clock::FixDate();
            Log("harness: the game's clocks can be frozen (fixed date)");
        }
        if (g_clockTrace) clock::InstallTrace();
        // The main loop's calls: the rewritten loop's hook point (game/app/harness_hooks.h) for each one taken.
        ei::mainloop::HarnessHooks& hooks = ei::mainloop::g_harnessHooks;
        Log("harness: the main loop and the frame step through the rewritten callers' hook points only");
        if (g_freezeUsed || g_cameraTraceUsed) hooks.frameCameraUpdate = &HeldCameraUpdate;
        hooks.serverUpdate = &HeldServerUpdate;
        hooks.clientSegment = &HeldClientSegment;
        if (pair::Active() && pair::Overlap()) hooks.frameStep = &HandOffThenFrameStep;
        else if (g_objectTraceUsed) hooks.frameStep = &TracedFrameStep;

        for (const Step& step : g_steps)
            if (step.verb == "capture") {
                capture::Install();
                break;
            }
        Log("harness: saves and install writes redirected to %s (root %s)", g_savesDir, g_rootDirs[0]);
        sys::StartThread(&Watchdog);
        g_running = true;
        Log("harness: %u steps from %s", unsigned(g_steps.size()), path);
    }
} g_harness;
} // namespace

namespace harnessplatform {

bool BeforeEvents() { return ei::verify::BeforeEvents(); }
bool CollectCursors() { return ei::verify::g_cursorCollecting && ei::verify::RampIsIdentity(); }
int TypedCharacter(platform::Key key) { return ei::verify::TypedCharacter(key); }

// The harness's mouse (the last position a step put it at, the client area's centre before), on the screen.
bool MousePosition(Point* at) {
    void* window = GameWindow();
    int width = 0, height = 0;
    if (!window || !ClientSize(&width, &height)) return false;
    Point p = g_mouseX < 0 ? Point{width / 2, height / 2} : Point{g_mouseX, g_mouseY};
    *at = platform::ClientToScreen(window, p);
    return true;
}

// A recorded scenario's game, or a recording's, quits itself after the last step (the player's Exit): the run ends as
// the quit step ends it.
void GameQuits() {
    if ((!g_recordedScenario && !record::On()) || g_quitting || g_next < g_steps.size()) return;
    Log("harness: the game quits itself after the scenario's last step");
    record::End();
    WriteReport();
    if (FILE* f = sys::OpenFile(ei::OutputPath("harness_quit"), "w")) std::fclose(f);
    g_quitting = true;
}

platform::MessageAnswer AnswerMessageBox(std::string_view text, std::string_view caption, platform::MessageButtons) {
    EndRunOnMessageBox(std::string(caption), std::string(text));
    return platform::MessageAnswer::Cancel;
}

bool Answering() { return g_answering; }

// A movie the game plays by itself (the new game's intro, the exit's, a script's PlayMovie) in a recording or a recorded
// scenario's run: a .bik other than the loading screen's (app::g_loadingMovieName). PlayMovie times its frames by the
// wall clock in a loop of its own, where the steps do not run: the player's skip of a long one (Esc) would reach
// nothing, and its replay would wait for all of it. So these runs skip them (PlayMovie plays nothing when the file is
// missing); the loading screen's movie is drawn by the load's progress and stays. Other runs play them.
bool PlayedMovie(const std::string& path) {
    if (!g_recordedScenario && !record::Asked()) return false;
    if (path.size() < 4 || text::CompareIgnoringCase(path.c_str() + path.size() - 4, ".bik") != 0) return false;
    const std::size_t slash = path.find_last_of("\\/");
    const char* loading = image::LoadingMovieName();
    return !loading || text::CompareIgnoringCase(path.c_str() + (slash == std::string::npos ? 0 : slash + 1), loading) != 0;
}

// The game's paths mapped onto the run's overlay (Layers above): config\movie.ini (the startup movies:
// "Start" lists 1C.bik and Nival.bik) is missing, so nothing plays, and in recorded play the movies the game plays
// (PlayedMovie); a write still in an install is refused. The saves trace sees every write (EI_SAVETRACE).
std::string MapPath(std::string_view path, platform::PathUse use) {
    const std::string name(path);
    const std::string tutorial = std::string(g_savesDir) + "\\tutorial.ini";
    if (use == platform::PathUse::Layer) {
        // One layer's own file (a record's TOML in each layer, ReadLayerFile): traced, never mapped.
        if (g_readTrace) TraceRead(name);
        modtrace::NoteRead(name);
        return name;
    }
    if (use == platform::PathUse::Read) {
        if (name.size() >= 9 && text::CompareIgnoringCase(name.c_str() + name.size() - 9, "movie.ini") == 0) return {};
        if (PlayedMovie(name)) return {};
        std::string mapped = TutorialFile(name) ? tutorial : Layers().Map(name, use);
        if (g_readTrace) TraceRead(mapped);
        modtrace::NoteRead(mapped);
        return mapped;
    }
    savetrace::OnFileWrite(name.c_str());   // harness_savetrace.h
    platform::PathLayers::Placed placed = platform::PathLayers::Placed::Kept;
    const std::string mapped = TutorialFile(name) ? tutorial : Layers().Map(name, use, &placed);
    if (placed == platform::PathLayers::Placed::Layer) LogInstallWrite(name.c_str(), mapped.c_str());
    modtrace::NoteWrite(mapped);
    if (InInstall(mapped)) {
        RefuseInstallWrite(name, mapped);
        return {};
    }
    return mapped;
}

// A folder search's layers (the overlay's folder, the mod's, the game folder's), each traced as a read.
std::vector<platform::FolderSearch> MapSearches(std::string_view pattern) {
    std::vector<platform::FolderSearch> searches = Layers().Searches(pattern);
    for (const platform::FolderSearch& search : searches) {
        if (g_readTrace) TraceRead(search.pattern);
        modtrace::NoteRead(search.pattern);
    }
    return searches;
}
} // namespace harnessplatform

} // namespace ei::verify
