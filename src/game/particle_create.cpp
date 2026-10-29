// CParticleSource set-up: Create, Reset and Clear.
// More: docs/notes/comments-c22i.md, "particle_create.cpp".
#include "core_transform.h"
#include "effects.h"
#include "map_object_figures.h"
#include "random.h"
#include "spell.h"
#include "texture.h"
#include "world_data.h"

#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/crt.h"

#include "ei/x87.h"

#include <cstring>

namespace {

inline double D(float f) { return f; }

// A zero-extended dword as a double (exact).
double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

// Constants the original reads from memory at run time.
#define EI_ONE 1.0f
#define EI_POINT_FOUR 0.4f
#define EI_POINT_SEVEN 0.7f
#define EI_POINT_EIGHT 0.8f
#define EI_POINT_TWO 0.2f
#define EI_TWELVE 12.0f
#define EI_SIXTY_FOURTH 0.015625f
#define EI_UV_INSET 0.001953125f   // 1/512: the frame rectangles are inset by this
#define EI_COLOR_SCALE 255.0f
#define EI_SCALE_2 4.656612874161595e-10   // ~2^-31
#define EI_SCALE_0_4 9.313226580990458e-11   // ~0.4 * 2^-32
#define EI_RING_STEP 0.62832   // 2 pi / 10

// IsaacRandom::Range leaves its result unrounded.
double Range(float min, float max) { return g_random.Range(min, max); }

// The scene light colour the Tornado takes its particle colour from (the sun's r, g, b).
const float* SceneLightColor() { return g_sceneRenderer->sun_->color_; }

// The rotation Reset gives a source: the identity (a static initialiser's stores).
constexpr Quaternion kIdentityRotation = /*007A9C30*/ {1.0f, 0.0f, 0.0f, 0.0f};

void SetBits(void* field, std::uint32_t bits) { lib::StoreBytes(field, bits); }

// The spread box particles start in, relative to the emission point.
void SetSpread(CParticleSource* s, float x0, float y0, float z0, float x1, float y1, float z1) {
    s->spreadMin = {x0, y0, z0};
    s->spreadMax = {x1, y1, z1};
}

// A null element callback keeps the one Reset left (null).
void SetCallbacks(CParticleSource* s, CParticleSource::EmitCallback emit, CParticleSource::UpdateCallback update,
                  CParticleSource::ElementCallback element) {
    s->emitFn = emit;
    s->updateFn = update;
    if (element) s->elementFn = element;
}

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
    SetCallbacks(s, &CParticleSource::EmitSmoke, &CParticleSource::UpdateSmoke, nullptr);
    if (ParticleTypeOf(s->type_) == ParticleType::Tendril) {
        s->emitCount = 20;
        s->maxParticles = 1500;
        s->elements.resize(1);
        s->emitFn = &CParticleSource::EmitTendril;
        s->elementFn = &CParticleSource::TickTendril;
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
    SetCallbacks(s, &CParticleSource::EmitBlast, &CParticleSource::UpdateBlast, &CParticleSource::TickBlast);
    s->elements.resize(15);
    for (int i = 0; i < std::ssize(s->elements); ++i) {
        s->elements[i].pos = {};
        float x, y, z;
        double lengthSquared;
        for (int tries = 10;;) {
            x = static_cast<float>(Unsigned(g_random.Next()) * EI_SCALE_2 - D(EI_ONE));
            y = static_cast<float>(Range(-1.0f, 1.0f));
            double zUnrounded = Range(-1.0f, 1.0f) + D(EI_POINT_SEVEN);
            z = static_cast<float>(zUnrounded);
            lengthSquared = zUnrounded * D(z) + D(y) * D(y) + D(x) * D(x);
            if (lengthSquared < D(EI_ONE)) break;   // random: never NaN
            if (!(--tries > 0)) break;
        }
        float length = static_cast<float>(std::sqrt(lengthSquared));
        Vector3 v;
        v.Construct(x, y, z);
        Vector3 direction;
        DivideVector(&direction, v, length);
        double speed = Unsigned(g_random.Next()) * EI_SCALE_0_4 + D(EI_POINT_EIGHT);
        float vx = static_cast<float>(D(direction.x) * speed);
        float vy = static_cast<float>(speed * D(direction.y));
        double vz = speed * D(direction.z);
        auto& fragment = s->elements[i].fragment;
        fragment.velocity.x = static_cast<float>(D(vx) * D(EI_POINT_EIGHT));
        fragment.velocity.y = static_cast<float>(D(vy) * D(EI_POINT_EIGHT));
        fragment.velocity.z = static_cast<float>(vz * D(EI_POINT_EIGHT));
        fragment.age = 0;
        fragment.radius = 0.0f;
        fragment.growth = 0.4f;
        fragment.decay = 0.7f;
        fragment.frame = static_cast<float>(Unsigned(g_random.Next() % 6));
        fragment.sizeFactor = static_cast<float>(Unsigned(g_random.Next() & 1));
    }
}

// Fireworks (0x2009): three rockets.
void SetupFireworks(CParticleSource* s) {
    SetSpread(s, -0.1f, -0.1f, -0.02f, 0.1f, 0.1f, 0.02f);
    s->emitChance = 88;
    s->emitCount = 20;
    s->frameDelay = 100;
    s->height = 0.3f;
    s->elements.resize(3);
    for (int i = 0; i < std::ssize(s->elements); ++i) {
        s->elements[i].ticks = 0;
        s->elements[i].vent.pause = 0;
    }
    SetCallbacks(s, &CParticleSource::EmitGeyser, &CParticleSource::UpdateGeyser, &CParticleSource::TickGeyser);
    SetBits(&s->additiveBlend, 1);
}

// Tornado (0x200A) and CurseHolder (0x2050): six rings stacked 2.4 apart.
void SetupTornadoRings(CParticleSource* s) {
    s->elements.resize(6);
    for (int i = 0; i < std::ssize(s->elements); ++i) {
        ParticleElement& e = s->elements[i];
        std::memset(&e.pos, 0, 2 * sizeof(float));
        e.pos.z = static_cast<float>(static_cast<double>(i) * D(EI_TWELVE) * D(EI_POINT_TWO));
        e.ticks = 0;
        e.word[9] = 0;
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
    std::uint32_t color = libcrt::Ftol(D(light[0]) * D(EI_COLOR_SCALE));
    color = (color | 0x1900) << 8;
    color |= libcrt::Ftol(D(light[1]) * D(EI_COLOR_SCALE));
    color <<= 8;
    color |= libcrt::Ftol(D(light[2]) * D(EI_COLOR_SCALE));
    SetBits(&s->frameDelay, color);
    SetupTornadoRings(s);
    s->elements[0].funnel.height = 12.0f;  // 12.0
    s->elements[0].funnel.bend = 0.5f;
    s->elements[0].funnel.damping = 0.99f;     // 0.99
    SetCallbacks(s, &CParticleSource::EmitTornado, &CParticleSource::UpdateTornado, &CParticleSource::TickTornado);
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
    s->elements[0].funnel.height = static_cast<float>(D(EI_TWELVE) * D(EI_POINT_FOUR));
    s->elements[0].funnel.bend = 0.0f;
    s->elements[0].funnel.damping = 0.997f;     // 0.997
    SetCallbacks(s, &CParticleSource::EmitTornado, &CParticleSource::UpdateTornado, &CParticleSource::TickTornado);
}

// Nuke (0x200C): one element (the mushroom's state).
void SetupNuke(CParticleSource* s) {
    s->emitCount = 128;
    s->maxParticles = 400;
    s->lifetime = -1;
    s->frameDelay = 1;
    s->height = 1.0f;
    SetCallbacks(s, &CParticleSource::EmitNuke, &CParticleSource::UpdateDebris, &CParticleSource::TickNuke);
    s->elements.resize(1);
    s->elements[0].ticks = 0;
}

// Rain and snow (0x200F, 0x2031-0x2033): one element, whose ticks pick the kind.
void SetupWeather(CParticleSource* s) {
    s->attachPoint = 7;
    s->flags_ |= 0x12;
    SetSpread(s, -0.2f, -0.2f, -0.2f, 0.2f, 0.2f, 0.2f);
    s->emitCount = 10;
    s->maxParticles = 300;
    s->lifetime = 6;
    s->frameDelay = 15;
    SetCallbacks(s, &CParticleSource::EmitShower, &CParticleSource::UpdateShower, &CParticleSource::TickShower);
    s->elements.resize(1);
    s->elements[0].ray.age = -100;
    switch (ParticleTypeOf(s->type_)) {
    case ParticleType::Shower1: s->elements[0].ticks = 12; break;
    case ParticleType::Shower2: s->elements[0].ticks = 13; break;
    case ParticleType::Shower3: s->elements[0].ticks = 14; break;
    case ParticleType::Shower4: s->elements[0].ticks = 15; break;
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
    SetCallbacks(s, &CParticleSource::EmitFalling, &CParticleSource::UpdateFalling, &CParticleSource::TickFalling);
    s->elements.resize(static_cast<int>(g_random.Next() % 5 + 4));
    for (int i = 0; i < std::ssize(s->elements); ++i) s->elements[i].wind.age = 0;
}

// ClayRing (0x2018): ten points on a circle around the source's (reset) position.
void SetupClayRing(CParticleSource* s) {
    SetBits(&s->additiveBlend, 1);
    s->emitCount = 80;
    s->lifetime = 15;
    s->frameDelay = 2;
    SetCallbacks(s, &CParticleSource::EmitClayRing, &CParticleSource::UpdateClayRing, &CParticleSource::TickClayRing);
    s->elements.resize(10);
    for (int i = 0; i < std::ssize(s->elements); ++i) {
        double angle = static_cast<double>(i) * EI_RING_STEP;
        float* direction = &s->elements[i].clay.direction.x;
        direction[0] = static_cast<float>(x87::fsin(angle));
        direction[1] = static_cast<float>(x87::fcos(angle));
        direction[2] = 0.0f;
        s->elements[i].pos = s->localPosition_;
        s->elements[i].clay.previous = s->localPosition_;
        s->elements[i].ticks = 14;
    }
}

// Teleport (0x2019) and Nuke share "one element with ticks = 0".
void SetupTeleport(CParticleSource* s) {
    s->emitCount = 20;
    s->maxParticles = 320;
    s->lifetime = 14;
    s->frameDelay = 1;
    SetCallbacks(s, &CParticleSource::EmitTeleport, &CParticleSource::UpdateTeleport, &CParticleSource::TickTeleport);
    s->elements.resize(1);
    s->elements[0].ticks = 0;
}

// Ground plane (0x203A green, 0x203B red): one element holding the colour.
void SetupGroundPlane(CParticleSource* s) {
    s->flags_ |= 9;
    s->elements.resize(1);
    s->elements[0].ticks = 0;
    if (ParticleTypeOf(s->type_) == ParticleType::GroundDust1) {
        s->emitCount = 128;
        s->elements[0].plane.color = 0x40FF40;
    } else if (ParticleTypeOf(s->type_) == ParticleType::GroundDust2) {
        s->emitCount = 256;
        s->elements[0].plane.color = 0xFF4040;
    }
    s->emitFn = &CParticleSource::EmitGroundDust;
    s->maxParticles = s->emitCount;
    s->updateFn = &CParticleSource::UpdateGroundDust;
    s->elementFn = &CParticleSource::TickGroundPlane;
}

// Bag (0x203C): eighty elements.
void SetupBag(CParticleSource* s) {
    s->emitCount = 100;
    s->flags_ |= 3;
    s->maxParticles = 800;
    SetCallbacks(s, &CParticleSource::EmitBag, &CParticleSource::UpdateBag, &CParticleSource::TickBag);
    s->elements.resize(80);
    for (int i = 0; i < 80; ++i) s->elements[i].ticks = 0;
}

// Portal (0x203E): one element.
void SetupPortal(CParticleSource* s) {
    s->emitCount = 50;
    s->maxParticles = 800;
    s->lifetime = 10;
    s->frameDelay = 3;
    SetCallbacks(s, &CParticleSource::EmitPortal, &CParticleSource::UpdatePortal, &CParticleSource::TickPortal);
    s->elements.resize(1);
    SetBits(&s->additiveBlend, 1);
}

// Casting circles (0x203F, 0x2040): two elements with a countdown of 100.
void SetupCastingCircle(CParticleSource* s) {
    s->flags_ |= 3;
    s->emitCount = 30;
    s->frameDelay = 1;
    s->maxParticles = 600;
    SetCallbacks(s, &CParticleSource::EmitCylinder, &CParticleSource::UpdateCylinder, &CParticleSource::TickCylinder);
    s->elements.resize(2);
    for (int i = 0; i < std::ssize(s->elements); ++i) s->elements[i].ticks = 100;
}

// FeetCloud1/2 (0x204A blue, 0x204B red): two elements, one per foot.
void SetupFeetCloud(CParticleSource* s) {
    SetBits(&s->additiveBlend, 1);
    s->emitCount = 8;
    s->flags_ |= 3;
    s->maxParticles = 300;
    s->elements.resize(2);
    for (int i = 0; i < 2; ++i) {
        s->elements[i].foot.countdown = -1;
        s->elements[i].ticks = i;
        s->elements[i].pos = {};
        s->elements[i].foot.radius = 0.0f;
    }
    SetCallbacks(s, &CParticleSource::EmitFeetCloud, &CParticleSource::UpdateFeetCloud, &CParticleSource::TickFeetCloud);
    if (ParticleTypeOf(s->type_) == ParticleType::FeetCloud1) s->lifetime = 0xFF;
    else if (ParticleTypeOf(s->type_) == ParticleType::FeetCloud2) s->lifetime = 0xFF0000;
}

// StartTrans (0x2051): one element with a 30-tick countdown.
void SetupStartTrans(CParticleSource* s) {
    s->attachPoint = 7;
    s->flags_ |= 0x12;
    s->emitCount = 1;
    s->maxParticles = 300;
    SetCallbacks(s, &CParticleSource::EmitStartTrans, &CParticleSource::UpdateStartTrans, &CParticleSource::TickStartTrans);
    s->elements.resize(1);
    s->elements[0].ticks = 30;
    s->elements[0].trans.scale = 0.0f;
    s->elements[0].trans.count = 1.0f;
}

// Fills the type's parameters; returns the texture name, or null for an unknown type.
const char* SetupType(CParticleSource* s) {
    const ParticleType type = ParticleTypeOf(s->type_);
    switch (type) {
    case ParticleType::FireBall:        // FireSmoke
        s->lifetime = 15;
        s->frameCount = 15;
        SetSpread(s, -0.2f, -0.2f, -1.0f, 0.2f, 0.2f, 0.0f);
        s->emitChance = 88;
        s->emitCount = 1;
        s->emitSpacing = 0.09f;
        s->frameDelay = 1;
        s->size = 0.8f;
        s->growth = 0.94f;
        SetCallbacks(s, &CParticleSource::EmitFireBall, &CParticleSource::UpdateFireBall, nullptr);
        return "FireSmoke";
    case ParticleType::CampFire: case ParticleType::Fire: case ParticleType::Smoke: case ParticleType::Tendril:
        SetupSmoke(s);
        return "TendriSmoke";
    case ParticleType::FireBlast:
        SetupExplosion(s);
        return "Explosion";
    case ParticleType::VulcanSmoke:        // Vulkan
        SetSpread(s, 0.2f, 0.5f, 0.0f, 0.3f, 4.0f, 0.0f);
        s->emitChance = 88;
        s->emitCount = 2;
        s->maxParticles = 400;
        s->lifetime = 16;
        s->frameDelay = 1;
        s->size = 2.5f;
        s->height = static_cast<float>(D(s->spreadMax.y) * D(EI_SIXTY_FOURTH));
        s->growth = 0.99f;
        SetCallbacks(s, &CParticleSource::EmitVulkan, &CParticleSource::UpdateVulkan, nullptr);
        return "Vulkan";
    case ParticleType::Healing:        // Healing
        s->flags_ |= 2;
        SetBits(&s->additiveBlend, 1);
        s->emitCount = 0x7FFFFFFF;
        s->lifetime = 8;
        s->frameDelay = 1;
        s->size = 0.5f;
        SetCallbacks(s, &CParticleSource::EmitHealing, &CParticleSource::UpdateHealing, &CParticleSource::TickHealing);
        return "Healing";
    case ParticleType::PoisonFog: case ParticleType::PoisonCloud:   // PoisonFog
        s->emitChance = 50;
        s->emitCount = 1;
        s->maxParticles = 0x7FFFFFFF;
        s->lifetime = 0xCA4;
        s->frameCount = 15;
        s->frameDelay = 1;
        SetCallbacks(s, &CParticleSource::EmitPoisonFog, &CParticleSource::UpdateCloud, nullptr);
        return "PoisonCloud";
    case ParticleType::Geyser:
        SetupFireworks(s);
        return "Fireworks";
    case ParticleType::Tornado:
        SetupTornado(s);
        return "Tornado";
    case ParticleType::Casting:
        s->flags_ |= 3;
        s->emitCount = 0x7FFFFFFF;
        s->lifetime = -1;
        s->frameDelay = 1;
        SetCallbacks(s, &CParticleSource::EmitCastRing, &CParticleSource::UpdateCastRing, &CParticleSource::TickCastRing);
        return "FireSmoke";
    case ParticleType::Nuke:
        SetupNuke(s);
        return "AcidRay";
    case ParticleType::Banshee:        // Banshee
        s->flags_ |= 3;
        s->emitCount = 0x7FFFFFFF;
        s->lifetime = -1;
        s->frameDelay = 1;
        SetCallbacks(s, &CParticleSource::EmitBanshee, &CParticleSource::UpdateBanshee, &CParticleSource::TickBanshee);
        s->elements.resize(0);
        return "Banshee";
    case ParticleType::Mushroom: case ParticleType::Transform:   // Mushroom, Transform
        s->emitCount = 10;
        s->maxParticles = 4000;
        s->lifetime = -1;
        s->frameDelay = 1;
        s->height = 1.0f;
        SetCallbacks(s, &CParticleSource::EmitMushroom, &CParticleSource::UpdateMushroom, &CParticleSource::TickMushroom);
        return type == ParticleType::FireBlast ? "Explosion" : "ChangeHero";   // the test never holds here
    case ParticleType::Shower1: case ParticleType::Shower2: case ParticleType::Shower3: case ParticleType::Shower4:
        SetupWeather(s);
        return "RainSnow";
    case ParticleType::FireArrow: case ParticleType::RikArrowSmoke:   // arrow smoke
        s->lifetime = 15;
        s->frameCount = 15;
        SetSpread(s, -0.2f, -0.2f, -0.2f, 0.2f, 0.2f, 0.2f);
        s->emitCount = 1;
        s->emitSpacing = 0.06f;
        s->frameDelay = 1;
        s->size = 0.3f;
        s->growth = 1.03f;
        s->emitState = 0.0f;
        SetCallbacks(s, &CParticleSource::EmitArrowSmoke, &CParticleSource::UpdateArrowSmoke, nullptr);
        return type == ParticleType::RikArrowSmoke ? "RikArrowSmoke" : "FireArrowSmoke";
    case ParticleType::AcidRay:
        s->lifetime = 15;
        s->frameCount = 15;
        SetSpread(s, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f);
        s->emitCount = 1;
        s->emitSpacing = 0.06f;
        s->frameDelay = 1;
        s->size = 0.3f;
        SetCallbacks(s, &CParticleSource::EmitAcidRay, &CParticleSource::UpdateAcidRay, nullptr);
        return "AcidRay";
    case ParticleType::BlueGas:        // Stench
        s->flags_ |= 2;
        SetSpread(s, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f);
        s->emitChance = 80;
        s->emitCount = 5;
        s->lifetime = 45;
        s->frameCount = 15;
        s->frameDelay = 3;
        s->size = 1.0f;
        SetCallbacks(s, &CParticleSource::EmitStench, &CParticleSource::UpdateStench, &CParticleSource::TickNone);
        return "Stench";
    case ParticleType::Link:        // Link
        SetSpread(s, -0.2f, -0.2f, -0.2f, 0.2f, 0.2f, 0.2f);
        s->emitChance = 80;
        s->extent = {-10.0f, 0.0f, 0.0f};
        s->emitCount = 10;
        s->lifetime = 45;
        s->frameCount = 15;
        s->frameDelay = 3;
        s->size = 1.0f;
        SetCallbacks(s, &CParticleSource::EmitLink, &CParticleSource::UpdateLink, &CParticleSource::TickLink);
        return "Link";
    case ParticleType::SphereAcid: case ParticleType::SphereElectricity: case ParticleType::SphereFire: case ParticleType::Antimagic:   // protection spheres, AntiMagic
        s->flags_ |= 2;
        SetBits(&s->additiveBlend, 1);
        s->emitChance = 90;
        s->emitCount = 1;
        s->lifetime = type == ParticleType::Antimagic ? 4 : 15;
        s->frameCount = 15;
        SetSpread(s, -0.2f, -0.2f, -1.0f, 0.2f, 0.2f, 0.0f);
        s->frameDelay = 1;
        s->size = 1.0f;
        SetCallbacks(s, &CParticleSource::EmitOrbit, &CParticleSource::UpdateOrbit, &CParticleSource::TickNone);
        if (type == ParticleType::SphereFire) return "ProtectionF";
        if (type == ParticleType::SphereElectricity) return "ProtectionE";
        if (type == ParticleType::Antimagic) return "AntiMagic";
        return "ProtectionA";
    case ParticleType::ClayRing:
        SetupClayRing(s);
        return "ClayRing";
    case ParticleType::Teleport:
        SetupTeleport(s);
        return "Teleport";
    case ParticleType::Modifier1: case ParticleType::Modifier2: case ParticleType::Modifier3: case ParticleType::Modifier4: case ParticleType::Modifier5: case ParticleType::Modifier6:
    case ParticleType::Modifier7: case ParticleType::Modifier8: case ParticleType::Modifier9: case ParticleType::Modifier10: case ParticleType::Modifier11: case ParticleType::Modifier12:   // Modifier1-12
        s->emitCount = 80;
        s->flags_ |= 3;
        s->lifetime = 14;
        s->frameDelay = 2;
        SetCallbacks(s, &CParticleSource::EmitModifier, &CParticleSource::UpdateModifier, &CParticleSource::TickModifier);
        return "Stun";
    case ParticleType::CastingFire: case ParticleType::CastingElectricity: case ParticleType::CastingAcid: case ParticleType::CastingDivination:
        s->flags_ |= 0x13;
        s->attachPoint = 0;
        SetCallbacks(s, &CParticleSource::EmitCasting, &CParticleSource::UpdateCasting, &CParticleSource::TickCasting);
        s->lifetime = 5;
        s->emitCount = 20;
        if (type == ParticleType::CastingFire) {
            s->frameDelay = 5;
            return "FireSmoke";
        }
        if (type == ParticleType::CastingElectricity) {
            s->frameDelay = 1;
            return "Casting8";
        }
        if (type == ParticleType::CastingAcid) {
            s->frameDelay = 5;
            return "Casting7";
        }
        s->emitCount = 6;
        s->frameDelay = 1;
        s->attachPoint = 2;
        SetBits(&s->additiveBlend, 1);
        return "Casting4";
    case ParticleType::CastingIllusion: case ParticleType::CastingDomination: case ParticleType::CastingEnchantment: case ParticleType::CastingHealing:
        s->flags_ |= 3;
        SetSpread(s, -0.04f, -0.04f, -0.04f, 0.04f, 0.04f, 0.04f);
        s->emitCount = 10;
        s->frameDelay = 1;
        s->maxParticles = 600;
        SetCallbacks(s, &CParticleSource::EmitCastSwirl, &CParticleSource::UpdateCastSwirl, &CParticleSource::TickCastSwirl);
        if (type == ParticleType::CastingIllusion) {
            s->lifetime = 2;
            return "Casting2";
        }
        if (type == ParticleType::CastingDomination) {
            s->lifetime = 1;
            return "Casting3";
        }
        if (type == ParticleType::CastingEnchantment) {
            s->lifetime = 2;
            return "Casting6";
        }
        s->lifetime = 1;
        return "Casting1";
    case ParticleType::TeleportRing:
        s->emitCount = 25;
        s->flags_ |= 3;
        s->lifetime = 14;
        s->frameDelay = 1;
        SetCallbacks(s, &CParticleSource::EmitColumn, &CParticleSource::UpdateColumn, &CParticleSource::TickColumn);
        return "Teleport";
    case ParticleType::LightningBlast:        // LightningBlast
        s->lifetime = 15;
        s->frameCount = 15;
        SetSpread(s, -0.2f, -0.2f, -0.2f, 0.2f, 0.2f, 0.2f);
        s->emitChance = 88;
        s->emitCount = 250;
        s->maxParticles = 600;
        s->frameDelay = 1;
        s->size = 1.0f;
        SetCallbacks(s, &CParticleSource::EmitLightning, &CParticleSource::UpdateLightning, &CParticleSource::TickLightning);
        SetBits(&s->additiveBlend, 1);
        return "TendriLight";
    case ParticleType::Falling1: case ParticleType::Falling2: case ParticleType::Falling3: case ParticleType::Falling4:
        SetupFalling(s);
        return "RainSnow";
    case ParticleType::ZoneExit:        // ZoneExit
        s->emitCount = 10;
        s->maxParticles = 600;
        s->alpha = 5.0f;
        s->typeParam = 5.0f;
        s->flags_ |= 0x21;
        SetCallbacks(s, &CParticleSource::EmitZoneExit, &CParticleSource::UpdateZoneExit, &CParticleSource::TickNone);
        return "ZoneExit";
    case ParticleType::ShapeChange:
        s->emitFn = &CParticleSource::EmitGlow;
        s->flags_ |= 9;
        s->emitCount = 2500;
        s->maxParticles = 2500;
        s->updateFn = &CParticleSource::UpdateGlow;
        s->elementFn = &CParticleSource::TickGlow;
        return "ShapeChange";
    case ParticleType::GroundDust1: case ParticleType::GroundDust2:
        SetupGroundPlane(s);
        return "ShapeChange";
    case ParticleType::Bag:
        SetupBag(s);
        return "Bag";
    case ParticleType::PortalStar:
        s->emitCount = 1;
        s->maxParticles = 1;
        SetCallbacks(s, &CParticleSource::EmitPortalStar, &CParticleSource::UpdatePortalStar, &CParticleSource::TickNone);
        return "Casting6";
    case ParticleType::Portal:
        SetupPortal(s);
        return "Teleport";
    case ParticleType::Cylinder1: case ParticleType::Cylinder2:
        SetupCastingCircle(s);
        return type == ParticleType::Cylinder1 ? "Casting2" : "Casting1";
    case ParticleType::FireStar: case ParticleType::AcidStar:
        s->flags_ |= 0x13;
        s->attachPoint = 7;
        s->emitCount = 1;
        SetCallbacks(s, &CParticleSource::EmitFireStar, &CParticleSource::UpdateFireStar, &CParticleSource::TickNone);
        s->lifetime = 15;
        return type == ParticleType::FireStar ? "FireArrowSmoke" : "AcidRay";
    case ParticleType::Sparks:        // Sparks
        SetBits(&s->additiveBlend, 1);
        s->flags_ |= 0x13;
        s->attachPoint = 7;
        s->emitCount = 10;
        s->maxParticles = 300;
        SetCallbacks(s, &CParticleSource::EmitSparks, &CParticleSource::UpdateSparks, &CParticleSource::TickNone);
        return "Casting6";
    case ParticleType::VisionStar1: case ParticleType::VisionStar2: case ParticleType::VisionStar3: case ParticleType::VisionStar4:
        s->flags_ |= 0x13;
        SetBits(&s->additiveBlend, 1);
        s->attachPoint = 2;
        s->emitCount = 3;
        SetCallbacks(s, &CParticleSource::EmitVisionStar, &CParticleSource::UpdateVisionStar, &CParticleSource::TickNone);
        if (type == ParticleType::VisionStar1) s->lifetime = 0;
        else if (type == ParticleType::VisionStar2) s->lifetime = 1;
        else if (type == ParticleType::VisionStar3) s->lifetime = 2;
        else s->lifetime = 3;
        return "Stun";
    case ParticleType::Regeneration:
        SetBits(&s->additiveBlend, 1);
        s->flags_ |= 0x13;
        s->attachPoint = 7;
        s->emitCount = 2;
        SetCallbacks(s, &CParticleSource::EmitRegeneration, &CParticleSource::UpdateRegeneration, &CParticleSource::TickNone);
        return "Stun";
    case ParticleType::Silence:
        SetBits(&s->additiveBlend, 1);
        s->flags_ |= 0x13;
        s->attachPoint = 7;
        s->emitCount = 4;
        s->maxParticles = 300;
        SetCallbacks(s, &CParticleSource::EmitSilence, &CParticleSource::UpdateSilence, &CParticleSource::TickNone);
        return "Stun";
    case ParticleType::FeebleMind:        // FeebleMind
        SetBits(&s->additiveBlend, 1);
        s->flags_ |= 0x13;
        s->attachPoint = 1;
        s->emitCount = 5;
        s->maxParticles = 300;
        SetCallbacks(s, &CParticleSource::EmitFeebleMind, &CParticleSource::UpdateFeebleMind, &CParticleSource::TickNone);
        return "Link";
    case ParticleType::FeetCloud1: case ParticleType::FeetCloud2:
        SetupFeetCloud(s);
        return "VisionFog";
    case ParticleType::Stun:
        SetBits(&s->additiveBlend, 1);
        s->flags_ |= 0x13;
        s->attachPoint = 0;
        s->emitCount = 1;
        s->maxParticles = 300;
        SetCallbacks(s, &CParticleSource::EmitStun, &CParticleSource::UpdateStun, &CParticleSource::TickNone);
        return "Stun";
    case ParticleType::CurseStars:
        SetBits(&s->additiveBlend, 1);
        s->flags_ |= 0x13;
        s->attachPoint = 7;
        s->emitCount = 8;
        s->maxParticles = 300;
        SetCallbacks(s, &CParticleSource::EmitCurseStars, &CParticleSource::UpdateCurseStars, &CParticleSource::TickNone);
        return "Healing";
    case ParticleType::CurseHolder:
        SetupCurseHolder(s);
        return "Tornado";
    case ParticleType::StartTrans:
        SetupStartTrans(s);
        return "ZoneExit";
    default:
        return nullptr;
    }
}

// The frames are cells of a grid `columns` wide, row 0 at the top (v = 1), each inset by 1/512.
void BuildFrames(CParticleSource* s) {
    float step = static_cast<float>(D(EI_ONE) / static_cast<double>(s->frameColumns));
    s->frames.resize(s->frameCount);
    SourceFrame* rect = s->frames.data();
    for (int i = 0; i < s->frameCount; ++i, ++rect) {
        double column = static_cast<double>(i % s->frameColumns);
        float row = static_cast<float>(static_cast<double>(i / s->frameColumns));
        rect->u0 = static_cast<float>(column * D(step) + D(EI_UV_INSET));
        rect->v0 = static_cast<float>(D(EI_ONE) - (D(row) * D(step) + D(EI_UV_INSET)));
        rect->u1 = static_cast<float>((column + D(EI_ONE)) * D(step) - D(EI_UV_INSET));
        rect->v1 = static_cast<float>(D(EI_ONE) - ((D(row) + D(EI_ONE)) * D(step) - D(EI_UV_INSET)));
    }
}

} // namespace

// Back to the defaults: no position, attachment, callbacks or elements; the texture
// released; unit sizes, a 1000 emission spacing, 10000 frame delay, a one-frame grid.
void CParticleSource::Reset() {
    localPosition_ = {};
    localRotation_ = kIdentityRotation;
    attachedTo = nullptr;
    emitChance = 0;
    emitCount = 0;
    lifetime = -1;
    frameCount = 1;
    frameColumns = 1;
    frameDelay = 10000;
    texture.Reset();
    windDirection = {};
    extent = {};
    emitFn = nullptr;
    updateFn = nullptr;
    elementFn = nullptr;
    windStrength = 0.0f;
    additiveBlend = 0.0f;
    lib::FreeStorage(elements);
    alpha = 1.0f;
    typeParam = 1.0f;
    size = 1.0f;
    baseSize = 1.0f;
    spreadMin = {};
    spreadMax = {};
    emitState = 0.0f;
    height = 0.0f;
    growth = 0.0f;
    attachPoint = 0;
    hiddenTicks = 0;
    turnCos = 0.0f;
    turnSin = 0.0f;
    emitSpacing = 1000.0f;
}

// Clear (the destroy virtual). Returns every particle to the pool, releases the texture, detaches from the
// object the source follows, then resets.
void CParticleSource::Clear() {
    ReleaseAllParticles();
    texture.Reset();
    if (attachedTo)
        attachedTo->RemoveEffectSource(this);
    Reset();
}

// Create. Sets the source up for its type; 1, or for an unknown type the fatal error report's result.
void CParticleSource::Create() {
    Reset();
    float farBound = 100000.0f;     // 100000.0f
    boundsMin.x = farBound;
    boundsMin.y = farBound;
    boundsMin.z = farBound;
    Vector3 nearBound;
    nearBound.Construct(-100000.0f, -100000.0f, -100000.0f);
    boundsMax = nearBound;
    frameColumns = 4;
    emitChance = 100;
    flags_ = (flags_ & ~0x18u) | 1;
    maxParticles = 500;
    frameCount = 16;

    const char* textureName = SetupType(this);
    if (!textureName) {
        game::FatalError("CParticle::Create().\nUnknown ETypeID.\n");
        return;
    }

    char name[268];
    std::memcpy(name, textureName, std::strlen(textureName) + 1);
    LoadNamedTexture(texture, 6, name);
    BuildFrames(this);
}

// --- Factory and destructor ----------------------------------------------------------------------

// A source never attached emits distance / emitSpacing tries (each a GlobalRandom draw) from the emission point
// it was allocated with, so the memory comes zeroed.
CParticleSource::CParticleSource(int type) {
    ClearFields();
    localRotation_.SetIdentity();
    worldRotation_.SetIdentity();
    std::memset(static_cast<void*>(&clip), 0, offsetof(CParticleSource, particles) - offsetof(CParticleSource, clip));
    Reset();
    type_ = type;
}

// Sources start zeroed: nothing in the constructor or Reset sets the emission point (origin) and the first Tick
// takes the motion from it. The harness zeroes the game's sources alike.
CParticleSource* NewParticleSource(int type) { return new (kZeroed) CParticleSource(type); }

CEngineObject* CParticleManager::NewObject(int type) {
    return NewParticleSource(type);
}

CParticleSource::~CParticleSource() {
    Clear();
    texture.Reset();
    particles.clear();
}

const char* CParticleSource::ClassName() { return "CParticle"; }
ClassId CParticleSource::GetClassId() { return ClassId::ParticleSource; }

// The element callbacks that do nothing one per type that stores one (Stench and the spheres,
// ZoneExit, PortalStar, FireStar, Sparks, VisionStar, Regeneration, Silence, FeebleMind, Stun, CurseStars).
void CParticleSource::TickNone(ParticleElement*) {}
