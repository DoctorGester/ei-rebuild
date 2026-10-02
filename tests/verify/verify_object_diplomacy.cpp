// Differential checks for the diplomacy table (object_diplomacy.cpp). The table is 0x100 bytes and
// the functions that change it touch nothing else, except for the unit updates of SetRelation and
// OnAttack, which are checked for consistency after ours runs.
#include "verify.h"

#include "../../src/game/object.h"
#include "../../src/game/object_diplomacy.h"
#include "ei/vcall.h"

#include <cstring>

namespace {

struct TableCopy {
    alignas(4) unsigned char bytes[sizeof(CDiplomacy)];
    explicit TableCopy(const CDiplomacy* from) { std::memcpy(bytes, from, sizeof(bytes)); }
    CDiplomacy* get() { return reinterpret_cast<CDiplomacy*>(bytes); }
    bool Same(const CDiplomacy* other) const { return std::memcmp(bytes, other, sizeof(bytes)) == 0; }
};

std::uint32_t Bit(int n) { return 1u << (static_cast<unsigned>(n) & 31); }

// 004554B0: MOV EDX,ECX (2), PUSH EDI (1), MOV ECX,0x20 (5).
void(__fastcall* s_reset)(CDiplomacy*);
void __fastcall ResetVerify(CDiplomacy* self) {
    TableCopy expected(self);
    s_reset(expected.get());
    self->Reset();
    ei::verify::Check("CDiplomacy::Reset", true, expected.Same(self), "");
}

// 004554E0: PUSH -1 (2), PUSH imm32 (5). The original runs on a scratch table without the unit
// update; ours runs for real. With applyToUnits, every unit of `player` must then have bit `other`
// of its hostile mask set exactly when the relation is hostile.
void(__fastcall* s_setRelation)(CDiplomacy*, void*, int, int, int, BOOL);
void __fastcall SetRelationVerify(CDiplomacy* self, void*, int player, int other, int relation, BOOL applyToUnits) {
    TableCopy expected(self);
    s_setRelation(expected.get(), nullptr, player, other, relation, 0);
    self->SetRelation(player, other, relation, applyToUnits);
    bool same = expected.Same(self);
    if (applyToUnits) {
        ObjectListIterator it;
        it.manager = nullptr;
        for (CEngineObject* object = it.First(WorldObjectManager()); object; object = it.Next()) {
            if (ei::VCall<int>(object, 12) != kClassUnit || FieldAt<int>(object, unitfield::kPlayer) != player) continue;
            bool hostile = (FieldAt<std::uint32_t>(object, unitfield::kHostileMask) & Bit(other)) != 0;
            if (hostile != (relation == kRelationHostile)) same = false;
        }
        if (it.manager) --it.manager->refCount_;
    }
    ei::verify::Check("CDiplomacy::SetRelation", true, same, "%d %d -> %d (units %d)", player, other, relation,
                      applyToUnits);
}

// 00455630: MOV EAX,ECX (2), MOV EDX,1 (5).
int(__fastcall* s_getRelation)(const CDiplomacy*, void*, int, int);
int __fastcall GetRelationVerify(const CDiplomacy* self, void*, int player, int other) {
    int expected = s_getRelation(self, nullptr, player, other);
    ei::verify::Check("CDiplomacy::GetRelation", expected, self->GetRelation(player, other), "%d %d", player, other);
    return expected;
}

// 00455660: MOV EAX,[ESP+4] (4), TEST EAX,EAX (2).
int(__fastcall* s_getHostileMask)(const CDiplomacy*, void*, int);
int __fastcall GetHostileMaskVerify(const CDiplomacy* self, void*, int player) {
    int expected = s_getHostileMask(self, nullptr, player);
    ei::verify::Check("CDiplomacy::GetHostileMask", expected, self->GetHostileMask(player), "%d", player);
    return expected;
}

// ReadFrom / WriteTo: the original against a stream that only records (a Read fills nothing, so
// the table is unchanged), ours against the real stream through a recording proxy; the sequences of
// (slot, offset from the table, size) must match.
struct StreamCall {
    int slot;
    std::ptrdiff_t offset;
    unsigned size;
};
struct Recording {
    const void* object;
    StreamCall calls[4];
    int count;
};
struct RecordingStream {
    void* const* vtable;
    Recording* rec;
    void* real;
};
unsigned Record(RecordingStream* s, int slot, void* p, unsigned n) {
    Recording& r = *s->rec;
    if (r.count < 4) r.calls[r.count] = {slot, static_cast<char*>(p) - static_cast<const char*>(r.object), n};
    ++r.count;
    return s->real ? ei::VCall<unsigned>(s->real, slot, p, n) : n;
}
unsigned __fastcall RecordRead(RecordingStream* s, void*, void* p, unsigned n) { return Record(s, 1, p, n); }
unsigned __fastcall RecordWrite(RecordingStream* s, void*, void* p, unsigned n) { return Record(s, 2, p, n); }
void* const* RecordingVtable() {
    static void* vtable[8] = {};
    vtable[1] = reinterpret_cast<void*>(&RecordRead);
    vtable[2] = reinterpret_cast<void*>(&RecordWrite);
    return vtable;
}

using StreamFn = void(__fastcall*)(CDiplomacy*, void*, void*);
void CheckStream(const char* name, StreamFn original, void (*ours)(CDiplomacy*, void*), CDiplomacy* self,
                 void* stream) {
    Recording expected{self, {}, 0};
    RecordingStream dry{RecordingVtable(), &expected, nullptr};
    original(self, nullptr, &dry);
    Recording actual{self, {}, 0};
    RecordingStream proxy{RecordingVtable(), &actual, stream};
    ours(self, &proxy);
    bool same = expected.count == actual.count &&
                std::memcmp(expected.calls, actual.calls, sizeof(StreamCall) * (expected.count < 4 ? expected.count : 4)) == 0;
    ei::verify::Check(name, true, same, "%d vs %d calls", expected.count, actual.count);
}

// 004557C0 / 004557F0: PUSH ESI (1), MOV ESI,[ESP+8] (4).
StreamFn s_readFrom, s_writeTo;
void __fastcall ReadFromVerify(CDiplomacy* self, void*, void* stream) {
    CheckStream("CDiplomacy::ReadFrom", s_readFrom, [](CDiplomacy* d, void* s) { d->ReadFrom(s); }, self, stream);
}
void __fastcall WriteToVerify(CDiplomacy* self, void*, void* stream) {
    CheckStream("CDiplomacy::WriteTo", s_writeTo, [](CDiplomacy* d, void* s) { d->WriteTo(s); }, self, stream);
}

// 00455680 changes units (through 0052E940): only ours runs. Afterwards, unless the victim's player
// is allied with the attacker's, the victim and its party members up to the first empty entry must
// have the attacker's bit in their hostile masks and the victim flag 0x80000; otherwise the victim's
// mask and flags must be unchanged.
void* s_unused;
void __fastcall OnAttackVerify(CDiplomacy* self, void*, CUnit* attacker, CUnit* victim, int unused) {
    if (!attacker || !victim) {
        self->OnAttack(attacker, victim, unused);
        return;
    }
    std::uint32_t bit = Bit(FieldAt<int>(attacker, unitfield::kPlayer));
    bool allied = (self->allied_[FieldAt<int>(victim, unitfield::kPlayer)] & bit) != 0;
    std::uint32_t maskBefore = FieldAt<std::uint32_t>(victim, unitfield::kHostileMask);
    std::uint32_t flagsBefore = victim->flags_;
    self->OnAttack(attacker, victim, unused);
    bool same;
    if (allied) {
        same = FieldAt<std::uint32_t>(victim, unitfield::kHostileMask) == maskBefore && victim->flags_ == flagsBefore;
    } else {
        same = (FieldAt<std::uint32_t>(victim, unitfield::kHostileMask) & bit) && (victim->flags_ & 0x80000);
        if (char* party = FieldAt<char*>(victim, unitfield::kParty)) {
            // hash_map<key, CUnit*> at party +0x40: buckets vector at +4 / +8, nodes {next, key, unit}.
            auto** buckets = FieldAt<void***>(party, 0x44);
            auto** finish = FieldAt<void***>(party, 0x48);
            bool stop = false;
            for (void*** b = buckets; b < finish && !stop; ++b) {
                for (void** node = *b; node; node = static_cast<void**>(node[0])) {
                    auto* unit = static_cast<CUnit*>(node[2]);
                    if (!unit) {
                        stop = true;
                        break;
                    }
                    if (!(FieldAt<std::uint32_t>(unit, unitfield::kHostileMask) & bit)) same = false;
                }
            }
        }
    }
    ei::verify::Check("CDiplomacy::OnAttack", true, same, "attacker %p victim %p allied %d", attacker, victim, allied);
}

} // namespace

EI_VERIFY(0x004554B0, ResetVerify, 8, s_reset);
EI_VERIFY(0x004554E0, SetRelationVerify, 7, s_setRelation);
EI_VERIFY(0x00455630, GetRelationVerify, 7, s_getRelation);
EI_VERIFY(0x00455660, GetHostileMaskVerify, 6, s_getHostileMask);
EI_VERIFY(0x004557C0, ReadFromVerify, 5, s_readFrom);
EI_VERIFY(0x004557F0, WriteToVerify, 5, s_writeTo);
EI_VERIFY(0x00455680, OnAttackVerify, 0, s_unused);
