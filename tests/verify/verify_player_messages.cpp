// Checks for player_messages.cpp. Each message writer runs ours into the player's outgoing stream;
// the stream (header and bytes), the last position and the player's dirty bits are saved,
// everything is restored, the original runs, and the results must be the same (skipped when ours
// had to grow the stream's buffer). The variable table they also update is set to the same value
// twice. SetVariableMax (reads the variable it sets), SendState73 and WriteTo (call into other
// objects' writers) run ours only. The party record lookup is pure.
#include "fake_stream.h"
#include "verify.h"

#include "../../src/game/party_record_lookup.h"
#include "../../src/game/player_messages.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;

struct MessageState {
    BitStream header;
    std::vector<unsigned char> bytes;
    unsigned char head[0x48 - 0x1C];    // player_, state_, ..., lastPosition_
    std::uint8_t flag70;
    std::uint32_t dirty;
    explicit MessageState(CPlayerServer* p) {
        RawCopy(&header, &p->out_);
        if (p->out_.data_) bytes.assign(p->out_.data_, p->out_.data_ + p->out_.capacity_);
        std::memcpy(head, reinterpret_cast<char*>(p) + 0x1C, sizeof(head));
        flag70 = p->flag70_;
        dirty = p->player_ ? *reinterpret_cast<std::uint32_t*>(p->player_ + 0x130) : 0;
    }
    void Restore(CPlayerServer* p) const {
        RawCopy(&p->out_, &header);
        if (!bytes.empty()) std::memcpy(p->out_.data_, bytes.data(), bytes.size());
        std::memcpy(reinterpret_cast<char*>(p) + 0x1C, head, sizeof(head));
        p->flag70_ = flag70;
        if (p->player_) *reinterpret_cast<std::uint32_t*>(p->player_ + 0x130) = dirty;
    }
    bool SameAs(const MessageState& o) const {
        if (std::memcmp(&header, &o.header, sizeof(BitStream)) != 0) return false;
        std::size_t n = header.size_ < bytes.size() ? header.size_ : bytes.size();
        if (n > o.bytes.size()) return false;
        if (n && std::memcmp(bytes.data(), o.bytes.data(), n) != 0) return false;
        return std::memcmp(head, o.head, sizeof(head)) == 0 && flag70 == o.flag70 && dirty == o.dirty;
    }
};

template <typename R, typename Ours, typename Original>
R CheckMessage(const char* name, CPlayerServer* self, bool compareResult, Ours ours, Original original) {
    MessageState before(self);
    R actual = ours();
    MessageState after(self);
    if (after.header.data_ != before.header.data_ || after.header.capacity_ != before.header.capacity_)
        return actual;   // ours grew the buffer: the original's run cannot be repeated
    before.Restore(self);
    R expected = original();
    MessageState again(self);
    bool same = again.SameAs(after) && (!compareResult || expected == actual);
    ei::verify::Check(name, true, same, "player %p: stream size %u vs %u", self, again.header.size_, after.header.size_);
    return expected;
}
} // namespace

#define EI_MESSAGE_VERIFY(addr, stolen, label, Params, Args, call)                                 \
    static void(__fastcall* s_orig_##addr) Params;                                                  \
    static void __fastcall Verify_##addr Params {                                                   \
        CheckMessage<int>(label, self, false, [&] { self->call; return 0; },                         \
                          [&] { s_orig_##addr Args; return 0; });                                    \
    }                                                                                               \
    EI_VERIFY(addr, Verify_##addr, stolen, s_orig_##addr)

#define EI_MESSAGE_VERIFY_INT(addr, stolen, label, Params, Args, call)                             \
    static int(__fastcall* s_orig_##addr) Params;                                                   \
    static int __fastcall Verify_##addr Params {                                                    \
        return CheckMessage<int>(label, self, true, [&] { return static_cast<int>(self->call); },    \
                                 [&] { return s_orig_##addr Args; });                                \
    }                                                                                               \
    EI_VERIFY(addr, Verify_##addr, stolen, s_orig_##addr)

EI_MESSAGE_VERIFY(0x0066EE10, 5, "CPlayerServer::SetVariable", (CPlayerServer* self, void*, const char* n, std::uint32_t v),
                  (self, nullptr, n, v), SetVariable(n, v));
EI_VERIFY(0x0066EE10, Verify_0x0066EE10, 5, s_orig_0x0066EE10);
EI_MESSAGE_VERIFY(0x0066EF30, 5, "CPlayerServer::DeleteVariable", (CPlayerServer* self, void*, const char* n),
                  (self, nullptr, n), DeleteVariable(n));
EI_VERIFY(0x0066EF30, Verify_0x0066EF30, 5, s_orig_0x0066EF30);
EI_MESSAGE_VERIFY(0x0066F180, 5, "CPlayerServer::SendGameTime", (CPlayerServer* self), (self), SendGameTime());
EI_VERIFY(0x0066F180, Verify_0x0066F180, 5, s_orig_0x0066F180);
EI_MESSAGE_VERIFY_INT(0x0066EBD0, 5, "CPlayerServer::SendState02", (CPlayerServer* self, void*, int v),
                      (self, nullptr, v), SendState02(v));
EI_VERIFY(0x0066EBD0, Verify_0x0066EBD0, 5, s_orig_0x0066EBD0);
EI_MESSAGE_VERIFY_INT(0x0066EC10, 5, "CPlayerServer::SendState70", (CPlayerServer* self, void*, std::uint32_t a, std::uint32_t b),
                      (self, nullptr, a, b), SendState70(static_cast<std::uint8_t>(a), static_cast<std::uint16_t>(b)));
EI_VERIFY(0x0066EC10, Verify_0x0066EC10, 5, s_orig_0x0066EC10);
EI_MESSAGE_VERIFY_INT(0x0066EC70, 6, "CPlayerServer::SendState71", (CPlayerServer* self, void*, std::uint32_t a, std::uint32_t b),
                      (self, nullptr, a, b), SendState71(a, b));
EI_VERIFY(0x0066EC70, Verify_0x0066EC70, 6, s_orig_0x0066EC70);
EI_MESSAGE_VERIFY_INT(0x0066ECD0, 5, "CPlayerServer::SendState72",
                      (CPlayerServer* self, void*, std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t w),
                      (self, nullptr, x, y, z, w), SendState72(x, y, z, w));
EI_VERIFY(0x0066ECD0, Verify_0x0066ECD0, 5, s_orig_0x0066ECD0);
EI_MESSAGE_VERIFY_INT(0x0066ED30, 5, "CPlayerServer::SendPosition04",
                      (CPlayerServer* self, void*, const Vector3* at, std::uint32_t a, std::uint32_t b),
                      (self, nullptr, at, a, b), SendPosition04(at, a, b));
EI_VERIFY(0x0066ED30, Verify_0x0066ED30, 5, s_orig_0x0066ED30);
EI_MESSAGE_VERIFY_INT(0x0066EDA0, 5, "CPlayerServer::SendPosition0F",
                      (CPlayerServer* self, void*, const Vector3* at, const Vector3* other, std::uint32_t f),
                      (self, nullptr, at, other, f), SendPosition0F(at, other, static_cast<std::uint8_t>(f)));
EI_VERIFY(0x0066EDA0, Verify_0x0066EDA0, 5, s_orig_0x0066EDA0);
EI_MESSAGE_VERIFY(0x0066EA10, 7, "CPlayerServer::Send17", (CPlayerServer* self), (self), Send17());
EI_VERIFY(0x0066EA10, Verify_0x0066EA10, 7, s_orig_0x0066EA10);
EI_MESSAGE_VERIFY(0x0066EA30, 5, "CPlayerServer::SendObject0A", (CPlayerServer* self, void*, const void* o),
                  (self, nullptr, o), SendObject0A(o));
EI_VERIFY(0x0066EA30, Verify_0x0066EA30, 5, s_orig_0x0066EA30);
EI_MESSAGE_VERIFY(0x0066EA70, 5, "CPlayerServer::SendPair16", (CPlayerServer* self, void*, std::uint32_t a, std::uint32_t b),
                  (self, nullptr, a, b), SendPair16(a, b));
EI_VERIFY(0x0066EA70, Verify_0x0066EA70, 5, s_orig_0x0066EA70);
EI_MESSAGE_VERIFY(0x0066EAC0, 5, "CPlayerServer::SendObjects0C", (CPlayerServer* self, void*, const void* l),
                  (self, nullptr, l), SendObjects0C(l));
EI_VERIFY(0x0066EAC0, Verify_0x0066EAC0, 5, s_orig_0x0066EAC0);
EI_MESSAGE_VERIFY(0x0066EB40, 5, "CPlayerServer::SendObject14", (CPlayerServer* self, void*, const void* o),
                  (self, nullptr, o), SendObject14(o));
EI_VERIFY(0x0066EB40, Verify_0x0066EB40, 5, s_orig_0x0066EB40);
EI_MESSAGE_VERIFY(0x0066EB90, 5, "CPlayerServer::SendText0D", (CPlayerServer* self, void*, const char* t),
                  (self, nullptr, t), SendText0D(t));
EI_VERIFY(0x0066EB90, Verify_0x0066EB90, 5, s_orig_0x0066EB90);
EI_MESSAGE_VERIFY(0x0066F1F0, 5, "CPlayerServer::SendText08", (CPlayerServer* self, void*, const char* t, std::uint32_t v),
                  (self, nullptr, t, v), SendText08(t, v));
EI_VERIFY(0x0066F1F0, Verify_0x0066F1F0, 5, s_orig_0x0066F1F0);
EI_MESSAGE_VERIFY(0x0066F280, 5, "CPlayerServer::SendValue15", (CPlayerServer* self, void*, std::uint32_t v),
                  (self, nullptr, v), SendValue15(v));
EI_VERIFY(0x0066F280, Verify_0x0066F280, 5, s_orig_0x0066F280);
EI_MESSAGE_VERIFY(0x0066F2C0, 5, "CPlayerServer::SendPair18", (CPlayerServer* self, void*, std::uint32_t a, std::uint32_t b),
                  (self, nullptr, a, b), SendPair18(a, b));
EI_VERIFY(0x0066F2C0, Verify_0x0066F2C0, 5, s_orig_0x0066F2C0);
EI_MESSAGE_VERIFY(0x0066D6A0, 5, "CPlayerServer::SendObject43", (CPlayerServer* self, void*, const void* o),
                  (self, nullptr, o), SendObject43(o));
EI_VERIFY(0x0066D6A0, Verify_0x0066D6A0, 5, s_orig_0x0066D6A0);
EI_MESSAGE_VERIFY(0x0066D6E0, 7, "CPlayerServer::SendObject0B", (CPlayerServer* self, void*, const void* o, std::uint32_t v),
                  (self, nullptr, o, v), SendObject0B(o, v));
EI_VERIFY(0x0066D6E0, Verify_0x0066D6E0, 7, s_orig_0x0066D6E0);
// 0066D730 (A-Z1): SUB ESP,8; MOV EAX,[ESP+0x10] (7 bytes). As the writers above, except that the
// payload's first byte is never written by the original (stack garbage) and is 0 in ours: after the
// original's run that byte is set to ours, so it is not compared and the stream keeps ours.
// CPlayerManager::SendValueToAll (the script command SendEvent; script_exec.txt) reaches it.
static void(__fastcall* s_sendValue12)(CPlayerServer*, void*, std::uint32_t, std::uint32_t);
static void __fastcall SendValue12Verify(CPlayerServer* self, void*, std::uint32_t unused, std::uint32_t value) {
    MessageState before(self);
    self->SendValue12(unused, value);
    MessageState after(self);
    if (after.header.data_ != before.header.data_ || after.header.capacity_ != before.header.capacity_) return;
    before.Restore(self);
    s_sendValue12(self, nullptr, unused, value);
    const std::uint32_t unwritten = before.header.position_ + 1;   // after the type byte
    if (unwritten < after.bytes.size()) self->out_.data_[unwritten] = after.bytes[unwritten];
    MessageState again(self);
    ei::verify::Check("CPlayerServer::SendValue12", true, again.SameAs(after), "player %p: stream size %u vs %u, value %u", self,
                      again.header.size_, after.header.size_, value);
}
EI_VERIFY(0x0066D730, SendValue12Verify, 7, s_sendValue12);
EI_MESSAGE_VERIFY(0x0066D770, 5, "CPlayerServer::SendText13", (CPlayerServer* self, void*, std::uint32_t u, const char* t),
                  (self, nullptr, u, t), SendText13(u, t));
EI_VERIFY(0x0066D770, Verify_0x0066D770, 5, s_orig_0x0066D770);
EI_MESSAGE_VERIFY(0x0066D7B0, 7, "CPlayerServer::SendText", (CPlayerServer* self, void*, std::uint32_t type, const char* t),
                  (self, nullptr, type, t), SendText(static_cast<std::uint8_t>(type), t));
EI_VERIFY(0x0066D7B0, Verify_0x0066D7B0, 7, s_orig_0x0066D7B0);
EI_MESSAGE_VERIFY(0x0066D250, 7, "CPlayerServer::SetDisconnect", (CPlayerServer* self), (self), SetDisconnect());
EI_VERIFY(0x0066D250, Verify_0x0066D250, 7, s_orig_0x0066D250);
EI_MESSAGE_VERIFY(0x0066D260, 7, "CPlayerServer::SetFlag70", (CPlayerServer* self, void*, std::uint32_t v),
                  (self, nullptr, v), SetFlag70(static_cast<std::uint8_t>(v)));
EI_VERIFY(0x0066D260, Verify_0x0066D260, 7, s_orig_0x0066D260);

// Ours only.
static void __fastcall SetVariableMaxVerify(CPlayerServer* self, void*, const char* name, float value) {
    std::uint32_t before = self->out_.size_;
    self->SetVariableMax(name, value);
    ei::verify::Check("CPlayerServer::SetVariableMax", true, self->out_.size_ >= before, "player %p", self);
}
EI_VERIFY(0x0066EE80, SetVariableMaxVerify, 0, s_unused);
static void __fastcall SendState73Verify(CPlayerServer* self, void*, void* source) {
    self->SendState73(source);
    bool ok = !self->player_ || (*reinterpret_cast<std::uint32_t*>(self->player_ + 0x130) & 0x20) == 0;
    ei::verify::Check("CPlayerServer::SendState73", true, ok, "player %p", self);
}
EI_VERIFY(0x0066F240, SendState73Verify, 0, s_unused);
static void __fastcall WriteToVerify(CPlayerServer* self, void*, CStream* target) { self->WriteTo(target); }
EI_VERIFY(0x0066D230, WriteToVerify, 0, s_unused);

// 006221B0: MOV EDX,[ECX+34h]; XOR EAX,EAX (5 bytes). Pure.
static std::uint8_t*(__fastcall* s_findRecord)(const PartyRecords*, void*, int);
static std::uint8_t* __fastcall FindRecordVerify(const PartyRecords* self, void*, int unitId) {
    std::uint8_t* expected = s_findRecord(self, nullptr, unitId);
    std::uint8_t* actual = self->Find(unitId);
    ei::verify::Check("PartyRecords::Find", expected, actual, "unit %d", unitId);
    return expected;
}
EI_VERIFY(0x006621B0, FindRecordVerify, 5, s_findRecord);

// 00665DE0. Ours only (it ticks objects): the list is consistent afterwards (count = nodes, links
// both ways).
static void __fastcall PlayerTickVerify(CPlayer* self) {
    self->Tick();
    int nodes = 0;
    bool links = true;
    decltype(self->objects_.head) previous = nullptr;
    for (auto* n = self->objects_.head; n != nullptr && nodes <= self->objects_.count; n = n->next, ++nodes) {
        links = links && n->prev == previous;
        previous = n;
    }
    bool ok = nodes == self->objects_.count && links && self->objects_.tail == previous;
    ei::verify::Check("CPlayer::Tick", true, ok, "player %p: %d nodes, count %d", self, nodes, self->objects_.count);
}
EI_VERIFY(0x00665DE0, PlayerTickVerify, 0, s_unused);

// 0066C9A0 (the server's Create; every newgame): ours only (it allocates the table and draws a random
// number). 256 handlers (spot checks: 0x00 0066F890, 0x36 00670310, 0x50 00670B60), type 0x0A empty,
// state 0, both sent-object arrays empty, the refresh timer in 900..1800.
static BOOL __fastcall CreateServerVerify(CPlayerServer* self) {
    BOOL result = self->CreateServer();
    auto at = [self](int type) { return reinterpret_cast<std::uintptr_t>(self->handlers_[type]); };
    bool ok = result == 1 && self->state_ == 0 && self->handlers_.size == 0x100 && at(0x00) == 0x0066F890u &&
              at(0x36) == 0x00670310u && at(0x50) == 0x00670B60u && at(0x0A) == 0 && self->sentObjects_.size == 0 &&
              self->sentTimed_.size == 0 && self->refreshTimer_ >= 900 && self->refreshTimer_ < 1801 && self->out_.size_ == 0;
    ei::verify::Check("CPlayerServer::CreateServer", true, ok, "server %p: %d handlers timer %d", self,
                      self->handlers_.size, self->refreshTimer_);
    return result;
}
EI_VERIFY(0x0066C9A0, CreateServerVerify, 0, s_unused);

// --- A-H15: the server's lifetime and slots ----------------------------------------------------------
// Scenarios: every newgame constructs the server (0066C540) and runs ClearState through the
// player's Create / Destroy; leaving the zone (zone_gz2g_traps, `zone`) runs Leave (slot 16) and
// the destructor at the session's end; party units' combat motion (ui_battle_commands, sound's
// goblin fight) sends the path message 0066D490.

// 0066D490: side by side (ours, the stream and fields restored, the original, both compared).
EI_MESSAGE_VERIFY(0x0066D490, 6, "CPlayerServer::SendPathMessage",
                  (CPlayerServer* self, void*, CEngineObject* unit, void* path, const float* point, int kind, int flag,
                   std::uint32_t limit),
                  (self, nullptr, unit, path, point, kind, flag, limit),
                  SendPathMessage(unit, path, point, kind, flag, limit));

namespace {
const char* (__fastcall* s_serverName)(CPlayerServer*);
int(__fastcall* s_serverId)(CPlayerServer*);

// The state ClearState leaves (the refresh timer drawn from ISAAC).
bool Cleared(const CPlayerServer* self) {
    return self->state_ == 0 && self->sentObjects_.size == 0 && self->sentTimed_.size == 0 && self->out_.size_ == 0 &&
           self->group50F8_.size == 0 && self->partyUnits_.size == 0 && self->placements_.size == 0 &&
           self->unknown_44 == -1 && self->unknown24_ == 0xFFFFFFFFu && self->refreshPending_ == 0 &&
           self->flag70_ == 0 && self->refreshTimer_ >= 900 && self->refreshTimer_ <= 1800;
}
} // namespace

const char* __fastcall PlayerServerClassName(CPlayerServer*);
int __fastcall PlayerServerClassId(CPlayerServer*);

// Constructor: ours only (an ISAAC draw): the vtable, the engine object's fields, an empty table
// and a cleared state.
static CPlayerServer* __fastcall ServerConstructVerify(CPlayerServer* self) {
    CPlayerServer* result = self->Construct();
    auto* object = reinterpret_cast<CEngineObject*>(self);
    bool ok = result == self && reinterpret_cast<std::uintptr_t>(self->vtable_) == 0x00743280u && object->refCount_ == 0 &&
              object->id_ == -1 && self->player_ == nullptr && self->handlers_.data == nullptr && Cleared(self);
    ei::verify::Check("CPlayerServer::Construct", true, ok, "server %p timer %d", self, self->refreshTimer_);
    return result;
}
// Destructor body / deleting destructor: ours only (the destructor body leaves CEngineObject's
// vtable and no player).
static void __fastcall ServerDestructVerify(CPlayerServer* self) {
    self->DestructBody();
    bool ok = reinterpret_cast<std::uintptr_t>(self->vtable_) == CEngineObject::kVtableAddress && self->player_ == nullptr;
    ei::verify::Check("CPlayerServer::DestructBody", true, ok, "server %p", self);
}
static CPlayerServer* __fastcall ServerDeleteVerify(CPlayerServer* self, void*, unsigned flags) {
    CPlayerServer* result = self->DeletingDestruct(flags);
    ei::verify::Check("CPlayerServer::DeletingDestruct", true, result == self, "server %p", self);
    return result;
}
// Class name / id: against the original.
static const char* __fastcall ServerNameVerify(CPlayerServer* self) {
    const char* expected = s_serverName(self);
    ei::verify::Check("CPlayerServer::GetClassName", expected, PlayerServerClassName(self), "server %p", self);
    return expected;
}
static int __fastcall ServerIdVerify(CPlayerServer* self) {
    int expected = s_serverId(self);
    ei::verify::Check("CPlayerServer::GetClassId", expected, PlayerServerClassId(self), "server %p", self);
    return expected;
}
// ClearState (slots 3 and 15), ForgetSentObjects (13), ClearGroup50F8 (21), Leave (16): ours only.
static void __fastcall ClearStateVerify(CPlayerServer* self) {
    self->ClearState();
    ei::verify::Check("CPlayerServer::ClearState", true, Cleared(self), "server %p timer %d", self, self->refreshTimer_);
}
static void __fastcall ForgetSentVerify(CPlayerServer* self) {
    self->ForgetSentObjects();
    bool ok = self->sentObjects_.size == 0 && self->sentObjects_.data == nullptr && self->sentTimed_.size == 0 &&
              self->group50F8_.size == 0 && self->partyUnits_.size == 0 && self->partyUnits_.data == nullptr;
    ei::verify::Check("CPlayerServer::ForgetSentObjects", true, ok, "server %p", self);
}
static void __fastcall ClearGroupVerify(CPlayerServer* self) {
    self->ClearGroup50F8();
    bool ok = self->group50F8_.size == 0 && self->group50F8_.data == nullptr && self->group50F8_.maxSize == 0;
    ei::verify::Check("CPlayerServer::ClearGroup50F8", true, ok, "server %p", self);
}
static void __fastcall LeaveVerify(CPlayerServer* self) {
    self->Leave();
    ei::verify::Check("CPlayerServer::Leave", 3, self->state_, "server %p", self);
}
EI_VERIFY(0x0066C540, ServerConstructVerify, 0, s_unused);
EI_VERIFY(0x0066C770, ServerDestructVerify, 0, s_unused);
EI_VERIFY(0x0066C750, ServerDeleteVerify, 0, s_unused);
EI_VERIFY(0x0066C730, ServerNameVerify, 5, s_serverName);
EI_VERIFY(0x0066C740, ServerIdVerify, 5, s_serverId);
EI_VERIFY(0x0066CC50, ClearStateVerify, 0, s_unused);
EI_VERIFY(0x0066CF20, ClearStateVerify, 0, s_unused);
EI_VERIFY(0x0066CE50, ForgetSentVerify, 0, s_unused);
EI_VERIFY(0x0066D090, ClearGroupVerify, 0, s_unused);
EI_VERIFY(0x0066D020, LeaveVerify, 0, s_unused);
