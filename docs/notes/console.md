# Developer console (0x005DC000-0x005E0B20, console window 0x005E3E90-0x005E4D25)

Code: `src/game/console.h` / `console.cpp` (streambuf, executor, tokenizer, script lines),
`console_commands.cpp` (handlers), `console_window.cpp` (window text and placement),
`console_addon.cpp` (Rebirth's commands, `Patch::AddNewConsoleCommands` and AutoExec, see
rebirth.md). The window's surface, drawing and key input (005E3FD0, 005E4180, 005E44A0,
005E4800, 005E4A10) are in ui.md ("Console window", `ui_console_window.cpp`).
Verify wrappers: `tests/verify/verify_console.cpp`. Scenario: `tests/scenarios/console.txt`.

## How it works

- **Output.** `007C55A8` is an old (pre-standard) MSVC `ostream` on the console streambuf `007C5868`
  (vtable `00740034`). Eight more streambuf/ostream pairs carry filtered categories: filter 1 ai
  (`007C76D0`, ostream `007C5810`), 2 event, 4 graphics, 8 rpg, 0x10 unitacks, 0x20 net, -1 all
  (`007C5CC0`, ostream `007C5600`); all built in static init (`005DC1E0`-`005DC3F0`, ostreams
  `005DC420`-`005DC5E0`). Every message is `os << text << '\n' << flush` (`flush` = `00435000`,
  applied through `00434FE0`).
- **ConsoleStreambuf** (0x454 bytes; layout in console.h): overflow (slot 7) moves the put area and
  the character into the line when the category is shown (category 0, or its bit in the mask
  `007C7B68`); a hidden category keeps its put area (later characters are dropped). sync (slot 1)
  calls overflow(EOF), hands the line to the console window through the interface manager `007C7D50`
  (`005F7EB0`, checks the window at `+0x14`) and empties it. underflow (slot 8, `005DC230`) returns EOF.
- **Executor** `005DC620` (text in ECX; Rebirth patches it, hooked in a Rebirth launch via
  `EI_REBIRTH_IMPLEMENTS`). On every call it re-registers all 27 commands in two STLport
  `map<CString, handler>`: `007C7B28` (always available) and `007C7B38` (after the cheat word);
  `operator[]` is `005E0B20`, node 0x18 bytes (color, parent, left, right, key `+0x10`, handler
  `+0x14`). The line is split by the tokenizer; the first token is the command.
  - `@x`, `#x`, `$x` (cheat word given, not a network game, `0057F440`): `005DD800` loads
    `WorldScript(x args)`, `WorldScript( ConsoleFloat( ... ))` or `WorldScript( ConsoleString( ... ))`
    (arguments rejoined with single spaces) into the mission script (`005785D0`: CScript::Load, one
    tick when it loaded); prints `Script error: <text>` when it does not load.
  - Otherwise command and arguments are lower-cased (`_mbslwr`); `quit`/`exit` call `00455F20`
    (PostQuitMessage when the window exists); a non-null public handler runs; without the cheat word
    the command XOR 0x66 is compared with `00740028` ("thingamabob"), which sets `007C7B64` and
    prints `Activated!`; with it the cheat map's handler runs (not in a network game); anything else
    prints `Unknown command`.
  - Handlers: `void handler(CStringArray* args)`, args in ECX (MFC CStringArray, 0x14 bytes).
- **Tokenizer** `004DC200`: states 0 between tokens, 1 in a token, 2 in double quotes. Quotes toggle
  and stay in the token; spaces end a token only outside quotes; double-byte lead bytes are taken
  with their trail byte.
- **Console window** (the interface manager's `+0x14`): `+0x00` the 3D interface, `+0x04` the
  surface while shown, `+0x30/+0x34` size in pixels (at least 76x69), `+0x38` rect in 800x600
  coordinates, `+0x48` rect in pixels, `+0x58` CStringArray of lines, `+0x6C` history depth, `+0x70`
  visible lines (`(height - 24) / 15 - 1`), `+0x74` lines scrolled back, `+0x7A` shown. AddText
  (`005E4090`) splits at '\n' (`004DBE40`): the first piece continues the last line, so each message
  line ends with an empty line that the next message fills.

Commands. Public (`007C7B28`): console, lastfps, exec, execute, kick, ban, fps, rate, localrate.
After the cheat word (`007C7B38`): help, debuginfo, history, filter, show, listvar, loadvar, give,
fadeout, join, server, memusage, lootall, net, disconnect, generateacks, days, partyresend.

## Rewritten

| Address | Name | Verification |
|---|---|---|
| 005DC150 / 005DC1B0 | ConsoleStreambuf::Overflow / Sync | ours only: line contents, put area reset, return 0 |
| 005DC200 / 005DC240 / 005DC260 | Construct / DeletingDestruct / Destruct | ours only: fields, vtable, buffer |
| 005DC620 | Execute | ours only: all 27 commands registered; cheat flag changes only for the cheat word |
| 004DC200 | Tokenize | original into a scratch CStringArray; tokens, size, capacity, result |
| 005DD800 | RunScriptLine | ours only, output captured: "Script error" printed exactly when the script's file count (+0x1AC) did not grow |
| 005DF5E0 help, 005DD920 debuginfo, 005DDBE0 console, 005DDF80 history, 005DDFE0 filter, 005DE460 show, 005DEA00 listvar, 005DEC90 loadvar, 005DEEE0 lastfps, 005E00D0 days, 005E0830 fps, 005E0990 rate, 005E0A10 localrate | handlers | side by side: output, plus the state each touches (window rects, history depth, filter mask `007C7B68`, debug display flags `007AF5B8+0x88`, `00783E34`/`00783E38`); state restored between runs |
| 005DEF50 exec/execute, 005DF0F0 give, 005DF5B0 lootall, 005DF8E0 fadeout, 005E0570 partyresend, 005E0590 kick, 005E06E0 ban | handlers | ours only: exec "Cannot open file" exactly when the file is missing; give money grows by the amount; lootall prints "Looting..."; fadeout prints "Ok"/usage; the rest silent |
| 005DF9B0 | net | "net time" and usage side by side (`0079BC60` restored); network subcommands ours only, silent |
| 005E3E90 / 005E3F90 / 005E4090 | ConsoleWindow::SetRect / SetHistory / AddText | side by side: sizes, visible lines, rects, lines, history, scroll (lines restored with CStringArray::Copy; a shown window gets its surface recreated twice) |

Output capture (verify only): the console ostream's streambuf pointer (`ios` at `+8`, `bp` at
`ios+4`) is pointed at a console streambuf built by the game's constructor whose vtable copy has a
sync that stores the line. Side-by-side wrappers capture the original's and ours' output, compare,
and replay ours into the real streambuf (overflow, append, sync through the vtable, so the harness
sees it). Captures nest.

Handler addresses in the maps are the original addresses (they jump to the rewrites). Calls between
console functions (tokenizer, script line, executor from exec) go through the original addresses so
verify wrappers and Rebirth's executor patch stay in the path. The handlers Ghidra has no function
for (all but listvar, exec, give, fadeout) have entries in `function_sizes.inc` from a flow walk, so
the installer still compares their whole bodies for foreign patches. MFC/iostream/CRT callees are
forwarders (`originals_*.h`, `console.h` `Str`); `006FD9B4` is `Mid(int)` (library.csv says Right).

## Original bugs and oddities kept

- The map's operator[] inserts on lookup: every unknown word typed adds a null-handler entry to the
  public map (and, after the cheat word, to the cheat map).
- The cheat-word test XORs the command in place: a command containing 'f' gets a NUL inside.
- Arguments are lower-cased too (`exec`/`listvar`/`loadvar` file names, `kick`/`ban` names).
- exec trims the quotes for opening but prints the argument with them in "Cannot open file".
- `show` with a valid item but neither 1/on nor 0/off does nothing; `fps` the same.
- `days`: the help's example (25011978 to 01072000 = 8192 days) is really 8193; dates before 1970
  go through mktime's -1; the digit test is signed (bytes >= 0x80 fail).
- `give exp` divides by the party size before checking it (0 gives an infinite share, unused).
- `net client` uses the local client (`005699F0` +0x1C) unchecked; `fadeout` uses the fader
  `007C7D68` unchecked; kick/ban with a lone `"` strip it to an empty name; AddText reads the first
  piece unchecked.

## Not rewritten

- `005DC230` underflow (4 bytes, too short for a jump), `005DF5A0` memusage and `005DFC10`
  disconnect (thunks), `005DC080` (cheat map destructor at exit), `005E0A90`-`005E13D0` (STLport
  `_Rb_tree` template members), `005E3700` (Rebirth patches it).
- `005DF3A0` join, `005DF490` server: build network session descriptors (`00661050`, `006628D0`,
  `00581730`/`00581760`), start network games, not testable in single player.
- `005DFDA0` generateacks (writes scenack.txt from the units' acknowledgement tables,
  `005DFC20`/`005DFCE0`): left for later.
- `005E1580`-`005F0000`: other subsystems.

## Scenario

`console.txt` (about 30 s): public commands and tokenizer (case, extra spaces, quotes) in the menu,
the cheat word, every rewritten command's messages and settings (the filter mask ends at its start
value 0), then in a game the script lines (spaces inside and outside quotes, a script error),
listvar/loadvar round trip through `.\saves\` (redirected to harness_saves), give, and the console
window's own input.
