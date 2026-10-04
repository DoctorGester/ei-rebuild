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
  through not-yet-rewritten unit code). Both rewritten later: objects.md, "Third round".
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

## Second round: 00430000-00444000 and compression

Files: `src/game/net_coder.h/.cpp` (buffers, bit cursors, the LZ coder); verify
`tests/verify/verify_net_coder.cpp`. `CoderState` moved from `core_streams.h` to `net_coder.h`
with its real field layout (core_streams.h now includes net_coder.h; the constructor 0042FCA0
stays in core_streams.cpp, same behaviour, field names updated).

### Map of 00430000-00444000

| Range | Contents |
|---|---|
| 0042FAE0 | startup initialiser (from .data 007830C0): fills the coder's code tables 0079B6B0 / 0079B738 (offsets 0..0x21: bit counts / codes) and 0079B7BC / 0079B844 (lengths 0..0x22) |
| 0042FCA0-00431070 | **LZ coder** (CoderState, 0x2828 bytes): constructor 0042FCA0, EncodeMatch 0042FD10, Compress 00430450, Decompress 00430960; cursor helpers PutBits 004301E0, WriteBytes 00430930, ReadBytes 00430F60, EndWrite 00430F90, Reset 00430FC0, GetBits 00430FE0, GetBit 00431040 |
| 00431070-00432D20 | **dead code**: a key exchange and stream cipher (RIPEMD-128-like compression function 004319E0, 4.8 KB; big-number multiply / modular reduction 00431370 / 004315B0 / 00431487; key setup 00431090 / 00431110, keystream XOR 00431220, 00431300). Only 00431070 (an atexit registration from the .data initialiser list) is referenced; nothing calls 00431090-00431300 (scanned for CALL/JMP rel32 and absolute references). Not rewritten. |
| 00432DA0-00436000 | lobby record containers: classes with vtables 0073BF38-0073BF5C on the CRefObject roots (00433060 / 00433320 / 004334E0 constructors and copies), their STLport list / hash_map instantiations (00433710-00439D80, the 666-byte functions 0043BBC0 ... 0043D040 are hashtable copies), serialisers over BufferCursor (004355B0 / 00435820 write / read records, called from the lobby record classes 00412AC0-00416850; 00435A00 / 00435F30 whole tables) |
| 00436000-00440000 | network session base (vtables 0073BBB4 / 0073BE30 / 0073BF64-0073BF94): 00436770 base constructor (called by the server 00412D80 and client 0041BC40 constructors), 00436CF0 remove client ("REMOVE client"), 00437080 update from a host ("ERROR: Update from unknown host"), 00437370 receive ("ZERO length packet received", "TIMEOUT client drop"), more STLport instantiations up to 0043FE90; 0043D830 / 0043FE90 are generic containers also used far outside (004F5090, 005E0E90, 005FAB80) |
| 00440000-00444000 | session / driver glue: 00440440 (called from save / load code 0057BDF0, 0057E740 and 0061C2E0), 004409A0 (sessions' constructors), client base slots 004418F0 ("Normal update when not in game"), 00441940 / 00441B40 / 00441D40, 004420E0 ("CONNECTION LOST"), 00442300 / 00442590 (from the menus 0061B2C0 / 0061C2E0), server base slots 00443080 (patched by Rebirth), 004434F0, 00443590 (called by the server Tick 004182E0) |

### Compression

Buffers (`GameBuffer`, 0x1C bytes: vtable, begin, cur, end, high, base, flags): the growing buffer
0073D624 (constructor 004DA7F0, grows to (base - begin + cur + n) * 2 + 0x40) and the fixed view
0073CA64 over memory owned elsewhere. A `BufferCursor` (0x18 bytes: p, reserved byte pointer, bit
accumulator, bit count, sink buffer, start) is bound to a buffer by 004DA450 / 004DA490 (read /
write; forwarders) and commits its position to the buffer's `cur` at the end (slot 2, empty in
both classes). Bits go LSB first; a byte is reserved in the output when a field starts in a new
byte, so literal bytes and bit fields interleave in the stream.

Format: int32 uncompressed size; per item a 0 bit and a literal byte, or a 1 bit, a length (2..0xE1:
table codes up to 0x22, else 0x17/5 bits + 6 bits, 0x3F/6 bits + 7 bits) and an offset - 1 in a
1 KB window (table codes below 0x22, then prefixes 2/3, 6/3, 3/2 with 6, 8, 10 bits). The
compressor hashes (previous byte << 2) ^ byte into 0x400 chains of distances, takes the first
2-byte match within 0x31E and extends it greedily, switching to an earlier string that repeats the
match plus the new byte. Callers: save game write 00579B80 (compresses, then decompresses the
result and compares it with the input; with a checksum at version > 0x6B) and read 00579830,
multiplayer map transfer 00662AC0 / 00662DD0, network messages 0047D7F0 / 0047D970, the client
0041D600. So every save (the new game's autosave) and load in single player runs it.

Rewritten:

| Address | Name | Verify |
|---|---|---|
| 00430450 | `CoderState::Compress` | original on a scratch coder into a clone of the output buffer; input header saved / restored; input header, output header and bytes, coder state compared |
| 00430960 | `CoderState::Decompress` | same |
| 0042FD10 | `CoderState::EncodeMatch` | original on a scratch holding the fields after the tables; those compared |
| 004301E0 / 00430FE0 | `BufferCursor::PutBits` / `GetBits` | original on a cursor copy; cursor (and value) compared |
| 00431040 | `BufferCursor::GetBit` | a copy of the 44-byte body (CMP / JAE at the start: no trampoline) runs as the original |
| 00430930 / 00430F60 | `BufferCursor::WriteBytes` / `ReadBytes` | cursor copy; bytes compared (reads over 64 bytes: ours only) |
| 00430F90 / 0041D930 / 0041D960 | `BufferCursor::EndWrite` / `End`, `CoderEndOutput` | ours only (virtual commit): no pending bits, the sink's cursor is ours, the return value |

Notes: `End` returns EAX as the original leaves it (the commit's result, or `this` without a
sink; 00579830 returns it with AL cleared). The compressor's decoder tables and the bit masks
(0078DB24) are read from the game's memory. Reset (00430FC0) and FlushBits (00442570) are tiny
and only used internally (not hooked).

Original oddities kept: exactly 8 pending bits are not stored until more bits arrive; the
decompressor writes the window without its mirror half (the compressor keeps both); the length
codes above 0xE2 and offsets from 0x562 have branches that cannot be reached.

Coverage: `world_load.txt` (autosave on a new game, then loading it) and every scenario that
starts a new game (its autosave) reach Compress / Decompress and the cursor helpers.
No new scenario.

### Skipped in this round

- 00431070-00432D20: dead (above).
- 00443080 (patched by Rebirth), 0041ECC0 (patched).
- The session / lobby classes 00432DA0-00444000 and the server / client constructors 00412D80 /
  0041BC40: multiplayer only, mostly STLport containers with EH frames; next candidates are the
  record serialisers 004355B0 / 00435820 (over BufferCursor, reached by the lobby scenario) and
  the server Tick's 00443590.

## Third round: sessions, connections, messages (00400000-00440000 worker)

Files: `src/game/net_classes.h` (session / client / server / message layouts), `net_session_base.cpp`,
`net_client.cpp`, `net_server.cpp`, `net_messages.cpp`, `net_connection.h/.cpp`; the RefPtr share
copies in `refptr_copies.cpp`. Verify: `tests/verify/net_verify.h` (BuiltCompare: two objects built
by two constructor implementations compared dword by dword, pointers outside the image pairing one to
one; session table and slot-13 helpers), `verify_net_session_base.cpp`, `verify_net_client.cpp`,
`verify_net_server.cpp`, `verify_net_messages.cpp`, `verify_net_connection.cpp`,
`verify_refptr_copies.cpp`. Scenario: `net_lobby.txt` (the host's client connects to its own server
over loopback, so constructors, replication and the slot-13 collectors run every frame).

### Classes

- SessionBase (vtable 0073BF5C, 0x2568 bytes): +0x18 frame, +0x20 double time, +0x28 connection list,
  +0x2C type registry (two hash_maps: message vtable -> type id, type id -> factory), +0x54 UDP socket
  driver, +0x64 one "reliable" byte per message type, +0x164 / +0xD64 / +0x1964 handler vectors per
  channel (0 created, 1 changed, 2 removed) and type. Handlers are `__fastcall(session, message)`.
- ClientSession (0x2700, vtable 0073BDEC) and ServerSession (0x2600, vtable 0073BB70): see
  net_classes.h. +8 of both is 0xDEC0AD07.
- Message (vtable 0073BC10): {vtable, CRefObject count, kind 1}; classes 0073BBF8 ... 0073BDD0 with
  slots 0 deleting destructor, 2 reset, 3 clone, 4 write, 5 read. Factories for types 1..17 are listed
  in SessionBase::RegisterMessageTypes.
- Connection (vtable 0073BF38, 0x1168): object replication to one peer (see net_connection.h for the
  format: 11 priority maps of ReplicaEntry (0073BF4C), the send history with what each packet carried,
  the reliable byte stream sent in chunks of up to 255 bytes, `records` of the objects received).
  Globals: 0079B920 the session being updated, 0079B924 the connection, 0079B900 the send buffer,
  0079B91C the network log stream, 0079B8D0 the static {object, priority} vector of CollectChanges.

### Rewritten

| Address | Name | Verify | Reached by |
|---|---|---|---|
| 0041BC40 | `ClientSession::Construct` | ours real; original into a scratch block; BuiltCompare (+0x54 socket skipped), tables, player record; scratch deleted by its destructor | net_lobby |
| 00412D80 | `ServerSession::Construct` | the same (ours first so the real one gets the port) | net_lobby |
| 00436770 | `SessionBase::Construct` | original on scratch, BuiltCompare; scratch destroyed by 00436A50 | net_lobby (inside the originals) |
| 00429F40 / 0042A890 / 00438930 | `TypeRegistry::TypeOf` / `Register` / `Create` | pure / ours only + lookups / both create, same vtable | net_lobby |
| 004377D0 / 004142F0 | `SessionBase::AddHandler` / `RegisterMessageTypes` | ours only, tables read back | net_lobby |
| 00413250 / 004186E0 | `NewRefObject` / `MessageConstructBase` | ours only / scratch | net_lobby |
| 0041C870 0041C680 0041C740 0041C7C0 00418C30 00413350 004138A0 00413C90 004140B0 | message constructors (named by vtable) | scratch copy (string blocks pair up, freed) | net_lobby |
| 0041C420 | `ClientVectorConstruct` | scratch | net_lobby |
| 004292F0 00429350 00429430 00429710 00429930 00429990 004299F0 00429A50 00429AB0 | `RefPtrShareReturn` (9 copies) | dry run on stand-ins (clone stand-in added) | net_lobby |
| 00435A00 | `Connection::SendUpdate` | ours only | net_lobby |
| 00435F30 | `Connection::ReadRecord` | ours only (cursor advanced) | net_lobby |
| 004352F0 / 00435F10 / 00434C20 / 00434990 / 00433F20 | `AttachRecord` / `IdPool::Allocate` / `WriteAcks` / `QueueHeader` / `RollBack` | ours + lookup / copy / private copies / ours + order check / ours + flag | net_lobby |
| 004346D0 / 00434110 | `Connection::CollectChanges` / `Track` | ours only + counts | net_lobby |
| 00417EB0 / 0041D050 | `ServerCollectReplicated` / `ClientCollectReplicated` (slot 13) | ours into the real vector, original into a scratch one, items compared | net_lobby |

### Forwarders added (the standalone build must replace)

STLport instantiations of the session containers (list constructors 0041F8B0 ... 00424750, 0041FD30,
00422400 ... 00421B20; hashtables 00438B70 / 00439F20 / 0042B9C0 / 0042AA90 / 0042B8A0 / 0042AA00 /
0042A810; vectors 0043D5F0 / 0041F790 / 00426B10 / 00439E10 / 00439DD0; the connection's lists and
trees 004397E0 00437F30 00438F60 00439C80 00439C50 00438660 00438640 00438650 00438770 004387C0
00439A60 0043DBE0 0043CE70 00439AA0 00439B70 00439A70 0043D3E0 00439C40 00439B80 0043D2E0 0043C3B0
00437B90 0043A090 0043C4B0 00437D60 00439540 00437D40 0043A050 0043CA00 00437F10 00439730 0043A070
0043C900; sort 0043F870 / 0043F9B0), the strings 00424E40 / 0041F460 / 00421800, RefPtr helpers
00438A90 / 00438F50 / 00438110 / 0041DD50, the GameBuffer constructor / grow 004DA7F0 / 004DA5C0,
socket driver 004407F0 / 004409A0 / 00440B90, unit record builder 00661050, 00418530 (server map list),
the handler registration templates 00428090 ... 004288E0 / 004279B0 / 00427AC0 / 00427C40, the
iostream inserters 006FC31D / 006FC0B0 / 00434FE0 and atexit 006E727B.

### Original behaviour kept

- SendUpdate: an entry that overflows the budget stays in the packet uncounted; the rest of that
  priority waits, lower priorities are still tried.
- ReadRecord's skip path creates a throwaway object without a null check.
- The player record and Track's entry have their reference count written before the null check.

## Fourth round: ChunkArchive (00409070-0040A400)

File `src/game/chunk_archive.cpp`, verify `tests/verify/verify_chunk_archive.cpp`. Shared header
`model.h`: ChunkArchive gained `Ref`, NextChild, AppendChild, FindChild, CountChildrenOf,
ResizeToCurrent, Open, Close (declarations only); the EnterChunk / LeaveChunk / Field / CountChunks
forwarders moved out of `model_db.cpp` (they are rewritten now).

Archive layout beyond model.h: +0x00 index stream, +0x28 data stream (data at +0x40), +0x54 version,
+0x5C hash_map<int key, object> of loaded objects, +0x70 list of objects to save. Class tables:
0079B284 class id -> factory, 0079B270 vtable -> class id. A saved archive is three top-level chunks:
1 (scratch data, read back on load), 0 (index: class id and key per object), 2 (data: one child 1
per object holding child 0 = key and child 1 = the object's own fields).

| Address | Name | Verify | Reached by |
|---|---|---|---|
| 00409070 / 004091C0 / 00409230 / 00409A60 | NextChild / FindChild / CountChildrenOf / CountChunks | pure, side by side | every scenario (database load at startup, unit saves) |
| 004092B0 | Field | loading: scratch buffer compared; writing: ours + size | every scenario |
| 004098D0 | EnterChunk | original first, its chunk popped, ours; same chunk | every scenario |
| 00409980 / 00409110 | LeaveChunk / AppendChild | ours only + depth / size | every scenario |
| 00409A40 / 00409E80 | ResizeToCurrent / ChunkRefReset | original then ours, same header / pure | every scenario |
| 00409EA0 / 00409F30 / 0040A3C0 | ReadStreamChunk / FindStreamChunk / WriteStreamChunk | ours only | every scenario |
| 00409AE0 / 0040A000 | Open / Close | ours only + stack depth | every scenario (startup databases; autosave) |

Kept: LeaveChunk (writing) takes the child's content pointer before AppendChild resizes the stream;
a failed EnterChunk while loading leaves the chunk pushed; a loaded archive's Close gives every
loaded object one reference and takes it back.

## Fifth round: CBaseManager (00402B20-00404A00)

src/game/base_manager_core.cpp (tests/verify/verify_base_manager_core.cpp); the list iterator in
object.cpp. Vtable 0073B870 (25 slots). Members as used: +0x1C objects table (STLport hash_map id ->
object), +0x30 highest id, +0x34 MFC CPtrList of registered objects (head +0x38, tail +0x3C, count
+0x40, free +0x44, blocks +0x48), +0x50 table id -> list position (+0x64 highest key), +0x68 table
type -> CPtrList* of spare objects (+0x7C highest type), +0x80 table type -> spare count (+0x94
highest type), +0x98 / +0x9C id range [low, limit) for new ids, +0xA0 / +0xA4 (0, 300 on Create),
+0xA8 last id handed out, +0xAC context (Init / SetContext).

Objects are never freed while the manager lives: RemoveObject turns them into spares of their type
(id -1) and CreateObject takes a spare before asking slot 14 for a new one. Ids come from maxId + 1
while that stays inside the range, otherwise from rand()-based probing (100000 tries; then the fatal
"CBaseManager::RegisterInstant" message, and the code carries on with id -1).

| Address | Name | Verify | Reached by |
|---|---|---|---|
| 00402B20 / 00402DF0 | ConstructBase / DestructBody | scratch copy, BuiltCompare / ours only | every scenario (managers built at startup) |
| 00402DD0 / 00402D20 | deleting destructors (manager / engine object) | ours only | every scenario (exit, object deletion) |
| 00402D40 / 00402CE0 / 00402CF0 | CopyFrom / default name / description | side by side | every scenario |
| 00403020 / 004031B0 / 004031E0 | Create (slot 2) / Init (13) / Destroy (3) | ours only + state | every scenario (startup, new game, exit) |
| 004034E0 | DestroyAll (slot 15) | ours only + empty table | only through vtables (caller not traced) |
| 00403730 / 00404040 / 00403AE0 | CreateObject (23) / RemoveObject / ReleaseObject (24) | ours only + registration | every scenario |
| 004039B0 / 00403BD0 | Unregister(id) / RegisterInstant(object) | ours only + registration | a derived manager (vtable around 00743D10: ReleaseObject override 00691B00, 00691BF0, 00693C60); not traced to a scenario |
| 00404410 | FreeRecords (spares freed) | ours only + empty tables | Destroy, Load, 00564E70 |
| 004046E0 / 00404760 | Load / Save (slots 9, 10) | ours only | save / load scenarios |
| 004047E0 / 00404860 | LoadObjects / SaveObjects (end mark -0x521) | ours only | save / load scenarios |
| 004048F0 | ReleaseUnused | ours only | world save code (00597BD0) |
| 00404920 / 00404960 | ObjectListIterator::First / Next | ours + state / side by side | lights, world object walks (every scenario) |
| 00403720 | SetContext | ours only | 0058AF1B |
| 00402D60 / 00402D70 / 00402D90 / 00402DA0 / 00402DB0 | empty slots 16, 17, 19, 20, 21 | side by side | every manager vtable |

Not hookable (shorter than the 5-byte jump; the standalone build defines them): 00402CC0 (slot 5,
RET), 00402CD0 (slot 6, return 0), 00402D00 / 00402D10 (engine object slots 9 / 10, RET 4),
00402D80 (slot 18, return 0), 00402DC0 (slot 22, RET), 00403010 (RET).

Kept: DestroyAll and Destroy read a node's next pointer after Release may have returned the object
(and freed its table node) — the STLport pool keeps the memory, the original does the same.
LoadObjects reads an id equal to the end mark as an object id when a 1 byte follows.

Forwarders left (STLport / MFC template instantiations used by the manager, all library code):
the hashtable finds / inserts / erases / clears / resizes at 00404C30-004085E0, CPtrList
NewNode 004069A0, AddTail 00405040, RemoveAt 004050C0, RemoveHead 00404FE0, CPlex::FreeDataChain
006FE90F, CEngineObject::SetOwner 00455970, the engine object's destructor body 00455930.

## Dead code found in this range

00402310 (452), 00402780 (radix sort, 887), 0042E700 (3436) and 0042F890 (397, called only
by 0042E700 and 0042F4C0) have no references: only coincidental aligned dwords inside other
functions' code made tools/find_dead_code.py count them live. Added to config/dead_code.csv.
