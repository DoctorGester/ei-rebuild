# Evil Islands (RU) decompilation: survey and proposed plan

Survey date: 2026-10-02. Source install: `F:\games\Evil Islands (Rebirth)` (read-only, nothing there was modified).

## 1. What is in the install

| File | Size | What it is |
|---|---|---|
| `game.exe` (root) | 3,825,725 | Main executable, **patched** (see 1.1) |
| `starter\Engine\game.exe` | 3,825,725 | Same build, closer to the original (see 1.1) |
| `2dintmmx.dll` | 94,208 | Nival 2D/interface renderer (MMX), own code, VC6, 2000-01-25 |
| `3dfpfpu.dll` | 151,552 | Nival 3D software/FPU module, own code, VC6, 2000-01-25 |
| `binkw32.dll`, `mss32.dll`, `mp3dec.asi` | | RAD Bink video and Miles Sound System (third-party, imported, not rebuilt) |
| `EI_Plugin.asi` | | Rebirth mod runtime patcher, loaded by Miles. Not part of the original game |
| `starter\*` (EIStarter, addon.dll, dgVoodoo DLLs) | | Rebirth launcher and dgVoodoo wrapper. Out of scope |

game.exe loads `2DintMMX.dll` and `3DfpFPU.dll` dynamically (by name string, not in the import table).

### 1.1 Which game.exe is the matching target

The two copies differ in exactly **2 bytes**:

| File offset | Root copy | Engine copy | Meaning |
|---|---|---|---|
| `0x13E` | `0x2F` | `0x0F` | PE characteristics: root copy has the "Large Address Aware" (>2 GB) patch |
| `0x56BB5` (VA `0x456BB5`) | `EB` (jmp) | `74` (jz) | After `GetVolumeInformationA` + string compare, before a "Warning" MessageBox retry loop: root copy has the **CD check disabled** |

**Target = `starter\Engine\game.exe`** (SHA-256 `8ED3F9A5…6C1`). It is the GOG 1.06 build with no known byte patches. I cannot prove it is fully pristine without an independent retail/GOG copy; if you have one, a hash comparison would settle it.

## 2. Build fingerprint (game.exe)

- **PE timestamp:** 2001-03-26 09:36:51 UTC (patch 1.06 era).
- **PDB path (CodeView NB10):** `D:\home\Allods3\Game\ReleaseRussian\Game.pdb` (the PDB itself is not shipped). Confirms this is the Russian release configuration; internal project name "Allods3".
- **Linker:** 6.00 (MSVC 6 / Visual Studio 98). Image base `0x400000`, section/file alignment `0x1000`/`0x1000`, subsystem GUI 4.0, no relocations, checksum 0.
- **Sections:** `.text` 0x339670 (≈3.3 MB of code), `.rdata` 0x478B2, `.data` 0x49298 (raw 0x19000), `.rsrc` 0x92C8. 61 bytes of overlay = the NB10 debug record.

### 2.1 Rich header (decoded)

| Tool | Build | Objects | Likely origin (inferred) |
|---|---|---|---|
| Utc12 C++ (cl 12.00) | **8966** | **210** | **The game's own C++ sources** |
| Utc12 C++ | 8569 | 50 | Another static lib or subproject built with a different VC6 service pack |
| Utc12 C | 8047 | 173 | Static C runtime (LIBC/LIBCMT) |
| Utc12 C / C++ | 8168 | 1 / 8 | VC6 RTM objects (MFC/CRT startup pieces) |
| Masm 6.13 | 7299 | 41 | CRT asm (memcpy, strlen, chkstk…) |
| AliasObj 6.0 | 7291 | 6 | CRT alias objects |
| Linker 6.00 | 8168 / 8447 | 49 / 15 | Import libs and MFC libs |
| Linker 5.12 | 8034 / 9049 | 20 / 10 | Older import libs (DirectX SDK, Bink, Miles) |
| Cvtres 5.00 | 1735 | 1 | Resource file |

So: **Visual C++ 6.0, compiler build 12.00.8966** for the game code. 8966 is a late VC6 compiler build (Service Pack 5 with the Processor Pack, as far as I know); the exact service pack should be confirmed by compiling a test function and checking that the Rich entry and codegen match. 8569 appears for 50 objects, which suggests a second library compiled by a different SP (candidate: STLport or a Nival engine lib).

### 2.2 Libraries linked in

- **MFC 4.2, statically linked** (RTTI for CWinApp, CWnd, CDC, CFile, CArchive exceptions, CMemFile, CStdioFile, CRecentFileList, CTL3D state; WINSPOOL and metafile GDI imports are MFC tells).
- **Static CRT** (no msvcrt.dll import) plus old-style iostreams (`ios`, `ostream`, `ostream_withassign`).
- **STLport** (`stlport::length_error`, `out_of_range`, `logic_error`). Version still to identify (likely 4.x).
- Only 61 RTTI type descriptors in total, all from MFC, STLport, iostream and CRT. **The game's own classes were compiled without RTTI (/GR-)** or have no virtuals that need it; class recovery will come from vtables, not RTTI names.
- Imported DLLs: Bink (12 functions), Miles MSS (47), WSOCK32 by ordinal (multiplayer, server `a3master.nival.com`), DirectDraw/Direct3D 7 era (`DirectDrawCreateEx`), DirectInput (`DirectInputCreateEx`), DirectSound, WINMM, plus KERNEL32 (159), USER32 (151), GDI32 (83), ADVAPI32, SHELL32, ole32, COMCTL32, comdlg32, WINSPOOL. `D3D8.DLL` and `Riched20.dll` are referenced by name for dynamic loading.

### 2.3 The two engine DLLs

Both are VC6 (compiler 8168 + 8447, linker 6.00.8447), base `0x10000000`, with relocations, MFC statically linked too. Small (52 KB and 105 KB of code). They are a separate, older build (January 2000) and can be a later phase.

## 3. Environment status

- **Ghidra MCP:** the tools are available but the Ghidra bridge is not reachable (connection refused on `127.0.0.1:8080`). Ghidra needs to be running with the GhidraMCP plugin server started and `starter\Engine\game.exe` imported and auto-analysed.
- **MSVC:** no compiler is installed. Only the Visual Studio Installer stub exists. A modern MSVC cannot produce matching code; VC6 is required.
- Python 3.14 is available (no `pefile`; I used a small stdlib-only PE parser instead).

## 4. Proposed project layout (`E:\workspace\ei`)

```
ei/
  orig/                 hashes + notes only; the original exe is referenced by path, not copied into git
  config/
    functions.csv       address, size, name, source file, status (unknown / lib / matching / nonmatching)
    globals.csv         data symbols with addresses and types
    build.json          compiler flags per source file / per library
  include/              recovered headers (structs, classes, vtables)
  src/
    game/ engine/ ui/ net/ sound/ ...   game C++ sources, split by subsystem as recovered
  lib/                  third-party stubs: Bink/MSS/DirectX headers, STLport version used
  tools/
    pe.py               PE / Rich header parser (from this survey)
    ghidra_export.py    dumps functions, xrefs and vtables from Ghidra to config/
    compare.py          per-function byte diff with relocation masking
    progress.py         % matched by bytes and by function count
  build/                obj files and the rebuilt exe (not committed)
  docs/
```

Git repository in `E:\workspace\ei` (local only until you say otherwise).

## 5. Function-by-function matching workflow

1. **Ghidra baseline.** Import `starter\Engine\game.exe`, auto-analyse, then export the full function list to `config/functions.csv`.
2. **Separate library code from game code.** Use Ghidra Function ID / FLIRT-style signatures built from VC6's own `nafxcw.lib` (MFC), `libcmt.lib`/`libc.lib`, and the STLport version. Those functions are marked `lib` and are *linked*, never decompiled. Expect a large share of the 3.3 MB to be MFC/CRT/STL template code.
3. **Recover types first.** Vtables, class layouts, global structures into `include/`, driven by Ghidra renames.
4. **Per function loop:**
   - Write C++ in `src/…` from the Ghidra decompilation.
   - Compile that single translation unit with VC6 `cl /c` using the flags inferred for the game (starting point: `/O2 /Ob1 /GX /MT /GR-`, refined by experiment).
   - `compare.py` pulls the function's bytes out of the COFF `.obj`, pulls the same range from the original exe, masks out relocated operands (call targets, global addresses) and reports *exact match / differs at offset N*.
   - Iterate until the masked bytes are identical, then mark `matching`. Functions that resist get `nonmatching` with a note, and the original bytes are kept as an asm stub so the build still links.
5. **Whole-exe check (later phase).** Link with VC6 `link.exe` 6.00 using function order (`/ORDER`) and object order matching the original layout, then byte-compare the full image section by section. This only becomes meaningful once most functions match; until then the per-function comparison is the measure of progress.

## 6. What I need from you

1. **Ghidra:** start Ghidra with the GhidraMCP plugin and `starter\Engine\game.exe` loaded (or tell me if you'd prefer I set this up).
2. **Compiler:** Visual C++ 6.0 plus Service Pack 5 and the Processor Pack (or SP6, then we check which matches build 8966). I will not download or install anything without your go-ahead; if you have an installer or ISO, point me to it.
3. **Scope:** I'd start with game.exe only and leave the two engine DLLs for later. Say if you want them in scope from the start.
4. **Optional:** a hash of an untouched retail or GOG 1.06 Russian game.exe, to confirm the target is pristine.
