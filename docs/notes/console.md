# Developer console (0x005DC000-0x005E0B20, console window 0x005E3E90-0x005E4170)

Code: `src/game/console.h`, `console.cpp` (streambuf, executor, tokenizer, script lines),
`console_commands.cpp` (command handlers), `console_window.cpp` (window text and placement).
Verify wrappers: `tests/verify/verify_console.cpp`. In-game scenario: `tests/scenarios/console.txt`.

## How it works

- **Output.** `007C55A8` is an (old, pre-standard) MSVC `ostream` whose streambuf is the console
  streambuf `007C5868` (vtable `00740034`). Eight more instances with their own ostreams carry the
  filtered message categories: filter 1 ai (`007C76D0`, ostream `007C5810`), 2 event, 4 graphics,
  8 rpg, 0x10 unitacks, 0x20 net, -1 all (`007C5CC0`, ostream `007C5600`). All are constructed at
  static initialisation (`005DC1E0`-`005DC3F0`, ostreams `005DC420`-`005DC5E0`). Every message is
  written `os << text << '\n' << flush` (`flush` is `00435000`, applied through `00434FE0`).
- **ConsoleStreambuf** (0x454 bytes): the old iostream streambuf (0x4C: base `+0x10`, ebuf `+0x14`,
  pbase `+0x18`, pptr `+0x1C`, epptr `+0x20`, ...), `+0x4C` category, `+0x50` CString line,
  `+0x54` a 0x400-byte put area. overflow (slot 7) moves the put area and the character into the
  line when the category is shown (category 0, or its bit in the mask `007C7B68`); a hidden
  category keeps its put area (later characters are dropped). sync (slot 1) calls overflow(EOF),
  hands the line to the console window through the interface manager `007C7D50` (`005F7EB0`, which
  checks the window at `+0x14`) and empties it. underflow (slot 8, `005DC230`) returns EOF.
- **Executor** `005DC620` (text in ECX). On every call it registers all 27 commands in two STLport
  `map<CString, handler>` (`007C7B28` always available, `007C7B38` after the cheat word;
  `operator[]` is `005E0B20`; node 0x18 bytes: color, parent, left, right, key `+0x10`, handler
  `+0x14`). The line is split by `004DC200` (see below); the first token is the command.
  - `@x`, `#x`, `$x` (cheat word given, not in a network game, `0057F440`): `005DD800` loads
    `WorldScript(` x args `)`, `WorldScript( ConsoleFloat( ` ... ` ))` or
    `WorldScript( ConsoleString( ` ... ` ))` (arguments rejoined with single spaces) into the
    mission script (`005785D0`: CScript::Load, one tick when it loaded) and prints
    `Script error: <text>` when it does not load.
  - Otherwise the command and its arguments are lower-cased (`_mbslwr`); `quit`/`exit` call
    `00455F20` (PostQuitMessage when the window exists); a non-null public handler runs; without
    the cheat word the command is XOR 0x66'd and compared with `00740028` ("thingamabob"), which
    sets `007C7B64` and prints `Activated!`; with it the cheat map's handler runs (not in a
    network game). Anything else prints `Unknown command`.
  - Handlers: `void handler(CStringArray* args)` with args in ECX (MFC CStringArray, 0x14 bytes).
- **Tokenizer** `004DC200` (fastcall text, array; returns 1): states 0 between tokens, 1 in a
  token, 2 inside double quotes. Spaces end a token only outside quotes; quotes toggle (and stay
  in the token); lead bytes of double-byte characters are taken with their trail byte.
- **Console window** (the interface manager's `+0x14`): `+0x00` the 3D interface (screen width,
  height), `+0x04` the surface while shown, `+0x30/+0x34` size in pixels (at least 76x69),
  `+0x38` rect in 800x600 coordinates, `+0x48` rect in pixels, `+0x58` CStringArray of lines,
  `+0x6C` history depth, `+0x70` visible lines (`(height - 24) / 15 - 1`), `+0x74` lines scrolled
  back, `+0x7A` shown. AddText (`005E4090`) splits at '\n' (`004DBE40`): the first piece continues
  the last line, so each message line ends with an empty line that the next message fills.

## Commands

Public (`007C7B28`): console, lastfps, exec, execute, kick, ban, fps, rate, localrate.
After the cheat word (`007C7B38`): help, debuginfo, history, filter, show, listvar, loadvar, give,
fadeout, join, server, memusage, lootall, net, disconnect, generateacks, days, partyresend.

## Rewritten functions

| Address | Name | Verification |
|---|---|---|
| 005DC150 | ConsoleStreambuf::Overflow | ours only: line = before + put area + c when shown (put area reset), unchanged when hidden; returns 0 |
| 005DC1B0 | ConsoleStreambuf::Sync (the harness also hooks its vtable slot) | ours only: returns 0, line empty |
| 005DC200 | ConsoleStreambuf::Construct (static init; used by the capture below) | ours only: fields, vtable, buffer |
| 005DC240 | ConsoleStreambuf::DeletingDestruct | ours only: a console streambuf, returns this |
| 005DC260 | ConsoleStreambuf::Destruct (at exit) | ours only: a console streambuf |
| 005DC620 | Execute (Rebirth patches it: vanilla only) | ours only: all 27 commands registered with their handlers; the cheat flag changes only for the cheat word |
| 004DC200 | Tokenize | output: original into a scratch CStringArray; tokens, size, capacity, result |
| 005DD800 | RunScriptLine | ours only, output captured: "Script error: <CScript error text>" printed exactly when the mission script's file count (+0x1AC) did not grow |
| 005DF5E0 | help | side by side, output |
| 005DD920 | debuginfo | side by side, output |
| 005DDBE0 | console | side by side, output and the window rectangles |
| 005DDF80 | history | side by side, output and history depth |
| 005DDFE0 | filter | side by side, output and mask (restored between the runs) |
| 005DE460 | show | side by side, output and the debug display flags (`007AF5B8+0x88`) |
| 005DEA00 | listvar | side by side, output (a file is written twice) |
| 005DEC90 | loadvar | side by side, output (the variables are set twice) |
| 005DEEE0 | lastfps | side by side, output |
| 005DEF50 | exec, execute | ours only: "Cannot open file" printed exactly when the file does not exist |
| 005DF0F0 | give | ours only: money grows by the amount |
| 005DF5B0 | lootall | ours only: prints "Looting..." first |
| 005DF8E0 | fadeout | ours only: prints "Ok" (or the usage) only |
| 005DF9B0 | net | "net time" and the usage side by side (setting `0079BC60` restored between runs); the network subcommands ours only, silent |
| 005E00D0 | days | side by side, output |
| 005E0570 | partyresend | ours only: silent |
| 005E0590 | kick | ours only: silent |
| 005E06E0 | ban | ours only: silent |
| 005E0830 | fps | side by side, output |
| 005E0990 | rate | side by side, output and `00783E34` (restored between runs) |
| 005E0A10 | localrate | side by side, output and `00783E38` (restored between runs) |
| 005E3E90 | ConsoleWindow::SetRect | side by side: sizes, visible lines, both rectangles (a shown window gets its surface recreated twice) |
| 005E3F90 | ConsoleWindow::SetHistory | side by side: lines, history, scroll (lines restored with CStringArray::Copy between runs) |
| 005E4090 | ConsoleWindow::AddText | side by side as SetHistory |

Output capture (verify only): the console ostream's streambuf pointer (its `ios` at `+8`, `bp` at
`ios+4`) is pointed at a console streambuf built by the game's constructor, whose vtable copy has
a sync that stores the line. Side-by-side wrappers capture the original's and ours' output,
compare, and replay ours into the real streambuf (overflow, append, sync through the vtable, so
the harness sees it). Captures nest (a replay goes to whatever the ostream pointed at before).

The handler addresses stored in the maps are the original addresses (as the original stores them);
they jump to the rewrites. Calls between console functions (the tokenizer, the script line, the
executor from exec) go through the original addresses, so verify wrappers and Rebirth's patch of
the executor stay in the path.

`src/hook/function_sizes.inc` had no entries for the handlers Ghidra has no function for (all but
listvar, exec, give, fadeout); 17 entries were added (body sizes from a flow walk of the code), so
the installer accepts them and still compares their whole bodies for foreign patches.

## Forwarders added (standalone build must replace)

- MFC: CString Mid(int) `006FD9B4` (config/library.csv calls it Right), Mid(int,int) `006FD9D7`,
  operator+=(TCHAR) `00700D73`, operator+=(LPCTSTR) `00700D4C`, operator=(TCHAR) `006FD559`,
  GetBuffer `00700DA0`, ReleaseBuffer `00700DEF`, MakeLower `00700EFE` (lib/mfc.h and
  library.csv call this address MakeUpper; it calls `_mbslwr`), TrimLeft(TCHAR) `006FE1D3`,
  TrimRight(TCHAR) `006FE0DD`; CStringArray ctor/dtor/SetSize/SetAtGrow/RemoveAt
  `006FE925`/`006FE958`/`006FE9B8`/`006FEBFA`/`006FED3C` (Copy `006FEB61` in verify); CStdioFile
  ctor/dtor/Open/ReadString/WriteString/Close `00701B79`/`00701C6D`/`00701CB0`/`00701E93`/
  `00701E23`/`00701FE0`; CTime(y, m, d, h, mi, s, dst) `006FE3BB`.
- iostream: ostream `<<` const char* `006FC31D`, char `006FC0B0`, int `006FC91C`, double
  `006FC7FA`; manipulator application `00434FE0`; streambuf(char*, int) `006FCAD2`,
  ~streambuf `006FCB35`.
- CRT: atoi `006E80D8`, atof `006E911A`, `_ismbblead` `006E99F2`, `isleadbyte` `006E9A6C`,
  `_mbsinc` `006E9947`.
- Command maps: operator[] `005E0B20`. Hash-map iteration of player variables: begin `004614C0`,
  next bucket `005E0DB0`. Split at a character `004DBE40`.
- Game: network check `0057F440`, quit `00455F20`, mission script load `005785D0`, interface
  manager `005F7EB0` (add console line), `005F7ED0` (console rect), `005F7EF0` (history),
  `005F7F10` (debug info rect), `005F7590` (top window), `005F5700` (fps window); debug display
  `007AF5B8` on/off `00569A10`/`00569A20`/`00569A40`/`00569A50`/`00569A90`/`00569AA0`/`00569B10`/
  `00569B20` and `005699F0` (local client); world `00594180` GetPlayer, `00593D60` loot all; player
  state `0066EE10` set variable, `0066EF80` refresh, `0066F450` resend party; unit stats
  `00523960` add experience; server command `0041D260`; fader `005E6B90`; frame timer `00500960`;
  console window `005E3FD0` show/hide, `005E4800` redraw; UI scaling `005F7CA0`/`005F7DA0`
  (Rebirth patches both: called through their addresses).

## Original bugs and oddities kept

- Looking a command up with the map's operator[] inserts it: every unknown word typed adds an
  entry with a null handler to the public map (and, after the cheat word, to the cheat map).
- The cheat-word test XORs the command in place: a command containing 'f' gets a NUL inside.
- Arguments are lower-cased too: `exec`/`listvar`/`loadvar` file names, `kick`/`ban` names.
- exec trims the quotes for opening but prints the argument with them in "Cannot open file".
- show with a valid item but neither 1/on nor 0/off does nothing (no message); fps the same.
- days: the help's example claims 25011978 to 01072000 is 8192 days (it is 8193); dates before
  1970 go through mktime's -1. The digit test is a signed comparison (bytes >= 0x80 fail).
- give exp divides by the party size before checking it (0 gives an infinite share, unused).
- net client uses the local client (`005699F0` +0x1C) unchecked.
- kick/ban with a lone `"` strip it to an empty name. AddText reads the first piece unchecked.
- fadeout uses the fader at `007C7D68` unchecked.

## Skipped

- `005DC230` underflow: 4 bytes (`OR EAX,-1; RET`), too short for a 5-byte jump.
- `005DF5A0` memusage (a `JMP 00455CD0`), `005DFC10` disconnect (`MOV ECX; JMP 00581620`):
  thunks.
- `005DF3A0` join, `005DF490` server: build network session descriptors (`00661050`, `006628D0`,
  `00581730`/`00581760`) not mapped; they start network games, not testable in single player.
- `005DFDA0` generateacks (writes scenack.txt from the units' acknowledgement tables,
  `005DFC20`/`005DFCE0`): left for later.
- `005DC080` (destructor of the cheat map at exit) and the map/tree template members
  `005E0A90`-`005E13D0` (STLport `_Rb_tree` constructor, lower_bound, insert, erase): template code.
- The rest of the console window: `005E3FD0` show/hide (surface creation), `005E4800`/`005E4A10`
  drawing, `005E44A0` key input (Rebirth patches it), `005E3700` (Rebirth patches it).
- `005E1580`-`005F0000`: other subsystems, not mapped.

## Scenario

`tests/scenarios/console.txt` (about 30 s): public commands and the tokenizer (case, extra spaces,
quotes) in the menu, the cheat word, every rewritten command's messages and settings (the filter mask
ends at its start value 0), then in a game the script lines (spaces inside and outside quotes, a script error),
listvar/loadvar round trip through `.\saves\` (redirected to harness_saves) and give.
