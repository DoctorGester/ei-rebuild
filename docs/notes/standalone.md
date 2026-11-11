# The standalone exe: the image, the library ports, UTF-8

The product is `ei-rebuild.exe`: an ordinary x64 exe with all of the game linked in. Dropped into a game folder it plays
from there ("The drop-in exe"). It runs no game.exe and reads none. How changes are checked:
docs/notes/standalone-verification.md.

## 1. game.exe's data and addresses

The code still names some of game.exe's data by the original's addresses: comments, the tagged definitions of
"Named data (C18)", the seam's arguments in "Own data (S18)". A few tools read game.exe to make or check them; the
build reads no game.exe.

**Finding game.exe.** Every tool that reads game.exe goes through `tools/exe_path.py`. That covers
retired_vtables without `--check` and named_data `--emit`. The lookup order:
1. `--exe` (or `EI_EXE`);
2. `run_all.run_dir(root)`'s `vanilla\game.exe`, then `rebirth\game.exe` (a worktree without a run\ junction uses the
   main checkout's).
If nothing is found, it gives one error naming the paths it looked in. Vanilla's and Rebirth's game.exe 1.06 differ in
one .text byte (file offset 0x56BB6) and nothing else, so the data the tools read is the same.

**Strings are literals.** A string the original passed by address is a literal with the same bytes. The literals were
read from both exes by a generator (C11) that is gone, not typed by hand.

The original's empty string (0079B298) has no name here: its readers copy or compare the text (none compares the
pointer), so a reader tests `empty()`, a writer calls `clear()` and a fallback passes `""`. A pointer-sized cell
read as `const char* const*` holds the address of the text, not the text: read the cell's pointer first.

## 2. The image

### How it runs

- **The program** (src/main.cpp, every system). `ei-rebuild [-- <game args>]` can be started from anywhere. Its main
  does this:
  1. enters the game folder: EI_GAME_ROOT when a launcher names it, else the exe's own;
  2. reads the settings file's game paths;
  3. sets EI_Plugin's values ("The Rebirth install");
  4. starts the renderer's device in the background;
  5. calls WinMain (`app::GameMain`) with what follows `--` (`platform::ArgumentsAfterSeparator`).

  WinMain's first lines do what game.exe's static initialisers did beyond setting values: the sessions' pointers, the
  coder's tables, Winsock, the weather's curves, and the exit functions in the game's order. Every value the
  initialisers set is a definition's initialiser.
- **Resources**: the exe's one resource is its own VERSIONINFO (`src/main.rc`). The window title "Evil Islands" and
  the class "EIGAME" are literals in app/main_loop.cpp. The window class has no icon.
- **A crash** in a harness run is reported by the harness: its crash log, tools/resolve_crash.py. run_all reports a
  game that ended by itself while its report still says RUNNING (`FAILED: the game ended ...`).
- **Suite.** `run_all` / `work.py test` run two installs:
  - `standalone`: the vanilla data;
  - `standalone-rebirth`: the Rebirth data (the same code; DisableExitOnCrashReport on).

  run_all starts the exe with EI_GAME_ROOT the install whose data a run plays (EI_GAME_VANILLA, EI_GAME_REBIRTH).

### Own data (S18)

The image holds nothing of game.exe's sections. Every value the code reads is one of three things:
- a named global of ours ("Named data (C18)");
- a tagged literal;
- a compiler-made vtable.

- **The seam**. No code names game.exe's data by the original's address but the harness's `ExeTableId(a)`, a
  game.exe table kept as an id and never read (src/'s `GameData` / `GamePointer` / `DataId` had no users and went).
- **The check** (`python tools/data_seam.py --check`, a build check). Every literal in game.exe's data range
  (0073B000-007D7000) in src/ and tests/ must be one of these, or the check fails:
  - an argument to the seam;
  - an argument to a helper whose every definition hands the address to the seam;
  - a constant whose every use does.

  `--list <section>` shows the raw uses.
- **.rdata constants as literals** (made by a generator that is gone, S18e). A float, double or integer cell that no code writes is
  the value in the source, tagged with its address. The type is the spelling:

  | Form | Means |
  |---|---|
  | `0.5f /*0073B86C*/` | a float (dword) |
  | `3.141592653589793 /*0073CAE8*/` | a double (qword) |
  | an integer literal | the dword |
  | `/*addr f*/` | a float widened to double |
  | `(-1.0f /*addr*/)` | a negative value, in parentheses |

  The literals were checked against game.exe's bytes when they were made.
  - **Folds:** the compiler folds a product of constants where the original multiplied at run time on the main
    thread. Where the rounding of that fold matters, write `static_cast<float>(a * b)`.
  - The pi * (1/180) product is `ei::kRadiansPerDegree` (src/engine/core/core_maths.h).
  - The cells addon.dll writes are not literals: the WideScreen layout widths 00742B08 and 007428AC are
    `screen_layout::g_layoutWidth` / `g_panelRight`, and tests/verify/addon_sites.inc lists the sites.
- **No table of game.exe's imports.** The exe's imports are its own, from the linker, and the platform calls the
  system directly (the wide calls for text). The slots of game.exe's imports that the program once kept existed only
  for the harness to patch; the harness now answers the calls it redirects at link time (docs/notes/platform.md, "The
  harness's answers").

### Named data (C18)

game.exe's tables and objects that our code reads are typed C++ definitions. Each initialiser carries the original's
address as a tag right after the `=`:
`constexpr int kSlotXs[7] = /*0074111C*/ {300, 500, 200, 400, 600, 75, 725};`. The code names the global, never the
address.
- **Where:** in the file that reads it (its anonymous namespace), or `inline constexpr` in the subsystem's header when
  several files read it.
- **What is not a table:** a single number is an rdata literal ("Own data (S18)"), not a table.
- **String pointers:** a table of them is `const char* const kX[] = /*addr*/ {"a", "b"};`. A struct with pointer
  fields takes string literals, names and nullptr in its initialiser.
- **const follows our code, not the section:** a table no code of ours writes is `constexpr`. An object the code
  writes is plain, with a `g_` prefix: `float g_bagSizes[12] = /*007A9BFC*/ {};`.
- **Arrays:** a `char` buffer is `char g_x[256] = /*addr*/ {""};`. A table of names is `char name[rows][cols][n]` of
  string literals.

**The check** (`python tools/named_data.py --check`, a build check, no game.exe):
- **Initialisers:** every tagged initialiser packs by its declared element type (`TYPES`). Nested braces are
  flattened, and a `[n]` bound must be decimal and match the count.
- **Placement:** the range lies in .rdata past the import table, or in .data and its zero-filled tail. A range in
  .rdata is constexpr unless the code writes it.
- **The csv:** config/named_data.csv lists exactly the tagged ranges; `--update` writes it.
- **Nothing else reaches a range:**
  - no address literal of src/ or tests/ lies inside it;
  - no addon.dll site does (except `NAMED_ADDON_SITES`);
  - no retired vtable or unread object (config/unread_data.csv) does;
  - no two ranges overlap.
- **Pointer elements** (TYPES `P`) name what game.exe's pointer pointed at:
  - a string literal;
  - another named range (or `NAME + k`, its element k);
  - a function of ours;
  - nullptr.

  A pointer to data that is not ours has no form, so a table that holds one stays data.

**Adding one:**
1. `named_data.py --split [KIND ...]` counts what is left by kind and owner area. `--refs BEGIN END` lists the
   original's absolute operands into a range, by function.
2. `--emit ADDRESS TYPE COUNT NAME` prints the definition from game.exe's bytes. For COUNT, `4x10` gives rows.
3. Paste the definition, then run `--update`.
4. `--rewrite NAME [--apply]` turns the `GamePointer` / `GameData` reads into the name.

Rules learned:
- **Blocks:** an address that looks like a lone object can be a member of a larger one the code reaches by its base.
  Check a candidate against the logic settings (007C53F8, `GameLogicSettings()`) and the options (0079BC70,
  `options::g_options`) first. An object inside another stays at its address until the whole enclosing object is
  named.
- **A table that follows another** can be one extent to code that indexes across both. The key actions 00787070
  follow the 88 key names, and the code took the actions' address as the names' end. Name the whole run of tables,
  and take an end as begin + count.
- **An object's start** is where the previous one ends, not the base the code indexes from. core/coder.cpp's length tables
  are indexed from one entry before their start, so ours take `length - 1`.
- **Not a global:** "campaign mode" 007C2CC4 is the server world's `zoneMode`. Naming it apart left its readers on
  the world's own copy.

**The static initialisers' objects.** The objects game.exe's static initialisers built are plain globals of ours: the
C++ runtime constructs them before the game starts and destroys them after the game's exit functions. That covers the
databases, the zone table, the frame's draw lists, the options, the logic settings, the camera, the scene renderer, the
console's streams, the network buffers and the draws' work arrays. A function-local static of the original is a
function-local static or a local of ours.

Three kinds of object keep the game's exit order instead:
- **The worlds** (`g_serverWorld`, `g_clientWorld`, world_create.cpp). Their Destroy disconnects players and releases
  sessions, managers and textures. So GameMain registers `EmptyWorlds` with atexit before it creates them, and it runs
  before every exit function of the list, also on a fatal exit. Their destructors then find them empty.
- **The game server / client Refs** (net_session.cpp). Their exit functions release the sessions, which send leave
  packets.
- **Containers whose exit function frees into the game's order** are `constinit` with no destructor, their exit
  function ported beside them and registered with `std::atexit`.


### Compiler-made vtables (C19)

Every class of ours with virtual functions is a real C++ class, and its vtable is the compiler's.

**config/vtables.csv** (made once from game.exe by a finder that is gone) lists game.exe's 608 tables. Each row gives the address, slot
count, RTTI name, kind, slot 0, class, file and a note. Every row is retired: the class and its declaring file are
named (`-` for a table no class of ours has).

**`tools/retired_vtables.py --check`** (a build check) checks the rows against the sources. named_data.py checks that
nothing names an address inside a retired table.

- **Not vtables:** some dwords only look like tables. These are strings whose dword falls in .text ("%ld" 0074676C,
  "dde" 0074718C, "a/p" 007474B8, "OCP" / "ACP" 00747E2C / 30, "ime" 00746E60) and the EH unwind entries (kind `eh`).
  Never retire them.
- **Tails:** The csv splits a table at every address the code holds as an operand, so some rows are the tail
  of the row before.
  - **Why:** the split happens at a devirtualised slot call (`call [0073BD98]` is MemoryStream's slot 2, so
    0073BD90's 8 slots are listed as 2 + 6), or at a byte match inside an instruction.
  - **Danger:** a tail is never stored, and retiring it alone zeroes live slots.
  - **Rule:** a table ends after its first slot when the next address is one the code stores. An address the code only
    calls through splits only after the second slot.
- **Ids for the harness:** the digest's AI modes keep game.exe's table addresses as ids (`ModeKinds()`, `ExeTableId`;
  the goldens hash them). The harness names windows by name, mapped to our classes by `typeid`. Changing an id means
  re-recording the goldens that hold it.
- **Slot conventions:**
  - a class declares the slots in game.exe's table order;
  - `_purecall` slots are pure;
  - a slot whose original leaves EAX unset returns 0, or is void only where nothing reads the result;
  - a base stays non-final where the original calls through the table. A devirtualised call let the client world's
    slot 29 inline slot 27's float body, and the bow height moved.
- **Trap:** a class constant left in a derived class compiles against the base's. The item tags once stored
  EngineObject's table and every run hit `_purecall`. To remove a constant, declare a poison member first.

### Real classes (C28)

Classes are ordinary C++: constructors, virtual destructors, virtual overrides with qualified base calls, and
`ClassName` / `GetClassId` overrides.
- **Destroy:** the destructor calls its own Destroy qualified (`Item::Destroy()`), as the original's does.
- **Allocation:**
  - `new T()`.
  - EngineObject has the game heap's `operator new`: noexcept, null without memory, and then no constructor runs, which
    the original's callers test. It also has `operator delete` and the placement pair.
  - A derived class declares no `operator delete` of its own: it would hide the placement delete (C4291).
  - An object built in storage someone else allocates is `new (place) X()` and `place->~X()`.
  - An object embedded in an owner that builds it is a `lib::InPlace<X>` member (PartyStorage).
- **Members:** members are trivially default-constructible or initialised in the constructor's list as the original
  does. A constructor default-initialises what it does not name.
- **The window base** (ui.h): game.exe's windows had a second base for the input (12 slots); ours take every input
  event through one virtual, `UiWindow::OnEvent(UiEvent&)`, and `UiContainer::OnEvent` is the dispatch. Screens call
  the container's qualified (`UiContainer::OnEvent(event)`).

### In place of game.exe's module

- **Command line:** `ei-rebuild ... -- <game args>` is WinMain's line. Nothing of ours calls GetCommandLineA.
- **Hook points:** code bytes the harness reached in game.exe are hook points in ours (`ei::mainloop::g_harnessHooks`,
  app/harness_hooks.h).
- **Rebirth:** what addon.dll and EI_Plugin patch is the code's own, with no flag (docs/notes/rebirth.md); the
  start-up sets EI_Plugin's values first ("The Rebirth install"). EI_Plugin.asi never loads: the image loads no Miles,
  which is what loaded `*.asi`. `starter::RebirthData()` is true on a Rebirth install's data.
- **Mod folders** (src/game/mod_folders.{h,cpp}): Maps, ResOverride, CameraPatch, StreamPatch and MoviesPatch work as
  addon.dll does them (rebirth.md, "Resources and paths"), whenever a mod is played (`[game] Mod`).
  - **Checked by:** five scenarios, each with a mod written into the run's `harness_saves\s19b_mod` / `s19d_mod` at
    prepare time from the vanilla install's own files (copies, repacked archives, made-up bytes; nothing committed):
    `rebirth_mod_folders` (`tools/make_test_mod.py`: one override archive, the MapPath site, CameraPatch),
    `rebirth_mod_stream_movies` (StreamPatch, MoviesPatch), `rebirth_mod_maps` (the new zone's map and mob, the client's
    zone load, MapFileExists); the last two are `tools/make_mod_s19d.py stream|maps`. Each file a scenario's mod holds is
    asked for with the steps `read` / `wrote` / `waitread` (the files the game touched: `tests/verify/harness_modtrace.h`),
    `modfile` / `modlist` / `modarchive` and `mapfile` (harness.cpp's header); "playstream" asks the music stream for
    a track at the main menu (its steps follow the virtual clock, so the wait is the same every run).
  - **A mod file is also asked about** (IsFile) before the game chooses between the mod's and its own, so "the mod's
    file was touched" cannot tell the choice by itself: a check pairs it with the game's path for the same name,
    untouched.

### Settings file

The image keeps its settings in **ei_settings.ini**, a plain ini of its own (src/engine/data/settings.h). It never reads or
writes the registry.

- **What each setting does:** `docs/settings.md`, generated by `tools/gen_settings_doc.py` from
  `src/engine/data/settings_catalog.inc` (section, name, typed default, written to a new file or not, values,
  description; `settings::Describe`). The defaults file carries each line's description as the comment above it; a
  file that already exists gains no comments. The build check fails when the doc is stale, when a setting the code
  reads has no row (or a row nothing reads), or when a read passes a default of its own.
- **One default per setting:** the catalog row's (`Int(n)`, `Bool(b)`, `Float(f)`, `Text("...")`, or
  `DesktopSize()`, which the machine decides: `settings::Machine`, the harness's desktop or the primary monitor's).
  `settings::Defaults` writes the rows marked `kWritten`; `GetInt / GetBool / GetFloat / GetString` take no default
  argument and return the row's (`settings::DefaultText` read as their type) when the value is absent or not of its
  type. GameOptions' fields have no defaults of their own: Load sets every one.
- **Where:** the first place that holds a file: `platform::DefaultSettingsFolder()` (next to the exe; on macOS
  `~/Library/Application Support/ei-rebuild`, since the exe is inside the app), then `%LOCALAPPDATA%\EvilIslands\ei_settings.ini`.
  - **Neither exists:** the defaults are written to the first place that takes them: next to the exe, else the per-user
    one. So an exe in a folder the user cannot write keeps its options per user.
  - **Nowhere writable:** the session runs on the defaults in memory.
  - **The file stops being writable:** it logs `cannot write` at each options save and keeps the change for the
    session only.
- **A file that exists but cannot be read is never written over.** Only a missing file (ERROR_FILE_NOT_FOUND /
  ERROR_PATH_NOT_FOUND) gets the defaults written. Any other failure logs `cannot read`, runs on the defaults and writes
  nothing:
  - a sharing violation;
  - access denied;
  - a cloud placeholder that cannot hydrate;
  - a read error;
  - a file of 16 MB or more.

  The read shares read, write and delete, so an editor holding the file does not stop it. A file that holds no setting
  (empty, or every line not understood) is replaced by the defaults, the same as a missing one.
- **The game folder:** main (src/main.cpp) makes the game folder the working directory first: EI_GAME_ROOT's when it is
  set, else the exe's own folder.
  - **What that fixes:** the game's relative paths (Work path `.\`, Resources path `.\res\`, saves,
    mods) are the exe's folder, whatever the shortcut's "Start in" says.
  - **Why it works:** the defaults hold no paths, so a moved game folder keeps working.
- **Format:**
  ```
  ; a comment (';' or '#' first; kept when the game rewrites the file)
  [video]
  VideoResolution = 1920x1080
  FullScreen = true
  Brightness = 0.5
  ```
  **Sections:**

  | Section | Holds |
  |---|---|
  | paths, video, sound, shadows, terrain, camera, game, network, statistics | the game's options (options.cpp's tables; value names are the original's registry value names) |
  | renderer | Msaa, Anisotropy, VSync, PerfOverlay |
  | patches | nothing: the flags of older builds stay in the file, unread |
  | autoexec | Rebirth's Console AutoExec lines, one value each |

  **Values:**
  - A value is the text after the first `=`, trimmed. It is in double quotes when it starts or ends with a space or
    starts with a quote.
  - Numbers are decimal (up to 4294967295, read as the int of those bits).
  - Floats are written as short as reads back to the same float (std::to_chars).
  - Booleans are true / false; 1 / 0, yes / no and on / off read too.

  **Names and lines:**
  - Sections and names compare without case.
  - A bad section name and its lines are skipped and reported.
  - A name cannot hold `=` or start with `[`, `;` or `#`.
  - A line the parser does not understand is dropped on the next save (logged).
- **Defaults** (`settings::Defaults`, settings_document.cpp: the catalog's `kWritten` rows in its order, a run of
  `<n>` rows shortcut by shortcut). They are made for the primary monitor's mode in physical pixels; with no size
  known, the defaults for 1024x768 are used in memory and not written. A new file must stay byte for byte what
  tests/unit/settings_defaults.ini holds (the unit check; a change on purpose rewrites it).
  - **video:** the monitor's resolution, FullScreen true (a borderless window over the monitor), Video 2.
  - **sound:** Audio, and the three volumes at 100.
  - **shadows:** Shadows true.
  - **camera:** the whole section as a game install writes it (config/harness/registry.reg).
  - **game:** SelectionType 1 and the master server a3master.nival.com.
  - **renderer:** VSync true, Msaa 8, Anisotropy 16, PerfOverlay false.
  - **Left out:** everything else, which the game reads as absent and takes the row's default for. The game's exit
    save then adds every option it keeps.
- **Who uses it:** `settings::GetInt / GetBool / GetFloat / GetString / GetValues / GetLines` and the setters.
  - **Writing:** the options loader and writer. `GameOptions::Save` (at exit and at the options window's OK) ends with
    `settings::Save`, which writes the file when a value changed, atomically through `ei_settings.ini.tmp`. The options
    window's graphics rows set `[renderer]` (renderer.md, "The graphics rows").
  - **Readers:** the AutoExec lines, mod_folders, and device_mesh's renderer section
    (renderer.md sections 11, 15, 15a; that section is not read in the harness build).
- **Read filters** change a value as it is read:
  - EI_GAME_ROOT's Work path.
- **Harness runs** (tests/verify/harness_settings.h): the harness pins `<run folder>\ei_settings.ini` and writes it at
  first use from config/harness/registry.reg. That file holds the install's option tree (Rebirth's when
  `starter::RebirthInstall`), with the scenario's starter lines in place.
  - **Read only:** the game's writes are dropped.
  - **Missing values:** a read of a value the file lacks fails the run, unless the snapshot marks it absent
    (`"name"=-`).
- **One game window:** WinMain's one-instance check would find a hidden harness game. So harness runs answer the
  check themselves, and title their window "<title> (harness)". When the check does end a game it logs `app: another
  game window exists ...`.
- **Checks:**
  - tests/unit/settings_file.cpp (in `ei_unit_check` and Linux's ei_port_check) covers the format and the defaults
    (the new file byte for byte against tests/unit/settings_defaults.ini).
  - `settingsfile writable` (mod_menu_list.txt, Rebirth) runs the game's exit save for real.
  - `settingsfile defaults` (settings_defaults.txt) starts with no file and a 1280x720 desktop.

  run_all compares the file after the game with the scenario's `# settings-file: settings/<name>.ini` and fails on a
  difference or a left `.tmp`. To change an expected file, copy the run's ei_settings.ini over it.

### The CRT (src/lib/crt*.cpp, namespace `libcrt`)

Most of the C runtime is the host's. What stays the game's, and why:

| Kept | Where | Why |
|---|---|---|
| `libcrt::Rand` / `Srand` | crt.cpp | VC6's LCG (seed * 214013 + 2531011, bits 16-30) on a thread_local seed. The game's draws are the game's: the host's rand would change every golden that draws. |
| `libcrt::Qsort` | crt.h (a template over the element type, typed comparator) | VC6's quicksort (middle pivot, selection sort up to 8, smaller part first). It orders equal elements its own way, and two callers show that order: the sector spiral's cells by distance and the ambience's class votes by weight. Where equal elements are identical, callers use std::sort. |
| `libcrt::Format` | crt_format.cpp | VC6's `_output` state machine. A malformed conversion prints as VC6 did, and the host never sees one: the UCRT's invalid-parameter handler would end the game on a run-time format from the game's texts. |

**Formatting** (`libcrt::Format`, behind `str::Format` / `str::FormatV`):
- Integers, characters and strings go to the host's `snprintf` with VC6's sizes spelled for it: 'l' and "ll" are 32
  bits, "I64" is 64, %p is "%.8X", and %n is ours.
- Floats are VC6's own: `$I10_OUTPUT` from a 12-byte long double. They differ from the host's like this:
  - rounding is half up;
  - three exponent digits;
  - "1#INF" / "1#IND" / "1#QNAN";
  - -0 prints as 0.

  The console's attack lines print "3.40282e+038", and goldens hold it.
- **std::format or str::Format:** a call with a literal format and no float conversion is `std::format` or
  `std::snprintf`. str::Format keeps the game's run-time formats, the float ones, and the "%.2d" of database ints
  ("%.2d" of -5 is "-05"; `{:02}` gives "-5").
- **When moving a call to std::format:**
  - a char prints as a character;
  - an unsigned value above 0x7FFFFFFF prints unsigned;
  - a null `const char*` throws;
  - a std::string prints whole, past a zero byte.

The rest:
- **Locale:** the host's "C" locale, never changed. The text is UTF-8 (section 4). No multibyte function is called.
- **Strings and characters:** std::strcmp / strncmp / strncpy, `str::Compare`, and text_case.h (`CompareIgnoringCase`,
  `LowerInPlace`, ASCII `ToLower` / `ToUpper`, `IsDigit` / `IsSpace` / `IsAlpha`: no class past ASCII). There are no
  lead bytes. Compared with VC6, the host's compares return a difference rather than -1 / 1, and atoi saturates. No
  golden depends on either.
- **Heap:** the host's malloc / free and `new` / `delete`. An exhausted heap throws std::bad_alloc.
- **atexit / exit:** the host's. std::exit runs the game's exit functions before the host's terminators.
- **Host functions:** atof, sscanf, fmod, modf, floor and ceil are the host's.
- **Conversions:** `_ftol` and FISTP are `static_cast<int>(static_cast<std::int64_t>(x))` and `lib::RoundEven`
  (float-maths.md, "Conversions and copies").
- **Files:** every open goes through platform/platform.h, which the harness answers in its builds for its save /
  install redirection.
  - `platform::File` is a Win32 handle; text lines are written with `WriteLine`.
  - `platform::ReadWholeFile` reads a whole file.
  - `platform::FolderEntries` does CFileFind's read-ahead.
  - `platform::PathExists` is `_access`.
  - The res archives keep a Win32 handle.
  - A truncating open of a just-written file, then a write past its end, can read the gap back as old bytes under load.
- **Not ported (no callers):** `_controlfp`, the old iostream (the console's streams are `console::Stream`), errno /
  `_matherr`, and VC6's exception filter.

### Rules from real bugs

- **NaN sort keys:** a NaN in a sort key makes a comparator irreflexive, and an unguarded scan runs off the array. The
  draw list sort crashed on a shadow-only object's never-written view depth; Object3D's constructor now zeroes it. Any
  comparator over a never-set float needs the same care.
- **Inaccessible pages:** a system call does not trap on an inaccessible page; it fails (WriteFile, error 1784).
- **Leftover memory is not preserved:** where the original read never-set bytes, ours writes a canonical value (zero)
  (standalone-verification.md section 5).

## 3. Containers and sorting

The game's MFC and STLport uses are std containers, std::string and small classes of ours (src/lib: str.h, hash_map.h,
intro_sort.h, calendar.h; platform/platform.h). No MFC or STLport class is left; WinMain never went through CWinApp.
What must survive:
- **Hash map order:** `lib::HashMap` (src/lib/hash_map.h) iterates in STLport's order (its bucket counts and insertion
  rule). A loop that feeds game logic, saves or network messages keeps that container.
- **Sort order:** STLport's `sort` leaves equal elements in another order than MSVC's. `lib::IntroSort`
  (src/lib/intro_sort.h: STLport's introsort, median of three, unguarded partition, heap sort at the depth limit,
  insertion sort of the first 16) is used where equal elements can differ and their order shows:
  - the draw lists (`object_order::DrawBefore`);
  - the touched models;
  - the particles by depth;
  - the picking hits;
  - the connection screen's servers;
  - the save list;
  - the objectives' quests;
  - the briefing topics.

  The save list is special: equal times compare true both ways, so its comparator is not a strict weak ordering.
  `std::sort` is used where equal elements are identical.
- **Maps:** std::map keyed by text uses CString's `_mbscmp` order (`str::MbsLess`).
- **Plain records:** `records::IsPlain<T>` decides plain data. It is never true for std::string, std::vector or arrays
  of them: an array of vectors is trivially copyable to the compiler.
- **Formats:** strings and arrays are encoded in saves and network messages by read / write functions, independent of
  the container type.
- **Replacements:**
  - CTime is lib/calendar.h and `platform::FormatLocalTime`;
  - threads are `platform::Thread` (suspended, priority set, resumed, as AfxBeginThread);
  - a failed file call throws `platform::FileError`, which is what the game's catches take.
- **Quirks kept:**
  - the link queue's stream copy 00479410 reads the new block's uninitialised bytes into the source;
  - 004CC0C0 copies a command's float arguments as dwords.

## 4. UTF-8 instead of cp1251

Every string inside the game is UTF-8. Text in the asset code page (`text::kAssetCodePage`, 1251) is
converted where it crosses into the game and back where it leaves: the assets when they are read, the saves and the
network both ways. So the files the game writes and the network's messages carry the original's bytes: saves stay
byte-identical to vanilla's, and vanilla peers read and send cp1251. The system calls are the wide ones.
Byte-equality compares, hashing, copying and formatting work unchanged on UTF-8.

### The helpers (src/engine/core/asset_text.h)

- **Conversion:**
  - `text::FromAssetText` turns asset bytes into UTF-8; ASCII is left alone.
  - `text::ToAssetText(text, &out)` converts back for a lookup. It returns false for ASCII or for text that is not
    valid UTF-8.
  - `text::ToAssetText(text)` converts back for writing. A character the code page lacks becomes '?'
    (`WC_NO_BEST_FIT_CHARS`, never a look-alike).
  - Every byte of 1250-1252 maps to one character and back, so a round trip restores an asset's bytes.
- **Lengths that mean time or a limit:** `text::CharacterCount` counts characters; a colour escape `\007C`+3 bytes
  counts by its bytes. A briefing phrase lasts 0.05 s per character.
- **Cuts by character:** `CharacterLength`, `CharacterBytes`, `FittingBytes`, `CopyFitting`.
- **Typing:** `Typed`, `TypedLength`, `NextCharacter`, `PreviousCharacter`, `CharacterStart`.

### U1: the asset boundary

- **Converted where display text is read:**
  - the text database (`TextSources::FindKeyText`; copies cached per data address and dropped when their archive
    closes);
  - `TextManager::ListKeys` and the lookup key (one "pers" key holds Cyrillic);
  - FileRegistry's `GetString` / `GetStringBuffer` / `GetStringArray`;
  - Chunk's `ReadString` / `ReadEncodedString` (.mob);
  - credits.scr;
  - the titles' headers.

  The titles' RTF is decoded by our reader by its own code pages.
- **Binary files in the archives:** a zone's quest.reg is opened with `TextManager::OpenData`, which keeps the bytes as
  they are.
- **Rule from a real bug:** the UTF-8 copies moved the heap's layout. The server's update listed records in the order
  of their addresses, and a player read before the binding was listed as nobody (network.md, world/object_order.h).

### U2: the system boundary

- **Where:** src/platform/win32/platform_win32.h. The Windows backend (src/platform/win32/*.cpp) calls the
  wide (UTF-16) functions wherever text crosses and converts with `text::Wide` / `text::Narrow`: file and folder names
  (create, find, delete, move, copy, attributes, mapping), the current folder and full paths, the program's path, the
  volume label, the key names, the other game window's class. The window, its title, the message box and typed text are
  SDL's (UTF-8). The harness's saves redirect, overlay and settings snapshot work on the UTF-8 path before the wide
  call.
- **The .ini files** are read by the game's own parser (`IniFile`, ini_file.h) through `platform::ReadWholeFile`, their
  text taken from the asset code page into UTF-8.
- **ANSI still:** OutputDebugString.
- **The environment and the command line** are read wide too (`platform::EnvironmentVariable`, the arguments after
  `--`), so a Cyrillic `EI_GAME_ROOT` or install folder arrives as UTF-8. The narrow calls turned it into question
  marks (U6). The check: tests/unit/platform_text.cpp (a variable and the current folder), and a run folder with a
  Cyrillic path: a folder under build/ with junctions to the install's data folders and copies of its small files, set
  as `EI_GAME_VANILLA` for `work.py test`; no write goes under the real install.
- **Save names:** saves are folders `saves\save<n>` with the title inside info.sav, so a title never reaches a file
  name. Cyrillic file names are checked by utf8_typing's `utf8files` step (tests/verify/harness_utf8.h).

### U4: typing

- **Typed text** (platform_sdl.cpp `QueueText`): SDL's text input is UTF-8, queued as a Text event; app.cpp's
  dispatcher passes the interface each character's code point.
- **The editors:** the caret stays a byte offset at a character's start. They are the message box's edit field (its
  letter class past ASCII is `platform::IsLetter`), the console line, the chat line and the save window's name.
- **Check:** the utf8_typing section of tests/scenarios/batch_menus_heroes.txt.
  - `typeunicode` types Cyrillic and a character past U+FFFF, and `consoleline` checks the line's bytes and caret after
    editing keys.
  - `diffcapture` checks that "λ" draws differently from "?".

### U5: the audit

Every site that treats text as bytes was decided. Identifiers were left: ASCII names, keys, file names, numbers, and
bytes compared and copied whole. Display text was fixed.
- **Case** (src/lib/text_case.h): a small table covering ASCII, Latin-1, the rest of code page 1252's letters, and
  Cyrillic U+0400-U+045F with Ґ ґ.
  - **Lengths:** each pair is two UTF-8 bytes both ways, so a case change never changes a length.
  - **Compares:** `text::CompareIgnoringCase` folds every byte. "Р" D0 A0 and "Ѡ" D1 A0 share a trail, so folding only
    the differing bytes is wrong.
  - **Classes:** `tolower` / `toupper` change ASCII only. Class tests give a byte past ASCII no class: 1252's
    `isspace(0xA0)`, the trail of "Р", would split words.
  - **Where it is used:** about 100 case-insensitive compares of names are identifiers through `CompareIgnoringCase`.
    Script `StrLower` / `StrUpper` are fixed.
  - **The network's player name filter** (`IsPrintableText`) checks per character, so each name is a valid UTF-8
    sequence of printable code points.
- **Buffers:** paths, numbers, ids and network model / texture / sound names stay byte buffers. These were fixed:
  - `FileRegistry::GetStringBuffer` and `ByteStream::ReadText` cut at a character's end;
  - the server list's 90-byte name and the message box's edit text use `CopyFitting`;
  - the chat line stays within 200 bytes (`ChatOverlay::Char`). A client's update carries 220 bytes, so a longer
    SayMessage would never arrive.
- **Byte indexing:** ASCII prefixes, markers and separators are left; a UTF-8 byte never equals one. Script `StrLen` /
  `StrMid` count characters, and the new game's name is cut to 10 characters.
- **Word wrap:** the chat overlay's `AddLine` takes a character as its whole sequence. The other wrappers break only at
  ASCII spaces and newlines.
  `DrawWrappedText` (WrapText) keeps the original's look: a word wider than the box stays whole on a line of its own
  (U6). The only text of a player's making that goes through it is a server's name in the connect list's details (cut at
  90 bytes, about 45 letters, which ends inside the panel), so no overflow shows and no breaking inside a word was added.
- **Check:** utf8_typing's `utf8text` (compares, case, classes, the name filter) and `chatwrap`.

### Saves, network and the harness

- **Saves:** mp\<n>.mp, info.sav and the .mob chunks are in the asset code page on disk.
  - **Writing:** every text a stream writes goes through `ByteStream::WriteString` (the asset code page, '?' for a
    missing character, then the terminator).
  - **Reading:** every text it reads goes through `ReadString` / `ReadInto` / `ReadText`, which convert to UTF-8.
    `ReadText` cuts to its buffer at a character's end.
  - **Fixed-size bytes:** what a stream writes as fixed-size bytes stays bytes (ASCII file names).
  - So a save written by the image is byte-identical to vanilla's.
- **Network:** the same streams carry the world's messages. The session layer converts in
  `BufferCursor::PutString` / `TakeString` and `GameBuffer::WriteString` / `ReadString`, so the update budget counts
  the original's bytes.
- **Colour escapes:** "\007C" and three raw colour bytes keep their five bytes; only the text between escapes is
  converted. Converting the whole string once left the top panel's text unconverted in the save.
- **Harness digests:** the digest's reader converts each text to the asset code page (`Text`), so it hashes the
  original's bytes.

### The drop-in exe

`ei-rebuild.exe` is the whole game. Dropped into an original game folder (GOG's vanilla 1.06 or Rebirth's), it plays
from there, started from anywhere, with no launcher, no DLL of ours and no file of ours beside it.
- **What a play needs** (release build): the exe alone.
  - **From the game folder:** the data (res, maps, camera, movies, stream, ...), but no DLL of the game's (no Miles,
    Bink or `*.asi`).
  - **Built in:** the shaders, the interface fonts and the version resource.
  - **Written when missing:** `ei_settings.ini`.
  - **Written as output:** `ei-rebuild.log` beside the exe (EI_OUT_DIR under the harness), and the saves.
  - **Not read:** `third_party_notices.txt` is the licences, shipped beside the exe.
- **The Rebirth install.** Every Rebirth patch is the code's own; the game folder
  decides nothing: every patch is always on, a fatal error ends the game on every install, and EI_Plugin.ini
  is not read (the player count and the pause mode are the host's choices on the create-server screen; rebirth.md,
  "EI_Plugin.asi"). ei_settings.ini's `[network]` SecondMaster, then a scenario's `secondmaster` line, set the second master.
- **The exe's own imports** (kernel32, user32, advapi32, winmm, ws2_32) come from System32 only
  (`/DEPENDENTLOADFLAG:0x800`). A wrapper DLL of the same name in a game folder (dgVoodoo, a dsound wrapper) is not
  taken. ASan's build leaves the flag out.
- **Checks:** `dropin_profile` (both installs; in the Rebirth smoke set) is a run like any other: its `folderprofile`
  step (harness_folder_profile.h) checks the working directory and the data folder. Every run starts the exe with EI_GAME_ROOT;
  main.cpp's fallback to the exe's own folder (no EI_GAME_ROOT) has no run.

### The x64 build

Every Windows build is x64 (release, verify, debug, asan, play; vcvars64).
- **What it builds:** ei-rebuild.exe, ei_unit_check, ei_text_check, ei_input_check, ei_ui_check and
  ei_perf_check. The verify and asan builds link the harness in. ASan uses clang_rt.asan_dynamic-x86_64.dll, copied
  beside the executables.
- **Sound and movies:** they go through src/audio and src/video (docs/notes/sound.md); there is no Miles or Bink.
- **No x87:** float-maths.md, "The control word".
- **Formats:** formats read or written as bytes are 32-bit (core.md, "Values read and written as bytes"). A
  container's size meets the game's int counts through `lib::Count` (lib/containers.h), not `std::ssize`.
- **Running it from a folder of its own:** use a folder made like the game folder.
  - **Folders:** each is a junction to the install's, except `saves`, `cache` and `Config`, which are real (`Config`
    a copy: the game rewrites Config\keyboard.ini and tutorial.ini at every exit, so a junction writes into the
    install; PL22, 2026-11-05).
  - **Files:** each is a copy, except game.exe.
  - **Settings:** ei_settings.ini's Work path must be `.\`. An absolute path sends saves\current into
    the install.
  - **DPI:** the exe is DPI-unaware. A script that clicks or captures its window calls SetProcessDPIAware first.

### The compiler cache

A build folder's first build (a fresh worktree's) compiles through sccache: it takes the objects that another checkout
already compiled from the same source and flags. Later builds in that folder compile directly. build.cmd sets it up
through tools/compiler_cache.py.
- **Measured (H52: a fresh worktree's verify + release, alternating with main's build, on the shared machine):**
  - The median of five pairs went from 170 s to 55 s. On a quiet machine it went from 101 s to 36 s.
  - The verify build's median went from 90 s to 20 s, the release build's from 84 s to 33 s.
  - With an empty cache a build takes about 5 s longer than without it.
  - Incremental builds are as fast as before or faster (no ei_game archive). One changed .cpp file: 4.5-4.9 s of
    ninja against 5.8-6.6 s. One header included by 291 objects: 26 s against 28 s.
- **The program:** sccache 0.19.0, Mozilla's x64 Windows release, pinned with its sha256. It is downloaded on first
  use into the main checkout's `build\third_party\sccache-0.19.0`, beside the shader toolchain.
- **The cache:** `build\compiler_cache\<configuration>` in the main checkout (release, verify, debug, play, asan apart),
  2 GB each, shared by every worktree. Objects are stored compressed: one source state takes about 0.35 GB.
- **The servers:** one per checkout and configuration, on a port taken from the checkout's path (a crc32 into
  42000-48999, below Windows' dynamic range, where Hyper-V and WSL reserve blocks). The choice steps up past ports in
  `netsh int ipv4 show excludedportrange protocol=tcp` and past ports whose bind is refused (WSAEACCES 10013), so it
  stays the same per checkout; a port in use is kept, it may be the checkout's own server
  (tools/test_compiler_cache.py). A server ends after 10 idle minutes. tools/compiler_cache.py starts it with its output going nowhere: a server started by the first
  compile could hold the build's output pipe open. The checkout's folder is the server's base folder
  (SCCACHE_BASEDIRS), which is cut out of the paths before they are hashed, so one checkout's objects hit in another.
- **The launcher (tools/compiler_cache_launcher.cpp):** CMake's compiler launcher, built once per version of its
  source into the main checkout's `build\third_party`. It runs each compile through sccache, or directly when build.cmd
  sets EI_COMPILER_CACHE_DIRECT (the folder has a .ninja_log). A cached object comes with the compiler output of the
  checkout that compiled it first, so its /showIncludes lines name that checkout's headers. Ninja keeps those lines as
  the object's dependencies, so a header changed here would have rebuilt nothing. The launcher changes every other
  checkout's folder in the output to this one's. Python was too slow for this (0.2 s to start, per compile).
- **Why only the first build:** sccache runs the preprocessor before every compile (its direct mode is for gcc and
  clang only), about 0.3-0.4 s of CPU per object for the whole hit (launcher, client, server, cl /E, reading the
  object). That makes a miss slower than a plain compile.
- **What the preprocessor reads (H52b):** the preprocessed text of an object is 75% standard library
  (`cl /E`, summed over the tree: 1556 MB before, 1378 MB after H52b), 16% the Windows SDK's C headers, 9% our own
  headers. `<string>` alone is 1.4 MB, so no object gets below about 1.6 MB. The headers that cost more than that
  are the ones to keep out of widely included headers (extra size per object): `<chrono>` 1.6 MB (it includes
  `<format>` and `<filesystem>`), `<filesystem>` 2.2 MB, `<format>` 1.1 MB, `<algorithm>` 0.45 MB, `<memory>`
  0.3 MB (with `<atomic>`), `<variant>` 0.28 MB, `<functional>` 0.3 MB, `<unordered_map>` 0.23 MB, `<list>` /
  `<deque>` / `<map>` 0.2 MB, windows.h 3.3 MB (1.8 MB less with WIN32_LEAN_AND_MEAN). Out of the widely
  included headers now: `<algorithm>` (rect.h, containers.h: a file that calls `std::sort` and the like includes
  it itself), `<chrono>` (render_internal.h) and the Windows files' full windows.h. Kept because the types are
  real members: `<memory>` (`unique_ptr` in script.h, unit_ai.h, ui.h, sound.h), `<variant>` (ai_map.h),
  `<unordered_map>` / `<list>` / `<deque>` / `<map>` (base_manager.h, link_loader.h: 0.4 MB in about 330 objects;
  lookup-only maps are `std::unordered_map` by design) and `std::format` (55 objects).
  - **Measured (all 476 compile commands of the verify build run through sccache, all hits, 8 at a time, CPU of the
    whole process tree):** the numbers move by +-20% between runs on the shared machine (three alternating pairs:
    107, 129, 149 s before and 123, 113, 110 s after), so the cut is about 10% at best. The `cl /E` alone is small
    (about 0.09 s of CPU per object) and a hit costs 0.17-0.3 s, so an 11% smaller text moves the hit by less than
    that. A plain compile of 12 sample objects (CPU): 14.3 s before, 13.4 s after (with the `unique_ptr`
    and `variant` cuts that were taken back). The warm first build (fresh build folder, warm cache) is 26 s with
    and without the cuts: its ninja part is 15-18 s and configure, checks and the links are the rest.
  - **How to measure again:** `ninja -t compdb` in a build folder, `cl /E /showIncludes` per entry (the compile
    command without /Fo /Fd /c), then add up the `#line` sections per header and the include tree's first-seen
    subtree per header. Put the process in a job object (CPU of the tree) and run the compile commands through an
    sccache server of its own for the hit cost.
- **Off:** set `EI_COMPILER_CACHE=0` and cl runs without the launcher (the changed command lines rebuild everything
  once). A server that cannot start makes sccache run cl itself (SCCACHE_IGNORE_SERVER_IO_ERROR).
- **What the cache needs from the build (CMakeLists.txt):**
  - **Debug information in each object (/Z7, CMP0141):** with /Zi every compile writes one shared PDB, which the cache
    cannot store.
  - **No precompiled header:** SDL's own is off (CMAKE_DISABLE_PRECOMPILE_HEADERS). Objects that use one are not
    cached.
  - **ei_game an object library:** its archive grew from 0.3 GB to 1.1 GB with /Z7. The archive was rewritten on every
    change, which took 3-5 s; now there is none.
- **Two configure savings that do not depend on the cache:** a fresh configure went from 11 s to 3 s.
  - SDL's configure checks that MSVC for x64 always answers the same way are preseeded.
  - FindPython is handed the `python` on the PATH, which saves its 2 s search.
- **Sizes:** a /Z7 object holds its own copy of the types it uses, so a verify build's objects grow from 0.35 GB to
  1.2 GB. With no ei_game archive, a verify or release build folder grows from 0.9 GB to 1.4 GB. ei-rebuild.exe stays
  the same size, and ei-rebuild.pdb about the same (55-60 MB).
- **Debugging:**
  - The debugger resolves symbols and lines as before (checked with cdb). resolve_crash.py reads the .map, which is
    unchanged.
  - An object taken from the cache names the source path of the checkout that compiled it first, which may be another
    worktree. If that worktree is gone, point the debugger at this checkout: WinDbg's `.srcpath` matches the path's
    tail, and Visual Studio asks for the file once.
  - Only a build folder's first build takes cached objects; anything compiled later names this checkout.

### Build steps that fail without a cause

On a loaded machine (a whole-machine load near 100% with games running) a build step now and then exits non-zero and
prints nothing, or the linker prints only `LNK1000: Internal error`; a plain rerun passes (H52a). Seen: lib.exe
archiving ei_render.lib, a cached compile of registry.cpp (the same command passed 20 times in a row on a quiet
machine), and the debug build's incremental link of ei_perf_check.exe (`LNK1000 ... IncrBuildImage`).
- **The exit codes:** ninja 1.12 does not print them. The step wrapper does: tools/compiler_cache_launcher.exe stands
  in front of every compile (CMAKE_CXX_COMPILER_LAUNCHER) and every link and archive step (CMAKE_CXX_LINKER_LAUNCHER,
  and CMAKE_CXX_CREATE_STATIC_LIBRARY in CMakeLists.txt, which has no launcher of its own). A step that exits non-zero
  gets the line `compiler_cache_launcher: <command> exited with 0xC0000142` after its output (standard output and
  error go through the wrapper together). 0xC0000142 (STATUS_DLL_INIT_FAILED) would point at the desktop heap,
  0xC0000005 or 0xC0000409 at a crashed tool, 1 at a tool that failed itself. No cause is known yet; the next
  occurrence names the code. ninja drops the compiler's own file-name line, so a compile that crashed after printing
  it shows nothing at all (H52a's own debug build: SDL_render_ps2.c, the first build of the folder, through sccache;
  the retry below built it).
- **The incremental link (fixed):** the debug build linked with `/INCREMENTAL` (CMake's default for Debug), the only
  configuration that did. The release-like builds have had `/INCREMENTAL:NO` since R16c2 (an incremental relink once
  dropped new code from ei_render.lib). All configurations link non-incrementally now.
- **The retry:** build.cmd runs the build through tools/build_retry.py (`cmake --build` with its output passed on). When
  every failed step has no output of its own (or only the wrapper's line above), or prints LNK1000, it says so
  (`build_retry: the build failed in steps that printed no error ...`) and builds once more. Any other failure, and a
  second failure, ends the build as before. A build through a pipe shows ninja's plain `[n/m]` lines, one per step.
  tools/test_build_retry.py checks the reading of a failed log.
- **The build gate (H64):** build.cmd runs itself again under tools/build_gate.py, which waits while a quiet test run
  holds the machine and puts the build's stage and ninja's `[n/m]` on the work board (docs/notes/work-board.md,
  "Builds"). A build that prints `build_gate: a quiet run ... holds the machine` starts on its own when that run ends.

### The harness in the image

The harness (tests/verify) is linked into ei-rebuild.exe in the verify and asan builds (EI_HARNESS), with its image side
(tests/verify/image) calling our code by name. The release and play builds have none of it.
- **Why linked, not a DLL:** the image side reaches about 120 functions (image_side.h), and the platform calls it
  answers are resolved at link time.
- **Where it reaches into game code:**
  - image/image_side.h and harness_digest_read.h;
  - the hook points: `ei::mainloop::g_harnessHooks` (app/harness_hooks.h), the settings
    file's read filters and pin, the random generators' draw observers and seed hook, the console line observer, the
    memory stream write observer;
  - the platform calls it answers (harness_platform.cpp: the backend's input, window and clocks; game_answers.cpp's
    seams: paths, file writes, the isolated run; game_sockets.cpp's sockets: platform.md, "The harness's answers");
  - the data seam;
  - `ei::SetMissingCodeHandler`;
  - the log.
- **Logs:** our addresses are `ei-rebuild+offset`. resolve_crash.py, randtrace.py and profile_report.py read
  ei-rebuild.map.
- **The user's real keyboard and mouse are kept out** (harness_input.h): a harness game reads its input only from the
  harness's own platform backend (harness_platform.h), which drops every system event. The steps still speak window
  messages; `input::Post` makes each into the game's event. The report's `MOUSE:` line counts the real events left
  out.

## 5. game.exe's role

Nothing runs game.exe: the goldens are our own (docs/notes/standalone-verification.md section 2). The tag
original-oracle has the last build that ran it as the harness's reference. What is left of it:
- **Its file:** the tools of section 1 read the installs' (tools/exe_path.py: the vanilla install's game.exe, Rebirth's
  starter\Engine\game.exe).
- **Its addresses as names:**
  - comments and config/functions.csv name the original functions by address;
  - the data seam keeps unnamed data at its addresses;
  - the digest keeps the AI modes' ids as game.exe's table addresses.
- **Its recordings:** the WideScreen references (run_all.py --widescreen).
- config/library.csv's names are often wrong for STLport / MFC rows; read the code.

## 6. The image's source files

### main.cpp

The program's `main`, the same on every system: the game folder, the settings file's game paths, EI_Plugin's values,
the renderer's device in the background, then WinMain (app::GameMain) with what follows `--`. "How it runs" above.

### platform/platform.h

The game's files (the wide calls on Windows; the harness answers the opens and searches in its builds for its
redirection). The types:
- `File`: binary, a Win32 handle, three open modes, every open shared; text lines with `WriteLine`;
- `MappedFile`: read only;
- `ReadFile`: a whole file;
- `FileFind`: a folder search with CFileFind's read-ahead and absolute paths;
- `FileExists`: `_access`.

A failed read, write or seek throws `FileError`, with the OS error and the path in what(). What the game caught (Dib's
reads and writes, the script reader) catches it; the rest ends the game as the original did. For a write to a full
disk, see tests/scenarios/harness_disk_full.txt.
