# Platform layer: effort estimate (2026-10-09)

Question: what it takes to isolate the platform so the rebuilt game can run on other operating systems.
The counts below come from game.exe's import table and from grepping `src/` at main 2026-10-09.

## What the game depends on

| Area | Original dependency | Size of the surface | Where it stands |
|---|---|---|---|
| Window, messages, cursor | USER32 (151 imports) | the main loop, nested message pumps, `gameapi::` calls from 76 files | SDL3 was planned for the standalone exe (not downloaded yet) |
| 2D text and surfaces | GDI32 (83), RichEdit (credits), comdlg32 / WINSPOOL (1 / 3) | font text drawn through DCs onto interface surfaces; `CDC`, `HDC` | needs a font rasteriser (stb_truetype / FreeType) behind the same calls |
| Rendering | DDRAW (2 imports), D3D7 through COM | `IDirectDrawSurface7` 38, `IDirect3DDevice7` 28 references | **already isolated**: the new renderer (sokol_gfx) sits behind the DDraw7 / D3D7 boundary; sokol has GL / Metal / Vulkan backends |
| Sound | Miles mss32 (47), DSOUND (1) | `AIL_*` calls in the sound manager | replace with SDL3 audio or miniaudio |
| Video | Bink binkw32 (12) | intro and loading movies | Bink 1 decoder needed (FFmpeg has one), or re-encode the movies |
| Input | DINPUT (1), window messages | the cursor thread, keyboard through messages | SDL3 |
| Network | WSOCK32 (22, by ordinal) | sessions, UDP | BSD sockets map almost 1:1 |
| Files, threads, time | KERNEL32 (159), WINMM (5), ADVAPI32 registry (11) | `CFile` 93 references, critical sections ~130 calls, `timeGetTime` 31 | std::filesystem / std::thread / std::chrono; registry becomes a config file |
| Runtime libraries | MFC 4.2, STLport and the MSVC6 CRT, statically linked | src/lib/mfc.h, stlp.h, crt.h already model them | must become our own code (no MFC on other systems) |
| Win32 types in game headers | `HWND`, `HDC`, `RECT`, `POINT`, `COLORREF`, `HANDLE` | 92 files | portable typedefs in a platform header |

## The real blockers (in order of cost)

1. **The port is not finished.** 74% of game functions are rewritten. The rest, plus the statically linked MFC / STLport / CRT, still run from game.exe through the hook DLL; the code holds about 13,500 references to fixed game addresses (forwarders and globals). A cross-platform build needs the standalone exe first: every function ours, every global a real variable.
2. **32-bit layouts.** 1,806 offset / size asserts, about 730 pointer members in asserted structs, and 69 raw struct reads and writes on streams. The layouts mirror the original so that hooked and original code share memory. A 32-bit Linux build can keep them. A 64-bit build (macOS has no 32-bit) needs save files, resource files and network messages read field by field, and the asserts relaxed once the original no longer shares memory.
3. **x87 floating point.** 222 `__asm` blocks and code that relies on 24-bit x87 precision. On other CPUs (ARM, or SSE on x86-64) results stop being bit-exact. That is acceptable for single player (the tests use tolerances). Multiplayer with original clients would desync.
4. **MSVC specifics.** About 10,800 `__fastcall` / `__thiscall` / `__stdcall` / naked annotations. They exist to match the original's calling conventions for the hooks and become plain functions in the standalone build. Mechanical work, but large.

## Effort, in worker tickets (about one focused worker session each)

| Phase | Work | Tickets |
|---|---|---|
| 0 | Finish porting (~26% of game code) and the standalone exe: globals as variables, forwarders gone, own MFC / STLport / CRT pieces | 25-35 (the bulk; already the main line of work) |
| 1 | `src/platform/` interface (window and events, time, threads and locks, files and directories, config, sockets) with a Windows backend over the current `gameapi::` calls; migrate the ~76 + 92 files | 8-12 |
| 2 | Replace binary middleware: Miles to SDL3 / miniaudio, Bink decoder, DirectInput to SDL3, Winsock to BSD sockets, GDI text to a font rasteriser | 6-10 (needs dependency approvals) |
| 3 | Build with clang for 32-bit Linux (i686): drop `__asm` for portable maths, drop calling-convention annotations, fix compiler differences | 5-8 |
| 4 | 64-bit and ARM (macOS, Linux arm64): serialise by field, relax layout asserts, pointer-size fixes | 10-20 |

Phases 1 and 2 can start before phase 0 ends, because the platform calls go through `gameapi::` already.
The cheapest first target after the standalone exe is 32-bit Linux (phases 1-3). macOS and arm64 need phase 4 as well.
