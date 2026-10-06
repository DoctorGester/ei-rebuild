// Checks for map_object_client_effects.cpp (A-U3).
// - ResetPartAlpha (0051CCA0) / ClearPartTints (0051CD60): the original runs, the drawn parts'
//   alpha, tint and flags_ are kept and put back, ours runs; compared bit for bit.
// - UpdateEffectParticles (00510B80): ours only (it makes particle sources and rings): every record
//   left is marked present; a merging type has one record at the highest power of its effects, any
//   other type one record per effect; an invisibility / stun record leaves its flag set on the object.
// - UpdateEffectSounds (005112A0): ours only (it plays sounds): one record per effect type, all
//   marked present.
// - PlayEffectSound (005114D0): ours only: stench's timer is 0; feeble mind's sequence handle
//   toggles (a stopped one is -1).
// - EffectsChanged (005117D0, slot 13): ours only (the two above check themselves).
// Reached by: the network state's effect part (part 3) on the client: every scenario where a spell
// with a lasting effect lands (spells_cast, effects, combat with heal / stun); invisibility /
// stun ending reaches the part resets (spells_cast).
#include "verify.h"

#include "../../src/game/map_object.h"
#include "../../src/game/map_object_figures.h"
#include "../../src/game/map_object_shadow.h"
#include "../../src/game/unit.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {
void* s_unusedParticles;
void* s_unusedSounds;
void* s_unusedPlay;
void* s_unusedChanged;

struct PartState {
    float alpha;
    float tint[3];
    std::uint32_t flags;
};

std::vector<PartState> Parts(const CObject3DClientSpecific* half) {
    std::vector<PartState> parts;
    for (int i = 0; i < half->drawnPartCount; ++i) {
        const FigurePart* part = half->drawnParts[i];
        PartState state;
        state.alpha = part->alpha;
        std::memcpy(state.tint, part->tint, sizeof(state.tint));
        state.flags = part->flags_;
        parts.push_back(state);
    }
    return parts;
}

void PutBack(CObject3DClientSpecific* half, const std::vector<PartState>& parts) {
    for (int i = 0; i < half->drawnPartCount; ++i) {
        FigurePart* part = half->drawnParts[i];
        part->alpha = parts[i].alpha;
        std::memcpy(part->tint, parts[i].tint, sizeof(part->tint));
        part->flags_ = parts[i].flags;
    }
}

using PartReset = void(__fastcall*)(CObject3DClientSpecific*);

void ComparePartReset(const char* name, PartReset original, CObject3DClientSpecific* self, bool tints) {
    std::vector<PartState> before = Parts(self);
    original(self);
    std::vector<PartState> expected = Parts(self);
    PutBack(self, before);
    if (tints) self->ClearPartTints();
    else self->ResetPartAlpha();
    std::vector<PartState> actual = Parts(self);
    bool same = expected.size() == actual.size() &&
                (expected.empty() || std::memcmp(expected.data(), actual.data(), expected.size() * sizeof(PartState)) == 0);
    ei::verify::Check(name, true, same, "%d parts", self->drawnPartCount);
}

bool Merges(int type) {
    return (type >= 0x09 && type <= 0x0E) || type == 0x10 || type == 0x13 || type == 0x19 || type == 0x1A ||
           (type >= 0x1D && type <= 0x22);
}
} // namespace

// 0051CCA0: MOV EDX,[ECX+0x3C]; XOR EAX,EAX (5 bytes).
static PartReset s_resetPartAlpha;
static void __fastcall ResetPartAlphaVerify(CObject3DClientSpecific* self) {
    ComparePartReset("CObject3DClientSpecific::ResetPartAlpha", s_resetPartAlpha, self, false);
}
EI_VERIFY(0x0051CCA0, ResetPartAlphaVerify, 5, s_resetPartAlpha);

// 0051CD60: MOV EDX,[ECX+0x3C]; PUSH ESI; XOR ESI,ESI (6 bytes).
static PartReset s_clearPartTints;
static void __fastcall ClearPartTintsVerify(CObject3DClientSpecific* self) {
    ComparePartReset("CObject3DClientSpecific::ClearPartTints", s_clearPartTints, self, true);
}
EI_VERIFY(0x0051CD60, ClearPartTintsVerify, 6, s_clearPartTints);

static void __fastcall UpdateEffectParticlesVerify(CMapObjectClientSpecific* self, void*, bool first) {
    self->UpdateEffectParticles(first);
    const CMapObject* owner = CompanionOwner(self);
    int bad = -1;
    for (int i = 0; i < self->particles.size && bad < 0; ++i) {
        const EffectParticle& record = self->particles.data[i];
        int effects = 0;
        int power = -1;
        for (int e = 0; e < owner->effectCount; ++e) {
            if (owner->effects[e].type != record.type) continue;
            ++effects;
            if (owner->effects[e].power > power) power = owner->effects[e].power;
        }
        int records = 0;
        for (int j = 0; j < self->particles.size; ++j) records += self->particles.data[j].type == record.type;
        bool ok = record.present == 1 && effects > 0 && records == (Merges(record.type) ? 1 : effects);
        if (Merges(record.type)) ok = ok && record.power == power;
        if (record.type == 0x0F) ok = ok && (owner->flags_ & 0x10000) != 0;
        if (record.type == 0x19) ok = ok && (owner->flags_ & 0x40000) != 0;
        if (!ok) bad = i;
    }
    ei::verify::Check("CMapObjectClientSpecific::UpdateEffectParticles", -1, bad, "object %d: %d effects, %d records%s",
                      owner->id_, owner->effectCount, self->particles.size, first ? " (first)" : "");
}
EI_VERIFY(0x00510B80, UpdateEffectParticlesVerify, 0, s_unusedParticles);

static void __fastcall UpdateEffectSoundsVerify(CMapObjectClientSpecific* self, void*, bool first) {
    self->UpdateEffectSounds(first);
    const CMapObject* owner = CompanionOwner(self);
    int types = 0;
    for (int e = 0; e < owner->effectCount; ++e) {
        bool seen = false;
        for (int f = 0; f < e && !seen; ++f) seen = owner->effects[f].type == owner->effects[e].type;
        types += !seen;
    }
    bool ok = self->sounds.size == types;
    for (int i = 0; ok && i < self->sounds.size; ++i) {
        const EffectSoundRecord& record = self->sounds.data[i];
        bool found = false;
        for (int e = 0; e < owner->effectCount && !found; ++e) found = owner->effects[e].type == record.kind;
        int same = 0;
        for (int j = 0; j < self->sounds.size; ++j) same += self->sounds.data[j].kind == record.kind;
        ok = found && same == 1 && record.present == 1;
    }
    ei::verify::Check("CMapObjectClientSpecific::UpdateEffectSounds", true, ok, "object %d: %d types, %d records",
                      owner->id_, types, self->sounds.size);
}
EI_VERIFY(0x005112A0, UpdateEffectSoundsVerify, 0, s_unusedSounds);

static void __fastcall PlayEffectSoundVerify(bool play, EffectSoundRecord* record, const float* position, int kind,
                                             const char* suffix) {
    int handle = record->handle;
    PlayEffectSound(play, record, position, kind, suffix);
    bool ok = true;
    if (kind == 0x13) ok = record->timer == 0;
    else if (kind == 0x20 && handle != -1) ok = record->handle == -1;
    else ok = record->handle == handle || kind == 0x20;
    ei::verify::Check("PlayEffectSound", true, ok, "kind 0x%X%s, handle %d -> %d", kind, suffix, handle, record->handle);
}
EI_VERIFY(0x005114D0, PlayEffectSoundVerify, 0, s_unusedPlay);

static void __fastcall EffectsChangedVerify(CMapObjectClientSpecific* self, void*, bool first) {
    self->EffectsChanged(first);
    ei::verify::Check("CMapObjectClientSpecific::EffectsChanged", true, true, "");
}
EI_VERIFY(0x005117D0, EffectsChangedVerify, 0, s_unusedChanged);
