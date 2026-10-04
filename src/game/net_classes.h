#pragma once

// Layouts of the multiplayer session classes and their message objects (00412000-00440000).
// Names are provisional; see docs/notes/unmapped_410_470.md ("Third round: sessions and messages").
//
// Class map (vtables):
//   0073BB68  CRefObject root (core_refcount.h): +4 16-bit reference count, +6 / +7 flags.
//   0073BF5C  SessionBase (constructor 00436770, destructor body 00436A50): 15 slots.
//             2 RemoveClient 00436CF0, 3 update from a host 00437080, 4..10 packet kinds (most only
//             log "ERROR: Can't handle ..."), 11 / 12 empty, 13 empty, 14 Receive 00437370.
//   0073BBB4  server base and 0073BB70 game server (0x2600 bytes; constructor 00412D80).
//   0073BE30  client base and 0073BDEC game client (0x2700 bytes; constructor 0041BC40).
//   0073BC10  Message: the network message base (+8 = 1); slots 0 deleting destructor, 1 empty,
//             2 reset, 3 clone, 4 write to a byte cursor, 5 read. Message classes 0073BBF8 ...
//             0073BDD0 (0x18 apart).

#include <cstddef>
#include <cstdint>

namespace net {

// A vector of message handlers (STLport vector<void(*)()>: begin, end, end of storage).
struct HandlerVector {
    void** begin;
    void** end;
    void** eos;
};
static_assert(sizeof(HandlerVector) == 12);

// An STLport string as the message classes embed it: start, finish, end of storage.
struct StringRep {
    char* start;
    char* finish;
    char* eos;
};
static_assert(sizeof(StringRep) == 12);

// The type registry's hash tables (STLport hashtable, 0x14 bytes: functors, bucket vector, count).
struct HashTable {
    std::uint32_t functors;   // +0x00 hasher / key_equal / select1st (empty)
    void** buckets;           // +0x04
    void** bucketsEnd;        // +0x08
    void** bucketsEos;        // +0x0C
    std::uint32_t count;      // +0x10
};
static_assert(sizeof(HashTable) == 0x14);

// The UDP socket of a session and the broadcast addresses of the host (004407F0 / 00440940).
struct SocketDriver {
    std::uint32_t socket;     // +0x00 SOCKET (-1: none)
    void* addresses;          // +0x04 vector<sockaddr_in> (16-byte elements)
    void* addressesEnd;       // +0x08
    void* addressesEos;       // +0x0C
};
static_assert(sizeof(SocketDriver) == 0x10);

constexpr int kChannels = 3;
constexpr int kMessageTypes = 0x100;

struct Message;
using MessageFactory = Message* (*)();

// A node of the registry's hash tables (16 bytes from the node pool): next, key, value.
struct TypeNode {
    TypeNode* next;
    std::uint32_t key;
    std::uint32_t value;
    std::uint32_t unused;
};

// The message type registry (+0x2C of a session): two STLport hash_maps.
struct TypeRegistry {
    HashTable byVtable;    // +0x00 message vtable -> type id (constructor 00438B70, 100 buckets)
    HashTable byType;      // +0x14 type id -> factory (constructor 00439F20, 100 buckets)

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

// The session base (vtable 0073BF5C). 0x2568 bytes (the double at +0x20 aligns it to 8).
struct SessionBase {
    void* vtable;                       // +0x00
    std::uint32_t refHeader;            // +0x04 CRefObject count and flags (set by the allocator)
    std::uint32_t unknown_08;           // +0x08 0
    std::uint32_t unknown_0C;           // +0x0C 0x3C
    std::uint32_t unknown_10;           // +0x10 5
    std::uint32_t unknown_14;           // +0x14 0x4B
    std::uint32_t frame;                // +0x18 1; Receive counts its calls
    std::uint32_t unknown_1C;           // +0x1C not set by the constructors
    double time;                        // +0x20 0; Receive adds the elapsed time
    void* clients;                      // +0x28 STLport list of connections (sentinel, 12-byte nodes)
    TypeRegistry types;                 // +0x2C
    SocketDriver socket;                // +0x54
    std::uint8_t typeFlags[0x100];      // +0x64 one byte per message type (004142F0 clears 1..17)
    HandlerVector handlers[kChannels][kMessageTypes];   // +0x164 / +0xD64 / +0x1964
    std::uint32_t padding_2564;         // +0x2564

    // 00436770. The base constructor body; returns this.
    SessionBase* Construct();
    // 004377D0. Appends `handler` to the handlers of message `type` (low byte) on `channel` (0..2;
    // other channels are ignored).
    void AddHandler(int channel, std::uint32_t type, void* handler);
    // 004142F0. Clears the flags of message types 1..17 and registers their factories.
    void RegisterMessageTypes();

    // A handler registration as the session constructors inline it (not a function of its own in
    // the original): a prototype message of `size` bytes built by `construct`, held through a
    // counted reference while its type id is looked up by vtable, then AddHandler.
    void AddHandlerFor(std::size_t size, Message* (__fastcall* construct)(Message*), int channel, void* handler);
};
static_assert(offsetof(SessionBase, time) == 0x20);
static_assert(offsetof(SessionBase, clients) == 0x28);
static_assert(offsetof(SessionBase, types) == 0x2C);
static_assert(offsetof(SessionBase, socket) == 0x54);
static_assert(offsetof(SessionBase, typeFlags) == 0x64);
static_assert(offsetof(SessionBase, handlers) == 0x164);
static_assert(sizeof(SessionBase) == 0x2568);

// The 0x70-byte unit record builder (constructor 00661050, destructor 006611D0; world.md): the
// client keeps two (slot 2 resets them).
struct UnitRecordBuilder {
    void* vtable;
    std::uint8_t unknown_04[0x6C];
};
static_assert(sizeof(UnitRecordBuilder) == 0x70);

// The game client (vtable 0073BDEC; client base 0073BE30). 0x2700 bytes, pointer at 0079B5D8.
struct ClientSession {
    SessionBase base;                   // +0x0000
    std::uint32_t unknown_2568[4];      // +0x2568 0
    std::uint8_t buffer[0x1C];          // +0x2578 GameBuffer (004DA7F0)
    std::uint32_t unknown_2594;         // +0x2594 0
    std::uint32_t unknown_2598;         // +0x2598 0
    std::uint32_t unknown_259C[3];      // +0x259C not set
    void* list_25A8;                    // +0x25A8 STLport list (0041F8B0)
    std::uint32_t vector_25AC[4];       // +0x25AC vector of 0x2C-byte buffers and a dword (0041C420)
    void* list_25BC;                    // +0x25BC STLport list (0041FB10)
    std::uint32_t unknown_25C0[4];      // +0x25C0 0
    std::uint32_t unknown_25D0[2];      // +0x25D0 not set
    void* list_25D8;                    // +0x25D8 STLport lists (00423990 ... 00424130)
    void* list_25DC;
    void* list_25E0;
    void* list_25E4;
    void* list_25E8;
    void* ref_25EC;                     // +0x25EC counted pointers (released in the destructor)
    void* ref_25F0;
    void* ref_25F4;
    void* list_25F8;                    // +0x25F8 STLport list (00424320)
    void* list_25FC;                    // +0x25FC STLport list (00424510)
    void* player;                       // +0x2600 RefPtr to the local player record (vtable 0073BBF8)
    void* list_2604;                    // +0x2604 STLport list (00420F10): timed entries {next, prev, time, ...}
    void* list_2608;                    // +0x2608 STLport list (00424750): pending awards
    void* unknown_260C;                 // +0x260C
    void* list_2610;                    // +0x2610 STLport list (004231C0)
    void* list_2614;                    // +0x2614 STLport list of 0x160-byte nodes (004249E0)
    void* unknown_2618;                 // +0x2618 CRefObject released in the destructor
    std::uint32_t unknown_261C;         // +0x261C set by the creator (0057E740)
    UnitRecordBuilder records[2];       // +0x2620 / +0x2690

    // 0041BC40. The constructor (the caller allocated 0x2700 bytes and cleared +4); returns this.
    ClientSession* Construct();
};

// 0041C420: the 0x10-byte member at +0x25AC (a vector and a dword), all zero. Returns this.
void* __fastcall ClientVectorConstruct(std::uint32_t* self);

// What a session replicates to the connection being updated (slot 13 fills it): STLport
// vector<{object, priority}>.
struct Replicated {
    void* object;              // a CRefObject with its version at +8
    std::uint8_t priority;     // 0..10
    std::uint8_t unknown_05[3];
};
struct ReplicatedVector {
    Replicated* begin;
    Replicated* end;
    Replicated* eos;
};
// 0079B924: the connection being updated or read (null outside the session code).
inline void* CurrentConnection() { return *reinterpret_cast<void**>(0x0079B924); }

// Slot 13 of the client (0041D050) and the server (00417EB0): appends what the connection at
// 0079B924 should receive to **out.
void __fastcall ClientCollectReplicated(ClientSession* self, void* edx, ReplicatedVector** out);

// The game server (vtable 0073BB70; server base 0073BBB4). 0x2600 bytes, pointer at 0079B5DC.
struct ServerSession {
    SessionBase base;                   // +0x0000
    void* list_2568;                    // +0x2568 STLport list (0041FD30)
    std::uint32_t unknown_256C;         // +0x256C not set
    std::uint32_t unknown_2570;         // +0x2570 0
    std::uint8_t unknown_2574;          // +0x2574 0
    std::uint8_t unknown_2575[3];
    std::uint32_t unknown_2578;         // +0x2578 0
    std::uint32_t unknown_257C[4];      // +0x257C 0
    std::uint32_t unknown_258C;         // +0x258C 0
    void* list_2590;                    // +0x2590 STLport lists (00422400, 004225E0, 00422800, 004229E0)
    void* list_2594;
    void* list_2598;
    void* list_259C;
    std::uint32_t unknown_25A0;         // +0x25A0 0
    void* list_25A4;                    // +0x25A4 STLport list (00422BE0)
    std::uint32_t unknown_25A8;         // +0x25A8 0
    void* list_25AC;                    // +0x25AC STLport lists (00422DC0 ... 00421B20)
    void* list_25B0;
    void* list_25B4;
    void* list_25B8;
    void* list_25BC;
    void* list_25C0;
    void* list_25C4;
    StringRep string_25C8;              // +0x25C8 empty with 8 bytes of room (00421800)
    StringRep string_25D4;              // +0x25D4
    std::uint32_t unknown_25E0;         // +0x25E0 8; set by the creator (0057E740)
    std::uint32_t unknown_25E4;         // +0x25E4 the constructor's argument
    std::uint32_t unknown_25E8[5];      // +0x25E8 0
    std::uint32_t unknown_25FC;         // +0x25FC set by the creator

    // 00412D80. The constructor (the caller allocated 0x2600 bytes and cleared +4); returns this.
    ServerSession* Construct(std::uint32_t arg);
};
static_assert(offsetof(ServerSession, list_2568) == 0x2568);
static_assert(offsetof(ServerSession, list_2590) == 0x2590);
static_assert(offsetof(ServerSession, list_25C4) == 0x25C4);
static_assert(offsetof(ServerSession, string_25C8) == 0x25C8);
static_assert(offsetof(ServerSession, unknown_25E0) == 0x25E0);
static_assert(sizeof(ServerSession) == 0x2600);

void __fastcall ServerCollectReplicated(ServerSession* self, void* edx, ReplicatedVector** out);

// Appending one {object, priority} to the vector, as the slot 13 implementations do: through the
// out-of-line push_back of a default element (0041F790) or inline (the same, with the overflow
// path 00426B10), then storing into the new last element.
void PushReplicated(ReplicatedVector* v, void* object, std::uint8_t priority);
void PushReplicatedInline(ReplicatedVector* v, void* object, std::uint8_t priority);
static_assert(offsetof(ClientSession, buffer) == 0x2578);
static_assert(offsetof(ClientSession, list_25A8) == 0x25A8);
static_assert(offsetof(ClientSession, list_25BC) == 0x25BC);
static_assert(offsetof(ClientSession, list_25D8) == 0x25D8);
static_assert(offsetof(ClientSession, player) == 0x2600);
static_assert(offsetof(ClientSession, list_2614) == 0x2614);
static_assert(offsetof(ClientSession, records) == 0x2620);
static_assert(sizeof(ClientSession) == 0x2700);

constexpr std::uintptr_t kSessionBaseVtable = 0x0073BF5C;
constexpr std::uintptr_t kClientBaseVtable = 0x0073BE30;
constexpr std::uintptr_t kClientVtable = 0x0073BDEC;
constexpr std::uintptr_t kServerBaseVtable = 0x0073BBB4;
constexpr std::uintptr_t kServerVtable = 0x0073BB70;
constexpr std::uint32_t kSessionMagic = 0xDEC0AD07;   // +8 of the game server and client

// Message base (vtable 0073BC10): CRefObject header, then the type (1).
struct Message {
    void* vtable;
    std::uint32_t refHeader;   // +0x04
    std::uint32_t kind;        // +0x08 1
};
static_assert(sizeof(Message) == 12);

constexpr std::uintptr_t kMessageVtable = 0x0073BC10;

// 00413250. operator new(size) with the CRefObject count cleared. No null check (as the original).
void* __fastcall NewRefObject(std::size_t size);
// 004186E0. The message base constructor body: vtable 0073BC10, kind 1. Returns this.
Message* __fastcall MessageConstructBase(Message* self);

// Constructors of message classes, named by vtable (net_messages.cpp). Each returns this.
Message* __fastcall ConstructMessage73BCB8(Message* self);   // 0041C870 (0x14 bytes)
Message* __fastcall ConstructMessage73BC70(Message* self);   // 0041C680 (0x3C bytes)
Message* __fastcall ConstructMessage73BC88(Message* self);   // 0041C740 (0x20 bytes)
Message* __fastcall ConstructMessage73BCA0(Message* self);   // 0041C7C0 (0x24 bytes)
Message* __fastcall ConstructMessage73BCD0(Message* self);   // 00418C30 (0x18 bytes)
Message* __fastcall ConstructMessage73BBF8(Message* self);   // 00413350 (0x34 bytes)
Message* __fastcall ConstructMessage73BC28(Message* self);   // 004138A0 (0x18 bytes)
Message* __fastcall ConstructMessage73BC40(Message* self);   // 00413C90 (0x1C bytes)
Message* __fastcall ConstructMessage73BC58(Message* self);   // 004140B0 (0x14 bytes)

} // namespace net
