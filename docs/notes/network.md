# Network

Files: `net_classes.h` (sessions, lobby, message base), `net_session*.cpp`, `net_connection*`, `net_messages.*`,
`net_server_*.cpp`, `net_client_*.cpp`, `net_lobby.cpp`, `net_socket_driver.cpp`, `net_coder.*`, `game_buffer.*`,
`net_delta.*`, `net_link.*`, `net_state.cpp`, `net_second_master.*`. Sockets are the platform layer's
(`platform::net`).

## What runs when

- Every frame `net::Pump` (a gap over 5 s counts as 1 s) ticks the game server and client sessions (null in single
  player), then the game flow's network step.
- Single player still uses the message code: every new game and load creates two joined loopback links (the server
  side's player link and the client's), so every message goes through Send / Receive, the LZ coder (every frame),
  `BitStream::WriteStream` / `ReadStream` and the delta writers. Saves go through `FileStream` and the coder (write
  compresses, decompresses the result and compares; a checksum above save version 0x6B).
- Multiplayer: the server / client sessions, the lobby, the master server, `DriverLink`s (remote players only), trades,
  kick / ban, zone commands, awards, shared files. Scenarios: `net_lobby` (the host's client joins its own server over
  loopback), the pairs (`peer` in harness.md): `net_pair_join` (find in the lobby, join, chat, leave), `net_pair_zone`
  (an exchange, the map choice, both into the zone), `net_pair_kick` (kick by serial, ban by name, a refused join),
  `net_pair_leave` (shared files, a peer lost in the zone, LeaveZone), `net_pair_quest` (a quest done, CompleteZone and
  its awards, a fake master), `net_pair_master` (the master's server list, a bad update), `net_second_master`;
  `net_host_death` (one hosted game: its hero killed in the game zone, the zone saved and entered again).

## The multiplayer game

- **The base** (bz1mpg for game type 0) runs locally in each game; the other players are faces in the briefing
  screen's chat window, not units.
- **Maps on offer.** FlowPickZones picks one zone per map group (zone3Obr, zone6, zone7, zone9 for bz1mpg) with one
  IsaacRandom draw each, so the offer follows the game's draws before the server starts; after LeaveZone the next
  offer leaves out the zone just played. The server queues a Zone message "<zone>_1" for each; a client marks the
  briefing "b.<speaker>.<zone>_1", so the zones are the speaker's topics (Nsheriff; a client's topics sorted by
  `_stricmp`). Finishing one sends command 1 "<zone>_1"; the host's server then sets the map ("<zone>_2" queued, the
  Map message, the network zone).
- **Into the zone.** A client may leave the base while "z.MPGame1" is not 1; the leader walking into exit 1's remove
  rectangle (bz1mpg: 56 39 62.5 45) makes the briefing screen leave. The host: state 6 and its Host message to the
  clients. A joiner: state 5, a DriverSession link over its session connection to the host's world (the Host
  message's "host:8000" is not used).
- **Back to the base**: the zone's exit 1 asks the leave box; the client goes to state 1 and the start zone; the host
  also sends command 9 with the announced zone and drops the Host message. The reject topic sends "<zone>_2":
  LeaveZone (text 6, the final award for the "<zone>_1" briefing's give-items, the next zones queued and their .mq
  files shared, the world's map cleared). CompleteZone ("<zone>_3") needs the zone's quest done (QuestComplete ->
  FinishQuestMap). Awards wait 60 s on the server's timed list before the Award message goes out.
- **Exchange**: a click on another player's face sends a Party offer; the partner's click on the offerer's face opens
  the exchange on both; both accepting completes the Trade.
- **Kick / ban**: console `kick` / `ban` without a name opens the host's player pick (a face click sends command 4 / 5
  with the serial); with a name, command 2 / 3 (guests whose name starts with it). A banned address's join is refused.
- **Shared files**: the server shares each queued zone's .mq (a File message, 64-byte FileChunk messages); a client
  that lacks one answers with a FileRequest; vanilla writes the file into Maps, Rebirth's MQClient keeps the chunks
  only.
- **A lost client**: the server drops it after the session timeout (60 s; text 4 to both); its own game shows the
  lost server box and returns to the main menu.
- The console's "@" commands do nothing in a network game (the harness's `hit` step acts on the world instead).
- **A hero's death** (PlayerServer::OnLeaderDied, the multiplayer branch: a party of one hero): StopSpeech to its
  client, the money times `lostMoney` and all the player's items left in a shared object (WorldServer::AddSharedObject,
  saved with the zone and read back by SharedObject::Load when the zone is entered again), the party collected,
  experience times `lostXp` taken, and the hero redeployed under a new id (below 1001000000 moved up). The host's own
  hero dies the same way: the host is a network client too.
- Original bug: a guest joining while the host's briefing dialogue runs crashes the host in TextManager::PeekWord
  (NextPhrase -> FindLine).

## Sessions and messages

- `SessionBase : RefCounted`: frame, time (a double holding float sums), the connections, the type registry (message
  class to type id to factory), the UDP socket driver, one "reliable" byte per message type, handler vectors per
  channel (0 created, 1 changed, 2 removed) and type. `ServerBase` / `ClientBase` are the lobby layers;
  `ServerSession` / `ClientSession` on top. +8 of both sessions is the magic 0xDEC0AD07.
- Packet kinds (first byte): -1 master, 0 update, 1 leave, 2 none, 3 get info, 4 login, 5 info, 6 accepted, 7
  rejected.
- Message types: client to server: Login (5), Say (6), Command (7, {kind, text}), FileRequest (13), Party (15, a trade
  offer), PartyMember (16, a MemoryStream); server to client: Player (1), Line (2, chat), Host (3, "host:port" + zone),
  Binding (4), Zone (8), Map (9), Variable (10), File (11), FileChunk (12), Award (14), Trade (17). Duplicate layouts
  share a class: `TextMessage` (Say / Zone / Map), `PairMessage` (Binding / FileRequest). Each class has its factory,
  reset, clone, Write and Read; a reset ends with the inlined placement new's null test of `this` in the original,
  which ours leaves out.
- Command kinds (host only): 0 preview a map, 1 zone command ("_1" enter, "_2" leave, "_3" finish), 2 / 3 kick / ban
  by name, 4 / 5 by serial.
- Rates: 12500 local / 2500 otherwise, clamped 500..50000. Say is announced as "^<number><name>^7: <text>".
- **Ports.** One UDP port, 8888, for the server's socket (a failed bind leaves the session without a socket), the
  local client's join of 127.0.0.1 and the lobby's pings. A second game on the machine joins the first one's server;
  harness games each get a port of their own, a pair a network of virtual ports (`tests/verify/harness_net.h`,
  `harness_pair.h`).
- **Lobby.** The client base keeps the server list, direct queries, join request / retry and pings; the server base
  answers pings and accepts or refuses joins (`Admit`: ban list, 20-byte password, player limit); a login with the
  wrong magic is ignored, a connected client's login too, a pending join is accepted again. An info packet is {echoed
  ping time, magic, entry}: the entry is foreign (Connect shows the wrong-version box) when the magic is not exactly
  the client's; the echoed time only gives the ping.
- **Master server**: port 28004, a TCP link; the server announces a `GameInfoRecord` (name, map title, players,
  maximum, kind, password flag, the master key, the master's last token, the magic) every interval while a master host
  is set; the master's reply is a compressed run of {address, GameInfoRecord}. EI_Plugin's second master
  (net_second_master.h; the setting `PluginSecondMaster`, empty by default) is used at the plugin's three call sites:
  the main menu's Multiplayer button, the announcement and the connect screen's master request; with the setting
  empty nothing changes.

## Replication (net_connection.h)

A connection to one peer tracks every replicated object (a Message, whose version changes with it) with a
`ReplicaEntry` in one of 11 priority maps. Each update packet carries, highest priority first, the entries whose
version differs from the one last sent (a 1 bit, type, id, the object's Write) or whose object went away (a 0 bit,
type, id). Every packet is remembered in the send history with what it carried, so an acknowledgement or its absence
marks entries delivered or resends them. A reliable byte stream travels in front of the entries in chunks of up to 255
bytes. The peer keeps received objects in `records` (key type << 16 | id). A connection is silent after 3 round trips
idle.
- Within a priority the original sent entries in the order of their addresses, so send order and net ids followed the
  heap. Ours keys the priority maps by a serial handed out as `CollectChanges` walks the session's lists (players,
  variables, the binding, chat lines, files), so they follow the lists; the goldens hold no net ids or send order.
- The client's player list (`ChatWindow::ShowPlayers`) lists nobody while the binding is missing, and the original's
  binding handler did not refresh it (players read before the binding left the base without faces); ours refreshes it
  at the binding.
- Kept: `SendUpdate`'s entry that overflows the budget stays in the packet uncounted, the rest of that priority
  waits, lower priorities are still tried. `ReadRecord`'s skip path creates a throwaway object. `ClientApplyParty`
  creates and drops a member's object with a positive id.

## Compression

The LZ coder (`CoderState`, net_coder.h) over `GameBuffer`s: the growing buffer (grows to (used + n) * 2 + 0x40, new
bytes zero) and fixed views over memory owned elsewhere. A `BufferCursor` writes bits LSB first and reserves a byte
when a field starts in a new byte, so literal bytes and bit fields interleave.
- Format: int32 uncompressed size; per item a 0 bit + a literal byte, or a 1 bit, a length (2..0xE1: table codes up to
  0x22, else 0x17/5 bits + 6 bits, 0x3F/6 bits + 7 bits) and an offset - 1 in a 1 KB window (table codes below 0x22,
  then prefixes 2/3, 6/3, 3/2 with 6, 8, 10 bits). The compressor hashes (previous byte << 2) ^ byte into 0x400
  chains, takes the first 2-byte match within 0x31E and extends it greedily.
- Users: saves, the multiplayer map transfer, the message streams of both sessions, the master's list.
- Kept: exactly 8 pending bits are not stored until more arrive; the decompressor writes the window without its mirror
  half. `BitStream::WriteStream` compares size * 2 with 0x100 signed (0x40000000 bytes or more get a one-byte header);
  `ReadStream` tests only bit 0 of the first header byte, a length over 100000 returns 0 without reading.

## Delta replication (net_delta.*)

The server writes every replicated object's dirty fields per player update (map objects, units in net_state.cpp); the
client reads them back; single player too, through the loopback.
- `DeltaField`: two snapshot streams (what channel 0 / 1 last sent) and two output streams. `DeltaState`: the fields,
  three 16-bit masks (sent, last, written), the reader, the mode (0 write all, 1 write dirty, 2 / 3 read against
  snapshot 0 / 1, 4 idle), two header streams. `Finish` after a delta write joins the written mask into the sent one
  and rebuilds both headers (the mask as 2 bytes, then each field's output of that channel, byte aligned).
- Format per value: an "all unchanged" bit, else a 0 bit and per 4-byte word an "unchanged" bit or 0 + the word, then
  the remaining bytes raw. Quantised: 3D position (x, y as (v + 16) * 120.47, z as (v + 32) * 341.33, 16 bits each),
  2D position ((v + 32) * 60.23), ranged ((v - min) * 65535 / (max - min)), 16-bit float (sign, exponent - 0x40 in 7
  bits, 8 rounded mantissa bits).
- Kept: `PackFloat16`'s mantissa rounding past 8 bits wraps to 0 without raising the exponent; exponents below 0x40
  give 0, above 0xBF wrap in 7 bits; `ReadDelta` / `ReadPosition` store a changed value in snapshot 1 even when
  reading against snapshot 0; `ReadText` leaves the buffer empty for a string of `size - 1` characters or more.

## Message links (net_link.h)

- `FileStream`: a ByteStream over a `platform::File` (saves, the connect screen's address book).
- `NetLink` (abstract): connected, alive, has incoming, Send, Receive, disconnect. `LoopbackLink`: two joined to each
  other, the single-player link; Send puts a copy at the end of the peer's queue, Receive takes the front.
  `FramedLink` (abstract): the received bytes in a memory stream, each message a 4-byte length and its bytes; Send
  frames the message (a 0x8000-byte buffer). `DriverLink`: a framed link over a connection's reliable streams; its
  disconnect writes unread bytes back over the incoming stream. `DriverSession` keeps a driver link per remote player.
  Links are created with `net::NewCounted`.
- The player sources hand new links over as `JoinedLinks` (`std::list<netlink::NetLink*>`, TakeNew).
- Kept: the driver's clone reads into the source's buffer where a write was meant.
