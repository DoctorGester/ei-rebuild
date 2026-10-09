# User interface (005E0000-00670000)

What is rewritten, scenario preconditions, hazards, original bugs kept, what is left. Layouts and per-function behaviour
are in the headers and code (`ui.h`, `ui_camp.h`, `ui_screens.h`, `ui_item_info.h`, `ui_mesh_build.h`, `ui_cursor.h`,
`ui_gdi.h`, `console.h`). Files are in `src/game/`, wrappers in `tests/verify/verify_<file>.cpp`. Verify: **S** side by
side (the original runs first, its state recorded and put back, then ours), **O** ours only (the original creates,
draws or sends; invariants checked), **P** pure. Old section titles in code comments ("Sixteenth pass" etc.) map to:
input and commands -> section 6, camp -> section 5, items -> section 9.

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
map, 5 camp (005F6240), 7, 8, 9. The client asks for a screen with message 4 (`00578B00(id)` -> 0066B280; sets the client
player's +0x20 to 3); the server's handler (00670CE0) accepts it only in state 1 (in the world): it removes the party from
the world (00590A40), moves to state 2 and answers with message 7. Message 5 (`00578B20` -> 0066B2C0, needs client state
3; the camp's page 7) brings the party back (00670DA0: state 2 -> 1). So the global map's camp button works only where the
map is the world's own screen (a "brief" zone). `LeaveToZone(player, zone, exit)` (0066F310, server) sends message 8; the
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

**Rebirth.** Rebirth's addon patches the five scaling helpers, the UI sprite placement (005E6FA0, 005E70E0, 005E7D60,
005E7680, 005E99D0), CUiRegion::SetRect / AddPoint, 005FDE20 (text), 005F8340 (text extent) and many screen builders and
key handlers. Rewritten callers go through the original addresses (`CInterface3D::CallScale*`, `CUiRegion::CallSetRect`,
`CUiMesh::SetRectangle / SetFrame / Place`): calling our scaling versions directly made `RegionAt` disagree with the
patched original. Rebirth also overwrites data the original reads at run time (00742B08, the clock's x centre, 800 in the
exe, becomes its widescreen width): read such constants from memory. Rebirth-patched functions are hooked in the vanilla
install only; the Rebirth smoke run is the check.

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

| Address | Name | V |
|---|---|---|
| 005F7CA0, 005F7D20, 005F7DA0, 005F7E00, 005F7E30 | `CInterface3D::ScaleRectToScreen`, `ScalePointToScreen`, `ScaleRectFromScreen`, `ScalePointFromScreen` (+Copy) | P |
| 005F7590, 005F74E0, 005F2EA0, 005F2EE0 | `GetActiveWindow`, `SetPendingValue`, `UiRecordTable::SpellAt` / `ModifierAt` | P |
| 005FD3A0, 005FE400, 005FC450 | `CUiSurface::SetRect`, `MoveTo`, `CUiModel::SetScreenPosition` | S |
| 005FC8F0, 005FC660, 005FFCC0, 005FFD30 | `CUiRegion::HitTest`, `HasActiveTip`, `CUiWindow::RegionAt`, `ContainsPoint` | P |
| 005FC9E0, 005FCA40, 005FCA70, 005FC880, 005FEBF0, 006002A0 | `CUiRegion::SetRect`, `ClearShape`, `AddPoint`, `OnMouseMove`, `CUiWindow::UpdateRegionTips`, `SetEnabled` | O |
| 005FC730, 005FC6B0, 005FC690, 005FC600 | `CUiRegion::SetTip` ("tip %d" up to '#'; a missing text shows the key), `SetTipString`, `SetTipText`, `ReleaseContents` (`ui_region_tips.cpp`) | O |
| 005FEDD0-005FF7D0, 005FFA90-005FFC50, 005FF830-005FFA20, 005FFD80, 005FFF40 | `AddObject`, `RemoveObject(At)`, `RemoveSceneObject(At)`, `ClearSceneObjects`, `AddSurface` (returns its argument), `RemoveSurfaceAt`, `ClearSurfaces`, `AddRegion`, `RemoveRegionAt`, `ClearRegions`, `AddModel` (0 on allocation failure), `ClearModels` | O |
| 006003D0, 006004E0, 00600730, 00600840, 00600950, 00601D00 | `UiPtrArray<T>::SetSize` copies | O |

Kept: `AddRegion` leaves the new region's bounds uninitialised; the append's dead branch for `size == -1` frees the storage
and stores through the null array; removing by index does not check it (past the end the original's `REP MOVSD` faults;
ours skips the move). Library: STLport / MFC code in 005F3000-005F4990, 005F9810-005FC010, 0060A320-0060BD70,
00613400-00613DF0, 00619420-0061B220, 0062A390 (reviewed ones in library.csv; 70 twins from the "A-I15 sweep").

**Byte-identical copies** registered with their anchors: iterator copies 005E2470, 005F97D0, 00609A30, 00609A70, 0062C080
(`TextSourceIterator::CopyTo`), 005E6810 (`DwordArraySetSize`), 00600A80 (`AllocateDwords`), 00600330-006003B0 / 00601B50
(`ArrayFree1`), 005ED7A0 (`RefObjectDestructBase`), 005FC1C0 (`RefPtrNull`), 00609960 (`RefPtrCopyPlain`), 0062D330 (= 
`CBriefingScreen::SaveState`), 005EA150 (`CFigureMesh::RefreshBuffers`). Left: twins under 5 bytes, the 30-byte scalar
deleting destructors, the CString-member destructors 005FBE30 / 006062B0 / 0060F460.

**Deleting destructors** (`ui_window_delete.cpp`, O). Slot 0 of every window in 005E0000-00640000 (50) and the input
sides' slot 0 (37 thunks `SUB ECX,0x94; JMP`; ours return the window); bodies that only jump to 005FE930 / 00600BA0 are
registered as those. Ported bodies: 005EACF0 / 00602BA0 (input side base 007401D0), 005EE380 `CItemInfoPanel`, 00608ED0
`CChatWindow` (00741478, the base screen's chat window, constructor 00608D20), 0060E2B0 the forwarding strip (0060E280, the
plain strip's, frees nothing of its own: kept), 0060E600 `CSpellMakerPage`, 0061E3E0 = 00633750 `CMessageBox`, 0061E460
`CMasterRequestBox` (00741DD8: message box + master server link at +0x180), 005E28E0 / 005E2670 `CChatOverlay` /
`ChatLine` (007400DC), 005F6130 `CBattleScreenSetup`, 005F8970 `CHelpWindow` (00740E28), 00603B90 `CObjectivesWindow` (does
not free the exit distances map +0x178: kept), 006070C0 `CSaveLoadWindow`, 005F6BA0 `CBriefingScreen`, 00618570
`CMpConnectScreen` (`MpAddressEntry` 0x34 bytes), the HUD panels' input thunks 0064BCF0-0064BD80. 0062EDA0 is 00499E30's
code.

### Window stack, screen construction, window base methods (O unless noted)

| Address | Name / note |
|---|---|
| 005F5BA0 | `CInterface3D::CreateScreen` (`ui_create_screen.cpp`): inlines the constructors of screens 1, 2, 3, 7; id 0 does nothing, 6 and ids above 9 reach the activation with the id as the window pointer |
| 005F71C0, 005F7350 | `Activate`, `PushWindow` (stack full: 005FA9A0; backdrops +0x54 / +0x58, +0x60 the server flag kept for the first window, five render states, slot 25, message pump) |
| 005F78C0, 005F7B40, 005F7BE0, 005E47E0 | `CInterface3D::OnKeyDown` (`ui_interface_keys.cpp`), `OnChar` (console, a client's open chat line, top window), `OnKeyUp`, `ConsoleWindow::OnKeyUp` (S) |
| 005F76A0, 005F76E0, 005F7740, 005F7780, 005F77C0, 005F7800, 005F7840, 005F7880 | `CInterface3D::OnLeftDown` .. `OnWheel` (`ui_interface_input.cpp`; to the capture, else the top window; +0x7F double click, +0x80 right button; S on stand-in vtables) |
| 005F74F0, 005F5AB0 | `CloseAll`, `CloseAllPending` (+0x44; screen flag 00784750) |
| 005FE700, 005FE930, 005FF460, 005FFFB0, 00600D30, 006005F0 | `CUiWindow::Construct`, `DestroyBase` (SetSize(0) on the arrays), `ReplaceSceneObject`, `CopyImage` (S), `CUiContainer::AddChild`, `UiPtrArray::SetAtGrow` |
| 005F67A0 / 005F6920, 005F6240, 005F6470 / 005F6620, 005F6F30 / 005F7010, 005F6D50 / 005F6E40 | `CGlobalMap` constructor / destructor body, `CCampPages::Construct`, item screens' and the camp's destructor bodies, screens 9 and 8 (the STLport containers' allocator bytes are stack bytes; ours passes the same value where a constructor keeps them) |
| 005FF3C0, 00600B20, 00600BA0, 00600CC0 | `CUiWindow::AddSprite`, `CUiContainer::Construct`, `DestroyContainer`, `AddInput` |
| 005FEB90, 005FECC0, 005FED00, 00601040, 00601090, 006010E0, 00600270 | per-frame slots (`verify_ui_window_slots.cpp`, stand-in vtables record the calls): `DrawBase` (20), `RedrawBase` (22), `RedrawTips` (23), `CUiContainer::RedrawContainer` / `RedrawChildTips` (nothing unless CInterface3D +0x30), `UpdateBase` (24), `FrameTime` (display slot 6 ms * 0.001f, left unrounded: forwarders return `double`) |
| 005FC1D0, 005FC2F0, 005FC3B0, 005FC3D0, 005FC4C0, 005FC4F0, 005FC1F0 | `CUiModel::ReleaseObject`, `Draw` (timeGetTime / 67 animation clock), `Play`, `Stop`, `ShowPart`, `ShowAllParts` (part flag 0x80 = hidden), `Load` (map object class 0x33 / id 0x88CA6C00; ui_layouts.cpp calls `CallLoad`) (`ui_model.cpp`) |
| 005F86F0, 005F8A80, 005F8C90, 005F8420, 005F85C0, 005EADD0 | `OpenHelp`, `ShowMessage`, `MessageText` (S), `LoadState`, `SaveState` (S through a stream recorder; every save), the battle camera's key input (`ui_camera_keys.cpp`, S) |
| 005FEF20, 005FF0B0, 006002C0, 00601AC0 | `CUiWindow::LoadTexture` (texture manager loads 006922A0 / 006926F0 by address), `CreateTexture` (flag 0x40000), `ShowSurfaces` (19), `CUiContainer::ShowChildren` (19) |

### Container input dispatch (`ui_container.cpp`; scenario `ui_container_input`)

The window procedure (00457970) maps messages to the top window's input slots (WM_LBUTTONDOWN 2, WM_RBUTTONDOWN 5 sets
+0x80, WM_LBUTTONDBLCLK 7 sets +0x7F, WM_KEYDOWN 9, WM_CHAR 10 unless the console or a multiplayer chat takes it, WM_KEYUP
11, WM_MOUSEWHEEL 12). The container's dispatchers (input vtable 00740FD4): 00601110 (2), 006011F0 (3), 006012D0 (4),
006013B0 (5), 00601490 (6), 00601570 (7), 00601650 (8), 00601820 (9), 006018E0 (10), 006019A0 (11), 00601730 (12, RET
0x10, returns 1), 00601A60 (1): identical apart from the slot; no return value except the wheel's. Dispatch: the
container's handled flag cleared; each extra input side (+0x158..+0x15C) gets its flag cleared and the event, one that sets
its flag ends it; then each enabled child (+0x148; mouse and wheel events also need 005FFD30) the same, the child and list
end re-read after each call; slot 1 calls every side and child and touches no flag. Verify (O): flags preset to 0x5A must
stay on the skipped ones.

## 3. Interface base: frame update, text, console window, cursor, chat (005E0000-00610000)

Every scenario reaches the frame, draw, cursor and shutdown code. Not rewritten here: 005E3700 (the chat line's draw) and
005F8340 (text extent), both Rebirth-patched.

| Address | Name (file) | V |
|---|---|---|
| 005F57C0, 005F5220, 005E69C0 | `CInterface3D::Update` (below), `Draw` (`ui_interface_draw.cpp`), `CScreenFade::Draw` (bars of height * [00740150]; alpha step `(ms as qword) * rate + alpha` stored as float, clamped at 255, below 0 the fade ends) | O |
| 005F8C50, 005F4F90, 005F4790, 005E3DF0 | `CInterface3D::ResetInput`, `Shutdown` (writes config\tutorial.ini from `helpTopics_`), `DestroyBody` (atexit), `ConsoleWindow::DestroyBody` (`ui_interface_shutdown.cpp`) | O |
| 005F7E60, 005F7E90, 005F7EB0, 005F7ED0, 005F7EF0, 005F7F10, 005F5700, 005F7FC0, 005F75B0 | `PlaySound` (Play2D, priority 1000, volume 100; the sound system gets the game's own string pointers), `StopSound`, `ConsoleAddText`, `SetConsoleRect`, `SetConsoleHistory`, `SetDebugInfoRect`, `ShowFps`, `OnQuestVariable` (at 1 the name's third '.' part becomes savedText_), `ShowStatusText` (`ui_interface_glue.cpp`) | O |
| 005F7F30, 005F7F60, 005F7F80, 005F7FA0, 005F82A0, 005F82D0, 005F8310, 005F86D0, 005F82F0 | news for the top window (slots 28-32, 36, 37 as `CUiWindow::Virtual*`), the bottom window's slot 40 (save game), the chat overlay's AddLine; 005F82D0 / 005F82F0 / 005F8310 multiplayer only | S (`slot_recorder.h`) |
| 005E6D90, 005E6C60, 005EA210, 005EA600, 005EA4C0, 005EA380 | `CUiMesh::Draw` (`ui_mesh_draw.cpp`), `CreateSprite`, `SetQuadCount` / `SetVertexCount` / `SetIndexCount` / `SetUvCount` (`lib::AlignedArray<T>::SetSize` on typed views) | O |
| 005FF1E0, 005FDCB0, 005FE370, 005FE2F0 | `CUiWindow::ClearObjects` (only 006091C0 calls it), `CUiSurface::FrameRect` (tooltip border; `ui_gdi::GameBrush`), `Fill`, `Clear` (`ui_surface_frame.cpp`) | O |
| 005FDFA0 | `CUiSurface::DrawWrappedText` (below) | S (pixels) |
| 005E4A10, 005E4800, 005E4180, 005E3FD0, 005E44A0 | `ConsoleWindow::DrawInput`, `DrawLines`, `OnKeyDown`, `Show` (O), `OnChar` (Rebirth's ImproveConsole behind its flag) | S (pixels: before ours the input strip's rows are put back inverted so anything left undrawn shows) |
| 005E3CE0, 005E26F0 | `ConsoleWindow::Construct` (`console_window_create.cpp`), `CChatOverlay::Construct` (ends in 005E3700 by address) | O |
| 005E60B0, 005E63F0, 005E5160, 005E51C0, 005E4E00, 005E5200, 005E5270, 005E67B0, 005E65F0, 005E6610, 005E6260, 005E62E0, 005E52E0 | `CCursor::Init`, `AddShape` (`ui_cursor_setup.cpp`), `CursorImage::Destroy`, `CCursor::Construct`, `DestroyBody`, `ChangeShape` (callers keep `SetShape` through the address), `ShowImage`, `SetHeld`, `TakeLock` / `ReleaseLock` (mode 2 only), `Start` (thread 005E52A0 unless mode 1), `Shutdown`, `UpdatePosition` (frames every 125 ms; +0x08 the DirectInput mouse, +0x98 the thread) | O |
| 005E54A0, 005E5690, 005E5C40, 005E4EB0 | cursor frame draw, thread, `CCursor::Blit` (S), image construct: renderer.md | |
| 005E18E0, 005E1710, 005E17E0, 005E1870 | `CFlyingHitPoints::Draw` (S, timeGetTime held, stand-in renderers replayed), `Construct` / `Destruct` / `Add` (flag 8 starts 0.5 s later) (`ui_flying_hit_points.cpp`; numbers need option 17, 0079BE74) | S / O |
| 005E2990, 005E29E0, 005E2A30, 005E36C0, 005E3930, 005E3230, 005E3550 | `CChatOverlay::Update` (lines older than 20 s erased), `Redraw`, `AddLine` (wrap at 780 px, "^<digit>" colours, at most 12 lines), `SetOpen`, `ClearLines`, the chat line's keys (Enter sends a `net::SayMessage` kept 60 s) (`ui_chat_overlay.cpp`; multiplayer only, no scenario) | O |
| 005EB090, 005EAD00, 005EAD50, 005EB580, 005EB540 | `CCameraInput::OnKeyUp`, `OnRightDown` / `OnRightUp` (turning with the cursor held), `OnDeactivate`, `OnWheel` (`ui_camera_input.cpp`) | S; ui_hud, ui_screens |
| 005EE310, 005EBFB0, 0060E360 | `CUiItemPanel::OnLeftDown` (input 2 of every item panel; S, ui_camp_deals), `ItemModel` (or a counted 100 x 100 stand-in sprite), `CHeroStatsPanel::DestroyBody` | |

`CInterface3D` declares +0x40 (queued window), the fonts +0x70, `messages_` +0x84 (stlp::list of CString), `helpTopics_` +0x98
(stlp::map<CString, int>); its root window +0x1C is the `CChatOverlay` (`chatOpen_` +0x14C, `caret_` +0x154). `CUiItemPanel`
(+0x148 `mode_`, +0x14C `owner_`) is the base of the description panel, hero equipment, quick strip, trade / repair grids,
constructor pages and the blank panel 00741B58. The hero stats panel's deleting destructors 0060E340 / 00613DD0 were once
registered as the plain window's and leaked (fixed).

`Update`: a pending value (+0x4C) pops the top window (input slot 1, slot 26, deleting destructor); 0x990001 sets close-all
and leaves the game (0057ECA0, 00581660); then +0x81 polls slot 35, close-all pops every window (005F8C50), a queued window
(+0x40) is activated, the mouse is copied (+0x68 / +0x6C from 007C7C50), a network client that lost the server (+0x88) gets
the "disconnect" box (00634650, 0x990000), else the top window's slot 24 runs, last the root's.

`DrawWrappedText` walks text + "\n" with the game's `_ismbblead` / `isleadbyte` / `_mbsinc`; 005F8340 takes a CString by
value, which a naked forwarder cannot declare (C3068): the data pointer is handed over and the local detached
(`TextMetricsAt::TextExtent`). Measuring uses a screen DC GetDC(0079BC28) and the font CInterface3D +0x70 + font*4, scaled to
800x600 by integer maths; a word that did not fit before a '\n' goes on a line of its own (even an empty one).

**Rule (GDI text pixel checks).** A rare pixel mismatch (a short Cyrillic word, two runs at the same millisecond) was an
outside event on the shared full-screen dgVoodoo desktop. The check redraws the original from the recorded pixels until two
draws agree (`ei::verify::SettleOriginal`, `tests/verify/pixel_settle.h`: up to 5 tries, 150 ms total) and compares ours with
that; an original that never settles is not compared. Used by DrawWrappedText, the console, the item panel's `CompareDrawing`
and `CTradePanel::DrawLists` (X11), the only wrappers that compare surface pixels.

Kept: the console's Up key increases the command index, Down stops at -1; Delete with no selection at the input's end does
nothing; a failed console font allocation still calls CreatePointFont on null; Shutdown deletes the queued window (+0x40) and
the fade (+0x18) without clearing them; `CCursor::Init` does not free the surface memory when 004EF7B0 throws;
`CCursor::Shutdown` dereferences a null shape entry (AddShape stores one when its allocation fails); the flying hit points
leave the render target size unset without a primary surface (ours 0); chat: more than 12 lines erases past the list's
sentinel, Enter with a failed allocation writes through null, Left / Right leave the caret unclamped (005E3700 clamps it).
Harness key names `grave`, `backspace`, `delete`, `home`, `end`, `pgup`, `pgdn`, `enterchar` (Enter plus its WM_CHAR, which
the console needs); scenario `console.txt`.

Library (A-I18): 005F3000 / 005F3170, 005F4390 / 005F44A0 / 005F9C70 / 0062C0C0, 005FAEA0, unwind-only destructors 005F60B0 /
005F6AD0 / 005F8FC0 / 005F9570 / 0062B480 / 00634090 / 0062B350 / 005E67F0, 005FBD30 / 005FBD80 / 005FBDD0, 005FBF50 / 005FBFB0
/ 005FC010. 00636203 is a Ghidra split of 006361B0, not a function.

**A-I20** (all O; none reached by a scenario unless noted):
- 00612CF0 `CCampPages::DebugFill` (`ui_camp_debug.cpp`; camp slot 39, only the vtable refers to it): the item / spell
  database name lists (006AD2F0, 006AD380, 006AD410, 006AD260, 00689570, 00689610) are now shared in originals_gameplay.h.
- `CChatWindow` (`ui_chat_window.cpp`, multiplayer base screen): 00608D20 `Construct`, 00608F90 `AddPortrait`, 006091C0
  `ShowPlayers`, 006095A0 `Update` (slot 24), 006097E0 / 00609900 input 2 / 4. The players vector (+0x150) holds counted
  `PlayerMessage`s; the front insert is the original's inlined `insert(begin(), x)` (copy up, assign back), overflow through
  0060A120. Kept: the binding is not null-checked on a click; the sex letter carries over between players.
- 005E2860 / 005E2880 `UiInputSide::LeftDoubleAsDown` / `RightDoubleAsDown` (`ui_input_defaults.cpp`; slot 7 / 8 of most
  input sides: the press handler with the same arguments; reached by `dblclick` on a window that keeps them).
- Exchange (`ui_exchange.cpp`): 0062C250 `Create`, 0062C9C0 `Draw` (children constructed inline: description panel,
  forwarding strip, figure strip, trade grid; scratch party not null-checked, as the original); 005F2E00
  `CItemIconWindow::PlaceBars` (`ui_item_screen.cpp`). Edit field forwarders 0065C010 / 0065C1A0 / 0065C290 shared in
  originals_ui.h (ui_layouts / ui_hero_screen_click use them; one duplicate fewer).

**A-I21** (`ui_connect_screen.cpp`, `ui_screens.h`; all O, multiplayer only: no scenario opens the connection screen,
which needs a hero on the multiplayer hero screen and Continue):
- `CMpConnectScreen`: 0061C430 `Update` (slot 24), 0061C2E0 `OnBoxResult` (slot 27), 0061C700 / 0061CA30 / 0061CAD0 input 2 /
  7 / 9, 0061CC00 `SelectAndScroll`, 0061CCE0 `Connect`, 0061CF50 `EditAddress`, 0061D0D0 `MasterRequest`, 0061D1C0
  `SaveAddressBook`, 0061D470 `RebuildList`. Slots 26 (0061C2D0, `RET`) and 41 (0061C420, returns 0) are too short to hook.
  Create 0061B2C0 and the list draw 0061D780 stay (Rebirth-patched; the draw is called by address). Layout and regions in
  ui_screens.h. The helpers call each other through their addresses (wrappers run).
- `CMasterRequestBox` 0061E2F0 `Construct`, 0061E4E0 `Update`; `net::MasterLink` moved to net_classes.h.
- The list's `vector<MpAddressEntry>` members: 0061E5A0 push default, 0061E660 insert default, 0061EA90 erase, 0061EE80 /
  0061E8F0 / 0061E8A0 the entry's copy, assignment, destructor. Forwarded (library): std::sort's 0061EF60 / 0061F6F0 /
  0061FB00, `_M_insert_overflow` 0061EC40; 0057E970 (the game flow joins a server, not rewritten).
- Differences: the original's default entry temporaries leave players, ping, game type, foreign, state and password as
  stack bytes; ours zero them. All but the new game row's password flag are overwritten by RebuildList; that flag (read only
  by the list draw) is 0 in ours.
- Kept: an empty list in SelectAndScroll selects -1 and reads the entry before the list; the join password answer
  (0x50001) does not check the selection; Connect joins a password server at once with no password and again from the box;
  RebuildList sets the scroll bar's position again and then scrollTop_ 0; the master box steps its link up to three
  times a frame.
- Small ones (`verify_ui_small_slots.cpp`): 005FD760 `CUiSurface::Freeze` (O; hero screen, menus), 0060E210
  `CUiItemPanel::ConstructBlank` (O; camp), 005EACB0 `CCameraInput::Construct` (O; every newgame), 005E7010
  `CUiMesh::GetPlacement` (S, CheckNear; global map, portraits), 005E6B90 `CScreenFade::Start` (O), 00638A10
  `CBattleScreenSetup::OnDeactivate` (O; a window over the battle screen), 0060FA80 `CCampPages::HeroRecord` (S, slot 64),
  005ED710 / 005ED780 `ItemTurn::Clone` / `DeletingDestruct` (O), 005EA850 the base input side's deleting destructor (O).
  Left in the range (not Rebirth-patched): STLport-like list / vector plumbing (005E38F0-005E3B60, 005F6080, 005FAF30,
  00632490 / 006324E0 twin assignments of a 0x18-byte record with two CStrings), screen 9's slot 42 0062D100, the static
  initialiser 005F46A0, functions under 32 bytes.

**Next in 005E0000-00640000:** the largest Rebirth-patched ones (vanilla only): 0060C870 (camp Create), 00613EA0 (hero
screen Create), 0063A560 (battle keys); the connection screen's Create 0061B2C0 and list draw 0061D780. The address list's
std::sort and vector plumbing (0061EC40-006200D0) are in library.csv (A-I21).

## 4. Main menu, dialogs, escape menu, credits

Scenarios `ui_main_menu` (regions 3, 1, 0, 4), `ui_container_input` (Esc on the menu, Esc on the quit box), `ui_screens`
(main menu region 5, the hero screen's list keys and rotate buttons, Esc back; then a new game and the escape menu: hover,
resume, Esc, the quit box cancelled), `ui_view_screen`. The harness's `newgame` bypasses the difficulty question.

- **Main menu** (click 00632D70, regions from the `MainMenu` layout in `menus.reg`): 0 new game (difficulty question:
  message box 0x184 bytes, "option_difficulty"), 1 load, 2 exit ("Movies\Exit.bik" through 005DB3F0, then 00455F20(0)), 3
  options, 4 credits, 5 multiplayer hero screen.
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
  "Movies\Titles.bik".

| Address | Name (file) | V |
|---|---|---|
| 00632D70, 006326B0, 006335E0, 00632B10, 00632910, 00632940, 00633400, 00633540 | `CMainMenu::OnLButtonDown`, `Create`, `AddVersionLine`, `Update` (rain at 0 / 7 o'clock, night lightning 1 in 41 per 100 ms), `Close`, `OnMessageResult`, `OnMouseMove`, `OnKeyDown` (`ui_main_menu.cpp`; a CUiContainer) | O |
| 005F51F0, 0062AC90 | `CInterface3D::ResetSavedState`, `CUiRegion::SetRectFrom` | S |
| 00634650, 0063B5D0, 0063FE70, 00634790, 006337B0, 0062B7D0 | `CMessageBox::Construct` (RET 0x18), `CObjectivesWindow::Construct` (RET 4), `CHudPortraits::Construct` (RET 8) (`ui_screen_constructors.cpp`), `CMessageBox::Build`, `CDifficultyBox::Build`, `CCreditsScreen::Build` | O |
| 0062AB20, 0062AE90, 0062AD10, 0062B300, 0062ACC0 | `CEscMenu::Build`, `OnLButtonDown`, `OnMouseMove`, `OnKeyDown`, `OnMessageResult` | O |
| 00634D20, 00634E60, 00633DD0, 00633EC0 | `CMessageBox::OnLButtonDown` / `OnKeyDown`, `CDifficultyBox::OnLButtonDown` / `OnKeyDown` (`ui_message_box.cpp`) | O; check boxes S |
| 0062BD80, 0062BD00, 0062BCB0, 0062BFE0, 0062C010 | `CCreditsScreen::Draw` (next line once the time passes `nextLineAt_` and the text stopped: `speed_` 0 or NaN), `OnClose`, `TitleLine::Destroy`, `OnLButtonDown` / `OnKeyDown` (`ui_credits.cpp`) | O |
| 0062D090 | `CExchangeScreen::OnKeyDown` | O |
| 0062A830, 0062AA50, 0062AA40 | `CViewScreen::Build`, `DrawView`, `OnClose` (`ui_view_screen.cpp`): screen 7, a debug view (0x168, 00740840 / 0074080C): inert input 00741154, `UnitPickInput`, camera keys; focus (30, 30), rotation premultiplied by 225 degrees about x | O |
| 005EB8C0, 005EB910, 005EB8F0, 005EB940, 005EBB40, 005EBB80, 005EBBC0 | `UnitPickInput` (00740278, 0x20: a CDWordArray of picked ids): `Construct` (S), `Destruct`, `DeleteThis`, `OnLeftDown` (pick / walk; Ctrl: attack, how 6), `OnRightDown`, `OnKeyDown` (Space toggles the server pause flag), `OnKeyUp` (no unit can be picked in ui_view_screen: the party leaves the world with the request) | O |

Kept: the main menu passes uninitialised stack bytes as the containers' allocators (ours 0); the Esc menu click stores the save
/ options flags through the new window without checking the allocation and activates a null quit box (FatalError) when that
fails; the main menu's load and options windows and the message box's edit field are written without checking the
allocation; the message box with neither button flag shows a cancel button behind an empty region 0; the difficulty question
writes its tip text even when "tip option_difficulty" is missing; the message box's OK copies the edit text without a length
check; the credits pass the address of +0x16C as uiParam with a null pvParam, so smoothing is never restored. Lessons:
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

Global map and camp pages (O unless marked):

| Address | Name (file) |
|---|---|
| 0062D350, 0062F050, 00631110 (S), 00631030 (S), 00630F50 (S), 00630E00 (S), 00630CC0 (S), 00630860 (S), 0062EE90 (S) | `CGlobalMap::Create` (6.7 KB), `Update`, `AllodTitle`, `ZoneTitle`, `QuestTitle` (`ui_global_map_places.cpp`), `ZonePosition`, `QuestPosition`, `SelectZone` (RET 8, flag is a byte), `SetWeather` |
| 00630670, 006312C0 (S) | `CGlobalMap::RefreshSelection` (zone markers alone in colours 0xFF181818 + 0x101010 i through a frame step 00458010(0, 0) into the backdrop; the click reads the zone back from the pixel), `FindPaths` (RET 4, recursive way search; `arrivals_` = stlp::map<int, float> exit -> shortest "#passtime" sum; `ui_global_map_paths.cpp`) |
| 00631CC0 | `GlobalMapZonesSetAtGrow` (`CArray<GlobalMapZone, GlobalMapZoneElements>`, elements 005FBF50 / 005FBD30) |
| 0062FF60 (S), 0062FFC0 (S), 0062FF10, 00630580, 0062EE30, 0062EDD0 | `CGlobalMap::OnRightDown` / `OnRightUp` (the drag; motion 007C7C60), `OnDeactivate`, `OnKeyDown` (Esc menu inline, '5' / '6' quick save / load "saveq"), `OnMessageResult`, `Close` (`ui_global_map_input.cpp`) |
| 00635250 | `CBattleScreenSetup::Create` (11 panels, 2 input handlers; every scenario) |
| 0060F470, 0060F5B0, 0060F600, 0060E900, 0060E980 (S) | `CCampPages::OnLButtonDown` / `OnLButtonUp` / `OnKeyDown`, `OnMessageResult` (0x10001 takes the first weapon back), `HelpTopic` (`ui_camp_input.cpp`; `turning_` +0x25C) |
| 00611930 (S), 006100E0 (S), 0060E9E0, 0060F050, 00612600, 00612290 | `UpdateAction`, `CanEquip` (slot 43, AL), `ShowPage`, `Draw`, `CanMakeItem`, `CanMakeSpell` |
| 0060E6B0, 0060FE40, 0060E3E0, 0060FCA0, 0060F9E0, 0060F980, 0060F6B0, 0060F710 / 0060F7A0, 0060F830, 00610AD0, 00610920 | `Close`, `TakeOff`, the trade panel's destructor body, `PutOn`, `RefreshWeapons` (46), `RefreshStats` (47), `Update` (turn via `_CIfmod`), `NextHero` / `PreviousHero`, `ShowHero`, `MoveItem` (42, RET 8), `PutOnFromStrip` (RET 4) |
| 00610020 (P), 006110F0 | `CCampScreen::BestPartySkill` (45), `ShowPageTabs` (44) |
| 00611220, 00611760, 00612500, 006129C0 | `CCampPages::DoAction` / `UndoAction` (trade pages stock the constructor's class template (+0x198) lists 0069908x-006992xx with the sold records; cost_ off except page 1), `DeconstructSpell` ("Prototype" + a "Rune" per modifier), `DeconstructItem` ("Instruction Weapon / Armor / Quick Item", "material.<key>" built with += instead of two operator+, a "spell container" through 00613820 instead of the inlined map insert) (`ui_camp_deals.cpp`; ui_camp_deals sells and takes back) |
| 00628B50 (S), 006298B0 (S) | `CConstructorPage::SpellMakerItemAt` / `ItemMakerItemAt` (`ui_constructor_slots.cpp`; ui_camp_deals pages 3 and 5) |

Hero panels, trade panel, strips (O unless marked):

| Address | Name (file) |
|---|---|
| 00623880, 006233C0, 00622DB0, 00622000, 00622A80, 006232D0 (S) | `CHeroStatsPanel::DrawStats`, `ShowStats`, `Build`, `CHeroEquipPanel::Build`, `CHeroQuickStrip::Build`, `CHeroStatsPanel::SetHero` (+0x170 = height * 100 truncated: an edge height may differ at another precision) |
| 00624CB0 (S), 00625690 (S) | `CHeroStatsPanel::HoverInfo`, `CTradePanel::HoverInfo` (`ui_camp_stats.cpp`) |
| 00624790, 00624A60 | `CHeroStatsPanel::OnLButtonDown` (page 10's -/+ regions 6-13; a hold starts at -0.5 s with the capture; regions 0 / 1 previous / next hero), `Update` (repeat after 0.25 s) (`ui_stats_buttons.cpp`; ui_hero_stats, coordinates unconfirmed) |
| 006242E0, 00624590 | `CHeroStatsPanel::DrawRow`, `DrawPart` (armour "%.1f" = the largest of the first six damage types' three-layer sums; a NaN keeps the old maximum) (`ui_stats_rows.cpp`) |
| 006229E0, 00622A30, 00622960, 00622860 (S), 00622BC0, 00622D40 (S) | `CHeroEquipPanel::ShowWeapon` / `ShowArmour` / `OnRightDown` (does not mark the event used) / `ItemAt`, `CHeroQuickStrip::ShowEntry` / `ItemAt` (`ui_equip_panels.cpp`) |
| 00622C10, 00627C90, 006284A0, 00628F80 | `CHeroQuickStrip::Draw`, `CRepairGrid::Draw` (models at depth 2^(3 - row) * 6), `CConstructorPage::DrawSpellMaker` / `DrawItemMaker` (`ui_panel_draws.cpp`; highlight p = (cos(5 t) + 1) / 2 stored as float, colours truncated and clamped: an integer edge may differ at another precision; ui_camp_deals pages 3, 5, 6) |
| 006270A0, 006279E0, 006280C0, 00628CC0, 00627920 (S), 00628060 (S) | `CTradeGrid::Build`, `CRepairGrid::Build`, `CConstructorPage::BuildSpellMaker`, `BuildItemMaker`, `CTradeGrid::ItemAt`, `CRepairGrid::ItemAt` (`ui_hero_panels.cpp`; lists are `UiItemList` = `lib::AlignedArray<CItem*>`) |
| 00625880 (S), 006261A0 (S, pixels), 00624F10, 00625430, 00626F10 (S), 006255A0, 00625860, 00626FA0 (S) | `CTradePanel::BuildLists`, `DrawLists`, `Build`, `ResetRecords`, `SnapshotRecord` (floats copied as dwords), `Settle` (the all-branch tests the low byte of `keep`), `SelectHero`, `RestoreRecord` (HealSlots by old hp / old max * new max - Reduce(new max), the maxima stored as float) (`ui_trade_panel.cpp`; ui_trade_restore) |
| 00626D70 (S), 00626920, 00626C30, 00626C50 | `CTradePanel::ClampScroll`, `OnLButtonDown` (regions 0-3 scroll, 4-9 a school level, 10 science, 11-33 a learnable perk, 34 up a held perk), the left-up and held-row update (`ui_trade_panel_input.cpp`; ui_trade_hold with `ldown` / `lup`, purchases only in the Rebirth run) |
| 00625530 (P), 00625560 (P), 00626090 (S) | `CTradePanel::Experience`, `PurchaseCost`, `FormatPrice` ("%d" / "%.1fk" / "%dk" / "%.1fm" / "%dm" / "%.1fg", negative as 0x7FFFFFFF) |
| 00620380 (S), 00620960 (S), 00620F10 (S), 00621010 (S), 00620E10, 006210F0, 006212A0, 00621420, 00621510, 00621530 | `CUiItemStrip::Build`, `SetTab`, `OnMouseMove`, `OnLButtonUp`, `OnLButtonDown`, `OnWheel`, `OnLButtonDownForward`, `SetFilter`, `SelectTab`, `CycleTab` |
| 006218F0 (P), 006219A0 (P), 00621A80 (P), 006212F0, 00621F30 (S), 00620A50 (S), 00621B50 | `ItemTab`, `SpellTab`, `ArmorTab`, `RebuildList`, `ItemAt`, `Update` (scroll physics, 4 ULPs), `Draw` of the forwarding strip (camp and exchange) |
| 00621160, 00629A40, 00629AF0, 00629AD0 (S), 00629DB0 | `CUiItemStrip::Setup`, `SetupFigures` (name list shares +0x194 with `filter_`), the figure strips' (00741BF4) `DrawFigures`, `CountFigures`, `OnFigureClick` (`ui_item_strip_setup.cpp`; ui_hero_screen mode 10, unconfirmed that its clicks land) |
| 005EE050-005EE130 (P), 005EE1B0, 005EE2C0, 00621570 | `UiItemKinds::IsSpell` / `IsModifier` / `IsSpellContainer` / `IsPart` / `IsKindOne` / `IsBuildable`, `CItemScreen::MoveItem` (RET 0x18), `ReturnAll` (RET 8; `ui_item_screen.cpp`), `CUiItemStrip::ScrollToItem` (RET 4) |

Multiplayer hero screen (`CMpHeroScreen`, `ui_hero_screen*.cpp`, `ui_hero_figure.cpp`; O unless marked). Modes: 9 hero list
(rows 9-18; 1 continue 006183B0, 5 new hero, 6 rename, 7 delete "lmp_deletepers", 8 the hero's inventory = the camp screen
built inline with `fromHeroScreen_` 1), 10 new hero (1 create 006186F0, rows 9-19 the voice), 11 equipment (1 next 00617A50).
Scenarios ui_hero_screen, ui_hero_stats (RefreshHero), ui_hero_delete (AddNewHero, DeleteHero, record life cycle; unconfirmed).

| Address | Name |
|---|---|
| 00618FF0 (P), 00619090 (P), 006193C0, 006188E0, 00618980, 00616900, 006177D0, 00617050, 00618A10 | `ClampHeroSelection`, `ClampVoiceSelection`, `EquipmentWeight` (53, double in ST0), `CanEquip` (43, AL), `OnItemChosen` (42), `GetHero` (51), `OnLButtonUp`, `OnLButtonDown`, `OnKeyDown` (`FinishRename`: the name before " \| " plus " \| " and the typed text; paging by 10 / 1 heroes or 11 / 1 voices) |
| 00618060, 00619150, 006183B0, 00618850, 006186F0 | `Rename` (Mid 006FD9B4), `PlayVoice` (prototype -> sound set map 007C8A6C -> a random entry, flags 0x2282; ours counts the entries instead of building the temporary vector), `Continue` (builds `CMpConnectScreen` 00741CD0 inline), `RefreshHero` (47), `CreateHero` (attribute sum vs 00741C98 equal or unordered, a name); forwarders `RenameAt` / `PlayVoiceAt` / `ContinueAt` / `CreateHeroAt` |
| 006159E0, 00617820, 00615850, 00633260, 00615800, 00634500, 00634560 | `Draw` (preview at depth 553 / 400), `Update` (turn fmod 007C7F10; create button tint), `Close`, `Destroy` (the hero screen's destructor body; vtable 0074214C slot 0 00633240 calls it), `MpVoiceEntry::Destroy`, `ReleaseCachedObjects` |
| 00619540, 00619560, 00613CF0, 0062A7C0, 00617E40, 00615940, 0061A350, 00617A50 | `MpHeroRecord::Construct` / `Destruct`, `TradeRow::Destroy` / `DeleteThis`, `DeleteHero`, `OnMessageResult` (0x10001 deletes; mode 9), `MpHeroRecord::Assign`, `AddNewHero` (file "<highest *.mp title + 1>.mp"; 00619920 when full; `RefreshHeroScroll` shared with DeleteHero) |
| 006167C0, 00616560, 00616910, 00616E20, 00616AC0 | `DrawHeroList` (a hero record is a CParty whose first member's text 0 is the name), `DrawVoices`, `ShowHero` (RET 4), `MakePreview` (RET 4; skill animation 0x21 from a random frame, controller slot 14 `RestartAnimation(float)`), `ChooseFigure` (52: +0x1BC / +0x1C0 are two item strips whose `selected_` (+0x17C) is the chosen figure; the strip listing the prototype picks the voice list; the voice (text 6) is the list's first file, read even when the list is empty) |

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
  and so must its wrapper (its log once faulted at 0x44). Hero skills: 00527EB0's result is stored as float then FISTP; the mana
  maximum (00523F50) is stored as float, divided by the period (00523CD0, unrounded) and truncated (`StoreFloat`).
- **MoveItem**: with `limit` > 0 (and at least that many items in the target) it moves only an item the same as one of the
  target's first `limit` items, and then gives no turning state; ScrollToItem (the camp's "AddItem" call sites) does not update
  `count_`; its second loop inlines the array's SetSize.
- **Trade panel** (vtable 007417A8; the camp's +0x1F4, the hero screen's +0x1C4): rows of 0x18 bytes (STLport vectors); perk text
  keys are the record's +4 name without trailing "123" plus "0"; held perks are a level byte per group of three skill records
  (007AF268, 0x44 bytes). **The hero screen's rotation state is window +0x1E4 (the turn +0x1E0), not +0x150**: `rotating_` was once
  declared at +0x150 and the preview kept turning (fixed).

Kept: `FindPaths` leaves `beyondTarget_` (+0x1DD) set when the target zone it went beyond is already on the way; the global map's
marker array templates have uninitialised numbers (ours 0); `ZonePosition` / `QuestPosition` leave uninitialised stack in `out`
without a map.txt entry (ours leave `out`); the camp Draw passes hover info to 005F1590 with uninitialised bytes +4..+0x13 unless
a panel fills them (ours zeroes); `CanMakeSpell` keeps the modifiers in an 8-entry stack array without a bound; `CanEquip` on page
1 reads the container's spell (+0x44) without a null check; `CanMakeItem`'s item type stays unset without a part; `SpellTab` /
`ArmorTab` read the record through null when the index is out of range; `ClampVoiceSelection` with an empty voice list reads the
entry before the vector; MoveItem's turning state keeps its glow (+0x14) uninitialised, writes its count before the allocation
check and faults on a null AddItem result; BuildLists pushes copies of a row whose price, level and flag are uninitialised stack
(ours 0; a held perk at the last record reads the next record's name through null), `DrawLists` leaves the third school's row
empty, SnapshotRecord leaves +0x2B and +0x39..+0x3B unwritten; a held purchase does not mark the record bought nor reset the hold
time; the trade grid's sold-list topic follows buyKind_ too; `PlayVoice` and `MakePreview` hand records over through 005789C0.

Not rewritten: 00612CF0 (section 3, "Next"); the exchange window's Create / Draw and the lobby screens. The trade grid draw
00627390 is in `ui_trade_grid_draw.cpp` (renderer.md).

### Briefing / base screen (`ui_briefing*.cpp`, `ui_briefing.h`, `ui_person_texts.cpp`)

Screen 3 of a "brief" zone (zone_base_bz1g: Create 006024B0, Update 00604130, Draw 00602BF0). States +0x16C: 0 walking, 1 topic
choice, 2 dialogue starting, 3 phrases, 4 rewards. Scenario `ui_base_dialogue`: the leader walks to Smith (MoveToObject),
`clickunit Smith`, region 0 (the first topic), waits 4 s, clicks through the phrases, Esc, a second Esc takes the rewards. A
person can talk when its server controller's command is 0 or 0xB and the next one 9 (or it has none). All O unless marked.

| Address | Name |
|---|---|
| 006024B0, 00604130, 00602BB0, 00606F40, 00603010 (S), 00608C70, 006030C0, 00603090, 00602490 | `Create`, `Update`, `Close`, `OnMessageResult` (the load window inline), `OnUnitLeft` (30), `OnGameOver` (31), `OnBriefing` (34, "briefing <name>"), `ShowChat` (36), `SaveState` (40: one zero dword) |
| 006030F0, 00603360, 00608B60, 006071E0, 00603D00, 006034A0 | `OnLeftDown`, `OnLeftDouble` (nothing while the chat window is active), `OpenTopics` (five rows 210..570 x 440.., "goodbye" appended), `DrawTopics`, `OnMouseMove` (cursors 0xD / 0xE / 0xF), `OnKeyDown` (`ui_briefing_keys.cpp`: action 4 the objectives window when a "q.<zone>.<quest>" = 1 lies in this allod, 0xB camera to the leader, '5' / '6' "saveq") |
| 00607360 (S), 006078C0 (S) | `FindTopics` ("b.<person>.<topic>" = 1, bit compare; a network client sorts them; the last "constr..." topic last), `TopicTitle` ("constr..." -> "briefing constr") (`ui_briefing_topics.cpp`) |
| 006064E0, 00608210, 00607A60, 00607BB0 (S), 00607EC0 (S), 006062C0 | `StartDialogue(immediate, remember)` (`ui_briefing_start.cpp`), `StartPhrases` (camera saved to +0x1F8, look-at +0x204, 0066F840(3, 0) to the server), `PlaySpeakerAnimation` (the controller's +0x978 callback 006023F0), `ViewSpeaker` (message 0x0F), `ViewPreset` ("#camera n"), `HideSpeaker` ("#hide") (`ui_briefing_dialogue.cpp`) |
| 00602C90, 00607720, 00608450, 00604C50, 006089D0, 00605DE0 | `OnUnitJoined` (29; the leader's "b.<zone>" topic starts at once), `PersonTextMap::Load` (also the battle screen's setup), `FinishDialogue` (completes "b.*.<topic>" through 00592090's address), `NextPhrase(skip)`, `MovePerson` (RET 0x28), `ShowItem` ("#show") |

Layout: speakers +0x1A4 hero, +0x1A8 person, +0x1AC third; places_ +0x1B0 / directions_ +0x1D4 ([0] person, [1] leader, [2]
third), thirdPlace_ / thirdDirection_ +0x210 / +0x21C, personHome_ / personHomeDirection_ +0x228 / +0x234, firstPhrase_ +0x244,
dialogueText_ +0x248, speaking_ +0x254, shown_ +0x278 (BriefingShownItem list), immediate_ +0x27C, autosave_ +0x27E, people_
+0x284 (stlp::vector<int>), markPhase_ +0x290, speakers_ +0x294 (12-byte BriefingSpeaker), text_ a CUiScrollText;
`BriefingRecord` 0x6C (exp, gold, four name lists, specials +0x68). The "constr" topic's number is `Mid(7)` (006FD9B4, not
Right). Helpers: `BriefingVariables` (00578C40 into a 100-bucket table, 00581790; walked with 005E0DB0), `BriefingPlayerName`
(00578A90). player_messages.h cannot be included next to ui_briefing.h (three MfcArray templates), hence the forwarders
(originals_ui.h) and `player_out_mark.h`. Inlined in the original, not functions: 00609A80, 0060B490 / 0060AC00, 00609A40 (pool
0x30). 00608D10 (slot 35, `MOV AL,1; RET`) is too short to hook; 006023F0 is no function in Ghidra or function_sizes.inc.
Kept: the mouse move's exit check names the zone of exit_ (+0x164), not of the exit under the mouse; the key handler builds the
objectives window inline and calls its setup even when the allocation failed; Create reads the map record's camera circle
without a null check; the people walk stops at an entry with a null object; "#show .. weapon" shows the item twice (flag 0,
then 1; ShowItem's third argument is not read); the camera / animation loops compare the speakers with the last split line's
second word unchecked; the new speaker's flag byte is uninitialised (ours false); the server player 0 is not null-checked; the
leader's server unit gets flag 0x40000000 when the leader is shown. Left out as unused: Update's sort / unique of the marked
ids, the unused party searches (00578BA0) in HideSpeaker / FinishDialogue, OnUnitJoined's server-world lookup.

## 6. Battle screen: update, selection, orders, pending actions (00635250-0064BE80)

Files `ui_battle_*.cpp`, `ui_hud.cpp`, `ui_hud_pick.cpp`, `ui_hud_tips.cpp`, `ui_hud_highlight.cpp`, `ui_spell_column.cpp`;
scenarios `ui_hud`, `ui_battle_commands`, `ui_battle_spells`, `ui_order_move`, `ui_quick_items`, `ui_quest_notice`. The key
handler 0063A560 is Rebirth-patched (the action table 0079BF70: 1 pause, 2 / 3 game speed, 4 objectives, 0x17-0x1A movement
modes, ...; Esc cancels a targeting mode (+0x17C) or opens the Esc menu). The Rebirth-patched callees 005E70E0, 005E7680,
005E7D60, 005FDE20, 005F8340, 005FC9E0, 0063BCC0, 0063C050, 0063E4B0, 0063F500, 00638EF0 are called through their addresses,
and so are the rewritten ones with wrappers (00637DC0, 006379E0, 00637190, 006373A0, 00637500, 00637750, 0063CEE0, 00639EF0,
0063EC90, 0063FB50, 0063DD70).

| Address | Name | V; scenario |
|---|---|---|
| 00638280, 00637DC0 | `CBattleScreenSetup::Update` (every frame), `RefreshSelection` | O; every game scenario |
| 0063D2B0, 0063E820, 0063C180, 0063F800, 0063CEE0, 00639EF0 | `CHudQuickItems::SetUnit`, `CHudSpellColumn::SetUnit`, `CHudWeaponBar::SetUnit`, `CHudActionColumn::SetUnit` (RET 8), `CHudQuickItems::ItemTip` (RET 8), `CBattleScreen::HoverUnit` (RET 0xC) | S; every game scenario |
| 00639D60, 0063B920, 00638B30, 00638A40 | `OnMouseMove`, `UnitName` (RET 8), `OnLButtonUp` (O), `OnLButtonDown` (O) | S; ui_hud, world_client, ui_describe |
| 00635080, 00635E60, 006360F0, 00636150, 006364E0, 006361B0, 00635EB0, 006365E0 | `SaveState` (40; S through a recorder), `Close` (26), `PollShown` (35, AL), `OnItemsPicked` (28), `OnUnitLeft` (30), `OnUnitJoined` (29: placed by the local player's record order, selected by the record's +0x778), `OnMessageResult` (27), `OnGameOver` (31) (`ui_battle_slots.cpp`) | O; every new game ("savea", the hero joins); 27 / 28 / 30 / 31 not known |
| 00636F10, 006379E0, 00637190, 006373A0, 00637500, 00637750 | `OrderAt` (RET 0x14), `ToggleMember` (RET 4), `AttackUnit`, `OrderDeadUnit` (RET 8), `UseOnUnit` (RET 0xC), `UseAtPoint` (RET 0x14) | O; ui_order_move, ui_hud, ui_battle_commands, ui_battle_spells, ui_quick_items |
| 006366F0, 006369D0 | `OnUnitSpeech` (32), `OnQuestVariable` (33) | O; any speech; ui_quest_notice |
| 0063CD20, 0063E750, 0063F730, 0063E7C0, 0063F7A0, 005E7060 | `Highlight` / `ClearHighlights` of the quick items, spell and action columns, `CUiMesh::SetScale` (callers use `HighlightAt` / `SpriteSetScaleOriginal`) | S |
| 00638AD0, 0063B6D0, 0063B770 | `OnLeftDouble`, `OnKeyUp` (the order key of actions 0x11..0x16, `orderKeys_` +0x18D in the jump table's order 0, 1, 5, 4, 3, 2), `OnWheel` (map, unit panel, message log, top panel) | O; ui_container_input, ui_hud |
| 0063DB20 (O), 0063EC90, 0063FB50, 0063DD70 | `CHudQuickItems::OnMouseUp`, `CHudSpellColumn::Click`, `CHudActionColumn::Select`, `CHudQuickItems::Pick` | S; ui_battle_spells, ui_quick_items; 0063A560's spell keys call them by address |
| 0063EF60, 0063E020 | `CHudSpellColumn::ClickAlternate`, `CHudQuickItems::UseNow` (an immediate cast on the unit, or for an attack spell (0068B770) on the nearest unit it sees; none: "nomagic") (`ui_hud_pick.cpp`) | O; `dblclick` in ui_quick_items |

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

Kept: a wand or scroll without a spell becomes the pending slot with mode 0 and no highlight; `UnitName` does not test the
prototype for null, the mouse move not the unit panel; the weapon bar keeps one tip CString for all weapons (004E0150
overwrites it), the quick item tip passes one CString as description and kind; the spell column sets the tooltip of region
`index` for spell slot `index` (a gap shifts them); the action column's builder is a loop of one pass over three buttons;
HoverUnit passes a dead unit with flag 0x400000 on to the action checks; Update passes the portraits a dword whose low byte only
is set; AttackUnit's order kind is the index of the first set byte of +0x18D..+0x192 (6 for none); ToggleMember's markers get
0x10 (not 1) for members that stay selected with +0x7D and a negative index reaches its array write unchecked; the quest notice
posts a bare colour-code line for "q." names that are neither three nor four parts; `OnUnitJoined` appends a unit without a
record but marks the portrait at the count of records matched; `OnMessageResult` (0x20001) writes the load window's flags
without checking the allocation. **Tooltips for regions without a button** get SetTip(0, 1, 0), a fresh 0x184-byte tooltip with
the empty text 0079B298 whose constructor 0065E320 writes the arguments (+0x164 / +0x168) and size (+0x174 / +0x178) only for a
non-empty text: leftover heap bytes, compared only for tips with lines. FCOMP / `TEST AH,0x40` compares treat NaN as equal
(damage "Unknown" when all weights are zero; quest values 2 and 3).

Not exercised: the battle screen's drag (no drag step in the harness), mode 1 orders and the toggle with Shift (the harness cannot
hold Shift during a click), the exit flag, RefreshSelection with several units, the attack kinds 0..5 and the level-gap speech,
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

| Panel / group | Addresses (S = side by side; the rest O) |
|---|---|
| `CHudWeaponBar` | OnMouseDown 0063C6D0 S, OnMouseMove 0063C7D0 S, OnMouseUp 0063C710, Update 0063C600, Draw 0063C4F0 |
| `CHudQuickItems` | OnMouseDown 0063DAA0 S, OnDoubleClick 0063DAE0 S, OnMouseMove 0063E270 S, Update 0063D9D0; Draw 0063CDC0 (= 0063F190, 0063FD50 of the columns) |
| `CHudSpellColumn`, `CHudActionColumn` | OnMouseDown 0063F1E0 / 0063FDA0, OnDoubleClick 0063F220, OnMouseMove 0063F2D0 / 0063FE40 (S); OnMouseUp 0063F260 / 0063FDE0 |
| `CHudPortraits` | OnMouseDown 00641CE0, OnMouseUp 00641E20, OnMouseMove 00641EA0; Rebuild 006407E0 (every newgame), AddPortrait 00640060 (RET 0x10, through its address), ShowFace 00640670 (spells_cast low mana, hits) (`ui_hud_portrait_setup.cpp`) |
| `CHudMap` | OnMouseDown 00645060, OnMouseMove 00645610 S, OnWheel 00645640 S |
| `CHudUnitPanel` | OnMouseDown 00648330 S, OnMouseMove 006484C0 S, OnWheel 006484F0 S, OnMouseUp 00648370; ShowHealth 006477E0 S, ShowParts 00647B70 S (4 ULPs) (`ui_hud_panels.cpp`); `DrawUnitInfo` 00647040 (from 006462A0, Rebirth-patched) |
| `CHudClock`, `CHudMovePanel`, `CHudTopPanel` | OnMouseDown 00648D30 / 00649630 / 0064A1C0, OnMouseMove 00649160 / 00649750 / 0064A2C0, OnWheel 0064A2F0 (S); OnMouseUp 00649670 / 0064A200, MovePanel Update 00649550, `ToggleFlag` 00649810 (orders through the local view 0066AF30); top panel `ShowPage` 0064A320 S, `Layout` 00649E20 S, `Build` 00649AF0 (disabled in tests/verify/disabled.txt, X17), `AddPickedUp` 0064A4E0, `AddMoney` 0064A630 (no scenario known), `ShowItems` 0064A7A0 (`this` the top panel in ECX; Ghidra mislabels it) (`ui_hud_top_panel.cpp`) |
| per frame (S, state put back) | `CHudMap::BuildMarkers` 00643DC0, `UpdateView` 00642E80, `CHudPortraits::Update` 00641080, `CHudClock::Update` 006488E0, `CHudTopPanel::Update` 00649FF0 |
| destructor bodies | map 00635CA0, unit panel 00635D00, top panel 00635E00, weapon bar 0063BC70, quick items 0063CA70, spell column 0063E460, action column 0063F490 (`ui_hud_destroy.cpp`; inlined MFC array destructors write the array's vtable, then delete), portraits 0063FF70; `PartyIds::Equals` 006381E0 (P, every frame) |

Rebirth-patched (vanilla-only hooks; callers use the original addresses): 00630010, 00636020, 00638EF0, 0063A560, 0063BAB0,
0063BCC0, 0063C050, 0063C800, 0063CAE0, 0063E2A0, 0063E4B0, 0063F300, 0063F500, 00641F20, 00642930, 00642B40, 00645210, 006456E0,
00645F80, 006462A0, 00647FB0, 00648520, 00648D70, 00649190, 00649910, 006567A0, 00659420, 0065C2E0, 0065E580. 006407E0 holds
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

| Address | Name (file) | Scenario |
|---|---|---|
| 00654660, 006547B0, 006544A0, 006545E0, 00654430, 00654450, 006548A0, 00655430, 00657820 (P), 00655050, 006577B0 | `CSaveLoadWindow::OnMouseDown`, `OnDoubleClick`, `Draw`, `Update`, `CloseDialog`, `OnDialogResult` (the caller 005F590D ignores EAX), `OnKeyDown` (Esc: 0x100001 / 0x110001), `ShowSelection`, `IsReservedSave`, `OnChar`, `TakeSelectedTitle` | ui_hud, ui_saveload |
| 00655290, 00655380, 00655A10, 00655B20 | `Confirm`, `AskDelete`, `LoadSelected`, `DeleteSave` (looked up through the game's GetFileAttributesA import, which harness runs redirect to harness_saves; X23); called by address (Call*) | ui_saveload, ui_saveload_delete |
| 00653A50 (S), 00653BD0 (S), 00653CD0 (S) | `SaveEntry::MakeTitle`, `ZoneTitle` / `AllodTitle` ("zone " / "allod " with the space, 007941F8 / 00794200: without it every title was the fallback, X23; else "save_camp" / "Unknown") (`ui_saveload.cpp`) | ui_saveload |
| 006570E0, 006575B0 | `QuickSave` (`__fastcall` slot, DL frame step), `QuickLoad` | newgame's savea |
| 00653DD0, 006503C0, 0062B180 | `CSaveLoadWindow::Build`, `COptionsWindow::Build`, `DestroyOptions` (`bindings_` / `settings_` are stlp::vectors, `groupNames_` an mfc::CStringArray) | ui_hud, ui_main_menu, ui_screens |
| 00650C20, 00650D70, 00650920, 00650A20, 006508E0, 00650900 (P), 00650A30, 00650E40 | `COptionsWindow::OnMouseDown`, `OnDoubleClick`, `Draw`, `Redraw`, `OnDialogResult`, `HelpTopic`, `Update`, `OnKeyDown` | ui_hud |
| 00651350, 00651D70, 00651940, 00651600, 00652200 (S), 006524A0 | `RefreshRows`, `ShowGroup` (RET 4), `BuildSettings`, `BuildBindings`, `LoadSettings` (sets every control twice), `Apply` (`ui_options.cpp`; `Call*` forms by address) | Build runs all but Apply (ui_hud also clicks group 2); Apply: ui_main_menu's OK |
| 0065B090 (S), 0065B360, 0065B240 (S), 0065B340 (S), 0065B8B0 | `CUiScrollbar::TakePosition`, `Update` (also the message log), `OnMouseDown`, `OnMouseUp`, `CUiSlider::SetRect` (slot 26, RET 4) | every frame |
| 0065EA50, 0065EA80, 0065D4B0, 0065D150, 0065C610 (S) | `CUiTooltip::Draw`, `Redraw`, `CUiScrollText::Tokenize`, `DrawLines` (RET 4; `ui_scroll_text.cpp`), `CUiEditField::OnKeyDown` (`ui_edit_field.cpp`) | message log; edit field: no scenario types into one |
| 0065F020 | `CUiScrollingSurface::RenderText` (the titles' RichEdit text via EM_FORMATRANGE) | ui_main_menu, render_titles_present |
| 0064BE80, 0064CC10, 0064DC60, 0064D9B0, 0064E330, 0064EA80 (S) | `CObjectivesWindow::Build`, `ShowZone`, `ShowQuest` (`__fastcall`), `DrawQuestList`, `OnLButtonDown` (`ui_objectives_input.cpp`: OK requests the zone at the nearest exit, travel time 00578700 forwarded), `LoadQuestPlaces` (map.txt "#quest <name>" pairs, or the quest record's floats for a client; the forwarder is `FillQuestPlaces`) | ui_hud's TAB, ui_objectives |
| 00658E10, 006590F0, 0065A5E0, 00659190, 006592C0 | help window: Build ("tutorial <topic>", "#screen" per page, "tutor <key>" texts), Close, the pulse, clicks, keys (`ui_help_window.cpp`; the page draw 00659420 is Rebirth-patched) | ui_help (F1) |

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
the CArray at +0x19C (next token 0065D970, not rewritten; ours calls 0065E010 SetSize for both cases).

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
per quest place, which the wrapper must allow (gz1g in a Rebirth run once mismatched). The +0x17C zone list and the distance
table (+0x178, a map<int, float> from the global map) are not reached.

Kept: options rows are indexed from the region number without a bound; save/load Left can take the caret below 0 and Right past
the name (RefreshList clamps it); options Left / Right do not set the handled flag; a key missing from the key name table keeps
the binding waiting; failed allocations of the message box or a token are written through; the options Build appends the 11
group titles without clearing the array; Tokenize's format token colour (0x1C8DC) and font are overwritten at once; the options
push_back copies a stack temporary whose non-string fields are unwritten (ours null; no definition kind needs them); DrawLines
indexes its three font heights out of range for a font above 2 (never with the game's texts); the edit field does not clamp
the caret on Left / Right (its draw 0065C2E0, Rebirth-patched, presumably does); ShowGroup draws a filled row twice; QuickSave
uses the surface even when its allocation failed; Confirm / AskDelete push the message box unchecked.

Shared forwarders: 006EAF20 `_access`, 00578D00 (ServerFlagOriginal), 00458010 (FrameStepOriginal), 005F8340
(InterfaceTextExtentOriginal). Library: SaveEntry's copy / assignment / destructor and its vector helpers 006578F0..00658020, the
0x14 / 0x18 record helpers 00653100..006535D0, the save list sort 00658030-00658C90, 0065A770, 006537E0 / 00653800 / 006518A0 /
00651CE0 (key records' destructors), the quest list sort 0064F310 .. 006500B0 (its comparison inlines 0064F550, a quest's order,
game code). Not rewritten: RefreshList 006567A0 (Rebirth-patched), 00654490 (3 bytes, too short), the tooltip's constructor
0065E320, the objectives window's 0064E5A0 (keys), 0064E8B0, 0064E990, 0064E760 (A-H19 did the other controls' constructors,
0064F550 and 0065C930: items_spells.md).

Cleanup leftovers (DR-C5): ui.h has `GameDeviceLock()` (007AEE78) while about twenty files keep a local `DeviceLock()`;
`kUnitPtrArrayVtable` (007402AC) is still spelled out in ui_battle_commands.cpp, ui_camp.cpp, ui_create_screen.cpp,
ui_hero_screen_click.cpp, ui_item_screen.cpp and player_server_handlers.cpp; ui_hud.cpp's portrait server command calls
`net::GameClient()->SendCommand` (0041D260) directly, so its wrapper does not see those calls.

## 9. Item descriptions, item view, sprite shapes, surface helpers (005E0000-00610000)

Scenario `ui_describe` (new game, GiveItem of a steel axe, an axe with a firewall spell, a potion and a rune, `screen 5`; the
mouse rests on the statistics rows, body slots, the item strip's "all" tab (region 7 at 785, 580) with `key ctrl` over the first
slots, then page 1; Esc; vanilla coordinates, under Rebirth some rows may be missed).

| Address | Name (file) | V; scenario |
|---|---|---|
| 005F1590, 005EE760, 005F0370, 005EF5D0, 005EFD90, 005F0B90, 005EF300, 005F1130, 005F2910 | `CItemInfoPanel::DescribeStats` (4.9 KB), `DescribeItem`, `DescribeBlueprint`, `DescribeCarriedSpell`, `DescribeMaterial`, `DescribeSpellRecord`, `DescribeQuickItem`, `DescribeModifierRecord`, `ShowHelp` (`ui_item_info.cpp`) | S on pixels (nested wrappers); ui_camp, ui_describe (blueprint, material, spell record, modifier: none) |
| 005ECA10 | `CItemScreen::DrawItem` (drawn twice per frame in the verify build) | S; ui_camp, ui_screens, ui_describe |
| 005E8450, 005E91F0, 005E8C60, 005E9F30 | `CUiMeshBuilder::Framed`, `Ring` (RET 0x2C), `Disc` (RET 0x24), `ResetColours` (`ui_mesh_build.cpp`; Commit's +0xC4 compared as "points at the current indices") | S (arrays poisoned 0xCD); every scenario |
| 005EE5C0, 005EE6C0, 005EE640, 005EE680, 005EE6B0, 005EE440 | `CItemInfoPanel::Draw`, `Describe`, `OnKeyDown` / `OnKeyUp`, slot 1, `Build` | O; ui_describe (`key ctrl`) |
| 005F13A0 | `ItemDamageLists` (EI_Plugin patches a JMP at +0x61: ours runs in vanilla only) | S |
| 005FD3D0, 005FD440, 005FE440, 005FD7A0, 005FD240 | `CUiSurface::CaptureScreen`, `Rgb565Copy` (the save picture), `Greyscale` ((2r + 4g + b) / 16), `SwapContents` (O), `PixelAt` (`ui_surface_pixels.cpp`) | S; ui_screens (the escape menu opens) |
| 005FCC00, 005FCDD0, 005FCF80, 005FD120 | `CUiSurface` constructors, picture window (dead), destructor body (`ui_surface_create.cpp`) | O; any window |
| 005ED7B0, 005EC020, 005EC300 | `DrawHeroFace` (EI_Plugin's fix at +0xD9 keeps the original under the plugin), `ItemFigureTexture` (256x256 from "<name>.mmp" via converter 007AF06C slot 5; locks without DDLOCK_WAIT, left held if the error report returns 0), `CUiWindow::DrawHeroPreview` (O) (`ui_item_view.cpp`) | S on pixels |
| 005EDD80, 005F2A90, 005F2F20, 005F2F90 | `CItemScreen::CreateBase`, `CItemIconWindow::Build`, `TrimItemModels` / `TrimItemTextures` | O |

Facts: the camp creates the description panel (+0x164 at 600, 100) and the help panel (+0x1DC at 0, 100); the owner (+0x14C) is the
item screen and +0x150 an item list holding a copy of the described item (00661DB0). `DescribeItem` dispatch: kind 0x3007 with +0x4C
1 material, 2 with +0x50 0-2 blueprint, 3 spell record, 4 modifier record; quick item category 8 -> 005EF300; a carried spell (+0x44)
while Ctrl is down, or a spell container (0x3008) -> 005EF5D0. Values computed by original functions are truncated straight off the
FPU stack (inline asm, the game's `_ftol`). The item screens keep model (+0x170) and figure texture (+0x180) caches (STLport
map<string, {object, last use}>, limit 30, or 20 with 00784754); each item has a turning state (vtable 007403F8, 0x18 bytes, at the
item's +0x1C) whose glow and angle wrap keep unrounded x87 values (inline asm). The sprite builders use the original's FPU sequence
for circle points and call the out-of-line array copies. `00700EFE` is `CString::MakeLower`. In the switch-over the surface
functions work on game-owned memory (renderer.md, "Interface windows").

Kept: `DescribeItem` reads the material record through null when the material index is out of range (and the prototype +0x4C
unchecked on the material line); `DescribeCarriedSpell` without an item or spell calls the surface's End without a Begin;
`DescribeBlueprint` for a kind outside 0-2 uses an uninitialised record (unreachable); `DrawItem` writes the new turning state's
count before checking the allocation; `Ring` writes two vertex records before the array when count is 0. Library: 0060BF20 /
0060C070 / 0060C0B0, 0060C390 / 0060C510, 0060A660 (STLport algorithms).

## 10. Harness steps and scenario lessons for the interface

The steps added for UI work (`screen`, `clickregion` / `moveregion`, `wheel`, `dblclick`, `clickunit`, `hudstate`, `campputon`,
`zone` / `waitzone`, `ldown` / `lup`, key names `ctrl`, `grave`, ... `enterchar`) are documented in the header of
`tests/verify/harness.cpp`. Facts behind them:
- **Picking** (`clickunit`): the client world (007AF5B8) keeps a list at +0xF0 (node: next +0, value +8 of 0x18 bytes with the
  object id at +0xC and a screen RECT at +0x10) of the objects drawn this frame (renderer.md section 3); 00565E40 (thiscall, RET 4;
  the button release 00638B30 calls it with the message's x, y) gathers entries whose rectangle holds the point (005718B0:
  PtInRect, then the object's +0x94 slot 24 finer test), sorts by depth (005659D0) and returns the first unit (type 0x50); the
  release then wants flag 0x400000. The step tries a 9x9 grid inside the unit's rectangle until the game's own pick answers with
  that unit, so the click cannot land on a unit in front; it fails when the unit is off screen or hidden. `ctrl` holds Ctrl (scan
  0x1D) around the click (SelectUnit's +0x7C branch then orders an attack, 00637190); the harness cannot hold Shift or drag.
- **Zone steps**: `zone` calls `LeaveToZone` (0066F310) on the local player's connection (server world 007AFF60, `GetPlayer`
  00594180 with player 0) as the zone exit handler (00670C20) and script command 0x8A do; it needs state 1, and in campaign mode 1
  or 3 a dead or missing leader loses the game instead. `waitzone` waits for no pending game request, campaign mode (007C2CC4) not
  2 (menu), the server world's zone name (+0x120, set by NewZone 00595480) and state 1; the party appears a little later
  (Redeploy), so follow it with `waitfor ... HP(GetLeader())`. The exit index is stored in the world player's +0xDC (zone +0xD8);
  Redeploy (00590700, modes 1 and 3) puts the party at the centre of that exit's deploy rectangle (exit +0x04..+0x10), facing away
  from the map centre unless the exit has a `#deployangle` (an index outside the zone's exits leaves it at (0, 0)); the index
  counts `#exit` blocks from 0, i.e. map.txt's `#exit n` minus 1. Zones: game `gz1g`..`gz21g`, brief `bz1g`..`bz18h`, edge zones
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

- Rebirth-patched callees are called through their original address (`CInterface3D::CallScale*`, `CUiRegion::CallSetRect`,
  `CUiMesh::SetRectangle / SetFrame / Place`, 005FDE20 text, 005F8340 extent, the panel builders, ...); rewritten functions that
  Rebirth patches itself are hookable in the vanilla install only. Read constants from the exe's memory (00742B08).
- Calls from a rewritten function to another rewritten one that a verify wrapper covers go through the original address too
  (`Call*` members, `...At` forwarders), so nested wrappers run.
- Float-heavy per-frame code (clock, top panel, strip scroll, minimap) keeps the original's operation order: values the original
  keeps on the FPU stay `double`, stores to float go through `StoreFloat`, truncations are `_ftol` (the compiler may drop a float
  store under /fp:fast), sums the original does not round are not rounded (`ItemAt`, `SetTab`), unsigned random words are loaded
  like FILD qword, NaN compares keep FCOMP / `TEST AH` behaviour.
- COM (Direct3D / DirectDraw) device methods are stdcall with the interface pushed first: call them through a stdcall helper (a
  thiscall helper once put the interface in ECX and crashed the camp's hero preview).
- Side-by-side checks of stateful handlers record the fields the original may change (handled flag, capture, panel state), put
  them back, run ours and compare; pixel checks use the settle rule (section 3).
- A function that returns its argument or `this` in EAX must too (`AddSurface`).
- A wrapper's snapshot must not follow pointers a Create sets later (the global map's weather object).
- Fields declared at a wrong offset hide bugs for a long time (`CMpHeroScreen::rotating_`): when a state seems stuck, check the
  offset in the handler that sets it against the one that clears it.
