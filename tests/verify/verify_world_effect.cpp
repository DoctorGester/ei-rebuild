// Checks for the effect objects (world_effect.cpp, world_effect_visual.cpp).
//
// - Getters run both versions and compare (the float results bit for bit).
// - Setters, Position and the ticks whose only outside call is idempotent (moving a playing sound)
//   run ours, snapshot the object (and the particle source's position), restore, run the original
//   and compare.
// - Pack runs ours into the real state field, saves the four streams' headers and bytes, restores
//   them, runs the original and compares (skipped when ours reallocated a stream buffer).
// - Save runs the original against a recording stand-in stream and ours against a forwarding
//   recorder; the written bytes must match. Load runs the original on a scratch copy of the object
//   against a stand-in that reads nothing, ours through a forwarding recorder; the sequence of
//   (offset in the object, size) must match.
// - Everything that starts sounds, lights or particle sources, releases references or consumes the
//   network reader runs ours only, with checks of what must hold afterwards.
#include "fake_stream.h"
#include "verify.h"

#include "../../src/game/random.h"
#include "../../src/game/world_effect.h"

#include "ei/vcall.h"

#include <cstring>
#include <vector>

namespace {

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
std::uint64_t Bits(double d) {
    std::uint64_t u;
    std::memcpy(&u, &d, 8);
    return u;
}

// A restorable copy of up to two memory regions.
struct Snapshot {
    std::vector<unsigned char> a, b;
    void* pa;
    void* pb;
    Snapshot(void* regionA, int sizeA, void* regionB, int sizeB) : pa(regionA), pb(regionB) {
        a.assign(static_cast<unsigned char*>(pa), static_cast<unsigned char*>(pa) + sizeA);
        if (pb) b.assign(static_cast<unsigned char*>(pb), static_cast<unsigned char*>(pb) + sizeB);
    }
    void Restore() const {
        std::memcpy(pa, a.data(), a.size());
        if (pb) std::memcpy(pb, b.data(), b.size());
    }
    int FirstDifference() const {
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i] != static_cast<unsigned char*>(pa)[i]) return static_cast<int>(i);
        if (pb)
            for (std::size_t i = 0; i < b.size(); ++i)
                if (b[i] != static_cast<unsigned char*>(pb)[i]) return 0x10000 + static_cast<int>(i);
        return -1;
    }
};

// Ours, snapshot, restore, original; the bytes and (with compareResult) the results must match.
template <typename R, typename Ours, typename Original>
R SameWrites(const char* name, void* regionA, int sizeA, void* regionB, int sizeB, bool compareResult, Ours ours,
             Original original) {
    Snapshot before(regionA, sizeA, regionB, sizeB);
    R actual = ours();
    Snapshot after(regionA, sizeA, regionB, sizeB);
    before.Restore();
    R expected = original();
    int diff = after.FirstDifference();
    bool same = diff < 0 && (!compareResult || expected == actual);
    ei::verify::Check(name, true, same, "object %p: result %X vs %X, first difference at +0x%X", regionA,
                      static_cast<unsigned>(expected), static_cast<unsigned>(actual), diff);
    return expected;
}

// --- Recording streams (slot 1 Read, slot 2 Write, +8 the save version) ---------------------------

struct StreamCall {
    int slot;
    int offset;                       // from the object, or -1 when outside it
    unsigned size;
    std::vector<unsigned char> data;  // Write only
};

struct RecordingStream {
    void* const* vtable;
    int unused;
    int version;                      // +8, as the real stream's
    const void* object;
    std::size_t objectSize;
    void* forward;                    // real stream, or null (reads nothing, writes nowhere)
    std::vector<StreamCall>* calls;

    void Record(int slot, const void* p, unsigned n, bool keepData) {
        StreamCall c{slot, -1, n, {}};
        auto at = static_cast<const char*>(p) - static_cast<const char*>(object);
        if (at >= 0 && static_cast<std::size_t>(at) < objectSize) c.offset = static_cast<int>(at);
        if (keepData) c.data.assign(static_cast<const unsigned char*>(p), static_cast<const unsigned char*>(p) + n);
        calls->push_back(std::move(c));
    }
};
static_assert(offsetof(RecordingStream, version) == 8);

unsigned __fastcall StreamRead(RecordingStream* s, void*, void* p, unsigned n) {
    s->Record(1, p, n, false);
    return s->forward ? ei::VCall<unsigned>(s->forward, 1, p, n) : n;
}
void __fastcall StreamWrite(RecordingStream* s, void*, const void* p, unsigned n) {
    s->Record(2, p, n, true);
    if (s->forward) ei::VCall<void>(s->forward, 2, p, n);
}
void* const* StreamVtable() {
    static void* vtable[16] = {};
    vtable[1] = reinterpret_cast<void*>(&StreamRead);
    vtable[2] = reinterpret_cast<void*>(&StreamWrite);
    return vtable;
}

bool SameCalls(const std::vector<StreamCall>& a, const std::vector<StreamCall>& b, bool compareData, bool compareOffsets) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].slot != b[i].slot || a[i].size != b[i].size) return false;
        if (compareOffsets && a[i].offset != b[i].offset) return false;
        if (compareData && a[i].data != b[i].data) return false;
    }
    return true;
}

// Save: the original records into a stand-in, ours writes through a recorder to the real stream.
template <typename Original, typename Ours>
void CheckSave(const char* name, void* self, std::size_t size, void* stream, Original original, Ours ours) {
    std::vector<StreamCall> expected, actual;
    RecordingStream dry{StreamVtable(), 0, static_cast<int*>(stream)[2], self, size, nullptr, &expected};
    original(&dry);
    RecordingStream proxy{StreamVtable(), 0, static_cast<int*>(stream)[2], self, size, stream, &actual};
    ours(&proxy);
    ei::verify::Check(name, true, SameCalls(expected, actual, true, false), "object %p: %u vs %u writes", self,
                      static_cast<unsigned>(expected.size()), static_cast<unsigned>(actual.size()));
}

// Load: the original on a scratch copy against a stand-in that reads nothing, ours through a
// recorder on the real object; the reads (offset, size) must match.
template <typename Original, typename Ours>
void CheckLoad(const char* name, void* self, std::size_t size, void* stream, Original original, Ours ours) {
    std::vector<unsigned char> scratch(static_cast<unsigned char*>(self), static_cast<unsigned char*>(self) + size);
    std::vector<StreamCall> expected, actual;
    RecordingStream dry{StreamVtable(), 0, static_cast<int*>(stream)[2], scratch.data(), size, nullptr, &expected};
    original(scratch.data(), &dry);
    RecordingStream proxy{StreamVtable(), 0, static_cast<int*>(stream)[2], self, size, stream, &actual};
    ours(&proxy);
    ei::verify::Check(name, true, SameCalls(expected, actual, false, true), "object %p: %u vs %u reads", self,
                      static_cast<unsigned>(expected.size()), static_cast<unsigned>(actual.size()));
}

// --- Pack: the field's four streams compared ---------------------------------------------------------

struct StreamState {
    BitStream header;
    std::vector<unsigned char> bytes;
};
struct FieldState {
    StreamState s[4];
    explicit FieldState(DeltaField* f) {
        BitStream* streams[4] = {&f->snapshot[0], &f->snapshot[1], &f->out[0], &f->out[1]};
        for (int i = 0; i < 4; ++i) {
            RawCopy(&s[i].header, streams[i]);
            if (streams[i]->data_)
                s[i].bytes.assign(streams[i]->data_, streams[i]->data_ + streams[i]->capacity_);
        }
    }
    void Restore(DeltaField* f) const {
        BitStream* streams[4] = {&f->snapshot[0], &f->snapshot[1], &f->out[0], &f->out[1]};
        for (int i = 0; i < 4; ++i) {
            RawCopy(streams[i], &s[i].header);
            if (!s[i].bytes.empty()) std::memcpy(streams[i]->data_, s[i].bytes.data(), s[i].bytes.size());
        }
    }
    bool SameBuffers(const FieldState& o) const {
        for (int i = 0; i < 4; ++i)
            if (s[i].header.data_ != o.s[i].header.data_ || s[i].header.capacity_ != o.s[i].header.capacity_) return false;
        return true;
    }
    // Headers and the bytes up to each stream's size equal.
    bool SameAs(const FieldState& o) const {
        for (int i = 0; i < 4; ++i) {
            if (std::memcmp(&s[i].header, &o.s[i].header, sizeof(BitStream)) != 0) return false;
            std::size_t n = s[i].header.size_ < s[i].bytes.size() ? s[i].header.size_ : s[i].bytes.size();
            if (n && std::memcmp(s[i].bytes.data(), o.s[i].bytes.data(), n) != 0) return false;
        }
        return true;
    }
};

template <typename Ours, typename Original>
void CheckPack(const char* name, void* self, DeltaField* field, Ours ours, Original original) {
    FieldState before(field);
    ours();
    FieldState after(field);
    if (!before.SameBuffers(after)) return;   // ours reallocated: the original's run cannot be repeated
    before.Restore(field);
    original();
    FieldState expected(field);
    ei::verify::Check(name, true, expected.SameAs(after), "object %p", self);
}

void* s_unused;

bool BaseCleared(const CEffect* e) {
    return Bits(e->localPosition_.x) == 0 && Bits(e->localPosition_.y) == 0 && Bits(e->localPosition_.z) == 0 &&
           Bits(e->velocity_.x) == 0 && Bits(e->velocity_.y) == 0 && Bits(e->velocity_.z) == 0 && e->startTick_ == 0 &&
           e->moving_ == 0 && e->changeStamp_ == -1 && e->state_.snapshot[0].size_ == 0 &&
           e->state_.out[0].size_ == 0;
}

} // namespace

// --- CEffect ----------------------------------------------------------------------------------------

static void __fastcall SendFullVerify(CEffect* self) { self->SendFull(); }
EI_VERIFY(0x00675B40, SendFullVerify, 0, s_unused);
static void __fastcall SendDeltaVerify(CEffect* self) { self->SendDelta(); }
EI_VERIFY(0x00675B70, SendDeltaVerify, 0, s_unused);
static int __fastcall ReceiveVerify(CEffect* self, void*, int update, BitStream* reader) {
    int result = self->Receive(update, reader);
    ei::verify::Check("CEffect::Receive", true, result == 0 || result == 1, "result %d", result);
    return result;
}
EI_VERIFY(0x00675BA0, ReceiveVerify, 0, s_unused);

static void __fastcall ResetBaseVerify(CEffect* self) {
    self->ResetBase();
    ei::verify::Check("CEffect::ResetBase", true, BaseCleared(self), "object %p", self);
}
EI_VERIFY(0x00675BE0, ResetBaseVerify, 0, s_unused);

// 00675CA0: MOV EAX,[ECX+8]; OR AL,2 (5 bytes).
static void(__fastcall* s_markChanged)(CEffect*);
static void __fastcall MarkChangedVerify(CEffect* self) {
    SameWrites<int>("CEffect::MarkChanged", self, sizeof(CEffect), nullptr, 0, false,
                    [&] { self->MarkChanged(); return 0; }, [&] { s_markChanged(self); return 0; });
}
EI_VERIFY(0x00675CA0, MarkChangedVerify, 5, s_markChanged);

static int __fastcall CreateEffectVerify(CEffect* self) {
    int result = self->CreateEffect();
    ei::verify::Check("CEffect::CreateEffect", 1, result, "object %p", self);
    return result;
}
EI_VERIFY(0x00675D10, CreateEffectVerify, 0, s_unused);
static void __fastcall DestroyEffectVerify(CEffect* self) { self->DestroyEffect(); }
EI_VERIFY(0x00675D40, DestroyEffectVerify, 0, s_unused);

// 00675D50: MOV EDX,[ESP+4]; LEA EAX,[ECX+98h] (10 bytes).
static void(__fastcall* s_setVelocity)(CEffect*, void*, const Vector3*);
static void __fastcall SetVelocityVerify(CEffect* self, void*, const Vector3* v) {
    Vector3 copy = *v;   // the argument may point into the object
    SameWrites<int>("CEffect::SetVelocity", self, sizeof(CEffect), nullptr, 0, false,
                    [&] { self->SetVelocity(&copy); return 0; }, [&] { s_setVelocity(self, nullptr, &copy); return 0; });
}
EI_VERIFY(0x00675D50, SetVelocityVerify, 10, s_setVelocity);

// 00675FA0: LEA EAX,[ECX+1Ch]; RET 4 (6 bytes).
static Vector3*(__fastcall* s_position)(CEffect*, void*, void*);
static Vector3* __fastcall PositionVerify(CEffect* self, void*, void* group) {
    Vector3* expected = s_position(self, nullptr, group);
    Vector3* actual = self->Position(group);
    ei::verify::Check("CEffect::Position", expected, actual, "object %p", self);
    return expected;
}
EI_VERIFY(0x00675FA0, PositionVerify, 6, s_position);

// Float getters: FLD [...] (6 bytes).
static double(__fastcall* s_defaultRadius)(CEffect*);
static double __fastcall DefaultRadiusVerify(CEffect* self) {
    double expected = s_defaultRadius(self);
    double actual = self->DefaultRadius();
    ei::verify::Check("CEffect::DefaultRadius", Bits(expected), Bits(actual), "object %p", self);
    return expected;
}
EI_VERIFY(0x006768F0, DefaultRadiusVerify, 6, s_defaultRadius);

#define EI_RADIUS_VERIFY(addr, Class, label)                                                       \
    static double(__fastcall* s_radius_##Class)(Class*);                                            \
    static double __fastcall RadiusVerify_##Class(Class* self) {                                    \
        double expected = s_radius_##Class(self);                                                   \
        double actual = self->Radius();                                                             \
        ei::verify::Check(label, Bits(expected), Bits(actual), "object %p", self);                  \
        return expected;                                                                            \
    }                                                                                               \
    EI_VERIFY(addr, RadiusVerify_##Class, 6, s_radius_##Class)

EI_RADIUS_VERIFY(0x00676050, CEffectSound, "CEffectSound::Radius");
EI_VERIFY(0x00676050, RadiusVerify_CEffectSound, 6, s_radius_CEffectSound);
EI_RADIUS_VERIFY(0x00676930, CEffectSoundSeq, "CEffectSoundSeq::Radius");
EI_VERIFY(0x00676930, RadiusVerify_CEffectSoundSeq, 6, s_radius_CEffectSoundSeq);
EI_RADIUS_VERIFY(0x00677690, CEffectAmbientSound, "CEffectAmbientSound::Radius");
EI_VERIFY(0x00677690, RadiusVerify_CEffectAmbientSound, 6, s_radius_CEffectAmbientSound);
EI_RADIUS_VERIFY(0x00677F70, CEffectPointLight, "CEffectPointLight::Radius");
EI_VERIFY(0x00677F70, RadiusVerify_CEffectPointLight, 6, s_radius_CEffectPointLight);

// --- Pack, Load, Save (every class) ----------------------------------------------------------------

#define EI_PACK_VERIFY(addr, Class, stolen, label)                                                  \
    static void(__fastcall* s_pack_##Class)(Class*, void*, DeltaField*);                            \
    static void __fastcall PackVerify_##Class(Class* self, void*, DeltaField* field) {              \
        CheckPack(label, self, field, [&] { self->Class::Pack(field); },                            \
                  [&] { s_pack_##Class(self, nullptr, field); });                                   \
    }                                                                                               \
    EI_VERIFY(addr, PackVerify_##Class, stolen, s_pack_##Class)

EI_PACK_VERIFY(0x00676550, CEffectSound, 6, "CEffectSound::Pack");
EI_VERIFY(0x00676550, PackVerify_CEffectSound, 6, s_pack_CEffectSound);
EI_PACK_VERIFY(0x00677180, CEffectSoundSeq, 5, "CEffectSoundSeq::Pack");
EI_VERIFY(0x00677180, PackVerify_CEffectSoundSeq, 5, s_pack_CEffectSoundSeq);
EI_PACK_VERIFY(0x00677B20, CEffectAmbientSound, 5, "CEffectAmbientSound::Pack");
EI_VERIFY(0x00677B20, PackVerify_CEffectAmbientSound, 5, s_pack_CEffectAmbientSound);
EI_PACK_VERIFY(0x00678610, CEffectPointLight, 6, "CEffectPointLight::Pack");
EI_VERIFY(0x00678610, PackVerify_CEffectPointLight, 6, s_pack_CEffectPointLight);
EI_PACK_VERIFY(0x006791A0, CEffectParticle, 6, "CEffectParticle::Pack");
EI_VERIFY(0x006791A0, PackVerify_CEffectParticle, 6, s_pack_CEffectParticle);
EI_PACK_VERIFY(0x00679DB0, CEffectWall, 6, "CEffectWall::Pack");
EI_VERIFY(0x00679DB0, PackVerify_CEffectWall, 6, s_pack_CEffectWall);
EI_PACK_VERIFY(0x0067A600, CEffectLightning, 6, "CEffectLightning::Pack");
EI_VERIFY(0x0067A600, PackVerify_CEffectLightning, 6, s_pack_CEffectLightning);

#define EI_SAVE_VERIFY(addr, Class, stolen, label)                                                  \
    static void(__fastcall* s_save_##Class)(Class*, void*, void*);                                  \
    static void __fastcall SaveVerify_##Class(Class* self, void*, void* stream) {                   \
        CheckSave(label, self, sizeof(Class), stream,                                               \
                  [&](void* s) { s_save_##Class(self, nullptr, s); },                               \
                  [&](void* s) { self->Class::SaveState(s); });                                     \
    }                                                                                               \
    EI_VERIFY(addr, SaveVerify_##Class, stolen, s_save_##Class)

EI_SAVE_VERIFY(0x006766D0, CEffectSound, 5, "CEffectSound::SaveState");
EI_VERIFY(0x006766D0, SaveVerify_CEffectSound, 5, s_save_CEffectSound);
EI_SAVE_VERIFY(0x00677430, CEffectSoundSeq, 5, "CEffectSoundSeq::SaveState");
EI_VERIFY(0x00677430, SaveVerify_CEffectSoundSeq, 5, s_save_CEffectSoundSeq);
EI_SAVE_VERIFY(0x00677C60, CEffectAmbientSound, 5, "CEffectAmbientSound::SaveState");
EI_VERIFY(0x00677C60, SaveVerify_CEffectAmbientSound, 5, s_save_CEffectAmbientSound);
EI_SAVE_VERIFY(0x00678740, CEffectPointLight, 5, "CEffectPointLight::SaveState");
EI_VERIFY(0x00678740, SaveVerify_CEffectPointLight, 5, s_save_CEffectPointLight);
EI_SAVE_VERIFY(0x006794E0, CEffectParticle, 5, "CEffectParticle::SaveState");
EI_VERIFY(0x006794E0, SaveVerify_CEffectParticle, 5, s_save_CEffectParticle);
EI_SAVE_VERIFY(0x00679ED0, CEffectWall, 5, "CEffectWall::SaveState");
EI_VERIFY(0x00679ED0, SaveVerify_CEffectWall, 5, s_save_CEffectWall);
EI_SAVE_VERIFY(0x0067A7F0, CEffectLightning, 5, "CEffectLightning::SaveState");
EI_VERIFY(0x0067A7F0, SaveVerify_CEffectLightning, 5, s_save_CEffectLightning);

#define EI_LOAD_VERIFY(addr, Class, stolen, label)                                                  \
    static void(__fastcall* s_load_##Class)(void*, void*, void*);                                   \
    static void __fastcall LoadVerify_##Class(Class* self, void*, void* stream) {                   \
        CheckLoad(label, self, sizeof(Class), stream,                                               \
                  [&](void* scratch, void* s) { s_load_##Class(scratch, nullptr, s); },             \
                  [&](void* s) { self->Class::LoadState(s); });                                     \
    }                                                                                               \
    EI_VERIFY(addr, LoadVerify_##Class, stolen, s_load_##Class)

EI_LOAD_VERIFY(0x006765F0, CEffectSound, 6, "CEffectSound::LoadState");
EI_VERIFY(0x006765F0, LoadVerify_CEffectSound, 6, s_load_CEffectSound);
EI_LOAD_VERIFY(0x00678690, CEffectPointLight, 6, "CEffectPointLight::LoadState");
EI_VERIFY(0x00678690, LoadVerify_CEffectPointLight, 6, s_load_CEffectPointLight);
EI_LOAD_VERIFY(0x006793E0, CEffectParticle, 6, "CEffectParticle::LoadState");
EI_VERIFY(0x006793E0, LoadVerify_CEffectParticle, 6, s_load_CEffectParticle);
EI_LOAD_VERIFY(0x0067A770, CEffectLightning, 5, "CEffectLightning::LoadState");
EI_VERIFY(0x0067A770, LoadVerify_CEffectLightning, 5, s_load_CEffectLightning);
EI_LOAD_VERIFY(0x00679EA0, CEffectWall, 6, "CEffectWall::LoadState");
EI_VERIFY(0x00679EA0, LoadVerify_CEffectWall, 6, s_load_CEffectWall);

// The string-array loaders read through helpers that use more of the stream: ours only.
static void __fastcall SoundSeqLoadVerify(CEffectSoundSeq* self, void*, void* stream) { self->LoadState(stream); }
EI_VERIFY(0x006772D0, SoundSeqLoadVerify, 0, s_unused);
static void __fastcall AmbientLoadVerify(CEffectAmbientSound* self, void*, void* stream) { self->LoadState(stream); }
EI_VERIFY(0x00677BF0, AmbientLoadVerify, 0, s_unused);

// --- Unpack and Start: ours only (they consume the reader and start sounds, lights, sources) -------

#define EI_UNPACK_VERIFY(addr, Class, label)                                                        \
    static int __fastcall UnpackVerify_##Class(Class* self, void*, int update, DeltaField* field) { \
        int result = self->Class::Unpack(update, field);                                            \
        ei::verify::Check(label, true, result == 0 || result == 1, "result %d", result);            \
        return result;                                                                              \
    }                                                                                               \
    EI_VERIFY(addr, UnpackVerify_##Class, 0, s_unused)

EI_UNPACK_VERIFY(0x00676430, CEffectSound, "CEffectSound::Unpack");
EI_VERIFY(0x00676430, UnpackVerify_CEffectSound, 0, s_unused);
EI_UNPACK_VERIFY(0x006779D0, CEffectAmbientSound, "CEffectAmbientSound::Unpack");
EI_VERIFY(0x006779D0, UnpackVerify_CEffectAmbientSound, 0, s_unused);
EI_UNPACK_VERIFY(0x00678520, CEffectPointLight, "CEffectPointLight::Unpack");
EI_VERIFY(0x00678520, UnpackVerify_CEffectPointLight, 0, s_unused);
EI_UNPACK_VERIFY(0x00678D30, CEffectParticle, "CEffectParticle::Unpack");
EI_VERIFY(0x00678D30, UnpackVerify_CEffectParticle, 0, s_unused);
EI_UNPACK_VERIFY(0x00679C70, CEffectWall, "CEffectWall::Unpack");
EI_VERIFY(0x00679C70, UnpackVerify_CEffectWall, 0, s_unused);
EI_UNPACK_VERIFY(0x0067A490, CEffectLightning, "CEffectLightning::Unpack");
EI_VERIFY(0x0067A490, UnpackVerify_CEffectLightning, 0, s_unused);

static void __fastcall SoundStartVerify(CEffectSound* self, void*, int update) { self->Start(update); }
EI_VERIFY(0x006763A0, SoundStartVerify, 0, s_unused);
static void __fastcall AmbientStartVerify(CEffectAmbientSound* self, void*, int update) { self->Start(update); }
EI_VERIFY(0x00677950, AmbientStartVerify, 0, s_unused);
static void __fastcall LightStartVerify(CEffectPointLight* self, void*, int update) {
    self->Start(update);
    bool ok = self->light_ != nullptr &&
              ((self->light_->flags_ & 0x400) != 0) == !(self->range_ >= 0.0f);
    ei::verify::Check("CEffectPointLight::Start", true, ok, "object %p update %d", self, update);
}
EI_VERIFY(0x006782D0, LightStartVerify, 0, s_unused);
static void __fastcall ParticleStartVerify(CEffectParticle* self, void*, int update) { self->Start(update); }
EI_VERIFY(0x00678CF0, ParticleStartVerify, 0, s_unused);
static void __fastcall ParticleSpawnVerify(CEffectParticle* self) {
    self->Spawn();
    ei::verify::Check("CEffectParticle::Spawn", true, self->source_ != nullptr, "object %p type %X", self, self->type_);
}
EI_VERIFY(0x00678EC0, ParticleSpawnVerify, 0, s_unused);
static void __fastcall ParticleAttachVerify(CEffectParticle* self) { self->Attach(); }
EI_VERIFY(0x00679350, ParticleAttachVerify, 0, s_unused);
static void __fastcall LightningStartVerify(CEffectLightning* self, void*, int update) { self->Start(update); }
EI_VERIFY(0x0067A370, LightningStartVerify, 0, s_unused);

// --- Reset: ours only (stops sounds, removes lights, releases references) -------------------------

static void __fastcall SoundResetVerify(CEffectSound* self) {
    self->Reset();
    bool ok = BaseCleared(self) && self->sound_ == -1 && self->looping_ == 1 && self->name_[0] == 0 &&
              self->fresh_ == 1 && self->noFade_ == 0;
    ei::verify::Check("CEffectSound::Reset", true, ok, "object %p", self);
}
EI_VERIFY(0x00676200, SoundResetVerify, 0, s_unused);
static void __fastcall SoundSeqResetVerify(CEffectSoundSeq* self) {
    self->Reset();
    bool ok = BaseCleared(self) && self->sound_ == -1 && self->names_.size == 0 && self->name_[0] == 0;
    ei::verify::Check("CEffectSoundSeq::Reset", true, ok, "object %p", self);
}
EI_VERIFY(0x00676B10, SoundSeqResetVerify, 0, s_unused);
static void __fastcall AmbientResetVerify(CEffectAmbientSound* self) {
    self->Reset();
    bool ok = BaseCleared(self) && self->names_.size == 0 && self->unknown_168 == 0 && self->unknown_16C == 0;
    ei::verify::Check("CEffectAmbientSound::Reset", true, ok, "object %p", self);
}
EI_VERIFY(0x00677870, AmbientResetVerify, 0, s_unused);
static void __fastcall LightResetVerify(CEffectPointLight* self) {
    self->Reset();
    bool ok = BaseCleared(self) && self->light_ == nullptr && self->lightFlags_ == 0;
    ei::verify::Check("CEffectPointLight::Reset", true, ok, "object %p", self);
}
EI_VERIFY(0x006780C0, LightResetVerify, 0, s_unused);
static void __fastcall ParticleResetVerify(CEffectParticle* self) {
    self->Reset();
    bool ok = BaseCleared(self) && self->source_ == nullptr && self->attachedTo_ == -1 && self->unitId_ == -1 &&
              Bits(self->size_) == 0x3F800000u;
    ei::verify::Check("CEffectParticle::Reset", true, ok, "object %p", self);
}
EI_VERIFY(0x00678A90, ParticleResetVerify, 0, s_unused);
static void __fastcall WallResetVerify(CEffectWall* self) {
    self->Reset();
    bool ok = BaseCleared(self) && self->source_ == nullptr && Bits(self->direction_.x) == 0;
    ei::verify::Check("CEffectWall::Reset", true, ok, "object %p", self);
}
EI_VERIFY(0x00679AF0, WallResetVerify, 0, s_unused);
static void __fastcall LightningResetVerify(CEffectLightning* self) {
    self->Reset();
    bool ok = BaseCleared(self) && self->bolt_ == nullptr && self->toObject_ == -1 && self->fromObject_ == -1 &&
              Bits(self->width_) == 0x3F800000u;
    ei::verify::Check("CEffectLightning::Reset", true, ok, "object %p", self);
}
EI_VERIFY(0x0067A1B0, LightningResetVerify, 0, s_unused);

// --- Ticks -------------------------------------------------------------------------------------------

// 006762F0: SUB ESP,0Ch; PUSH EBX; PUSH ESI (5 bytes). Moving the playing sound twice is harmless.
static int(__fastcall* s_soundTick)(CEffectSound*);
static int __fastcall SoundTickVerify(CEffectSound* self) {
    return SameWrites<int>("CEffectSound::Tick", self, sizeof(CEffectSound), nullptr, 0, true,
                           [&] { return self->Tick(); }, [&] { return s_soundTick(self); });
}
EI_VERIFY(0x006762F0, SoundTickVerify, 5, s_soundTick);

// 00677250: MOV EAX,[ECX+0A4h] (6 bytes).
static int(__fastcall* s_seqTick)(CEffectSoundSeq*);
static int __fastcall SoundSeqTickVerify(CEffectSoundSeq* self) {
    return SameWrites<int>("CEffectSoundSeq::Tick", self, sizeof(CEffectSoundSeq), nullptr, 0, true,
                           [&] { return self->Tick(); }, [&] { return s_seqTick(self); });
}
EI_VERIFY(0x00677250, SoundSeqTickVerify, 6, s_seqTick);

// Ours only (the light is moved or re-registered): the position advanced by the velocity, and the
// light (if any) placed there.
static int __fastcall LightTickVerify(CEffectPointLight* self) {
    Vector3 before = self->localPosition_;
    int moving = self->moving_;
    int result = self->Tick();
    bool ok = result == 1;
    if (moving) {
        float x = before.x + self->velocity_.x;
        ok = ok && Bits(self->localPosition_.x) == Bits(x);
        if (self->light_)
            ok = ok && std::memcmp(reinterpret_cast<char*>(self->light_) + 0x1C, &self->localPosition_, 12) == 0;
    }
    ei::verify::Check("CEffectPointLight::Tick", true, ok, "object %p", self);
    return result;
}
EI_VERIFY(0x006781B0, LightTickVerify, 0, s_unused);

// 006795C0: PUSH EBP; MOV EBP,ESP; SUB ESP,24h (6 bytes). Only fields and the source's position
// change (the ground height queries are pure).
static int(__fastcall* s_particleTick)(CEffectParticle*);
static int __fastcall ParticleTickVerify(CEffectParticle* self) {
    void* source = self->source_;
    return SameWrites<int>("CEffectParticle::Tick", self, sizeof(CEffectParticle),
                           source, source ? 0x28 : 0, true, [&] { return self->Tick(); },
                           [&] { return s_particleTick(self); });
}
EI_VERIFY(0x006795C0, ParticleTickVerify, 6, s_particleTick);

static int __fastcall LightningTickVerify(CEffectLightning* self) {
    int result = self->Tick();
    ei::verify::Check("CEffectLightning::Tick", 1, result, "object %p", self);
    return result;
}
EI_VERIFY(0x0067A8F0, LightningTickVerify, 0, s_unused);

// --- Setters and Position ----------------------------------------------------------------------------

// 00679280 / 006792B0: MOV EAX,[ESP+4]; MOV [ECX+...],EAX (10 bytes).
static void(__fastcall* s_setAttached)(CEffectParticle*, void*, int);
static void __fastcall SetAttachedVerify(CEffectParticle* self, void*, int id) {
    SameWrites<int>("CEffectParticle::SetAttachedTo", self, sizeof(CEffectParticle), nullptr, 0, false,
                    [&] { self->SetAttachedTo(id); return 0; }, [&] { s_setAttached(self, nullptr, id); return 0; });
}
EI_VERIFY(0x00679280, SetAttachedVerify, 10, s_setAttached);

static void(__fastcall* s_setSize)(CEffectParticle*, void*, float);
static void __fastcall SetSizeVerify(CEffectParticle* self, void*, float size) {
    SameWrites<int>("CEffectParticle::SetSize", self, sizeof(CEffectParticle), nullptr, 0, false,
                    [&] { self->SetSize(size); return 0; }, [&] { s_setSize(self, nullptr, size); return 0; });
}
EI_VERIFY(0x006792B0, SetSizeVerify, 10, s_setSize);

// 006792E0: MOV EAX,[ESP+4]; MOV EDX,[ESP+8] (8 bytes).
static void(__fastcall* s_setColors)(CEffectParticle*, void*, float, float);
static void __fastcall SetColorsVerify(CEffectParticle* self, void*, float a, float b) {
    SameWrites<int>("CEffectParticle::SetColors", self, sizeof(CEffectParticle), nullptr, 0, false,
                    [&] { self->SetColors(a, b); return 0; }, [&] { s_setColors(self, nullptr, a, b); return 0; });
}
EI_VERIFY(0x006792E0, SetColorsVerify, 8, s_setColors);

// 00679310 / 00679C30: MOV EDX,[ESP+4]; PUSH ESI (5 bytes).
static void(__fastcall* s_setTarget)(CEffectParticle*, void*, const Vector3*);
static void __fastcall SetTargetVerify(CEffectParticle* self, void*, const Vector3* v) {
    Vector3 copy = *v;
    SameWrites<int>("CEffectParticle::SetTarget", self, sizeof(CEffectParticle), nullptr, 0, false,
                    [&] { self->SetTarget(&copy); return 0; }, [&] { s_setTarget(self, nullptr, &copy); return 0; });
}
EI_VERIFY(0x00679310, SetTargetVerify, 5, s_setTarget);

static void(__fastcall* s_setDirection)(CEffectWall*, void*, const Vector3*);
static void __fastcall SetDirectionVerify(CEffectWall* self, void*, const Vector3* v) {
    Vector3 copy = *v;
    SameWrites<int>("CEffectWall::SetDirection", self, sizeof(CEffectWall), nullptr, 0, false,
                    [&] { self->SetDirection(&copy); return 0; },
                    [&] { s_setDirection(self, nullptr, &copy); return 0; });
}
EI_VERIFY(0x00679C30, SetDirectionVerify, 5, s_setDirection);

// 0067A890: MOV EAX,[ESP+8]; MOV EDX,[ESP+4] (8 bytes).
static void(__fastcall* s_setObjects)(CEffectLightning*, void*, int, int);
static void __fastcall SetObjectsVerify(CEffectLightning* self, void*, int from, int to) {
    SameWrites<int>("CEffectLightning::SetObjects", self, sizeof(CEffectLightning), nullptr, 0, false,
                    [&] { self->SetObjects(from, to); return 0; },
                    [&] { s_setObjects(self, nullptr, from, to); return 0; });
}
EI_VERIFY(0x0067A890, SetObjectsVerify, 8, s_setObjects);

// 0067A8C0: MOV AL,[ESP+4]; MOV [ECX+174h],AL (10 bytes).
static void(__fastcall* s_setFlag)(CEffectLightning*, void*, int);
static void __fastcall SetFlagVerify(CEffectLightning* self, void*, int flag) {
    SameWrites<int>("CEffectLightning::SetFlag", self, sizeof(CEffectLightning), nullptr, 0, false,
                    [&] { self->SetFlag(static_cast<std::uint8_t>(flag)); return 0; },
                    [&] { s_setFlag(self, nullptr, flag); return 0; });
}
EI_VERIFY(0x0067A8C0, SetFlagVerify, 10, s_setFlag);

// 0067A6B0: SUB ESP,18h; MOV EDX,[ESP+1Ch] (7 bytes).
static void(__fastcall* s_setEnds)(CEffectLightning*, void*, const Vector3*, const Vector3*, float);
static void __fastcall SetEndsVerify(CEffectLightning* self, void*, const Vector3* from, const Vector3* to, float width) {
    Vector3 a = *from, b = *to;
    SameWrites<int>("CEffectLightning::SetEnds", self, sizeof(CEffectLightning), nullptr, 0, false,
                    [&] { self->SetEnds(&a, &b, width); return 0; },
                    [&] { s_setEnds(self, nullptr, &a, &b, width); return 0; });
}
EI_VERIFY(0x0067A6B0, SetEndsVerify, 7, s_setEnds);

// 00678BA0: SUB ESP,0Ch; PUSH ESI; MOV ESI,ECX (6 bytes). Writes worldPosition_ only.
static Vector3*(__fastcall* s_particlePosition)(CEffectParticle*, void*, void*);
static Vector3* __fastcall ParticlePositionVerify(CEffectParticle* self, void*, void* group) {
    Vector3* result = nullptr;
    SameWrites<int>("CEffectParticle::Position", self, sizeof(CEffectParticle), nullptr, 0, true,
                    [&] { return static_cast<int>(reinterpret_cast<std::uintptr_t>(self->Position(group))); },
                    [&] {
                        result = s_particlePosition(self, nullptr, group);
                        return static_cast<int>(reinterpret_cast<std::uintptr_t>(result));
                    });
    return result;
}
EI_VERIFY(0x00678BA0, ParticlePositionVerify, 6, s_particlePosition);

// --- Missiles (world_effect_missile.cpp; scenario spells: the arrow and the acid ray) ---------------

// 0067AAB0: PUSH EBP; MOV EBP,ESP; SUB ESP,28h (6 bytes). Writes only the velocity and arrived_
// (the world lookups are read-only): ours, restore, original; floats within a few ULPs.
static int(__fastcall* s_missileSteer)(CEffectMissile*);
static int __fastcall MissileSteerVerify(CEffectMissile* self) {
    Vector3 velocity = self->velocity_;
    int arrived = self->arrived_;
    int actual = self->Steer();
    Vector3 ours = self->velocity_;
    int oursArrived = self->arrived_;
    self->velocity_ = velocity;
    self->arrived_ = arrived;
    int expected = s_missileSteer(self);
    ei::verify::Check("CEffectMissile::Steer", true, expected == actual && oursArrived == self->arrived_,
                      "object %p: result %d vs %d, arrived %d vs %d", self, expected, actual, self->arrived_, oursArrived);
    ei::verify::CheckNear("CEffectMissile::Steer.x", self->velocity_.x, ours.x, "object %p", self);
    ei::verify::CheckNear("CEffectMissile::Steer.y", self->velocity_.y, ours.y, "object %p", self);
    ei::verify::CheckNear("CEffectMissile::Steer.z", self->velocity_.z, ours.z, "object %p", self);
    return expected;
}
EI_VERIFY(0x0067AAB0, MissileSteerVerify, 6, s_missileSteer);

// 0067AC70: MOV EAX,[ESP+4] (4); MOV DWORD PTR [ECX+14Ch],0 (10).
static void(__fastcall* s_missileSetTarget)(CEffectMissile*, void*, int);
static void __fastcall MissileSetTargetVerify(CEffectMissile* self, void*, int id) {
    SameWrites<int>("CEffectMissile::SetTarget", self, sizeof(CEffectMissile), nullptr, 0, false,
                    [&] { self->SetTarget(id); return 0; }, [&] { s_missileSetTarget(self, nullptr, id); return 0; });
}
EI_VERIFY(0x0067AC70, MissileSetTargetVerify, 14, s_missileSetTarget);

// Ours only (a client tick moves the sound, light and source): while flying the position moved by
// the velocity Steer left; otherwise arrived.
static int __fastcall ArrowTickVerify(CEffectArrow* self) {
    Vector3 before = self->localPosition_;
    int result = self->Tick();
    bool ok = result == 0 || result == 1;
    if (result == 1) {
        float x = before.x + self->velocity_.x;
        ok = ok && Bits(self->localPosition_.x) == Bits(x);
    } else {
        ok = ok && self->arrived_ == 1;
    }
    ei::verify::Check("CEffectArrow::Tick", true, ok, "object %p result %d", self, result);
    return result;
}
EI_VERIFY(0x0067AC90, ArrowTickVerify, 0, s_unused);

static void __fastcall ArrowResetVerify(CEffectArrow* self) {
    self->Reset();
    bool ok = BaseCleared(self) && self->targetId_ == -1 && self->arrived_ == 0 && self->casterId_ == -1 &&
              self->Particle().source_ == nullptr && self->Light().light_ == nullptr && self->Sound().sound_ == -1 &&
              Bits(self->particleSize_) == 0x3F800000u;
    ei::verify::Check("CEffectArrow::Reset", true, ok, "object %p", self);
}
EI_VERIFY(0x0067AD10, ArrowResetVerify, 0, s_unused);

// Ours only (the parts' Start(1) moves them): the parts placed at the arrow.
static bool PartsAtArrow(const CEffectArrow* self) {
    return std::memcmp(&self->Particle().localPosition_, &self->localPosition_, 12) == 0 &&
           std::memcmp(&self->Light().localPosition_, &self->localPosition_, 12) == 0 &&
           std::memcmp(&self->Sound().localPosition_, &self->localPosition_, 12) == 0;
}
static void __fastcall ArrowMovePartsVerify(CEffectArrow* self) {
    self->MoveParts();
    ei::verify::Check("CEffectArrow::MoveParts", true, PartsAtArrow(self), "object %p", self);
}
EI_VERIFY(0x0067B450, ArrowMovePartsVerify, 0, s_unused);

// Ours only (starts the source, light and sound): the parts carry the arrow's settings.
static void __fastcall ArrowStartVerify(CEffectArrow* self, void*, int update) {
    self->Start(update);
    bool ok = PartsAtArrow(self) && self->Particle().type_ == self->particleType_ &&
              Bits(self->Particle().size_) == Bits(self->particleSize_) &&
              Bits(self->Light().range_) == Bits(self->lightRange_) && self->Light().lightFlags_ == 0x840 &&
              std::strcmp(self->Sound().name_, self->soundName_) == 0 && self->Sound().looping_ == 1;
    ei::verify::Check("CEffectArrow::Start", true, ok, "object %p update %d", self, update);
}
EI_VERIFY(0x0067B4E0, ArrowStartVerify, 0, s_unused);

EI_UNPACK_VERIFY(0x0067B690, CEffectArrow, "CEffectArrow::Unpack");
EI_VERIFY(0x0067B690, UnpackVerify_CEffectArrow, 0, s_unused);
// 0067B800: SUB ESP,10h; PUSH ESI; PUSH EDI (5). Both run Steer first (it rewrites all it writes).
EI_PACK_VERIFY(0x0067B800, CEffectArrow, 5, "CEffectArrow::Pack");
EI_VERIFY(0x0067B800, PackVerify_CEffectArrow, 5, s_pack_CEffectArrow);
// 0067B9A0: PUSH EBX; PUSH EBP; MOV EBX,ECX; PUSH ESI (5).
EI_SAVE_VERIFY(0x0067B9A0, CEffectArrow, 5, "CEffectArrow::SaveState");
EI_VERIFY(0x0067B9A0, SaveVerify_CEffectArrow, 5, s_save_CEffectArrow);
// The CString reader uses more of the stream: ours only.
static void __fastcall ArrowLoadVerify(CEffectArrow* self, void*, void* stream) { self->LoadState(stream); }
EI_VERIFY(0x0067B8E0, ArrowLoadVerify, 0, s_unused);

// --- CEffectArrowFigure (world_effect_missile.cpp; scenario effects_bow, vanilla) ------------------

// Ours only (removes and releases the figure): the fields Reset sets.
static void __fastcall ArrowFigureResetVerify(CEffectArrowFigure* self) {
    self->Reset();
    bool ok = BaseCleared(self) && self->targetId_ == -1 && self->arrived_ == 0 && self->fromCaster_ == 0 &&
              self->casterId_ == -1 && self->figure_ == nullptr && Bits(self->hit_.multiplier) == 0x3F800000u &&
              Bits(self->hit_.amount) == 0;
    ei::verify::Check("CEffectArrowFigure::Reset", true, ok, "object %p", self);
}
EI_VERIFY(0x0067BAA0, ArrowFigureResetVerify, 0, s_unused);

// Ours only (on arrival the server delivers the hit): while flying the position moved by the
// velocity Steer left; otherwise arrived.
static int __fastcall ArrowFigureTickVerify(CEffectArrowFigure* self) {
    Vector3 before = self->localPosition_;
    int result = self->Tick();
    bool ok = result == 0 || result == 1;
    if (result == 1) {
        float x = before.x + self->velocity_.x;
        ok = ok && Bits(self->localPosition_.x) == Bits(x);
    } else {
        ok = ok && self->arrived_ == 1;
    }
    ei::verify::Check("CEffectArrowFigure::Tick", true, ok, "object %p result %d", self, result);
    return result;
}
EI_VERIFY(0x0067BE20, ArrowFigureTickVerify, 0, s_unused);

// Ours only (moves the figure in the client's sector grid): the figure is at the shot.
static bool FigureAtShot(const CEffectArrowFigure* self) {
    if (self->figure_ == nullptr) return true;
    return std::memcmp(reinterpret_cast<const char*>(self->figure_) + 0x1C, &self->localPosition_, 12) == 0;
}
static void __fastcall ArrowFigureMoveVerify(CEffectArrowFigure* self) {
    self->MoveFigure();
    ei::verify::Check("CEffectArrowFigure::MoveFigure", true, FigureAtShot(self), "object %p", self);
}
EI_VERIFY(0x0067BEF0, ArrowFigureMoveVerify, 0, s_unused);

// Ours only (makes the figure object and files it in the client's maps): a new start leaves a
// figure.
static void __fastcall ArrowFigureStartVerify(CEffectArrowFigure* self, void*, int update) {
    self->Start(update);
    bool ok = update != 0 ? FigureAtShot(self) : self->figure_ != nullptr;
    ei::verify::Check("CEffectArrowFigure::Start", true, ok, "object %p update %d", self, update);
}
EI_VERIFY(0x0067BF20, ArrowFigureStartVerify, 0, s_unused);

EI_UNPACK_VERIFY(0x0067C080, CEffectArrowFigure, "CEffectArrowFigure::Unpack");
EI_VERIFY(0x0067C080, UnpackVerify_CEffectArrowFigure, 0, s_unused);
// 0067C170: SUB ESP,10h; PUSH ESI; PUSH EDI (5). Both run Steer first (it rewrites all it writes).
EI_PACK_VERIFY(0x0067C170, CEffectArrowFigure, 5, "CEffectArrowFigure::Pack");
EI_VERIFY(0x0067C170, PackVerify_CEffectArrowFigure, 5, s_pack_CEffectArrowFigure);
// 0067C290: PUSH ESI; MOV ESI,[ESP+8] (5).
EI_SAVE_VERIFY(0x0067C290, CEffectArrowFigure, 5, "CEffectArrowFigure::SaveState");
EI_VERIFY(0x0067C290, SaveVerify_CEffectArrowFigure, 5, s_save_CEffectArrowFigure);
// The figure reference is resolved against the world (a reference taken): ours only.
static void __fastcall ArrowFigureLoadVerify(CEffectArrowFigure* self, void*, void* stream) { self->LoadState(stream); }
EI_VERIFY(0x0067C200, ArrowFigureLoadVerify, 0, s_unused);

// --- CEffectShowVisibility (world_effect_visibility.cpp; scenario spells_cast: clairvoyance) -------

static void __fastcall VisibilityResetVerify(CEffectShowVisibility* self) {
    self->Reset();
    bool ok = self->fog_ == nullptr && self->seerId_ == -1 && self->observerId_ == -1 && self->grid_.width == 1 &&
              self->grid_.height == 1 && self->grid_.x0 == 0x7FFFFFFF && self->grid_.rows[0] == self->grid_.cells;
    ei::verify::Check("CEffectShowVisibility::Reset", true, ok, "object %p", self);
}
EI_VERIFY(0x0067C5F0, VisibilityResetVerify, 0, s_unused);

// 0067C640: MOV EAX,[ESP+4]; MOV EDX,[ESP+8] (8 bytes).
static void(__fastcall* s_visibilitySetUnits)(CEffectShowVisibility*, void*, int, int);
static void __fastcall VisibilitySetUnitsVerify(CEffectShowVisibility* self, void*, int observer, int seer) {
    SameWrites<int>("CEffectShowVisibility::SetUnits", self, sizeof(CEffectShowVisibility), nullptr, 0, false,
                    [&] { self->SetUnits(observer, seer); return 0; },
                    [&] { s_visibilitySetUnits(self, nullptr, observer, seer); return 0; });
}
EI_VERIFY(0x0067C640, VisibilitySetUnitsVerify, 8, s_visibilitySetUnits);

// Ours only (makes a fog object and reveals the squares in it).
static void __fastcall VisibilityStartVerify(CEffectShowVisibility* self, void*, int update) {
    self->Start(update);
    ei::verify::Check("CEffectShowVisibility::Start", true, update != 0 || self->fog_ != nullptr, "object %p", self);
}
EI_VERIFY(0x0067C660, VisibilityStartVerify, 0, s_unused);

EI_UNPACK_VERIFY(0x0067C6B0, CEffectShowVisibility, "CEffectShowVisibility::Unpack");
EI_VERIFY(0x0067C6B0, UnpackVerify_CEffectShowVisibility, 0, s_unused);
// 0067C7A0: PUSH -1; PUSH imm32 (7).
EI_PACK_VERIFY(0x0067C7A0, CEffectShowVisibility, 7, "CEffectShowVisibility::Pack");
EI_VERIFY(0x0067C7A0, PackVerify_CEffectShowVisibility, 7, s_pack_CEffectShowVisibility);
// 0067C910: PUSH ECX; PUSH ESI; MOV ESI,[ESP+0Ch] (6).
EI_SAVE_VERIFY(0x0067C910, CEffectShowVisibility, 6, "CEffectShowVisibility::SaveState");
EI_VERIFY(0x0067C910, SaveVerify_CEffectShowVisibility, 6, s_save_CEffectShowVisibility);
// 0067C8E0: PUSH ESI; MOV ESI,[ESP+8] (5).
EI_LOAD_VERIFY(0x0067C8E0, CEffectShowVisibility, 5, "CEffectShowVisibility::LoadState");
EI_VERIFY(0x0067C8E0, LoadVerify_CEffectShowVisibility, 5, s_load_CEffectShowVisibility);

// Ours only (the sight computation reallocates the grid): on the server the effect is at the seer.
static int __fastcall VisibilityTickVerify(CEffectShowVisibility* self) {
    int result = self->Tick();
    bool ok = result == 0 || result == 1;
    if (result == 1) ok = ok && self->grid_.width >= 1 && self->grid_.rows != nullptr;
    ei::verify::Check("CEffectShowVisibility::Tick", true, ok, "object %p result %d", self, result);
    return result;
}
EI_VERIFY(0x0067C840, VisibilityTickVerify, 0, s_unused);

// --- CEffectTornado (world_effect_tornado.cpp; the server weather launches it: no scenario yet) ------

// Ours only (stops the sound, clears and releases the source).
static void __fastcall TornadoResetVerify(CEffectTornado* self) {
    self->Reset();
    ei::verify::Check("CEffectTornado::Reset", true, BaseCleared(self) && self->source_ == nullptr && self->sound_ == -1,
                      "object %p", self);
}
EI_VERIFY(0x0067D970, TornadoResetVerify, 0, s_unused);

// 0067DA50: PUSH EBP; MOV EBP,ESP; SUB ESP,0Ch (6). Ours with the ISAAC generator (007A9D40) saved,
// object and generator put back, the original: generator and integers bit for bit, the floats near.
static void(__fastcall* s_tornadoLaunch)(CEffectTornado*, void*, const Vector3*, const Vector3*);
static void __fastcall TornadoLaunchVerify(CEffectTornado* self, void*, const Vector3* from, const Vector3* to) {
    IsaacRandom& random = IsaacRandom::Global();
    static IsaacRandom randomBefore, randomOurs;
    Vector3 start = *from, end = *to;
    std::memcpy(&randomBefore, &random, sizeof(IsaacRandom));
    Snapshot before(self, sizeof(CEffectTornado), nullptr, 0);
    self->Launch(&start, &end);
    Snapshot ours(self, sizeof(CEffectTornado), nullptr, 0);
    const auto* o = reinterpret_cast<const CEffectTornado*>(ours.a.data());
    std::memcpy(&randomOurs, &random, sizeof(IsaacRandom));
    before.Restore();
    std::memcpy(&random, &randomBefore, sizeof(IsaacRandom));
    s_tornadoLaunch(self, nullptr, &start, &end);
    bool same = std::memcmp(&randomOurs, &random, sizeof(IsaacRandom)) == 0 && o->framesLeft_ == self->framesLeft_ &&
                o->launchTick_ == self->launchTick_ && o->moving_ == self->moving_ &&
                o->changeStamp_ == self->changeStamp_ && o->flags_ == self->flags_ &&
                std::memcmp(&o->localPosition_, &self->localPosition_, 12) == 0;
    ei::verify::Check("CEffectTornado::Launch", true, same, "object %p: ticks %d vs %d", self, self->framesLeft_,
                      o->framesLeft_);
    ei::verify::CheckNear("CEffectTornado::Launch", self->velocity_.x, o->velocity_.x, "velocity x");
    ei::verify::CheckNear("CEffectTornado::Launch", self->velocity_.y, o->velocity_.y, "velocity y");
}
EI_VERIFY(0x0067DA50, TornadoLaunchVerify, 6, s_tornadoLaunch);

// Ours only (hurts units on the server, moves the sound): one tick used, the result says whether
// any are left.
static int __fastcall TornadoTickVerify(CEffectTornado* self) {
    int ticks = self->framesLeft_;
    int result = self->Tick();
    bool ok = self->framesLeft_ == ticks - 1 && result == (self->framesLeft_ >= 0 ? 1 : 0);
    ei::verify::Check("CEffectTornado::Tick", true, ok, "object %p ticks %d result %d", self, ticks, result);
    return result;
}
EI_VERIFY(0x0067DBD0, TornadoTickVerify, 0, s_unused);

// Ours only (plays the sound and makes the source): a source afterwards.
static void __fastcall TornadoStartVerify(CEffectTornado* self, void*, int update) {
    self->Start(update);
    ei::verify::Check("CEffectTornado::Start", true, self->source_ != nullptr, "object %p update %d", self, update);
}
EI_VERIFY(0x0067DF10, TornadoStartVerify, 0, s_unused);

EI_UNPACK_VERIFY(0x0067DDD0, CEffectTornado, "CEffectTornado::Unpack");
EI_VERIFY(0x0067DDD0, UnpackVerify_CEffectTornado, 0, s_unused);
// 0067DEB0: SUB ESP,0Ch; PUSH ESI; MOV ESI,ECX (6).
EI_PACK_VERIFY(0x0067DEB0, CEffectTornado, 6, "CEffectTornado::Pack");
EI_VERIFY(0x0067DEB0, PackVerify_CEffectTornado, 6, s_pack_CEffectTornado);
// 0067E070 / 0067E020: PUSH ESI; MOV ESI,[ESP+8] (5).
EI_SAVE_VERIFY(0x0067E070, CEffectTornado, 5, "CEffectTornado::SaveState");
EI_VERIFY(0x0067E070, SaveVerify_CEffectTornado, 5, s_save_CEffectTornado);
EI_LOAD_VERIFY(0x0067E020, CEffectTornado, 5, "CEffectTornado::LoadState");
EI_VERIFY(0x0067E020, LoadVerify_CEffectTornado, 5, s_load_CEffectTornado);

// --- CEffectSoundSeq Start / Unpack (world_effect_sequence.cpp; scenario spells: the fireball's
// sequence) -------------------------------------------------------------------------------------------

EI_UNPACK_VERIFY(0x00676FD0, CEffectSoundSeq, "CEffectSoundSeq::Unpack");
EI_VERIFY(0x00676FD0, UnpackVerify_CEffectSoundSeq, 0, s_unused);

// Ours only (starts the sequence): a new start leaves an id (generation << 8 | slot) or -1.
static void __fastcall SoundSeqStartVerify(CEffectSoundSeq* self, void*, int update) {
    int before = self->sound_;
    self->Start(update);
    bool ok = update != 0 ? self->sound_ == before : (self->sound_ == -1 || self->sound_ >= 0x100);
    ei::verify::Check("CEffectSoundSeq::Start", true, ok, "object %p update %d sound %X", self, update, self->sound_);
}
EI_VERIFY(0x00676C00, SoundSeqStartVerify, 0, s_unused);
