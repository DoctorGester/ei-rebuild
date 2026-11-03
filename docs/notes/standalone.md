# The standalone exe: the image, the library ports, UTF-8

The product is `ei-rebuild.exe`: an ordinary x64 exe with all of the game linked in. Dropped into a game folder it plays
from there ("The drop-in exe"). It runs no game.exe and reads none. How changes are checked:
docs/notes/standalone-verification.md.

## 1. game.exe's data and addresses

The code still names some of game.exe's data by the original's addresses: comments, the tagged definitions of
"Named data (C18)", the seam's arguments in "Own data (S18)". A few tools read game.exe to make or check them; the
build reads no game.exe.

**Finding game.exe.** Every tool that reads game.exe goes through `tools/exe_path.py`. That covers exe_strings,
rdata_literals, retired_vtables without `--check`, gen_iat without `--check`, find_vtables, find_dead_code and
named_data `--emit`. The lookup order:
1. `--exe` (or `EI_EXE`);
2. `run_all.run_dir(root)`'s `vanilla\game.exe`, then `rebirth\game.exe` (a worktree without a run\ junction uses the
   main checkout's).
If nothing is found, it gives one error naming the paths it looked in. Vanilla's and Rebirth's game.exe 1.06 differ in
one .text byte (file offset 0x56BB6) and nothing else, so the data the tools read is the same.

**Strings are literals.** A string the original passed by address is a literal with the same bytes. The literals were
read from both exes by `tools/exe_strings.py`, not typed by hand. Its options:
- `--wrap X` takes `X(0x...)`;
- `--before RE` gives a parameter position;
- `--skip` leaves addresses alone.

The empty string is `game::kEmptyText` (game_strings.h). Its readers copy or compare the text; none compares the
pointer. A pointer-sized cell read as `const char* const*` holds the address of the text, not the text: read the
cell's pointer first.

## 2. The image

### How it runs

- **The program** (src/main.cpp, every system). `ei-rebuild [-- <game args>]` can be started from anywhere. Its main
  does this:
  1. enters the game folder: EI_GAME_ROOT when a launcher names it, else the exe's own;
  2. reads the settings file's game paths;
  3. loads the Rebirth flags ("The Rebirth profile");
  4. starts the renderer's device in the background;
  5. calls WinMain (`app::GameMain`) with what follows `--` (`platform::ArgumentsAfterSeparator`).

  WinMain's first lines do what game.exe's static initialisers did beyond setting values: the sessions' pointers, the
  coder's tables, Winsock, the weather's curves, and the exit functions in the game's order. Every value the
  initialisers set is a definition's initialiser.
- **Resources**: the exe's one resource is its own VERSIONINFO (`src/main.rc`). The window title "Evil Islands" and
  the class "EIGAME" are literals in app_main.cpp. The window class has no icon.
- **A crash** in a harness run is reported by the harness: its crash log, tools/resolve_crash.py. ei_launch reports a
  game that ended by itself while its report still says RUNNING (`FAILED: the game ended ...`).
- **Suite.** `run_all` / `work.py test` run two installs:
  - `standalone`: the vanilla data;
  - `standalone-rebirth`: the Rebirth data with the patches of `config/rebirth_profile.ini`. Those are the 40 a Rebirth
    launch with addon turns on; refresh the list when addon.dll or EI_Plugin.ini change.

  `--game` names the install whose data a run plays, and ei_launch sets EI_GAME_ROOT from it.

### Own data (S18)

The image holds nothing of game.exe's sections. Every value the code reads is one of three things:
- a named global of ours ("Named data (C18)");
- a tagged literal;
- a compiler-made vtable.

- **The seam** (src/game/game_data.h). Code that names game.exe's data by the original's address reaches it only
  through `GameData<T>(a)` or `GamePointer<T>(a)`. `DataId(a)` marks an address kept in a table and resolved by its
  reader; the harness's `ExeTableId(a)` is a game.exe table kept as an id and never read. src/ names no such address
  any more.
- **The check** (`python tools/data_seam.py --check`, a build check). Every literal in game.exe's data range
  (0073B000-007D7000) in src/, tests/ and include/ must be one of these, or the check fails:
  - an argument to the seam;
  - an argument to a helper whose every definition hands the address to the seam;
  - a constant whose every use does.

  `--list <section>` shows the raw uses.
- **.rdata constants as literals** (`tools/rdata_literals.py`). A float, double or integer cell that no code writes is
  the value in the source, tagged with its address. The type is the spelling:

  | Form | Means |
  |---|---|
  | `0.5f /*0073B86C*/` | a float (dword) |
  | `3.141592653589793 /*0073CAE8*/` | a double (qword) |
  | an integer literal | the dword |
  | `/*addr f*/` | a float widened to double |
  | `(-1.0f /*addr*/)` | a negative value, in parentheses |
  | `kFloatConstant<0.5f /*0073B86C*/>` | a reference result (effects take the constant's address) |

  The literals were checked against game.exe's bytes when they were made.
  - **Folds:** the compiler folds a product of constants where the original multiplied at run time on the main
    thread. Where the rounding of that fold matters, write `static_cast<float>(a * b)`.
  - The pi * (1/180) product is `ei::kRadiansPerDegree` (src/game/angles.h).
  - The cells addon.dll writes are not literals: the WideScreen layout widths 00742B08 and 007428AC are
    `screen_layout::g_layoutWidth` / `g_panelRight`, and tests/verify/addon_sites.inc lists the sites.
- **Own import table** (src/platform/win32/import_table.h, src/platform/win32/image_imports.cpp, gameapi.h). The
  table holds the slots of game.exe's imports that a call of ours or the harness goes through (gen_iat.py's
  `reached()`). A slot is reached by `ImportTable::X` in the harness, `Imports().X`, or `gameapi::X`. The reached
  slots are:
  - Winsock 1.1 by ordinal (WSOCK32; prototypes in src/platform/win32/import_prototypes.h);
  - the clocks;
  - the files, folders and profile reads;
  - the window and cursor calls the harness redirects.

  How the slots are set up:
  - **When:** image_imports.cpp resolves them from System32 at static initialisation (`init_seg(compiler)`). That is
    before the harness's static constructor takes its slots.
  - **Text calls:** a text call's slot takes its UTF-8 version ("U2: the system boundary").
  - **Calling:** our code calls a slot as `gameapi::X`, a reference bound in gameapi.cpp under `init_seg(lib)`. Every
    other Windows function is called as `::X`.
  - **Generated files:** `tools/gen_iat.py` writes import_table.h, image_imports.inc and gameapi.h / .cpp. Its
    `--check` (a build check) fails on any of these:
    - a hand edit;
    - a new harness route on a slot;
    - a `gameapi::` call of an import the table lacks;
    - a slot nothing reaches any more.
  - **Moving off:** a slot goes from the table once the harness redirects that call in its own platform backend.

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
- **An object's start** is where the previous one ends, not the base the code indexes from. net_coder's length tables
  are indexed from one entry before their start, so ours take `length - 1`.
- **Not a global:** "campaign mode" 007C2CC4 is the server world's `sessionKind`. Naming it apart left its readers on
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

A plain global's constructor may call `gameapi`: the import slots are resolved before every other static constructor.

### Compiler-made vtables (C19)

Every class of ours with virtual functions is a real C++ class, and its vtable is the compiler's.

**config/vtables.csv** (made by tools/find_vtables.py) lists game.exe's 608 tables. Each row gives the address, slot
count, RTTI name, kind, slot 0, class, file and a note. Every row is retired: the class and its declaring file are
named (`-` for a table no class of ours has).

**`tools/retired_vtables.py --check`** (a build check) checks the rows against the sources. named_data.py checks that
nothing names an address inside a retired table. `python tools/find_vtables.py` rewrites the csv when it runs; restore
it with git.

- **Not vtables:** some dwords only look like tables. These are strings whose dword falls in .text ("%ld" 0074676C,
  "dde" 0074718C, "a/p" 007474B8, "OCP" / "ACP" 00747E2C / 30, "ime" 00746E60) and the EH unwind entries (kind `eh`).
  Never retire them.
- **Tails:** find_vtables.py splits a table at every address the code holds as an operand, so some rows are the tail
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
- **The window base** (ui.h): `UiInputSide` holds input slots 1-12. `UiWindow : UiWindowBase, UiInputSide` puts the
  input side at +0x94 and adds slots 18-24. `UiContainer` adds 25-41 and the input dispatch. Screens call the defaults
  qualified (`UiContainer::LeftDown(...)`).

### In place of game.exe's module

- **Command line:** `ei-rebuild ... -- <game args>` is WinMain's line. Nothing of ours calls GetCommandLineA.
- **DirectInput:** called from System32's dinput.dll with the exe's module as hInstance. c_dfDIMouse is our copy,
  `DirectInputMouse::MouseDataFormat`.
- **Hook points:** code bytes the harness reached in game.exe are hook points in ours (`ei::mainloop::g_harnessHooks`,
  main_loop.h).
- **Rebirth:** the start-up reads the flags first ("The Rebirth profile"). What addon.dll and EI_Plugin patch is
  reproduced behind flags (docs/notes/rebirth.md). EI_Plugin.asi never loads: the image loads no Miles, which is what
  loaded `*.asi`. `starter::RebirthInstall()` is true when a Rebirth flag is on.
- **Mod folders** (src/game/mod_folders.{h,cpp}): Maps, ResOverride, CameraPatch, StreamPatch and MoviesPatch work as
  addon.dll does them (rebirth.md, "Resources and paths"), only with the flag on.
  - **Checked by:** `rebirth_mod_folders`, a mod written by `tools/make_test_mod.py`.
  - **Not covered by a run:** StreamPatch, MoviesPatch, the extra / ignored archives, and Maps' zone-table and quest
    sites.

### Settings file

The image keeps its settings in **ei_settings.ini**, a plain ini of its own (src/game/settings.h). It never reads or
writes the registry.

- **What each setting does:** `docs/settings.md`, generated by `tools/gen_settings_doc.py` from
  `src/game/settings_catalog.inc` (section, name, default, values, description; `settings::Describe`). The defaults
  file carries each line's description as the comment above it; a file that already exists gains no comments. The
  build check fails when the doc is stale, or when a setting the code reads has no row (or a row nothing reads).
- **Where:** the first place that holds a file: next to the exe, then `%LOCALAPPDATA%\EvilIslands\ei_settings.ini`.
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
  - **What that fixes:** the game's relative paths (Work path and CD-ROM Path `.\`, Resources path `.\res\`, saves,
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
  | patches | Rebirth's patches (`Name = true\|false`) and EI_Plugin's NPlayers, Pause, SecondMaster (rebirth.md, "The flags") |
  | rebirth | the starter's switches (AddonPath) |
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
- **Defaults** (`settings::Defaults`, settings_document.cpp). They are made for the primary monitor's mode in physical
  pixels; with no size known, the defaults for 1024x768 are used in memory and not written.
  - **video:** the monitor's resolution, FullScreen true (a borderless window over the monitor), 32 bits,
    Antialiasing, Mipmapping, Filtering 2, TextureQuality 0, ShowParticles.
  - **sound:** Audio, and the three volumes at 100.
  - **shadows:** ShadowQuality 2 and the five shadow switches.
  - **terrain:** the starter's best landscape detail: water waves, LightingFreq 1000, self shadowing, dynamic lights
    and the prints.
  - **camera:** the whole section as a game install writes it (config/harness/registry.reg).
  - **game:** ObjectsLightingCoeff 2, the hero's and the quest's lights, UseSWCursors false, SelectionType 1, the
    default map and stream, and the master server a3master.nival.com.
  - **renderer:** VSync true, Msaa 8, Anisotropy 16, PerfOverlay false.
  - **Left out:** everything else, which the game reads as absent and takes its own defaults for. The game's exit save
    then adds every option it keeps.
- **Who uses it:** `settings::GetInt / GetBool / GetFloat / GetString / GetValues / GetLines` and the setters.
  - **Writing:** the options loader and writer. `GameOptions::Save` ends with `settings::Save`, which writes the file
    when a value changed, atomically through `ei_settings.ini.tmp`.
  - **Readers:** the AutoExec lines, the start-up's Rebirth patches, mod_folders, and device_mesh's renderer section
    (renderer.md sections 11, 15, 15a; that section is not read in the harness build).
- **Read filters** change a value as it is read:
  - EI_GAME_ROOT's Work path / CD-ROM Path;
  - the harness's `discprompt`;
  - drop-in runs' `.\`.
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
  - tests/unit/settings_file.cpp (in `ei_unit_check` and Linux's ei_port_check) covers the format and the defaults.
  - `settingsfile writable` (settings_save.txt) runs the game's exit save for real.
  - `settingsfile defaults` (settings_defaults.txt) starts with no file and a 1280x720 desktop.

  ei_launch compares the file after the game with the scenario's `# settings-file: settings/<name>.ini` and fails on a
  difference or a left `.tmp`. To change an expected file, copy the run's ei_settings.ini over it.

### The CRT (src/lib/crt*.cpp, namespace `libcrt`)

Most of the C runtime is the host's. What stays the game's, and why:

| Kept | Where | Why |
|---|---|---|
| `libcrt::Rand` / `Srand` | crt.cpp | VC6's LCG (seed * 214013 + 2531011, bits 16-30) on a thread_local seed. The game's draws are the game's: the host's rand would change every golden that draws. |
| `libcrt::Qsort` | crt.cpp | VC6's quicksort (middle pivot, selection sort up to 8, smaller part first). It orders equal elements its own way, and two callers show that order: the sector spiral's cells by distance and the ambience's class votes by weight. Where equal elements are identical, callers use std::sort. |
| `libcrt::Format` | crt_format.cpp | VC6's `_output` state machine. A malformed conversion prints as VC6 did, and the host never sees one: the UCRT's invalid-parameter handler would end the game on a run-time format from the game's texts. |
| `CiAcos`, `CiAsin`, `CiPow`, `Hypot` | crt_maths.cpp | The original's x87 sequence at the main thread's 24-bit precision (float-maths.md, "Wider operands"). The host's results differ (pow in 4.1% of arguments rounded to a float, acos 7.8%, asin 18.8%) and changed 14 goldens. Hypot is kept for its last bit. |

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
- **Files:** every open goes through the import slots, so the harness's redirection and a mod's hooks apply.
  - `platform::File` is a Win32 handle; text lines are written with `WriteLine`.
  - `platform::ReadFile` reads a whole file.
  - `platform::FileFind` does CFileFind's read-ahead.
  - `platform::FileExists` is `_access`.
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
  - the draw lists (`address_order::DrawBefore`);
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

Every string inside the game is UTF-8. Text in the asset code page (`EI_ASSET_CODEPAGE`, a CMake cache string, 1251) is
converted where it crosses into the game and back where it leaves: the assets when they are read, the saves and the
network both ways. So the files the game writes and the network's messages carry the original's bytes: saves stay
byte-identical to vanilla's, and vanilla peers read and send cp1251. The system calls are the wide ones.
Byte-equality compares, hashing, copying and formatting work unchanged on UTF-8.

### The helpers (src/game/asset_text.h)

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
  - Chunk's `ReadString` / `ReadEncodedString` (.mob), with `WriteString` / `WriteStringArray` converting back;
  - credits.scr;
  - the titles' headers.

  The titles' RTF is decoded by our reader by its own code pages.
- **Binary files in the archives:** a zone's quest.reg is opened with `TextManager::OpenData`, which keeps the bytes as
  they are.
- **Rule from a real bug:** the UTF-8 copies moved the heap's layout. The server's update listed records in the order
  of their addresses, and a player read before the binding was listed as nobody (network.md, address_order.h).

### U2: the system boundary

- **Where:** src/platform/win32/platform_win32.h. The import slot of each ANSI call that takes or gives text holds its
  UTF-8 version (`text::utf8::<Name>A`, with the same signature). `text::SystemRoute` routes it, from the `TEXT_ROUTE`
  list that gen_iat.py's `routed()` reads.
- **Routed calls:**
  - the files: CreateFile, FindFirstFile / FindNextFile, Delete / Move / CopyFile, Create- / RemoveDirectory,
    GetFileAttributes, GetCurrentDirectory, GetFullPathName, GetModuleFileName, GetDiskFreeSpace,
    GetVolumeInformation;
  - CreateWindowEx (a non-ASCII title is set through `DefWindowProcW(WM_SETTEXT)`), FindWindow, MessageBox,
    GetKeyNameText;
  - the window's class and messages (U4).

  Every call through `gameapi::` passes and receives UTF-8. The harness wraps the UTF-8 versions, so its redirects work
  on the same bytes. A new `gameapi::` call that takes or gives text needs its version and route.
- **Direct calls:** our own code calls `text::utf8::<Name>A` by name: mod_folders, script_addon_*, console_addon,
  settings, world_client_maps.
- **Input that is not UTF-8** is taken in the ANSI code page. Output is always UTF-8.
- **Not routed:** the profile calls (`IniFile`; their values round-trip as bytes and the config\*.ini names are
  ASCII), OutputDebugString.
- **Save names:** saves are folders `saves\save<n>` with the title inside info.sav, so a title never reaches a file
  name. Cyrillic file names are checked by utf8_typing's `utf8files` step (tests/verify/harness_utf8.h).

### U4: typing

- **The window is a Unicode one.** The slots of `RegisterClassExA`, `DefWindowProcA`, `GetMessageA`, `PeekMessageA`
  and `DispatchMessageA` take the wide calls. Retrieval and dispatch must both be wide: a message taken with an A call
  is converted to the ANSI code page whatever the window.
- **Characters:** WM_CHAR carries a UTF-16 unit. `TypedCharacter` (app.cpp) joins a surrogate pair, drops a lone half,
  and passes the interface a code point.
- **The editors:** the caret stays a byte offset at a character's start. They are the message box's edit field (its
  letter class past ASCII is `platform::IsLetter`), the console line, the chat line and the save window's name.
- **Rule from a real bug:** a route must be a function of ours (`text::utf8::DefWindowProcA` calling
  `DefWindowProcW`), never `&::DefWindowProcW`. The routes table is read during static initialisation, before an
  import's address is set.
- **Check:** tests/scenarios/utf8_typing.txt.
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
- **Check:** utf8_typing's `utf8text` (compares, case, classes, the name filter) and `chatwrap`.

### Saves, network and the harness

- **Saves:** mp\<n>.mp, info.sav and the .mob chunks are in the asset code page on disk.
  - **Writing:** every text a stream writes goes through `ByteStream::WriteString` (the asset code page, '?' for a
    missing character, then the terminator).
  - **Reading:** every text it reads goes through `ReadString` / `ReadInto` / `ReadText`, which convert to UTF-8.
    `ReadText` cuts to its buffer at a character's end.
  - **Fixed-size bytes:** what a stream writes as fixed-size bytes stays bytes (ASCII file names).
  - So a save written by the image is byte-identical to vanilla's.
- **Network:** the same streams carry the world's messages. The session layer converts in net_messages.cpp's
  `PutString` / `TakeString` and `GameBuffer::WriteString` / `ReadString`, so the update budget counts the original's
  bytes.
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
  - **Built in:** the shaders, the interface fonts, the import table, the Rebirth profile and the version resource.
  - **Written when missing:** `ei_settings.ini`.
  - **Written as output:** `ei-rebuild.log` beside the exe (EI_OUT_DIR under the harness), and the saves.
  - **Not read:** `third_party_notices.txt` is the licences, shipped beside the exe.
- **The Rebirth profile** (src/standalone/startup.cpp). The game data folder picks the profile:
  - **Rebirth's** where `starter\addon.dll` or `EI_Plugin.asi` is. That is config/rebirth_profile.ini's patches (made a
    spec at configure time: build/<cfg>/config/rebirth_profile.inc), plus EI_Plugin.ini's `[CONFIG] NPlayers` / `Pause`.
  - **Else the plain install's**, which is the defaults.

  Then ei_settings.ini's `[patches]` apply, then EI_REBIRTH and a scenario's flags. When EI_REBIRTH is set no profile
  is taken. ei_launch sets it to `defaults` for a harness run whose caller set none, and run_all sets it to the profile's
  names on the Rebirth install. So the goldens do not read the install's EI_Plugin.ini.
- **The exe's own imports** (d3d11, kernel32, user32, advapi32, winmm, ws2_32) come from System32 only
  (`/DEPENDENTLOADFLAG:0x800`). A wrapper DLL of the same name in a game folder (dgVoodoo, a dsound wrapper) is not
  taken. ASan's build leaves the flag out.
- **The drop-in runs:** `ei_launch --drop-in`, or a `--standalone` scenario with a `# drop-in:` line on both installs.
  1. **Makes the folder:** `<run folder>\dropin` is built like the install. Each folder is a junction to the install's.
     Each file is a copy, except game.exe and our own files. Then the build's exe is added.
  2. **Starts the copy:** it runs with EI_GAME_ROOT and EI_REBIRTH cleared, so the game folder and the profile are
     found as in a player's folder.
  3. **Cleans up:** it removes the folder afterwards. Junctions are removed, never followed.

  Writes to the install are refused. The pinned settings read Work path / CD-ROM Path as `.\` there
  (harness_dropin.cpp). The scenarios are `dropin_boot` and `dropin_profile`. The `dropin` step checks every flag
  against the profile worked out from config/rebirth_profile.ini.

### The x64 build

Every Windows build is x64 (release, verify, debug, asan, play; vcvars64).
- **What it builds:** ei-rebuild.exe, ei_launch, ei_unit_check, ei_text_check, ei_input_check, ei_ui_check and
  ei_perf_check. The verify and asan builds link the harness in. ASan uses clang_rt.asan_dynamic-x86_64.dll, copied
  beside the executables.
- **Sound and movies:** they go through src/audio and src/video (docs/notes/sound.md); there is no Miles or Bink.
- **No x87:** float-maths.md, "The control word".
- **Formats:** formats read or written as bytes are 32-bit (core.md, "Values read and written as bytes"). A
  container's size meets the game's int counts through `lib::Count` (lib/containers.h), not `std::ssize`.
- **Running it without ei_launch:** use a folder made like the game folder.
  - **Folders:** each is a junction to the install's, except `saves` and `cache`, which are real.
  - **Files:** each is a copy, except game.exe.
  - **Settings:** ei_settings.ini's Work path and CD-ROM Path must be `.\`. An absolute path sends saves\current into
    the install.
  - **DPI:** the exe is DPI-unaware. A script that clicks or captures its window calls SetProcessDPIAware first.

### The harness in the image

The harness (tests/verify) is linked into ei-rebuild.exe in the verify and asan builds (EI_HARNESS), with its image side
(tests/verify/image) calling our code by name. The release and play builds have none of it.
- **Why linked, not a DLL:** the image side reaches about 120 functions (image_side.h) and must take import slots
  before the exe's static initialisers.
- **Where it reaches into game code:**
  - image/image_side.h and harness_digest_read.h;
  - the hook points: `ei::mainloop::g_harnessHooks` (main_loop.h), `g_directInputObserver` (ui_cursor.h), the settings
    file's read filters and pin, the random generators' draw observers and seed hook, the console line observer, the
    memory stream write observer;
  - the import slots it routes (harness_imports.h);
  - the data seam;
  - `ei::SetMissingCodeHandler`;
  - the log.
- **Logs:** our addresses are `ei-rebuild+offset`. resolve_crash.py, randtrace.py and profile_report.py read
  ei-rebuild.map; check_x87.py reads ei-rebuild.exe.
- **The user's real mouse is kept out:** the game window's subclass answers WM_NCHITTEST with HTNOWHERE, and the
  DirectInput mouse state is zeroed after each read (through `g_directInputObserver`). The report's `MOUSE:` line
  counts reads, real moves left out and hit tests.

## 5. game.exe's role

Nothing runs game.exe: the goldens are our own (docs/notes/standalone-verification.md section 2). The tag
original-oracle has the last build that ran it as the harness's reference. What is left of it:
- **Its file:** the tools of section 1 read a copy in run\vanilla or run\rebirth (tools/setup_vanilla_run.cmd,
  setup_rebirth_run.cmd).
- **Its addresses as names:**
  - comments and config/functions.csv name the original functions by address;
  - the data seam keeps unnamed data at its addresses;
  - the digest keeps the AI modes' ids as game.exe's table addresses.
- **Its recordings:** the WideScreen references (run_all.py --widescreen) and tests/coverage/reach.txt.
- **Function sizes:** src/hook/function_sizes.inc (from Ghidra) and config/extra_function_sizes.csv, for coverage and
  find_dead_code. Add rows by hand, in order: gen_function_sizes.py rewrites every size from the Ghidra database.
  config/library.csv's names are often wrong for STLport / MFC rows; read the code.

## 6. The image's source files

### main.cpp

The program's `main`, the same on every system: the game folder, the settings file's game paths, the Rebirth flags,
the renderer's device in the background, then WinMain (app::GameMain) with what follows `--`. "How it runs" above.

### platform/win32/image_imports.cpp

game.exe's imports in the program's own table ("Own import table"). `g_table`, an ordinary ImportTable of ours, is what
platform/win32/import_table.h's Imports() returns. So everything that calls through a slot reaches it: gameapi.h's
routed imports, Winsock, and the harness's slot routes.
- **The slots:** every slot of image_imports.inc (written by tools/gen_iat.py: ImportTable member, DLL, function or
  ordinal) resolves from System32.
- **Text calls:** a text call's slot takes its UTF-8 version (text::SystemRoute).
- **Failure:** a slot that does not resolve is logged and stays null.
- **Order:** it runs before every other static constructor of the exe (init_seg(compiler)), because the harness takes
  import slots in its own.

### platform/platform.h

The game's files go through its import slots (CreateFileA, FindFirstFileA, GetFileAttributesA), so the harness's
redirection and a mod's hooks apply. The types:
- `File`: binary, a Win32 handle, three open modes, every open shared; text lines with `WriteLine`;
- `MappedFile`: read only;
- `ReadFile`: a whole file;
- `FileFind`: a folder search with CFileFind's read-ahead and absolute paths;
- `FileExists`: `_access`.

A failed read, write or seek throws `FileError`, with the OS error and the path in what(). What the game caught (Dib's
reads and writes, the script reader) catches it; the rest ends the game as the original did. For a write to a full
disk, see tests/scenarios/harness_disk_full.txt.
