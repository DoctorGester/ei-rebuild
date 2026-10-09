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
#include "../../src/game/spell.h"
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

// --- Lights: ours only (the light manager and the factory's list change). ---
// - HideLight (005119B0) / ShowLight (00511950): a light that could change (not held, shown /
//   hidden) changes its hidden flag 0x80; any other light keeps its flags.
// - CreateLight (00511830): a light of the factory's class 0x31 exists with the given range, flags_
//   0x500 set, at the object's x and y.
// - SetLightMode (00511A00): mode 1 leaves a held light (0x200) whose id is on the factory's list;
//   mode 2 leaves none.
// - DestroyClientCompanion (005107B0): no light, no particles, no object.
// Reached by: the party light of a unit joining the player (CUnitClientSpecific slot 18) and its
// end on death (slot 48): units, combat, map_objects. SetLightMode's callers (slot 19) were not
// traced to a scenario.
namespace {
void* s_unusedHide;
void* s_unusedShow;
void* s_unusedCreate;
void* s_unusedMode;
void* s_unusedDestroy;

struct LitNode {
    LitNode* next;
    LitNode* prev;
    int id;
};

bool OnLitList(int id) {
    char* factory = *reinterpret_cast<char**>(0x007AF748);
    LitNode* head = *reinterpret_cast<LitNode**>(factory + 0xD4);
    for (LitNode* node = head->next; node != head; node = node->next)
        if (node->id == id) return true;
    return false;
}

void CheckToggle(const char* name, CMapObjectClientSpecific* self, bool show) {
    CEngineObject* light = self->light;
    std::uint32_t before = light ? light->flags_ : 0;
    if (show) self->ShowLight();
    else self->HideLight();
    bool ok = self->light == light;
    if (ok && light) {
        bool changes = !(before & 0x200) && (show ? (before & 0x80) != 0 : (before & 0x80) == 0);
        std::uint32_t expected = changes ? (show ? before & ~0x80u : before | 0x80u) : before;
        ok = light->flags_ == expected;
    }
    ei::verify::Check(name, true, ok, "flags 0x%X -> 0x%X", before, light ? light->flags_ : 0);
}
} // namespace

static void __fastcall HideLightVerify(CMapObjectClientSpecific* self) { CheckToggle("CMapObjectClientSpecific::HideLight", self, false); }
EI_VERIFY(0x005119B0, HideLightVerify, 0, s_unusedHide);

static void __fastcall ShowLightVerify(CMapObjectClientSpecific* self) { CheckToggle("CMapObjectClientSpecific::ShowLight", self, true); }
EI_VERIFY(0x00511950, ShowLightVerify, 0, s_unusedShow);

static void __fastcall CreateLightVerify(CMapObjectClientSpecific* self, void*, const float* colour, float radius, bool flag) {
    self->CreateLight(colour, radius, flag);
    const CMapObject* owner = CompanionOwner(self);
    auto* light = static_cast<CLight*>(self->light);
    bool ok = light && light->type_ == 0x31 && std::memcmp(&light->range_, &radius, 4) == 0 &&
              (light->flags_ & 0x500) == 0x500 && (!flag || (light->flags_ & 0x40)) &&
              std::memcmp(&light->position_.x, &owner->position.x, 4) == 0 &&
              std::memcmp(&light->position_.y, &owner->position.y, 4) == 0;
    ei::verify::Check("CMapObjectClientSpecific::CreateLight", true, ok, "object %d radius %g", owner->id_, radius);
}
EI_VERIFY(0x00511830, CreateLightVerify, 0, s_unusedCreate);

static void __fastcall SetLightModeVerify(CMapObjectClientSpecific* self, void*, int mode) {
    bool had = self->light != nullptr;
    self->SetLightMode(mode);
    bool ok = true;
    if (mode == 1) ok = self->light && (self->light->flags_ & 0x200) && OnLitList(self->light->id_);
    else if (mode == 2) ok = self->light == nullptr;
    ei::verify::Check("CMapObjectClientSpecific::SetLightMode", true, ok, "mode %d, light before %d", mode, had);
}
EI_VERIFY(0x00511A00, SetLightModeVerify, 0, s_unusedMode);

static void __fastcall DestroyClientCompanionVerify(CMapObjectClientSpecific* self) {
    self->DestroyClientCompanion();
    bool ok = !self->light && !self->object && self->particles.size == 0;
    ei::verify::Check("CMapObjectClientSpecific::DestroyClientCompanion", true, ok, "");
}
EI_VERIFY(0x005107B0, DestroyClientCompanionVerify, 0, s_unusedDestroy);
