// Particle drawing (004976B0-00497EB0): each visible source fills the shared vertex array
// (007A9BD0: position and size interpolated between the last two ticks) and sprite array
// (007A9BE0: interpolated colour, texture frame), binds its texture and hands both arrays to the
// renderer (007AF060, vtable 0073D93C, slot 17 = 00502CA0). Names are provisional.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the
// FPU stack, Store where it stores to a float. Compiled with precise semantics so that the order
// of operations and the NaN compares stay as written.
#include "effects.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>
#include "originals_library.h"

#pragma float_control(precise, on)

namespace {

inline double D(float f) { return f; }

// Rounds an x87 intermediate to float, as the original's FSTP to a float does.
float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

// A zero-extended dword loaded with FILD qword (exact).
double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
#define EI_ONE GameFloat(0x0073B84C)

// x87 FCOMP; FNSTSW; C0: "a < b or unordered".
bool LessOrUnordered(double a, double b) { return !(a >= b); }

ParticleVertexArray& Vertices() { return *reinterpret_cast<ParticleVertexArray*>(0x007A9BD0); }
ParticleSpriteArray& Sprites() { return *reinterpret_cast<ParticleSpriteArray*>(0x007A9BE0); }
std::uint8_t& ArraysConstructed() { return *reinterpret_cast<std::uint8_t*>(0x007A9BF4); }

// The world clock: whole ticks (007AF61C) and the fraction of the current one (007AFCA8).
int WorldTicks() { return *reinterpret_cast<const volatile int*>(0x007AF61C); }
float TickFraction() { return *reinterpret_cast<const volatile float*>(0x007AFCA8); }

constexpr int kReserve = 0x1000;

__declspec(naked) ParticleVertex* __fastcall AllocateVertices(int /*count*/) EI_ORIGINAL(0x00468F70)
__declspec(naked) void __fastcall ConstructSpriteArray(ParticleSpriteArray*) EI_ORIGINAL(0x00468D60)
__declspec(naked) int __fastcall ClipTest(void* /*pipeline*/, void*, const void* /*bounds*/) EI_ORIGINAL(0x004F16A0)
__declspec(naked) void __fastcall EnsureTextureLoaded(void** /*holder*/) EI_ORIGINAL(0x00500970)
// The game's _ftol (006E853C): truncation.
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

// The vertex array's SetSize, inlined twice by the original: no constructor, so the new elements
// keep whatever the allocation held; the copy keeps the old ones.
void SetVertexCount(ParticleVertexArray& a, int count) {
    if (count == 0) {
        if (a.data) {
            gamecrt::operator_delete(reinterpret_cast<void**>(a.data)[-1]);
            a.data = nullptr;
        }
        a.max = 0;
        a.size = 0;
        return;
    }
    if (!a.data) {
        a.data = AllocateVertices(count);
        a.max = count;
        a.size = count;
        return;
    }
    if (count <= a.max) {
        a.size = count;
        return;
    }
    int grow = a.growBy;
    if (grow == 0) {
        grow = a.size / 8;
        if (grow < 4) grow = 4;
        else if (grow > 1024) grow = 1024;
    }
    int capacity = grow + a.max;
    if (!(count < capacity)) capacity = count;
    ParticleVertex* data = AllocateVertices(capacity);
    std::memcpy(data, a.data, static_cast<std::size_t>(a.size) * sizeof(ParticleVertex));
    gamecrt::operator_delete(reinterpret_cast<void**>(a.data)[-1]);
    a.data = data;
    a.size = count;
    a.max = capacity;
}

// The arrays are function-local statics in the original (guard bits 1 and 2 of 007A9BF4, the
// destructors 00497D30 / 00497D10 registered with atexit).
void ConstructArrays() {
    if (!(ArraysConstructed() & 1)) {
        ArraysConstructed() |= 1;
        ConstructSpriteArray(&Sprites());
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x00497D30));
    }
    if (!(ArraysConstructed() & 2)) {
        Vertices().data = nullptr;
        ArraysConstructed() |= 2;
        Vertices().growBy = 0;
        Vertices().max = 0;
        Vertices().size = 0;
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x00497D10));
    }
}

// How far the frame is between the last tick and the next, clamped to [0, 1] (NaN gives 1).
float Interpolation() {
    double ticks = WorldTicks();
    float fraction = Store((ticks + D(TickFraction())) - ticks);
    float one;
    std::uint32_t oneBits = 0x3F800000;
    std::memcpy(&one, &oneBits, sizeof(one));
    float clamped = LessOrUnordered(one, fraction) ? one : fraction;
    return LessOrUnordered(clamped, 0.0) ? 0.0f : clamped;
}

// One colour channel (at `shift`) between the previous and the current colour, truncated.
int MixChannel(std::uint32_t current, std::uint32_t previous, int shift, float t, float u) {
    return GameFtol(Unsigned((current >> shift) & 0xFF) * D(t) + Unsigned((previous >> shift) & 0xFF) * D(u));
}

} // namespace

// 00497D40.
Particle** __fastcall NextParticle(void*, void*, mfc::CTypedPtrList<Particle>::Node** position) {
    mfc::CTypedPtrList<Particle>::Node* node = *position;
    *position = node->next;
    return &node->data;
}
EI_REPLACE(0x00497D40, NextParticle);

// 00497CE0.
int __fastcall BindParticleTexture(void* renderer, void*, int stage, void** holder) {
    EnsureTextureLoaded(holder);
    using BindFn = int(__fastcall*)(void*, void*, int, void*);
    BindFn bind = reinterpret_cast<BindFn>((*static_cast<void***>(renderer))[11]);
    return bind(renderer, nullptr, stage, *holder);
}
EI_REPLACE(0x00497CE0, BindParticleTexture);

// 00497D50. CArray::SetSize for the 8-byte sprites: new elements are zeroed.
void ParticleSpriteArray::SetSize(int count, int newGrowBy) {
    if (newGrowBy != -1) growBy = newGrowBy;
    if (count == 0) {
        if (data) {
            gamecrt::operator_delete(data);
            data = nullptr;
        }
        max = 0;
        size = 0;
        return;
    }
    if (!data) {
        data = static_cast<ParticleSprite*>(gamecrt::operator_new(static_cast<std::size_t>(count) * sizeof(ParticleSprite)));
        std::memset(data, 0, static_cast<std::size_t>(count) * sizeof(ParticleSprite));
        max = count;
        size = count;
        return;
    }
    if (count <= max) {
        if (count > size) std::memset(data + size, 0, static_cast<std::size_t>(count - size) * sizeof(ParticleSprite));
        size = count;
        return;
    }
    int grow = growBy;
    if (grow == 0) {
        grow = size / 8;
        if (grow < 4) grow = 4;
        else if (grow > 1024) grow = 1024;
    }
    int capacity = grow + max;
    if (!(count < capacity)) capacity = count;
    auto* fresh = static_cast<ParticleSprite*>(gamecrt::operator_new(static_cast<std::size_t>(capacity) * sizeof(ParticleSprite)));
    std::memcpy(fresh, data, static_cast<std::size_t>(size) * sizeof(ParticleSprite));
    std::memset(fresh + size, 0, static_cast<std::size_t>(count - size) * sizeof(ParticleSprite));
    gamecrt::operator_delete(data);
    size = count;
    data = fresh;
    max = capacity;
}
EI_REPLACE_METHOD(0x00497D50, ParticleSpriteArray::SetSize);

// 004976B0. Off screen (the renderer's clip test on the bounds says 2) only counts the hidden
// ticks. Otherwise the arrays are reserved for 0x1000 particles, sized to this source's, filled,
// and drawn; 0 when the texture cannot be bound.
int CParticleSource::Draw(void* renderer) {
    if (particles.count == 0) return 1;
    void* pipeline = static_cast<void**>(renderer)[2];
    if (ClipTest(pipeline, nullptr, &boundsFlags) == 2) {
        ++hiddenTicks;
        return 1;
    }
    hiddenTicks = 0;
    ConstructArrays();
    float t = Interpolation();
    Sprites().SetSize(kReserve, -1);
    SetVertexCount(Vertices(), kReserve);
    Sprites().SetSize(particles.count, -1);
    SetVertexCount(Vertices(), particles.count);
    if (!BindParticleTexture(renderer, nullptr, 0, &texture)) return 0;

    ParticleSprite* sprite = Sprites().data;
    mfc::CTypedPtrList<Particle>::Node* cursor = particles.head;
    if (cursor) {
        float u = Store(D(EI_ONE) - D(t));
        ParticleVertex* vertex = Vertices().data;
        do {
            const Particle* p = *NextParticle(&particles, nullptr, &cursor);
            vertex->x = Store(D(u) * D(p->prevPos.x) + D(t) * D(p->pos.x));
            vertex->y = Store(D(u) * D(p->prevPos.y) + D(t) * D(p->pos.y));
            vertex->z = Store(D(u) * D(p->prevPos.z) + D(t) * D(p->pos.z));
            vertex->size = Store(D(u) * D(p->prevSize) + D(t) * D(p->size));
            std::uint32_t now = p->color;
            std::uint32_t before = p->prevColor;
            std::uint32_t argb = static_cast<std::uint32_t>(MixChannel(now, before, 16, t, u));
            argb |= static_cast<std::uint32_t>(MixChannel(now, before, 24, t, u)) << 8;
            argb <<= 8;
            argb |= static_cast<std::uint32_t>(MixChannel(now, before, 8, t, u));
            argb <<= 8;
            argb |= static_cast<std::uint32_t>(MixChannel(now, before, 0, t, u));
            sprite->color = argb;
            sprite->frame = p->prevFrame * 16 + frameBase;
            ++sprite;
            ++vertex;
        } while (cursor);
    }
    using DrawFn = int(__fastcall*)(void*, void*, int, ParticleVertexArray*, ParticleSpriteArray*);
    DrawFn draw = reinterpret_cast<DrawFn>((*static_cast<void***>(renderer))[17]);
    int blend;
    std::memcpy(&blend, &unknown_150, sizeof(blend));
    return draw(renderer, nullptr, blend != 0, &Vertices(), &Sprites());
}
EI_REPLACE_METHOD(0x004976B0, CParticleSource::Draw);
