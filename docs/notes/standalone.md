# Standalone exe: runtime plan and UTF-8 (planned 2026-10-13, re-checked and split 2026-10-15)

The hook DLL shares objects with the original code, so every shared type keeps the exe's layout and
allocator. A standalone exe has no original code. This file records what still ties the code to the
exe, how the first standalone exe gets around it, and what a switch from cp1251 to UTF-8 takes.

## 1. What ties the code to the exe (measured 2026-10-15)

| Tie | Count |
|---|---|
| Unported game functions | 201, all under 5 bytes (58 `ret`, 35 `xor eax,eax; ret`, 35 `ret 4`, ...); each has 5 bytes of room before the next function; 154 are slots in the exe's vtables, 5 are forwarded to |
| Forwarders (`EI_ORIGINAL`) | 3,795 addresses: 2,347 to rewritten code (C13), 5 unported, 1,443 library: STLport 881, MFC 399, compiler-generated 49, CRT 37, import thunks and others 77 |
| Code pointers in the exe's data | 4,524 to rewritten code (vtables, callback tables), 391 to 359 library functions (336 MFC: the `CArray` / `CList` / `CMap` deleting destructors and `Serialize` slots) |
| Game data addresses | 3,601 distinct in 524 files: .rdata 912, import table 465, initialised .data 1,812, .bss 410 |
| Static initialisers | 748 in the .data list 00783004-00783BB0, one hooked (0042FAE0); 15 instruction patterns cover about 720 (175 guarded `atexit`, 286 stores, 114 float folds, about 100 with an `atexit`) |
| Reads of the exe's code bytes | the frame's camera call target (00457595), app_frame's CALL test, Rebirth detection (live bytes against the file) |
| Runtime uses outside src/lib | `mfc::` 3,481, `stlp::` 1,009, `gamecrt::` 1,191 (865 new / delete), `gameapi::` 536 |
| Cleanup counts (no effect on the first exe) | C11 shims 1,034, `__asm` 221, `float_control` 114, raw addresses 1,025 |

The old renderer code the first plan counted is ported (only the 201 short functions are left).

## 2. The first standalone exe: an address image

S comes before C12 and C13 (user, 2026-10-15), so the first exe has to run with the 2,347 forwarders to
rewritten code, the 3,601 raw data addresses and the exe's vtables as they are. It does that by
rebuilding the exe's address space without its code:

- **Host and image** (S1). `ei_standalone.exe` is linked below 0x400000 and reserves 0x400000-0x7D7000
  with a zero-filled array. It copies the headers, .rdata, .data and .rsrc of game.exe on disk into place
  (data only, nothing from .text runs), fills .text with INT3, resolves the import table itself (DDraw,
  mss32 and binkw32 from Engine\, the rest from System32, so Rebirth's dinput proxy and addon.dll stay
  out), loads `ei_game.dll` (the hook DLL's sources built with `EI_STANDALONE`) and lets `InstallHooks`
  write its JMP stubs into the blank text. Forwarders, function pointers in data and vtable slots then
  reach our code through the stubs. An INT3 hit logs the address's name and exits: that is the list of
  what is still missing.
- **Library code is ported like game code** (S4-S13). Bodies for the MFC / STLport / CRT pieces live in
  src/lib (layout- and allocator-compatible, as now); each instantiation our code forwards to or the exe's
  data points at is registered at its library address (`EI_REPLACE` / `EI_REPLACE_METHOD`) with a verify
  wrapper where the elements are plain data, so the hook build checks it against the original exactly as
  it checks game code. The forwarders in src/game stay (C13 makes them direct calls); no call site
  changes, so these tickets do not touch C11's shims. The allocator (operator new / delete, malloc /
  free, STLport's node allocator) is the one piece registered for the standalone image only: the hook
  build keeps the game's heap.
- **What only the image needs** (S2, S3, S14): the 201 short functions (registered, skipped by the hook
  build, stubbed in the image), the 748 static initialisers (generated from their patterns, run in list
  order before WinMain), and the few sites that read the exe's code bytes or detect Rebirth from them
  (the standalone takes its Rebirth flags from ei_rebirth.ini alone; Miles must not load EI_Plugin.asi
  into the image).
- **Boot** (S15-S17): the harness runs the standalone exe like a game (`--standalone`), then the first
  boot to the main menu, then the suite against the SV goldens.

Later, after C13 (S18): generated definitions for the data addresses, vtables as real virtual classes,
imports linked directly, plain new / delete; then the image and the read of game.exe go.

## 3. Runtime: MFC, STLport, CRT

MFC pieces in use are containers (`CArray` 470 qualified uses, lists 218, `CMap` 4), `CString` 2,546,
files and archives 91, a little GDI (`CDC`, `CPen`, `CFont`), `AfxMessageBox` and the TRY / CATCH link.
The game's WinMain does not go through `CWinApp`.

1. **First standalone exe:** section 2. `src/lib/mfc.h`, `stlp.h` and `containers.h` get full bodies,
   one header per family so the library tickets can run side by side.
2. **Later cleanup: standard types.** Only after the hook build is retired (side-by-side checks need
   the shared layout). `CArray` to `std::vector`, lists to `std::list`, `stlp::string` and `CString`
   to one string type, `stlp::vector / list / map` to their `std` twins. The framework pieces stay
   small classes of our own. Linking real MFC would be quicker but ties the build to Windows
   (platform-layer.md).

What must survive either step:

- **Hash map order** (39 `stlp::hash_map` uses): iteration order follows STLport's bucket counts and
  insertion rule. A loop over one that feeds game logic, saves or network messages keeps our copy of
  the container; the rest can use `std::unordered_map`. Check each use.
- **Sort order**: STLport's `sort` leaves equal elements in another order than MSVC's. The
  objectives list showed it is visible. Keep the original algorithm at those sites.
- **CRT behaviour**: `_ftol` (low half of a 64-bit FISTP, NaN / overflow gives 0), `rand` (MSVC6's
  generator and seed handling), `sprintf` float formatting (to check against today's CRT; the old one stops at 17 significant digits), the pooled
  small-block allocator only where code depends on reuse order (none known).
- **Formats**: the archive encoding of strings and arrays in saves and network messages is a pair of
  read / write functions, independent of the container type.
- **Struct sizes**: a `CString` is 4 bytes, a `std::string` 24. Every raw offset left (986 offset
  accesses, 223 byte-offset sums on 2026-10-13) breaks when a member changes size, so step 2 waits
  for the member cleanup.
- **x87 maths**: unchanged by this; the standalone exe stays a 32-bit MSVC build with the same
  float settings until the float checks are replaced.

## 4. UTF-8 instead of cp1251

Goal: every string inside the game is UTF-8. Original assets stay as they are and are converted when
read. Saves and multiplayer need not match the vanilla game (user, 2026-10-13), so they simply hold
UTF-8. Do it in the standalone build behind one compile-time switch, so the hook build stays cp1251
and remains the reference; frame captures of the same scenario in both builds should match pixel
for pixel, which is the check.

How text moves today: assets hold cp1251 bytes; the interface draws through GDI's ANSI calls with a
font whose charset comes from the "Font CharSet" option; typed characters arrive as WM_CHAR in the
ANSI code page; file names (save names typed by the player) go to the ANSI file calls.

| Ticket | Work | Size |
|---|---|---|
| U1 Asset boundary | One `FromAssetText(bytes, codepage)` and calls to it where display text is read: the text database (`TextSource`, texts.res), .reg string values (menus.reg, quest.reg, smessbase.reg), strings in .mob files (unit names, dialogue, script literals), credits.scr. The source code page is a setting (1251 for this release). Identifiers and file names inside assets are ASCII and need nothing. RTF (titles) carries its own code page. | 1 |
| U2 System boundary | Wide versions of the file, registry, window title, message box, clipboard and key name calls, converting at the call. 19 direct ANSI call sites outside src/lib, plus `gameapi.h`, `CFile` and `CFileFind` themselves. | 1 |
| U3 Drawing and measuring | The interface's text draw and extent (005F8340 and the draw routines), the console and the debug panel call the wide GDI functions; the charset option is dropped. Falls away if the native UI's font rasteriser comes first. | 1 |
| U4 Typing | Unicode WM_CHAR; the edit field, console line, chat line and name entry move the caret, delete and limit length by character, not byte. | 1 |
| U5 Audit | Classify each site as identifier (leave) or display text (fix): 99 case-insensitive compares and case changes (Cyrillic needs a small case table once the CRT's code page is gone), 95 fixed `char` buffers (Cyrillic doubles in UTF-8, so limits sized for names may truncate), per-byte indexing of text (about 37 sites), `%-20s`-style padding in console tables, word wrap that splits inside a long word. | 1-2 |

Total 5-6 tickets. Byte-equality compares, hashing, copying and formatting (the large majority of
string code) work unchanged on UTF-8. It cannot start before the port is complete: until then the
original code reads the same strings.
