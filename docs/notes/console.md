# Developer console

Code: `console.h` / `console.cpp` (streams, executor, tokenizer, script lines), `console_commands.cpp` (handlers),
`console_window.cpp` (window text and placement, drawing and key input: `ui.md`), `console_addon.cpp` (Rebirth's
commands and AutoExec, `rebirth.md`). Scenario: `tests/scenarios/console.txt`
(public commands, the tokenizer's cases, the cheat word, every command's messages and settings, script lines,
listvar / loadvar through `.\saves\`, give, the window's own input).

## Output

- `console::Stream` (console.h) on a `ConsoleStreambuf`: inserters for text, char, int ("%d"), unsigned ("%u") and
  double ("%.6g" through `str::Format`, VC6's digits); `console::EndLine` is "'\n' and flush". A message is
  `os << text << '\n' << flush`.
- Categories: the console itself (0) and 1 ai, 2 event, 4 graphics, 8 rpg, 0x10 unitacks, 0x20 net, -1 all, each its
  own streambuf and stream (`g_consoleOut`, `g_aiLogOut`, ...). A category shows when it is 0 or its bit is in the
  filter mask. The streambuf keeps MSVC 6's put area: a hidden category's characters fill it (up to 0x400) and show
  with the next line once the category is shown again; a sync of a hidden category hands an empty line to the window
  (the harness drops empty lines). Sync hands the line to the console window through the interface and empties it.
- AddText splits at '\n': the first piece continues the last line, so each message line ends with an empty line that
  the next message fills. Visible lines: `(height - 24s) / 15s - 1` with s = height43 / 600 (ui.md, "Console window";
  7 lines in the default 150-unit window at any size). The harness reads each synced line through
  `console::g_consoleLineObserver`.

## Executor

Every call registers all commands again in two `std::map<std::string, handler>` (public, and after the cheat word).
The tokenizer: quotes toggle and stay in the token, spaces end a token only outside quotes, a double-byte lead byte
takes its trail byte. The first token is the command.
- `@x`, `#x`, `$x` (cheat word given, not a network game): loads `WorldScript(x args)`, `WorldScript( ConsoleFloat(
  ... ))` or `WorldScript( ConsoleString( ... ))` into the mission script (one tick when it loaded) and prints `Script
  error: <text>` exactly when the script's file count did not grow.
- Otherwise the command word is lower-cased (the arguments keep their case; handlers compare keywords without case);
  `quit` / `exit` quit; a public handler runs; without the cheat word the command XOR 0x66 is compared with
  "thingamabob" (sets the flag, prints `Activated!`); with it the cheat map's handler runs (not in a network game);
  anything else prints `Unknown command`.
- Public: console, lastfps, exec, execute, kick, ban, fps, rate, localrate. After the cheat word: help, history,
  filter, listvar, loadvar, give, fadeout, join, server, memusage, lootall, net, disconnect, generateacks, days,
  partyresend. DEBUGINFO and show are gone (they drove nothing): `Unknown command`. The goldens keep the original's
  text: tools/golden.py `REMOVED_CONSOLE` lists the removed commands' lines, dropped from both sides before comparing
  (add them there when a command is removed).

## Original bugs kept

Visible behaviour of the original kept until the user decides (rewrite-guide rule 9). Crashes, undefined behaviour and
invisible bookkeeping are fixed, not listed.

None left. The cheat word must match the whole command; `fps` without 1 / on or 0 / off (or with no argument) toggles
the frame rate.

