#pragma once

// The multiplayer session classes and the message base; see docs/notes/network.md.
// SessionBase holds the UDP socket and the connections and hands received messages to its OnMessage; ServerBase /
// ClientBase add the lobby side (pending joins, finding servers) under ServerSession / ClientSession, the game server
// and client (g_gameServer, g_gameClient).
// Message (below) is the replicated object base, ServerEntry a server as the lobby lists it.

#include "engine/core/core_refcount.h"
#include "game/players/party.h"
#include "engine/core/coder.h"
#include "game/net/mod_identity.h"
#include "platform/platform.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <list>
#include <type_traits>
#include <vector>

struct QuestBriefingRecord;

namespace console { class Stream; }

namespace net {

// The suffixes of a zone's messages (ServerSession's queued maps) and of its briefing keys in the quest
// database: the zone entered, left, completed.
inline constexpr const char* kZoneEnterSuffix = "_1";
inline constexpr const char* kZoneLeaveSuffix = "_2";
inline constexpr const char* kZoneCompleteSuffix = "_3";

class Connection;
class Message;
class BindingMessage;
class HostMessage;
class LoginMessage;
class MapMessage;
class SayMessage;
class CommandMessage;
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

// The first byte of a session datagram; each kind goes to its handler slot of the session (SessionBase::OnUpdate
// ...) with the sender and a cursor after that byte. The numbers are the wire format; 2 is unused.
enum class PacketKind : std::uint8_t {
    Master = 0xFF,      // -1: the master server's answer
    Update = 0,         // a connection's update
    Leave = 1,          // a client leaves: its connection id follows
    GetInfo = 3,        // a ping: the sender's time follows
    Login = 4,          // a join request
    Info = 5,           // a server's description (then our mod mark)
    Accepted = 6,       // the join id follows (then our mod mark)
    Rejected = 7,       // the join id and a RefusalReason follow (then our mod mark for ModMismatch)
};

// Why a server refuses a join (the byte after the join id of a Rejected packet; ClientBase::refusal).
enum class RefusalReason : std::uint8_t {
    None = 0,           // no refusal received
    Full = 1,
    Banned = 2,
    WrongPassword = 3,
    // Ours: the client's mod is not the server's (modidentity::Agrees; an original client's join to a server playing a
    // mod too). An original client shows its unknown reasons' box, lmp_cannot_connect.
    ModMismatch = 4,
};

// Ours (docs/notes/network.md, "Mods"): our peers' mod mark, written where an original peer reads nothing: after a
// login's 20-byte password, an info answer's entry, an acceptance's join id and a ModMismatch refusal's reason. kModMark (with its zero), a
// format byte (kModMarkFormat), the content hash (its low dword, then its high), the title's length (a byte) and its
// bytes (UTF-8).
inline constexpr char kModMark[] = "ei-rebuild mod";
inline constexpr std::uint8_t kModMarkFormat = 1;
void WriteModMark(GameBuffer* out, const modidentity::Identity& mod);
// The mark at the cursor, none when there is none (an original peer's packet ends before it) or it is of another
// format.
std::optional<modidentity::Identity> ReadModMark(BufferCursor* cursor);
// The harness's "originalnet" step: this game's lobby acts as the original's: its joins and acceptances carry no
// mark, its server admits any mod and its client reads no mark.
inline bool g_originalPeer = false;

// The kind of an award message (AwardMessage::kind; ClientSession::ApplyAwards).
// Who may pause a hosted game (the host's choice on the create-server screen, ServerSession::pauseMode;
// PlayerServer::OnPause). Original: only the leader pauses (anyone resumes), the pause announced. Announced: only the
// leader pauses and resumes, both announced (EI_Plugin's Pause 1). AnyPlayer: everyone pauses and resumes, announced
// under the player's own name (Pause 2). Off: no pausing (Pause 3).
enum class PauseMode : std::uint8_t { Original, Announced, AnyPlayer, Off };

// The most players a hosted game takes (the create-server screen's slider): the chat strip's faces, 0x38 wide from
// x 0x2E8 leftwards, fit 14 in the layout's width.
inline constexpr int kMostPlayers = 14;
// Ours: the companions the create-server screen's switch offers: a hero and two make a party of three.
inline constexpr int kMostCompanions = 2;

enum class AwardKind : std::uint32_t {
    Experience = 0,
    Gold = 1,
    Item = 2,           // the item named by the text
    RemoveItem = 3,
    QuestReset = 4,     // the party's quest items removed
};

// The kind of a command for the server (CommandMessage::kind; ServerOnCommand).
enum class CommandKind : std::uint32_t {
    PreviewMap = 0,     // the host shows a map
    Zone = 1,           // the host picks a queued map message: "<zone>_1" / "_2" / "_3"
    KickByName = 2,
    BanByName = 3,
    KickBySerial = 4,
    BanBySerial = 5,
};

// Where a multiplayer client's player is, kept as the state of its login record (LoginMessage::state; the server's
// PlayerMessage::state copies it; the chat window shows it as the player's status text).
enum class PlayerStatus : std::uint32_t {
    InZone = 0,
    InBase = 1,
    InCamp = 2,
    Connecting = 3,     // the client's party goes to the server; InZone again once answered
    Fresh = 4,          // a new record (shown as connecting too)
};

// An IPv4 address and port (the platform's: sockaddr_in's 16 bytes), as the messages carry it.
struct NetAddress : platform::net::Address {
    // This address from "host[:port]" (an IPv4 number or a host name looked up); `defaultPort` unless the
    // text names one. False when the host is unknown.
    bool Parse(std::string_view text, int defaultPort);
    // "a.b.c.d:port". The original's Format could name the host instead (gethostbyaddr), but only ever ran without the
    // look-up, which was given the whole 16 bytes and so could not have found the name.
    std::string ToString() const;
};
static_assert(sizeof(NetAddress) == 0x10);

// The TCP link to the master server. The master request box (ui_screens.h) holds one.
class MasterLink {
public:
    // A non-blocking TCP socket connecting to `address` (state 1), or state 0.
    explicit MasterLink(const NetAddress* address);
    ~MasterLink();
    MasterLink(const MasterLink&) = delete;
    MasterLink& operator=(const MasterLink&) = delete;

    enum class State : std::int32_t { Closed = 0, Connecting = 1, Receiving = 2, Done = 3, Sending = 4 };

    // The constructor's work: opens the socket and starts connecting; the state stays Closed when that fails.
    void Connect(const NetAddress& address);

    GrowingBuffer out;           // what is sent once connected
    GrowingBuffer in;
    platform::net::Socket socket;   // (kNoSocket: none)
    State state = State::Closed;
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
// The buffer every update packet is built in and the one datagrams are received into (net_session.cpp).
extern GrowingBuffer g_sendBuffer;
extern GrowingBuffer g_receiveBuffer;
// The game client and the game server (null when there is none): counted pointers, cleared by their static
// initialisers and released by the exit functions those register (net_session.cpp).
inline Ref<ClientSession> g_gameClient = /*0079B5D8*/ {};
inline Ref<ServerSession> g_gameServer = /*0079B5DC*/ {};

// ---- The message base ---------------------------------------------------------------------------

// The type of a message class (Message::Type, the classes' kType): the byte an update entry or an object reference
// carries. The numbers are the wire format.
enum class MessageType : std::uint8_t {
    Player = 1,
    Line = 2,
    Host = 3,
    Binding = 4,
    Login = 5,
    Say = 6,
    Command = 7,
    Zone = 8,
    Map = 9,
    Variable = 10,
    File = 11,
    FileChunk = 12,
    FileRequest = 13,
    Award = 14,
    Party = 15,
    PartyMember = 16,
    Trade = 17,
};
constexpr int kMessageTypes = 0x100;   // the type byte's values

// What happened to a received message (SessionBase::OnMessage).
enum class MessageEvent : std::uint8_t {
    Created,    // a new record
    Changed,    // a newer version of a record
    Removed,    // the record went away (the peer removed it or the connection closed)
};

// A replicated network object: a connection sends it again whenever its version differs from the one last
// sent. OnShutdown resets the message (each class its own fields), Clone copies it.
class Message : public RefObject {
public:
    ~Message() override;
    // Version 1 again, as constructed.
    void OnShutdown() override { version = 1; }
    virtual MessageType Type() const = 0;
    // The fields at the cursor's byte position; object references through `peer` (the connection sending or
    // receiving the message).
    virtual void Write(Connection* peer, BufferCursor* cursor) = 0;
    virtual void Read(Connection* peer, BufferCursor* cursor) = 0;

    std::uint32_t version = 1;    // 1 when constructed; raised by every change

protected:
    Message();
    Message(const Message& other) : RefObject(), version(other.version) {}
};

// A new message of wire type `type` (no references), null for a type no class has.
Message* CreateMessage(std::uint8_t type);

// ---- The server entries of the lobby ------------------------------------------------------------

constexpr float kNoPingSeconds = 9.999f;   // a listed server's ping while unknown

class ServerEntryBase : public RefObject {
public:
    ~ServerEntryBase() override;
    virtual void Write(BufferCursor* cursor) = 0;
    virtual void Read(BufferCursor* cursor) = 0;

    enum class State : std::uint32_t {
        Local = 0,        // found by a broadcast ping
        Asked = 1,        // asked directly
        FromList = 2,     // from the master server's list
    };

    NetAddress address;
    float timeout = 0.0f;    // seconds until the entry is checked again (1e38: never)
    float ping = kNoPingSeconds;  // seconds; a constant when unknown
    std::uint8_t foreign = 0;  // the reply's echoed word differs from the magic
    State state = State::Local;

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
    ServerEntry* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    std::string name;
    static constexpr std::uint32_t kNoKind = 4;
    std::uint32_t kind = kNoKind;  // the game kind (kNoKind: none)
    std::string description;         // the current map's title
    std::uint8_t players = 0;
    std::uint8_t maxPlayers = 0;
    std::uint8_t password = 0;    // whether joining needs a password
    // Ours: the mod mark read after the entry of the server's info answer (net::ReadModMark; none: an original
    // server's answer). Not part of Write / Read: a saved reply and the master's list carry no mark, so `modRead` is
    // false until an answer is read.
    std::optional<modidentity::Identity> mod;
    bool modRead = false;

    // Whether this game may join the server as far as the mods go (modidentity::Agrees; an entry without an answer
    // read yet counts as joinable): the connection screen marks the others.
    bool ModAgrees() const { return !modRead || modidentity::Agrees(mod); }
};

// A server the client asks directly: its address and the reply last seen.
struct Query {
    Query();
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
    std::vector<Query>::iterator Find(const NetAddress* address);
};

// ---- The session base ---------------------------------------------------------------------------

// The UDP socket of a session and the broadcast addresses of the host.
struct SocketDriver {
    // Starts Winsock and notes the broadcast address of the host's first address.
    SocketDriver();
    // Closes the socket and stops Winsock.
    ~SocketDriver();
    SocketDriver(const SocketDriver&) = delete;
    SocketDriver& operator=(const SocketDriver&) = delete;

    platform::net::Socket socket;   // (kNoSocket: none)
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
    bool Receive(NetAddress* from, GrowingBuffer* buffer);
};

// How long a command, an award and a finished trade stay on their timed lists.
inline constexpr float kTimedEntryLifetime = 60.0f;

// The value of the timed lists (the client's timers, the server's awards and finished trades): the seconds left
// and a counted object, held shared (the client's timers, the server's awards) or counted (the server's finished
// trades). A copy copies the seconds, then the reference; a vector's growth moves them (a copy of a shared one
// would share a clone).
template <typename Holder>
struct TimedEntry {
    TimedEntry(float seconds, Holder object) : seconds(seconds), ref(std::move(object)) {}

    float seconds = 0.0f;
    Holder ref = {};
};
static_assert(std::is_nothrow_move_constructible_v<TimedEntry<SharedRef<RefObject>>>);

// Each entry of a timed list loses `dt` seconds; those below zero are erased.
template <typename List>
void CountDown(List& list, float dt) {
    for (auto node = list.begin(); node != list.end();) {
        node->seconds -= dt;
        if (node->seconds < 0.0f) node = list.erase(node);
        else ++node;
    }
}

// What a session replicates to the connection being updated (CollectReplicated fills it).
struct Replicated {
    Message* object = nullptr;  // replicated while it is listed
    std::uint8_t priority = 0;  // 0..10
};

// Appends one {object, priority}.
inline void PushReplicated(std::vector<Replicated>* v, Message* object, std::uint8_t priority) {
    v->push_back({object, priority});
}

// The datagrams of a session by PacketKind.
class SessionBase : public RefCounted {
public:
    // Defined in net_session.cpp: the members' destructors need the connection's definition.
    SessionBase();
    ~SessionBase() override;
    virtual void RemoveClient(std::list<SharedRef<Connection>>::iterator node);                     // end() is ignored
    virtual bool OnUpdate(const NetAddress* from, BufferCursor* cursor);      // PacketKind::Update
    virtual void OnGetInfo(const NetAddress* from, BufferCursor* cursor);     // PacketKind::GetInfo (logs an error)
    virtual void OnLogin(const NetAddress* from, BufferCursor* cursor);       // PacketKind::Login (logs an error)
    virtual void OnLeave(const NetAddress* from, BufferCursor* cursor);       // PacketKind::Leave
    virtual void OnInfo(const NetAddress* from, BufferCursor* cursor);        // PacketKind::Info (logs an error)
    virtual void OnAccepted(const NetAddress* from, BufferCursor* cursor);    // PacketKind::Accepted (logs an error)
    virtual void OnRejected(const NetAddress* from, BufferCursor* cursor);    // PacketKind::Rejected (logs an error)
    virtual void OnMaster(const NetAddress* from, BufferCursor* cursor);      // PacketKind::Master (logs an error)
    virtual void OnClientLost(Connection*) {}                                 // timed out or a bad update
    virtual void OnClientLeft(Connection*) {}                                 // sent a leave packet
    // Appends what `connection` should have to *out.
    virtual void CollectReplicated(const Connection* connection, std::vector<Replicated>* out) = 0;
    // A message `from` received, changed or lost.
    virtual void OnMessage(Connection* from, MessageEvent event, Message* message) = 0;
    // Advances the session clock by `dt`, hands the waiting datagrams to the handlers, ticks the connections
    // (dropping timed-out ones) and counts the frame.
    virtual void Tick(double dt);

    // The connection to `address` in clients, end() when none.
    std::list<SharedRef<Connection>>::iterator FindClient(const NetAddress* address);
    // Logs it, removes a connection to the same address, appends a new connection with `id` and `address`;
    // its send interval follows from `rate`.
    void AddClient(const NetAddress* address, std::uint32_t id, int rate);
    bool Send(const NetAddress* address, GameBuffer* buffer);
    // The send rate for a peer: g_localRate on the local network, else g_remoteRate.
    std::uint32_t RateFor(const NetAddress* address);
    // Sends a leave packet (PacketKind::Leave, then the connection id) to `address`.
    void SendLeave(const NetAddress* address, std::uint32_t id);
    std::uint32_t ClientCount() const;

    std::uint32_t magic = 0;            // 0; the game server and client set kSessionMagic
    std::uint32_t frame = 1;            // 1; Tick counts its calls
    double time = 0.0;                  // 0; Tick adds the elapsed time
    // The connections. A list: OnUpdate keeps its connection's node while the update's handlers may remove
    // others (a kick, ServerOnCommand).
    std::list<SharedRef<Connection>> clients;
    SocketDriver socket;
};

constexpr std::uint32_t kSessionMagic = 0xDEC0AD07;

// ---- The server base and the game server ----------------------------------------------------------

// A peer told to join that has not sent an update yet (ServerBase::joins).
struct PendingJoin {
    NetAddress address;
    std::uint32_t id = 0;
    std::int32_t rate = 0;
    float age = 0.0f;            // seconds (dropped past the join timeout)
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
    virtual void Describe(Ref<ServerEntry>* out) = 0;   // this server's entry into *out
    // Whether a client from `from` may join (and `*reason` when not): the login's password at the cursor, read
    // when one is checked.
    virtual bool Admit(const NetAddress* from, std::uint32_t joinId, BufferCursor* password, RefusalReason* reason) = 0;

    // Answers a ping from `address` with this server's description (5, the echoed time,
    // the magic and the entry).
    void SendInfo(const NetAddress* address, std::uint32_t echo);

    std::vector<PendingJoin> joins;
};

// The game server (g_gameServer). Its lists hold counted pointers.
class ServerSession final : public ServerBase {
public:
    explicit ServerSession(std::uint32_t kind);
    ~ServerSession() override;
    void OnMaster(const NetAddress* from, BufferCursor* cursor) override;
    void OnClientLost(Connection* connection) override;                       // text 4
    void OnClientLeft(Connection* connection) override;                       // text 1
    void CollectReplicated(const Connection* connection, std::vector<Replicated>* out) override;
    // The clients' messages: logins, chat, commands, file requests, trade offers and their members.
    void OnMessage(Connection* from, MessageEvent event, Message* message) override;
    void Tick(double dt) override;
    void Describe(Ref<ServerEntry>* out) override;
    bool Admit(const NetAddress* from, std::uint32_t joinId, BufferCursor* password, RefusalReason* reason) override;

    // Switches the server to map `name`.
    void SetMap(const std::string& name);
    // Queues a map message for each zone that follows the current map.
    void QueueMapList();
    // When the held map message names `name`: drops it and queues "name_3" instead.
    bool FinishMap(const std::string& name);
    // The variable named `name`, variables.end() when missing.
    std::vector<SharedRef<VariableMessage>>::iterator FindVariable(const std::string& name);
    // The variable named `name`, made (value 0) at the front of the list when missing.
    std::vector<SharedRef<VariableMessage>>::iterator FindOrAddVariable(const std::string& name);
    void SetVariable(const std::string& name, float value);       // zero removes it
    void RaiseVariable(const std::string& name, float value);     // to the larger value
    void RemoveVariable(const std::string& name);
    double GetVariable(const std::string& name);                  // 0 when missing
    // The zone completion command ("<zone>_3"): the clients'
    // shares of the zone's two values and the host's items, then the map state is dropped.
    void CompleteZone(const std::string& zone);
    // The zone leaving command ("<zone>_2"): the host's items with the final award, then
    // the map state is dropped and the world's map cleared.
    void LeaveZone();
    // Queues the host's item awards of the briefing `entry` (and, with `withFinal`, the final award).
    void AwardHost(const QuestBriefingRecord* entry, bool withFinal);
    // Queues an award message for `connection` (60 s delay); destroys `text`.
    void QueueAward(RefObject* connection, AwardKind awardKind, float value, std::string text);
    // Lists `data` under `name` (unless listed) as a file message with 64-byte chunk messages.
    void ShareFile(const std::string& name, GameBuffer* data);
    // Whether the server is announced to the master server (port 28004).
    void SetPublic(bool announce);
    // Sends the master server this game's record: the name, the played map's title (empty without a map), the
    // players and their maximum, the game kind, whether a password is set, the dword the master last sent and
    // the session magic. EI_Plugin's NewMaster patches the send; this is the version without it.
    void AnnounceToMaster();
    // Keeps a chat or system line for the clients (`player` null for the server's own; at most 50 lines).
    void Announce(std::string_view text, RefObject* player);
    // Announce with text `kind` of the text table formatted with `text`.
    void AnnounceText(int kind, std::string_view text, RefObject* player);
    // The trade between two offers (either order), or null.
    TradeMessage* FindTrade(const PartyMessage* offer, const PartyMessage* other);
    int LeaderId();                                       // the first host's id, -1
    void SetPassword(std::string_view text);
    Connection* FindGuestConnection(const std::string& name);
    Connection* FindConnectionBySerial(int serial);
    PlayerMessage* FindPlayer(int id);
    void AnnounceToGuests(int kind, std::string_view text);
    void Kick(Connection* connection);
    void Ban(Connection* connection);
    void ReportDeparture(Connection* connection, int text);
    bool IsHostConnection(const Connection* connection);
    // The first binding of `connection`, null without one.
    BindingMessage* BindingOf(const Connection* connection);
    // Whether the player bound to `connection` is Connecting.
    bool IsConnecting(const Connection* connection);

private:
    // OnMessage's longer cases: a login created (a client joined), changed or removed (it left), a chat line, a
    // command, a trade offer created, changed or removed.
    void OnJoin(Connection* from, LoginMessage* msg);
    void OnPlayerChanged(LoginMessage* message);
    void OnPlayerGone(LoginMessage* message);
    void OnSay(Connection* from, SayMessage* message);
    void OnCommand(Connection* from, CommandMessage* message);
    void OnTradeOffer(PartyMessage* message);
    void OnTradeUpdate(PartyMessage* message);
    void OnTradeCancel(PartyMessage* message);

public:

    float announceTimer = 0.0f;         // seconds to the next announcement to the master server
    std::uint8_t announced = 0;         // announced to the master server
    std::uint32_t masterToken = 0;      // 0; the dword the master server last sent (OnMaster), sent back
                                        // with the next announcement (GameInfoRecord::masterToken)
    NetAddress masterAddress;           // the master server (port 28004)
    std::uint32_t nextLine = 0;         // the number of the next chat line
    std::vector<SharedRef<PlayerMessage>> players;
    std::vector<SharedRef<LineMessage>> lines; // chat and system lines, at most 50
    std::vector<SharedRef<FileMessage>> files;
    std::vector<HeldRef<FileRequestMessage>> fileRequests; // the clients' requests (file requests, connections)
    // where the game is hosted (a HostMessage; app/game_flow.cpp's AnnounceHost), replicated to everyone when set
    Ref<HostMessage> hostInfo;
    std::vector<SharedRef<BindingMessage>> bindings; // player <-> connection
    Ref<MapMessage> map;           // the map message while a map is played
    std::vector<SharedRef<ZoneMessage>> queuedMaps; // map messages the host may pick next
    std::vector<SharedRef<VariableMessage>> variables;
    std::vector<TimedEntry<SharedRef<AwardMessage>>> awards;             // {delay, award}
    std::vector<HeldRef<PartyMessage>> offers; // the players' trade offers
    std::vector<HeldRef<TradeMessage>> trades;
    std::vector<TimedEntry<HeldRef<TradeMessage>>> finishedTrades;    // {delay, trade}: done trades, kept 60 s
    std::vector<NetAddress> banned;
    std::string name;
    std::string mapName;
    std::uint32_t maxPlayers = 8;       // 8; set by the creator
    PauseMode pauseMode = PauseMode::Original;   // set by the creator (StartServer)
    // Ours: the most companions (other saved heroes) a player may add to its hero in a zone, 0 none; set by
    // the creator (StartServer). Only our clients learn it (ServerMessage::Companions).
    std::uint32_t companions = 0;
    std::uint32_t kind = 0;             // the constructor's argument: the game kind
    char password[0x14] = {};           // the join password (empty: none)
    std::uint32_t masterKey = 0;        // set by the creator (StartServer); announced, and the master
                                        // server's packets must start with it (OnMaster)
    // Ours: the joins refused for the client's mod (each join once, however often it asks), the last one's address,
    // join id and mod (none: an original client's).
    std::uint32_t modRefusals = 0;
    NetAddress modRefusedFrom = {};
    std::uint32_t modRefusedJoin = 0;
    std::optional<modidentity::Identity> modRefused;
};

// ---- The client base and the game client ----------------------------------------------------------

// The client base: the servers found (by broadcast pings, by asking known addresses, from the master
// server's list) and the join request to one of them.
class ClientBase : public SessionBase {
public:

    enum class JoinState : std::uint32_t { NotJoined = 0, Asking = 1, Joined = 2 };

    ClientBase();
    ~ClientBase() override;
    void RemoveClient(std::list<SharedRef<Connection>>::iterator node) override;
    bool OnUpdate(const NetAddress* from, BufferCursor* cursor) override;
    void OnInfo(const NetAddress* from, BufferCursor* cursor) override;
    void OnAccepted(const NetAddress* from, BufferCursor* cursor) override;
    void OnRejected(const NetAddress* from, BufferCursor* cursor) override;
    void Tick(double dt) override;
    virtual void OnJoined() = 0;
    virtual void CreateEntry(Ref<ServerEntry>* out) = 0;   // a new server entry into *out

    // A ping (PacketKind::GetInfo, the session time) to `address`.
    void SendPing(const NetAddress* address);
    // A join request (PacketKind::Login, magic, join id, the send rate for the host, then `data`).
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

    NetAddress host;                    // the host joined or being asked
    GrowingBuffer joinData;             // sent with every join request
    std::uint32_t joinId = 0;           // rand() << 16 | tick count
    JoinState joinState = JoinState::NotJoined;
    RefusalReason refusal = {};         // the reason the host gave
    // Ours: the mod the host's acceptance or ModMismatch refusal named (none: no mark, an original host's), or the one
    // this client refused itself for after an acceptance (OnAccepted: refusal ModMismatch).
    std::optional<modidentity::Identity> hostMod;
    float retryTimer = 0.0f;            // seconds to the next join request
    float retryInterval = 0.0f;         // doubles with every request (0.1 first)
    std::vector<SharedRef<ServerEntry>> servers;
    ClientQueries queries;              // servers asked directly
    std::vector<NetAddress> waiting;    // addresses waiting to be asked, the first next
    NetAddress alwaysAsk;               // asked on every refresh (ip 0: none)
    std::uint32_t nextQuery = 0;        // the query the refresh pings next
};

// A finished trade of the client's (ClientSession::tradeRecords): the revisions of the trade and copies of
// the two parties and the traded one. A copy copies the parties in order (Party's copy saves and loads the
// party: it draws object ids), so the client adds a record as the game's insert(end()) did: a blank's copy.
struct TradeRecord {
    TradeRecord() = default;
    // Party's copy takes its source non-const (it saves it into a stream).
    TradeRecord(const TradeRecord& from)
        : acceptedRevision(from.acceptedRevision), revision(from.revision),
          parties{Party(const_cast<Party&>(from.parties[0])), Party(const_cast<Party&>(from.parties[1])),
                  Party(const_cast<Party&>(from.parties[2]))} {}
    TradeRecord& operator=(const TradeRecord&) = delete;

    std::uint32_t acceptedRevision = 0;   // our offer's accepted revision of the partner's
    std::uint32_t revision = 0;           // our offer's revision
    Party parties[3];                    // ours, the partner's, the traded party
};

// The game client (g_gameClient). It keeps what the server replicates to it, by class, in lists of counted
// pointers.
class ClientSession final : public ClientBase {
public:
    // A buffer's stream size over the dword at its start.
    float BufferRatio(GameBuffer* buffer);
    // Reads a server list reply.
    void ReadServerList(GameBuffer* in);
    void StartParty(PlayerMessage* partner);
    void RecordTrade(Party* party);
    // The game client's trade offer: publish ours, find the partner's offer that pairs with ours (null without
    // one), take the acceptance back, drop the offer, rebuild the partner's party from its offer.
    void PublishParty();
    PartyMessage* FindParty();
    void CommitParty();
    void DropParty();
    void ApplyParty();

    // Parties made, then the local player's login shared, then the parties' managers created.
    ClientSession();
    void CollectReplicated(const Connection* connection, std::vector<Replicated>* out) override;
    // The server's messages, kept in the lists below.
    void OnMessage(Connection* from, MessageEvent event, Message* message) override;
    void Tick(double dt) override;
    void OnJoined() override;
    void CreateEntry(Ref<ServerEntry>* out) override;

    // Applies the awards received: experience, gold, items.
    void ApplyAwards();
    // Keeps `object` on the timers list for `seconds`.
    void AddTimed(Message* object, float seconds);
    // A command message ({kind, text}) for the server, kept 60 s.
    void SendCommand(CommandKind kind, std::string_view text);
    // The players the session's screens lay out for: the game's limit, or the players present when more (the limit
    // unknown).
    int PlayerLimit() const;

private:
    // OnMessage's longer cases, each for a created message (OnTradeChanged a changed one).
    void OnBinding(BindingMessage* message);
    void OnMap(MapMessage* message);
    void OnFile(FileMessage* message);
    void OnFileChunk(FileChunkMessage* message);
    void OnTrade(TradeMessage* message);
    void OnTradeChanged(TradeMessage* message);
    // The trade, the party description and the trade records dropped.
    void EndTrade();

public:
    // Destroyed in the original's order, the reverse of this one: the parties first, the players last.
    std::vector<HeldRef<PlayerMessage>> players;
    std::vector<HeldRef<LineMessage>> lines;
    std::vector<HeldRef<FileMessage>> files;
    std::vector<HeldRef<FileChunkMessage>> chunks;
    std::vector<SharedRef<FileRequestMessage>> fileRequests; // file requests for the server
    Ref<HostMessage> info;              // the game's description
    Ref<BindingMessage> binding;        // our binding
    Ref<MapMessage> map;                // the map message
    std::vector<HeldRef<ZoneMessage>> zones; // map messages the host may pick
    std::vector<HeldRef<VariableMessage>> variables;
    SharedRef<LoginMessage> player;     // the local player's login record
    std::vector<TimedEntry<SharedRef<Message>>> timers;             // {seconds left, object}: objects kept for a while
    std::vector<HeldRef<AwardMessage>> awards; // awards received, applied by ApplyAwards
    SharedRef<PartyMessage> party;      // our trade offer (party description)
    std::vector<HeldRef<PartyMessage>> offers; // the other players' trade offers
    std::list<TradeRecord> tradeRecords;   // a list: a record's copy draws object ids, so records never move
    Ref<TradeMessage> trade;            // the current trade
    std::uint32_t gameType = 0;         // the multiplayer game type 0..3 (StartServer): start zone, type variable
    // The joined game's player limit: the host's own (StartServer), a joiner's from the lobby's answer for the server
    // it joined (the connect screen); 0 unknown.
    std::uint32_t maxPlayers = 0;
    Party parties[2];                   // ours, the partner's
};

// ---- The game announcement ------------------------------------------------------------------------

// The game announcement the server builds and the client reads in its entry.
struct GameInfoRecord {
    std::string name;
    std::string description;
    std::uint8_t players = 0;
    std::uint8_t maxPlayers = 0;
    std::uint8_t kind = 0;       // the server's kind (ServerEntry::kind)
    std::uint8_t hasPassword = 0;  // joining needs a password (ServerEntry::password)
    std::uint32_t masterKey = 0;
    std::uint32_t masterToken = 0;
    std::uint32_t magic = 0;     // kSessionMagic

    void Write(GameBuffer* buffer) const;
    int Read(GameBuffer* buffer);           // 1 unless the buffer ran short
};

// --- The network pump ---------------------------------------------------------------------------

// Ticks the game server and the game client (g_gameServer / g_gameClient). Names provisional; see
// docs/notes/network.md. Called from the main loop and from the loading screens : ticks the two sessions with the
// real time since the last call, then the game's per-frame network step.
void Pump();

} // namespace net

// Start-up (GameMain, first): the game server's and the game client's counted pointers cleared and their exit
// functions registered, so the sessions go in the game's exit order.
void InitGameServerPointer();
void InitGameClientPointer();
