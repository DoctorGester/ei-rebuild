# Cleanup Phase 1: network (cleanup plan subsystem 9)

## Ticket C2c: sessions, connections, the socket driver (2026-10-05)

Behaviour unchanged; every rewritten address keeps its hook and its verify wrapper (the wrappers'
checks are the same; only the names they call changed, and wrappers of virtual slots call the
qualified member).

### Declarations

- `core_refcount.h`: new root `CRefCounted` (vtable 0073BB68: count, flags, slot 0 deleting
  destructor, slot 1 Serialize); `CRefObject` (0073BB58) derives from it and keeps slots 2 / 3,
  Release and Shutdown. Layout and every existing use unchanged; the sessions derive from the root
  because their slot 2 is RemoveClient, not OnShutdown.
- `net_classes.h`, one hierarchy, all `__declspec(novtable)` (never built by C++; the game's
  vtables dispatch the virtual calls):
  - `SessionBase : CRefCounted` (0073BF5C): slots 2-14 declared (RemoveClient, the eight packet
    handlers OnUpdate / OnGetInfo / OnLogin / OnLeave / OnInfo / OnAccepted / OnRejected / OnMaster,
    OnClientLost / OnClientLeft, CollectReplicated, Tick); fields named (`magic_`, `timeout_` +0xC,
    `maxRoundTrip_` +0x10, `idReuseTime_` +0x14, `frame_`, `time_`, `clients_` (ClientNode list),
    `types_`, `socket_`, `reliable_` (was typeFlags), `handlers_`). The free `Session*(self, void*, ...)`
    functions became members: FindClient, AddClient, Send, RateFor, SendLeave, DeletingDestruct;
    slot bodies are registered through `...Hook` shims (rule 4). Receive is `SessionBase::Tick`.
  - `ServerBase` (0073BBB4) and `ClientBase` (0073BE30) are new classes for the layers the game
    server / client constructors inline: the server's pending joins (`PendingJoin`), the client's
    lobby fields (host, join data / id / state / refusal, retry timer and interval, servers,
    queries, waiting addresses, always-asked address, next query). net_lobby.cpp's `At<>(self, k...)`
    offsets and `void* self` functions became members of these classes.
  - `ServerSession` / `ClientSession` derive from them (were `{ SessionBase base; ... }`); their
    lists and counted pointers are named from what the handlers do with them (players, lines,
    files, file requests, bindings, map, queued maps, variables, awards, offers, trades, finished
    trades, banned; client: players, lines, files, chunks, file requests, info, binding, map,
    zones, variables, player, timers, awards, party, offers, trade records, trade, parties).
    Non-virtual member hooks are `EI_REPLACE_METHOD` now (were shims).
  - `Message : CRefObject` (0073BC10) with virtual Write / Read (slots 4 / 5) and `version_` (+8,
    was `kind`); `ServerEntryBase` (0073BB40) and `ServerEntry` (0073BB28) replace net_lobby.cpp's
    `EntryBase` and net_server_entry.cpp's raw offsets; `Query` / `QueryVector` moved here
    (`QueryVector::Serialize` 004414E0 uses `ChunkArchive` members instead of forwarders).
  - `NetAddress` (sockaddr_in) with `Parse` 00440440, `Format` 00440690, `ToString` 00435010
    (were free functions on byte pointers); `SocketDriver` members Construct / Destruct / Open /
    Broadcast / IsLocal / SendTo / Receive (were free functions).
  - `TypeRegistry` holds two `stlp::hash_map`s (net::HashTable / TypeNode removed; stlp.h gained
    `Bucket` / `LinkAtHead` for the inlined insert that still calls the game's resize / new node).
  - One `LogStream` (the network log's inserters, defined once in net_session_tick.cpp) and the
    session globals declared once: `NetLog()`, `CurrentSession()`, `CurrentConnection()`,
    `SendBuffer()`, `ReceiveBuffer()`, `GameServer()`, `GameClient()`.
- `net_connection.h`: `Connection : CRefObject` (0073BF38; slot 2 OnShutdown = the reset 00433320,
  slot 3 Clone) and `ReplicaEntry : CRefObject` (0073BF4C); fields `name_`; typed STLport
  containers (`TreeMap<Key, Value>` / `TreeNode`, `HistoryNode` with a double time, `HeaderNode` /
  `ReliableChunk`, `FreedBatch` / `FreedId`, `Record`, `AttachedId`) replace the `+0x10` / `+0x14`
  node offsets; `Silent` (00433B10) is a member.

### Behaviour notes

- The reset slot (00433320) now returns the constructor's `this` as the original leaves it in EAX
  (the hook returned void before); ReplicaEntry's reset returns 0 (the original's EAX is garbage).
- Connection::Close passed a 4-byte local as the 8-byte hash iterator out-parameter of 0042A810 (our
  port's stack overrun, not the game's); it is a two-word iterator now.
- Calls that went through forwarders to rewritten functions of this subsystem (socket driver,
  Connection::Construct / Tick / Close, the entry base constructor, AddressParse, the archive
  fields) are direct calls now; the release build runs the same code, the verify build no longer
  runs those wrappers for these callers. Calls the old code made "at" an address on purpose
  (net_lobby.cpp's ...At forwarders, the server base tick from the game server's tick) are kept.

### Other files (mechanical)

- world_save.cpp: `net::GameServer()` and `flags7_ & kDetached` instead of the old accessor and
  `At<>(session, 4) & kSessionInactive`.
- tests/verify: verify_net_*.cpp, net_verify.h, verify_atexit_400.cpp renamed accordingly.

### Left (follow-up list)

- The message classes (net_message_classes.cpp, net_messages.cpp; the `F<>` / `At<>` layouts of
  0073BBF8 ... 0073BDD0 and the trade / party / award / file messages read by net_server_*.cpp and
  net_client_*.cpp): one class per message deriving from `Message`, their factories / clones /
  resets / Write / Read as members (most remaining At<> and VCall uses of the subsystem).
- The links (net_link.h / .cpp, net_session_pump.cpp): NetLink / Loopback / Replay / Framed /
  Driver as classes with their 13-14 virtual slots (56 VCalls); netsession::Session and its
  driver layout duplicate netlink's; FileStream should derive from CStream (also world_game.cpp's
  own FileStream copy).
- GameBuffer (net_coder.h) slots 0-6 are still VCalls (it is used by value and aggregate-built in
  world_savefile.cpp, so it needs the novtable treatment and care with copies).
- net_delta.h BitStream (ticket C4), net_state.cpp (unit / map object offsets: units ticket).
- StringRep (raw STLport string layout) vs stlp::string; the forwarders of this subsystem that call
  rewritten functions at their addresses (ticket C3); hash_tables_400.cpp's own view of the type
  registry (004368F0).

## Ticket C2e: the message classes (2026-10-05)

Behaviour unchanged; every rewritten address keeps its hook and its verify wrapper (the same checks;
the wrappers of slots call the qualified member).

### Declarations

- New `net_messages.h` / `net_messages.cpp` (net_message_classes.cpp merged into net_messages.cpp,
  whose constructor bodies were the other half of the same classes): one `__declspec(novtable)`
  class per message, deriving from `Message`, with slot 2 (`OnShutdown`, the reset), slot 3
  (`Clone`, covariant), slots 4 / 5 (`Write` / `Read`) as overrides, a static `Create` (the type
  registry's factory), `Construct` (only the nine classes whose constructor body is a function of
  its own) and `Destruct` (the destructor body). Named members with offset asserts:
  - client to server: `LoginMessage` 0073BBF8 (type 5), `SayMessage` 0073BC28 (6, a chat line from
    a client), `CommandMessage` 0073BC40 (7), `FileRequestMessage` 0073BC58 (13), `PartyMessage`
    0073BDD0 (15, a trade offer: members, value, accepted flag and revision, revision, player,
    partner, trading flag), `PartyMemberMessage` 0073BD78 (16, a CMemoryStream);
  - server to client: `PlayerMessage` 0073BC70 (1), `LineMessage` 0073BC88 (2), `HostMessage`
    0073BCA0 (3, "host:port" and the zone; built by world_game.cpp's AnnounceHost), `BindingMessage`
    0073BCB8 (4), `ZoneMessage` 0073BCD0 (8), `MapMessage` 0073BD00 (9), `VariableMessage` 0073BCE8
    (10), `FileMessage` 0073BD18 (11), `FileChunkMessage` 0073BD30 (12), `AwardMessage` 0073BD48
    (14), `TradeMessage` 0073BD60 (17).
  - Duplicate layouts merged: `TextMessage` (one string; Say / Zone / Map differ only in their
    vtables) and `PairMessage` (two counted pointers; Binding / FileRequest, with `PlayerRef` /
    `FileRef` / `ConnectionRef` accessors) hold the shared slot bodies (`...As(vtable)` helpers);
    `RefVector` (vector of counted pointers) replaces net_client_party.cpp's own copy;
    net_client_party.cpp's `PartyState` is `PartyMessage`, net_server_files.cpp's `FileMessage`
    and net_server_vars.cpp's `ScriptVariable` are the classes here; the client's trade records
    are `TradeRecordNode` (0x160 bytes: the two revisions and three UnitRecordBuilders) in
    net_client_handlers.cpp.
- `ServerEntry` (net_classes.h) gained the slot overrides and `Destruct` (its bodies moved here).
- `NewMessageMemory<T>()`: the inlined operator new with the count dword cleared before the null
  check, used by every file that builds messages inline.
- `SessionBase::AddHandlerFor<T>(channel, handler)` builds the prototype with `T::Construct`
  (was a size and a constructor function pointer); the part after it is `AddHandlerForPrototype`.
- The `ConstructMessage73XXXX` / `MessageFactory73XXXX` / ... free functions are gone; the
  registrations name the members (slots through `...Hook` shims, rule 4).

### Other files (offsets to members)

net_server.cpp (CollectReplicated), net_server_join, _players, _tick, _trade, _commands, _chat,
_awards, _files, _map, _list, _vars; net_client.cpp (the local login, CollectReplicated),
net_client_handlers, _party, _awards, _commands, _life; net_handler_registrars.cpp. The handlers
take the message class they receive (same registers). The message-related `ei::VCall`s became
virtual calls: deleting destructor (`delete`), OnShutdown, and the party member streams'
Clear / Write / Read / Seek / Size through a `CStream*` (checked in the objects: every one still
goes through the vtable; no direct call to a CMemoryStream slot).

### Behaviour notes

- The resets (slot 2) return 0 now; the originals leave EAX undefined (-1 for the player record,
  other values elsewhere). Only `CRefObject::Shutdown` passes it on, and no caller reads it.
- Message reads and writes of object references call `Connection::WriteObjectRef` /
  `ReadObjectRef` directly instead of through forwarders to 004355B0 / 00435820, and the server
  entry's reset calls `ServerEntryBase::ConstructBase` directly (same code in the release build;
  the verify build no longer runs those wrappers for these callers).
- The reset slots' `if (!this)` (the inlined placement new's null test) is left out.

### Original bugs kept

- PartyMemberMessage::Clone (00417420) sizes the copy's stream like the source, then reads the
  copy's uninitialised bytes into the source's buffer (slot 1, Read) instead of writing the source
  into the copy.

### Left

- world_game.cpp's AnnounceHost builds a HostMessage by hand (world subsystem; it can use
  `HostMessage` and `NewMessageMemory`).
- The UnitRecordBuilder offsets the party code reads through the client (+0x2660, +0x2664, +0x2670,
  +0x26E0) and the party / unit VCalls in net_client_party.cpp and net_client_awards.cpp (units /
  items tickets).
- The forwarders to the STLport list / vector / string instantiations the handlers call (library
  layer) and the timed-entry list nodes ({delay, object}) still use raw node offsets.
- The links, GameBuffer and the session pump (ticket C2f).

## Ticket C2f: the links, the session pump, GameBuffer (2026-10-05)

Behaviour unchanged; every rewritten address keeps its hook and its verify wrapper (the same checks;
the wrappers of slots call the qualified member, the others the member).

### Declarations

- `net_link.h`, all `__declspec(novtable)` with the game's slots in vtable order (the slots that are
  not rewritten are declared only, so a devirtualised call would not link):
  - `FileStream : CStream` (0073C8D8): Read / Write / Seek / Size / Position / Clear overrides,
    `Construct` 00478470, `CopyConstruct` 00479B10, `Destruct` 00477FB0, `DeletingDestruct`
    00477F90; the slot 3 and slot 6 bodies return the CFile's seek result, so they are the
    non-virtual `SeekFrom` / `Restart` (registered directly; the overrides call them). `Open` /
    `Close` (the CFile's virtual Open / Close, inlined by every user) replace four copies.
    Fields from CStream (`reading_`, `ok_`, `version_`, was `limit`); `file_` stays raw storage.
  - `NetLink : CRefObject` (0073C8A4): slots 4-12 (Connected, Alive, HasIncoming, IncomingCount
    00478000, Send 00478100, Receive 00478110, Pump 00478010, Disconnect, Unknown12 00478020);
    `Construct` 00477E60 (was net_session_pump.cpp's `RecordConstruct`, and the inlined
    `LinkConstructInline` / `RefValueConstructBase` + `FileStreamConstruct` sequences),
    `ConstructCopyOf` (the clones' shared head), `Destruct`, `DeletingDestruct`, `OpenRecording`,
    `CloseRecording`, `Record`.
  - `LoopbackLink` (0073C8F8, `peer_` typed), `ReplayLink` (0073C92C), `FramedLink` (0073C9C8, adds
    slot 13 `SendBytes`), `DriverLink` (0073C990, `player_` a `net::Connection*`; `Construct`
    0047A030 moved here from net_session_pump.cpp). The free `XxxSlot(self, void*, ...)` functions
    became members; static `LoopbackLink::CreatePair` 004782E0 and `ReplayLink::Create` 00478960.
  - Slots 4 / 5 / 6 / 13 are declared `bool` (the replay's and the driver's own bodies set only AL);
    the hooks of bodies that returned an int (loopback Connected / Alive, replay Alive /
    HasIncoming, framed HasIncoming, driver Connected / SendBytes) still return 0 / 1 as an int.
  - `DriverSession` (+8 / +0xC lists of `DriverNode`) replaces netsession::Session / ListNode;
    `Pump` 00479E30 and `Start` 0047A080 are members (EI_REPLACE_METHOD). The pump reads the
    player's `Connection::reliableIn_` (was `+0x1148` and raw offsets) through `BufferUpdateHigh`.
- `net_coder.h` `GameBuffer`: a `__declspec(novtable)` class with slots 0-6 (ReserveRead,
  ReserveWrite, Commit, ReadMore, WriteMore, the deleting destructor at slot 5, Seek), all declared
  only; `InitView` replaces the two aggregate-built views (net_delta.cpp, world_savefile.cpp).
  Copy construction and assignment are byte copies (the vtable included), as they were for the
  plain struct, so the verify wrappers' copies are unchanged.

### VCalls replaced

The links' own slots and the streams' slots (net_link.cpp 56 -> 6, net_session_pump.cpp 4 -> 0),
world_game.cpp's CopyFrom, ui_saveload.cpp's stream Read / Write, and GameBuffer's slots 2, 3, 4,
6 in net_coder.cpp, net_link.cpp, net_game_info.cpp, net_lobby.cpp, net_server_list.cpp,
net_session_tick.cpp, net_socket_driver.cpp, net_client_commands.cpp. What is left in these files
are the MFC CFile slots (library layer). Checked in the release objects: no C++ vtable of these
classes is emitted, and no stream or link slot is called directly (the queue's element destructor
is `call [vtbl]` with 0, as before). net_lobby.cpp's SendInfo calls slot 4 of its local buffer
through a pointer (a call on the local itself was devirtualised and did not link).

### Other files (mechanical)

- world_game.cpp: its own FileStream struct is `netlink::FileStream`; LocalFileStream uses
  `Construct` / `Destruct` / `Open` / `Close` (its destructor now also stores CStream's vtable
  into the dying local, as 00477FB0 does; not observable).
- ui_saveload.cpp: LocalFileStream likewise (identical stores).
- CMemoryStream's Resize / SetSize forwarders in net_link.cpp are the shared members (same
  addresses).

### Behaviour notes

- The resets (slot 2) return 0 (the hooks returned void before; nobody reads it).
- The resets' `if (!this)` (the inlined placement new's null test) is left out.

### Left

- The MFC CFile slots (FileStream, world_savefile.cpp's file stream calls) and world_savefile.cpp's
  CStream VCalls on `void*` streams (world subsystem).
- GameBuffer's own slot bodies (the view's Seek / destructors in net_coder.cpp) are still free
  functions; the growing buffer's forwarders (004DA7F0 ...) are declared per file.
- The world object the pump reads players from (+0x28) and the manager 0079B5DC.
