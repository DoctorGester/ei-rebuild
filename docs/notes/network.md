# Network

Files: `net/net_session.h` (sessions, lobby, message base), `net_session*.cpp`, `net_connection*`, `net_messages.*`,
`net_server_*.cpp`, `net_client_*.cpp`, `net_lobby.cpp`, `net_socket_driver.cpp`, `core/coder.*`, `core/buffer.cpp`,
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
  loopback), `net_second_master`, the hosted games and the pairs of standalone-verification.md section 3 (`peer` in
  harness.md), `net_pair_companion` ("Companions" below), `net_pair_mod_*` ("Mods" below) and `net_pair_mod_effect` (a
  mod's data effect started by the host's server, shown on the peer; effects.md, "Data effects").

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
- **The host's choices** (the create-server screen: ui.md, "MpConnectScreen::CreateScreen"): the player limit (1 to
  `net::kMostPlayers`, 14; 6 by default) and the pause mode (`net::PauseMode`: Original, Announced, AnyPlayer, Off;
  rebirth.md, "EI_Plugin.asi"), both on the ServerSession (StartServer). The limit refuses joins (`Admit`) and is in the
  lobby's answer and the master's record; the session's messages do not carry it, so a joiner keeps the joined entry's
  `maxPlayers` as it keeps its game type (`ClientSession::maxPlayers`, `PlayerLimit`: the HUD portraits and the chat
  strip are laid out for it). The pause mode stays on the server (`PlayerServer::OnPause`, `World::SetServerPaused`).
  Scenarios: `net_pair_portraits` (limit 8 on both games, the peer pausing under AnyPlayer), `net_host_pause` (Off).
- **Kick / ban**: console `kick` / `ban` without a name opens the host's player pick (a face click sends KickBySerial /
  BanBySerial with the serial); with a name, KickByName / BanByName (guests whose name starts with it). A banned
  address's join is refused.
- **Shared files**: the server shares each queued zone's .mq (a File message, 64-byte FileChunk messages); a client
  that lacks one answers with a FileRequest and keeps the chunks without writing the file (Rebirth's MQClient, always
  on).
- **A lost client**: the server drops it after the session timeout (60 s; text 4 to both); its own game shows the
  lost server box and returns to the main menu.
- The console's "@" commands do nothing in a network game (the harness's `hit` step acts on the world instead).
- **A hero's death** (PlayerServer::LeaderDied, the multiplayer branch; the hero is the party's first record): StopSpeech
  to its client, the money times `lostMoney` and all the player's items left in a shared object
  (WorldServer::AddSharedObject, saved with the zone and read back by SharedObject::Load when the zone is entered again),
  the party collected, experience times `lostXp` (of what is above the class's floor) taken, and the hero redeployed
  under a new id (below 1001000000 moved up). The host's own hero dies the same way: the host is a network client too.
- Original bug: a guest joining while the host's briefing dialogue runs crashes the host in TextManager::PeekWord
  (NextPhrase -> FindLine).

## Companions (ours)

A player may take its other saved heroes (the .mp files of the hero list) into a network game as companions: one hero
to start; the list is picked in the multiplayer base (the briefing screen's companions button or F3, ui.md "Companions
window") and only there: the base is a local zone served by the player's own game, with no server link, where the
picked heroes are party members; a network zone only asks for the picked list again. Scenario `net_pair_companion`.
- **The host's option**: the create-server screen's region 30 ("Companions: off / two", `net::kMostCompanions`) into
  `ServerSession::companions`. Off is the default and changes nothing: no message below is sent.
- **Who knows it**: our client writes `kCompanionLoginMark` into its world login's password field past the
  terminating zero (`kCompanionMarkAt`), where an original server reads nothing. Our server answers a marked login in a
  network zone with `ServerMessage::Companions` (0x84, the limit) when the option is on; the client keeps it in
  `PlayerClient::companionLimit` (zeroed with each new connection) and only then sends `ClientMessage::AddCompanion`
  (0x51) for its picked list. An original server never sends 0x84, so our client never sends 0x51 to it; an
  original client never marks its login, so it never gets 0x84. **A mixed game**: original clients play one hero as
  before and see the others' companions as units of those players (the party state's other units); our clients add
  companions.
- **The join** (`PlayerServer::OnAddCompanion`): a dword (1: the client's list asked for again at a network zone's
  start; 0: a pick in the base), then a sized block, the hero's party saved without variables. A network zone takes
  only the 1 (a 0 is read and dropped, logged: nothing there changes the list; the harness's `askcompanion` sends one)
  within the host's option; the base (`InMultiplayerBase`: a multiplayer client's own game playing a zone of kind
  Base) takes either within `net::BaseCompanionLimit` (the host's option, `kMostCompanions` in a joined game). Taken
  when the player is joined, its hero lives, the party has room (1 + companions) and the block is one record of known
  items and spells; else read and dropped (logged). The record is appended (`PartyUnitRecord::CopyFrom`: every field,
  the items copied into the player's party), gets the next free object id (as OnNewParty's), follows the hero
  (`leaderId`), and its unit is made beside the hero (`WorldServer::CreateUnits`, a Player mode, the Heroes group); the
  party is sent. Orders need nothing new: the walk / attack / cast / use handlers take any unit id with a Player mode
  (they do not check the owner).
- **In the base**: picked in the window, the hero joins at once (the window sends the 0x51 with 0 to the base's
  server). Entering the base (each party message's `GameFlow::ReturnLocalParty`, below) the party records the base
  deploys are the hero and a copy of each picked companion's record from its file (`SavedParties::JoinCompanions`,
  unit ids following the hero's), so the base's login deploys them all. The camp then lists and
  edits them like any party member (equipment, spells and skills, the trader, the hero arrows); closing it rejoins
  (`PlayerClient::Rejoin`), and its `ReturnLocalParty` writes each companion's record into its file and the hero
  (with the backpack and the money: the hero's) alone into the hero's, so an item moved between them lands in the
  right file. The base's gate waits for the hero only (`BriefingScreen::LeaveIfExitOpen`, unchanged); leaving
  (`GameFlow::SwitchMultiplayerZone`) hands the companions' records home again and takes the hero alone to the network
  zone, whose client asks for them anew. A joined game's list trimmed to its host's limit there comes back trimmed.
- **Sent away**: in the base only, on the client: the window hands the companion's record from the local party back
  to its file (`SavedParties::ReturnCompanion`), takes it off `SavedParties::companions` and sends
  `ClientMessage::RemoveCompanion` (0x52, its unit id) to the base's server, which drops its record and its unit
  (`WorldServer::RemoveObject`) and sends the party. A network zone reads and drops a 0x52 (logged); the zone's
  records go home with the party.
- **Death**: a companion's death (`PlayerServer::CompanionDied`, from the unit's death report when it is not the
  leader) takes the hero's experience penalty at once, on the unit and on its record; it lies dead, nothing dropped
  (money and backpack are the hero's). The hero's death keeps the rule above for the hero; the living companions are
  synced (`WorldServer::SyncCompanions`: records from the units, then whole) and the whole party comes back together at
  the zone's entrance, a dead companion standing again with the penalty it already paid.
- **Back to the files**: at the zone's end (`LeaveToZone`, `Leave`) the living companions are synced and the party is
  sent; the client's `GameFlow::ReturnLocalParty` hands each record after the first to its hero's file
  (`SavedParties::ReturnCompanions`: the first companion file asked for whose hero has the record's name and
  prototype; the entry's record becomes a copy, the file written) and keeps the hero alone in its own file; the party
  records take the companions again from their files (above). Every party message does this (the add's answer, a
  death's resend, the base's login and each camp's rejoin), so a lost game keeps the last one.
- **The whole game**: `SavedParties::companions` (the files picked in the base) lasts until they are sent away or a
  multiplayer game begins (leaving a game and joining or hosting another). In each network zone of the game the 0x84
  answer sets `PlayerClient::restoreCompanions`; once the hero's unit stands (the in-zone tick, `RestoreCompanions`)
  every file is asked for (restore dword 1; files past this host's limit dropped). A joined game does not know the
  limit in the base, so its list may be longer than the host takes. A regroup after the hero's death keeps them
  (above). The leave box waits for every living unit of the party in the exit, the companions too
  (`WorldClient::PartyInExit`); the exits stay closed while another player is in the game
  (`GameFlow::UpdateMultiplayerVariable`: "z.MPGame1" at 1 while the server counts more than one). A hero cannot be two at once: the window offers only heroes that are neither the played one nor a companion.
- **Selection**: the HUD's member keys toggle the hero and its companions only (`ToggleMember`), and a drag selects
  them as in a single-player game (ours, MP1c; `BattleScreen::BoxMembers`, ui.md "Box select in a network game"):
  never another player's unit, which the server would take orders for (its handlers do not check the owner). An order
  with several selected sends one message naming them all (Walk, GoTo, Attack, a dead unit's use) or one per unit
  (each cast, Follow).
- **Experience and awards**: a kill's experience (`ShareAmongSoloPlayers`, the multiplayer share) is split per player
  first, as the original did among players of one hero: each player whose hero stands gets a part in proportion to the
  square root of its hero's experience; the player's heroes (the hero and its companions) then share that part in
  proportion to the square roots of theirs. A hero alone takes its whole part (the weight over itself is exactly 1), so
  games without companions share as the original; companions do not take experience from the other players. Quest
  experience, gold and items come through the Award message 60 s after the zone, when the companions are back in their
  files: they go to the hero and the party.

## Mods (ours)

Peers learn each other's mod from the lobby's info answer and at the join: the mod's title and its content
hash (`modidentity::Identity`, src/game/net/mod_identity.h; the hash: mods.md, "Mods in multiplayer"). Scenarios
`net_pair_mod_same`, `net_pair_mod_differ`, `net_pair_mod_none` and `net_pair_mod_list`.
- **The mark** (`net::WriteModMark` / `ReadModMark`, net_lobby.cpp): `kModMark` "ei-rebuild mod" with its zero, a
  format byte (1), the hash's low and high dwords, the title's length (a byte) and its UTF-8 bytes. Our client writes it
  after the login's 20-byte password (`JoinWithPassword`); our server after the info answer's entry (`SendInfo`), an
  acceptance's join id (`SendAccepted`) and a refusal's reason when the reason is `ModMismatch` (4). An original peer
  reads none of it: its server reads the password's 20 bytes at most, its client the entry, the id and the reason
  byte. No mark (the packet ends before it) means the original game, which counts as playing no mod.
- **The server** (`ServerSession::Admit`, after the ban check, before the password and the limit): a client whose mod
  is not the server's (`modidentity::Agrees`: the same title and hash) is refused with `ModMismatch` and the server's
  mark. Each refused join (address and join id: a client asks again until answered) is logged ("mod: refused the join
  of ..."), counted (`modRefusals`, `modRefused`) and named in a chat line to every player (`RefusedLine`, text
  `lmp_mod_refused`).
- **The client**: a ModMismatch refusal keeps the host's mark (`ClientBase::hostMod`); the connection screen's refusal
  box then names both mods (`modidentity::RefusalBox`: `lmp_mod_mismatch` with `lmp_mod_mismatch_msg`, or
  `lmp_mod_version_msg` for the same title with other files). An acceptance is read for the mark too: a host whose mod
  is not this game's, or a host without the mark (the original game) while this game plays a mod, is not joined
  (`OnAccepted`: refusal ModMismatch, the box `lmp_mod_original_msg`); the server's pending join then times out.
- **Original clients on a server that plays a mod are refused.** They send no mark, so the server cannot tell whether
  they play the same mod; the formats our mods use (TOML records, PNG, glTF, Lua, loose files, zips) are not readable
  by the original game at all, and a mismatch shows inside the zone as a fatal error (an unknown particle or record) or
  a game drifting apart. Refused at the door, the original client shows its box for an unknown reason
  (`lmp_cannot_connect`). A Rebirth player of the original exe with the same classic mod (Lost in Astral) is refused
  as well; a host's option to admit unmarked clients would be the way back if wanted. A server without a mod admits
  them as before, and our client without a mod joins an original server as before.
- **The list**: the client reads the mark after an info answer's entry (`ClientBase::OnInfo`: `ServerEntry::mod`,
  `modRead`; an original server's answer has none, the original game). The connection screen names the selected
  host's mod in its details and dims the rows of hosts this game cannot join for the mods (`ServerEntry::ModAgrees`,
  `modidentity::Listed`: `lmp_mod_listed`, `lmp_mod_cannot_join`, `lmp_mod_cannot_join_version`; ui.md, "MpConnectScreen::DrawListVanilla").
  Entries without an answer read (the master's list, the address book's saved replies) show no mod and count as
  joinable. Joining such a host is still tried and
  refused as above: the list only shows it. A switch to the host's mod from the list (when it is in the menu's list) is
  not offered: mods switch only in the main menu (`app::RequestModSwitch`).
- **The texts** are the game's when its texts have the key (a texts.toml of a mod or a converted install can carry
  `string lmp_mod_mismatch` and the others), else ours in Russian where the game's texts are, otherwise English.
- `net::g_originalPeer` (the harness's `originalnet`): this game's lobby acts as the original's (no mark sent or read,
  any mod admitted), so the scenarios meet an original peer.

## Sessions and messages

- `SessionBase : RefCounted`: frame, time (a double holding float sums), the connections and the UDP socket driver.
  A connection hands each received message to its session's `OnMessage` (the connection, a `MessageEvent`: Created,
  Changed or Removed, the message), one switch on the message's `Type()` per session. `ServerBase` / `ClientBase` are
  the lobby layers; `ServerSession` / `ClientSession` on top. Both sessions hold the magic 0xDEC0AD07.
- Packet kinds (first byte, `net::PacketKind`): Master (0xFF, -1), Update 0, Leave 1, (2 none), GetInfo 3, Login 4,
  Info 5, Accepted 6, Rejected 7 (the join id and a `RefusalReason`: Full 1, Banned 2, WrongPassword 3; ours
  ModMismatch 4, "Mods"). The other numbers of the wire are enums too, in `net/net_session.h`: `AwardKind`,
  `CommandKind`, `PlayerStatus` (the state of a client's login record: InZone 0, InBase 1, InCamp 2, Connecting 3, Fresh
  4); the player link's message types are `ServerMessage` / `ClientMessage` (`player_messages.h`), the connection states
  `PlayerServer::State` / `PlayerClient::State`, a map object's delta fields `NetField` (`net_delta.h`).
- Message types: client to server: Login (5), Say (6), Command (7, {kind, text}), FileRequest (13), Party (15, a trade
  offer), PartyMember (16, a MemoryStream); server to client: Player (1), Line (2, chat), Host (3, "host:port" + zone),
  Binding (4), Zone (8), Map (9), Variable (10), File (11), FileChunk (12), Award (14), Trade (17). Duplicate layouts
  share a class: `TextMessage` (Say / Zone / Map); Binding and FileRequest hold their typed pair. Each class has its `kType`
  (`MessageType`, the wire's type byte) and `Type()`, `OnShutdown`, Write and Read (object references go through the
  connection passed in); `CreateMessage` makes one by type. `OnShutdown` (a still-shared message losing its owner)
  gives every field a constructor sets its new value (version 1, references dropped); the fields a constructor leaves
  unset keep theirs, as the original's destroy-and-construct-in-place left them. Only the messages a second share of an
  object clones (Say, Command, FileChunk, Award, ServerEntry) have `Clone` and a copy constructor; `RefObject::Clone` of
  any other class is a fatal error, and `OnShutdown` is empty unless a class overrides it.
- Command kinds (host only, `CommandKind`): PreviewMap, Zone ("_1" enter, "_2" leave, "_3" finish), KickByName /
  BanByName, KickBySerial / BanBySerial.
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
  (net_second_master.h; the setting `[network] SecondMaster`, empty by default) is used at the plugin's three call sites:
  the main menu's Multiplayer button, the announcement and the connect screen's master request; with the setting
  empty nothing changes.

## Replication (net_connection.h)

A connection to one peer tracks every replicated object (a Message, whose version changes with it) with a
`ReplicaEntry` in one of 11 priority maps. Each update packet carries, highest priority first, the entries whose
version differs from the one last sent (a 1 bit, type, id, the object's Write) or whose object went away (a 0 bit,
type, id). Every packet is remembered in the send history with what it carried, so an acknowledgement or its absence
marks entries delivered or resends them. A reliable byte stream travels in front of the entries in chunks of up to 255
bytes; the entries themselves are never resent once delivered or superseded. The peer keeps received objects in `records` (`RecordKey`: type
<< 16 | id); an object reference in a message is a flag bit (1: an object the writer received from the reader), the
type and the id. A connection holds its session (`session`, from AddClient: the socket it sends on, the OnMessage and
CollectReplicated it calls). Its timings are constants (`kPeerTimeout` 60 s, `kMaxRoundTrip` 5 s, `kIdReuseTime`
75 s). A connection is silent after 3 round trips idle. `Connection::OnShutdown` (the session dropped a
connection a message still holds) restores a new connection's state (`ConnectionState`, id 0).
- Within a priority the original sent entries in the order of their addresses, so send order and net ids followed the
  heap. Ours keys the priority maps by a serial handed out as `CollectChanges` walks the session's lists (players,
  variables, the binding, chat lines, files), so they follow the lists; the goldens hold no net ids or send order.
- The client's player list (`ChatWindow::ShowPlayers`) lists nobody while the binding is missing, so it is refreshed
  at the binding too (the original's was not: players read before the binding left the base without faces).
- Kept: `SendUpdate`'s entry that overflows the budget stays in the packet uncounted, the rest of that priority
  waits, lower priorities are still tried. `ReadRecord`'s skip path creates a throwaway object. `ClientSession::ApplyParty`
  creates and drops a member's object with a positive id.
- Undefined in the original, defined in ours: a new connection's `roundTripSquare` starts at 0 (the original squared
  what the memory held); the original found a message's type by its vtable and an unknown one read `reliable[-1]`,
  ours asks the message (`Type()`); a kick or ban inside an update left the original's current-connection global
  null for the update's later entries (an object reference there read through null), ours passes the connection;
  every read through a `BufferCursor` stops at its `limit` (`BeginRead`'s size: a datagram's end, a compressed
  stream's end): bytes and bits past it read as zero and a text without its zero byte ends there (the original read on
  into whatever memory followed); `ClientSession::ApplyParty` stops at a spell or item type its factory does not know.

## Compression

The LZ coder (`CoderState`, core/coder.h) over `GameBuffer`s: the growing buffer (grows to (used + n) * 2 + 0x40, new
bytes zero) and fixed views over memory owned elsewhere. A `BufferCursor` writes bits LSB first and reserves a byte
when a field starts in a new byte, so literal bytes and bit fields interleave.
- The primitives (core/coder.h): a buffer's `Size` (the high mark raised to the cursor), `Unread`, `Clear`, `Write` /
  `Read` and typed `Put` / `Get`, the growing buffer's `SetSize`; a cursor's `PutBits` / `GetBit(s)`, `WriteBytes` /
  `ReadBytes`, typed `Put` / `Take`, `PutString` / `TakeString`, `End`. Packets and streams go through these only; a
  buffer's one error flag is `readPastEnd` (a read past the high mark, a string over its limit). Unit checks:
  tests/unit/game_buffer.cpp.
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
- `DeltaField`: two snapshot streams (what channel 0 / 1 last sent) and two output streams. Channel 0 is written
  against the last full write, channel 1 against the last write of any kind. `DeltaState`: the fields, three 16-bit
  masks (sent, last, written), the reader, the `Mode` (write full, write changes, read against snapshot 0 / 1, idle),
  two header streams. `Finish` after a write of the changes joins the written mask into the sent one and rebuilds
  both headers (the mask as 2 bytes, then each field's output of that channel, byte aligned).
- A field is written through a `DeltaWriter` and read through a `DeltaReader` (`DeltaState::WriteField` /
  `ReadField`, none when the field is not in the mask; an effect's single field and the path marker construct them
  directly); each carries what the original kept in four globals (which snapshots a write keeps, the snapshot read
  against and the received stream) and ends with `End`. `DeltaWrite::Full` keeps both channels' snapshots,
  `Changes` only channel 1's. `DeltaReader::SnapshotConsumed` is every reader's "the field ended where expected".
- Format per value: an "all unchanged" bit, else a 0 bit and per 4-byte word an "unchanged" bit or 0 + the word, then
  the remaining bytes raw. Quantised: 3D position (x, y as (v + 16) * 120.47, z as (v + 32) * 341.33, 16 bits each),
  2D position ((v + 32) * 60.23), ranged ((v - min) * 65535 / (max - min)), 16-bit float (sign, exponent - 0x40 in 7
  bits, 8 rounded mantissa bits).
- Kept: the 16-bit float's mantissa rounding past 8 bits wraps to 0 without raising the exponent; exponents below 0x40
  give 0, above 0xBF wrap in 7 bits; `ReadDelta` (every reader's) stores a changed value in snapshot 1 even when
  reading against snapshot 0; `ReadText` leaves the buffer empty for a string of `size - 1` characters or more.

## Message links (net_link.h)

- `FileStream`: a ByteStream over a `platform::File` (saves, the connect screen's address book).
- `NetLink` (abstract): connected, alive, has incoming, Send, Receive, disconnect. `LoopbackLink`: two joined to each
  other, the single-player link; Send puts a copy at the end of the peer's queue, Receive takes the front.
  `FramedLink` (abstract): the received bytes in a memory stream, each message a 4-byte length and its bytes; Send
  frames the message (a 0x8000-byte buffer). `DriverLink`: a framed link over a connection's reliable streams; its
  disconnect writes unread bytes back over the incoming stream. `DriverSession` keeps a driver link per remote player.
  Counted objects start with no references and no flags (`RefCounted`'s initialisers).
- The player sources hand new links over as `JoinedLinks` (`std::list<netlink::NetLink*>`, TakeNew).

## Unreached in the shipped game

Kept on purpose (they carry a game action or the protocol), each with a one-line comment at the definition; no
coverage run reaches them:
- `ServerSession::RemoveVariable` (GSDelVar's multiplayer branch in `script_exec.cpp`): the only live GSDelVar calls
  (zone13.mob, zone2.mob) sit behind single-player story variables.
- Handlers for messages our own peer never sends, kept for a vanilla peer: `PlayerClient::OnDisconnect` (0x01) and
  `OnObjectValues` (0x0C); `PlayerServer::OnLeave` (0x01), `OnRefreshParty` (0x02), `OnBriefingDone` (0x07),
  and `OnUnitLookup` (ClientMessage::UnitLookup, 0x38, reads and drops; 0x37 has no handler).
- Reached in play but not by the scenarios: `SendGoTo`, `SendFollow`, `TakeExit`, `OnGoTo`, `OnFollow`, `OnTakeExit`,
  `OnWeaponRemoved`, `ItemsTaken`, `ItemTaken`, `SendWeaponRemoved` (the hud's orders, a broken weapon, taken loot).
- The `SessionBase` handlers of the packet kinds a session does not serve log an error and are not empty.
