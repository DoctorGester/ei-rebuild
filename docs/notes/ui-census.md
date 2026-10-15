# Interface drawing census (tickets N0 and N0a, 2026-10-14)

What each window surface is drawn with, how often, and who reads it back. It was measured on 26 interface scenarios, on vanilla and on Rebirth. The plan this checks is in ui-native.md (section 1 is the inventory).

## How it was measured

- **`tests/verify/ui_census.cpp`** (verify build only). It is switched on by the scenario step `uicensus` or by `EI_UI_CENSUS=1`, and needs `EI_RENDER=new,cpuwindows`.
  - It hooks the gdi32 / user32 import slots of game.exe and every module beside it, addon.dll included: text, metrics, fills, lines, pixels, blits, SelectObject, CreateFont* / CreatePen / CreateSolidBrush, and EM_FORMATRANGE.
  - It also hooks Blt, BltFast, Lock and Unlock of the surfaces' DirectDraw vtable. A Lock whose DIB hash changes by Unlock counts as a write.
  - The HDC is mapped to a surface through `dc_`, and `IsSurfaceMemoryDc` (`ui_surface_dc.cpp`) tells "a surface DC" from "some other DIB DC".
  - The surface is mapped to its window through `g_uiSurfaceOwnerObserver`, which `AddSurface` / `RemoveSurfaceAt` / `ClearSurfaces` / the destructor call (`ui_window.cpp`).
  - The whole-surface pixel helpers (CaptureScreen 005FD3D0, Rgb565Copy 005FD440, Greyscale 005FE440, SwapContents 005FD7A0, PixelAt 005FD240; `ui_surface_pixels.cpp`) report through `g_uiSurfacePixelObserver` (N0a). Our callers call them directly, and they lock through the surface's own pointer, so neither the verify wrappers nor the vtable hooks saw them in N0's run.
  - A window that wears a verify stand-in copy of its vtable for the length of a call is labelled with its game vtable (`RealVtable`; see "Heap vtables" below).
  - The DIB is hashed around every DC session and at every frame. Any change that no hooked call explains is reported as "uncaught".
  - Output: one `ui_census.csv` in each run folder.
  - **Call chains (N0d).** x86 code here has no frame pointers and no unwind tables, so `Chain()` scans the stack for return addresses. The unwritten slots of live frames hold return addresses from earlier, deeper calls (even whole old call paths), and the scan took them for callers. A scanned value is now kept only when its call is tied to the frames before it:
    - a direct call (E8) must lead into the function holding the previous frame, through the jumps on the way (E9 thunks and hook entries, the `mov eax, imm32; jmp eax` forwarders, a tail call). Function starts come from the code itself: call targets, and addresses stored in data or in code (virtual methods, callbacks), each after padding or a RET;
    - a call through a register or memory has no target to check. Its frame counts only when the callee's address is stored somewhere, and only once a later direct call (or a frame above it) proves it; an import call (`CALL [address]`) is never a caller. Unproven frames left at the end of the chain are dropped, so a chain can end early where its next caller was reached by a virtual call, never name a wrong one.
    - A Rebirth greyscale row is no longer `[in verify]`: `GreyscaleVerify` hooks the game's own call, so the one run of ours inside its `CompareDrawing` is that call, not an extra one (`tools/ui_census_report.py`, `PIXEL_HELPERS`).
    - The report no longer resolves a bare-named function of ours (the harness's `Send` lambda) to a game function that shares only the last part of its name (`SessionBase::Send`).
- **`tools/ui_census_report.py [--summary]`** merges the runs into `build/verify/ui_census_merged.csv`, one row per window × primitive × call site, with vanilla and Rebirth counts kept apart.
  - Frames are named from config/functions.csv, ei_hook.map and the EI_REPLACE lines.
  - A verify wrapper's second run of the original body is left out of the counts.
  - Calls made under a verify pixel helper (AccessPixels, CopySurfaceDC, WindowBytes, CompareDrawing) are marked `[in verify]`. Their reads are not the game's.
  - A function whose hook a run skipped ("SKIP ... at <address>" in its ei_hook.log, e.g. CChatWindow::Update 006095A0, which EI_Plugin.asi patches under Rebirth) runs only the game code, so it is not counted as a verify duplicate.
- **Run:**
  ```
  EI_UI_CENSUS=1 EI_RENDER=new,cpuwindows python tools/run_all.py ui_main_menu ui_screens ui_hud ui_camp ui_camp_deals ui_hero_screen ui_hero_stats ui_hero_delete ui_trade_hold ui_trade_restore ui_describe ui_help ui_objectives ui_saveload ui_saveload_delete ui_options_bind ui_quest_notice ui_quick_items ui_view_screen ui_base_dialogue ui_battle_commands ui_battle_spells ui_container_input console trader_stock render_present_cpuwindows ui_help_page ui_mp_host ui_global_map_pick --all-rebirth --jobs 4
  python tools/ui_census_report.py --summary
  ```
- **Results:** 52 runs (N0); all passed except two Rebirth runs that crash without the census too. N0a's rerun with its three scenarios: 58 runs, the same two failures (with CBattleScreenSetup::Create's 2 mismatches, in those two runs only); three runs that timed out before their first step while the machine was loaded passed when run again.
  - ui_saveload-rebirth crashes under cpuwindows in SetTexture → device::Surface.
  - render_present_cpuwindows-rebirth is the known R9c crash.
- **Counts are over the whole suite.** Where a verify wrapper is ours they include its reruns, so they are relative sizes, not per-frame costs.

## Result: what draws where

**No uncaught drawing.** Not one DIB change, either inside a DC session or between frames, is missing a hooked call that explains it.

**addon.dll makes no GDI calls.** It imports one hooked function and never called it on a surface. Rebirth draws only through game functions: its patched bodies, which still run the game's GDI code, or ours.

| Window (vtable) #surface | Text (DrawTextA) v / r | Fills (FillSolidRect) v / r | Other | Redrawn every frame | Main sites |
|---|---|---|---|---|---|
| CTradePanel rows `TradeRow` (007417A8) #0 | 16080 / 27360 | 120 / 144 | — | no (maxrun 4) | 006261A0 TradeDrawLists, CTradePanel::DrawLists, DrawSchool |
| CItemInfoPanel (007404DC) #0 | 12116 / 9568 | 1423 / 1501 | — | **yes** (694 of 796 frames; content changed in 63) | 005EE760 DescribeItem, 005F1590 DescribeStats, 005F1130, 005EF5D0, DrawWrappedText |
| CHeroStatsPanel (00741ABC) #0 | 5136 / 4896 | 140 / 133 | — | no | 00623880 DrawStats, DrawRow / DrawPart |
| CHudUnitPanel (007426F8) #0 | 3718 / 3520 | 706 / 673 | — | **yes** (drawn in 666 frames, alive in 648, changed in 52) | 006462A0 DrawVanilla, 00647040, 006477E0, 00647B70 |
| options (00741FAC) #0 | 1000 / 1000 | 25 / 25 | — | no | 00651350 RefreshRows |
| camp (0074091C) #0 | 892 / 894 | 430 / 417 | — | **yes** (414 of 414, changed in 21) | 00611930 CCampPages::UpdateAction |
| item strip (0074197C, 00621B50) #0 / #1 | 290 / 330, 826 / 950 | 469 / 466 each | — | **yes** (414 of 469, changed in 1 / 14) | CUiItemStrip::Draw < 00600EF0 UiContainerDraw |
| saveload (00741388) #0 | 532 / 412 | 32 / 32 | LineTo 13, pen w1 31B3FF | no | 006567A0 RefreshListVanilla |
| CUiSpin (00742D5C) #0 | 450 / 450 | 225 / 225 | — | no | 0065BEE0 SetValue |
| CUiScrollText (00742E9C) #0 | 410 / 432 | 71 / 67 | — | no | 0065D150 DrawLines (DrawWord) |
| heroes (0074214C) #0..#2 | 402 / 360 | 50 / 50 | — | no | CMpHeroScreen::DrawHeroList, DrawVoices, 00613EA0 Create |
| console | 195 / 195 | 30 / 30 | LineTo 25, pen w2 FFFFFF | no | 005E4800 DrawLines, 005E4A10 DrawInput (direct CDC, Courier New h-24) |
| CUiTooltip (00742F64) #0 | 158 / 184 | 29 / 34 | **FrameRect 29 / 34** (brush 31B3FF) | no (maxrun ≤ 9) | 0065E880 DrawLines |
| CUiEditField (00742DF4) #0 | 88 / 104 | 44 / 52 | LineTo 44 / 52, pens w1 31B3FF / 0000FF | no | 0065C2E0 DrawContentsVanilla |
| status | 62 / 62 | 31 / 31 | — | no | 005F75B0 ShowStatusText |
| main menu version line ("unowned", the menu surface before AddSurface) | 54 / 54 | 27 / 27 | — | no | 006335E0 CMainMenu::AddVersionLine |
| titles (00740C3C) #0 | — | 51 / 58 | **EM_FORMATRANGE 51 / 58** (RichEdit) | **yes** while scrolling | 0065F020 RenderText |
| CHudMap (00742660) #0, 24x18 (Rebirth 18x18) | — | — | **SetPixel 33 / 32** | no | 00641F20 SetupVanilla |
| msgbox, difficulty, objectives, briefing, map #1, CTradeGrid, CRepairGrid, titles #1 | 2–24 each | 1–58 each | DT_CALCRECT in BoxText (msgbox, difficulty) | CTradeGrid **yes** (50 of 50) | BoxText, 0064D9B0, 0064CC10, SurfaceText, 00627390 |
| map #0, objectives #2, briefing #0, menu #0 | 0 | 0 | — | alive, never drawn here | — |
| **N0a's scenarios** | | | | | |
| CChatWindow (00741478) #0, the briefing's player list | 112 / 100 | 56 / 50 | — | **yes** (56 of 56, 50 of 50; changed in 1) | 006095A0 CChatWindow::Update (Rebirth: EI_Plugin.asi's patch, so the game body) |
| CMpConnectScreen (00741CD0) #0, the connect list | 576 / 480 + 32 addon | 18 / 16 | LineTo 18 / 16 | no (9 of 38) | 0061D780 DrawListVanilla (Rebirth's patched body, and addon+134D2 through DrawTextIn); #1: 0061B2C0 Create, 4 texts |
| CChatOverlay (007400DC) #0, the typed line | 30 / 30 | 15 / 15 | LineTo 15 / 15 (the caret) | no (3 frames) | 005E3700 DrawTyped; measured with TextWidth on `GetDC(game window)` |
| chat line surface ("unowned", 494x61) | 6 / 6 | 1 / 1 | — | no | 005E2A30 CChatOverlay::AddLine (DrawPiece); never added to a window |
| help (00740E28) #0 / #1 / #2 | 24 / 24, 346 / 346, 26 / 26 | 6, 10, 6 | — | no | 00659420 ShowPageVanilla (DrawLine, DrawWrappedText; 2,002 TextExtent calls); Rebirth patches 00659420, so its body runs |
| fps (336x64, Rebirth 252x64) | 24 / 24 | 6 / 6 | — | no (5 of 46: every 400 ms) | 005F5320 DrawOverlays |

**GDI calls never made:** FillRect, InvertRect, DrawFocusRect, DrawEdge, DrawIconEx, TextOut, ExtTextOut with text, TabbedTextOut, Polyline, Rectangle, Ellipse, GetPixel, BitBlt, StretchBlt, PatBlt, StretchDIBits and SetDIBitsToDevice. The primitive set is therefore:

- opaque fill (MFC FillSolidRect = ExtTextOutA with ETO_OPAQUE and no text);
- FrameRect, in the tooltip only;
- 1- and 2-pixel solid lines (MoveTo / LineTo);
- SetPixel, in the minimap marker only;
- DrawTextA;
- RichEdit EM_FORMATRANGE.

**Metrics: what N2's own layout must replace.** All GDI metrics go through a few places:

| Calls | Where | Through |
|---|---|---|
| about 60,000 | GetTextExtentPoint32A on `GetDC(game window)` | 005F8340 TextExtent. Callers: DrawWrappedText (35k, most in verify), tooltip Construct / CheckSplit (21k), CUiScrollText Layout / Tokenize / DrawLines (2.8k), unit panel (0.7k), edit field, save/load list, difficulty box |
| 64 | DrawTextA with DT_CALCRECT | BoxText (message box and difficulty box) |
| 50 | GetTextExtentPoint32A on the console's own surface DC | `console::PromptWidth`, 005E4A10 |
| 208 + 104 + 2 × 52 | GetDeviceCaps 90 (LOGPIXELSY) / 88 on the screen DC | font creation (CreatePointFont in 005F4990, the console, the debug panel) |
| 220 | GetDeviceCaps on the surface DC | RichEdit RenderText / HeightInTwips (stays CPU) |

**Fonts made:**
- Times New Roman, six heights per resolution (h-49 to h-71, plus h-200 for the credits), all from 005F4990, made again at each Create;
- Courier New h-24 for the console and for the debug panel.

**Readers.** Besides the per-frame blit 005FDA80 (a Lock), these read surface pixels (counts vanilla / Rebirth):
- every window's `CUiWindow::ShowSurfaces` 006002C0 swaps every surface out and back (`SwapContents` 005FD7A0) when the window is hidden or shown;
- `CUiSurface::Freeze` 005FD760 does the same for the hero screen;
- the backdrop: `DrawOverlays` 005F5320 captures the screen into it (CaptureScreen, 45 / 45), turns it grey (Greyscale, 38 / 38) and copies it at 16 bits (Rgb565Copy, 38 / 38), when a window that shows the screen behind it opens (the escape menu among them). N0's suite reached it in 15 scenarios; the census did not see it.
- the save picture: `QuickSave` 006570E0 captures the screen and copies it at 16 bits (25 / 25), at every new game's autosave;
- the global map: `CGlobalMap::RefreshSelection` 00630670 captures the pick picture (6 / 6), and the mouse moves read it with PixelAt (8 / 8: ours under vanilla, the game's 00630010 under Rebirth). ui_camp reached it too.

The verify wrappers' pixel helpers read about 11,000 times, most of them on the item info panel. That is test traffic, not the game's. (Rebirth's greyscale used to be marked `[in verify]`, and some rows named callers that were not on the stack, such as `Connection::SendUpdate`. Both came from the call chains (N0d, below) and from a bare-name lookup in the report; see "Call chains".)

## Corrections to the ui-native.md inventory

1. **SetPixel.** It is `CHudMap::SetupVanilla` 00641F20, about 33 pixels on a 24x18 surface (18x18 under Rebirth), drawn again at each HUD setup. The inventory had "a 5x5 surface, drawn once".
2. **Swap.** `SwapContents` runs for every window on every show and hide (006002C0), plus the hero screen's Freeze. The inventory had "camp, menus, backdrop modes 3/4". P1 should swap canvas handles, not read back on each show and hide.
3. **Every-frame surfaces** are the item info panel, the item strip (both surfaces), the unit panel, the camp page #0, the trade grid and the scrolling titles. Tooltips are not redrawn every frame; ui-native section 3 says they are. Except for the titles, 91–99% of these redraws leave the pixels unchanged: the CPU path's memcmp hides this today.
4. **Main-menu version line.** 006335E0 draws into the menu surface before `AddSurface` gives the surface an owner. A native table keyed at `AddSurface` must apply to a surface's earlier drawing too, or the menu falls back to the CPU.
5. **Fills.** They are FillSolidRect everywhere: Clear and Fill both end in ExtTextOutA ETO_OPAQUE. FillRect is never called. The inventory had this right; the census confirms it.
6. **The console measures text on its own surface DC** (PromptWidth), not through 005F8340.
7. **The chat overlay surface exists in single-player.** It is blitted every frame and never drawn.

**Coverage (N0a).** N0 listed eight things its suite did not reach. Three new scenarios reach the rest; the table above has their rows.
- **ui_help_page:** the help page 00659420. The help key is H ("tutorial_script"); ui_help presses F1, which is "select1", so it never opened help. Only topics with a "tutorial" text open: the options, the objectives (paged) and the camp.
- **ui_mp_host:** a hero is made, the connect list 0061D780 is clicked, keyed and sorted, a game is hosted, and on the briefing the chat window (player list) is drawn and the chat line is typed, edited and sent. Nothing leaves the machine. The "disconnect" box that sometimes followed the chat line was the harness's, not Rebirth's: parallel games shared UDP port 8888 (N0c, `docs/notes/ui-native.md`); each harness game now has a port of its own and the scenario checks the briefing is still on top at its end.
- **ui_global_map_pick:** the global map's pick picture and PixelAt, with "fps on" for the FPS surface.
- **The escape-menu capture and greyscale, and the save picture,** were reached by N0's suite already (15 and 20 scenarios). The census could not see them (see "How it was measured").
- **The debug panel (0079B930) cannot be drawn in 1.06.** Only `PanelShow` 00444F50 sets it visible, and its only caller is `PanelSetRect` 00444C10, which calls it only when the panel is already visible (`string_map.cpp`). No console command or key reaches it. A gallery can draw it only by calling 00444F50 itself.

**Heap vtables (resolved).** N0 saw four owners in ui_trade_hold under Rebirth with vtables on the heap (2FBD6554, 2FBD6A1C, 2FBD6BB4, 2FBD6EE4), drawn by `CHeroStatsPanel::DrawStats` and `TradeDrawLists`. They are verify stand-ins, not Rebirth's: `verify_ui_window_slots.cpp` gives a container's children a heap copy of their vtable (`StandInTable`, the real vtable in the entry before it) while slots 22 / 23 / 24 are dispatched, and `slot_recorder.h` does the same for one object. That is when the item panels, the unit panel and the hero stats draw. The census now labels them with the game vtable, and the rerun has no heap vtables. **P3c can key on vtables**: a release build has no stand-ins, and a verify build must look through them (`RealVtable` in `ui_census.cpp`).

## Effect on the P tickets

- **P1:** unchanged in size. `SwapContents` must keep or swap the canvases (it is frequent), and need not read back. The other readers are the backdrop's capture, greyscale and 16-bit copy (each window that shows the screen behind it), the save picture (each save) and the global map's pick (PixelAt per mouse move). All are now exercised by the suite.
- **P2 / N2:** unchanged. The no-GDI metrics have exactly four entry points: 005F8340, 005FDE20's return value and DT_CALCRECT, the console's PromptWidth, and the LOGPIXELSY font sizing in 005F4990.
- **P3c is the heaviest port.** It has the trade rows, item info panel, hero stats and item strip: most of the text volume, and two of the every-frame surfaces. Keep 1.5, but hoisting and the pass split matter most here. A cheap "same commands as last frame" check would skip most every-frame canvas redraws.
- **P3d / P5:** the chat window, chat line, connect list and help page are now covered (N0a). The chat window is redrawn every frame on the briefing. The chat line's text is drawn on a surface no window owns, like the main menu's version line.
- **Rebirth:** no addon GDI to port. What blocks Rebirth surfaces is only the patched game bodies (005FDE20 and the others in ui-native section 5), as planned.
