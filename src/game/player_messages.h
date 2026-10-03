#pragma once

// The server side of a player's connection (CPlayerServer, vtable 00743280; 0066C730-00671080) as far
// as the message writers use it, and the party record lookup. Each writer appends one message to
// the outgoing stream at +0x48 (a CMemoryStream; its slot 2 writes bytes): a type byte, then the
// payload. The single-player game runs the same server / client pair, so the per-tick player state
// (0058B750), script variables and unit events all go through them. Names are provisional; see
// docs/notes/range_660.md.

#include "maths.h"
#include "net_delta.h"

#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

class CPlayerServer {
public:
    // Variables and state.
    void SetVariable(const char* name, std::uint32_t value);        // 0066EE10: message 0x81
    void SetVariableMax(const char* name, float value);             // 0066EE80: raises a variable, 0x81
    void DeleteVariable(const char* name);                          // 0066EF30: message 0x82
    void SendGameTime();                                            // 0066F180: message 0x07
    int SendState02(int value);                                     // 0066EBD0: message 0x02 (0047DA80 payload)
    int SendState70(std::uint8_t a, std::uint16_t b);               // 0066EC10: message 0x70
    int SendState71(std::uint32_t a, std::uint32_t b);              // 0066EC70: message 0x71
    int SendState72(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t w);   // 0066ECD0: 0x72
    void SendState73(void* source);                                 // 0066F240: message 0x73 (006D4570 payload)
    int SendPosition04(const Vector3* at, std::uint32_t a, std::uint32_t b);   // 0066ED30: message 0x04
    std::uint32_t SendPosition0F(const Vector3* at, const Vector3* other, std::uint8_t flag);   // 0066EDA0: 0x0F
    // Simple messages.
    void Send17();                                                  // 0066EA10 (slot 18)
    void SendObject0A(const void* object);                          // 0066EA30 (slot 19): id
    void SendPair16(std::uint32_t a, std::uint32_t b);              // 0066EA70
    void SendObjects0C(const void* list);                           // 0066EAC0: ids and +0x28 of a list
    void SendObject14(const void* object);                          // 0066EB40 (slot 22): id, +0x28
    void SendText0D(const char* text);                              // 0066EB90
    void SendText08(const char* text, std::uint32_t value);         // 0066F1F0
    void SendValue15(std::uint32_t value);                          // 0066F280
    void SendPair18(std::uint32_t a, std::uint32_t b);              // 0066F2C0
    void SendObject43(const void* object);                          // 0066D6A0: id, nothing without an object
    void SendObject0B(const void* object, std::uint32_t value);     // 0066D6E0: id, value
    void SendText13(const char* text, std::uint32_t unused);        // 0066D770
    void SendText(std::uint8_t type, const char* text);             // 0066D7B0
    void WriteTo(void* target);                                     // 0066D230: target's slot 7 (stream)
    void SetDisconnect();                                           // 0066D250: state 4
    void SetFlag70(std::uint8_t value);                             // 0066D260

    void* vtable_;
    std::uint8_t engineObject_[0x1C - 0x04];
    std::uint8_t* player_;          // +0x1C the world player (+0xC4 variables, +0x100, +0x130 dirty bits)
    int state_;                     // +0x20
    std::uint8_t unknown_24[0x38 - 0x24];
    Vector3 lastPosition_;          // +0x38 sent with 0x04 / 0x0F
    std::uint32_t unknown_44;
    BitStream out_;                 // +0x48 messages to the client (CMemoryStream)
    std::uint8_t flag70_;           // +0x70
};
static_assert(offsetof(CPlayerServer, player_) == 0x1C);
static_assert(offsetof(CPlayerServer, lastPosition_) == 0x38);
static_assert(offsetof(CPlayerServer, out_) == 0x48);
static_assert(offsetof(CPlayerServer, flag70_) == 0x70);

// The records of a party's units (0x784 bytes each; the party is the world player's +0x70).
struct PartyRecords {
    // 006621B0. The record whose +0x1C is `unitId`, or null.
    std::uint8_t* Find(int unitId) const;

    std::uint8_t unknown_00[0x30];
    std::uint8_t* records;          // +0x30
    int count;                      // +0x34
};
static_assert(offsetof(PartyRecords, records) == 0x30);

// A world player (vtable 00743104; "CPlayer"). Only what its per-tick update uses.
class CPlayer {
public:
    // 00665DE0. Ticks the objects in the list at +0x114 (slot 6; looked up by id through the
    // party's slot 18 manager) and drops the ones that are gone or whose tick returned 0, then runs
    // slot 21 of the connection at +0x20.
    void Tick();

    std::uint8_t unknown_00[0x20];
    void* connection_;                          // +0x20 CPlayerServer / CPlayerClient
    std::uint8_t unknown_24[0x70 - 0x24];
    void* party_;                               // +0x70 an embedded object (vtable); slot 18 gives a manager
    std::uint8_t unknown_74[0x114 - 0x74];
    mfc::CTypedPtrList<void> objects_;          // +0x114 object ids (stored as the node data)
};
static_assert(offsetof(CPlayer, party_) == 0x70);
static_assert(offsetof(CPlayer, objects_) == 0x114);
