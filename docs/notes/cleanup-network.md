# Cleanup phase 1: network (cleanup plan subsystem 9)

How the network classes are declared after the cleanup (headers `net_classes.h`, `net_connection.h`, `net_messages.h`, `net_link.h`,
`net_coder.h`, `core_refcount.h`) and what is still left. Layouts and behaviour of the classes are in `network.md`. Behaviour was unchanged by every
step; the conventions of `cleanup-engine-base.md` apply (novtable classes over the game's vtables, slot shims, wrappers of virtual slots call the
qualified member).

## Declarations

- `CRefCounted` (0073BB68: count, flags, slot 0 deleting destructor, slot 1 Serialize) is the root; `CRefObject` (0073BB58) derives and keeps slots 2 / 3,
  Release and Shutdown. The sessions derive from the root because their slot 2 is RemoveClient, not OnShutdown.
- One hierarchy in `net_classes.h`: `SessionBase : CRefCounted` (0073BF5C; slots 2-14 named, fields named: `magic_`, `timeout_` +0xC, `maxRoundTrip_`
  +0x10, `idReuseTime_` +0x14, `frame_`, `time_`, `clients_`, `types_`, `socket_`, `reliable_`, `handlers_`; FindClient, AddClient, Send, RateFor, SendLeave,
  DeletingDestruct are members; the receive step is `SessionBase::Tick`), `ServerBase` (0073BBB4: pending joins) and `ClientBase` (0073BE30: lobby fields,
  retry timer and interval, servers, queries, waiting addresses), `ServerSession` / `ClientSession` on top with their lists and counted pointers named
  from what the handlers do with them. `Message : CRefObject` (0073BC10, virtual Write / Read, `version_` +8), `ServerEntryBase` (0073BB40) /
  `ServerEntry` (0073BB28), `Query` / `QueryVector`, `NetAddress` (sockaddr_in: Parse 00440440, Format 00440690, ToString 00435010), `SocketDriver` members,
  `TypeRegistry` over two `stlp::hash_map`s (`Bucket` / `LinkAtHead` added to stlp.h for the inlined insert that still calls the game's resize / new node),
  one `LogStream`, and the session globals declared once (`NetLog()`, `CurrentSession()`, `CurrentConnection()`, `SendBuffer()`, `ReceiveBuffer()`,
  `GameServer()`, `GameClient()`).
- `net_connection.h`: `Connection : CRefObject` (0073BF38; slot 2 OnShutdown = the reset 00433320, slot 3 Clone) and `ReplicaEntry : CRefObject`
  (0073BF4C); typed STLport containers (`TreeMap`, `HistoryNode` with a double time, `HeaderNode` / `ReliableChunk`, `FreedBatch` / `FreedId`, `Record`,
  `AttachedId`) replace the `+0x10` / `+0x14` node offsets.
- `net_messages.h`: one novtable class per message deriving from `Message` with slot 2 `OnShutdown` (the reset), slot 3 `Clone` (covariant), slots 4 / 5
  `Write` / `Read`, a static `Create` (the registry's factory), `Construct` (only where the constructor body is a function of its own) and `Destruct`.
  Duplicate layouts merged: `TextMessage` (one string; Say / Zone / Map differ only in vtable), `PairMessage` (two counted pointers; Binding /
  FileRequest, accessors `PlayerRef` / `FileRef` / `ConnectionRef`), `RefVector`, `TradeRecordNode` (0x160: two revisions and three `UnitRecordBuilder`s).
  `NewMessageMemory<T>()` is the inlined operator new with the count dword cleared before the null check; `SessionBase::AddHandlerFor<T>(channel,
  handler)` builds the prototype with `T::Construct`.
- `net_link.h`: `FileStream : CStream` (0073C8D8; the slot 3 and slot 6 bodies return the CFile's seek result, so they are the non-virtual `SeekFrom` /
  `Restart`, registered directly; `Open` / `Close` replace four inlined copies), `NetLink : CRefObject` (slots 4-12, `Construct`, `ConstructCopyOf`, `Destruct`,
  `OpenRecording`, `CloseRecording`, `Record`), `LoopbackLink`, `ReplayLink`, `FramedLink` (adds slot 13 `SendBytes`), `DriverLink` (`player_` a
  `Connection*`), `DriverSession` with `Pump` / `Start` members. Slots 4 / 5 / 6 / 13 are declared `bool` (some bodies set only AL), the hooks of bodies that
  returned an int still return 0 / 1 as an int. `net_coder.h` `GameBuffer`: novtable, slots 0-6 declared only (ReserveRead, ReserveWrite, Commit, ReadMore,
  WriteMore, deleting destructor, Seek); `InitView` replaces the aggregate-built views; copies are byte copies, vtable included.

## Behaviour notes

- The reset slots (slot 2) return 0 (the originals leave EAX undefined; only `CRefObject::Shutdown` passes it on and nobody reads it); `ReplicaEntry`'s
  reset returns 0; the `if (!this)` of the inlined placement new is left out.
- `Connection::Close` passed a 4-byte local as the 8-byte hash iterator out-parameter of 0042A810 (our port's stack overrun); it is a two-word iterator now.
- Calls the old code made "at" an address on purpose (`net_lobby.cpp`'s `...At` forwarders, the server base tick from the game server's tick) are kept.
  A call on a local buffer's slot is made through a pointer in `SendInfo` (a call on the local itself was devirtualised and did not link).

## Left

- The remaining `At<>` / `VCall` uses of the message classes' readers in `net_server_*.cpp` / `net_client_*.cpp`; `world_game.cpp`'s `AnnounceHost`
  builds a `HostMessage` by hand (can use `HostMessage` and `NewMessageMemory`); the `UnitRecordBuilder` offsets the party code reads through the
  client (+0x2660, +0x2664, +0x2670, +0x26E0) and the party / unit VCalls in `net_client_party.cpp` / `net_client_awards.cpp` (units / items tickets).
- Forwarders to the STLport list / vector / string instantiations the handlers call (library layer) and the timed-entry list nodes ({delay, object}) use raw
  node offsets; the MFC `CFile` slots (FileStream, `world_savefile.cpp`) and its `CStream` VCalls on `void*` streams (world subsystem); GameBuffer's own slot
  bodies (the view's Seek / destructors in `net_coder.cpp`) are free functions; the growing buffer's forwarders (004DA7F0 ...) are declared per file.
- `net_state.cpp` (unit / map object offsets: units ticket); `StringRep` (raw STLport string layout) vs `stlp::string`; `hash_tables_400.cpp`'s own view
  of the type registry (004368F0); the world object the pump reads players from (+0x28) and the manager 0079B5DC.
