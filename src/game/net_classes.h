#pragma once

// The multiplayer session classes and the message base (00412000-00444000). Names are
// provisional; see docs/notes/network.md and
// docs/notes/cleanup-network.md.
//
// Class map (vtables):
//   0073BB68  CRefCounted (core_refcount.h): the root the sessions derive from.
//   0073BF5C  SessionBase (constructor 00436770, destructor body 00436A50): the UDP socket, the
//             connections to the peers, the message type registry and the handler tables.
//   0073BBB4  ServerBase: the pending joins; answers pings and logins. Inlined into the game
//             server's constructor; destructor body 004132F0.
//   0073BB70  ServerSession, the game server (0x2600 bytes; constructor 00412D80; pointer at 0079B5DC).
//   0073BE30  ClientBase: finding servers and joining one. Inlined into the game client's
//             constructor; destructor body 0041C530.
//   0073BDEC  ClientSession, the game client (0x2700 bytes; constructor 0041BC40; pointer at 0079B5D8).
//   0073BC10  Message (below): the replicated object base; classes 0073BBF8 ... 0073BDD0 (0x18 apart,
//             net_messages.h).
//   0073BB40  ServerEntry's base and 0073BB28 ServerEntry (below): a server as the lobby lists it.
//
// The sessions are allocated and built by the game's code (or our rewrites of their constructor
// bodies), which store the game's vtables. The classes are __declspec(novtable) and never built by
// C++, so their virtual functions dispatch through the game's vtables; the slots that are not
// rewritten are declared only.

#include "core_refcount.h"
#include "net_coder.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "lib/stlp_list_refs.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

class CParty;   // party.h
class ChunkArchive;

namespace net {

class Connection;   // net_connection.h
class Message;

// The memory of a message (or another reference-counted object) as the game's code allocates it
// inline: operator new, then the reference count dword cleared before the null check.
template <typename T>
T* NewMessageMemory() {
    auto* p = static_cast<std::uint8_t*>(gamecrt::operator_new(sizeof(T)));
    const std::uint32_t zero = 0;
    std::memcpy(p + 4, &zero, 4);
    return reinterpret_cast<T*>(p);
}

// A sockaddr_in: family 2, then the port and the IPv4 address in network order, 8 zero bytes.
struct NetAddress {
    std::uint16_t family;
    std::uint16_t port;
    std::uint32_t ip;
    std::uint8_t zero[8];

    // 00440440 (net_socket_driver.cpp). This address from "host[:port]" (an IPv4 number or a host
    // name looked up); `defaultPort` unless the text names one. False when the host is unknown.
    bool Parse(const char* text, int defaultPort);
    // 00440690. "a.b.c.d:port" (or, with `resolve`, "hostname:port" when the look-up succeeds) as
    // an STLport string in `out`; returns `out`.
    stlp::string* Format(stlp::string* out, int resolve) const;
    // 00435010 (net_session_tick.cpp). Format without the look-up; returns `out`.
    stlp::string* ToString(stlp::string* out) const;
};
static_assert(sizeof(NetAddress) == 0x10);

// The TCP link to the master server (0x40 bytes; constructor 00440C60, destructor 00440D40, tick
// 00440DE0; net_socket_driver.cpp). The master request box (ui_screens.h) holds one.
struct MasterLink {
    // 0x00440C60 (net_socket_driver.cpp).
    MasterLink* Construct(const void* address);
    enum State : std::int32_t { kClosed = 0, kConnecting = 1, kReceiving = 2, kDone = 3, kSending = 4 };

    GameBuffer out;              // +0x00 what is sent once connected
    GameBuffer in;               // +0x1C what was received
    std::uint32_t socket;        // +0x38 (-1: none)
    std::int32_t state;          // +0x3C State
};
static_assert(sizeof(MasterLink) == 0x40);
// 00440D40 (net_socket_driver.cpp): the socket closed (when open) and the buffers destroyed.
void __fastcall MasterLinkDestruct(MasterLink* self);

// The network log (0079B91C; null when logging is off): an old iostream whose inserters are library
// code, called at their addresses (net_session_tick.cpp).
class LogStream {
public:
    LogStream* Put(const char* text);       // 006FC31D
    LogStream* PutChar(char c);             // 006FC0B0
    LogStream* PutInt(int value);           // 006FC14E
    LogStream* Apply(void* manipulator);    // 00434FE0
    // The flush manipulator (00435000) the session code ends its lines with.
    LogStream* Flush() { return Apply(reinterpret_cast<void*>(0x00435000)); }
};

// ---- The globals of the session code ------------------------------------------------------------

class SessionBase;
class ServerSession;
class ClientSession;

// 0079B91C: the network log.
inline LogStream*& NetLog() { return *reinterpret_cast<LogStream**>(0x0079B91C); }
// 0079B920: the session being updated (set by Tick / Receive and by the connection code).
inline SessionBase*& CurrentSession() { return *reinterpret_cast<SessionBase**>(0x0079B920); }
// 0079B924: the connection being updated or read (null outside the session code).
inline Connection*& CurrentConnection() { return *reinterpret_cast<Connection**>(0x0079B924); }
// 0079B900: the buffer every update packet is built in; 0079B8E0: the one datagrams are received into.
inline GameBuffer* SendBuffer() { return reinterpret_cast<GameBuffer*>(0x0079B900); }
inline GameBuffer* ReceiveBuffer() { return reinterpret_cast<GameBuffer*>(0x0079B8E0); }
// 0079B5DC / 0079B5D8: the game server and the game client (null when there is none).
inline ServerSession*& GameServer() { return *reinterpret_cast<ServerSession**>(0x0079B5DC); }
inline ClientSession*& GameClient() { return *reinterpret_cast<ClientSession**>(0x0079B5D8); }
// The game client's trade offer (net_client_party.cpp, net_client_handlers.cpp; plain RET each):
// 0041DA50 publishes our offer, 0041E220 finds the partner's offer that pairs with ours (a
// PartyMessage, or null), 0041E180 takes the acceptance back, 0041E3E0 drops the offer.
void __fastcall ClientPublishParty(ClientSession* self);
void* __fastcall ClientFindParty(ClientSession* self);
void __fastcall ClientCommitParty(ClientSession* self);
void __fastcall ClientDropParty(ClientSession* self);

// ---- The message base ---------------------------------------------------------------------------

// A replicated network object (vtable 0073BC10): a connection sends it again whenever its version
// differs from the one last sent. Slot 2 (OnShutdown) of the message classes resets the message
// (destroyed and built again in place), slot 3 clones it.
class __declspec(novtable) Message : public CRefObject {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073BC10);

    // 0: 00413410 deleting destructor; 1: Serialize (empty); 2 / 3 pure in the base.
    virtual void Write(BufferCursor* cursor) = 0;   // 4: the fields at the cursor's byte position
    virtual void Read(BufferCursor* cursor) = 0;    // 5

    std::uint32_t version_;   // +0x08 1 when constructed; raised by every change
};
static_assert(sizeof(Message) == 12);

// The message base vtable under the name the message code uses.
constexpr std::uintptr_t kMessageVtable = Message::kVtableAddress;

using MessageFactory = Message* (__fastcall*)();

// 00413250. operator new(size) with the CRefObject count cleared. No null check (as the original).
void* __fastcall NewRefObject(std::size_t size);
// 004186E0. The message base constructor body: vtable 0073BC10, version 1. Returns this.
Message* __fastcall MessageConstructBase(Message* self);

// The message classes are declared in net_messages.h.

// ---- The server entries of the lobby ------------------------------------------------------------

// A server entry's base (vtable 0073BB40, 0x28 bytes; constructor body 004413E0).
class __declspec(novtable) ServerEntryBase : public CRefObject {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073BB40);

    // 0: 00412970 deleting destructor; 1: Serialize; 2 / 3 pure.
    virtual void Write(BufferCursor* cursor) = 0;   // 4
    virtual void Read(BufferCursor* cursor) = 0;    // 5

    // 004413E0. The constructor body (the reference header is left alone). Returns this.
    ServerEntryBase* ConstructBase();

    enum State : std::uint32_t {
        kLocal = 0,        // found by a broadcast ping
        kAsked = 1,        // asked directly
        kFromList = 2,     // from the master server's list
    };

    NetAddress address_;     // +0x08
    float timeout_;          // +0x18 seconds until the entry is checked again (1e38: never)
    float ping_;             // +0x1C seconds; the constant at 0073BFA0 when unknown
    std::uint8_t foreign_;   // +0x20 the reply's echoed word differs from the magic
    std::uint8_t unused_21[3];
    std::uint32_t state_;    // +0x24 State
};
static_assert(offsetof(ServerEntryBase, address_) == 0x08);
static_assert(offsetof(ServerEntryBase, state_) == 0x24);
static_assert(sizeof(ServerEntryBase) == 0x28);

// A server entry (vtable 0073BB28, 0x48 bytes): what a server says about itself (net_server_entry.cpp)
// and the client lists (net_lobby.cpp, net_server_list.cpp).
class __declspec(novtable) ServerEntry : public ServerEntryBase {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073BB28);

    // 0: 004128C0 deleting destructor; 1: 00412580 (empty). net_messages.cpp.
    int OnShutdown() override;                  // 2: 00412750 reset (EAX undefined in the original)
    ServerEntry* Clone() override;              // 3: 00412590
    void Write(BufferCursor* cursor) override;  // 4: 00412990
    void Read(BufferCursor* cursor) override;   // 5: 00412A30

    void Destruct();                            // 004128E0

    stlp::string name_;              // +0x28
    std::uint32_t kind_;          // +0x34 the game kind (4: none)
    stlp::string description_;       // +0x38 the current map's title
    std::uint8_t players_;        // +0x44
    std::uint8_t maxPlayers_;     // +0x45
    std::uint8_t password_;       // +0x46 whether joining needs a password
    std::uint8_t unused_47;
};
static_assert(offsetof(ServerEntry, name_) == 0x28);
static_assert(offsetof(ServerEntry, description_) == 0x38);
static_assert(offsetof(ServerEntry, players_) == 0x44);
static_assert(sizeof(ServerEntry) == 0x48);

// A server the client asks directly (0x2C bytes; constructor 00443770): its address and the reply
// last seen.
struct Query {
    // 00443E90 (slot 0, the deleting destructor; net_lobby.cpp). The reply buffer destroyed, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    Query* QueryDelete(unsigned flags);
    // 00444270 (RET 4; net_lobby.cpp). Copy constructor: the address and the reply buffer copied.
    // Returns this.
    Query* QueryCopy(const Query* from);
    NetAddress address;
    GameBuffer reply;            // +0x10
};
static_assert(sizeof(Query) == 0x2C);

// The client's queries (constructor 0041C420): an STLport vector of queries and a dword saved with
// it (not itself a vector). The vector's out-of-line functions are the game's instantiations of
// stlp::vector<Query> (net_lobby.cpp forwards to them).
struct ClientQueries {
    stlp::vector<Query> list;    // +0x00
    std::uint32_t extra;         // +0x0C

    // 004414E0 (net_lobby.cpp). The queries in a chunk archive (chunk 1 of chunks 1: field 1 the
    // address, 2 the reply's length, 3 the reply), then field 2 the extra dword.
    void Serialize(ChunkArchive* archive);
    // 00441410 (net_lobby.cpp). The query with `address`, or the end.
    Query* QueriesFind(const NetAddress* address);
};
static_assert(sizeof(ClientQueries) == 0x10 && offsetof(ClientQueries, extra) == 0x0C);
// 0041C420 (net_client.cpp). The queries' constructor: an empty vector and a zero dword. Returns self.
ClientQueries* __fastcall ClientVectorConstruct(ClientQueries* self);

// ---- The session base ---------------------------------------------------------------------------

constexpr int kChannels = 3;          // handler channels: 0 created, 1 changed, 2 removed
constexpr int kMessageTypes = 0x100;

// A vector of message handlers (STLport vector<void(*)()>: begin, end, end of storage). Handlers are
// `void __fastcall (SessionBase*, Message*)`.
using HandlerVector = stlp::vector<void*>;
static_assert(sizeof(HandlerVector) == 12);

// The message type registry (+0x2C of a session): two STLport hash_maps.
struct TypeRegistry {
    using ByVtable = stlp::hash_map<std::uint32_t, std::uint32_t, stlp::IntHash>;
    using ByType = stlp::hash_map<std::uint32_t, MessageFactory, stlp::IntHash>;

    ByVtable byVtable;     // +0x00 message vtable -> type id (constructor 00438B70, 100 buckets)
    ByType byType;         // +0x14 type id -> factory (constructor 00439F20, 100 buckets)

    // 00429F40. The type id of the message class whose vtable is *key, or -1.
    int TypeOf(const std::uint32_t* key) const;
    // 0042A890. Registers `type`: makes a prototype with `factory` (to learn its vtable), maps the
    // vtable to the type and the type to the factory, and releases the prototype.
    void Register(std::uint32_t type, MessageFactory factory);
    // 00438930. A new message of `type` from its factory (operator[] on byType: an unknown type
    // gets an empty entry), or null.
    Message* Create(std::uint32_t type);
};
static_assert(sizeof(TypeRegistry) == 0x28);

// The UDP socket of a session and the broadcast addresses of the host (net_socket_driver.cpp).
struct SocketDriver {
    std::uint32_t socket;        // +0x00 SOCKET (-1: none)
    NetAddress* addresses;       // +0x04 vector<sockaddr_in> of broadcast addresses
    NetAddress* addressesEnd;    // +0x08
    NetAddress* addressesEos;    // +0x0C

    // 004407F0. Starts Winsock and notes the broadcast address of the host's first address. Returns this.
    SocketDriver* Construct();
    // 00440940. Closes the socket, stops Winsock, frees the addresses.
    void Destruct();
    // 004409A0. A new non-blocking broadcast UDP socket, bound to `port` when positive.
    bool Open(int port);
    // 00440A90. The `index`th broadcast address with `port` into `out`; false past the last.
    bool Broadcast(NetAddress* out, unsigned index, int port);
    // 00440AF0. Whether `address` is the loopback or on the network of a broadcast address.
    bool IsLocal(const NetAddress* address);
    // 00440B90. Sends the buffer's contents to `address`; true when all of it went.
    bool SendTo(const NetAddress* address, GameBuffer* buffer);
    // 00440BE0. The next datagram (up to 0x800 bytes) into `buffer`, its sender into `from`.
    bool Receive(NetAddress* from, GameBuffer* buffer);
};
static_assert(sizeof(SocketDriver) == 0x10);

// The sessions' lists of counted messages (STLport list<RefPtr>: the value at +8).
using RefNode = stlp::list<RefPtr>::Node;

// The value of the timed lists (the client's timers, the server's awards and finished trades): the
// seconds left (a float; the code that queues an entry stores a constant's bits) and a counted object.
struct TimedEntry {
    union {
        float seconds;
        std::uint32_t key;   // the seconds' bits, as the lists copy them
    };
    RefPtr ref;
};
static_assert(sizeof(TimedEntry) == 8);
using TimedNode = stlp::list<TimedEntry>::Node;
// The timed lists by how they hold the object: shared (the client's timers, the server's awards)
// or counted (the server's finished trades).
using SharedTimedList = stlp::PolicyList<TimedEntry, stlp::SharedEntry>;
using CountedTimedList = stlp::PolicyList<TimedEntry, stlp::CountedEntry>;

// A node of the clients list (STLport list<RefPtr<Connection>>).
struct ClientNode {
    ClientNode* next;
    ClientNode* prev;
    Connection* connection;      // counted
};

// What a session replicates to the connection being updated (slot 13 fills it): STLport
// vector<{object, priority}>.
struct Replicated {
    Message* object;           // replicated while it is listed
    std::uint8_t priority;     // 0..10
    std::uint8_t unused_05[3];
};
using ReplicatedVector = stlp::vector<Replicated>;
static_assert(sizeof(ReplicatedVector) == 12);

// Appending one {object, priority} to the vector, as the slot 13 implementations do: through the
// out-of-line push_back of a default element (0041F790) or inline (the same, with the overflow
// path 00426B10), then storing into the new last element.
void PushReplicated(ReplicatedVector* v, void* object, std::uint8_t priority);
void PushReplicatedInline(ReplicatedVector* v, void* object, std::uint8_t priority);

// The session base (vtable 0073BF5C). 0x2568 bytes (the double at +0x20 aligns it to 8).
// Packets (the datagram's first byte): -1 master, 0 update, 1 leave, 3 get info (ping), 4 login,
// 5 info, 6 accepted, 7 rejected; each goes to its handler slot with the sender and a cursor after
// that byte.
class __declspec(novtable) SessionBase : public CRefCounted {
public:
    void RegisterTradeHandler(int channel, void* handler);
    void RegisterAwardHandler(int channel, void* handler);
    void RegisterChunkHandler(int channel, void* handler);
    void RegisterFileHandler(int channel, void* handler);
    void RegisterVariableHandler(int channel, void* handler);
    void RegisterMapHandler(int channel, void* handler);
    void RegisterMemberHandler(int channel, void* handler);
    void RegisterPartyHandler(int channel, void* handler);
    void RegisterRequestHandler(int channel, void* handler);
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073BF5C);

    // 0: 004368D0 deleting destructor (DeletingDestruct); 1: Serialize (empty).
    virtual void RemoveClient(ClientNode* node);                                // 2: 00436CF0 (the sentinel is ignored)
    void RemoveClientImpl(ClientNode* node);   // body of the slot above (registered by member pointer)
    virtual bool OnUpdate(const NetAddress* from, BufferCursor* cursor);      // 3: kind 0, 00437080
    bool OnUpdateImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    virtual void OnGetInfo(const NetAddress* from, BufferCursor* cursor);     // 4: kind 3, 004371E0 (logs an error)
    void OnGetInfoImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    virtual void OnLogin(const NetAddress* from, BufferCursor* cursor);       // 5: kind 4, 00437210 (logs an error)
    void OnLoginImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    virtual void OnLeave(const NetAddress* from, BufferCursor* cursor);       // 6: kind 1, 00437240
    void OnLeaveImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    virtual void OnInfo(const NetAddress* from, BufferCursor* cursor);        // 7: kind 5, 004372B0 (logs an error)
    void OnInfoImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    virtual void OnAccepted(const NetAddress* from, BufferCursor* cursor);    // 8: kind 6, 004372E0 (logs an error)
    void OnAcceptedImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    virtual void OnRejected(const NetAddress* from, BufferCursor* cursor);    // 9: kind 7, 00437310 (logs an error)
    void OnRejectedImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    virtual void OnMaster(const NetAddress* from, BufferCursor* cursor);      // 10: kind -1, 00437340 (logs an error)
    void OnMasterImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    virtual void OnClientLost(Connection* connection);                        // 11: 004132B0 (empty): timed out or a bad update
    virtual void OnClientLeft(Connection* connection);                        // 12: 004132C0 (empty): sent a leave packet
    // 13: 004376D0. Appends what the connection being updated (CurrentConnection) should have to **out.
    virtual void CollectReplicated(ReplicatedVector** out);
    // 14: 00437370 (net_session_tick.cpp). Advances the session clock by `dt`, hands the waiting
    // datagrams to the handler slots, ticks the connections (dropping timed-out ones) and counts
    // the frame.
    virtual void Tick(double dt);
    void TickImpl(double dt);   // body of the slot above (registered by member pointer)

    // 00436770. The base constructor body; returns this.
    SessionBase* Construct();
    // 00436A50 (net_session_tick.cpp). The destructor body: tells every client it leaves, then
    // destroys the handler tables, the socket, the type registry and the clients list.
    void Destruct();
    // 004368D0 (slot 0). Destruct, then operator delete when bit 0 of `flags` is set.
    SessionBase* DeletingDestruct(unsigned flags);
    // 004377D0. Appends `handler` to the handlers of message `type` (low byte) on `channel` (0..2;
    // other channels are ignored).
    void AddHandler(int channel, std::uint32_t type, void* handler);
    // 004142F0. Clears the reliable flags of message types 1..17 and registers their factories.
    void RegisterMessageTypes();
    // A handler registration as the session constructors inline it (not a function of its own in
    // the original): a prototype message T (net_messages.h) built by its constructor body, held
    // through a counted reference while its type id is looked up by vtable, then AddHandler.
    template <typename T>
    void AddHandlerFor(int channel, void* handler) {
        auto* memory = static_cast<T*>(NewRefObject(sizeof(T)));
        AddHandlerForPrototype(memory ? memory->Construct() : nullptr, channel, handler);
    }
    void AddHandlerForPrototype(Message* prototype, int channel, void* handler);

    // 00436CC0. The clients list node of the connection to `address` (the sentinel when none) into
    // `*out`; returns `out`.
    ClientNode** FindClient(ClientNode** out, const NetAddress* address);
    // 00436DD0. Logs it, removes a connection to the same address (slot 2), appends a new
    // connection with `id` and `address`; its send interval follows from `rate`.
    void AddClient(const NetAddress* address, std::uint32_t id, int rate);
    // 00436F60. Sends the buffer's contents to `address` through the socket.
    bool Send(const NetAddress* address, GameBuffer* buffer);
    // 00436F80. The send rate for a peer: 12500 on the local network, else 2500 (read from the
    // dwords at 00783E38 / 00783E34).
    std::uint32_t RateFor(const NetAddress* address);
    // 00436FA0. Sends a leave packet (kind 1, then the connection id) to `address`.
    void SendLeave(const NetAddress* address, std::uint32_t id);
    // The clients list node of the connection to `address`, or the sentinel (FindClient as the
    // slots inline it).
    ClientNode* ClientAt(const NetAddress* address);
    // The number of connections.
    std::uint32_t ClientCount() const;

    std::uint32_t magic_;               // +0x08 0; the game server and client set kSessionMagic
    std::int32_t timeout_;              // +0x0C 60: seconds of silence before a peer is dropped
    std::int32_t maxRoundTrip_;         // +0x10 5: a new connection's round trip estimate, the
                                        //       longest loss timeout and how long lost packets
                                        //       stay remembered (seconds)
    std::int32_t idReuseTime_;          // +0x14 75: seconds before a freed message id is reused
    std::uint32_t frame_;               // +0x18 1; Tick counts its calls
    std::uint32_t unused_1C;           // +0x1C not set by the constructors
    double time_;                       // +0x20 0; Tick adds the elapsed time
    ClientNode* clients_;               // +0x28 the connections (list sentinel, 12-byte nodes)
    TypeRegistry types_;                // +0x2C
    SocketDriver socket_;               // +0x54
    std::uint8_t reliable_[kMessageTypes];   // +0x64 per message type: its objects are sent reliably
    HandlerVector handlers_[kChannels][kMessageTypes];   // +0x164 / +0xD64 / +0x1964
    std::uint32_t padding_2564;         // +0x2564
};
static_assert(offsetof(SessionBase, magic_) == 0x08);
static_assert(offsetof(SessionBase, frame_) == 0x18);
static_assert(offsetof(SessionBase, time_) == 0x20);
static_assert(offsetof(SessionBase, clients_) == 0x28);
static_assert(offsetof(SessionBase, types_) == 0x2C);
static_assert(offsetof(SessionBase, socket_) == 0x54);
static_assert(offsetof(SessionBase, reliable_) == 0x64);
static_assert(offsetof(SessionBase, handlers_) == 0x164);
static_assert(sizeof(SessionBase) == 0x2568);

constexpr std::uint32_t kSessionMagic = 0xDEC0AD07;   // +8 of the game server and client

// ---- The server base and the game server ----------------------------------------------------------

// A peer told to join that has not sent an update yet (the server base's +0x2568 list).
struct PendingJoin {
    NetAddress address;
    std::uint32_t id;            // +0x10
    std::int32_t rate;           // +0x14
    float age;                   // +0x18 seconds (dropped past 0073BF9C)
};

// The server base (vtable 0073BBB4): answers pings (slot 4) and logins (slot 5), turns a pending
// join into a connection on its first update (slot 3).
class __declspec(novtable) ServerBase : public SessionBase {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073BBB4);

    // 0: 004132D0 deleting destructor (body 004132F0, DestructBase).
    bool OnUpdate(const NetAddress* from, BufferCursor* cursor) override;    // 3: 004434F0
    bool OnUpdateImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    void OnGetInfo(const NetAddress* from, BufferCursor* cursor) override;   // 4: 00443060 (a ping)
    void OnGetInfoImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    void OnLogin(const NetAddress* from, BufferCursor* cursor) override;     // 5: 00443080 (patched by Rebirth)
    void OnLoginImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    void Tick(double dt) override;                                           // 14: 00443590
    void TickImpl(double dt);   // body of the slot above (registered by member pointer)
    virtual void Describe(RefPtr* out) = 0;                                  // 15: this server's entry into *out
    // 16: 00443050 (admits everybody). Whether a client from `address` may join: `*password` (moved
    // past the password when one is checked) and `*reason` (1 full, 2 banned, 3 bad password).
    virtual bool Admit(std::uint32_t address, const std::uint8_t** password, std::uint8_t* reason);
    bool AdmitImpl(std::uint32_t address, const std::uint8_t** password, std::uint8_t* reason);   // body of the slot above (registered by member pointer)

    // 004132F0. The destructor body of this layer: the pending joins, then the session base.
    void DestructBase();
    // 00442C60. Answers a ping from `address` with this server's description (5, the echoed time,
    // the magic and the entry).
    void SendInfo(const NetAddress* address, std::uint32_t echo);

    stlp::list<PendingJoin> joins_;     // +0x2568 (constructor 0041FD30)
    std::uint32_t unused_256C;         // +0x256C not set
};
static_assert(offsetof(ServerBase, joins_) == 0x2568);
static_assert(sizeof(ServerBase) == 0x2570);

// The game server (vtable 0073BB70). 0x2600 bytes, pointer at 0079B5DC (GameServer()).
// The lists are STLport lists of counted pointers (value at +8, or at +0xC after a float delay).
class __declspec(novtable) ServerSession : public ServerBase {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073BB70);

    // 0: 00417A10 deleting destructor (DeletingDestruct).
    void OnMaster(const NetAddress* from, BufferCursor* cursor) override;    // 10: 0041ACA0
    void OnMasterImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    void OnClientLost(Connection* connection) override;                       // 11: 0041A700 (text 4)
    void OnClientLostImpl(Connection* connection);   // body of the slot above (registered by member pointer)
    void OnClientLeft(Connection* connection) override;                       // 12: 0041A770 (text 1)
    void OnClientLeftImpl(Connection* connection);   // body of the slot above (registered by member pointer)
    void CollectReplicated(ReplicatedVector** out) override;                  // 13: 00417EB0
    void CollectReplicatedImpl(ReplicatedVector** out);   // body of the slot above (registered by member pointer)
    void Tick(double dt) override;                                            // 14: 004182E0
    void TickImpl(double dt);   // body of the slot above (registered by member pointer)
    void Describe(RefPtr* out) override;                                      // 15: 0041A7E0
    void DescribeImpl(RefPtr* out);   // body of the slot above (registered by member pointer)
    bool Admit(std::uint32_t address, const std::uint8_t** password, std::uint8_t* reason) override;   // 16: 0041A050
    bool AdmitImpl(std::uint32_t address, const std::uint8_t** password, std::uint8_t* reason);   // body of the slot above (registered by member pointer)

    // 00412D80. The constructor (the caller allocated 0x2600 bytes and cleared +4); returns this.
    ServerSession* Construct(std::uint32_t kind);
    // 00417BC0 (net_server_life.cpp). The destructor body (the server's own vtable is not stored first).
    void Destruct();
    // 00417A10 (slot 0). Destruct, then operator delete when bit 0 of `flags` is set.
    ServerSession* DeletingDestruct(unsigned flags);
    // 004186F0 (net_server_map.cpp). Switches the server to map `name`.
    void SetMap(const stlp::string* name);
    // 00418530. Queues a map message for each zone that follows the current map.
    void QueueMapList();
    // 004193F0. When the held map message names `name`: drops it and queues "name_3" instead.
    bool FinishMap(const stlp::string* name);
    // 00419570 .. 00419940 (net_server_vars.cpp): the script variables (named floats).
    RefNode** FindVariable(RefNode** out, const stlp::string* name);
    RefNode** FindOrAddVariable(RefNode** out, const stlp::string* name);
    void SetVariable(const stlp::string* name, float value);      // zero removes it
    void RaiseVariable(const stlp::string* name, float value);    // to the larger value
    void RemoveVariable(const stlp::string* name);
    double GetVariable(const stlp::string* name);                 // 0 when missing
    // 00418ED0 (net_server_awards.cpp). The zone completion command ("<zone>_3"): the clients'
    // shares of the zone's two values and the host's items, then the map state is dropped.
    void CompleteZone(const stlp::string* zone);
    // 00418CC0. The zone leaving command ("<zone>_2"): the host's items with the final award, then
    // the map state is dropped and the world's map cleared.
    void LeaveZone();
    // 004191C0. Queues the host's item awards of map registry `entry` (and, with `withFinal`, the
    // final award).
    void AwardHost(const std::uint8_t* entry, bool withFinal);
    // 00419D90. Queues an award message for `connection` (60 s delay); destroys `text`.
    void QueueAward(CRefObject* connection, int kind, float value, stlp::string text);
    // 00419A00 (net_server_files.cpp). Lists `data` under `name` (unless listed) as a file message
    // with 64-byte chunk messages.
    void ShareFile(const stlp::string* name, GameBuffer* data);
    // 00417E10 (net_server_entry.cpp). Whether the server is announced to the master server (port 28004).
    void SetPublic(bool announce);
    // 0041AA10 (net_server_handlers.cpp, A-E1; EI_Plugin's NewMaster patches its send at 0041ABDA,
    // so it is hooked only where that feature is off). Sends the master server this game's record:
    // the name, the played map's title (empty without a map), the players and their maximum, the
    // game kind, whether a password is set, +0x25FC, the dword the master last sent (+0x2578) and
    // the session magic.
    void AnnounceToMaster();
    // 0041A310 (net_server_chat.cpp). Keeps a chat or system line for the clients (`player` null
    // for the server's own; at most 50 lines).
    void Announce(const char* text, CRefObject* player);
    // 0041A490. Announce with text `kind` of the text table formatted with `text`.
    void AnnounceText(int kind, const char* text, CRefObject* player);
    // 0041ACF0 (net_server_trade.cpp). The trade between two offers (either order), or null.
    void* FindTrade(void* offer, void* other);
    // 0041A020 .. 0041A770 (net_server_players.cpp): players, connections, admission.
    int LeaderId();                                       // 0041A020: the first host's id, -1
    void SetPassword(const char* text);                   // 0041A140
    void* FindGuestConnection(const stlp::string* name);     // 0041A170
    void* FindConnectionBySerial(int serial);             // 0041A270
    void* FindPlayer(int id);                             // 0041A2E0
    void AnnounceToGuests(int kind, const char* text);    // 0041A500
    void Kick(void* connection);                          // 0041A550
    void Ban(void* connection);                           // 0041A620
    void ReportDeparture(void* connection, int text);     // slots 11 (text 4) and 12 (text 1)
    bool IsHostConnection(void* connection);              // 00419F90 (net_server_commands.cpp)
    // 00417E40 (net_server_tick.cpp). Whether the player bound to `connection` is in state 3.
    bool PlayerStateIs3(void* connection);

    float announceTimer_;               // +0x2570 seconds to the next announcement to the master server
    std::uint8_t public_;               // +0x2574 announced to the master server
    std::uint8_t unused_2575[3];
    std::uint32_t masterToken_;         // +0x2578 0; the dword the master server last sent (OnMaster), sent back
                                        // with the next announcement (GameInfoRecord::masterToken)
    NetAddress masterAddress_;          // +0x257C the master server (port 28004)
    std::uint32_t nextLine_;            // +0x258C the number of the next chat line
    stlp::SharedRefList players_;       // +0x2590 player records (0073BC70)
    stlp::SharedRefList lines_;         // +0x2594 chat and system lines (0073BC88), at most 50
    stlp::SharedRefList files_;         // +0x2598 shared files (0073BD18)
    stlp::CountedRefList fileRequests_; // +0x259C the clients' requests (0073BC58: file, connection)
    // +0x25A0 where the game is hosted (a HostMessage, net_messages.h; world_game.cpp's AnnounceHost),
    // replicated to everyone when set
    RefPtr hostInfo_;
    stlp::SharedRefList bindings_;      // +0x25A4 player <-> connection (0073BCB8)
    RefPtr map_;                        // +0x25A8 the map message (0073BD00) while a map is played
    stlp::SharedRefList queuedMaps_;    // +0x25AC map messages (0073BCD0) the host may pick next
    stlp::SharedRefList variables_;     // +0x25B0 script variables (0073BCE8)
    SharedTimedList awards_;            // +0x25B4 {delay, award (0073BD48)}
    stlp::CountedRefList offers_;       // +0x25B8 the players' trade offers (party messages 0073BD78)
    stlp::CountedRefList trades_;       // +0x25BC trades (0073BD60)
    CountedTimedList finishedTrades_;   // +0x25C0 {delay, trade}: done trades, kept 60 s
    stlp::list<NetAddress> banned_;     // +0x25C4 banned IPv4 addresses
    stlp::string name_;                    // +0x25C8 the game's name
    stlp::string mapName_;                 // +0x25D4 the current map
    std::uint32_t maxPlayers_;          // +0x25E0 8; set by the creator (0057E740)
    std::uint32_t kind_;                // +0x25E4 the constructor's argument: the game kind
    std::uint8_t password_[0x14];       // +0x25E8 the join password (empty: none)
    std::uint32_t masterKey_;           // +0x25FC set by the creator (StartServer); announced, and the master
                                        // server's packets must start with it (OnMaster)
};
static_assert(offsetof(ServerSession, announceTimer_) == 0x2570);
static_assert(offsetof(ServerSession, masterAddress_) == 0x257C);
static_assert(offsetof(ServerSession, players_) == 0x2590);
static_assert(offsetof(ServerSession, bindings_) == 0x25A4);
static_assert(offsetof(ServerSession, banned_) == 0x25C4);
static_assert(offsetof(ServerSession, name_) == 0x25C8);
static_assert(offsetof(ServerSession, maxPlayers_) == 0x25E0);
static_assert(offsetof(ServerSession, password_) == 0x25E8);
static_assert(sizeof(ServerSession) == 0x2600);

// ---- The client base and the game client ----------------------------------------------------------

// The client base (vtable 0073BE30): the servers found (by broadcast pings, by asking known
// addresses, from the master server's list) and the join request to one of them.
class __declspec(novtable) ClientBase : public SessionBase {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073BE30);

    enum JoinState : std::uint32_t { kNotJoined = 0, kAsking = 1, kJoined = 2 };

    // 0: 0041C450 deleting destructor (body 0041C530, DestructBase).
    void RemoveClient(ClientNode* node) override;                               // 2: 004420E0
    void RemoveClientImpl(ClientNode* node);   // body of the slot above (registered by member pointer)
    bool OnUpdate(const NetAddress* from, BufferCursor* cursor) override;      // 3: 004418F0
    bool OnUpdateImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    void OnInfo(const NetAddress* from, BufferCursor* cursor) override;        // 7: 00441D40
    void OnInfoImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    void OnAccepted(const NetAddress* from, BufferCursor* cursor) override;    // 8: 00441940
    void OnAcceptedImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    void OnRejected(const NetAddress* from, BufferCursor* cursor) override;    // 9: 00441B40
    void OnRejectedImpl(const NetAddress* from, BufferCursor* cursor);   // body of the slot above (registered by member pointer)
    void Tick(double dt) override;                                             // 14: 00442AD0
    void TickImpl(double dt);   // body of the slot above (registered by member pointer)
    virtual void OnJoined();                                                   // 15: 0041C440 (empty)
    virtual void CreateEntry(RefPtr* out) = 0;                                 // 16: a new server entry into *out

    // 0041C530 (net_client_life.cpp). The destructor body of this layer, then the session base.
    void DestructBase();
    // 00441660. A ping (3, the session time) to `address`.
    void SendPing(const NetAddress* address);
    // 00441740. A join request (4, magic, join id, the send rate for the host, then `data`).
    void SendJoin(const NetAddress* address, GameBuffer* data);
    // 00442180. Pings every broadcast address, then the first waiting address, or else the next
    // query in turn (and the always-asked address is queued again when set).
    void Refresh();
    // 004422D0. Sets the address asked on every refresh.
    void SetAlwaysAsk(const NetAddress* address);
    // 00442300. Takes over the queries (from the options); unlisted servers are listed as asked.
    void SetQueries(const ClientQueries* from);
    // 00442590. Asks the server at `address` directly.
    void Ask(const NetAddress* address);
    // 004427F0. Forgets the server at `address`: its query and every list entry with that address.
    void Forget(const NetAddress* address);
    // 004428A0. A server from the master server's list.
    void AddServer(ServerEntry* entry);
    // 00442980. Starts joining the host at `address` with `data` sent along.
    void Join(const NetAddress* address, const GameBuffer* data);
    // 00442A40. The entry of the `index`th server found, or null.
    ServerEntry* ServerAt(int index);

    NetAddress host_;                   // +0x2568 the host joined or being asked
    GameBuffer joinData_;               // +0x2578 sent with every join request (a growing buffer)
    std::uint32_t joinId_;              // +0x2594 rand() << 16 | tick count
    std::uint32_t joinState_;           // +0x2598 JoinState
    std::uint8_t refusal_;              // +0x259C the reason the host gave
    std::uint8_t unused_259D[3];
    float retryTimer_;                  // +0x25A0 seconds to the next join request
    float retryInterval_;               // +0x25A4 doubles with every request (0.1 first)
    stlp::SharedRefList servers_;       // +0x25A8 the servers found (ServerEntry)
    ClientQueries queries_;             // +0x25AC servers asked directly
    stlp::list<NetAddress> waiting_;    // +0x25BC addresses waiting to be asked
    NetAddress alwaysAsk_;              // +0x25C0 asked on every refresh (ip 0: none)
    std::uint32_t nextQuery_;           // +0x25D0 the query the refresh pings next
    std::uint32_t unused_25D4;         // +0x25D4 not set
};
static_assert(offsetof(ClientBase, host_) == 0x2568);
static_assert(offsetof(ClientBase, joinData_) == 0x2578);
static_assert(offsetof(ClientBase, joinState_) == 0x2598);
static_assert(offsetof(ClientBase, servers_) == 0x25A8);
static_assert(offsetof(ClientBase, queries_) == 0x25AC);
static_assert(offsetof(ClientBase, alwaysAsk_) == 0x25C0);
static_assert(offsetof(ClientBase, nextQuery_) == 0x25D0);
static_assert(sizeof(ClientBase) == 0x25D8);

// An embedded CParty (party.h; constructor 00661050, destructor 006611D0): its bytes, as CPlayer
// keeps its party (the class is abstract in C++). The client keeps two (slot 2, Create, makes their
// managers), a trade record three.
struct PartyStorage {
    alignas(4) std::uint8_t bytes[0x70];   // sizeof(CParty), checked in net_client.cpp

    CParty* Party() { return reinterpret_cast<CParty*>(bytes); }
};
static_assert(sizeof(PartyStorage) == 0x70);

// A finished trade of the client's (ClientSession::tradeRecords_, 0x160-byte nodes; its list's
// element policy is record_lists.h's TradeElement): the revisions of the trade and copies of the
// two parties and the traded one.
struct TradeRecord {
    std::uint32_t acceptedRevision;   // our offer's accepted revision of the partner's
    std::uint32_t revision;           // our offer's revision
    PartyStorage parties[3];          // ours, the partner's, the traded party
};
using TradeRecordNode = stlp::list<TradeRecord>::Node;
static_assert(sizeof(TradeRecordNode) == 0x160);

// The game client (vtable 0073BDEC). 0x2700 bytes, pointer at 0079B5D8 (GameClient()). It keeps
// what the server replicates to it, by class, in STLport lists of counted pointers (value at +8).
class __declspec(novtable) ClientSession : public ClientBase {
public:
    // Body of the slot at 0041D9A0 (registered by member pointer).
    double BufferRatioImpl(GameBuffer* buffer);
    // Body of the slot at 0041D600 (registered by member pointer).
    void ReadServerListImpl(GameBuffer* in);
    // 0x0041E270 (net_client_handlers.cpp).
    void StartParty(CRefObject* partner);
    // 0x0041E0A0 (net_client_handlers.cpp).
    void RecordTrade(const void* party);
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073BDEC);

    // 0: 0041C8B0 deleting destructor (DeletingDestruct).
    void CollectReplicated(ReplicatedVector** out) override;   // 13: 0041D050
    void CollectReplicatedImpl(ReplicatedVector** out);   // body of the slot above (registered by member pointer)
    void Tick(double dt) override;                             // 14: 0041CCD0
    void TickImpl(double dt);   // body of the slot above (registered by member pointer)
    void OnJoined() override;                                  // 15: 0041D410
    void CreateEntry(RefPtr* out) override;                    // 16: 0041D420
    void CreateEntryImpl(RefPtr* out);   // body of the slot above (registered by member pointer)

    // 0041BC40. The constructor (the caller allocated 0x2700 bytes and cleared +4); returns this.
    ClientSession* Construct();
    // 0041CA10 (net_client_life.cpp). The destructor body (the client's own vtable is not stored first).
    void Destruct();
    // 0041C8B0 (slot 0). Destruct, then operator delete when bit 0 of `flags` is set.
    ClientSession* DeletingDestruct(unsigned flags);
    // 0041CD80 (net_client_awards.cpp). Applies the awards received: experience, gold, items.
    void ApplyAwards();
    // 0041D1A0 (net_client_commands.cpp). Keeps `object` on the timers list for `seconds`.
    void AddTimed(CRefObject* object, std::uint32_t seconds);
    // 0041D260. A command message (0073BC40 {kind, text}) for the server, kept 60 s.
    void SendCommand(int kind, const char* text);

    stlp::CountedRefList players_;      // +0x25D8 player records (0073BC70)
    stlp::CountedRefList lines_;        // +0x25DC chat lines (0073BC88)
    stlp::CountedRefList files_;        // +0x25E0 shared files (0073BD18)
    stlp::CountedRefList chunks_;       // +0x25E4 file chunks (0073BD30)
    stlp::SharedRefList fileRequests_;  // +0x25E8 file requests for the server (0073BC58)
    CRefObject* info_;                  // +0x25EC the game's description (0073BCA0), counted
    CRefObject* binding_;               // +0x25F0 our binding (0073BCB8), counted
    CRefObject* map_;                   // +0x25F4 the map message (0073BD00), counted
    stlp::CountedRefList zones_;        // +0x25F8 map messages the host may pick (0073BCD0)
    stlp::CountedRefList variables_;    // +0x25FC script variables (0073BCE8)
    RefPtr player_;                     // +0x2600 the local player's record (0073BBF8)
    SharedTimedList timers_;            // +0x2604 {seconds left, object}: objects kept for a while
    stlp::CountedRefList awards_;       // +0x2608 awards received, applied by ApplyAwards (0073BD48)
    RefPtr party_;                      // +0x260C our trade offer (party description 0073BD78)
    stlp::CountedRefList offers_;       // +0x2610 the other players' trade offers (0073BD78)
    stlp::list<TradeRecord> tradeRecords_;   // +0x2614 the trades done
    CRefObject* trade_;                 // +0x2618 the current trade (0073BD60), counted
    std::uint32_t gameType_;            // +0x261C the multiplayer game type 0..3 (StartServer 0057E740): start zone, type variable
    PartyStorage parties_[2];           // +0x2620 ours (published at +0x260C), +0x2690 the partner's
};
static_assert(offsetof(ClientSession, players_) == 0x25D8);
static_assert(offsetof(ClientSession, info_) == 0x25EC);
static_assert(offsetof(ClientSession, player_) == 0x2600);
static_assert(offsetof(ClientSession, party_) == 0x260C);
static_assert(offsetof(ClientSession, tradeRecords_) == 0x2614);
static_assert(offsetof(ClientSession, parties_) == 0x2620);
static_assert(sizeof(ClientSession) == 0x2700);

// ---- The game announcement ------------------------------------------------------------------------

// The game announcement the server builds in 0041AA10 (written by 00401000) and the client reads
// in 0041D600 (00401170). Field meanings from 0041AA10: player count, maximum players, the
// session magic. Names provisional.
struct GameInfoRecord {
    stlp::string name;              // +0x00
    stlp::string description;       // +0x0C
    std::uint8_t players;        // +0x18 number of players
    std::uint8_t maxPlayers;     // +0x19 the server's maxPlayers_
    std::uint8_t kind;           // +0x1A the server's kind_ (ServerEntry::kind_)
    std::uint8_t hasPassword;    // +0x1B joining needs a password (ServerEntry::password_)
    std::uint32_t masterKey;     // +0x1C the server's masterKey_
    std::uint32_t masterToken;   // +0x20 the server's masterToken_
    std::uint32_t magic;         // +0x24 kSessionMagic

    void Write(GameBuffer* buffer) const;   // 00401000
    int Read(GameBuffer* buffer);           // 00401170: 1 unless the buffer ran short
};
static_assert(sizeof(GameInfoRecord) == 0x28);

// The byte buffer's read helpers (net_game_info.cpp).
GameBuffer* __fastcall BufferCopyOut(GameBuffer* buffer, void* edx, void* to, unsigned size);   // 00401310
unsigned __fastcall BufferRead(GameBuffer* buffer, void* edx, void* to, unsigned size);         // 00401340
// net_session_tick.cpp.
void __fastcall BufferUpdateHigh(GameBuffer* buffer);                                          // 00419CC0

} // namespace net
