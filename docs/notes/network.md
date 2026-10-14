# Network (0x00412D80-0x00444000 sessions, 0x00470000-0x00480000 streams, links, delta replication)

Files: `net_classes.h` (session / client / server layouts), `net_session*.cpp`, `net_connection*.{h,cpp}`,
`net_messages.{h,cpp}`, `net_server_*.cpp`, `net_client_*.cpp`, `net_lobby.cpp`, `net_socket_driver.cpp`,
`net_coder.*`, `net_delta.*`, `net_link.*`, `net_session_pump.cpp`, `net_state.cpp`. Verify:
`tests/verify/verify_net_*.cpp` (+ `net_verify.h`). How the classes are declared after the cleanup:
`cleanup-network.md`. Names are provisional. Header comments of the `net_*` files say what each
function does; this file keeps what is not visible in the code.

## What runs when

- Every frame: `net::Pump` 004122D0 (WinMain's loop and loading screens) ticks the game server and
  client sessions, then 0057F190 (`NetGame::Step`, on 007AFE20). In single player the sessions are
  null (pointers 0079B5DC server / 0079B5D8 client, bit 31 of +4 = inactive), so only 0057F190 runs.
- Single player still uses the stream code: every new game and load creates two joined loopback
  links (`LoopbackLink::CreatePair` 004782E0, from the client world 00566CE0): the first is the
  server-side player's link, the second the client's, so every message goes through Send / Receive,
  the LZ coder (`CompressTo` / `DecompressTo` every frame: ~820 coder calls in `sandbox_actions`),
  `BitStream::WriteStream` / `ReadStream` and the delta writers. Saves go through `FileStream` and the
  coder (`ReadSaveFile` / `WriteSaveFile` 00579830 / 00579B80: write compresses, decompresses the
  result and compares it with the input; checksum above version 0x6B).
- Multiplayer only: the server / client sessions (`server <map>` in the console after the cheat word
  opens a lobby), lobby records, the TCP / UDP drivers, `DriverLink`s (remote players only), trades,
  kick / ban, zone commands, awards, shared files. `net_lobby.txt` (`server bz7g-lmp`, ~25 s) is the
  only scenario: the host's client connects to its own server over loopback, so constructors,
  replication, the slot 13 collectors and the pump run every frame. Not reached by any scenario:
  other players joining, trades, kick / ban, awards, shared files, `GameInfoRecord::Read`, the drop
  paths (timeouts, bad updates), recording and replay (no `.apl` file exists), the drivers.

## Class map

- `SessionBase : CRefCounted` (vtable 0073BF5C, 0x2568; `ServerBase` 0073BBB4 and `ClientBase` 0073BE30
  are the layers the server / client constructors inline): +0x18 frame, +0x20 double time, +0x28
  connection list, +0x2C type registry (two hash_maps: message vtable -> type id, type id -> factory),
  +0x54 UDP socket driver, +0x64 one "reliable" byte per message type, +0x164 / +0xD64 / +0x1964
  handler vectors per channel (0 created, 1 changed, 2 removed) and type; handlers are
  `__fastcall(session, message)`. Slots: 2 RemoveClient, 3 OnUpdate, 4 OnGetInfo, 5 OnLogin, 6 OnLeave,
  7 OnInfo, 8 OnAccepted, 9 OnRejected, 10 OnMaster, 11 OnClientLost, 12 OnClientLeft, 13 CollectReplicated,
  14 Tick (receive). Packet kinds (first byte): -1 master (10), 0 update (3), 1 leave (6), 2 none,
  3 get info (4), 4 login (5), 5 info (7), 6 accepted (8), 7 rejected (9). +8 of both sessions is 0xDEC0AD07.
- `ServerSession` (0x2600, vtable 0073BB70, global 0079B5DC) and `ClientSession` (0x2700, 0073BDEC,
  0079B5D8): their lists are named in `net_classes.h`. Server slots 14 Tick (004182E0: base tick
  00443590, timed lists +0x25C0 / +0x25B4, master-announcement timer +0x2570 through 0041AA10), 15
  Describe, 16 Admit (ban list, 20-byte password, player limit). Client slots 15 OnJoined (base 0041C440 is
  empty; `ClientSession` 0041D410 begins the multiplayer game through `GameFlow::BeginMultiplayerGame`), 16
  CreateEntry (0041D420: an empty server entry, kind 4).
- `Connection : CRefObject` (0073BF38, 0x1168): replication to one peer: 11 priority maps of
  `ReplicaEntry` (0073BF4C), the send history (what each packet carried), the reliable byte stream sent in
  chunks of up to 255 bytes, `records` of received objects; slot 2 = reset 00433320, slot 3 Clone.
  Globals: 0079B920 session being updated, 0079B924 connection, 0079B900 send buffer, 0079B91C
  network log stream, 0079B8D0 static {object, priority} vector of `CollectChanges`, 0079B8E0 receive buffer.
- `Message : CRefObject` (0073BC10, +8 version) with slots 2 reset, 3 clone, 4 Write, 5 Read. Type id
  (registered in `SessionBase::RegisterMessageTypes` 004142F0):
  client to server: Login 0073BBF8 (5), Say 0073BC28 (6), Command 0073BC40 (7, {kind, text}), FileRequest
  0073BC58 (13), Party 0073BDD0 (15, a trade offer), PartyMember 0073BD78 (16, a CMemoryStream);
  server to client: Player 0073BC70 (1), Line 0073BC88 (2, chat), Host 0073BCA0 (3, "host:port" +
  zone; built by `world_game.cpp` AnnounceHost), Binding 0073BCB8 (4), Zone 0073BCD0 (8), Map 0073BD00
  (9), Variable 0073BCE8 (10), File 0073BD18 (11), FileChunk 0073BD30 (12, 64-byte chunks), Award
  0073BD48 (14), Trade 0073BD60 (17). The lobby's server entry is 0073BB28 on base 0073BB40.
  Command kinds in `ServerOnCommand` 0041B2A0 (host only): 0 preview a map, 1 zone command (queued map
  whose zone is the text minus "_1" enter / "_2" leave / "_3" finish), 2 / 3 kick / ban by name, 4 / 5 by serial.
- Master server: port 28004 (`SetPublic` 00417E10); TCP link 0x40 bytes (outgoing and incoming
  growing buffers, socket, state; 00440C60 / 00440D40 / 00440DE0). UDP driver 004407F0-00440BE0 and
  address parse / format 00440440 / 00440690; session rate 12500 local / 2500 otherwise (00783E34 / 38,
  clamp 500..50000, send interval from 0073BF48). Winsock is called through the game's import thunks.
- Game port: one UDP port, 8888 (int 00783E30), for the server session's socket (00412D80 opens it through
  004409A0 and ignores a failed bind: the session then has no socket), the local client's join of 127.0.0.1
  (StartServer 0057E740) and the lobby's pings. A second game on the machine therefore joins the first
  one's server and loses it when that game quits (the "disconnect" box, N0c); harness games each get a
  port of their own (`tests/verify/harness_net.h`). Our `Open`, `ClientBase::RemoveClient` and the frame's
  disconnect box log a line to ei_hook.log (diagnostics only).
- Lobby: client base 004413E0-00442AD0 (server list, direct queries, join request / retry, ping /
  info / accepted / refused, FlushBits 00442570), server base 00442C60-00443590 (pings answered,
  joins accepted / refused, pending joins aged), the 0x2C-byte query 00443770-00444270. The master's
  server list reply (0041D600) is a compressed run of {address, GameInfoRecord}.

## Rewritten

Verify idioms: *BuiltCompare* = the original builds a scratch object, ours the real one, dword by dword (heap pointers paired
one to one, the +0x54 socket skipped); *deep copies* and *recorded CFile*: core.md ("Streams"); *ours only* = state checks.

| Addresses | Names | Verify |
|---|---|---|
| 004122D0 | `net::Pump` (a gap over 5 s counts as 1 s) | ours only |
| 0041BC40, 00412D80, 00436770 | `ClientSession` / `ServerSession` / `SessionBase::Construct` | BuiltCompare |
| 00429F40, 0042A890, 00438930, 004377D0, 004142F0 | `TypeRegistry::TypeOf` / `Register` / `Create`, `AddHandler`, `RegisterMessageTypes` | pure / ours only |
| 00413250, 004186E0, 0041C870 0041C680 0041C740 0041C7C0 00418C30 00413350 004138A0 00413C90 004140B0, 004279B0 ... 004288E0 (11) | `NewRefObject`, `MessageConstructBase`, message constructors, handler-registration templates (`net_handler_registrars.cpp`); the 17 message classes' factory / clone / reset / Write / Read / destructor body (`net_messages.cpp`) | scratch copy; Write / Read side by side; registrars ours only |
| 00433BD0, 00433FA0, 00434CE0, 00434D70, 00434E50, 00434AE0, 00436470, 00436620, 00433B10 | `Connection::Tick`, Delivered, Acknowledge, ReadAcks, Accept, ProcessHeaders, ReadUpdate, Close, Silent (idle > 3 round trips) | ours only |
| 00435A00, 00435F30, 004352F0, 00435F10, 00434C20, 00434990, 00433F20, 004346D0, 00434110, 004355B0 / 00435820, 004357C0 | `SendUpdate`, `ReadRecord`, AttachRecord, `IdPool::Allocate`, WriteAcks, QueueHeader, RollBack, CollectChanges, Track, WriteObjectRef / ReadObjectRef, `BufferCursor::PutBit` | ours only; PutBit side by side |
| 00432DA0 .. 004344C0 (11) | `Connection` Construct / Reset / Destruct / Clone, `IdPool::CopyFrom`, replica entry clone / reset / destructors | BuiltCompare / ours only |
| 00437370, 00436CC0 .. 00437340, 00436F60 .. 00436DD0, 00436A50 / 004368D0 | `SessionBase::Receive`, FindClient, RemoveClient, update, leave, the "Can't handle ..." loggers (slots 4, 5, 7-10), Send, RateFor, SendLeave, AddClient (drops an old connection to the address), destructors | ours only |
| 00417EB0, 0041D050 | `ServerCollectReplicated` / `ClientCollectReplicated` (slot 13) | ours into the real vector, original into a scratch |
| 0041ADB0, 004186F0, 00418530, 004193F0, 00418ED0, 00418CC0, 004191C0, 00419D90, 00419A00, 00419570-00419940, 0041A310-0041A770, 0041ACF0 / 0041B720-0041BA30, 00417E10, 00417BC0 / 00417A10, 0041B2A0, 0041B690, 0041AD80, 0041B0C0, 004182E0, 00417E40 | server: join handler, SetMap, QueueMapList, FinishMap, CompleteZone / LeaveZone, AwardHost, QueueAward, ShareFile, script variables (Find / FindOrAdd / Set / Raise / Remove / Get), Announce*, Describe, lookups, Admit, Kick, Ban, departures, trades, SetPublic, destructors, command / object-added / player-changed / player-gone handlers, Tick | ours only; lookups side by side |
| 0041D600, 0041DA50, 00429FC0, 0041E220, 0041DDC0, 0041E180, 0041CCD0, 0041CD80, 0041CA10 / 0041C8B0, 0041D1A0, 0041D260, 0041D420, 0041D410, 0041D9A0, 0041E430-0041F3A0 (29 handlers), 0041E0A0 / 0041E270 / 0041E3E0 | client: server list reply, ClientPublishParty / FindSameMessage / FindParty / ApplyParty / CommitParty, Tick, ApplyAwards, destructors, AddTimed, SendCommand (kept 60 s), Describe, BeginGame, BufferRatio (`double`), message handlers, party-screen trade recording | ours only; FindParty / FindSameMessage side by side |
| 004413E0-00442AD0 (21), 00442C60 .. 00443590, 00443770 .. 00444270, 00440440, 00440690, 004407F0-00440BE0, 00440C60-00440DE0 | lobby (`net_lobby.cpp`), 0x2C-byte query, UDP driver and master TCP link (`net_socket_driver.cpp`) | ours only |
| 00430450, 00430960, 0042FD10, 004301E0 / 00430FE0, 00431040, 00430930 / 00430F60, 00430F90 / 0041D930 / 0041D960, 0042FAE0 | LZ coder, `BufferCursor` PutBits / GetBits / GetBit / WriteBytes / ReadBytes / EndWrite, coder tables | original on a scratch coder; GetBit runs a copy of its 44-byte body; EndWrite ours only |
| 0047D7F0, 0047D970, 0047DA80, 0047DAE0, 0047E190, 0047E240, 0047D910-0047D960 | `BitStream::CompressTo / DecompressTo / WriteStream / ReadStream`, `DeltaField::WriteStreamBoth / ReadStream`, fixed-buffer-view Seek / destructors | deep copies (and a coder copy; the output cursor's pointers into the freed result are excluded) |
| 0047DB70-0047FD80 (~35, `net_delta.cpp`) | delta replication | stream / field copies, globals mapped |
| 00478470-004782E0, 00478960-00478CC0, 00479B10-00479D40, 0047B980-0047BB70, 0047A030, 00477E60, 00479E30 / 0047A080 | FileStream, NetLink base, LoopbackLink, ReplayLink, FramedLink, DriverLink, session pump / start | recorded CFile or copies with their own inbox; Disconnect / Destruct / Reset ours only |

## Compression (LZ coder, 0x0042FCA0-0x00431070)

`CoderState` (0x2828 bytes): Construct 0042FCA0, EncodeMatch 0042FD10, Compress 00430450, Decompress
00430960, cursor helpers 004301E0-00431040. Code tables 0079B6B0 / 0079B738 / 0079B7BC / 0079B844 (filled by
the static initialiser 0042FAE0); bit masks at 0078DB24 are read from the exe. Buffers (`GameBuffer`, 0x1C:
vtable, begin, cur, end, high, base, flags): the growing buffer 0073D624 (constructor 004DA7F0, grows to
(base - begin + cur + n) * 2 + 0x40) and the fixed view 0073CA64 over memory owned elsewhere (slots 0 / 1 / 3
/ 4 004DA580-004DA5B0, 2 the empty 0047D900, 5 deleting destructor 0047D940 (body 0047D960), 6 Seek 0047D930;
base 0073CA48; views live on the stack, 0047D960 only runs while unwinding). A `BufferCursor` (0x18: p,
reserved byte pointer, bit accumulator, bit count, sink buffer, start) is bound by 004DA450 / 004DA490 and
commits its position to the buffer's `cur` at the end; bits go LSB first and a byte is reserved when a field
starts in a new byte, so literal bytes and bit fields interleave.

Format: int32 uncompressed size; per item a 0 bit + literal byte, or a 1 bit, a length (2..0xE1: table
codes up to 0x22, else 0x17/5 bits + 6 bits, 0x3F/6 bits + 7 bits) and an offset - 1 in a 1 KB window
(table codes below 0x22, then prefixes 2/3, 6/3, 3/2 with 6, 8, 10 bits). The compressor hashes
(previous byte << 2) ^ byte into 0x400 chains, takes the first 2-byte match within 0x31E and extends it
greedily. Callers: save write / read, multiplayer map transfer 00662AC0 / 00662DD0, messages (the send
00667810 compresses the client's message stream +0x5C with its coder +0x88; the server flush 0066D100
does the same with +0x48; receivers 00667A30 / 0066D270 decompress with the coder at +0x28B0 into +0x34 /
the global stream 007C83D0), the client 0041D600.
Kept: exactly 8 pending bits are not stored until more arrive; the decompressor writes the window without its
mirror half; length codes above 0xE2 and offsets from 0x562 have unreachable branches; `End` returns EAX as the
original leaves it (the commit's result, or `this` without a sink).
`BitStream::WriteStream` compares size * 2 with 0x100 signed (a stream of 0x40000000 bytes or more gets a
one-byte header); `ReadStream` tests only bit 0 of the first header byte, a length over 100000 returns 0
without reading, a short read returns 0 after `SetSize`.

## Delta replication (0x0047DB70-0x0047FE90, `net_delta.*`)

The server writes every replicated object's dirty fields per player update (0058B750 and the objects'
serialisers: map objects 0050EAF0, units 0052EC70 in `net_state.cpp`, state at unit +0x1C4); the client reads
them back; reached in single player through the loopback.
- `DeltaField` (0xA0): two snapshot streams (+0, +0x28: what channel 0 / 1 last sent) and two output streams
  (+0x50, +0x78). `DeltaState` (at unit +0x1C4): field array +4, count +8, masks +0x14 / +0x16 / +0x18 (u16),
  reader +0x1C, mode +0x20 (0 write all, 1 write dirty, 2 / 3 read against snapshot 0 / 1, 4 idle), header
  streams +0x24 / +0x4C. Globals: 007A9784 / 007A9780 (writing: does channel 0 / 1 keep what it sends as its
  snapshot), 007A9794 / 007A9798 (reading: snapshot and received stream), 007A9788-007A9790 (zeroed by 0047D7D0).
- Format per value: an "all unchanged" bit, else a 0 bit and per 4-byte word an "unchanged" bit or 0 + the
  word, then the remaining bytes raw. Quantised types: 3D position (x, y as (v + 16) * 120.47, z as (v + 32) *
  341.33, 16 bits each), 2D position ((v + 32) * 60.23), ranged ((v - min) * 65535 / (max - min)), 16-bit
  float (sign, exponent - 0x40 in 7 bits, 8 rounded mantissa bits).
- `CEffectShowVisibility` (vision fog spell, effect 0x17) replicates through `WriteStreamBoth` / `ReadStream`
  (slots 00743874 / 00743870); no scenario is known to reach it.
- Floats: `net_delta.cpp` and `clip.cpp` keep `float_control(precise, on)`; values the original rounds through a
  float before FISTP go through `StoreFloat` and `x87::fistp`; FPU copies (a signalling NaN becomes quiet) go
  through `FpuCopyBits`; floats only moved as bytes are declared as uint32.
- Kept: `PackFloat16` (a mantissa rounding past 8 bits wraps to 0 without raising the exponent; exponents
  below 0x40 give exponent 0; above 0xBF wrap in 7 bits); `ReadDelta` / `ReadPosition` store a changed value in
  snapshot[1] even when reading against snapshot 0; `BeginWriteA` / `BeginReadA` return values nobody reads.

## Message links (0x00477E60-0x0047BB80)

`NetLink : CRefObject` (0073C8A4, 0x28, abstract): +8 recording, +9 drop the next recorded message, +0xC the
recording file. Slots 2 reset, 3 clone, 4 connected, 5 alive, 6 has incoming, 7 incoming count, 8 Send (base:
dropped, true), 9 Receive / record, 10 pump (00478010, empty), 11 disconnect, 12 (00478020, returns 0), 13
`FramedLink::SendBytes`. Slots 10 / 12 are shorter than the 5-byte jump and not hooked.
- `LoopbackLink` (0073C8F8, 0x30): +0x28 incoming queue (list of CMemoryStream, vtable 0073C960), +0x2C peer.
  Send puts a copy at the end of the peer's queue; Receive copies the front out and records.
- `ReplayLink` (0073C92C, 0x44): a second file stream at +0x28 plays back a recording (each message a size
  prefix and its bytes); made by 00478960 for `autoplay.apl` or `<name>.apl` (005793E0 from 00602190). Receive does
  not record.
- `FramedLink` (0073C9C8, 0x50, abstract): memory stream at +0x28 as inbox (4-byte length + bytes). Send frames
  the stream in a 0x8000-byte stack buffer into slot 13 (disconnects through slot 11 when that fails); Receive
  takes the first message and moves the rest down. `DriverLink` (0073C990, 0x54): a framed link over a
  `Connection` (+0x50): Pump 004797A0 moves the connection's incoming reliable bytes (+0x1148) to the inbox,
  slot 13 004796B0 appends to its outgoing reliable stream (+0x112C), Disconnect 00479810 writes unread inbox
  bytes back over the incoming stream. Drivers exist only for remote players (`DriverSession` lists +8 owning /
  +0xC holders; Pump 00479E30, Start 0047A080).
- 0073CA00 / 0073CA0C / 0073CA10: a dead framed-link family (0047C8E0-0047D7B0 `CTcpNetDriver` code,
  "CTcpNetDriver::AcceptNewClient()": nothing live installs it).
- Kept: link constructors / clones write the new object's count before the null check; `Open` switches
  recording on even when the file did not open; `Reset` keeps +9 and the count; the driver's clone reads
  into the source's buffer where a write was meant; the link base's destructor body does not set its own vtable.

## Original bugs kept (rest)

`SendUpdate`: an entry that overflows the budget stays in the packet uncounted, the rest of that priority waits,
lower priorities are still tried. `ReadRecord`'s skip path creates a throwaway object without a null check. The
player record and `Track`'s entry write their count before the null check; `Receive` and `Tick` set and clear the
current session / connection globals without the SEH frames that would reset them. `Connection::Construct`
squares the uninitialised round-trip time. `PartyMemberMessage::Clone` (00417420) reads the copy's
uninitialised bytes into the source's buffer instead of writing the source into the copy.
`ClientApplyParty` creates the slot 22 object of a member with a positive id and does not keep it;
`0041E850` reads the zone name after releasing its own reference; `0041E270` / the message factories write the
count before the null check; the handler registration templates read the prototype's vtable before any null
check. `QueueAward`, `FindOrAddVariable` assign strings in place; `LeaveZone` / `FinishMap` free queued
messages inline.

## Not rewritten / skipped

- Rebirth-patched: 0041ECC0 (file chunk handler; `MQClient`), 00443080 (`FixProcessLogin`), 0041AA10 (master
  announce, patched at a call site), 0041ACA0, 0041B1C0 (chat handler, call site patched).
- Shorter than 5 bytes: 0041BA20 (a 0073BD78 handler), the 1-4 byte slots of the range.
- Static initialisers: 0047C300 (WSAStartup(0x101) + atexit 0047C330: waits while 007A9774 is nonzero, then
  WSACleanup; live only at exit), 0047D7D0.
- Library classification: ~270 STLport / MFC instantiations of 0x400000-0x440000 and ~160 of 0x440000-0x480000
  (config/library.csv, notes "range 400 / 440 / 470 review").
- Dead (config/dead_code.csv): 0041A230, 0041A2B0, 0041AD40, 0041E1A0, 0041E1E0, 0047BC10 (a 0x2014-byte stack
  frame, timeGetTime), 0047BE80, 0047C070, 0047C0F0, 0047C360 (installs 0073CA00), 0047C4B0, 0047C550, 0047D190,
  0047D350, 0047D380 and the 0073CA10 slots.
- Exception funclet targets and compiler-generated copy constructors of exception classes (0040C510, 0040C540,
  0040C570, 00429130, 0043AD60) are reached only through C++ exception tables.
- 0047A220 (0073F3B8, the session's Query: 3 bytes, cannot be hooked).

## A-E1: the leftovers of the delta code and the links

- `DeltaState::Finish` (0047FE90, net_delta.cpp): ends a write or a read. After a delta write the written mask
  joins `sentMask_` and becomes `lastMask_`, and both headers are rebuilt (the mask as 2 bytes, then each field's
  output of that channel, byte aligned). After a full write the headers and the three masks are emptied. Main
  thread (the world's update writes). Verified side by side on the state itself (`deltasnap::StateCopy` before,
  the original run, its result copied, the first copy put back, ours run); the wrapper copies every field's
  streams twice per call, which costs time in a verify run with many objects. Reached by every scenario that
  runs a server world (each object's state write ends with it).
- `DeltaField::ReadText` (0047E340): `ReadString` into a caller's buffer; a string of `size - 1` characters or
  more leaves the buffer empty. Side by side on a copy of the reader, the texts compared by content. The unit
  and effect readers of the client call it (a multiplayer or single-player client update with a named object).
- The forwarders other files keep for these (`DeltaStateFinishOriginal` in originals_engine.h, `ReadText` in
  world_effect_internal.h) reach the hooked code.
- The player sources' slot 6 (`TakeNew`, player_sources.cpp; the server world's AcceptNewPlayers 0058DAF0 calls
  it with an MFC list of counted links, main thread). `CWorldHandle::TakeNew` (00478880): the pending link is
  appended to the list (one more reference) and the handle drops its own. `DriverSession::TakeNew` (0047A230):
  the link of every holder that is not detached is appended, then the holders list is emptied (each reference
  dropped, the nodes pushed on the allocator's 12-byte list, the sentinel linked to itself). Both side by side:
  the original on a copy of the source (its references added) with an empty list of the same kind, ours on the
  real one; the lists' elements, the emptied source and the link's count compared. Registered through shims (a
  pointer to a virtual member would dispatch). By reading, the handle's is reached when a server world takes
  its local player (a new game); the session's only on a network server.

## A-E1: the server's master announcement and two handlers (net_server_handlers.cpp)

All on the main thread (the session's tick from `net::Pump`); none is called from a library's thread.

- `ServerSession::AnnounceToMaster` (0041AA10; the tick calls it through its address every announcement
  interval while a master host name is set): a `GameInfoRecord` on the stack (name, the played map's title
  from 005814C0 or "" without a map, players, maximum, kind, password flag, +0x25FC, the dword the master
  last sent, the session magic) written into a growing buffer and sent from the session's socket to the
  master's address. Ours only (it sends a datagram).
- `ServerSession::OnMaster` (0041ACA0, slot 10): a kind -1 packet from the master's address (16 bytes
  compared) whose first dword equals +0x25FC stores its second dword at +0x2578; the first dword is consumed
  either way. Side by side (the kept dword and the cursor put back between the runs).
- `ServerOnSay` (0041B1C0, ECX the server, EDX the SayMessage; registered by the server's constructor): the
  first binding of the connection being read (0079B924) names the player; "^<number><name>^7: <text>" is
  announced to everybody (`Announce` with no player). Nothing without a live connection or binding. Ours
  only.
- Rebirth: EI_Plugin's NewMaster has sites in 0041AA10 (0041ABDA, the send) and 0041ACA0 (0041ACB7), addon's
  ChatLog one in 0041B1C0 (0041B25B, disabled in this install). The rewrites are the vanilla functions; the
  installer does not hook a function with an active site. No scenario reaches any of the three (a master
  server, or a second client's chat line, is needed).
- A-E2 (vanilla bodies, vanilla launch only; ours only, no scenario reaches them):
  - 0041ECC0 `ClientOnFileChunk` (`net_client_handlers.cpp`; Rebirth's MQClient): collects a file's chunks,
    sorts and joins them and hands the file to the game flow (005813A0).
  - 00443080 `ServerBase::OnLogin` (`net_lobby.cpp`; Rebirth's FixProcessLogin): a wrong magic is ignored; the
    rate is cut to the address's limit; a connected client's login is ignored ("ERROR: Ignoring login..."); a
    pending join is accepted again (a new id replaces the old with "WARNING: LinkID mismatch"); otherwise
    `Admit` decides: accepted (a pending join appended, packet 6 with the id) or refused (packet 7 with the id
    and the reason). The original leaves the new join's age as stack garbage (Rebirth's patch clears it); ours
    writes 0.
