# Rewrite guide

How to write and change the game's code (fixes, cleanup, new features). Every rule here comes from a bug we hit.

## Where code goes

- `src/game/`: the game. Classes are normal C++: real virtual functions, members of their real types, no
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
cache"). An include change can change how MSVC inlines `std::basic_string` members; a precompiled
header was measured (-18% clean build) and not adopted for that reason. The exe links with `/OPT:ICF`, so never
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
   `::operator new(bytes)`. Every field starts initialised (Lifetimes below), so no object is allocated zeroed. A local with a destructor that replaced a hand-freed helper runs at the
   end of its scope: scope it so it ends before anything it holds is deleted (WorldServer::Destroy's iterator once
   outlived the list it walked: a flaky crash at quit). A `std::vector` is read and written within its size (code
   that read the popped element through `end()` or wrote into spare capacity is undefined): `build.cmd asan`
   finds these (`docs/notes/harness.md`, "AddressSanitizer").
4. **Floating point** is plain C++ (`docs/notes/float-maths.md`): the grouping written is the grouping compiled
   (`/fp:precise`, never `/fp:fast`); no `__asm`, no `#pragma float_control`. FISTP's rounding is `lib::RoundEven`,
   `_ftol` is `static_cast<int>(static_cast<std::int64_t>(x))` where a NaN or a value beyond int can arrive; not
   `std::lrint` or `std::lround`. A select of two floats on `>=` / `<=` goes through `lib/float_select.h`.
5. **Comparisons** are the plain C++ ones; a NaN compares as IEEE says (the original counted NaN as equal and less).
6. **Random draws.** Game state draws from the game's generators in the original's order; a draw whose result is
   unused still happens. Client-only effects draw from their own generator (`docs/notes/core.md`, "Random
   generators").
7. **Iteration order is behaviour** where it feeds game logic or a save: `lib::HashMap` / `lib::IntroSort` keep the
   original's orders; don't swap a container without checking who walks it.
8. **Rebirth's changes** are our code's own (`docs/notes/rebirth.md`); the few left behind flags are
   `ei::rebirth::Enabled(Patch::<name>)` (`src/lib/rebirth.h`). With a flag off the code behaves as the original.
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
  `Ref<T>` (src/game/ref.h), a container element holding a reference a `HeldRef<T>`; a counted object is tested with
  `IsLive(object)`. A record that starts with another's fields derives from it.
- Lifetimes: every field has a default initialiser (counted: a scalar, pointer or enum field without one). An embedded object is a plain member: the compiler builds and
  destroys it, and the member order gives the teardown order (no constructor or destructor called by hand). A
  reset assigns the fields (`Reset()`), never destroys and builds the object again in place.

## Style rules

From the style review rounds (2026-11-04). Checks are named where a pattern can be counted; the others are for the
writer and the reviewer.
- **Numbers have meaning names.** No `#define` for a number or a string (`#define EI_ASSET_CODEPAGE 1251`): a
  `constexpr` in the owning header. A constant is named by what it is for (`kStandingHeight`, `kRateBase`), never by
  its value (`kOne`, `kHalf`, `kTwo`, `kZero`). Checked: `check_cleanup.py` counts "#define EI_ number" and
  "value-named constant" (ratchet; the dashboard charts both).
- **A code has its enum.** The codes of a kind (message ids, states, modes, results) are one `enum` or `enum class` in
  the header of the type that owns them; a function that takes or returns a kind says the enum, not `int`. No
  second list of the same codes in another file.
- **One comment per declaration**, a full sentence that says what it is for and what a caller must know. No hex ids
  in comments: not addon.dll's or EI_Plugin's addresses ("addon's 10025C9D"), not a patch's code location. Say what the
  code does. A format's own ids (a chunk id, a field id) stay where the format is described. Checked:
  `comments.py check` counts "addon addresses" (8 hex digits from 10 to 1F with a letter) in comments, shaders
  included.
- **Rebirth's patch is named.** A comment that says "Rebirth-patched" names the flag (`Patch::CameraPatch`) or says
  "always on" (a fix that has no flag, rule 8). Checked: "unnamed Rebirth patches" per comment line.
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
- **Inputs are `const T&`, results are returned.** An input too big to copy is `const T&` (`std::string_view` for
  text); a result is the return value (a struct or `std::optional` for several or none), not an out-parameter, unless
  the caller supplies the storage (a buffer it reuses).
- **One integer type per quantity.** A count, an index, an id or a size has one type through the code that passes it
  around (`std::int32_t` for a saved id, `std::size_t` for a container index); no converting back and forth at each
  call, no `int` that is compared with an unsigned size.

From round 3 (networking, scripts, saves, sound; 2026-11-04):
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
- **Every field is initialised, a reset assigns.** All fields have default initialisers ("field without a default
  initialiser"); a reset is `Reset()` that assigns the fields, never `OnShutdown` destroying members by hand and
  `new (this)`. A counted object's count starts in its constructor.
- **A saved type pairs `Load` and `Save`**, members declared side by side, reading and writing the same fields in the
  same order (`UnitServer::Load` / `Save` are the model); one name pair (`Load` / `Save`, not `ReadFrom` / `WriteTo`).
  Positional records (script arguments) are read through one cursor in declaration order.
- **When a library is replaced, justify every parameter again.** A parameter that every caller passes as the same
  constant, or that the callee ignores, goes; so does a scale, a callback or a buffer-ownership rule the old library
  needed (sound's `OpenOutput` ignoring three of its four parameters is the example).
- **No current-object globals.** Pass the object or make it a member. Where a global cannot be avoided (a load in
  progress), a scope guard sets it and restores it on every exit; nothing resets it by hand before each `return`.
- **Rebirth's behaviour is named by its flag or "always on"** in comments (see the comment guide); a comment never
  claims a flag that does not exist.
- **One helper per owner; names are unique within a subsystem; declare where initialised.** `IsZero`, the 1/255 colour
  scale and "does the game server run" live once, in their owner's header. Two functions or types of one subsystem
  never share a name with different meanings (`Arg`, `Drop`). No scope block that exists only to copy a temporary
  (`{ std::string tmp = ...; x = tmp; }`): `x = f(...)`. A function that only returns a string literal is a constant
  or a lookup table. Checked: "copied small helper", "function returning only a string literal", "scope block around a
  temporary".
- **Never key on a code address.** A class that needs an id carries an enum `Type()`; dispatch is a `switch` or a
  virtual, not a table of member-function pointers (the exe links with `/OPT:ICF`: two functions can share an
  address). Checked: "pointer to member function" (also the round-2 rule on callbacks).

The ratchet columns are listed by `python tools/check_cleanup.py` (counts) and `python tools/comments.py check`. A
column's pattern is narrow on purpose: it finds the plain cases; the rule is wider than the count.

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
