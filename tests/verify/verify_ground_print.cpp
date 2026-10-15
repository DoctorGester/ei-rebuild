// Verify wrappers for the ground prints (src/game/ground_print.cpp): footprints, blood and scorch
// marks. Footprints appear wherever units walk (every scenario with a moving unit on zone1); blood
// prints with hits and deaths (combat scenarios); scorch marks with fire spells (spells_cast). The
// prints tick and draw every frame while they last; Save / Load run with a saved game that has
// prints on the ground.
#include "combat_verify_util.h"
#include "fake_stream.h"
#include "verify.h"

#include "../../src/game/figure_mesh.h"
#include "../../src/game/ground_decal.h"
#include "../../src/game/ground_print.h"
#include "../../src/game/texture.h"
#include "../../src/game/world.h"

#include "ei/x87.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

namespace {
void* s_unused;

// ---- Comparison ---------------------------------------------------------------------------------

// The first differing field of two prints, or an empty text. Floats within a few ULPs match.
struct PrintDiff {
    char text[160] = "";
    bool Ok() const { return text[0] == 0; }
    void Int(const char* field, long long expected, long long actual) {
        if (Ok() && expected != actual) std::snprintf(text, sizeof(text), "%s %lld / %lld", field, expected, actual);
    }
    void Float(const char* field, float expected, float actual) {
        if (Ok() && !ei::verify::NearlyEqual(expected, actual, 4, 1e-6f))
            std::snprintf(text, sizeof(text), "%s %.9g / %.9g", field, expected, actual);
    }
};

std::uintptr_t VtableOf(const void* p) {
    std::uintptr_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

PrintDiff Compare(const GroundPrint& e, const GroundPrint& a) {
    PrintDiff d;
    d.Int("vtable", static_cast<long long>(VtableOf(&e)), static_cast<long long>(VtableOf(&a)));
    d.Int("built", e.built_, a.built_);
    d.Int("visible", e.visible_, a.visible_);
    d.Float("x", e.position_.x, a.position_.x);
    d.Float("y", e.position_.y, a.position_.y);
    d.Float("halfWidth", e.halfWidth_, a.halfWidth_);
    d.Float("halfLength", e.halfLength_, a.halfLength_);
    d.Float("growth", e.growth_, a.growth_);
    d.Int("life", e.life_, a.life_);
    d.Int("name", e.texture_.name == a.texture_.name, 1);
    d.Int("texture", reinterpret_cast<std::intptr_t>(e.texture_.texture), reinterpret_cast<std::intptr_t>(a.texture_.texture));
    d.Float("uvOffset.x", e.uvOffset_.x, a.uvOffset_.x);
    d.Float("uvOffset.y", e.uvOffset_.y, a.uvOffset_.y);
    d.Float("uvScale.x", e.uvScale_.x, a.uvScale_.x);
    d.Float("uvScale.y", e.uvScale_.y, a.uvScale_.y);
    d.Float("uvAngle", e.uvAngle_, a.uvAngle_);
    d.Float("angle", e.angle_, a.angle_);
    d.Int("lit", e.lit_, a.lit_);
    d.Float("alpha", e.alpha_, a.alpha_);
    d.Float("alphaScale", e.alphaScale_, a.alphaScale_);
    d.Int("colour", e.colour_, a.colour_);
    d.Int("lifetime", e.lifetime_, a.lifetime_);
    d.Int("positions", reinterpret_cast<std::intptr_t>(e.positions_.data), reinterpret_cast<std::intptr_t>(a.positions_.data));
    d.Int("texCoords", reinterpret_cast<std::intptr_t>(e.texCoords_.data), reinterpret_cast<std::intptr_t>(a.texCoords_.data));
    d.Int("indices", reinterpret_cast<std::intptr_t>(e.indices_.data), reinterpret_cast<std::intptr_t>(a.indices_.data));
    return d;
}

PrintDiff Compare(const Footprint& e, const Footprint& a) {
    PrintDiff d = Compare(static_cast<const GroundPrint&>(e), static_cast<const GroundPrint&>(a));
    d.Float("scale", e.scale_, a.scale_);
    d.Int("kind", e.kind_, a.kind_);
    return d;
}

PrintDiff Compare(const BloodPrint& e, const BloodPrint& a) {
    PrintDiff d = Compare(static_cast<const GroundPrint&>(e), static_cast<const GroundPrint&>(a));
    d.Float("spin", e.spin_, a.spin_);
    d.Int("spreadTicks", e.spreadTicks_, a.spreadTicks_);
    d.Int("kind", e.kind_, a.kind_);
    d.Int("fresh", e.fresh_, a.fresh_);
    return d;
}

template <typename T>
void CheckPrint(const char* name, const T& expected, const T& actual) {
    PrintDiff d = Compare(expected, actual);
    ei::verify::Check(name, true, d.Ok(), "%s", d.text);
}

// A raw copy that owns its own name and a reference to the texture (the arrays stay shared and
// must not be freed through it).
template <typename T>
T* CopyOf(const T& print, void* storage) {
    std::memcpy(storage, &print, sizeof(T));
    T* copy = static_cast<T*>(storage);
    new (&copy->texture_.name) stlp::string(print.texture_.name);
    if (copy->texture_.texture) ++copy->texture_.texture->refCount_;
    return copy;
}
void ReleaseCopy(GroundPrint* copy) {
    if (copy->texture_.texture) copy->texture_.texture->Release();
    copy->texture_.name.~string();
}

// ---- Streams ------------------------------------------------------------------------------------

// Records what a save function writes, then forwards it in the same pieces.
class CaptureStream : public FakeStream {
public:
    std::uint32_t Read(void*, std::uint32_t) override { return 0; }
    void Write(const void* buffer, std::uint32_t size) override {
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
        chunks.push_back(size);
    }
    void Forward(CStream* stream) const {
        std::size_t pos = 0;
        for (unsigned size : chunks) {
            stream->Write(bytes.data() + pos, size);
            pos += size;
        }
    }
    std::vector<unsigned char> bytes;
    std::vector<unsigned> chunks;
};

// Reads from the real stream and records the bytes.
class RecordStream : public FakeStream {
public:
    explicit RecordStream(CStream* real) : real_(real) {}
    std::uint32_t Read(void* buffer, std::uint32_t size) override {
        std::uint32_t result = real_->Read(buffer, size);
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
        return result;
    }
    void Write(const void*, std::uint32_t) override {}
    std::vector<unsigned char> bytes;

private:
    CStream* real_;
};

// Plays recorded bytes back.
class ReplayStream : public FakeStream {
public:
    explicit ReplayStream(const std::vector<unsigned char>& bytes) : bytes_(bytes) {}
    std::uint32_t Read(void* buffer, std::uint32_t size) override {
        for (unsigned i = 0; i < size; ++i) static_cast<unsigned char*>(buffer)[i] = pos_ < bytes_.size() ? bytes_[pos_++] : 0;
        return size;
    }
    void Write(const void*, std::uint32_t) override {}
    bool Consumed() const { return pos_ == bytes_.size(); }

private:
    const std::vector<unsigned char>& bytes_;
    std::size_t pos_ = 0;
};

// ---- Tick: the original on a raw copy, ours on the print; compared --------------------------------

// 006CECD0: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes). Every footprint and scorch mark, every tick.
int(__fastcall* s_tick)(GroundPrint*);

int __fastcall TickVerify(GroundPrint* self) {
    alignas(GroundPrint) unsigned char storage[sizeof(GroundPrint)];
    std::memcpy(storage, self, sizeof(GroundPrint));
    auto* copy = reinterpret_cast<GroundPrint*>(storage);
    int expected = s_tick(copy);
    int actual = self->GroundPrint::TickImpl();
    ei::verify::Check("GroundPrint::Tick", expected, actual, "life %d", actual);
    PrintDiff d = Compare(*copy, *self);
    ei::verify::Check("GroundPrint::Tick fields", true, d.Ok(), "%s", d.text);
    return actual;
}
EI_VERIFY(0x006CECD0, TickVerify, 6, s_tick);

// 006CFA20: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes). Every blood print, every tick.
int(__fastcall* s_bloodTick)(BloodPrint*);

int __fastcall BloodTickVerify(BloodPrint* self) {
    alignas(BloodPrint) unsigned char storage[sizeof(BloodPrint)];
    std::memcpy(storage, self, sizeof(BloodPrint));
    auto* copy = reinterpret_cast<BloodPrint*>(storage);
    int expected = s_bloodTick(copy);
    int actual = self->BloodPrint::TickImpl();
    ei::verify::Check("BloodPrint::Tick", expected, actual, "life %d", actual);
    PrintDiff d = Compare(*copy, *self);
    ei::verify::Check("BloodPrint::Tick fields", true, d.Ok(), "%s", d.text);
    return actual;
}
EI_VERIFY(0x006CFA20, BloodTickVerify, 6, s_bloodTick);

// ---- Save: both into capture streams, compared; ours forwarded ----------------------------------------

template <typename T, typename Ours>
void SaveCompare(const char* name, void(__fastcall* original)(T*, void*, CStream*), T* self, CStream* stream, Ours ours) {
    CaptureStream expected, actual;
    original(self, nullptr, &expected);
    ours(&actual);
    int at = -1;
    std::size_t n = expected.bytes.size() < actual.bytes.size() ? expected.bytes.size() : actual.bytes.size();
    for (std::size_t i = 0; i < n && at < 0; ++i)
        if (expected.bytes[i] != actual.bytes[i]) at = static_cast<int>(i);
    ei::verify::Check(name, true, expected.bytes == actual.bytes && expected.chunks == actual.chunks,
                      "%u / %u bytes in %u / %u writes, first difference at %d", static_cast<unsigned>(expected.bytes.size()),
                      static_cast<unsigned>(actual.bytes.size()), static_cast<unsigned>(expected.chunks.size()),
                      static_cast<unsigned>(actual.chunks.size()), at);
    actual.Forward(stream);
}

// 006CF370: PUSH ECX; PUSH EBX; MOV EBX,ECX; PUSH EBP; PUSH ESI (6 bytes). Saving a game with scorch
// marks (and, through the originals below, footprints and blood prints).
void(__fastcall* s_save)(GroundPrint*, void*, CStream*);
void __fastcall SaveVerify(GroundPrint* self, void*, CStream* stream) {
    SaveCompare("GroundPrint::Save", s_save, self, stream, [self](CStream* s) { self->GroundPrint::SaveImpl(s); });
}
EI_VERIFY(0x006CF370, SaveVerify, 6, s_save);

// 006CFE10 / 006CFAC0: PUSH ESI; MOV ESI,[ESP+8] (5 bytes).
void(__fastcall* s_footprintSave)(Footprint*, void*, CStream*);
void __fastcall FootprintSaveVerify(Footprint* self, void*, CStream* stream) {
    SaveCompare("Footprint::Save", s_footprintSave, self, stream, [self](CStream* s) { self->Footprint::SaveImpl(s); });
}
EI_VERIFY(0x006CFE10, FootprintSaveVerify, 5, s_footprintSave);

void(__fastcall* s_bloodSave)(BloodPrint*, void*, CStream*);
void __fastcall BloodSaveVerify(BloodPrint* self, void*, CStream* stream) {
    SaveCompare("BloodPrint::Save", s_bloodSave, self, stream, [self](CStream* s) { self->BloodPrint::SaveImpl(s); });
}
EI_VERIFY(0x006CFAC0, BloodSaveVerify, 5, s_bloodSave);

// ---- Load: ours from the real stream (recorded), the original on a copy from the recording ------------

template <typename T, typename Ours>
void LoadCompare(const char* name, void(__fastcall* original)(T*, void*, CStream*), T* self, CStream* stream, Ours ours) {
    alignas(T) unsigned char storage[sizeof(T)];
    T* copy = CopyOf(*self, storage);
    RecordStream recorder(stream);
    ours(&recorder);
    ReplayStream replay(recorder.bytes);
    original(copy, nullptr, &replay);
    PrintDiff d = Compare(*copy, *self);
    if (d.Ok() && !replay.Consumed()) std::snprintf(d.text, sizeof(d.text), "the original read %s", "fewer bytes");
    ei::verify::Check(name, true, d.Ok(), "%s", d.text);
    ReleaseCopy(copy);
}

// 006CF4F0: SUB ESP,0x100 (6 bytes). Loading a saved game with scorch marks (and from the originals
// of the two below, run on copies).
void(__fastcall* s_load)(GroundPrint*, void*, CStream*);
void __fastcall LoadVerify(GroundPrint* self, void*, CStream* stream) {
    LoadCompare("GroundPrint::Load", s_load, self, stream, [self](CStream* s) { self->GroundPrint::LoadImpl(s); });
}
EI_VERIFY(0x006CF4F0, LoadVerify, 6, s_load);

// 006CFE60 / 006CFB40: PUSH ESI; MOV ESI,[ESP+8] (5 bytes).
void(__fastcall* s_footprintLoad)(Footprint*, void*, CStream*);
void __fastcall FootprintLoadVerify(Footprint* self, void*, CStream* stream) {
    LoadCompare("Footprint::Load", s_footprintLoad, self, stream, [self](CStream* s) { self->Footprint::LoadImpl(s); });
}
EI_VERIFY(0x006CFE60, FootprintLoadVerify, 5, s_footprintLoad);

void(__fastcall* s_bloodLoad)(BloodPrint*, void*, CStream*);
void __fastcall BloodLoadVerify(BloodPrint* self, void*, CStream* stream) {
    LoadCompare("BloodPrint::Load", s_bloodLoad, self, stream, [self](CStream* s) { self->BloodPrint::LoadImpl(s); });
}
EI_VERIFY(0x006CFB40, BloodLoadVerify, 5, s_bloodLoad);

// ---- Constructors: the original into scratch storage (random state restored), ours; compared -------

// rand() and ISAAC before, and after the original's run.
struct RandomRun {
    combatverify::RandomState before, afterOriginal, afterOurs;
};

template <typename T>
void FinishConstruct(const char* name, RandomRun& run, T* scratch, T* self) {
    run.afterOurs.Save();
    ei::verify::Check(name, true, run.afterOriginal.SameAs(run.afterOurs), "random state after");
    CheckPrint(name, *scratch, *self);
    scratch->Destruct();
}

// 006CFEA0: PUSH -1; PUSH 007372F9 (7 bytes). Every scorch mark made (0056D4C0).
GroundPrint*(__fastcall* s_scorch)(GroundPrint*, void*, float, float, float, float, float, float, int, int);
GroundPrint* __fastcall ScorchVerify(GroundPrint* self, void*, float x, float y, float halfWidth, float halfLength,
                                     float angle, float alphaScale, int life, int lifetime) {
    RandomRun run;
    run.before.Save();
    alignas(GroundPrint) unsigned char storage[sizeof(GroundPrint)];
    auto* scratch = s_scorch(reinterpret_cast<GroundPrint*>(storage), nullptr, x, y, halfWidth, halfLength, angle,
                             alphaScale, life, lifetime);
    run.afterOriginal.Save();
    run.before.Restore();
    self->ConstructScorch(x, y, halfWidth, halfLength, angle, alphaScale, life, lifetime);
    FinishConstruct("GroundPrint::ConstructScorch", run, scratch, self);
    return self;
}
EI_VERIFY(0x006CFEA0, ScorchVerify, 7, s_scorch);

// 006CFBA0: PUSH -1; PUSH 007372B9 (7 bytes). Every footprint made (0056D150).
Footprint*(__fastcall* s_footprint)(Footprint*, void*, float, float, float, float, float, float, int, int, std::uint32_t,
                                    float, int, int);
Footprint* __fastcall FootprintVerify(Footprint* self, void*, float x, float y, float halfWidth, float halfLength,
                                      float angle, float alphaScale, int life, int lifetime, std::uint32_t tile,
                                      float scale, int visible, int kind) {
    RandomRun run;
    run.before.Save();
    alignas(Footprint) unsigned char storage[sizeof(Footprint)];
    auto* scratch = s_footprint(reinterpret_cast<Footprint*>(storage), nullptr, x, y, halfWidth, halfLength, angle,
                                alphaScale, life, lifetime, tile, scale, visible, kind);
    run.afterOriginal.Save();
    run.before.Restore();
    self->Construct(x, y, halfWidth, halfLength, angle, alphaScale, life, lifetime, tile, scale, visible, kind);
    FinishConstruct("Footprint::Construct", run, scratch, self);
    return self;
}
EI_VERIFY(0x006CFBA0, FootprintVerify, 7, s_footprint);

// 006CF660: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes). Every blood print made (0056CE90).
BloodPrint*(__fastcall* s_blood)(BloodPrint*, void*, float, float, float, float, float, int, int, std::uint32_t,
                                 std::uint32_t, std::uint32_t);
BloodPrint* __fastcall BloodVerify(BloodPrint* self, void*, float x, float y, float halfSize, float fullSize,
                                   float alphaScale, int life, int lifetime, std::uint32_t tile, std::uint32_t colour,
                                   std::uint32_t fresh) {
    RandomRun run;
    run.before.Save();
    alignas(BloodPrint) unsigned char storage[sizeof(BloodPrint)];
    auto* scratch = s_blood(reinterpret_cast<BloodPrint*>(storage), nullptr, x, y, halfSize, fullSize, alphaScale, life,
                            lifetime, tile, colour, fresh);
    run.afterOriginal.Save();
    run.before.Restore();
    self->Construct(x, y, halfSize, fullSize, alphaScale, life, lifetime, tile, colour, static_cast<std::uint8_t>(fresh));
    FinishConstruct("BloodPrint::Construct", run, scratch, self);
    return self;
}
EI_VERIFY(0x006CF660, BloodVerify, 5, s_blood);

// ---- Draw and the destructor: ours only ---------------------------------------------------------------

// 006CEDE0 (ours only: the original would draw a second time). Every visible print, every frame.
// Checks: the result (1 when hidden), the colour's alpha byte from alpha_, and that the geometry
// made on the first draw holds together: whole triangles, indices below the vertex count and the
// texture coordinate count. The decal builder leaves the texture coordinate array at its reserved
// size (at least the vertices written, which fill all but at most the last 3 slots of the packets),
// not at one per vertex (X29).
int __fastcall DrawVerify(GroundPrint* self) {
    bool wasBuilt = self->built_;
    int result = self->Draw();
    if (!self->visible_) {
        ei::verify::Check("GroundPrint::Draw hidden", 1, result, "");
        return result;
    }
    std::uint32_t alpha = static_cast<std::uint32_t>(x87::fistp(static_cast<float>(self->alpha_ * 255.0f)));
    ei::verify::Check("GroundPrint::Draw alpha", alpha, self->colour_ >> 24, "alpha %g", self->alpha_);
    if (!wasBuilt) {
        int vertices = self->positions_.size * 4;
        bool enoughTexCoords = self->texCoords_.size >= vertices - 3;
        bool wholeTriangles = self->indices_.size % 3 == 0;
        int badIndex = -1;
        for (int i = 0; badIndex < 0 && i < self->indices_.size; ++i)
            if (self->indices_[i] >= vertices || self->indices_[i] >= self->texCoords_.size) badIndex = i;
        bool ok = self->built_ && enoughTexCoords && wholeTriangles && badIndex < 0;
        ei::verify::Check("GroundPrint::Draw geometry", true, ok,
                          "built %d; %d blocks, %d coordinates%s, %d indices%s; index %d out of range (%d)", self->built_,
                          self->positions_.size, self->texCoords_.size, enoughTexCoords ? "" : " (too few)",
                          self->indices_.size, wholeTriangles ? "" : " (not whole triangles)", badIndex,
                          badIndex < 0 ? -1 : self->indices_[badIndex]);
    }
    return result;
}
EI_VERIFY(0x006CEDE0, DrawVerify, 0, s_unused);

// 006CF960 (ours only): reached only from the constructors' unwinding (never in practice).
void __fastcall DestructVerify(GroundPrint* self) {
    self->Destruct();
    ei::verify::Check("GroundPrint::Destruct", true, true, "");
}
EI_VERIFY(0x006CF960, DestructVerify, 0, s_unused);

// S13 (ours only): the client world's copies. 0056D080 (blood) when a print's life ends and when
// the lists are emptied (zone change, exit); 0056D400 / 0056D680 through the deleting destructors
// 00575560 / 00575580 (footprints, scorch marks), which return the print.
void __fastcall ListDestructVerify(GroundPrint* self) {
    bool ok = self->positions_.size <= self->positions_.capacity && self->indices_.size <= self->indices_.capacity;
    self->Destruct();
    ei::verify::Check("GroundPrint::Destruct (lists)", true, ok, "positions %d / %d, indices %d / %d",
                      self->positions_.size, self->positions_.capacity, self->indices_.size, self->indices_.capacity);
}
GroundPrint* __fastcall DeletingDestructVerify(GroundPrint* self, void*, unsigned flags) {
    GroundPrint* result = self->DeletingDestruct(flags);
    ei::verify::Check("GroundPrint::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result),
                      "flags %u", flags);
    return result;
}
EI_VERIFY(0x0056D080, ListDestructVerify, 0, s_unused);
EI_VERIFY(0x0056D400, ListDestructVerify, 0, s_unused);
EI_VERIFY(0x0056D680, ListDestructVerify, 0, s_unused);
EI_VERIFY(0x00575560, DeletingDestructVerify, 0, s_unused);
EI_VERIFY(0x00575580, DeletingDestructVerify, 0, s_unused);

// ---- The client world's prints: the original appends a node, which is taken out again (random
// state restored); ours appends; the two records compared ---------------------------------------------

template <typename T>
typename stlp::list<T>::Node* TakeLast(stlp::list<T>& list, typename stlp::list<T>::Node* lastBefore) {
    auto* node = list.head->prev;
    if (node == lastBefore) return nullptr;
    node->prev->next = node->next;
    node->next->prev = node->prev;
    return node;
}

template <typename T>
void FinishAdd(const char* name, RandomRun& run, stlp::list<T>& list, typename stlp::list<T>::Node* lastBefore,
               typename stlp::list<T>::Node* expected) {
    run.afterOurs.Save();
    ei::verify::Check(name, true, run.afterOriginal.SameAs(run.afterOurs), "random state after");
    auto* actual = list.head->prev == lastBefore ? nullptr : list.head->prev;
    ei::verify::Check(name, expected != nullptr, actual != nullptr, "a print added");
    if (expected && actual) CheckPrint(name, expected->data, actual->data);
    if (expected) {
        expected->data.Destruct();
        stlp::deallocate(expected, sizeof(*expected));
    }
}

// 0056CE90: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes). Hits and deaths that bleed.
void(__fastcall* s_addBlood)(CWorldClient*, void*, float, float, float, float, std::uint32_t, int);
void __fastcall AddBloodVerify(CWorldClient* self, void*, float x, float y, float halfSize, float fullSize,
                               std::uint32_t tile, int fresh) {
    RandomRun run;
    run.before.Save();
    auto* last = self->bloodPrints_.head->prev;
    s_addBlood(self, nullptr, x, y, halfSize, fullSize, tile, fresh);
    auto* expected = TakeLast(self->bloodPrints_, last);
    run.afterOriginal.Save();
    run.before.Restore();
    self->AddBloodPrint(x, y, halfSize, fullSize, tile, fresh);
    FinishAdd("CWorldClient::AddBloodPrint", run, self->bloodPrints_, last, expected);
}
EI_VERIFY(0x0056CE90, AddBloodVerify, 5, s_addBlood);

// 0056D150: PUSH -1; PUSH 00722AA4 (7 bytes). Every step of a unit that leaves footprints.
int(__fastcall* s_addFootprint)(CWorldClient*, void*, float, float, float, float, float, std::uint32_t, float, int, int);
int __fastcall AddFootprintVerify(CWorldClient* self, void*, float x, float y, float halfWidth, float halfLength,
                                  float angle, std::uint32_t tile, float scale, int visible, int bloodKind) {
    RandomRun run;
    run.before.Save();
    auto* last = self->footprints_.head->prev;
    int expectedKind = s_addFootprint(self, nullptr, x, y, halfWidth, halfLength, angle, tile, scale, visible, bloodKind);
    auto* expected = TakeLast(self->footprints_, last);
    run.afterOriginal.Save();
    run.before.Restore();
    int actualKind = self->AddFootprint(x, y, halfWidth, halfLength, angle, tile, scale, visible, bloodKind);
    ei::verify::Check("CWorldClient::AddFootprint", expectedKind, actualKind, "at %g, %g kind %d", x, y, bloodKind);
    FinishAdd("CWorldClient::AddFootprint", run, self->footprints_, last, expected);
    return actualKind;
}
EI_VERIFY(0x0056D150, AddFootprintVerify, 7, s_addFootprint);

// 0056D4C0: MOV EAX,FS:[0] (6 bytes). Fire spells' scorch marks.
void(__fastcall* s_addScorch)(CWorldClient*, void*, float, float, float, float, float, float);
void __fastcall AddScorchVerify(CWorldClient* self, void*, float x, float y, float halfWidth, float halfLength,
                                float angle, float alpha) {
    RandomRun run;
    run.before.Save();
    auto* last = self->scorchMarks_.head->prev;
    s_addScorch(self, nullptr, x, y, halfWidth, halfLength, angle, alpha);
    auto* expected = TakeLast(self->scorchMarks_, last);
    run.afterOriginal.Save();
    run.before.Restore();
    self->AddScorchMark(x, y, halfWidth, halfLength, angle, alpha);
    FinishAdd("CWorldClient::AddScorchMark", run, self->scorchMarks_, last, expected);
}
EI_VERIFY(0x0056D4C0, AddScorchVerify, 6, s_addScorch);

} // namespace
