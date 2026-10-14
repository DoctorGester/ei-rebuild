// CParticleSource set-up: Create (vtable slot 2, 00485540), Reset (004847E0) and Clear (vtable
// slot 3, 00484710). Names are provisional.
//
// Create resets the source, then fills the per-type parameters by `type_` (0x2000-0x2052, the
// script names are the table at 0078B120): emission rates, lifetime, spread, sizes, the three
// callbacks (+0x1E0 emit, +0x1E4 update, +0x1E8 element), the element array some types keep, and
// the texture. Last it loads the texture (kind 6, by name) and builds one UV rectangle per texture
// frame (the frames are a grid of `frameColumns` columns).
//
// The callbacks are stored as the original's addresses (several are rewritten and hooked there).
// Floating point follows the original's x87 code: `double` for values the original keeps on the
// FPU stack, Store where it stores to a float.
#include "effects.h"

#include "random.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_render.h"

#include <cstring>

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

// Constants the original reads from memory at run time.
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
double GameDouble(std::uintptr_t address) { return *reinterpret_cast<const volatile double*>(address); }
#define EI_ONE GameFloat(0x0073B84C)            // 1.0
#define EI_POINT_FOUR GameFloat(0x0073B868)     // 0.4
#define EI_POINT_SEVEN GameFloat(0x0073C538)    // 0.7
#define EI_POINT_EIGHT GameFloat(0x0073CCA8)    // 0.8
#define EI_POINT_TWO GameFloat(0x0073CCA0)      // 0.2
#define EI_TWELVE GameFloat(0x0073CB18)         // 12.0
#define EI_SIXTY_FOURTH GameFloat(0x0073CCA4)   // 0.015625
#define EI_UV_INSET GameFloat(0x0073CC90)       // 1/512: the frame rectangles are inset by this
#define EI_COLOR_SCALE GameFloat(0x0073C370)    // 255.0
#define EI_SCALE_2 GameDouble(0x0073C508)       // ~2^-31
#define EI_SCALE_0_4 GameDouble(0x0073CCB0)     // ~0.4 * 2^-32
#define EI_RING_STEP GameDouble(0x0073CC98)     // 0.62832 (2 pi / 10)

IsaacRandom& GlobalRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }

// 00488970 IsaacRandom::Range leaves its result unrounded in ST0.
double Range(float min, float max) { return RandomRange(&GlobalRandom(), nullptr, min, max); }

// 006E853C, the game's _ftol: truncation of ST0.
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

// 00497EB0 AllocateElements, called through its address (the original calls it there for some
// types and inlines it for others; the same code either way).

// 00500A00. Releases the texture in `holder`, then finds (or creates) the texture resource `name`
// of `kind` in the resource manager (007AF744) and keeps a reference in `holder`. __thiscall.

// 00518AE0. Detaches `source` from the object it is attached to. __thiscall on that object.
__declspec(naked) void __fastcall DetachSource(void* /*object*/, void*, CParticleSource* /*source*/) EI_ORIGINAL(0x00518AE0)

// The scene light colour the Tornado takes its particle colour from (007AF07C, +0x98 r, g, b).
const float* SceneLightColor() {
    return reinterpret_cast<const float*>(*reinterpret_cast<char* const*>(0x007AF07C) + 0x98);
}

// The original's 16-byte constant at 007A9C30 (an identity rotation), copied to +0x34 by Reset.
const void* IdentityRotation() { return reinterpret_cast<const void*>(0x007A9C30); }

void SetBits(void* field, std::uint32_t bits) { std::memcpy(field, &bits, sizeof(bits)); }

// Element fields by byte offset (their meaning depends on the type).
void SetElementDword(CParticleSource* s, int index, int offset, std::uint32_t bits) {
    SetBits(reinterpret_cast<std::uint8_t*>(&s->elements[index]) + offset, bits);
}
void SetElementFloat(CParticleSource* s, int index, int offset, float value) {
    std::memcpy(reinterpret_cast<std::uint8_t*>(&s->elements[index]) + offset, &value, sizeof(value));
}

// The element array's SetSize(n) as the original inlines it (MFC CArray: data +0x154, size
// +0x158, max +0x15C, grow by +0x160). After Reset the array is always empty, so only the first
// branch (allocate exactly n) and the n == 0 branch can run; the others are kept as written.
void SetElementCount(CParticleSource* s, int n) {
    if (n == 0) {
        if (s->elements) {
            gamecrt::operator_delete(reinterpret_cast<void**>(s->elements)[-1]);
            s->elements = nullptr;
        }
        s->elementCapacity = 0;
        s->elementCount = 0;
    } else if (!s->elements) {
        s->elements = AllocateElementsAt(n);
        s->elementCapacity = n;
        s->elementCount = n;
    } else if (n <= s->elementCapacity) {
        s->elementCount = n;
    } else {
        int grow = s->elementGrowBy;
        if (grow == 0) {
            grow = s->elementCount / 8;
            if (grow < 4) grow = 4;
            else if (grow > 1024) grow = 1024;
        }
        int capacity = s->elementCapacity + grow;
        if (!(capacity > n)) capacity = n;
        ParticleElement* data = AllocateElementsAt(capacity);
        std::memcpy(data, s->elements, static_cast<std::size_t>(s->elementCount) * sizeof(ParticleElement));
        gamecrt::operator_delete(reinterpret_cast<void**>(s->elements)[-1]);
        s->elements = data;
        s->elementCount = n;
        s->elementCapacity = capacity;
    }
}

// The spread box particles start in, relative to the emission point.
void SetSpread(CParticleSource* s, float x0, float y0, float z0, float x1, float y1, float z1) {
    s->spreadMin = {x0, y0, z0};
    s->spreadMax = {x1, y1, z1};
}

void SetCallbacks(CParticleSource* s, std::uintptr_t emit, std::uintptr_t update, std::uintptr_t element) {
    s->emitFn = reinterpret_cast<void*>(emit);
    s->updateFn = reinterpret_cast<void*>(update);
    if (element) s->elementFn = reinterpret_cast<void*>(element);
}

// 00485530: an element callback that does nothing (RET 4).
constexpr std::uintptr_t kNoElementTick = 0x00485530;

// --- Types with their own set-up code ------------------------------------------------------------

// Smoke (0x2001, 0x2003, 0x2004) and the tendril smoke 0x2010, which also keeps one element.
void SetupSmoke(CParticleSource* s) {
    SetSpread(s, -0.2f, -0.2f, 0.0f, 0.2f, 0.2f, 0.0f);
    s->emitChance = 88;
    s->emitCount = 6;
    s->lifetime = 32;
    s->frameCount = 15;
    s->frameDelay = 1;
    s->size = 0.8f;
    s->height = 0.3f;
    s->growth = 0.85f;
    SetCallbacks(s, 0x0048E9E0, 0x0048EC20, 0);
    if (s->type_ == 0x2010) {
        s->emitCount = 20;
        s->maxParticles = 1500;
        SetElementCount(s, 1);
        s->emitFn = reinterpret_cast<void*>(0x0048ED60);
        s->elementFn = reinterpret_cast<void*>(0x0048EE20);
    }
}

// Explosion (0x2002): fifteen fragments, each flying in a random direction (a point in the unit
// ball shifted up by 0.7, ten tries) with a random speed.
void SetupExplosion(CParticleSource* s) {
    SetSpread(s, -0.02f, -0.02f, -0.02f, 0.02f, 0.02f, 0.02f);
    s->emitChance = 100;
    s->emitCount = 60;
    s->maxParticles = 600;
    s->lifetime = 15;
    s->frameDelay = 1;
    s->size = 0.6f;
    s->height = 1.0f;
    s->growth = 0.8f;
    SetCallbacks(s, 0x0048C090, 0x0048C590, 0x0048C760);
    SetElementCount(s, 15);
    for (int i = 0; i < s->elementCount; ++i) {
        std::memset(&s->elements[i].pos, 0, sizeof(Vector3));
        float x, y, z;
        double lengthSquared;
        for (int tries = 10;;) {
            x = Store(Unsigned(GlobalRandom().Next()) * EI_SCALE_2 - D(EI_ONE));
            y = Store(Range(-1.0f, 1.0f));
            double zUnrounded = Range(-1.0f, 1.0f) + D(EI_POINT_SEVEN);
            z = Store(zUnrounded);
            lengthSquared = zUnrounded * D(z) + D(y) * D(y) + D(x) * D(x);
            if (!(lengthSquared >= D(EI_ONE))) break;
            if (!(--tries > 0)) break;
        }
        float length = Store(x87::fsqrt(lengthSquared));
        Vector3 v;
        v.Construct(x, y, z);
        Vector3 direction;
        DivideVector(&direction, v, length);
        double speed = Unsigned(GlobalRandom().Next()) * EI_SCALE_0_4 + D(EI_POINT_EIGHT);
        float vx = Store(D(direction.x) * speed);
        float vy = Store(speed * D(direction.y));
        double vz = speed * D(direction.z);
        float* velocity = &s->elements[i].value[0];
        velocity[0] = Store(D(vx) * D(EI_POINT_EIGHT));
        velocity[1] = Store(D(vy) * D(EI_POINT_EIGHT));
        velocity[2] = Store(vz * D(EI_POINT_EIGHT));
        SetElementDword(s, i, 0x30, 0);
        SetElementDword(s, i, 0x18, 0);
        SetElementFloat(s, i, 0x1C, 0.4f);
        SetElementFloat(s, i, 0x20, 0.7f);
        SetElementFloat(s, i, 0x24, Store(Unsigned(GlobalRandom().Next() % 6)));
        SetElementFloat(s, i, 0x28, Store(Unsigned(GlobalRandom().Next() & 1)));
    }
}

// Fireworks (0x2009): three rockets.
void SetupFireworks(CParticleSource* s) {
    SetSpread(s, -0.1f, -0.1f, -0.02f, 0.1f, 0.1f, 0.02f);
    s->emitChance = 88;
    s->emitCount = 20;
    s->frameDelay = 100;
    s->height = 0.3f;
    SetElementCount(s, 3);
    for (int i = 0; i < s->elementCount; ++i) {
        SetElementDword(s, i, 0x34, 0);
        SetElementDword(s, i, 0x30, 0);
    }
    SetCallbacks(s, 0x0048A8F0, 0x0048ADA0, 0x0048AEC0);
    SetBits(&s->additiveBlend, 1);
}

// Tornado (0x200A) and CurseHolder (0x2050): six rings stacked 2.4 apart.
void SetupTornadoRings(CParticleSource* s) {
    SetElementCount(s, 6);
    for (int i = 0; i < s->elementCount; ++i) {
        ParticleElement& e = s->elements[i];
        std::memset(&e.pos, 0, 2 * sizeof(float));
        e.pos.z = Store(static_cast<double>(i) * D(EI_TWELVE) * D(EI_POINT_TWO));
        SetElementDword(s, i, 0x34, 0);
        SetElementDword(s, i, 0x30, 0);
    }
}

void SetupTornado(CParticleSource* s) {
    s->emitChance = 100;
    s->emitCount = 4;
    s->maxParticles = 800;
    s->frameDelay = 1;
    s->height = 0.2f;
    // The particle colour: 0x19 alpha, then the scene light's r, g, b (the delay field holds it).
    const float* light = SceneLightColor();
    std::uint32_t color = static_cast<std::uint32_t>(GameFtol(D(light[0]) * D(EI_COLOR_SCALE)));
    color = (color | 0x1900) << 8;
    color |= static_cast<std::uint32_t>(GameFtol(D(light[1]) * D(EI_COLOR_SCALE)));
    color <<= 8;
    color |= static_cast<std::uint32_t>(GameFtol(D(light[2]) * D(EI_COLOR_SCALE)));
    SetBits(&s->frameDelay, color);
    SetupTornadoRings(s);
    std::memcpy(&s->elements[0].value[6], reinterpret_cast<const void*>(0x0073CB18), sizeof(float));  // 12.0
    SetElementFloat(s, 0, 0x28, 0.5f);
    SetBits(&s->elements[0].value[8], 0x3F7D70A4);     // 0.99
    SetCallbacks(s, 0x00489F70, 0x0048A310, 0x0048A560);
}

void SetupCurseHolder(CParticleSource* s) {
    s->flags_ |= 0x12;
    s->emitChance = 100;
    s->emitCount = 4;
    s->attachPoint = 7;
    s->maxParticles = 800;
    s->height = 0.2f;
    SetBits(&s->frameDelay, 0x19000000);
    SetupTornadoRings(s);
    s->elements[0].value[6] = Store(D(EI_TWELVE) * D(EI_POINT_FOUR));
    SetElementDword(s, 0, 0x28, 0);
    SetBits(&s->elements[0].value[8], 0x3F7F3B64);     // 0.997
    SetCallbacks(s, 0x00489F70, 0x0048A310, 0x0048A560);
}

// Nuke (0x200C): one element (the mushroom's state).
void SetupNuke(CParticleSource* s) {
    s->emitCount = 128;
    s->maxParticles = 400;
    s->lifetime = -1;
    s->frameDelay = 1;
    s->height = 1.0f;
    SetCallbacks(s, 0x00488F90, 0x00489190, 0x004892E0);
    SetElementCount(s, 1);
    SetElementDword(s, 0, 0x34, 0);
}

// Rain and snow (0x200F, 0x2031-0x2033): one element, whose +0x34 picks the kind.
void SetupWeather(CParticleSource* s) {
    s->attachPoint = 7;
    s->flags_ |= 0x12;
    SetSpread(s, -0.2f, -0.2f, -0.2f, 0.2f, 0.2f, 0.2f);
    s->emitCount = 10;
    s->maxParticles = 300;
    s->lifetime = 6;
    s->frameDelay = 15;
    SetCallbacks(s, 0x0048EEA0, 0x0048F300, 0x0048F380);
    SetElementCount(s, 1);
    SetElementDword(s, 0, 0x30, static_cast<std::uint32_t>(-100));
    switch (s->type_) {
    case 0x200F: SetElementDword(s, 0, 0x34, 12); break;
    case 0x2031: SetElementDword(s, 0, 0x34, 13); break;
    case 0x2032: SetElementDword(s, 0, 0x34, 14); break;
    case 0x2033: SetElementDword(s, 0, 0x34, 15); break;
    }
}

// Falling leaves and the like (0x2034-0x2037): four to eight elements.
void SetupFalling(CParticleSource* s) {
    s->attachPoint = 7;
    s->flags_ |= 0x12;
    SetSpread(s, -0.2f, -0.2f, -0.2f, 0.2f, 0.2f, 0.2f);
    s->emitCount = 45;
    s->maxParticles = 300;
    s->lifetime = 6;
    s->frameDelay = 15;
    SetCallbacks(s, 0x0048F3B0, 0x0048F670, 0x0048F6F0);
    SetElementCount(s, static_cast<int>(GlobalRandom().Next() % 5 + 4));
    for (int i = 0; i < s->elementCount; ++i) SetElementDword(s, i, 0x30, 0);
}

// ClayRing (0x2018): ten points on a circle around the source's (reset) position.
void SetupClayRing(CParticleSource* s) {
    SetBits(&s->additiveBlend, 1);
    s->emitCount = 80;
    s->lifetime = 15;
    s->frameDelay = 2;
    SetCallbacks(s, 0x0048F7F0, 0x0048FAB0, 0x0048FBE0);
    SetElementCount(s, 10);
    for (int i = 0; i < s->elementCount; ++i) {
        double angle = static_cast<double>(i) * EI_RING_STEP;
        float* direction = &s->elements[i].value[3];
        direction[0] = Store(x87::fsin(angle));
        direction[1] = Store(x87::fcos(angle));
        SetBits(&direction[2], 0);
        std::memcpy(&s->elements[i].pos, &s->position, sizeof(Vector3));
        std::memcpy(&s->elements[i].value[0], &s->position, sizeof(Vector3));
        SetElementDword(s, i, 0x34, 14);
    }
}

// Teleport (0x2019) and Nuke share "one element with +0x34 = 0".
void SetupTeleport(CParticleSource* s) {
    s->emitCount = 20;
    s->maxParticles = 320;
    s->lifetime = 14;
    s->frameDelay = 1;
    SetCallbacks(s, 0x0048FDB0, 0x00490060, 0x00490110);
    SetElementCount(s, 1);
    SetElementDword(s, 0, 0x34, 0);
}

// Ground plane (0x203A green, 0x203B red): one element holding the colour.
void SetupGroundPlane(CParticleSource* s) {
    s->flags_ |= 9;
    SetElementCount(s, 1);
    SetElementDword(s, 0, 0x34, 0);
    if (s->type_ == 0x203A) {
        s->emitCount = 128;
        SetElementDword(s, 0, 0x30, 0x40FF40);
    } else if (s->type_ == 0x203B) {
        s->emitCount = 256;
        SetElementDword(s, 0, 0x30, 0xFF4040);
    }
    s->emitFn = reinterpret_cast<void*>(0x00493300);
    s->maxParticles = s->emitCount;
    s->updateFn = reinterpret_cast<void*>(0x004933F0);
    s->elementFn = reinterpret_cast<void*>(0x004934F0);
}

// Bag (0x203C): eighty elements.
void SetupBag(CParticleSource* s) {
    s->emitCount = 100;
    s->flags_ |= 3;
    s->maxParticles = 800;
    SetCallbacks(s, 0x00493560, 0x00493770, 0x004938C0);
    SetElementCount(s, 80);
    for (int i = 0; i < 80; ++i) SetElementDword(s, i, 0x34, 0);
}

// Portal (0x203E): one element.
void SetupPortal(CParticleSource* s) {
    s->emitCount = 50;
    s->maxParticles = 800;
    s->lifetime = 10;
    s->frameDelay = 3;
    SetCallbacks(s, 0x00494470, 0x00494770, 0x00494930);
    SetElementCount(s, 1);
    SetBits(&s->additiveBlend, 1);
}

// Casting circles (0x203F, 0x2040): two elements with a countdown of 100.
void SetupCastingCircle(CParticleSource* s) {
    s->flags_ |= 3;
    s->emitCount = 30;
    s->frameDelay = 1;
    s->maxParticles = 600;
    SetCallbacks(s, 0x00493DE0, 0x00494170, 0x00494340);
    SetElementCount(s, 2);
    for (int i = 0; i < s->elementCount; ++i) SetElementDword(s, i, 0x34, 100);
}

// FeetCloud1/2 (0x204A blue, 0x204B red): two elements, one per foot.
void SetupFeetCloud(CParticleSource* s) {
    SetBits(&s->additiveBlend, 1);
    s->emitCount = 8;
    s->flags_ |= 3;
    s->maxParticles = 300;
    SetElementCount(s, 2);
    for (int i = 0; i < 2; ++i) {
        SetElementDword(s, i, 0x30, 0xFFFFFFFFu);
        SetElementDword(s, i, 0x34, static_cast<std::uint32_t>(i));
        std::memset(&s->elements[i].pos, 0, sizeof(Vector3));
        SetElementDword(s, i, 0x18, 0);
    }
    SetCallbacks(s, 0x00495A30, 0x00495C80, 0x00495E00);
    if (s->type_ == 0x204A) s->lifetime = 0xFF;
    else if (s->type_ == 0x204B) s->lifetime = 0xFF0000;
}

// StartTrans (0x2051): one element with a 30-tick countdown.
void SetupStartTrans(CParticleSource* s) {
    s->attachPoint = 7;
    s->flags_ |= 0x12;
    s->emitCount = 1;
    s->maxParticles = 300;
    SetCallbacks(s, 0x004965E0, 0x00496840, 0x00496990);
    SetElementCount(s, 1);
    SetElementDword(s, 0, 0x34, 30);
    SetElementDword(s, 0, 0x18, 0);
    SetElementFloat(s, 0, 0x1C, 1.0f);
}

// Fills the type's parameters; returns the texture name, or null for an unknown type.
const char* SetupType(CParticleSource* s) {
    const int type = s->type_;
    switch (type) {
    case 0x2000:        // FireSmoke
        s->lifetime = 15;
        s->frameCount = 15;
        SetSpread(s, -0.2f, -0.2f, -1.0f, 0.2f, 0.2f, 0.0f);
        s->emitChance = 88;
        s->emitCount = 1;
        s->emitSpacing = 0.09f;
        s->frameDelay = 1;
        s->size = 0.8f;
        s->growth = 0.94f;
        SetCallbacks(s, 0x0048C800, 0x0048CAD0, 0);
        return "FireSmoke";
    case 0x2001: case 0x2003: case 0x2004: case 0x2010:
        SetupSmoke(s);
        return "TendriSmoke";
    case 0x2002:
        SetupExplosion(s);
        return "Explosion";
    case 0x2005:        // Vulkan
        SetSpread(s, 0.2f, 0.5f, 0.0f, 0.3f, 4.0f, 0.0f);
        s->emitChance = 88;
        s->emitCount = 2;
        s->maxParticles = 400;
        s->lifetime = 16;
        s->frameDelay = 1;
        s->size = 2.5f;
        s->height = Store(D(s->spreadMax.y) * D(EI_SIXTY_FOURTH));
        s->growth = 0.99f;
        SetCallbacks(s, 0x0048BD50, 0x0048C010, 0);
        return "Vulkan";
    case 0x2006:        // Healing
        s->flags_ |= 2;
        SetBits(&s->additiveBlend, 1);
        s->emitCount = 0x7FFFFFFF;
        s->lifetime = 8;
        s->frameDelay = 1;
        s->size = 0.5f;
        SetCallbacks(s, 0x0048B630, 0x0048BBE0, 0x0048BCE0);
        return "Healing";
    case 0x2007: case 0x2008:   // PoisonFog
        s->emitChance = 50;
        s->emitCount = 1;
        s->maxParticles = 0x7FFFFFFF;
        s->lifetime = 0xCA4;
        s->frameCount = 15;
        s->frameDelay = 1;
        SetCallbacks(s, 0x0048B080, 0x0048B570, 0);
        return "PoisonCloud";
    case 0x2009:
        SetupFireworks(s);
        return "Fireworks";
    case 0x200A:
        SetupTornado(s);
        return "Tornado";
    case 0x200B:
        s->flags_ |= 3;
        s->emitCount = 0x7FFFFFFF;
        s->lifetime = -1;
        s->frameDelay = 1;
        SetCallbacks(s, 0x00489320, 0x00489A50, 0x00489B40);
        return "FireSmoke";
    case 0x200C:
        SetupNuke(s);
        return "AcidRay";
    case 0x200D:        // Banshee
        s->flags_ |= 3;
        s->emitCount = 0x7FFFFFFF;
        s->lifetime = -1;
        s->frameDelay = 1;
        SetCallbacks(s, 0x00489BE0, 0x00489EF0, 0x00489F30);
        SetElementCount(s, 0);
        return "Banshee";
    case 0x200E: case 0x2052:   // Mushroom, Transform
        s->emitCount = 10;
        s->maxParticles = 4000;
        s->lifetime = -1;
        s->frameDelay = 1;
        s->height = 1.0f;
        SetCallbacks(s, 0x004889D0, 0x00488DB0, 0x00488F00);
        return type == 0x2002 ? "Explosion" : "ChangeHero";   // the test never holds here
    case 0x200F: case 0x2031: case 0x2032: case 0x2033:
        SetupWeather(s);
        return "RainSnow";
    case 0x2011: case 0x204E:   // arrow smoke
        s->lifetime = 15;
        s->frameCount = 15;
        SetSpread(s, -0.2f, -0.2f, -0.2f, 0.2f, 0.2f, 0.2f);
        s->emitCount = 1;
        s->emitSpacing = 0.06f;
        s->frameDelay = 1;
        s->size = 0.3f;
        s->growth = 1.03f;
        SetBits(&s->emitState, 0);
        SetCallbacks(s, 0x0048CBA0, 0x0048CED0, 0);
        return type == 0x204E ? "RikArrowSmoke" : "FireArrowSmoke";
    case 0x2012:
        s->lifetime = 15;
        s->frameCount = 15;
        SetSpread(s, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f);
        s->emitCount = 1;
        s->emitSpacing = 0.06f;
        s->frameDelay = 1;
        s->size = 0.3f;
        SetCallbacks(s, 0x0048D050, 0x0048D330, 0);
        return "AcidRay";
    case 0x2013:        // Stench
        s->flags_ |= 2;
        SetSpread(s, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f);
        s->emitChance = 80;
        s->emitCount = 5;
        s->lifetime = 45;
        s->frameCount = 15;
        s->frameDelay = 3;
        s->size = 1.0f;
        SetCallbacks(s, 0x0048D420, 0x0048D7F0, kNoElementTick);
        return "Stench";
    case 0x2014:        // Link
        SetSpread(s, -0.2f, -0.2f, -0.2f, 0.2f, 0.2f, 0.2f);
        s->emitChance = 80;
        s->extent = {-10.0f, 0.0f, 0.0f};
        s->emitCount = 10;
        s->lifetime = 45;
        s->frameCount = 15;
        s->frameDelay = 3;
        s->size = 1.0f;
        SetCallbacks(s, 0x0048D8D0, 0x0048DE30, 0x0048E040);
        return "Link";
    case 0x2015: case 0x2016: case 0x2017: case 0x201A:   // protection spheres, AntiMagic
        s->flags_ |= 2;
        SetBits(&s->additiveBlend, 1);
        s->emitChance = 90;
        s->emitCount = 1;
        s->lifetime = type == 0x201A ? 4 : 15;
        s->frameCount = 15;
        SetSpread(s, -0.2f, -0.2f, -1.0f, 0.2f, 0.2f, 0.0f);
        s->frameDelay = 1;
        s->size = 1.0f;
        SetCallbacks(s, 0x0048E070, 0x0048E7C0, kNoElementTick);
        if (type == 0x2017) return "ProtectionF";
        if (type == 0x2016) return "ProtectionE";
        if (type == 0x201A) return "AntiMagic";
        return "ProtectionA";
    case 0x2018:
        SetupClayRing(s);
        return "ClayRing";
    case 0x2019:
        SetupTeleport(s);
        return "Teleport";
    case 0x201B: case 0x201C: case 0x201D: case 0x201E: case 0x201F: case 0x2020:
    case 0x2021: case 0x2022: case 0x2023: case 0x2024: case 0x2025: case 0x2026:   // Modifier1-12
        s->emitCount = 80;
        s->flags_ |= 3;
        s->lifetime = 14;
        s->frameDelay = 2;
        SetCallbacks(s, 0x00490130, 0x00490990, 0x00490B00);
        return "Stun";
    case 0x2027: case 0x2028: case 0x2029: case 0x202A:
        s->flags_ |= 0x13;
        s->attachPoint = 0;
        SetCallbacks(s, 0x004918E0, 0x00492030, 0x004921D0);
        s->lifetime = 5;
        s->emitCount = 20;
        if (type == 0x2027) {
            s->frameDelay = 5;
            return "FireSmoke";
        }
        if (type == 0x2028) {
            s->frameDelay = 1;
            return "Casting8";
        }
        if (type == 0x2029) {
            s->frameDelay = 5;
            return "Casting7";
        }
        s->emitCount = 6;
        s->frameDelay = 1;
        s->attachPoint = 2;
        SetBits(&s->additiveBlend, 1);
        return "Casting4";
    case 0x202B: case 0x202C: case 0x202D: case 0x202E:
        s->flags_ |= 3;
        SetSpread(s, -0.04f, -0.04f, -0.04f, 0.04f, 0.04f, 0.04f);
        s->emitCount = 10;
        s->frameDelay = 1;
        s->maxParticles = 600;
        SetCallbacks(s, 0x00490C10, 0x00491230, 0x00491390);
        if (type == 0x202B) {
            s->lifetime = 2;
            return "Casting2";
        }
        if (type == 0x202C) {
            s->lifetime = 1;
            return "Casting3";
        }
        if (type == 0x202D) {
            s->lifetime = 2;
            return "Casting6";
        }
        s->lifetime = 1;
        return "Casting1";
    case 0x202F:
        s->emitCount = 25;
        s->flags_ |= 3;
        s->lifetime = 14;
        s->frameDelay = 1;
        SetCallbacks(s, 0x00491420, 0x00491830, 0x004918D0);
        return "Teleport";
    case 0x2030:        // LightningBlast
        s->lifetime = 15;
        s->frameCount = 15;
        SetSpread(s, -0.2f, -0.2f, -0.2f, 0.2f, 0.2f, 0.2f);
        s->emitChance = 88;
        s->emitCount = 250;
        s->maxParticles = 600;
        s->frameDelay = 1;
        s->size = 1.0f;
        SetCallbacks(s, 0x004923B0, 0x00492B80, 0x00492CC0);
        SetBits(&s->additiveBlend, 1);
        return "TendriLight";
    case 0x2034: case 0x2035: case 0x2036: case 0x2037:
        SetupFalling(s);
        return "RainSnow";
    case 0x2038:        // ZoneExit
        s->emitCount = 10;
        s->maxParticles = 600;
        s->alpha = 5.0f;
        s->typeParam = 5.0f;
        s->flags_ |= 0x21;
        SetCallbacks(s, 0x00492D30, 0x00492F40, 0x00492FB0);
        return "ZoneExit";
    case 0x2039:
        s->emitFn = reinterpret_cast<void*>(0x00492FC0);
        s->flags_ |= 9;
        s->emitCount = 2500;
        s->maxParticles = 2500;
        s->updateFn = reinterpret_cast<void*>(0x00493280);
        s->elementFn = reinterpret_cast<void*>(0x004932F0);
        return "ShapeChange";
    case 0x203A: case 0x203B:
        SetupGroundPlane(s);
        return "ShapeChange";
    case 0x203C:
        SetupBag(s);
        return "Bag";
    case 0x203D:
        s->emitCount = 1;
        s->maxParticles = 1;
        SetCallbacks(s, 0x00493D10, 0x00493D50, 0x00493DD0);
        return "Casting6";
    case 0x203E:
        SetupPortal(s);
        return "Teleport";
    case 0x203F: case 0x2040:
        SetupCastingCircle(s);
        return type == 0x203F ? "Casting2" : "Casting1";
    case 0x2041: case 0x2042:
        s->flags_ |= 0x13;
        s->attachPoint = 7;
        s->emitCount = 1;
        SetCallbacks(s, 0x00494950, 0x004949B0, 0x00494A00);
        s->lifetime = 15;
        return type == 0x2041 ? "FireArrowSmoke" : "AcidRay";
    case 0x2043:        // Sparks
        SetBits(&s->additiveBlend, 1);
        s->flags_ |= 0x13;
        s->attachPoint = 7;
        s->emitCount = 10;
        s->maxParticles = 300;
        SetCallbacks(s, 0x00494A10, 0x00494CD0, 0x00494DF0);
        return "Casting6";
    case 0x2044: case 0x2045: case 0x2046: case 0x204C:
        s->flags_ |= 0x13;
        SetBits(&s->additiveBlend, 1);
        s->attachPoint = 2;
        s->emitCount = 3;
        SetCallbacks(s, 0x00494E00, 0x00494F30, 0x004950A0);
        if (type == 0x2044) s->lifetime = 0;
        else if (type == 0x2045) s->lifetime = 1;
        else if (type == 0x2046) s->lifetime = 2;
        else s->lifetime = 3;
        return "Stun";
    case 0x2047:
        SetBits(&s->additiveBlend, 1);
        s->flags_ |= 0x13;
        s->attachPoint = 7;
        s->emitCount = 2;
        SetCallbacks(s, 0x004950B0, 0x00495120, 0x00495310);
        return "Stun";
    case 0x2048:
        SetBits(&s->additiveBlend, 1);
        s->flags_ |= 0x13;
        s->attachPoint = 7;
        s->emitCount = 4;
        s->maxParticles = 300;
        SetCallbacks(s, 0x00495320, 0x004955D0, 0x004957D0);
        return "Stun";
    case 0x2049:        // FeebleMind
        SetBits(&s->additiveBlend, 1);
        s->flags_ |= 0x13;
        s->attachPoint = 1;
        s->emitCount = 5;
        s->maxParticles = 300;
        SetCallbacks(s, 0x004957E0, 0x00495950, 0x00495A20);
        return "Link";
    case 0x204A: case 0x204B:
        SetupFeetCloud(s);
        return "VisionFog";
    case 0x204D:
        SetBits(&s->additiveBlend, 1);
        s->flags_ |= 0x13;
        s->attachPoint = 0;
        s->emitCount = 1;
        s->maxParticles = 300;
        SetCallbacks(s, 0x00495FB0, 0x004961B0, 0x00496260);
        return "Stun";
    case 0x204F:
        SetBits(&s->additiveBlend, 1);
        s->flags_ |= 0x13;
        s->attachPoint = 7;
        s->emitCount = 8;
        s->maxParticles = 300;
        SetCallbacks(s, 0x00496270, 0x004964B0, 0x004965D0);
        return "Healing";
    case 0x2050:
        SetupCurseHolder(s);
        return "Tornado";
    case 0x2051:
        SetupStartTrans(s);
        return "ZoneExit";
    default:
        return nullptr;
    }
}

// One texture frame's UV rectangle.
struct FrameRect {
    float u0, v0, u1, v1;
};
static_assert(sizeof(FrameRect) == 16);

// The frame rectangle array (+0x1D0 data, +0x1D4 size, +0x1D8 capacity, +0x1DC grow by); the
// original inlines its SetSize(n).
using FrameArray = lib::AlignedArray<FrameRect>;

// The frames are cells of a grid `columns` wide, row 0 at the top (v = 1), each inset by 1/512.
void BuildFrames(CParticleSource* s) {
    float step = Store(D(EI_ONE) / static_cast<double>(s->frameColumns));
    auto& frames = *reinterpret_cast<FrameArray*>(&s->frameBase);
    frames.SetSize(s->frameCount);
    FrameRect* rect = frames.data;
    for (int i = 0; i < s->frameCount; ++i, ++rect) {
        double column = static_cast<double>(i % s->frameColumns);
        float row = Store(static_cast<double>(i / s->frameColumns));
        rect->u0 = Store(column * D(step) + D(EI_UV_INSET));
        rect->v0 = Store(D(EI_ONE) - (D(row) * D(step) + D(EI_UV_INSET)));
        rect->u1 = Store((column + D(EI_ONE)) * D(step) - D(EI_UV_INSET));
        rect->v1 = Store(D(EI_ONE) - ((D(row) + D(EI_ONE)) * D(step) - D(EI_UV_INSET)));
    }
}

} // namespace

// 004847E0. Back to the defaults: no position, attachment, callbacks or elements; the texture
// released; unit sizes, a 1000 emission spacing, 10000 frame delay, a one-frame grid.
void CParticleSource::Reset() {
    std::memset(&position, 0, sizeof(Vector3));
    std::memcpy(&orientation, IdentityRotation(), sizeof(orientation));
    attachedTo = nullptr;
    emitChance = 0;
    emitCount = 0;
    lifetime = -1;
    frameCount = 1;
    frameColumns = 1;
    frameDelay = 10000;
    void* held = texture;
    texture = nullptr;
    if (held) (*static_cast<void (__fastcall***)(void*, void*)>(held))[4](held, nullptr);
    std::memset(&windDirection, 0, sizeof(Vector3));
    std::memset(&extent, 0, sizeof(Vector3));
    emitFn = nullptr;
    updateFn = nullptr;
    elementFn = nullptr;
    SetBits(&windStrength, 0);
    SetBits(&additiveBlend, 0);
    if (elements) {
        gamecrt::operator_delete(reinterpret_cast<void**>(elements)[-1]);
        elements = nullptr;
    }
    elementCapacity = 0;
    elementCount = 0;
    alpha = 1.0f;
    typeParam = 1.0f;
    size = 1.0f;
    baseSize = 1.0f;
    std::memset(&spreadMin, 0, sizeof(Vector3));
    std::memset(&spreadMax, 0, sizeof(Vector3));
    SetBits(&emitState, 0);
    SetBits(&height, 0);
    SetBits(&growth, 0);
    attachPoint = 0;
    hiddenTicks = 0;
    SetBits(&turnCos, 0);
    SetBits(&turnSin, 0);
    emitSpacing = 1000.0f;
}
EI_REPLACE_METHOD(0x004847E0, CParticleSource::Reset);

// 00484710 (vtable slot 3). Returns every particle to the pool, releases the texture, detaches
// from the object the source follows, then resets.
void CParticleSource::Clear() {
    ReleaseAllParticles();
    void* held = texture;
    texture = nullptr;
    if (held) (*static_cast<void (__fastcall***)(void*, void*)>(held))[4](held, nullptr);
    if (attachedTo) DetachSource(attachedTo, nullptr, this);
    Reset();
}
EI_REPLACE_METHOD(0x00484710, CParticleSource::Clear);

// 00485540 (vtable slot 2). Sets the source up for its type; 1, or for an unknown type the fatal
// error report's result.
int CParticleSource::CreateForType() {
    Reset();
    std::uint32_t far = 0x47C35000;     // 100000.0f
    SetBits(&boundsMin.x, far);
    SetBits(&boundsMin.y, far);
    SetBits(&boundsMin.z, far);
    Vector3 near;
    near.Construct(-100000.0f, -100000.0f, -100000.0f);
    std::memcpy(&boundsMax, &near, sizeof(Vector3));
    frameColumns = 4;
    emitChance = 100;
    flags_ = (flags_ & ~0x18u) | 1;
    maxParticles = 500;
    frameCount = 16;

    const char* textureName = SetupType(this);
    if (!textureName) return game::FatalError("CParticle::Create().\nUnknown ETypeID.\n");

    char name[268];
    std::memcpy(name, textureName, std::strlen(textureName) + 1);
    LoadTextureOriginal(&texture, nullptr, 6, name);
    BuildFrames(this);
    return 1;
}
EI_REPLACE_METHOD(0x00485540, CParticleSource::CreateForType);

// --- Factory and destructor ----------------------------------------------------------------------

namespace {

// Not rewritten (forwarders): the engine object's base constructor (vtable 0073B8E0, id -1, the
// rest zero) and its base destructor body.

constexpr std::uintptr_t kTransformObjectVtable = 0x0073BA38;
constexpr std::uintptr_t kParticleSourceVtable = 0x0073CFD0;
constexpr std::uintptr_t kParticleListVtable = 0x0073D018;     // CTypedPtrList<CPtrList, Particle*>

void SetVtableAt(void* object, std::uintptr_t vtable) { std::memcpy(object, &vtable, sizeof(vtable)); }

} // namespace

// 004981B0. The class factory's creator (in the table at 0073F67C; the register arguments are not
// used, `type` is the source type): a new 0x1F0-byte source on the game's heap (null when that
// fails) with the engine object and transform bases (two identity rotations at +0x34 / +0x44), an
// empty element array, bounds, particle list (blocks of 10), frame rectangles and texture holder,
// then Reset and the type. The original's EH frame only unwinds the member constructors, none of
// which can throw.
CParticleSource* __fastcall NewParticleSource(void*, void*, int type) {
    void* memory = gamecrt::operator_new(sizeof(CParticleSource));
    if (!memory) return nullptr;
    auto* bytes = static_cast<std::uint8_t*>(memory);
    auto* s = static_cast<CParticleSource*>(memory);
    ConstructEngineObject(s);
    reinterpret_cast<Quaternion*>(bytes + 0x34)->SetIdentity();
    reinterpret_cast<Quaternion*>(bytes + 0x44)->SetIdentity();
    SetVtableAt(s, kTransformObjectVtable);
    s->elements = nullptr;
    s->elementGrowBy = 0;
    s->elementCapacity = 0;
    s->elementCount = 0;
    std::memset(bytes + offsetof(CParticleSource, boundsFlags), 0, 0x38);
    SetVtableAt(&s->particles, kParticleListVtable);
    s->particles.count = 0;
    s->particles.free = nullptr;
    s->particles.tail = nullptr;
    s->particles.head = nullptr;
    s->particles.blocks = nullptr;
    s->particles.blockSize = 10;
    // 00498E20 (the frame rectangle array) and 004983F0 (the texture holder) only zero their fields.
    s->frameBase = 0;
    s->frameRectGrowBy = 0;
    s->frameRectCapacity = 0;
    s->frameRectCount = 0;
    s->texture = nullptr;
    SetVtableAt(s, kParticleSourceVtable);
    s->Reset();
    s->type_ = type;
    return s;
}
EI_REPLACE(0x004981B0, NewParticleSource);

// 00498310. The destructor body: Clear (particles back to the pool, detached, Reset), release the
// texture and free the frame rectangles if any are left, empty the particle list and free its
// blocks, free the element array, then the engine object's base destructor.
void CParticleSource::Destruct() {
    SetVtableAt(this, kParticleSourceVtable);
    Clear();
    if (texture) (*static_cast<void(__fastcall***)(void*, void*)>(texture))[4](texture, nullptr);
    if (frameBase) gamecrt::operator_delete(reinterpret_cast<void**>(static_cast<std::uintptr_t>(frameBase))[-1]);
    SetVtableAt(&particles, kParticleListVtable);
    particles.count = 0;
    particles.free = nullptr;
    particles.tail = nullptr;
    particles.head = nullptr;
    mfc::PlexFreeDataChain(particles.blocks);
    particles.blocks = nullptr;
    if (elements) gamecrt::operator_delete(reinterpret_cast<void**>(elements)[-1]);
    SetVtableAt(this, kTransformObjectVtable);
    EngineObjectDestructBase(reinterpret_cast<CEngineObject*>(this));
}
EI_REPLACE_METHOD(0x00498310, CParticleSource::Destruct);

// 004982F0 (slot 0, RET 4). Destruct, then operator delete when `flags` bit 0 is set; returns this.
CParticleSource* CParticleSource::DeletingDestruct(int flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004982F0, CParticleSource::DeletingDestruct);

// 004982D0 / 004982E0 (slots 11 / 12): the exe's "CParticle" (0078B020) and class id 12.
const char* __fastcall ParticleSourceClassName(CParticleSource*) { return reinterpret_cast<const char*>(0x0078B020); }
int __fastcall ParticleSourceClassId(CParticleSource*) { return 12; }
EI_REPLACE(0x004982D0, ParticleSourceClassName);
EI_REPLACE(0x004982E0, ParticleSourceClassId);
