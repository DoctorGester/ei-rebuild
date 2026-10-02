// Differential checks for the particle callbacks, rain/snow fading and the visibility fog
// (effects.cpp, effects_weather.cpp).
//
// Particle and element callbacks change the particle (or element), a few fields of their source
// and the ISAAC generator (007A9D40): ours runs first, the touched state (source 0x1F0 bytes,
// particle 0x64 / element 0x38 bytes, generator 0x814 bytes) is captured, restored, the original
// runs and the two results are compared; the original's results stand. Snapshots are on the stack.
// Setters that only write their object run on a scratch copy (ours or the original, whichever
// cannot have side effects there). The fog's per-frame update and reveal allocate and free cells:
// only ours runs, and the resulting map is checked for consistency.
#include "verify.h"

#include "../../src/game/effects.h"
#include "../../src/game/random.h"

#include <bit>
#include <cstring>

namespace {

IsaacRandom& GlobalRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }

std::uint32_t Bits(float f) { return std::bit_cast<std::uint32_t>(f); }

// Everything a particle or element callback may change.
template <typename Item>
struct CallbackState {
    unsigned char source[sizeof(CParticleSource)];
    unsigned char item[sizeof(Item)];
    unsigned char random[sizeof(IsaacRandom)];

    void Capture(const CParticleSource* s, const Item* i) {
        std::memcpy(source, s, sizeof(source));
        std::memcpy(item, i, sizeof(item));
        std::memcpy(random, &GlobalRandom(), sizeof(random));
    }
    void Restore(CParticleSource* s, Item* i) const {
        std::memcpy(s, source, sizeof(source));
        std::memcpy(i, item, sizeof(item));
        std::memcpy(&GlobalRandom(), random, sizeof(random));
    }
    // Which part differs: 0 none, 1 source, 2 particle/element, 3 generator.
    int Difference(const CallbackState& other) const {
        if (std::memcmp(source, other.source, sizeof(source)) != 0) return 1;
        if (std::memcmp(item, other.item, sizeof(item)) != 0) return 2;
        if (std::memcmp(random, other.random, sizeof(random)) != 0) return 3;
        return 0;
    }
};

template <typename Item, typename Ours, typename Original, typename... Args>
int RunBoth(const char* name, CParticleSource* self, Item* item, Ours ours, Original original, Args... args) {
    CallbackState<Item> before, mine, theirs;
    before.Capture(self, item);
    int actual = ours(self, item, args...);
    mine.Capture(self, item);
    before.Restore(self, item);
    int expected = original(self, nullptr, item, args...);
    theirs.Capture(self, item);
    int part = mine.Difference(theirs);
    ei::verify::Check(name, true, part == 0 && actual == expected, "type %X result %d vs %d, differs in %d",
                      self->type_, expected, actual, part);
    return expected;
}

using UpdateFn = int(__fastcall*)(CParticleSource*, void*, Particle*);
using EmitFn = int(__fastcall*)(CParticleSource*, void*, Particle*, int);
using TickFn = void(__fastcall*)(CParticleSource*, void*, ParticleElement*);

} // namespace

// --- Particle updates ---------------------------------------------------------------------------

// 0048F670: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
static UpdateFn s_updateFalling;
static int __fastcall UpdateFallingVerify(CParticleSource* self, void*, Particle* p) {
    return RunBoth("CParticleSource::UpdateFalling", self, p,
                   [](CParticleSource* s, Particle* q) { return s->CParticleSource::UpdateFalling(q); }, s_updateFalling);
}
EI_VERIFY(0x0048F670, UpdateFallingVerify, 6, s_updateFalling);

// 0048B570: PUSH EBP; MOV EBP,ESP; PUSH ECX; MOV EAX,[EBP+8] (7 bytes).
static UpdateFn s_updateCloud;
static int __fastcall UpdateCloudVerify(CParticleSource* self, void*, Particle* p) {
    return RunBoth("CParticleSource::UpdateCloud", self, p,
                   [](CParticleSource* s, Particle* q) { return s->CParticleSource::UpdateCloud(q); }, s_updateCloud);
}
EI_VERIFY(0x0048B570, UpdateCloudVerify, 7, s_updateCloud);

// 0048EC20: PUSH ESI; MOV ESI,[ESP+8] (5 bytes).
static UpdateFn s_updateSmoke;
static int __fastcall UpdateSmokeVerify(CParticleSource* self, void*, Particle* p) {
    return RunBoth("CParticleSource::UpdateSmoke", self, p,
                   [](CParticleSource* s, Particle* q) { return s->CParticleSource::UpdateSmoke(q); }, s_updateSmoke);
}
EI_VERIFY(0x0048EC20, UpdateSmokeVerify, 5, s_updateSmoke);

// 0048E9E0: SUB ESP,0x10; PUSH EBX; PUSH ESI (5 bytes).
static EmitFn s_emitSmoke;
static int __fastcall EmitSmokeVerify(CParticleSource* self, void*, Particle* p, int index) {
    return RunBoth("CParticleSource::EmitSmoke", self, p,
                   [](CParticleSource* s, Particle* q, int i) { return s->CParticleSource::EmitSmoke(q, i); },
                   s_emitSmoke, index);
}
EI_VERIFY(0x0048E9E0, EmitSmokeVerify, 5, s_emitSmoke);

// 00489190: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
static UpdateFn s_updateDebris;
static int __fastcall UpdateDebrisVerify(CParticleSource* self, void*, Particle* p) {
    return RunBoth("CParticleSource::UpdateDebris", self, p,
                   [](CParticleSource* s, Particle* q) { return s->CParticleSource::UpdateDebris(q); }, s_updateDebris);
}
EI_VERIFY(0x00489190, UpdateDebrisVerify, 6, s_updateDebris);

// 0048E7C0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x30 (6 bytes).
static UpdateFn s_updateOrbit;
static int __fastcall UpdateOrbitVerify(CParticleSource* self, void*, Particle* p) {
    return RunBoth("CParticleSource::UpdateOrbit", self, p,
                   [](CParticleSource* s, Particle* q) { return s->CParticleSource::UpdateOrbit(q); }, s_updateOrbit);
}
EI_VERIFY(0x0048E7C0, UpdateOrbitVerify, 6, s_updateOrbit);

// --- Element updates ----------------------------------------------------------------------------

// 004892E0: PUSH ESI; MOV ESI,[ESP+8] (5 bytes).
static TickFn s_tickNuke;
static void __fastcall TickNukeVerify(CParticleSource* self, void*, ParticleElement* e) {
    RunBoth("CParticleSource::TickNuke", self, e,
            [](CParticleSource* s, ParticleElement* x) { s->CParticleSource::TickNuke(x); return 0; },
            [](CParticleSource* s, void*, ParticleElement* x) { s_tickNuke(s, nullptr, x); return 0; });
}
EI_VERIFY(0x004892E0, TickNukeVerify, 5, s_tickNuke);

// 0048EE20: PUSH ESI; MOV ESI,ECX; FLD [ESI+0x138] (9 bytes).
static TickFn s_tickTendril;
static void __fastcall TickTendrilVerify(CParticleSource* self, void*, ParticleElement* e) {
    RunBoth("CParticleSource::TickTendril", self, e,
            [](CParticleSource* s, ParticleElement* x) { s->CParticleSource::TickTendril(x); return 0; },
            [](CParticleSource* s, void*, ParticleElement* x) { s_tickTendril(s, nullptr, x); return 0; });
}
EI_VERIFY(0x0048EE20, TickTendrilVerify, 9, s_tickTendril);

// 00488F00: PUSH ESI; MOV ESI,[ESP+8] (5 bytes). The Mushroom's first tick shakes the camera; that
// call runs ours only (shaking twice would be visible).
static TickFn s_tickMushroom;
static void __fastcall TickMushroomVerify(CParticleSource* self, void*, ParticleElement* e) {
    if (self->type_ == 0x200E && e->ticks == 0 && e == self->elements) {
        self->CParticleSource::TickMushroom(e);
        return;
    }
    RunBoth("CParticleSource::TickMushroom", self, e,
            [](CParticleSource* s, ParticleElement* x) { s->CParticleSource::TickMushroom(x); return 0; },
            [](CParticleSource* s, void*, ParticleElement* x) { s_tickMushroom(s, nullptr, x); return 0; });
}
EI_VERIFY(0x00488F00, TickMushroomVerify, 5, s_tickMushroom);

// --- Source setters -----------------------------------------------------------------------------

namespace {
struct SourceCopy {
    alignas(4) unsigned char bytes[sizeof(CParticleSource)];
    CParticleSource* get() { return reinterpret_cast<CParticleSource*>(bytes); }
};
} // namespace

// 00485480: FLD [ESP+4]; FMUL [ECX+0xF0] (10 bytes).
static void(__fastcall* s_scaleSize)(CParticleSource*, void*, float);
static void __fastcall ScaleSizeVerify(CParticleSource* self, void*, float factor) {
    SourceCopy actual;
    std::memcpy(actual.bytes, self, sizeof(actual.bytes));
    actual.get()->CParticleSource::ScaleSize(factor);
    s_scaleSize(self, nullptr, factor);
    ei::verify::Check("CParticleSource::ScaleSize", true, std::memcmp(self, actual.bytes, sizeof(actual.bytes)) == 0,
                      "factor %g", factor);
}
EI_VERIFY(0x00485480, ScaleSizeVerify, 10, s_scaleSize);

// 00485440: MOV EAX,[ECX+8]; AND AL,0xFE (5 bytes).
static void(__fastcall* s_stopEmitting)(CParticleSource*);
static void __fastcall StopEmittingVerify(CParticleSource* self) {
    SourceCopy actual;
    std::memcpy(actual.bytes, self, sizeof(actual.bytes));
    actual.get()->CParticleSource::StopEmitting();
    s_stopEmitting(self);
    ei::verify::Check("CParticleSource::StopEmitting", self->flags_, actual.get()->flags_, "");
}
EI_VERIFY(0x00485440, StopEmittingVerify, 5, s_stopEmitting);

// 00485450: MOV EAX,[ECX+8]; OR AL,5 (5 bytes).
static void(__fastcall* s_startEmitting)(CParticleSource*);
static void __fastcall StartEmittingVerify(CParticleSource* self) {
    SourceCopy actual;
    std::memcpy(actual.bytes, self, sizeof(actual.bytes));
    actual.get()->CParticleSource::StartEmitting();
    s_startEmitting(self);
    ei::verify::Check("CParticleSource::StartEmitting", self->flags_, actual.get()->flags_, "");
}
EI_VERIFY(0x00485450, StartEmittingVerify, 5, s_startEmitting);

// 00485360 detaches the source (side effects on the attached object): ours only.
static void* s_unusedStop;
static void __fastcall StopVerify(CParticleSource* self) {
    self->CParticleSource::Stop();
    ei::verify::Check("CParticleSource::Stop", true, (self->flags_ & 5) == 0 && self->attachedTo == nullptr,
                      "flags %08X attached %p", self->flags_, self->attachedTo);
}
EI_VERIFY(0x00485360, StopVerify, 0, s_unusedStop);

// --- Free helpers -------------------------------------------------------------------------------

// 00488940: FLD [EDX+8]; FDIV [ESP+4] (7 bytes). Ours into a scratch vector first (out may alias v).
static Vector3*(__fastcall* s_divideVector)(Vector3*, const Vector3*, float);
static Vector3* __fastcall DivideVectorVerify(Vector3* out, const Vector3& v, float d) {
    Vector3 actual;
    Vector3* ret = DivideVector(&actual, v, d);
    Vector3* expected = s_divideVector(out, &v, d);
    bool same = ret == &actual && expected == out && std::memcmp(out, &actual, sizeof(actual)) == 0;
    ei::verify::Check("DivideVector", true, same, "d %g", d);
    return expected;
}
EI_VERIFY(0x00488940, DivideVectorVerify, 7, s_divideVector);

// 00488970: SUB ESP,8; PUSH ESI; MOV ESI,ECX (6 bytes). Draws from the generator it is called on.
static float(__fastcall* s_range)(IsaacRandom*, void*, float, float);
static float __fastcall RangeVerify(IsaacRandom* self, void*, float min, float max) {
    static_assert(sizeof(IsaacRandom) == 0x814);
    unsigned char before[sizeof(IsaacRandom)], mine[sizeof(IsaacRandom)];
    std::memcpy(before, self, sizeof(before));
    float actual = self->IsaacRandom::Range(min, max);
    std::memcpy(mine, self, sizeof(mine));
    std::memcpy(self, before, sizeof(before));
    float expected = s_range(self, nullptr, min, max);
    bool same = Bits(actual) == Bits(expected) && std::memcmp(mine, self, sizeof(mine)) == 0;
    ei::verify::Check("IsaacRandom::Range", true, same, "[%g, %g]: %g vs %g", min, max, expected, actual);
    return expected;
}
EI_VERIFY(0x00488970, RangeVerify, 6, s_range);

// --- Rain and snow ------------------------------------------------------------------------------

namespace {
struct RainCopy {
    alignas(4) unsigned char bytes[sizeof(CRainSnow)];
    CRainSnow* get() { return reinterpret_cast<CRainSnow*>(bytes); }
};

// The fade's results: the original ran on a copy without the sound (so it makes no sound calls),
// ours on the real object.
bool SameFade(const CRainSnow* expected, const CRainSnow* actual, int soundBefore) {
    return expected->mode == actual->mode && expected->density == actual->density &&
           Bits(expected->fadeDuration) == Bits(actual->fadeDuration) &&
           (soundBefore != 0 || actual->sound == 0);
}
} // namespace

// 004E40B0: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes).
static void(__fastcall* s_updateFade)(CRainSnow*, void*, float);
static void __fastcall UpdateFadeVerify(CRainSnow* self, void*, float now) {
    RainCopy expected;
    std::memcpy(expected.bytes, self, sizeof(expected.bytes));
    expected.get()->sound = 0;
    s_updateFade(expected.get(), nullptr, now);
    int soundBefore = self->sound;
    self->CRainSnow::UpdateFade(now);
    ei::verify::Check("CRainSnow::UpdateFade", true, SameFade(expected.get(), self, soundBefore),
                      "now %g mode %d density %d vs %d", now, self->mode, expected.get()->density, self->density);
}
EI_VERIFY(0x004E40B0, UpdateFadeVerify, 6, s_updateFade);

// 004E4050: PUSH ESI; MOV ESI,ECX; PUSH EDI; XOR EDI,EDI (6 bytes).
static void(__fastcall* s_finishFade)(CRainSnow*);
static void __fastcall FinishFadeVerify(CRainSnow* self) {
    RainCopy expected;
    std::memcpy(expected.bytes, self, sizeof(expected.bytes));
    expected.get()->sound = 0;
    s_finishFade(expected.get());
    int soundBefore = self->sound;
    self->CRainSnow::FinishFade();
    ei::verify::Check("CRainSnow::FinishFade", true, SameFade(expected.get(), self, soundBefore), "mode %d",
                      self->mode);
}
EI_VERIFY(0x004E4050, FinishFadeVerify, 6, s_finishFade);

// 004E41F0: SUB ESP,0xC; MOV EAX,[ESP+0x10] (7 bytes).
static void(__fastcall* s_setWind)(CRainSnow*, void*, const Vector3*, float);
static void __fastcall SetWindVerify(CRainSnow* self, void*, const Vector3& direction, float strength) {
    RainCopy actual;
    std::memcpy(actual.bytes, self, sizeof(actual.bytes));
    actual.get()->CRainSnow::SetWind(direction, strength);
    s_setWind(self, nullptr, &direction, strength);
    ei::verify::Check("CRainSnow::SetWind", true, std::memcmp(self, actual.bytes, sizeof(actual.bytes)) == 0,
                      "(%g %g %g) x %g", direction.x, direction.y, direction.z, strength);
}
EI_VERIFY(0x004E41F0, SetWindVerify, 7, s_setWind);

// --- Visibility fog -----------------------------------------------------------------------------

namespace {
struct FogCopy {
    alignas(4) unsigned char bytes[sizeof(CVisibilityFog)];
    CVisibilityFog* get() { return reinterpret_cast<CVisibilityFog*>(bytes); }
};

std::size_t CountNodes(const CVisibilityFog* fog) {
    std::size_t n = 0;
    fog->cells.ForEach([&n](CVisibilityFog::CellMap::Node*) { ++n; });
    return n;
}

bool LessOrUnordered(float a, float b) { return !(a >= b); }

// The fog's static list of dropped cells (007ABB58).
struct KeyList {
    std::uint32_t* begin;
    std::uint32_t* finish;
};
const KeyList& DeadCells() { return *reinterpret_cast<const KeyList*>(0x007ABB58); }
} // namespace

// 004E1770: PUSH EBP; MOV EBP,ESP; SUB ESP,0x60 (6 bytes). Reads the cells, writes only the object:
// the original on a scratch copy (sharing the cell map read-only), ours on the real one.
static void(__fastcall* s_computeBounds)(CVisibilityFog*);
static void __fastcall ComputeBoundsVerify(CVisibilityFog* self) {
    FogCopy expected;
    std::memcpy(expected.bytes, self, sizeof(expected.bytes));
    s_computeBounds(expected.get());
    self->CVisibilityFog::ComputeBounds();
    ei::verify::Check("CVisibilityFog::ComputeBounds", true,
                      std::memcmp(self, expected.bytes, sizeof(expected.bytes)) == 0, "%u cells",
                      static_cast<unsigned>(self->cells.Size()));
}
EI_VERIFY(0x004E1770, ComputeBoundsVerify, 6, s_computeBounds);

// 004E1AB0 (slot 6): ours only. Afterwards no cell is below the drop threshold, the dropped keys
// are gone and the count matches.
static void* s_unusedUpdateCells;
static int __fastcall UpdateCellsVerify(CVisibilityFog* self) {
    std::size_t before = self->cells.Size();
    int result = self->CVisibilityFog::UpdateCells();
    std::size_t dropped = static_cast<std::size_t>(DeadCells().finish - DeadCells().begin);
    bool ok = self->cells.Size() == before - dropped && CountNodes(self) == self->cells.Size();
    self->cells.ForEach([&ok](CVisibilityFog::CellMap::Node* n) {
        if (LessOrUnordered(n->value.alpha, std::bit_cast<float>(0x3C23D70Au))) ok = false;
        if (n->value.frame < 0 || n->value.frame >= 15) ok = false;
    });
    for (const std::uint32_t* k = DeadCells().begin; k != DeadCells().finish; ++k)
        if (self->cells.Find(*k)) ok = false;
    ok = ok && result == ((self->refCount_ > 1 || self->cells.Size() != 0) ? 1 : 0);
    ei::verify::Check("CVisibilityFog::UpdateCells", true, ok, "%u cells, %u dropped", static_cast<unsigned>(before),
                      static_cast<unsigned>(dropped));
    return result;
}
EI_VERIFY(0x004E1AB0, UpdateCellsVerify, 0, s_unusedUpdateCells);

// 004E1C60: ours only (inserts cells, draws random numbers). Afterwards exactly the grid's
// visible squares are marked, each with a cell whose square matches its key.
static void* s_unusedReveal;
static void __fastcall RevealVerify(CVisibilityFog* self, void*, const FogGrid* grid) {
    self->CVisibilityFog::Reveal(grid);
    bool ok = CountNodes(self) == self->cells.Size();
    std::size_t marked = 0;
    for (int row = 0; row < grid->height; ++row) {
        for (int col = 0; col < grid->width; ++col) {
            if (grid->rows[row][col] == 0) continue;
            std::uint32_t key = ((grid->y0 + row) << 16) + grid->x0 + col;
            CVisibilityFog::CellMap::Node* n = self->cells.Find(key);
            if (!n || !n->value.visible) ok = false;
            ++marked;
        }
    }
    std::size_t visible = 0;
    self->cells.ForEach([&](CVisibilityFog::CellMap::Node* n) {
        if (n->value.visible) ++visible;
        std::uint32_t key = (static_cast<std::uint32_t>(n->value.y) << 16) + static_cast<std::uint16_t>(n->value.x);
        if ((key & 0xFFFF) != (n->key & 0xFFFF)) ok = false;
    });
    ok = ok && visible == marked;
    ei::verify::Check("CVisibilityFog::Reveal", true, ok, "%u cells, %u visible, %u marked",
                      static_cast<unsigned>(self->cells.Size()), static_cast<unsigned>(visible),
                      static_cast<unsigned>(marked));
}
EI_VERIFY(0x004E1C60, RevealVerify, 0, s_unusedReveal);
