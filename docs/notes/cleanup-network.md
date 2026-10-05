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
