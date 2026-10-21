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
  `ServerEntry` (0073BB28), `Query` / `ClientQueries`, `NetAddress` (sockaddr_in: Parse 00440440, Format 00440690, ToString 00435010), `SocketDriver` members,
  `TypeRegistry` over two `stlp::hash_map`s (`Bucket` / `LinkAtHead` added to stlp.h for the inlined insert that still calls the game's resize / new node),
  one `LogStream`, and the session globals declared once (`NetLog()`, `CurrentSession()`, `CurrentConnection()`, `SendBuffer()`, `ReceiveBuffer()`,
  `GameServer()`, `GameClient()`).
- `net_connection.h`: `Connection : CRefObject` (0073BF38; slot 2 OnShutdown = the reset 00433320, slot 3 Clone) and `ReplicaEntry : CRefObject`
  (0073BF4C); typed STLport containers (`TreeMap`, `HistoryNode` with a double time, `HeaderNode` / `ReliableChunk`, `FreedBatch` / `FreedId`, `Record`,
  `AttachedId`) replace the `+0x10` / `+0x14` node offsets.
- `net_messages.h`: one novtable class per message deriving from `Message` with slot 2 `OnShutdown` (the reset), slot 3 `Clone` (covariant), slots 4 / 5
  `Write` / `Read`, a static `Create` (the registry's factory), `Construct` (only where the constructor body is a function of its own) and `Destruct`.
  Duplicate layouts merged: `TextMessage` (one string; Say / Zone / Map differ only in vtable), `PairMessage` (two counted pointers; Binding /
  FileRequest, accessors `PlayerRef` / `FileRef` / `ConnectionRef`), `RefVector`, `TradeRecordNode` (0x160: two revisions and three `PartyStorage`s; in `net_classes.h` since C2n).
  `NewMessageMemory<T>()` is the inlined operator new with the count dword cleared before the null check; `SessionBase::AddHandlerFor<T>(channel,
  handler)` builds the prototype with `T::Construct`.
- `net_link.h`: `FileStream : CStream` (0073C8D8; the slot 3 and slot 6 bodies return the CFile's seek result, so they are the non-virtual `SeekFrom` /
  `Restart`, registered directly; `Open` / `Close` replace four inlined copies), `NetLink : CRefObject` (slots 4-12, `Construct`, `ConstructCopyOf`, `Destruct`,
  `OpenRecording`, `CloseRecording`, `Record`), `LoopbackLink`, `ReplayLink`, `FramedLink` (adds slot 13 `SendBytes`), `DriverLink` (`player_` a
  `Connection*`), `DriverSession` with `Pump` / `Start` members. Slots 4 / 5 / 6 / 13 are declared `bool` (some bodies set only AL), the hooks of bodies that
  returned an int still return 0 / 1 as an int. `net_coder.h` `GameBuffer`: novtable, slots 0-6 declared only (ReserveRead, ReserveWrite, Commit, ReadMore,
  WriteMore, deleting destructor, Seek); `InitView` replaces the aggregate-built views; copies are byte copies, vtable included.

## C2n: the remaining network files and the players

- `ClientSession::parties_` are `PartyStorage` (a CParty's 0x70 bytes with `Party()`, as `CPlayer::party_`; the class is abstract in C++);
  `TradeRecordNode` (three parties) is in `net_classes.h` and `tradeRecords_` is typed. The party publish / apply (`net_client_party.cpp`)
  read our party's items (`items_`, `CItem::spell_`, `type_`, `unknown_18` the packed id, `Save` / `Load` slots 10 / 9) and the partner
  party's `SpellManager()` / `ItemManager()` (slots 22 / 18) and `CreateObject` (slot 23); `PartyMessage::partyValue_` is `money_`
  (`CParty::Money()`). The object vector is an `stlp::vector<CEngineObject*>`.
- `ApplyAwards` / the client tick go through `CPlayer` (`Party()->Money()`, `items_`, `connection_->state_`) and `CItem::IsSameAs` /
  `GetClassId` / `Release`; kind 3 takes the first given item whose slot 14 returns 0 (checked against Ghidra, 0041CD80).
- `CPlayerConnection` has `state_` (+0x20, both connection classes); `CPlayer::unknown_130` is `dirty_` (the state bits the server's writers
  clear); `CPlayerServer::player_` and `CPlayerClient::player_` are `CPlayer*`. The writers reach the party variables
  (`Party()->variables_`) and the game state (`sessionKind100_`) through `Owner()`. `WriteTo` takes a `CStream*` (slot 7 `CopyFrom`, the
  world server's zone header); `CPlayer::Tick` calls `RunTick` (slot 6). The value the 0x0C / 0x14 writers send is the item's `charge_`
  (+0x28; the FLD / FSTP copy is kept).
- `GameBuffer` slot calls are virtual (`Seek` 6, `ReadMore` 3, `WriteMore` 4); `FileStream`'s CFile goes through `mfc::CFileVirtuals`
  (`File()`). `DriverSession::PumpDrivers` takes the `SessionBase` it is given (the game client or server) and asks the game server
  (`GameServer()`, 00417E40) about each connection.
- Shared list values in `net_classes.h`: `RefNode` (= `stlp::list<RefPtr>::Node`), `TimedEntry` / `TimedNode` ({seconds as a float or its
  bits, counted object}: the client's timers, the server's awards and finished trades). `SentPacket` is a history node's value: `RollBack`
  / `Delivered` take it. `NewMessageMemory` is in `net_classes.h`.
- Every new virtual call must dispatch through the vtable (no devirtualised call to a rewritten body such as `CMemoryStream::Write` or
  `CItem::Save`). `AppendServer` clears the copy-on-share bit with a byte AND of `flags6_` where the original ANDs the word at +6 with
  0xFFFE (same result).

## Behaviour notes

- The reset slots (slot 2) return 0 (the originals leave EAX undefined; only `CRefObject::Shutdown` passes it on and nobody reads it);
  `ReplicaEntry`'s reset returns 0; the `if (!this)` of the inlined placement new is left out.
- `Connection::Close` passes a two-word hash iterator out-parameter to 0042A810 (a 4-byte local overran the stack).
- Calls made "at" an address on purpose (`net_lobby.cpp`'s `...At` forwarders, the server base tick from the game server's tick) stay. A
  call on a local buffer's slot is made through a pointer in `SendInfo` (a call on the local itself was devirtualised and did not link).

## Left

- The `ClientSession` / `ServerSession` / `ClientBase` list members are still `void*` sentinels, typed at the use sites (`RefNode`,
  `TimedNode`, `TradeRecordNode`); typing the members touches `ui_chat_window.cpp` (`ClientList(client->offers_)`). `net_lobby.cpp` walks
  its lists through a links-only `Node`; `net_client_handlers.cpp`'s `Append` / `RemoveValue` take `void*` lists.
- `Connection::Construct` stores float members as integer bits (`Put32` on `sendInterval_` and the halves of `clock_`): kept (no FPU load).
- `player_messages.cpp`: `PartyRecords` (`party_record_lookup.h`; 006621B0) is a byte view of a CParty's record array (`records + i * 0x784
  + 0x1C`). `net_server_awards.cpp` /
  `net_server_map.cpp` read map registry entries as bytes (+0xC, +0x10, +0x14). `world_game.cpp`'s `AnnounceHost` builds a `HostMessage`
  by hand. Forwarders to the STLport list / vector / string instantiations the handlers call, the MFC `CFile` slots, GameBuffer's own
  slot bodies (free functions) and the growing buffer's forwarders (004DA7F0 ...) are declared per file. `StringRep` vs `stlp::string`;
  `hash_tables_400.cpp`'s own view of the type registry (004368F0).

## C2z: the client's queries

`ClientQueries` (`net_classes.h`): `stlp::vector<Query> list` plus the `extra` dword saved with it (+0x0C). The original's vector
functions are the game's `stlp::vector<Query>` instantiation, forwarded with it as `this`: 004439C0 `_M_insert_overflow`, 00443BF0
`operator=`, 004442B0 `erase(first, last)`, 00444090 `resize`. `Ask` and `Forget` keep the inlined push_back / erase over `Finish()` /
`EndOfStorage()`; `ui_connect_screen.cpp`'s address book follows (the constructor's wrapper copies raw bytes: the vector has no copy).

## F5: meanings of unknown fields in the rest (2026-10-14)

- `CEngineObject` +0x18 is `prototypeId_`: the database prototype (items: index << 16 | material; spells: index << 16; units: the plain
  index).
- `CPlayer` is the one player class (world.h's `WorldPlayer` and `WorldSession` views folded in, C15): +0xFC `partyId_`, +0x104 `lastScreen_`, +0x108 `godMode_`, +0x110 `lastTick_`; `CPlayerClient`
  +0x84 is `localSession_`. The login's +0x84 is the party id.
- `CPlayerServer` +0x24 `possessedId_`: -1, or the Possession spell's target; the next party refresh sends group50F8_ and resets it.
  world_update.cpp's `IsLocal` tests it (its name may be wrong: any player without a pending possession passes).
- `ServerSession` +0x25FC `masterKey_` (StartServer's last argument, the client queries' `extra`) and +0x2578 `masterToken_` (the master
  server's last dword, sent back); `GameInfoRecord` +0x1A..+0x23 carry them with the kind and the password flag.
- app_device_list.cpp's `DeviceRecord` is the lever table's record (`LeverRecord`, lever.h): Levers.ldb.
- `mapspec::Waypoint` +0x10 / +0x12 are the look's wait and stop-first bit (its turning time is `time`, as AILook::time); `Behaviour` +0x38
  `enabled` (HearAlarm); `ObjectSpec` +0x6E `side`, +0x74 `skinTexture` (chunk B008).
- Only written, nothing reads them: `CBaseManager` `reservedA_` / `reservedB_` (0 / 300 after Create), `CPlayerServer::spare_`,
  `CSoundManager::spare`, `Spec::spareId` (copied); chunk or archive fields read and written with their record only keep the field's id
  (`field0A`, `field04`, `field0E`, `field2`, `field10`).