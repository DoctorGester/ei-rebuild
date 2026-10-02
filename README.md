# Evil Islands (Проклятые Земли) rebuild

Readable C++ source for the Russian `game.exe` 1.06, rebuilt with modern MSVC (x86). The goal is
identical behaviour, not identical bytes.

While the rewrite is incomplete, `ei_hook.dll` is loaded into the original `game.exe` and each
rewritten function replaces the original with a jump. Everything else still runs the original code.

## Build

Requires the VS 2022 Build Tools (MSVC 14.43, x86) at `E:\bin\vs\buildtools` (edit `build.cmd` if elsewhere).

```
build.cmd release    # rewritten functions replace the originals
build.cmd verify     # each rewritten function runs next to the original; differences are logged
```

## Run

```
build\verify\ei_launch.exe
```

Starts `F:\games\Evil Islands (Rebirth)\starter\Engine\game.exe` the way the Rebirth starter does
(working directory = game root, so the Rebirth addon still loads) and injects `ei_hook.dll`.
Use `--game <dir>` for another install. Nothing in the game folder is modified.
The log is written next to the DLL: `build\<config>\ei_hook.log`.

## Layout

- `src/game/` rewritten game code. Each function notes its original address.
- `src/hook/` the replacement framework (`include/ei/hook.h`).
- `tests/verify/` differential wrappers compiled into the `verify` build.
- `tools/` launcher, Ghidra export (`export_functions.py`), PE inspection (`pe.py`).
- `config/functions.csv` every function in game.exe with kind (game/lib/eh/thunk) and status.
- `docs/` survey and notes.
