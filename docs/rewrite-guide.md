# Rewrite guide

How to write and change the game's code (fixes, cleanup, new features). Every rule here comes from a bug we hit.

## Where code goes

- `src/engine/`, `src/game/`: the engine (no game header included: `docs/notes/file-layout.md`, "The engine
  library") and the game. Classes are normal C++: real virtual functions, members of their real types, no
  hand-built vtables, no layout asserts. Keep a byte layout only where bytes leave the process or come from a fixed
  format (saves, packets, resource files, GPU structs), and say so there (`docs/notes/core.md`, "Values read and
  written as bytes").
- `src/lib/`: shared helpers: `crt.h` (the game CRT's behaviour where it differs from the host's), `str.h` (what
  CString did that std::string has no member for), `hash_map.h` (`lib::HashMap`: STLport's walk order),
  `intro_sort.h` (`lib::IntroSort`: the game's sort, where the order of equal keys is behaviour), `vecmath.h`,
  `float_select.h`, `rect.h`, `bytes.h`. Add only what is needed, in small separate additions.
- `src/platform/`: every call of the operating system, by area (files, clock, window, input, sound, sockets), with no
  SDK types; the backends (`win32`, `sdl`, `posix`, `d3d11`, ...) are the only game-side places that include system
  headers (`docs/notes/platform.md`; `tools/check_platform.py` fails otherwise). A new system call goes there.
- `src/render/`: the renderer (`docs/notes/renderer.md`). `src/audio/`, `src/video/`: our mixer and Bink decoder.
- `tests/unit/`: checks of our code without the game. `tests/verify/`: the harness.

Includes: a header that only passes a rectangle, point or size includes `lib/rect.h`; only a file that calls the
system includes its headers. A widely included header names no `<algorithm>`, `<chrono>`, `<format>` or
`<filesystem>` without a need of its own (each costs 0.3-2 MB per object; docs/notes/standalone.md, "The compiler
cache"). An include change can change how MSVC inlines `std::basic_string` members, so there is no precompiled
header. The exe links with `/OPT:ICF`, so never
compare function pointers for identity.

## The rules

1. **Virtual functions are the compiler's.** Don't give a non-virtual method the name and signature of a base
   virtual (`EngineObject::Create`, `Destroy`, `Release`, `Load`, `Save`, `GetName`, ...): it silently becomes an
   override. A derived class that runs the base's body calls it qualified (`MapObject::AttachObject(...)`): an
   unqualified call dispatches back to the override.
2. **No forwarders.** Call every function by name. Where the harness changes what our code does, it sets a named
   seam (`IsaacRandom::SetSeedHook`, `IsaacRandom::SetDrawObserver`, `libcrt::SetRandObserver`), never an address.
3. **Memory.** `new T(...)` / `delete p`; an owned array is a `std::vector<T>` (`std::unique_ptr<T[]>` where its
   address is handed out and must not move); a block of a byte count other than its type's is
   `::operator new(bytes)`. Every field starts initialised (Lifetimes below), so no object is allocated zeroed. A
   local with a destructor runs at the end of its scope: scope it so it ends before anything it holds is deleted (an
   iterator that outlived the list it walked was a flaky crash at quit). A `std::vector` is read and written within
   its size (code that read the popped element through `end()` or wrote into spare capacity is undefined):
   `build.cmd asan` finds these (`docs/notes/harness.md`, "AddressSanitizer").
4. **Floating point** is plain C++ (`docs/notes/float-maths.md`): the grouping written is the grouping compiled
   (`/fp:precise`, never `/fp:fast`); no `__asm`, no `#pragma float_control`. FISTP's rounding is `lib::RoundEven`,
   `_ftol` is `static_cast<int>(static_cast<std::int64_t>(x))` where a NaN or a value beyond int can arrive; not
   `std::lrint`, and `std::lround` only in client-only code (effects, particles, drawing, window sizes and the
   interface's displays; "Style rules" below). A select of two floats on `>=` / `<=` goes through `lib/float_select.h`.
5. **Comparisons** are the plain C++ ones; a NaN compares as IEEE says (the original counted NaN as equal and less).
6. **Random draws.** Game state draws from the game's generators in the original's order; a draw whose result is
   unused still happens. Client-only effects draw from their own generator (`docs/notes/core.md`, "Random
   generators").
7. **Iteration order is behaviour** where it feeds game logic or a save: `lib::HashMap` / `lib::IntroSort` keep the
   original's orders; don't swap a container without checking who walks it.
8. **Rebirth's changes** are our code's own (`docs/notes/rebirth.md`), with no flag: a GOG and a Rebirth install run
   the same code. The mod-folder patches apply whenever a mod is played (`modfolders::Active`); EI_Plugin's player
   count and pause mode are the host's choices on the create-server screen (the original's by default). A fatal error
   ends the game on every install (`game::FatalExit`); a data error the game can get past is a warning
   (`game::Warning`) and the safe local thing (rebirth.md, "Fatal errors").
9. **Fix the original's bugs** where they are found: crashes, undefined behaviour (null or out-of-range access, NaN
   loops, unchecked divisions, uninitialised fields, addresses used as values), leaks and bookkeeping nobody sees. A
   fix that moves a golden re-records it and names the reason. A bug that shapes gameplay (balance, AI, movement,
   sounds, messages the player sees) needs the user: leave it, and list it in the area's notes under "Original bugs
   kept" with what a fix would change.

## Clean code

`build.cmd` runs `tools/check_cleanup.py`: per file, the counts of the patterns below may only go down
(`config/cleanup_baseline.csv`; `--update` after lowering them).
- Fields are members of the class's one declaration, named by what they mean: no offset accessors (`At<>`,
  `Field<>`, a file's own `Get<T>(base, 0x40)`), no byte arithmetic on pointers, no member named after its offset
  (`field1B`). A format's field nothing reads goes to the record's skip block (`unreadFields`, ...) with the format's
  ids; bytes a save or packet carries as a block stay as padding (`reserved`).
- Virtual calls through named virtual functions; no `VCall`, no slot numbers.
- Game globals are named variables; no literal addresses.
- No one-line getter that only returns a field under another name; getters stay where they are virtual, compute or
  convert, or are a file's only const view.
- Kinds, class ids, flags and save versions are named by the one enum or constant in the owning header (`ClassId`,
  `ItemKind`, `UnitOrderKind`, `AIModeType`, `LandscapeFlag`, `kVersion*` in save_versions.h, ...).
- Text is `std::string` (`src/lib/str.h` for the CString extras); containers are `std::` (the `lib::` ones where the
  order is behaviour). Call the container's members, don't hand-inline them; no per-file container layouts; no
  `using` alias for a container used once.
- Parameters and fields carry their types: no `void*` that every caller casts. Manager objects come typed
  (`BaseManager::CreateObjectAs<T>` / `FindObjectAs<T>`, `itemdb::RecordAs<T>`). A counted pointer field is a
  `Ref<T>` (src/engine/core/ref.h), a container element holding a reference a `HeldRef<T>`; a counted object is tested
  with `IsLive(object)`. A record that starts with another's fields derives from it.
- Lifetimes: every field has a default initialiser (counted: "field without a default initialiser", a scalar, pointer
  or enum field without one). An embedded object is a plain member: the compiler builds and destroys it, and the member
  order gives the teardown order (no constructor or destructor called by hand). A reset assigns the fields
  (`Reset()`), never destroys members by hand and builds the object again with `new (this)`. A counted object's count
  starts in its constructor.

## Style rules

From the style review's rounds; the tools cite them by round. Checks are named where a pattern can be counted; the
others are for the writer and the reviewer.
- **Numbers have meaning names.** No `#define` for a number or a string (`#define EI_ASSET_CODEPAGE 1251`): a
  `constexpr` in the owning header. A constant is named by what it is for (`kStandingHeight`, `kRateBase`), never by
  its value (`kOne`, `kHalf`, `kTwo`, `kZero`). Checked: `check_cleanup.py` counts "#define EI_ number" and
  "value-named constant" (ratchet; the dashboard charts both).
- **A code has its enum, and it is an `enum class`.** The codes of a kind (message ids, states, modes, results) are one
  `enum class` in the header of the type that owns them, with a fixed underlying type where the value is saved or sent;
  a function that takes or returns a kind says the enum, not `int`. No second list of the same codes in another file.
  Enumerators carry no `k` or kind prefix (`LogCategory::Ai`, not `kLogAi`). A flag set is an `enum class` that opts in
  to `src/lib/bit_flags.h` (`|`, `&`, `~`, `lib::Has`), not a plain enum and not int constants. An enum that indexes a
  table is indexed through `lib::EnumArray<T, E, N>` (`src/lib/enum_array.h`), so no cast stands at each use; cast
  (`static_cast`) only where a value is saved, sent or read from data. The one plain `enum` left is an array-index enum
  local to a .cpp whose last enumerator is a count (`...Count`, `...Kinds`, `...Values`). Checked: `check_cleanup.py`
  counts "plain enum (not enum class)" (ratchet at 0).
- **One comment per declaration**, a full sentence that says what it is for and what a caller must know. No hex ids
  in comments: not addon.dll's or EI_Plugin's addresses ("addon's 10025C9D"), not a patch's code location. Say what the
  code does. A format's own ids (a chunk id, a field id) stay where the format is described. Checked:
  `comments.py check` counts "addon addresses" (8 hex digits from 10 to 1F with a letter) in comments, shaders
  included.
- **Rebirth's patch is named.** A comment that says "Rebirth-patched" names the patch (CameraPatch) or says
  "always on" (rule 8), and never claims a flag that does not exist. Checked: "unnamed Rebirth patches" per comment
  line.
- **In-memory structs are typed.** Members have their real types (an enum, a `Ref<T>`, a `Vector3`), not the bytes they
  were read from. A byte layout (fixed-width members, padding, `static_assert` on the size) belongs to the structs
  that cross the process: a file, a save, a packet, a GPU buffer; and says so.
- **A virtual needs two real overrides.** A class with one implementation is a plain class; a callback is a virtual
  interface or a `std::function`, never a pointer-to-member plus an object pointer passed together.
- **A feature's code shares a file.** The members of one feature (a window, a tool, a message group) live in the
  feature's files, not spread by the order they were decompiled in. The checks that look at game state (digests,
  consistency, debug dumps) go to `*_checks` files and are not mixed into the code they check.
- **State is taken or read, never both.** A function that receives the state it works on (a reference to the world,
  the player) does not also read the global that names the same object; one path chooses. No function-local `static`
  state: a counter or a cache that outlives the call is a member of its owner (Reset() clears it).
- **Inputs are `const T&` or views, results are returned.** An input too big to copy is `const T&`
  (`std::string_view` for text); a pointer that is never null is a reference, and a helper that builds a string only
  to pass its address takes the text. A result is the return value (a struct or `std::optional` for several or none),
  not an out-parameter, unless the caller supplies the storage (a buffer it reuses). Checked: "const string or vector
  pointer parameter" (a non-const `std::vector<T>*` may be the caller's buffer and is not counted).
- **One integer type per quantity.** A count, an index, an id or a size has one type through the code that passes it
  around (`std::int32_t` for a saved id, `std::size_t` for a container index); no converting back and forth at each
  call, no `int` that is compared with an unsigned size.

From round 3 (networking, scripts, saves, sound):
- **Documented undefined behaviour is not a fix** (rule 9). A comment that says a read is uninitialised, a pointer may
  be null or a copy is unbounded, and keeps it, is a bug listed: fix it, or, if only a player can see it, list it under
  "Original bugs kept" in the area's notes. `volatile` is not a thread flag (`std::atomic`).
- **One little-endian reader, one stream vocabulary.** Integers come out of bytes through `lib::Load<T>` /
  `lib::Store`, and through the streams' and buffers' own members (`ReadAs<T>`, `ReadCount`, `ReadId`, `Put<T>`,
  `Get<T>`); a file does not define its own `U32` / `Get32` / `ReadWord`, and nothing but a buffer's own file moves a
  cursor by hand (`memcpy(...); p += N`). Checked: "own integer reader" and "cursor moved by hand after memcpy".
- **A field or parameter that holds a kind takes the enum**, so a literal does not compile; message ids, connection
  states, packet and award kinds, music modes, image formats and the like are enumerators, and a method is named by
  what it means (`SendWeather`), never by its id (`SendState71`). A version test follows `save_versions.h`'s
  convention. Checked: "enum never used as a type", "hex-named message method".
- **A saved type pairs `Load` and `Save`**, members declared side by side, reading and writing the same fields in the
  same order (`UnitServer::Load` / `Save` are the model); one name pair (`Load` / `Save`, not `ReadFrom` / `WriteTo`).
  Positional records (script arguments) are read through one cursor in declaration order.
- **When a library is replaced, justify every parameter again.** A parameter that every caller passes as the same
  constant, or that the callee ignores, goes; so does a scale, a callback or a buffer-ownership rule the old library
  needed (sound's `OpenOutput` ignoring three of its four parameters is the example).
- **No current-object globals.** Pass the object or make it a member. Where a global cannot be avoided (a load in
  progress), a scope guard sets it and restores it on every exit; nothing resets it by hand before each `return`.
- **One helper per owner; names are unique within a subsystem; declare where initialised.** `IsZero`, the 1/255 colour
  scale and "does the game server run" live once, in their owner's header. Two functions or types of one subsystem
  never share a name with different meanings (`Arg`, `Drop`). No scope block that exists only to copy a temporary
  (`{ std::string tmp = ...; x = tmp; }`): `x = f(...)`. A function that only returns a string literal is a constant
  or a lookup table. Checked: "copied small helper", "function returning only a string literal", "scope block around a
  temporary".
- **Never key on a code address.** A class that needs an id carries an enum `Type()`; dispatch is a `switch` or a
  virtual, not a table of member-function pointers (the exe links with `/OPT:ICF`: two functions can share an
  address). Checked: "pointer to member function" (also the round-2 rule on callbacks).

From round 4 (world, objects, combat, app and platform):
- **A member body lives once.** When a class is split across files, three lines in a row that match a member's body
  are the member's call, not a second copy: the body stays in the virtual or method itself, and a sibling file calls
  it. Checked: "member body copied in a family file" (three
  significant lines of at least 90 characters that an earlier `.cpp` of the same family also has; a family is the
  directory and the first two words of the file name; headers and repeats inside one file are not counted).
- **One name per code, one code per name.** A sentinel or code constant (`kNone`, `kNoRange`, `kFar`, `kKind*`,
  `kMode*`, `kFlag*`, `kMask*`) is defined once, in the owner's header, as an enum where it is a kind; the same name
  with another value in a second file is a bug waiting (`kNoNaturalWeapon` was 0x10, 7 and 8). Checked: "constant
  defined twice with another value" (column-0 `k...` definitions; narrowed to those names, since a plain tuning number
  such as `kLineHeight` or `kMargin` is meant to differ from file to file).
- **A flag constant never renames an owner's bit.** A local `kFlagX = 0x80000` beside an enum that gives the bit a name
  (`MapObjectFlag::RelationsChanged`, the AI's `kFlagNearbyListed`) is the same bit under two names; use the enumerator.
  Checked: "flag bit under a second name" (a `k...Flag/Bit/Mask` constant of one bit of 0x100 or more in a file other
  than the one that defines an enumerator of that bit in an enum named `...Flag(s)` or `...Bit(s)`).
- **A comment that says "never read" is a deletion** in the same change: the member, parameter or branch goes with it
  (gameplay code stays; technical dead code goes). A field that a saved file or a packet carries and the code does not
  use keeps its place under an `unread...` name (`unreadText`, `unreadFields`); the comment on its line is not
  counted. Checked: `comments.py` "never-read comments" ("never read", "nothing reads", "nobody reads", "not read").
- **No virtual without a caller, no empty virtual that is still called.** A virtual that nothing calls is a deletion,
  and so is a call to a virtual that nothing implements (`Unused14` through `NotifyCompanion`). Checked: "virtual never
  called" and "empty virtual still called" (a name-based index of the tree: a call is `.Name(`, `->Name(` or `Name(`
  after something that is not a type; two classes that share a name count together).
- **A downcast virtual has two overrides, or it is a test of the kind.** `virtual X* AsFoo() { return nullptr; }` or
  `virtual bool IsFoo() const { return false; }` with one override in the tree becomes a non-virtual member that tests
  the kind field and casts (UI tokens first, then the items' `AsWeapon`). Checked: "downcast virtual with one
  override" (a pointer that answers `nullptr`, or an `Is...`/`Has...` that answers `false`; a virtual that answers 0
  for a count or a stage is a policy hook and not counted).
- **Helpers take and return values.** No helper over the address of a constant (`ConstPtr(2.0f)`, a `kFloatConstant`
  reference taken only for its address) and no `const float* a, const float* b, float* out` signature; a float field
  or parameter is a `float`, never its bits. Inlining an `inline` helper is exact by construction. Checked: "float
  helper over addresses (ConstPtr, out pointer)".
- **No x87 store residue** on an x64-only build: no `volatile float`, no `float stored = x; x = stored;`, no comment
  that a value is "stored as a float". Every SSE float operation already rounds to float, so deleting the residue is
  exact. Checked: "x87 store residue (volatile float, store and reload)" and the `comments.py` ban on "stored as/to/in
  a float".
- **A data header holds data.** A function whose body is `return 12.5f;` or `return "text";` is a constant in its
  owner's header. A header shaped like the exe's `.rdata` (constants for AI, interface and world in one file) is split
  by the code that uses them. Checked: "function returning only a number (header)" (virtuals
  and overrides are not counted) beside the string-literal column of round 3.
- **A member uses `this`.** Inside a member the object is `this`, never its own global (`WorldServer` members calling
  `g_serverWorld.Loot`); a free function that needs a world takes it. Checked: "member through its own global" (a
  `Class::Member` body that names a global declared `extern Class g_...;`).
- **The platform contract names concepts.** `platform.h` and the game speak in UTF-8, fixed-width integers, our enums
  and structs; Windows encodings (`WM_KEYUP`'s `lParam`, wheel steps of 120, `HWND`, `GetTickCount`) stay at the reader
  of a format that stores them and in `src/platform/win32`. Every printf-like function carries
  `EI_PRINTF_FORMAT(format index, first argument)`, so `%lu` with a `uint32_t` does not compile clean. Checked:
  "Windows name outside the Windows backend" (the Win32 type, message and clock names, `<windows.h>`, in code) and
  "printf-like function without format annotation" (a function with `const char* format, ...` or a `va_list` that no
  declaration annotates).
- **No empty namespace** left by deleted code. Checked: "empty namespace".
- **Comments** (`comments.py` banned words, round 4): "new renderer" (it is the renderer), "inlined", "provisional"
  (a name is final or it is renamed), a layout in bytes ("the first 0x20 bytes of PathOrder"), "Direct3D" and
  "DirectDraw" (the old device), "old path", "game.exe" and "through its address".

From round 1b (unit AI, items, terrain):
- **A re-spelled copy of an existing helper is a defect.** A random word scaled by hand (`Next() * 0x1p-32f + low`, a
  `2.3283064e-10f` scale, `kTwoToMinus32`) is `IsaacRandom::Range` or a member beside it; the copies drift. Checked:
  "random word scaled by hand (Range copy)" (`src/lib` and `random.cpp` are exempt; the scale as `0x1p-30`..`0x1p-32`
  or a `[1-9].dddddde-10` decimal).
- **A field has one start state.** A field with a default initialiser is not given another value in a constructor's
  body or initialiser list, and not given a different one in `Reset()`; the initialiser carries the real start value
  and the constructor needs no body. A value that comes from a constructor parameter is not a start state. Checked:
  "field with two start values (initialiser, constructor or Reset)" (extends the "field without a default
  initialiser" column of round 3).
- **`std::lround` is allowed in client-only code** (rule 4, `docs/notes/float-maths.md`): the effects, particles,
  lightning, weather and terrain drawing, the renderer, the platform layer's window sizes, and the interface's layout
  and displays round values that only the picture, the sound or the window see, and a half differs there by one
  colour step at most. A rounded value that gates an order or reaches game state is `lib::RoundEven` or
  `lib::Truncate`, as the original's FISTP or `_ftol` did. Checked: "std::lround in game-affecting code" (every
  `std::lround` outside the client-only files listed in `tools/check_cleanup.py`).

The ratchet columns are listed by `python tools/check_cleanup.py` (counts) and `python tools/comments.py check`. A
column's pattern is narrow on purpose: it finds the plain cases; the rule is wider than the count. The columns that
read the whole tree (constants, virtuals, start states, copied bodies, printf annotations) are tested in
`tools/test_style_checks.py` on small trees.

## Names and files

- Members have plain names, no trailing `_`; where a member and a parameter would share a name, rename the parameter
  (never `this->`). /W4's C4458 finds the clashes.
- No `C` prefix on classes. Rename wherever a better name helps, through the whole tree in one step.
- Files are grouped by feature: a class's members live in its subsystem's files.

## Verification

A change is held to the behaviour the goldens pinned; neither check depends on heap or memory layout.
- **Goldens** (`tests/golden`, `docs/notes/standalone-verification.md`): a scenario's run, its state digests and
  saves compared field by field, with `work.py test <ID> --golden`. Reach the code from a scenario with a golden, or
  add one under `tests/scenarios/`.
- **Unit checks** (`tests/unit`, `ei_unit_check.exe`, run by the build): our code without the game.

## Notes

Record in `docs/notes/<area>.md`: how the system works, decisions and why, formats, original bugs kept, and how the
area is checked. Current facts only; git keeps the history.
