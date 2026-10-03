# 00410000-00430000 and 00470000-00480000

Files: `src/game/core_streams.*` (stream and reference-counted base classes, aligned pointer arrays,
coder state), `src/game/net_session.*` (network pump), `src/game/net_delta.*` (delta replication of
object state), `src/game/chunk_stream.*` (chunked .mob / spec files), `src/game/clip.*` (screen-space
triangle clipping), `src/game/refptr_copies.cpp` (copies of the RefPtr helpers). Verify:
`tests/verify/verify_core_streams.cpp`, `verify_net_session.cpp`, `verify_net_delta.cpp`,
`verify_chunk_stream.cpp`, `verify_clip.cpp`, `verify_refptr_copies.cpp`. Scenario:
`tests/scenarios/net_lobby.txt`. STLport instantiations named in `config/library.csv`.

Shared files changed: `config/library.csv` (217 rows appended). No shared header was changed; the new
code includes `object.h` (CMemoryStream), `core_refcount.h` (RefPtr), `core_maths.h` (StoreFloat),
`maths.h` (Vector3).

## Map

### 00410000-00430000

| Range | Contents |
|---|---|
| 00410750-004114F0 | CCamera members (the camera at 0079B2A8; core.md has the rest): 004107A0 zoom by a power (mouse wheel, 005EB540; clamps to the camera settings object at +0x94), 00410860 / 004108D0 save / restore view slot 0-7 (+0x120 + 0x18 i: zoom, rotation, +0x104; keys in 005EADD0, saves 0057A780), 00410D70 / 00410D10 views 0-3 to / from the settings record (+0x178, axis-angle through `_CIacos`), 00410AB0 / 00410C10 camera to / from a file (0057ABA0, 0057B3B0), 00410E50 / 00411060 / 004110D0 / 00411470 free-camera look (00667BA0), 00411360 look-at target (flag 0x100), 004113A0 camera shake (effects; distance by `_hypot`, entry added to the list at +0x108), 004109E0 rotate by axis-angle |
| 00411530-00412190 | STLport list instantiations for the camera shake list and the camera's other containers (list end / insert / erase / clear, node creation); 00411DF0 / 00412140 / 00412190 are called from 0040CE60 / 0040CFB0 (core range) |
| 004122D0 | **network pump** (main loop, loading screens): ticks the game server and client sessions, then 0057F190 |
| 00412370-00412D70 | network record classes: 0073BB28 (probably the player / session record; installed by 0041A7E0, 0041D420), 0073BB40 |
| 00412D80 | **game server** constructor (pointer 0079B5DC; vtable 0073BB70, base 0073BBB4 = network session at 00436000-00444000); registers its message handlers (0041ADB0, 0041B1C0 chat, 0041B720, 0041B7A0, 0041B970, 0041BA30) and builds ~20 STLport lists |
| 00413250-004179F0 | lobby record / message classes, 0x18 bytes apart from vtable 0073BBF8 to 0073BDD0 (slot 0 deleting destructor, 1 empty, 2 reset, 3 clone, 4 write to a byte cursor, 5 read); their base 0073BC10 (+8 type id), the reference-counted bases 0073BB58 / 0073BB68 (CRefObject); the memory stream's destructor (004173D0), Clear (00417350) and its base CStream (0073BDB0, 004174E0) |
| 00417A10-0041B970 | game server: destructor (00417A10 / 00417BC0), slot 13 description (00417EB0), Tick (slot 14, 004182E0: timers, player list), players (0041A2E0 FindPlayer, 0041A020 leader id), the script's global variables in a network game (00419940 Get, 00419730 / 004197D0 / 004198D0 Set from 004B6760), chat and system text (0041A310, 0041A490), "^%i%s^7: %s" chat line (0041B1C0) |
| 0041BC40-0041D600 | **game client** (pointer 0079B5D8; vtable 0073BDEC, base 0073BE30): constructor 0041BC40 (registers 0041DDC0-0041F1E0 as message handlers; 0041ECC0 is patched by Rebirth), destructor 0041C8B0 / 0041CA10, Tick 0041CCD0 / 0041CD80, kick / ban / zone server command 0041D260 (console), session UI glue (0041D600, 0041D9A0, 0041DA50) |
| 0041D930 / 0041D960 | coder output flush (two copies, the second for the coder state at +0x2800) |
| 0041DD70 | CMemoryStream constructor |
| 0041F460-0042BE60 | STLport instantiations: basic_string (construct, assign, append, substr, compare), the named exceptions, list (construct, begin, end, insert, erase, clear, destroy, node creation), vector bases, `_STLP_alloc_proxy`, hashtable (hash 004298D0, find / insert / resize around 00429B70-0042A150), vector insert (0042AC70, 0042B1A0, 0042BB50), plus 21 copies each of RefPtr::Destroy and a plain RefPtr release |
| 0042BEC0-0042D150 | **chunked file** reader / writer (Chunk, vtable 0073BE94) |
| 0042D160-0042D2C0 | the root chunk that owns the file (vtable 0073BE9C, CFile at +0x18): 0042D1C0 constructor, 0042D160 open, 0042D270 destructor; used by CWorldServer::LoadMapObjects (00596030) |
| 0042D310-0042D4E0 | STLport vector helpers (base constructor, insert / reserve) used by the chunk readers 0042C650 / 004D89A0 |
| 0042D690-0042D700 | CClientSpecificManager (vtable 0073BECC, 15 slots; 0042D700 creates objects by type through 0051AB00, 00510450, 005A7010, 00666D10, 00533AD0, 0052D0D0) |
| 0042D910-0042E6A0 | **triangle clipping** for the mesh renderer's software path |
| 0042E700 / 0042F4C0 / 0042F890 | polygon clipping variants with no references (dead code) |
| 0042FAE0 | referenced from .data (007830C0) only |
| 0042FCA0 / 0042FD10 | coder state (0x2828 bytes; save game and network compression): constructor, coder step (with 004301E0 / 00430450) |

### 00470000-00480000

| Range | Contents |
|---|---|
| 00471A90-004775F0 | the scene loader of CFakeUnit (slot 62, 00446BA0 -> 00446C10 -> 00471B10, 12.6 KB; "Scene %s have more then one top") with its CArray / CMap instantiations (00474C50-004773F0; vtables 0073C800-0073C86C) |
| 00477B00 | applies a unit record to a new unit when the world server creates objects (005927C0): stats, quick items, spells |
| 00477E60-0047A6D0 | network message objects over streams: 0073C8A4 (reference-counted record with a file stream), 0073C8D8 (stream over an MFC CFile), 0073C8F8, 0073C92C, 0073C960 (a second CMemoryStream vtable), 0073C990, 0073C9C8; the connection pump 00479E30 (00566E20, 0058DFF0) and 0047A080 (00566D80) |
| 0047A710-0047ACA0 | Huffman code builder (256 symbol counts at +0x800, qsort, code tree at +0xC04; no references) |
| 0047ADF0 / 0047B1F0 | merges of sorted id arrays (probably which objects a player gains / loses) for the server's per-player update (0058B750) |
| 0047B620 / 0047B940 | arrays of object pointers in 32-byte aligned blocks |
| 0047B980-0047C680 | network driver base (0073C9C8) and another driver (0073C990); slot 9 is 0047BB70 in all of them |
| 0047C8E0-0047D7B0 | CTcpNetDriver (vtables 0073CA00-0073CA10, "CTcpNetDriver::AcceptNewClient()"; winsock socket / select / accept) |
| 0047D7F0-0047DBD0 | stream compression (0047D7F0 / 0047D970 through the coder at 00430450 / 00430960), length-prefixed stream copy (0047DA80 / 0047DAE0) |
| 0047DB70-0047FE90 | **delta replication** of object state |

### What runs when

- Every frame: the network pump (WinMain's loop calls it; the sessions are null in a single-player
  game, so only 0057F190 runs). The stream constructors and destructors (CMemoryStream is created and
  destroyed all over: 28 + 126 + 19 call sites). The clipper runs for meshes drawn through slot 17 of
  vtable 0073D93C (00502CA0) whose triangles cross the viewport.
- Delta replication: the server writes every replicated object's dirty fields per player update
  (0058B750 and the objects' serialisers, e.g. 0050EAF0 for units at +0x1C4); the client reads them
  back. Whether a single-player game goes through it is not known yet: the verify counts of the first
  harness runs will tell.
- Zone load: the chunked-file reader (CWorldServer::LoadMapObjects 00596030 -> 00481660 -> 00481F00,
  the .mob objects), the RefPtr helpers.
- Multiplayer only: the game server and client (`server <map>` in the console after the cheat word
  opens a lobby), the lobby records, the TCP driver.

### Classes

- `Chunk` (vtable 0073BE94, 0x18 bytes; not the models' `ChunkArchive`, which is a different
  format over memory streams): {vtable, id, consumed, size, start, parent}. Slot 0 is a
  parameterless handler called for every chunk ForEachChild reads (empty in the base); slot 1 returns
  the file: the base asks its parent, the root (0073BE9C) returns its embedded CFile. Sizes include
  the 8-byte header; every byte read or written is counted in the chunk and all its parents.
- `CMemoryStream` (object.h): the fields at +0xC / +0x10 / +0x14 are a bit writer / reader: the byte
  being assembled, the mask of the last bit (0x80: a new byte is due; object.h calls it `growBy_`),
  and the index of the byte the bits go to (-1: none). +0x1C is the capacity. Slots: 1 Read
  (004A5380), 2 Write (004A53F0, grows to twice the needed size), 3 Seek (004A5490; also stores the
  pending bits), 4 Size, 5 Position, 6 Clear (00417350), 7 004A55D0.
- `DeltaField` (0xA0): two snapshot streams (+0, +0x28: what channel 0 / 1 last sent) and two
  output streams (+0x50, +0x78). `DeltaState` (at unit +0x1C4): field array +4, count +8, masks
  +0x14 / +0x16 / +0x18 (u16), reader +0x1C, mode +0x20 (0 write all, 1 write dirty, 2 / 3 read
  against snapshot 0 / 1, 4 idle), two header streams +0x24 / +0x4C. Globals: 007A9784 / 007A9780
  (writing: does channel 0 / 1 keep what it sends as its snapshot), 007A9794 / 007A9798 (reading:
  snapshot and received stream).
- Delta format: per value, "all unchanged" bit, else a 0 bit and per 4-byte word an "unchanged" bit
  or a 0 bit and the word, then the remaining bytes raw. Quantised types: 3D position (x, y as
  (v + 16) * 120.47, z as (v + 32) * 341.33, 16 bits each), 2D position ((v + 32) * 60.23), ranged
  values ((v - min) * 65535 / (max - min)), 16-bit floats (sign, exponent - 0x40 in 7 bits, 8
  rounded mantissa bits).
- Reference-counted roots: 0073BB68 (CRefObject: slot 0 deleting destructor, 1 empty) and its
  abstract child 0073BB58 (pure slots 2 / 3: shutdown, clone; see core_refcount.h). CStream 0073BDB0
  (+5 ok, +8 limit 2000000000) is not one of them (its +4..+7 are not a reference count).

## Rewritten

| Address | Name | Verify |
|---|---|---|
| 004122D0 | `net::Pump` | ours only: the stored time is the current one |
| 0041DD70 | `MemoryStreamConstruct` | scratch copy |
| 004173D0, 004790A0 | `MemoryStreamDestruct` (two copies) | scratch copy with the buffer cleared |
| 004173B0, 004790D0 | `MemoryStreamDeletingDestructor` (slot 0) | ours on a scratch copy against the destructor body; then real |
| 00417350 | `MemoryStreamClear` (slot 6) | scratch copy with the buffer cleared |
| 0047A200 / 004174E0 / 00417400 | `StreamConstructBase` / `StreamDestructBase` / `StreamDeletingDestructor` | scratch copy (no delete) |
| 00412720, 00413430 | `RefObjectDestructBase` (two copies) | scratch copy |
| 00412730 | `RefObjectDeletingDestructor` | scratch copy (no delete) |
| 00419720 / 00412700 | `RefValueConstructBase` / `RefValueDeletingDestructor` | scratch copy |
| 0047B940 / 0047B620 | `AlignedPointerArrayAlloc` / `AlignedObjectArray::Clear` | ours only: alignment, header pointer / emptied array |
| 0042FCA0 | `CoderState::Construct` | scratch copy (0x2828 bytes) |
| 0042E4D0 | `clip::LerpVertex` | output: ours into a scratch vertex |
| 0042DB70 / 0042DD00 / 0042DE90 / 0042E020 / 0042E1B0 / 0042E340 | `clip::ClipXBelow` / `XAbove` / `YBelow` / `YAbove` / `ZBelow` / `ZAbove` | ours, record, restore inputs, original, compare |
| 0042D910 | `clip::ClipTriangle` | same |
| 0047DFC0 | `BitStream::PutBit` | stream copies (below) |
| 0047EAE0 / 0047EB20 | `delta::GetBit` / `delta::ReadDiff` | stream copies |
| 0047E6D0 / 0047E5E0 / 0047E740 | `delta::WriteRaw` / `WriteDiff` / `WriteDeltaChannel` | stream copies |
| 0047DB70, 0047FA40 / 0047DBD0, 0047FAA0 | `DeltaField::BeginWriteA/B` / `BeginReadA/B` | field copy, globals mapped |
| 0047DC20, 0047FAF0 | `DeltaField::EndField` (two copies) | field and reader copies |
| 0047DCA0 / 0047E000 | `DeltaField::WriteDelta` / `ReadDelta` | field (and reader, snapshot) copies |
| 0047E140 / 0047E2D0 / 0047E170 / 0047E310 | `WriteBoth` / `WriteStringBoth` / `ReadRaw` / `ReadString` | copies; ReadString ours only (CString) |
| 0047E3A0 / 0047E930 | `DeltaField::WritePosition` / `ReadPosition` | copies, output compared |
| 0047EBF0 / 0047EE00 | `WritePosition2D` / `ReadPosition2D` | same |
| 0047EF80 / 0047F110 | `WriteRanged` / `ReadRanged` | same |
| 0047F1E0 / 0047F3D0 / 0047F4D0 / 0047F7F0 | `WriteFloat16` / `ReadFloat16` / `WriteFloat16x3` / `ReadFloat16x3` | same |
| 0047FB70 / 0047FBF0 / 0047FD50 / 0047FD80 | `DeltaState::Reset` / `BeginField` / `SetWriteMode` / `BeginRead` | state copies |
| 0042BEC0 / 0042BEF0 | `Chunk::Construct` / `Destruct` | scratch copy |
| 0042BF00 / 0042BF40 | `Chunk::Advance` / `ChunkFile` (slot 1) | chain copy / pure |
| 0042BF50 / 0042BFB0 / 0042C000 | `Chunk::BeginWrite` / `EndWrite` / `WriteHeader` | recorded file (below) |
| 0042C060 / 0042C0B0 | `Chunk::SkipContent` / `ReadHeader` | recorded file |
| 0042C140 | `Chunk::ForEachChild` | ours only (calls the zone loader's handlers) |
| 0042C280 / 0042C340 / 0042C4C0 | `Chunk::ReadFloat` / `ReadInt` / `ReadByte` | recorded file, value compared |
| 0042CCC0 / 0042CEC0 / 0042D060 / 0042D020 | `Chunk::ReadVector3` / `ReadQuad` / `ReadQuaternion` / `ReadBytes` | recorded file, output compared |
| 0042C2C0 / 0042C380 / 0042C500 / 0042CD70 / 0042D0E0 / 0042CFA0 | `Chunk::WriteFloat` / `WriteInt` / `WriteByte` / `WriteVector3` / `WriteQuaternion` / `WriteBlob` | recorded file (written bytes compared) |
| 00417A30 ... 0047D560 (21) | copies of `RefPtr::Destroy` (00413270, core) | dry run on a stand-in, as core |
| 004141D0 ... 0047D7B0 (21) | `RefPtrRelease` | dry run; the original is a copy of the 28-byte body |

All names are provisional.

## Verify wrappers

- Scratch copies: constructors, destructor bodies, Clear, the coder state: the original on a copy of
  the bytes it touches, ours on the real object. Where the original would free memory the copy's
  buffer pointer is cleared or the delete flag dropped.
- Clipper: ours runs first on the real data (index list, counts, appended vertices, work buffer,
  last ratio recorded), then the inputs and the ratio are restored and the original runs; counts,
  index lists, appended vertices (bitwise), the work buffer and the ratio must match. The original
  ClipTriangle calls the clippers through their hooked addresses, so their wrappers run inside it.
- Streams (delta replication): every stream the function can touch is deep-copied (header plus a
  private copy of the buffer from the game heap), the globals are pointed at the copies, the original
  runs on the copies; then the globals are restored and ours runs on the real streams. Headers (all
  fields but the buffer pointer) and buffer contents up to the size must match; outputs (read values)
  are compared; globals compared with pointers into the copied field mapped back. Copies are freed.
- Only memory streams (vtables 0073BD90 / 0073C960) are copied; with any other stream class involved
  the wrapper runs ours only.
- Chunked files: ours runs first on the real chunks with the root's slot 1 temporarily answering a
  proxy file that forwards to the real file and records every call (GetPosition, Seek, Read, Write;
  arguments, results, bytes). The original then runs on copies of the chunk chain whose root answers
  a replaying proxy: same calls in the same order with the same arguments and written bytes, recorded
  results and bytes handed back. Chains (id, consumed, size, start of every chunk) and outputs must
  match. A log overflow (more than 256 calls or 256 KB) skips the comparison.
- Network pump: ours only (it ticks the sessions and calls the network step).
- RefPtr copies: dry runs on a stand-in object whose vtable records the destructor and shutdown
  calls (as verify_core_refcount.cpp). The release copies cannot have a trampoline (a short jump at
  +4); one copy of their identical 28-byte body (only internal jumps) is made when the DLL loads.

## Floating point

`clip.cpp` and the quantisation in `net_delta.cpp` are compiled with `float_control(precise, on)`;
dumpbin confirmed the operation order (`(b - a) * t + a`, `(v - offset) * scale`). The clipper's
intersection parameter is computed in a small naked x87 helper (`StoreRatio`) so that, as in the
original, the float is stored with FST while the unrounded quotient is compared with 1. Values the
original rounds through a float before FISTP go through StoreFloat and x87::fistp. Values the
original copies through the FPU (FLD / FSTP: a signalling NaN becomes quiet) go through a small
FpuCopyBits helper (ReadVector3 x / z, WriteFloat16x3 inputs); the other copies are byte copies.
Floats taken only as bytes (WriteFloat, WriteVector3, WriteFloat16) are declared as uint32 bits so
the compiler cannot load them into the FPU.

## Forwarders added (the standalone build must replace)

- 0057F190 the per-frame network step (`NetGame::Step`, on 007AFE20).
- 004A5610 / 004A5650 / 004A54D0 bit stream start byte / end bits / resize (CMemoryStream helpers).
- 00461190 stream ReadString (CString).
- 006FEEFA CWordArray::SetSize (MFC; the clip work buffer).
- Virtual calls through the objects' vtables: CMemoryStream slots 1-5, CFile slots 3 / 10 / 13 / 14,
  the sessions' Tick (slot 14), chunk slots 0 / 1, the objects' slot 4 in AlignedObjectArray::Clear.

## Original bugs and oddities kept

- `PackFloat16`: a mantissa that rounds up past 8 bits wraps to 0 without raising the exponent;
  floats with an exponent below 0x40 get exponent 0 (unpacked as denormals); exponents above 0xBF wrap
  in the 7 bits.
- `ReadDelta` / `ReadPosition` store a changed value in snapshot[1] even when reading against
  snapshot 0.
- `ForEachChild` reads the parent's fields without checking it for null.
- `ReadQuad` returns the four values in the order 2, 0, 3, 1 of the file.
- `ReadVector3` copies x and z through the FPU, y as an integer.
- The clippers keep a vertex exactly on a "keep above" plane out but add the t = 0 intersection
  (equal to it); t above 1 or below 0 (or NaN) emits the edge's end vertex instead of an
  intersection. The work buffer (32 indices) is not bounds-checked.
- `BeginWriteA` returns its argument or 1, `BeginReadA` its argument or the reader (callers ignore
  them); `DeltaState::BeginRead` returns its argument.
- `MemoryStreamDestruct` leaves the freed buffer pointer in place.
- The pump treats a gap of more than 5 s as 1 s.

## Skipped

- Rebirth-patched: 0041ECC0 (a client message handler).
- The game server / client, lobby records, chat, TCP driver, message objects: multiplayer only;
  only the lobby (`net_lobby.txt`) reaches some of it.
- Camera members (00410750-004114F0): driven by keys, the mouse wheel and saves; 004113A0 (shake) and
  00410D70 (`_CIacos`) are good next candidates. The shake list goes through STLport list code.
- 00471B10 scene loader (12.6 KB) and its CArray helpers; 00477B00 unit record (spells / items
  through not-yet-rewritten unit code).
- 0042C580 ReadString (CString buffer API), 0042CE10 / 0042C400 / 0042C440 (not separate functions in
  the function list: inside 0042CD70 / 0042C380), 0042C650 / 0042C8F0 / 0042CA20 / 0042CB80 / 0042C820
  (chunk readers of string arrays and maps).
- 0047D7F0 / 0047D970 (compression through 00430450 / 00430960, outside the range), 0047DA80 /
  0047DAE0, 0047FDC0 / 0047FE90 (inside the function list's 0047FD80 / 0047FE90 bodies; 0047FE90
  commits the masks).
- Functions too short for the 5-byte jump (`RET`, `MOV EAX,[ECX]; RET`, ...), STLport / MFC / CRT
  instantiations (named in library.csv), EH funclets.
- 0042E700 / 0042F4C0 / 0042F890 / 0047A710-0047ACA0: no references.

## Scenarios

- `net_lobby.txt` (new, ~25 s): the lobby (`server bz7g-lmp`): the pump with live sessions, streams,
  RefPtr copies.
- Everything else is covered by the existing new-game scenarios (`sandbox_actions.txt`,
  `sandbox_map.txt`, `combat.txt`, `effects.txt`, ...): zone load (chunk reader), streams, the pump,
  and replication / clipping if a single-player game uses them.
