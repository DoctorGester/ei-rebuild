# Standalone exe: runtime plan and UTF-8 (planned 2026-10-13, nothing started)

The hook DLL shares objects with the original code, so every shared type keeps the exe's layout and
allocator. A standalone exe has no original code, and those constraints go. This file records what
still ties the code to the exe, what replaces the exe's MFC / STLport / CRT, and what a switch from
cp1251 to UTF-8 takes. Counts are from `tools/dashboard_data.py` (the dashboard's "Standalone" table)
on 2026-10-13; ticket counts are estimates.

## 1. What ties the code to the exe

| Tie | Count | Goes away by |
|---|---|---|
| Unported game functions | 1,117 (261 under 5 bytes, 77 Rebirth-patched) | the area tickets; the ones under 5 bytes cannot be hooked and are written at standalone time |
| Forwarders to unported game code | 220 addresses | the port |
| Forwarders to code already rewritten | 2,101 addresses | calling the rewrite directly (cleanup C3; Rebirth-patched callees stay forwarded while the hook build exists, rule 9) |
| Forwarders to library code | 1,380 addresses: STLport 855, MFC 383, CRT and others about 140 | section 2. Most are per-type template copies (`vector<T>::insert`, `CArray<T>::SetSize`) that collapse into one template each |
| Game data addresses | 3,239 distinct literals in 0x730000-0x8FFFFF | generated definitions with the exe's initial values; vtables become real virtual classes (config/vtables.csv) |
| Imports through the exe's table | 411 `gameapi::` uses | linking the import libraries directly |
| The exe's allocator | 781 `gamecrt::operator_new` / `operator_delete` uses | plain new / delete |
| Old renderer code | 100 functions, 43 KB in 0x4C0000-0x500000 | ported as is, or the parked device removal (renderer.md section 9) |

Also needed: WinMain and the static initialisers (they run before the hooks, so they are unchecked
today), resources, and the functions kept original on purpose (MFC TRY / CATCH frames 004F0B90,
006D5910, 006D5AC0; 0066D730's uninitialised byte).

## 2. Runtime: MFC, STLport, CRT

MFC pieces in use (qualified uses outside src/lib): `CString` 2,228; arrays (`CArray`,
`CStringArray`) 405; lists (`CTypedPtrList`, `CList`, `CPtrList`) 199; files (`CFile`, `CFileFind`,
`CArchive`, `CMemFile`) 61; `CMap` 3. Framework pieces with no count: `CObject` with runtime classes
and serialisation (config/runtime_classes.csv), `CWinApp` / `CWinThread`, the main window, file
exceptions, `CDC` / `CFont` for interface text, the rich edit control (titles).

Plan, in order:

1. **First standalone exe: our own layout-compatible classes.** `src/lib/mfc.h`, `stlp.h` and
   `containers.h` already hold the layouts and part of the members. Give them full bodies so no
   forwarder reaches library code. Same source builds the hook DLL (bodies forward or are checked
   side by side) and the standalone exe. Estimate 6-8 tickets: CString 1, arrays / lists / maps 1,
   files and archives 1, CObject / runtime classes / app and window 1-2, STLport containers 1-2
   (855 forwarded addresses, but a handful of templates), CRT pieces 1.
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

## 3. UTF-8 instead of cp1251

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
