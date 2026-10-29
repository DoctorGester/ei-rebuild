#pragma once

// The multiplayer session classes and the message base; see docs/notes/network.md and
// docs/notes/cleanup-network.md. SessionBase holds the UDP socket, the connections, the message type
// registry and the handler tables; ServerBase / ClientBase add the lobby side (pending joins, finding
// servers) under ServerSession / ClientSession, the game server and client (g_gameServer, g_gameClient).
// Message (below) is the replicated object base, ServerEntry a server as the lobby lists it.

#include "core_refcount.h"
#include "net_coder.h"
#include "party_storage.h"
#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/in_place.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>
#include <unordered_map>
#include <vector>

struct QuestBriefingRecord;

namespace console { class Stream; }

namespace net {

class Connection;
class Message;
class BindingMessage;
class HostMessage;
class LoginMessage;
class MapMessage;
class PartyMessage;
class TradeMessage;
class AwardMessage;
class FileChunkMessage;
class FileMessage;
class FileRequestMessage;
class LineMessage;
class PlayerMessage;
class VariableMessage;
class ZoneMessage;

// A new counted object of the network code: no references and no flags, or null without memory.
template <typename T, typename... Args>
T* NewCounted(const Args&... args) {
    T* object = new T(args...);
    if (object) {
        object->refs_ = 0;
        object->flags6_ = 0;
        object->flags7_ = 0;
    }
    return object;
}

// `self` destroyed and built again in place (the count and the flags kept): OnShutdown of the messages and
// the server entries.
template <typename T>
void Rebuild(T* self) {
    self->T::~T();
    new (self) T();
}

// A sockaddr_in: family 2, then the port and the IPv4 address in network order, 8 zero bytes.
struct NetAddress {
    std::uint16_t family;
    std::uint16_t port;
    std::uint32_t ip;
    std::uint8_t zero[8];

    // This address from "host[:port]" (an IPv4 number or a host name looked up); `defaultPort` unless the
    // text names one. False when the host is unknown.
    bool Parse(const char* text, int defaultPort);
    // "a.b.c.d:port" (or, with `resolve`, "hostname:port" when the look-up succeeds).
    std::string Format(int resolve) const;
    // Format without the look-up.
    std::string ToString() const;
};
static_assert(sizeof(NetAddress) == 0x10);

// The TCP link to the master server. The master request box (ui_screens.h) holds one.
class MasterLink {
public:
    // A non-blocking TCP socket connecting to `address` (state 1), or state 0.
    explicit MasterLink(const void* address);
    ~MasterLink();
    MasterLink(const MasterLink&) = delete;
    MasterLink& operator=(const MasterLink&) = delete;

    enum State : std::int32_t { kClosed = 0, kConnecting = 1, kReceiving = 2, kDone = 3, kSending = 4 };

    GrowingBuffer out;           // what is sent once connected
    GrowingBuffer in;
    std::uint32_t socket;        // (-1: none)
    std::int32_t state;
};

// ---- The globals of the session code ------------------------------------------------------------

// The send rates in bytes per second (the console's rate / localrate set them): to a remote address (2500;
// Rebirth's SetDefaultRate, disabled here, writes Settings\Rate over it) and on the local network (12500).
extern int g_remoteRate;
extern int g_localRate;

class SessionBase;
class ServerSession;
class ClientSession;

// The game's UDP port (8888; the harness writes a free port of its own, tests/verify/harness_net.h).
inline int g_gamePort = /*00783E30*/ {8888};
// The network log (the net stream; null when logging is off).
inline console::Stream* g_netLog = /*0079B91C*/ {nullptr};
// The session being updated (set by Tick / Receive and by the connection code).
inline SessionBase* g_currentSession = /*0079B920*/ {nullptr};
// The connection being updated or read (null outside the session code).
inline Connection* g_currentConnection = /*0079B924*/ {nullptr};
// The buffer every update packet is built in and the one datagrams are received into (net_session.cpp).
extern GrowingBuffer g_sendBuffer;
extern GrowingBuffer g_receiveBuffer;
// The game client and the game server (null when there is none): counted pointers, cleared by their static
// initialisers and released by the exit functions those register (net_session.cpp).
inline Ref<ClientSession> g_gameClient = /*0079B5D8*/ {};
inline Ref<ServerSession> g_gameServer = /*0079B5DC*/ {};
// The game client's trade offer: publish ours, find the partner's offer that pairs with ours (a
// PartyMessage, or null), take the acceptance back, drop the offer.
void ClientPublishParty(ClientSession* self);
PartyMessage* ClientFindParty(ClientSession* self);
void ClientCommitParty(ClientSession* self);
void ClientDropParty(ClientSession* self);

// ---- The message base ---------------------------------------------------------------------------

// A replicated network object: a connection sends it again whenever its version differs from the one last
// sent. OnShutdown resets the message (destroyed and built again in place), Clone copies it.
class Message : public CRefObject {
public:
    ~Message() override;
    virtual void Write(BufferCursor* cursor) = 0;   // the fields at the cursor's byte position
    virtual void Read(BufferCursor* cursor) = 0;

    std::uint32_t version_ = 1;   // 1 when constructed; raised by every change

protected:
    Message();
    Message(const Message& other) : CRefObject(), version_(other.version_) {}
};

using MessageFactory = Message* (*)();

// ---- The server entries of the lobby ------------------------------------------------------------

class ServerEntryBase : public CRefObject {
public:
    ~ServerEntryBase() override;
    virtual void Write(BufferCursor* cursor) = 0;
    virtual void Read(BufferCursor* cursor) = 0;

    enum State : std::uint32_t {
        kLocal = 0,        // found by a broadcast ping
        kAsked = 1,        // asked directly
        kFromList = 2,     // from the master server's list
    };

    NetAddress address_;
    float timeout_;          // seconds until the entry is checked again (1e38: never)
    float ping_;             // seconds; a constant when unknown
    std::uint8_t foreign_;   // the reply's echoed word differs from the magic
    std::uint32_t state_;

protected:
    ServerEntryBase();
    ServerEntryBase(const ServerEntryBase& other);
};

// A server entry: what a server says about itself, and what the client lists hold.
class ServerEntry final : public ServerEntryBase {
public:
    ServerEntry();
    ServerEntry(const ServerEntry& other);
    ~ServerEntry() override {}
    void OnShutdown() override;   // the result is undefined
    ServerEntry* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    std::string name_;
    std::uint32_t kind_;          // the game kind (4: none)
    std::string description_;        // the current map's title
    std::uint8_t players_;
    std::uint8_t maxPlayers_;
    std::uint8_t password_;       // whether joining needs a password
};

// A server the client asks directly: its address and the reply last seen.
struct Query {
    Query();
    Query(const Query& from);
    Query& operator=(const Query& from) = default;
    ~Query();
    NetAddress address;
    GrowingBuffer reply;
};

// The client's queries: a vector of queries and a dword saved with it (not itself a vector).
struct ClientQueries {
    std::vector<Query> list;
    std::uint32_t extra = 0;

    // The queries in a chunk archive (chunk 1 of chunks 1: field 1 the
    // address, 2 the reply's length, 3 the reply), then field 2 the extra dword.
    void Serialize(ChunkArchive* archive);
    // The query with `address`, or the end.
    std::vector<Query>::iterator QueriesFind(const NetAddress* address);
};

// ---- The session base ---------------------------------------------------------------------------

constexpr int kChannels = 3;          // handler channels: 0 created, 1 changed, 2 removed
constexpr int kMessageTypes = 0x100;

// A message handler as the session keeps it: its session and the message. The handlers themselves take their
// own session class and message class (net_handlers.h); AsHandler is the one place they become this.
using MessageHandler = void(*)(SessionBase*, CRefObject*);
template <typename Session, typename MessageType>
MessageHandler AsHandler(void(*handler)(Session*, MessageType*)) {
    return reinterpret_cast<MessageHandler>(handler);
}

// The message type registry of a session.
struct TypeRegistry {

    std::unordered_map<std::uint32_t, std::uint32_t> byVtable;     // message vtable -> type id
    std::unordered_map<std::uint32_t, MessageFactory> byType;         // type id -> factory

    // The key of a message's class: its vtable (the object's first dword).
    static std::uint32_t KeyOf(const CRefObject* message) {
        std::uint32_t key;
        lib::LoadBytes(key, message);   // read before any null check
        return key;
    }
    // The type id of the message class whose key is *key, or -1.
    int TypeOf(const std::uint32_t* key) const;
    // Registers `type`: makes a prototype with `factory` (to learn its vtable), maps the
    // vtable to the type and the type to the factory, and releases the prototype.
    void Register(std::uint32_t type, MessageFactory factory);
    // A new message of `type` from its factory (operator[] on byType: an unknown type
    // gets an empty entry), or null.
    Message* Create(std::uint32_t type);
};

// The UDP socket of a session and the broadcast addresses of the host.
struct SocketDriver {
    // Starts Winsock and notes the broadcast address of the host's first address.
    SocketDriver();
    // Closes the socket and stops Winsock.
    ~SocketDriver();
    SocketDriver(const SocketDriver&) = delete;
    SocketDriver& operator=(const SocketDriver&) = delete;

    std::uint32_t socket;        // SOCKET (-1: none)
    std::vector<NetAddress> addresses;

    // A new non-blocking broadcast UDP socket, bound to `port` when positive.
    bool Open(int port);
    // The `index`th broadcast address with `port` into `out`; false past the last.
    bool Broadcast(NetAddress* out, unsigned index, int port);
    // Whether `address` is the loopback or on the network of a broadcast address.
    bool IsLocal(const NetAddress* address);
    // Sends the buffer's contents to `address`; true when all of it went.
    bool SendTo(const NetAddress* address, GameBuffer* buffer);
    // The next datagram (up to 0x800 bytes) into `buffer`, its sender into `from`.
    bool Receive(NetAddress* from, GameBuffer* buffer);
};

// The value of the timed lists (the client's timers, the server's awards and finished trades): the
// seconds left (a float; the code that queues an entry stores a constant's bits) and a counted object, held
// shared (the client's timers, the server's awards) or counted (the server's finished trades). A copy copies
// the key, then the reference.
template <typename Ref>
struct TimedEntry {
    TimedEntry(std::uint32_t bits, Ref object) : key(bits), ref(std::move(object)) {}

    union {
        float seconds;
        std::uint32_t key;   // the seconds' bits, as the lists copy them
    };
    Ref ref;
};

// What a session replicates to the connection being updated (CollectReplicated fills it).
struct Replicated {
    Message* object;           // replicated while it is listed
    std::uint8_t priority;     // 0..10
};

// Appends one {object, priority}.
inline void PushReplicated(std::vector<Replicated>* v, void* object, std::uint8_t priority) {
    v->push_back({static_cast<Message*>(object), priority});
}

// Packets (the datagram's first byte): -1 master, 0 update, 1 leave, 3 get info (ping), 4 login, 5 info, 6
// accepted, 7 rejected; each goes to its handler slot with the sender and a cursor after that byte.
class SessionBase : public CRefCounted {
public:
    void RegisterTradeHandler(int channel, MessageHandler handler);
    void RegisterAwardHandler(int channel, MessageHandler handler);
    void RegisterChunkHandler(int channel, MessageHandler handler);
    void RegisterFileHandler(int channel, MessageHandler handler);
    void RegisterVariableHandler(int channel, MessageHandler handler);
    void RegisterMapHandler(int channel, MessageHandler handler);
    void RegisterMemberHandler(int channel, MessageHandler handler);
    void RegisterPartyHandler(int channel, MessageHandler handler);
    void RegisterRequestHandler(int channel, MessageHandler handler);

    // The count and the flags are the creator's.
    SessionBase();
    ~SessionBase() override;
    virtual void RemoveClient(std::list<SharedRefTo<Connection>>::iterator node);                     // end() is ignored
    virtual bool OnUpdate(const NetAddress* from, BufferCursor* cursor);      // kind 0
    virtual void OnGetInfo(const NetAddress* from, BufferCursor* cursor);     // kind 3 (logs an error)
    virtual void OnLogin(const NetAddress* from, BufferCursor* cursor);       // kind 4 (logs an error)
    virtual void OnLeave(const NetAddress* from, BufferCursor* cursor);       // kind 1
    virtual void OnInfo(const NetAddress* from, BufferCursor* cursor);        // kind 5 (logs an error)
    virtual void OnAccepted(const NetAddress* from, BufferCursor* cursor);    // kind 6 (logs an error)
    virtual void OnRejected(const NetAddress* from, BufferCursor* cursor);    // kind 7 (logs an error)
    virtual void OnMaster(const NetAddress* from, BufferCursor* cursor);      // kind -1 (logs an error)
    virtual void OnClientLost(Connection*) {}                                 // timed out or a bad update
    virtual void OnClientLeft(Connection*) {}                                 // sent a leave packet
    // Empty here. Appends what the connection being updated (CurrentConnection) should have to **out.
    virtual void CollectReplicated(std::vector<Replicated>**) {}
    // Advances the session clock by `dt`, hands the waiting datagrams to the handlers, ticks the connections
    // (dropping timed-out ones) and counts the frame.
    virtual void Tick(double dt);

    // Appends `handler` to the handlers of message `type` (low byte) on `channel` (0..2;
    // other channels are ignored).
    void AddHandler(int channel, std::uint32_t type, MessageHandler handler);
    // Clears the reliable flags of message types 1..17 and registers their factories.
    void RegisterMessageTypes();
    // A handler registration as the session constructors do it: a prototype message T, held through a
    // counted reference while its type id is looked up by its class, then AddHandler.
    template <typename T>
    void AddHandlerFor(int channel, MessageHandler handler) {
        AddHandlerForPrototype(NewCounted<T>(), channel, handler);
    }
    void AddHandlerForPrototype(Message* prototype, int channel, MessageHandler handler);

    // The connection to `address` in clients_, end() when none.
    std::list<SharedRefTo<Connection>>::iterator FindClient(const NetAddress* address);
    // Logs it, removes a connection to the same address, appends a new connection with `id` and `address`;
    // its send interval follows from `rate`.
    void AddClient(const NetAddress* address, std::uint32_t id, int rate);
    bool Send(const NetAddress* address, GameBuffer* buffer);
    // The send rate for a peer: g_localRate on the local network, else g_remoteRate.
    std::uint32_t RateFor(const NetAddress* address);
    // Sends a leave packet (kind 1, then the connection id) to `address`.
    void SendLeave(const NetAddress* address, std::uint32_t id);
    std::uint32_t ClientCount() const;

    std::uint32_t magic_;               // 0; the game server and client set kSessionMagic
    std::int32_t timeout_;              // 60: seconds of silence before a peer is dropped
    std::int32_t maxRoundTrip_;         // 5: a new connection's round trip estimate, the longest loss
                                        // timeout and how long lost packets stay remembered (seconds)
    std::int32_t idReuseTime_;          // 75: seconds before a freed message id is reused
    std::uint32_t frame_;               // 1; Tick counts its calls
    double time_;                       // 0; Tick adds the elapsed time
    std::list<SharedRefTo<Connection>> clients_;                // the connections
    TypeRegistry types_;
    SocketDriver socket_;
    std::uint8_t reliable_[kMessageTypes];   // per message type: its objects are sent reliably
    std::vector<MessageHandler> handlers_[kChannels][kMessageTypes];
    std::uint32_t padding_2564;
};

constexpr std::uint32_t kSessionMagic = 0xDEC0AD07;

// ---- The server base and the game server ----------------------------------------------------------

// A peer told to join that has not sent an update yet (ServerBase::joins_).
struct PendingJoin {
    NetAddress address;
    std::uint32_t id;
    std::int32_t rate;
    float age;                   // seconds (dropped past the join timeout)
};

// The server base: answers pings and logins, turns a pending join into a connection on its first update.
class ServerBase : public SessionBase {
public:

    // The pending joins, the socket bound to the game port.
    ServerBase();
    ~ServerBase() override;
    bool OnUpdate(const NetAddress* from, BufferCursor* cursor) override;
    void OnGetInfo(const NetAddress* from, BufferCursor* cursor) override;   // a ping
    void OnLogin(const NetAddress* from, BufferCursor* cursor) override;     // patched by Rebirth
    void Tick(double dt) override;
    virtual void Describe(RefPtr* out) = 0;                                  // this server's entry into *out
    // Admits everybody. Whether a client from `from` may join: `*password` (the login cursor's next byte,
    // moved past the password when one is checked) and `*reason` (1 full, 2 banned, 3 bad password).
    virtual bool Admit(const NetAddress* from, std::uint8_t** password, std::uint8_t* reason);

    // Answers a ping from `address` with this server's description (5, the echoed time,
    // the magic and the entry).
    void SendInfo(const NetAddress* address, std::uint32_t echo);

    std::list<PendingJoin> joins_;
};

// The game server (g_gameServer). Its lists are STLport lists of counted pointers.
class ServerSession final : public ServerBase {
public:
    explicit ServerSession(std::uint32_t kind);
    ~ServerSession() override;
    void OnMaster(const NetAddress* from, BufferCursor* cursor) override;
    void OnClientLost(Connection* connection) override;                       // text 4
    void OnClientLeft(Connection* connection) override;                       // text 1
    void CollectReplicated(std::vector<Replicated>** out) override;
    void Tick(double dt) override;
    void Describe(RefPtr* out) override;
    bool Admit(const NetAddress* from, std::uint8_t** password, std::uint8_t* reason) override;

    // Switches the server to map `name`.
    void SetMap(const std::string* name);
    // Queues a map message for each zone that follows the current map.
    void QueueMapList();
    // When the held map message names `name`: drops it and queues "name_3" instead.
    bool FinishMap(const std::string* name);
    // The variable named `name`, variables_.end() when missing.
    std::list<SharedRefTo<VariableMessage>>::iterator FindVariable(const std::string* name);
    // The variable named `name`, made (value 0) at the front of the list when missing.
    std::list<SharedRefTo<VariableMessage>>::iterator FindOrAddVariable(const std::string* name);
    void SetVariable(const std::string* name, float value);       // zero removes it
    void RaiseVariable(const std::string* name, float value);     // to the larger value
    void RemoveVariable(const std::string* name);
    double GetVariable(const std::string* name);                  // 0 when missing
    // The zone completion command ("<zone>_3"): the clients'
    // shares of the zone's two values and the host's items, then the map state is dropped.
    void CompleteZone(const std::string* zone);
    // The zone leaving command ("<zone>_2"): the host's items with the final award, then
    // the map state is dropped and the world's map cleared.
    void LeaveZone();
    // Queues the host's item awards of the briefing `entry` (and, with `withFinal`, the final award).
    void AwardHost(const QuestBriefingRecord* entry, bool withFinal);
    // Queues an award message for `connection` (60 s delay); destroys `text`.
    void QueueAward(CRefObject* connection, int kind, float value, std::string text);
    // Lists `data` under `name` (unless listed) as a file message with 64-byte chunk messages.
    void ShareFile(const std::string* name, GameBuffer* data);
    // Whether the server is announced to the master server (port 28004).
    void SetPublic(bool announce);
    // Sends the master server this game's record: the name, the played map's title (empty without a map), the
    // players and their maximum, the game kind, whether a password is set, the dword the master last sent and
    // the session magic. EI_Plugin's NewMaster patches the send; this is the version without it.
    void AnnounceToMaster();
    // Keeps a chat or system line for the clients (`player` null for the server's own; at most 50 lines).
    void Announce(std::string_view text, CRefObject* player);
    // Announce with text `kind` of the text table formatted with `text`.
    void AnnounceText(int kind, const char* text, CRefObject* player);
    // The trade between two offers (either order), or null.
    TradeMessage* FindTrade(const PartyMessage* offer, const PartyMessage* other);
    int LeaderId();                                       // the first host's id, -1
    void SetPassword(const char* text);
    Connection* FindGuestConnection(const std::string* name);
    Connection* FindConnectionBySerial(int serial);
    PlayerMessage* FindPlayer(int id);
    void AnnounceToGuests(int kind, const char* text);
    void Kick(Connection* connection);
    void Ban(Connection* connection);
    void ReportDeparture(Connection* connection, int text);
    bool IsHostConnection(const Connection* connection);
    // Whether the player bound to `connection` is in state 3.
    bool PlayerStateIs3(const Connection* connection);

    float announceTimer_;               // seconds to the next announcement to the master server
    std::uint8_t public_;               // announced to the master server
    std::uint32_t masterToken_;         // 0; the dword the master server last sent (OnMaster), sent back
                                        // with the next announcement (GameInfoRecord::masterToken)
    NetAddress masterAddress_;          // the master server (port 28004)
    std::uint32_t nextLine_;            // the number of the next chat line
    std::list<SharedRefTo<PlayerMessage>> players_;
    std::list<SharedRefTo<LineMessage>> lines_; // chat and system lines, at most 50
    std::list<SharedRefTo<FileMessage>> files_;
    std::list<CountedRefTo<FileRequestMessage>> fileRequests_; // the clients' requests (file requests, connections)
    // where the game is hosted (a HostMessage; world_game.cpp's AnnounceHost), replicated to everyone when set
    RefPtrTo<HostMessage> hostInfo_;
    std::list<SharedRefTo<BindingMessage>> bindings_; // player <-> connection
    RefPtrTo<MapMessage> map_;          // the map message while a map is played
    std::list<SharedRefTo<ZoneMessage>> queuedMaps_; // map messages the host may pick next
    std::list<SharedRefTo<VariableMessage>> variables_;
    std::list<TimedEntry<SharedRefPtr>> awards_;            // {delay, award}
    std::list<CountedRefTo<PartyMessage>> offers_; // the players' trade offers
    std::list<CountedRefTo<TradeMessage>> trades_;
    std::list<TimedEntry<CountedRefPtr>> finishedTrades_;   // {delay, trade}: done trades, kept 60 s
    std::list<NetAddress> banned_;
    std::string name_;
    std::string mapName_;
    std::uint32_t maxPlayers_;          // 8; set by the creator
    std::uint32_t kind_;                // the constructor's argument: the game kind
    char password_[0x14];               // the join password (empty: none)
    std::uint32_t masterKey_;           // set by the creator (StartServer); announced, and the master
                                        // server's packets must start with it (OnMaster)
};

// ---- The client base and the game client ----------------------------------------------------------

// The client base: the servers found (by broadcast pings, by asking known addresses, from the master
// server's list) and the join request to one of them.
class ClientBase : public SessionBase {
public:

    enum JoinState : std::uint32_t { kNotJoined = 0, kAsking = 1, kJoined = 2 };

    ClientBase();
    ~ClientBase() override;
    void RemoveClient(std::list<SharedRefTo<Connection>>::iterator node) override;
    bool OnUpdate(const NetAddress* from, BufferCursor* cursor) override;
    void OnInfo(const NetAddress* from, BufferCursor* cursor) override;
    void OnAccepted(const NetAddress* from, BufferCursor* cursor) override;
    void OnRejected(const NetAddress* from, BufferCursor* cursor) override;
    void Tick(double dt) override;
    virtual void OnJoined() {}
    virtual void CreateEntry(RefPtr* out) = 0;                                 // a new server entry into *out

    // A ping (3, the session time) to `address`.
    void SendPing(const NetAddress* address);
    // A join request (4, magic, join id, the send rate for the host, then `data`).
    void SendJoin(const NetAddress* address, GameBuffer* data);
    // Pings every broadcast address, then the first waiting address, or else the next
    // query in turn (and the always-asked address is queued again when set).
    void Refresh();
    // Sets the address asked on every refresh.
    void SetAlwaysAsk(const NetAddress* address);
    // Takes over the queries (from the options); unlisted servers are listed as asked.
    void SetQueries(const ClientQueries* from);
    // Asks the server at `address` directly.
    void Ask(const NetAddress* address);
    // Forgets the server at `address`: its query and every list entry with that address.
    void Forget(const NetAddress* address);
    // A server from the master server's list.
    void AddServer(ServerEntry* entry);
    // Starts joining the host at `address` with `data` sent along.
    void Join(const NetAddress* address, const GrowingBuffer* data);
    // The entry of the `index`th server found, or null.
    ServerEntry* ServerAt(int index);

    NetAddress host_;                   // the host joined or being asked
    GrowingBuffer joinData_;            // sent with every join request
    std::uint32_t joinId_;              // rand() << 16 | tick count
    std::uint32_t joinState_;
    std::uint8_t refusal_;              // the reason the host gave
    float retryTimer_;                  // seconds to the next join request
    float retryInterval_;               // doubles with every request (0.1 first)
    std::list<SharedRefTo<ServerEntry>> servers_;
    ClientQueries queries_;             // servers asked directly
    std::list<NetAddress> waiting_;    // addresses waiting to be asked
    NetAddress alwaysAsk_;              // asked on every refresh (ip 0: none)
    std::uint32_t nextQuery_;           // the query the refresh pings next
};

// A finished trade of the client's (ClientSession::tradeRecords_): the revisions of the trade and copies of
// the two parties and the traded one. A copy copies the parties in order (CParty's copy saves and loads the
// party: it draws object ids), so the client adds a record as the game's insert(end()) did: a blank's copy.
struct TradeRecord {
    TradeRecord() = default;
    // CParty's copy takes its source non-const (it saves it into a stream).
    TradeRecord(const TradeRecord& from)
        : acceptedRevision(from.acceptedRevision), revision(from.revision),
          parties{CParty(const_cast<CParty&>(from.parties[0])), CParty(const_cast<CParty&>(from.parties[1])),
                  CParty(const_cast<CParty&>(from.parties[2]))} {}
    TradeRecord& operator=(const TradeRecord&) = delete;

    std::uint32_t acceptedRevision = 0;   // our offer's accepted revision of the partner's
    std::uint32_t revision = 0;           // our offer's revision
    CParty parties[3];                    // ours, the partner's, the traded party
};

// The game client (g_gameClient). It keeps what the server replicates to it, by class, in STLport lists of
// counted pointers.
class ClientSession final : public ClientBase {
public:
    // A buffer's stream size over the dword at its start.
    double BufferRatio(GameBuffer* buffer);
    // Reads a server list reply.
    void ReadServerList(GameBuffer* in);
    void StartParty(CRefObject* partner);
    void RecordTrade(const void* party);

    ClientSession();
    ~ClientSession() override;
    void CollectReplicated(std::vector<Replicated>** out) override;
    void Tick(double dt) override;
    void OnJoined() override;
    void CreateEntry(RefPtr* out) override;

    // Applies the awards received: experience, gold, items.
    void ApplyAwards();
    // Keeps `object` on the timers list for `seconds`.
    void AddTimed(CRefObject* object, std::uint32_t seconds);
    // A command message ({kind, text}) for the server, kept 60 s.
    void SendCommand(int kind, std::string_view text);

    std::list<CountedRefTo<PlayerMessage>> players_;
    std::list<CountedRefTo<LineMessage>> lines_;
    std::list<CountedRefTo<FileMessage>> files_;
    std::list<CountedRefTo<FileChunkMessage>> chunks_;
    std::list<SharedRefTo<FileRequestMessage>> fileRequests_; // file requests for the server
    HostMessage* info_;                 // the game's description, counted
    BindingMessage* binding_;           // our binding, counted
    MapMessage* map_;                   // the map message, counted
    std::list<CountedRefTo<ZoneMessage>> zones_; // map messages the host may pick
    std::list<CountedRefTo<VariableMessage>> variables_;
    RefPtrTo<LoginMessage> player_;     // the local player's login record
    std::list<TimedEntry<SharedRefPtr>> timers_;            // {seconds left, object}: objects kept for a while
    std::list<CountedRefTo<AwardMessage>> awards_; // awards received, applied by ApplyAwards
    RefPtrTo<PartyMessage> party_;      // our trade offer (party description)
    std::list<CountedRefTo<PartyMessage>> offers_; // the other players' trade offers
    std::list<TradeRecord> tradeRecords_;
    TradeMessage* trade_;               // the current trade, counted
    std::uint32_t gameType_;            // the multiplayer game type 0..3 (StartServer): start zone, type variable
    lib::InPlace<CParty> parties_[2];           // ours, the partner's
};

// ---- The game announcement ------------------------------------------------------------------------

// The game announcement the server builds and the client reads in its entry.
struct GameInfoRecord {
    std::string name;
    std::string description;
    std::uint8_t players;
    std::uint8_t maxPlayers;
    std::uint8_t kind;           // the server's kind_ (ServerEntry::kind_)
    std::uint8_t hasPassword;    // joining needs a password (ServerEntry::password_)
    std::uint32_t masterKey;
    std::uint32_t masterToken;
    std::uint32_t magic;         // kSessionMagic

    void Write(GameBuffer* buffer) const;
    int Read(GameBuffer* buffer);           // 1 unless the buffer ran short
};

// The byte buffer's read helper.
void BufferUpdateHigh(GameBuffer* buffer);
} // namespace net
