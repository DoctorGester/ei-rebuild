# Cleanup tooling (ticket C1, cleanup plan Phase 0)

Tooling only; no source file under `src/` changed.

## `tools/progress.py`: cleanup artefacts

After the usual rewritten/verified numbers it prints a "cleanup artefacts" block over `src/*.cpp`
and `*.h` (comments blanked first, so a mention in a comment does not count). One small function per
count; the plain "how often does this appear" counts are two regex tables at the end of the file.
`tools/srcscan.py` holds what both scripts share (loading the sources, blanking comments, finding
the `EI_ORIGINAL(0x...)` sites).

How each number is defined, so later runs stay comparable:

- **EI_ORIGINAL forwarders**: sites of `EI_ORIGINAL(0x...)`, and distinct addresses.
- **forwarded from several files**: addresses with a forwarder in more than one file.
- **naked, not forwarders**: `__declspec(naked)` whose declaration reaches `{` before `EI_ORIGINAL`
  (a hand-written asm body). The plan's "328 x87/asm helpers" counted something wider.
- **files with #pragma float_control**: real pragmas only (the baseline's 83 included
  `particle_common.h`, which only mentions it in a comment).
- **unknown_XXX fields**: occurrences of `unknown_<hex>`.
- **At<> / Field<> uses**: occurrences of `At<` and `Field<`. **files defining their own At()**:
  any file with a function `At(...) {` (the baseline's 47 matched only the exact
  `T& At(void* object, int offset)` form; that form is 53 files today).
- **ei::VCall uses**: `VCall<`.
- **game data addresses**: distinct literals `0x0073xxxx`-`0x008Fxxxx` (data, bss, vtables, strings).
- **`__fastcall Fn(T* self, void*)` shims**: non-naked `__fastcall` definitions with a body whose
  first parameter is a pointer and whose second is `void*` (the EDX dummy of the thiscall trick),
  with any further parameters; the number in brackets is the strict two-parameter form. The
  baseline's 174 used a narrower pattern that we could not reproduce, so do not compare it with
  today's number.
- **struct names defined in several files**: `struct`/`class` with a body at column 0, same name in
  more than one file.
- Also reported from the plan's table: `reinterpret_cast`, `constexpr uintptr_t kX = 0x00...`,
  `*At` view structs, anonymous namespaces, `mfc::`/`stlp::`/`gamecrt::`/`gameapi::` uses,
  `EI_REPLACE` registrations.

## `tools/gen_originals.py`: `docs/originals-report.md`

Reads `src/` and writes the report (never edits sources): every original address that has an
`EI_ORIGINAL` forwarder in more than one file, with each file, line and declared signature (the
declaration back to the previous statement, comments and `__declspec(naked)` removed, parameter
name comments kept). Each address is tagged by how far the files agree (identical, same signature
with different helper names, differing types or calling convention) and sorted into three groups:
already rewritten (a direct call replaces the forwarders), library code (goes with the library
layer) and not yet ported (one shared declaration; the future `src/game/originals.h`).

The output is deterministic. `python tools/gen_originals.py --check` exits 1 when the committed
report is stale. "Rewritten" means an `EI_REPLACE(0x...)` / `EI_REPLACE_METHOD(0x...)` with a literal
address (the macro-built registrations in `ui_hud.cpp` are not seen, as in `progress.py`).

## `tools/check_returns.py`: void declarations whose callers use EAX (T1)

Two crashes came from rewrites declared `void` whose original leaves a value in EAX that callers
use (UnitActionPoint 0052FDF0 returned its out pointer, CUiWindow::AddSurface 005FFA90 the surface
it was given). For every `EI_REPLACE` / `EI_REPLACE_METHOD` / `EI_ORIGINAL` declared void (map +
undname; the parsing is shared with `check_signatures.py`, which now exposes `declarations()`,
`registrations()` and `candidates_for()`), the tool finds the direct `CALL rel32` sites in game.exe
(checked to be instruction starts by disassembling the containing function) and follows the code
after each call with capstone, both ways at conditional jumps, until EAX is written, a call, or
RET. Reading EAX/AX/AL/AH first flags the site. A path reaching RET (and a `JMP rel32` tail call)
means the caller hands EAX to its own callers, which are followed up to three levels. Sites inside
replaced functions are skipped. A write of AL/AX counts as a full write (MSVC's `MOV AL,..; PUSH
EAX` bool argument, `FNSTSW AX; SHL EAX,n`), and `XOR/SUB/SBB EAX,EAX`, `OR EAX,-1`, `AND EAX,0`
are writes; without those rules the first run had five false positives (Chunk::Destruct,
Chunk::WriteFloat, DriverSession::Pump, CUiWindow::ClearRegions, CWorldServer::UpdateWeather).

Not seen: calls through vtable slots and other indirect calls (UnitActionPoint is slot 53, so the
tool cannot find its crash); the summary counts void replacements with no direct call site.
`--address 005FFA90` checks an address whatever its declaration: it flags AddSurface's three sites
(0061C008, 0062BB3D, 0062BBA7, MOV reg,EAX).

build.cmd runs it after check_thunks with the build's own map. Replacement hits fail the build
unless listed in `tools/check_returns_allow.txt`; forwarder hits are warnings, counted and listed
with `-v` (our code ignores the value, and the check fails once such a function is replaced with
the void declaration). First run: 0 replacement hits, 22 forwarder warnings, e.g. constructors
returning `this` declared void (StringFromChars, MessageStringConstruct, RecordsConstruct),
FatalDirectX (returns 0 when it does not exit; callers compare it), BitStream::ReadString, SurfaceDraw::Begin/Text,
CampItems::ReturnAll, PartyAdd, SoundStop. No declarations needed fixing.

## C3, first part: shared forwarder headers

One declaration per address for 156 of the 213 forwarded addresses that the report marked identical or
"names differ" (405 sites in 151 files; the nested ones inside `namespace {}` / named namespaces moved to the
global shared header). The headers are `src/game/originals_<area>.h` (core, engine, script, render, units,
world, ai, ui, gameplay, effects by address range; library for STLport/MFC/CRT code). Each declares the
forwarders as `__declspec(naked) inline ... EI_ORIGINAL(addr)` and forward-declares the types it names (so
including them does not pull class headers or their globals into the files); the files include the header after
their own includes and use one helper name per address (the most common one; the rest were renamed in the
files, and in files that include an edited header). Rewritten targets stay forwarders (no call to the hook).
Left alone (57 addresses still listed): member-style declarations (`Str::Mid`, `SurfaceDraw::Clear`, ...),
signatures that use types defined in several places or only in a .cpp, the definitions in `src/lib/`, and
0x006E9180 (`scriptparse::Strncmp` is the same address declared elsewhere). Not touched: the 504 addresses whose
signatures differ in types or calling convention.

Void fixes: every forwarder `check_returns -v` listed (22 at the start, 38 sites in the end, because each run
only names one declaration per address) now has the real return type: constructors `this` (`StringRep*`,
`RawString*`, `void*`), `PartyAdd`/`QueueAdd`/`Text`/`PlaySound`/`FatalDirectX`/`GetText`/`ServerUpdateAt` `int`,
`FindFreeCellNear`/`AddressInit` `bool`, `SurfaceDraw::Begin` and `ReturnAll` `void*`, `TopicsInsert` `void**`;
member forwarders changed in their class stubs too. `check_returns` now reports 0 forwarder warnings.

## C3b (top 10 by site count): one declaration each

Decided from the game.exe disassembly (RET n, callers' use of EAX/ST0/AL), each now declared once:

| Address | Declaration | Header |
| --- | --- | --- |
| 006E727B | `int __cdecl GameAtExit(void(__cdecl*)())` (atexit, RET, 0/-1) | originals_library.h |
| 00455950 | `void* __fastcall ObjectWorld(const void*)` (object +0x14 -> +0xAC, slot 13, or null) | originals_engine.h |
| 0040C720 | `void __fastcall StlStringDestroy(void*)` (STLport string dtor; plain RET, the old `(void*, void*)` forms dropped the dummy EDX) | originals_library.h |
| 006FDAE9 | `CString CString::Left(int) const` (RET 8: result slot, count), a member of `mfc::CString` | lib/mfc.h, lib/mfc.cpp |
| 005C1010 / 005C0DF0 | `bool __fastcall SectorMapRemove / SectorMapAdd(CSectorMap*, void*, CEngineObject*)` (AL = done, RET 4); callers cast with `AsSectorMap` / `AsEngineObject` | originals_ai.h |
| 00700970 | `mfc::CString::~CString`; `CStringDestroy(void*)` is an inline over it | originals_library.h |
| 006FE925 | `void* __fastcall StringArrayConstruct(void*)` (returns `this`); `console::CStringArray::CStringArray()` calls it | originals_library.h |
| 005FDC00 / 005FDC60 | `ui_gdi::GameDC* __fastcall SurfaceGetDC(void*)` / `void SurfaceReleaseDC(void*)` (surface = CUiSurface) | originals_ui.h |

The class members that were forwarders for the same address (`CUnit::World`, `CMapObject::World`,
`CVisibilityFog::World`, the per-file `SurfaceDraw::Begin/End`, `ui_gdi::SurfaceDC::GetDC/ReleaseDC`,
`StripSurface`, `BoxSurfaceAt`, `SurfaceText`) are now one-line inline wrappers over the shared forwarder.
`CString::Left` callers that built the result into raw memory (`scene_material`, `world_briefing`,
`world_game`) now use a normal `CString` local. The shared forwarder replaced a duplicate local `CIacos` in
sound_manager.cpp (identical bytes) that clashed with originals_library.h.

## C3c (next 15 by site count): one declaration each

Same method as C3b. 494 of the 509 "differ" addresses are left (the 15 below were removed from the group). Decided
from the game.exe disassembly (capstone), each now declared once in `originals_<area>.h`:

| Address | Declaration | Header |
| --- | --- | --- |
| 00455940 | `void* __fastcall ManagerContextOf(const void*)` (object +0x14 -> +0xAC, plain RET) | originals_engine.h |
| 0050E9F0 | `void* __fastcall UnitMarkChanged(void*, void*, int part)` (RET 4; EAX on exit is the object's world or null, which the tail-calling rewrites hand on, so it is not void) | originals_units.h |
| 0041F460 / 004217E0 | `void* __fastcall StringInitEmpty(void*)` (RET, returns `this`) / `StringInitWithAllocator(void*, void*, const void*)` (RET 4, returns `this`) | originals_library.h |
| 00428F10 / 00425040 | `void __fastcall StringAssignSpan(void*, void*, const char*, const char*)` (RET 8) / `StringBufferFree(void*)` (RET). `StringAssignRange` was taken by 00426A80 | originals_library.h |
| 00403F40 | `CEngineObject* __fastcall ManagerFindObject(void*, void*, int)` (RET 4, object or null) | originals_core.h |
| 0057F440 | `bool __cdecl IsNetworkClientOriginal()` (no arguments: it reads the global at 0079B5D8 and never touches ECX; the callers' `0x007AFE20` / `kGameFlow` ECX arguments were dummies) | originals_world.h |
| 005935C0 | `void __fastcall WorldRemoveObject(void*, void*, int id)` (RET 4) | originals_world.h |
| 00434FE0 / 006FC0B0 / 006FC31D | `void* __fastcall OstreamManipulate(void*, void*, void*)` / `OstreamPutChar(void*, void*, char)` / `OstreamPutString(void*, void*, const char*)` (all RET 4, return the stream) | originals_library.h |
| 004800E0 | `int __cdecl FatalDirectX(long, const char*, ...)` (printf style; the `(long, const char*)` copies were the same call with no varargs) | originals_engine.h |
| 005FDE20 | `int __fastcall SurfaceDrawText(void*, void*, const char*, tagRECT*, int, uint32, int, int)` (RET 0x18) | originals_ui.h |
| 005C0BD0 | `void __fastcall SectorMapFindUnitsInRadius(void*, void*, const void* center, void* list, float)` (RET 0xC) | originals_ai.h |

Class members that forwarded the same address (`AiLogStream` / `Ostream` / `GameOstream` / `GameOStream` / `LogStream` /
`UnitLogStream` members, `SurfaceDraw::Text`, `StripSurface::Text`, `BoxSurfaceAt::Text`, `SurfaceTextAt::Text`,
`SurfaceText::Draw`, `StlString::InitEmpty/AssignRange/Destroy`, `Network::IsMultiplayer`, `SessionAt::IsMultiplayerClient`,
`World::RemoveObject`, `Unit::MarkChanged`, `CMapObject::MarkChanged`) are now one-line definitions over the shared forwarder.
`world_effect_internal.h` keeps `effect::ManagerContext` / `effect::FindObject` as inline wrappers. The release and verify builds,
check_stolen, check_x87, check_signatures and check_returns (0 problems, 0 forwarder warnings) pass.

## C3d (next 15 by site count): one declaration each

Same method as C3b/C3c; the "differ" group is 482 addresses after this (it was 497). Decided from the game.exe disassembly
(capstone), each now declared once:

| Address | Declaration | Header |
| --- | --- | --- |
| 006FE958 / 006FE9B8 | `void __fastcall StringArrayDestruct(void*)` (plain RET) / `StringArraySetSize(void*, void*, int count, int growBy)` (RET 8); `console::CStringArray` members are one-line wrappers | originals_library.h |
| 004DBE40 | `void __fastcall StringSplit(const char*, void* array, char)` (ECX text, EDX array, RET 4) | originals_library.h |
| 004DA7F0 / 004DA910 | `void* __fastcall GrowingBufferConstruct(void*)` (returns `this`, plain RET) / `void GrowingBufferDestruct(void*)`; the old `(GameBuffer*, void*)` forms passed a dummy EDX that is dropped | originals_library.h |
| 0040C5A0 / 0040C770 | `void* __fastcall StringInitFromChars(void*, void*, const char*, const void* alloc)` (RET 8, returns `this`; files keep typed `static` wrappers) / `void StringRepDestroy(void*)` (plain RET) | originals_library.h |
| 006E79A0 | `void* __cdecl GameMemmove(void*, const void*, size_t)` | originals_library.h |
| 00480080 | `int __cdecl FatalErrorOriginal(const char*, ...)` (plain RET). `FatalError` is the rewrite in app.cpp, so the shared name differs; `src/lib/crt.cpp` keeps its own copy (lib cannot include game headers) | originals_engine.h |
| 004DEFC0 / 004E0020 | `int __fastcall TextsOpenSection(void*, void*, const char*)` (RET 4, EAX 0/1) / `TextsGetString(void*, void*, const char*, mfc::CString*)` (RET 8, EAX 0/1) | originals_engine.h |
| 0055FB00 / 0058E5D0 / 00594180 | `CEngineObject* __fastcall WorldFindUnit(void*, void*, int)` (RET 4, unit or null) / `void WorldRemoveTimed(void*, void*, int)` (RET 4, EAX not a result) / `void* WorldGetPlayer(void*, void*, int)` (RET 4, player record or null) | originals_world.h |
| 00530930 | `void* __fastcall UnitPrototypeOf(const void* unit)` (plain RET, record or null); callers keep `static` typed wrappers (`UnitPrototype`, `HoverPrototype`) | originals_units.h |

Class members that forwarded the same addresses (`World::GetPlayer`, `World::FindUnitObject`, `WorldBase::FindUnit`,
`World::RemoveTimedObject`, `StlString::InitFrom`, `RawString::InitFrom`, `TextReader::Open`, `TextTable::Find`, `Texts::Open/GetString`,
`UiTexts::Find`, `WorldAt::GetPlayer`) are one-line definitions over the shared forwarders. Release and verify builds, check_stolen,
check_x87, check_signatures and check_returns (0 problems, 0 forwarder warnings) pass.

## C3e (next 15 by site count): one declaration each

Same method as C3b-C3d; the "differ" group is 468 addresses after this (it was 483). Addresses with a site in a `net_*` file or in
ui_battle_update.cpp / ui_hud.cpp were skipped (other tickets work there); the next ones by site count were taken. Decided from the
game.exe disassembly (capstone), each now declared once:

| Address | Declaration | Header |
| --- | --- | --- |
| 004C2050 | `void __fastcall PtrArraySetSize(void*, void*, int count, int growBy)` (MFC CPtrArray::SetSize, RET 8) | originals_library.h |
| 006E848B | `int __cdecl GameMbscmp(const char*, const char*)` (plain RET). `src/lib/crt.cpp` keeps a private `MbscmpOriginal` (lib cannot include game headers) under the `unsigned char` `gamecrt::mbscmp` | originals_library.h |
| 006FDA6D | `mfc::CString mfc::CString::Mid(int first) const` (RET 8: result slot, first). It is Mid(nFirst), not Right: the callers' `Right(Length - n)` and `CStringMid` all pass a start index | lib/mfc.h, lib/mfc.cpp |
| 0040A3A0 / 0040F560 | `void __fastcall RefObjectRelease(void*)` (plain RET, EAX not a result) / `float* __fastcall QuaternionAxisY(const void*, void*, float* out)` (RET 4, returns `out`; the `void` copy in world_client_draw.cpp is gone) | originals_core.h |
| 00460AF0 / 00460BC0 | `float __fastcall NamedTableGet(void*, void*, const char*)` (RET 4, ST0) / `void NamedTableSet(void*, void*, const char*, uint32_t valueBits)` (RET 8; raw dword, float callers pass `std::bit_cast`, so no FPU load touches a NaN payload) | originals_engine.h |
| 004DF8D0 | `int __fastcall TextsReadLineOriginal(void*, void*, mfc::CString*, int advance)` (RET 8, EAX 0/1) | originals_engine.h |
| 00500960 / 00468F70 | `void* __cdecl DisplayObject()` (returns the global 007AF060) / `char* __fastcall AllocAligned16(int count)` (plain RET) | originals_render.h |
| 004D23A0 | `int __fastcall ScriptLoadLine(void*, void*, const mfc::CString*, int)` (RET 8) | originals_script.h |
| 00530730 / 00531070 | `void* __fastcall UnitEquipArmourOriginal(void*, void*, void*)` (RET 4, previous item or null) / `int UnitAddWeaponOriginal(void*, void*, void*)` (RET 4, index / 1 / -1) | originals_units.h |
| 00554450 | `void __fastcall ControllerRepositionOriginal(void*)` (plain RET; the `(void*, void*)` forms passed a dummy EDX) | originals_units.h |
| 00675CA0 | `void __fastcall ObjectMarkNewOriginal(void*)` (plain RET) | originals_effects.h |

Every other site keeps its local name and signature as a one-line wrapper over the shared forwarder (generated, then the self-named
duplicates deleted), so call sites are unchanged. The `Right` members of the per-file CString views are gone; their callers use
`CString::Mid`. Release and verify builds, check_stolen, check_x87, check_signatures and check_returns pass (0 problems).
