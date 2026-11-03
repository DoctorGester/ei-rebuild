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
  `pc24.h`, `float_select.h`, `rect.h`, `bytes.h`. Add only what is needed, in small separate additions.
- `src/platform/`: every call of the operating system, by area (files, clock, window, input, sound, sockets), with no
  SDK types; the backends (`win32`, `sdl`, `posix`, `d3d11`, ...) are the only game-side places that include system
  headers (`docs/notes/platform.md`; `tools/check_platform.py` fails otherwise). A new system call goes there.
- `src/render/`: the renderer (`docs/notes/renderer.md`). `src/audio/`, `src/video/`: our mixer and Bink decoder.
- `tests/unit/`: checks of our code without the game. `tests/verify/`: the harness.

Includes: a header that only passes a rectangle, point or size includes `lib/rect.h`; only a file that calls the
system includes its headers. An include change can change how MSVC inlines `std::basic_string` members; a precompiled
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
   `::operator new(bytes)`. An engine object whose constructor leaves fields the game reads is
   `new (kZeroed) T(...)` (engine_object.h). A local with a destructor that replaced a hand-freed helper runs at the
   end of its scope: scope it so it ends before anything it holds is deleted (WorldServer::Destroy's iterator once
   outlived the list it walked: a flaky crash at quit). A `std::vector` is read and written within its size (code
   that read the popped element through `end()` or wrote into spare capacity is undefined): `build.cmd asan`
   finds these (`docs/notes/harness.md`, "AddressSanitizer").
4. **Floating point** is plain C++ (`docs/notes/float-maths.md`): the grouping written is the grouping compiled
   (`/fp:precise`, never `/fp:fast`); no `__asm`, no `#pragma float_control`. FISTP's rounding is `lib::RoundEven`,
   `_ftol` is `static_cast<int>(static_cast<std::int64_t>(x))` where a NaN or a value beyond int can arrive; not
   `std::lrint` or `std::lround`. A select of two floats on `>=` / `<=` goes through `lib/float_select.h`.
5. **Comparisons** are the plain C++ ones; a NaN compares as IEEE says. Next to a comparison whose result changes
   for a NaN (where the original counted NaN as equal and less), `EI_NAN_AUDIT(values...)` logs the first NaN per
   site in the verify and debug builds.
6. **Random draws.** Game state draws from the game's generators in the original's order; a draw whose result is
   unused still happens. Client-only effects draw from their own generator (`docs/notes/core.md`, "Random
   generators").
7. **Iteration order is behaviour** where it feeds game logic or a save: `lib::HashMap` / `lib::IntroSort` keep the
   original's orders; don't swap a container without checking who walks it.
8. **Rebirth's changes** are our code's own (`docs/notes/rebirth.md`); the few left behind flags are
   `ei::rebirth::Enabled(Patch::<name>)` (`src/lib/rebirth.h`). With a flag off the code behaves as the original.
9. **Keep the original's bugs** (leaks, odd messages, kept quirks) unless a ticket decides otherwise, and note them
   in the area's notes under "Original bugs kept".

## Clean code

`build.cmd` runs `tools/check_cleanup.py`: per file, the counts of the patterns below may only go down
(`config/cleanup_baseline.csv`; `--update` after lowering them).
- Fields are members of the class's one declaration, named by what they mean: no offset accessors (`At<>`,
  `Field<>`), no byte arithmetic on pointers, no member named after its offset (`field1B`). A format's field nothing
  reads goes to the record's skip block (`unreadFields`, ...) with the format's ids; bytes a save or packet carries
  as a block stay as padding (`reserved`).
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
  `IsLive(object)`. An object embedded and built by its owner is a `lib::InPlace<T>`. A record that starts with
  another's fields derives from it.

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
