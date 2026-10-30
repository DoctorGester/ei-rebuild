// ParticleSource set-up: Create, Reset and Clear.
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

#include <cmath>
#include <cstring>

namespace {

// A random word as a float.
float Unsigned(std::uint32_t value) { return static_cast<float>(value); }

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
#define EI_SCALE_2 4.656613e-10f   // ~2^-31
#define EI_SCALE_0_4 9.3132266e-11f   // ~0.4 * 2^-32
#define EI_RING_STEP 0.62832f   // 2 pi / 10

// The scene light colour the Tornado takes its particle colour from (the sun's r, g, b).
const float* SceneLightColor() { return g_sceneRenderer.sun->color; }

// The rotation Reset gives a source: the identity (a static initialiser's stores).
constexpr Quaternion kIdentityRotation = /*007A9C30*/ {1.0f, 0.0f, 0.0f, 0.0f};

// The spread box particles start in, relative to the emission point.
void SetSpread(ParticleSource* s, float x0, float y0, float z0, float x1, float y1, float z1) {
    s->spreadMin = {x0, y0, z0};
    s->spreadMax = {x1, y1, z1};
}

// A null element callback keeps the one Reset left (null).
void SetCallbacks(ParticleSource* s, ParticleSource::EmitCallback emit, ParticleSource::UpdateCallback update,
                  ParticleSource::ElementCallback element) {
    s->emitFn = emit;
    s->updateFn = update;
    if (element) s->elementFn = element;
}

// --- Types with their own set-up code ------------------------------------------------------------

// Smoke (0x2001, 0x2003, 0x2004) and the tendril smoke 0x2010, which also keeps one element.
void SetupSmoke(ParticleSource* s) {
    SetSpread(s, -0.2f, -0.2f, 0.0f, 0.2f, 0.2f, 0.0f);
    s->emitChance = 88;
    s->emitCount = 6;
    s->lifetime = 32;
    s->frameCount = 15;
    s->frameDelay = 1;
    s->size = 0.8f;
    s->height = 0.3f;
    s->growth = 0.85f;
    SetCallbacks(s, &ParticleSource::EmitSmoke, &ParticleSource::UpdateSmoke, nullptr);
    if (ParticleTypeOf(s->type) == ParticleType::Tendril) {
        s->emitCount = 20;
        s->maxParticles = 1500;
        s->elements.resize(1);
        s->emitFn = &ParticleSource::EmitTendril;
        s->elementFn = &ParticleSource::TickTendril;
    }
}

// Explosion (0x2002): fifteen fragments, each flying in a random direction (a point in the unit
// ball shifted up by 0.7, ten tries) with a random speed.
void SetupExplosion(ParticleSource* s) {
    SetSpread(s, -0.02f, -0.02f, -0.02f, 0.02f, 0.02f, 0.02f);
    s->emitChance = 100;
    s->emitCount = 60;
    s->maxParticles = 600;
    s->lifetime = 15;
    s->frameDelay = 1;
    s->size = 0.6f;
    s->height = 1.0f;
    s->growth = 0.8f;
    SetCallbacks(s, &ParticleSource::EmitBlast, &ParticleSource::UpdateBlast, &ParticleSource::TickBlast);
    s->elements.resize(15);
    for (int i = 0; i < std::ssize(s->elements); ++i) {
        s->elements[i].pos = {};
        float x, y, z;
        float lengthSquared;
        for (int tries = 10;;) {
            x = Unsigned(g_clientRandom.Next()) * EI_SCALE_2 - EI_ONE;
            y = g_clientRandom.Range(-1.0f, 1.0f);
            z = g_clientRandom.Range(-1.0f, 1.0f) + EI_POINT_SEVEN;
            lengthSquared = LengthSquaredUnrounded<Sum3::ZYX>(x, y, z);
            if (lengthSquared < EI_ONE) break;   // random: never NaN
            if (!(--tries > 0)) break;
        }
        float length = std::sqrt(lengthSquared);
        Vector3 v;
        v.Construct(x, y, z);
        Vector3 direction;
        DivideVector(&direction, v, length);
        float speed = Unsigned(g_clientRandom.Next()) * EI_SCALE_0_4 + EI_POINT_EIGHT;
        float vx = direction.x * speed;
        float vy = speed * direction.y;
        float vz = speed * direction.z;
        auto& fragment = s->elements[i].fragment;
        fragment.velocity.x = vx * EI_POINT_EIGHT;
        fragment.velocity.y = vy * EI_POINT_EIGHT;
        fragment.velocity.z = vz * EI_POINT_EIGHT;
        fragment.age = 0;
        fragment.radius = 0.0f;
        fragment.growth = 0.4f;
        fragment.decay = 0.7f;
        fragment.frame = Unsigned(g_clientRandom.Next() % 6);
        fragment.sizeFactor = Unsigned(g_clientRandom.Next() & 1);
    }
}

// Fireworks (0x2009): three rockets.
void SetupFireworks(ParticleSource* s) {
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
    SetCallbacks(s, &ParticleSource::EmitGeyser, &ParticleSource::UpdateGeyser, &ParticleSource::TickGeyser);
    lib::StoreBytes(&s->additiveBlend, 1);
}

// Tornado (0x200A) and CurseHolder (0x2050): six rings stacked 2.4 apart.
void SetupTornadoRings(ParticleSource* s) {
    s->elements.resize(6);
    for (int i = 0; i < std::ssize(s->elements); ++i) {
        ParticleElement& e = s->elements[i];
        std::memset(&e.pos, 0, 2 * sizeof(float));
        e.pos.z = i * EI_TWELVE * EI_POINT_TWO;
        e.ticks = 0;
        e.word[9] = 0;
    }
}

void SetupTornado(ParticleSource* s) {
    s->emitChance = 100;
    s->emitCount = 4;
    s->maxParticles = 800;
    s->frameDelay = 1;
    s->height = 0.2f;
    // The particle colour: 0x19 alpha, then the scene light's r, g, b (the delay field holds it).
    const float* light = SceneLightColor();
    std::uint32_t color = static_cast<std::uint32_t>(light[0] * EI_COLOR_SCALE);
    color = (color | 0x1900) << 8;
    color |= static_cast<std::uint32_t>(light[1] * EI_COLOR_SCALE);
    color <<= 8;
    color |= static_cast<std::uint32_t>(light[2] * EI_COLOR_SCALE);
    lib::StoreBytes(&s->frameDelay, color);
    SetupTornadoRings(s);
    s->elements[0].funnel.height = 12.0f;  // 12.0
    s->elements[0].funnel.bend = 0.5f;
    s->elements[0].funnel.damping = 0.99f;     // 0.99
    SetCallbacks(s, &ParticleSource::EmitTornado, &ParticleSource::UpdateTornado, &ParticleSource::TickTornado);
}

void SetupCurseHolder(ParticleSource* s) {
    s->flags |= ParticleSourceFlag::AttachedOnly | ParticleSourceFlag::OnPart;
    s->emitChance = 100;
    s->emitCount = 4;
    s->attachPoint = 7;
    s->maxParticles = 800;
    s->height = 0.2f;
    lib::StoreBytes(&s->frameDelay, 0x19000000);
    SetupTornadoRings(s);
    s->elements[0].funnel.height = EI_TWELVE * EI_POINT_FOUR;
    s->elements[0].funnel.bend = 0.0f;
    s->elements[0].funnel.damping = 0.997f;     // 0.997
    SetCallbacks(s, &ParticleSource::EmitTornado, &ParticleSource::UpdateTornado, &ParticleSource::TickTornado);
}

// Nuke (0x200C): one element (the mushroom's state).
void SetupNuke(ParticleSource* s) {
    s->emitCount = 128;
    s->maxParticles = 400;
    s->lifetime = -1;
    s->frameDelay = 1;
    s->height = 1.0f;
    SetCallbacks(s, &ParticleSource::EmitNuke, &ParticleSource::UpdateDebris, &ParticleSource::TickNuke);
    s->elements.resize(1);
    s->elements[0].ticks = 0;
}

// Rain and snow (0x200F, 0x2031-0x2033): one element, whose ticks pick the kind.
void SetupWeather(ParticleSource* s) {
    s->attachPoint = 7;
    s->flags |= ParticleSourceFlag::AttachedOnly | ParticleSourceFlag::OnPart;
    SetSpread(s, -0.2f, -0.2f, -0.2f, 0.2f, 0.2f, 0.2f);
    s->emitCount = 10;
    s->maxParticles = 300;
    s->lifetime = 6;
    s->frameDelay = 15;
    SetCallbacks(s, &ParticleSource::EmitShower, &ParticleSource::UpdateShower, &ParticleSource::TickShower);
    s->elements.resize(1);
    s->elements[0].ray.age = -100;
    switch (ParticleTypeOf(s->type)) {
    case ParticleType::Shower1: s->elements[0].ticks = 12; break;
    case ParticleType::Shower2: s->elements[0].ticks = 13; break;
    case ParticleType::Shower3: s->elements[0].ticks = 14; break;
    case ParticleType::Shower4: s->elements[0].ticks = 15; break;
    }
}

// Falling leaves and the like (0x2034-0x2037): four to eight elements.
void SetupFalling(ParticleSource* s) {
    s->attachPoint = 7;
    s->flags |= ParticleSourceFlag::AttachedOnly | ParticleSourceFlag::OnPart;
    SetSpread(s, -0.2f, -0.2f, -0.2f, 0.2f, 0.2f, 0.2f);
    s->emitCount = 45;
    s->maxParticles = 300;
    s->lifetime = 6;
    s->frameDelay = 15;
    SetCallbacks(s, &ParticleSource::EmitFalling, &ParticleSource::UpdateFalling, &ParticleSource::TickFalling);
    s->elements.resize(static_cast<int>(g_clientRandom.Next() % 5 + 4));
    for (int i = 0; i < std::ssize(s->elements); ++i) s->elements[i].wind.age = 0;
}

// ClayRing (0x2018): ten points on a circle around the source's (reset) position.
void SetupClayRing(ParticleSource* s) {
    lib::StoreBytes(&s->additiveBlend, 1);
    s->emitCount = 80;
    s->lifetime = 15;
    s->frameDelay = 2;
    SetCallbacks(s, &ParticleSource::EmitClayRing, &ParticleSource::UpdateClayRing, &ParticleSource::TickClayRing);
    s->elements.resize(10);
    for (int i = 0; i < std::ssize(s->elements); ++i) {
        float angle = i * EI_RING_STEP;
        float* direction = &s->elements[i].clay.direction.x;
        direction[0] = std::sin(angle);
        direction[1] = std::cos(angle);
        direction[2] = 0.0f;
        s->elements[i].pos = s->localPosition;
        s->elements[i].clay.previous = s->localPosition;
        s->elements[i].ticks = 14;
    }
}

// Teleport (0x2019) and Nuke share "one element with ticks = 0".
void SetupTeleport(ParticleSource* s) {
    s->emitCount = 20;
    s->maxParticles = 320;
    s->lifetime = 14;
    s->frameDelay = 1;
    SetCallbacks(s, &ParticleSource::EmitTeleport, &ParticleSource::UpdateTeleport, &ParticleSource::TickTeleport);
    s->elements.resize(1);
    s->elements[0].ticks = 0;
}

// Ground plane (0x203A green, 0x203B red): one element holding the colour.
void SetupGroundPlane(ParticleSource* s) {
    s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::TicksWhilePaused;
    s->elements.resize(1);
    s->elements[0].ticks = 0;
    if (ParticleTypeOf(s->type) == ParticleType::GroundDust1) {
        s->emitCount = 128;
        s->elements[0].plane.color = 0x40FF40;
    } else if (ParticleTypeOf(s->type) == ParticleType::GroundDust2) {
        s->emitCount = 256;
        s->elements[0].plane.color = 0xFF4040;
    }
    s->emitFn = &ParticleSource::EmitGroundDust;
    s->maxParticles = s->emitCount;
    s->updateFn = &ParticleSource::UpdateGroundDust;
    s->elementFn = &ParticleSource::TickGroundPlane;
}

// Bag (0x203C): eighty elements.
void SetupBag(ParticleSource* s) {
    s->emitCount = 100;
    s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly;
    s->maxParticles = 800;
    SetCallbacks(s, &ParticleSource::EmitBag, &ParticleSource::UpdateBag, &ParticleSource::TickBag);
    s->elements.resize(80);
    for (int i = 0; i < 80; ++i) s->elements[i].ticks = 0;
}

// Portal (0x203E): one element.
void SetupPortal(ParticleSource* s) {
    s->emitCount = 50;
    s->maxParticles = 800;
    s->lifetime = 10;
    s->frameDelay = 3;
    SetCallbacks(s, &ParticleSource::EmitPortal, &ParticleSource::UpdatePortal, &ParticleSource::TickPortal);
    s->elements.resize(1);
    lib::StoreBytes(&s->additiveBlend, 1);
}

// Casting circles (0x203F, 0x2040): two elements with a countdown of 100.
void SetupCastingCircle(ParticleSource* s) {
    s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly;
    s->emitCount = 30;
    s->frameDelay = 1;
    s->maxParticles = 600;
    SetCallbacks(s, &ParticleSource::EmitCylinder, &ParticleSource::UpdateCylinder, &ParticleSource::TickCylinder);
    s->elements.resize(2);
    for (int i = 0; i < std::ssize(s->elements); ++i) s->elements[i].ticks = 100;
}

// FeetCloud1/2 (0x204A blue, 0x204B red): two elements, one per foot.
void SetupFeetCloud(ParticleSource* s) {
    lib::StoreBytes(&s->additiveBlend, 1);
    s->emitCount = 8;
    s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly;
    s->maxParticles = 300;
    s->elements.resize(2);
    for (int i = 0; i < 2; ++i) {
        s->elements[i].foot.countdown = -1;
        s->elements[i].ticks = i;
        s->elements[i].pos = {};
        s->elements[i].foot.radius = 0.0f;
    }
    SetCallbacks(s, &ParticleSource::EmitFeetCloud, &ParticleSource::UpdateFeetCloud, &ParticleSource::TickFeetCloud);
    if (ParticleTypeOf(s->type) == ParticleType::FeetCloud1) s->lifetime = 0xFF;
    else if (ParticleTypeOf(s->type) == ParticleType::FeetCloud2) s->lifetime = 0xFF0000;
}

// StartTrans (0x2051): one element with a 30-tick countdown.
void SetupStartTrans(ParticleSource* s) {
    s->attachPoint = 7;
    s->flags |= ParticleSourceFlag::AttachedOnly | ParticleSourceFlag::OnPart;
    s->emitCount = 1;
    s->maxParticles = 300;
    SetCallbacks(s, &ParticleSource::EmitStartTrans, &ParticleSource::UpdateStartTrans, &ParticleSource::TickStartTrans);
    s->elements.resize(1);
    s->elements[0].ticks = 30;
    s->elements[0].trans.scale = 0.0f;
    s->elements[0].trans.count = 1.0f;
}

// Fills the type's parameters; returns the texture name, or null for an unknown type.
const char* SetupType(ParticleSource* s) {
    const ParticleType type = ParticleTypeOf(s->type);
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
        SetCallbacks(s, &ParticleSource::EmitFireBall, &ParticleSource::UpdateFireBall, nullptr);
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
        s->height = s->spreadMax.y * EI_SIXTY_FOURTH;
        s->growth = 0.99f;
        SetCallbacks(s, &ParticleSource::EmitVulkan, &ParticleSource::UpdateVulkan, nullptr);
        return "Vulkan";
    case ParticleType::Healing:        // Healing
        s->flags |= ParticleSourceFlag::AttachedOnly;
        lib::StoreBytes(&s->additiveBlend, 1);
        s->emitCount = 0x7FFFFFFF;
        s->lifetime = 8;
        s->frameDelay = 1;
        s->size = 0.5f;
        SetCallbacks(s, &ParticleSource::EmitHealing, &ParticleSource::UpdateHealing, &ParticleSource::TickHealing);
        return "Healing";
    case ParticleType::PoisonFog: case ParticleType::PoisonCloud:   // PoisonFog
        s->emitChance = 50;
        s->emitCount = 1;
        s->maxParticles = 0x7FFFFFFF;
        s->lifetime = 0xCA4;
        s->frameCount = 15;
        s->frameDelay = 1;
        SetCallbacks(s, &ParticleSource::EmitPoisonFog, &ParticleSource::UpdateCloud, nullptr);
        return "PoisonCloud";
    case ParticleType::Geyser:
        SetupFireworks(s);
        return "Fireworks";
    case ParticleType::Tornado:
        SetupTornado(s);
        return "Tornado";
    case ParticleType::Casting:
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly;
        s->emitCount = 0x7FFFFFFF;
        s->lifetime = -1;
        s->frameDelay = 1;
        SetCallbacks(s, &ParticleSource::EmitCastRing, &ParticleSource::UpdateCastRing, &ParticleSource::TickCastRing);
        return "FireSmoke";
    case ParticleType::Nuke:
        SetupNuke(s);
        return "AcidRay";
    case ParticleType::Banshee:        // Banshee
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly;
        s->emitCount = 0x7FFFFFFF;
        s->lifetime = -1;
        s->frameDelay = 1;
        SetCallbacks(s, &ParticleSource::EmitBanshee, &ParticleSource::UpdateBanshee, &ParticleSource::TickBanshee);
        s->elements.resize(0);
        return "Banshee";
    case ParticleType::Mushroom: case ParticleType::Transform:   // Mushroom, Transform
        s->emitCount = 10;
        s->maxParticles = 4000;
        s->lifetime = -1;
        s->frameDelay = 1;
        s->height = 1.0f;
        SetCallbacks(s, &ParticleSource::EmitMushroom, &ParticleSource::UpdateMushroom, &ParticleSource::TickMushroom);
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
        SetCallbacks(s, &ParticleSource::EmitArrowSmoke, &ParticleSource::UpdateArrowSmoke, nullptr);
        return type == ParticleType::RikArrowSmoke ? "RikArrowSmoke" : "FireArrowSmoke";
    case ParticleType::AcidRay:
        s->lifetime = 15;
        s->frameCount = 15;
        SetSpread(s, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f);
        s->emitCount = 1;
        s->emitSpacing = 0.06f;
        s->frameDelay = 1;
        s->size = 0.3f;
        SetCallbacks(s, &ParticleSource::EmitAcidRay, &ParticleSource::UpdateAcidRay, nullptr);
        return "AcidRay";
    case ParticleType::BlueGas:        // Stench
        s->flags |= ParticleSourceFlag::AttachedOnly;
        SetSpread(s, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f);
        s->emitChance = 80;
        s->emitCount = 5;
        s->lifetime = 45;
        s->frameCount = 15;
        s->frameDelay = 3;
        s->size = 1.0f;
        SetCallbacks(s, &ParticleSource::EmitStench, &ParticleSource::UpdateStench, &ParticleSource::TickNone);
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
        SetCallbacks(s, &ParticleSource::EmitLink, &ParticleSource::UpdateLink, &ParticleSource::TickLink);
        return "Link";
    case ParticleType::SphereAcid: case ParticleType::SphereElectricity: case ParticleType::SphereFire: case ParticleType::Antimagic:   // protection spheres, AntiMagic
        s->flags |= ParticleSourceFlag::AttachedOnly;
        lib::StoreBytes(&s->additiveBlend, 1);
        s->emitChance = 90;
        s->emitCount = 1;
        s->lifetime = type == ParticleType::Antimagic ? 4 : 15;
        s->frameCount = 15;
        SetSpread(s, -0.2f, -0.2f, -1.0f, 0.2f, 0.2f, 0.0f);
        s->frameDelay = 1;
        s->size = 1.0f;
        SetCallbacks(s, &ParticleSource::EmitOrbit, &ParticleSource::UpdateOrbit, &ParticleSource::TickNone);
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
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly;
        s->lifetime = 14;
        s->frameDelay = 2;
        SetCallbacks(s, &ParticleSource::EmitModifier, &ParticleSource::UpdateModifier, &ParticleSource::TickModifier);
        return "Stun";
    case ParticleType::CastingFire: case ParticleType::CastingElectricity: case ParticleType::CastingAcid: case ParticleType::CastingDivination:
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly | ParticleSourceFlag::OnPart;
        s->attachPoint = 0;
        SetCallbacks(s, &ParticleSource::EmitCasting, &ParticleSource::UpdateCasting, &ParticleSource::TickCasting);
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
        lib::StoreBytes(&s->additiveBlend, 1);
        return "Casting4";
    case ParticleType::CastingIllusion: case ParticleType::CastingDomination: case ParticleType::CastingEnchantment: case ParticleType::CastingHealing:
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly;
        SetSpread(s, -0.04f, -0.04f, -0.04f, 0.04f, 0.04f, 0.04f);
        s->emitCount = 10;
        s->frameDelay = 1;
        s->maxParticles = 600;
        SetCallbacks(s, &ParticleSource::EmitCastSwirl, &ParticleSource::UpdateCastSwirl, &ParticleSource::TickCastSwirl);
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
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly;
        s->lifetime = 14;
        s->frameDelay = 1;
        SetCallbacks(s, &ParticleSource::EmitColumn, &ParticleSource::UpdateColumn, &ParticleSource::TickColumn);
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
        SetCallbacks(s, &ParticleSource::EmitLightning, &ParticleSource::UpdateLightning, &ParticleSource::TickLightning);
        lib::StoreBytes(&s->additiveBlend, 1);
        return "TendriLight";
    case ParticleType::Falling1: case ParticleType::Falling2: case ParticleType::Falling3: case ParticleType::Falling4:
        SetupFalling(s);
        return "RainSnow";
    case ParticleType::ZoneExit:        // ZoneExit
        s->emitCount = 10;
        s->maxParticles = 600;
        s->alpha = 5.0f;
        s->typeParam = 5.0f;
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::IdlesOffScreen;
        SetCallbacks(s, &ParticleSource::EmitZoneExit, &ParticleSource::UpdateZoneExit, &ParticleSource::TickNone);
        return "ZoneExit";
    case ParticleType::ShapeChange:
        s->emitFn = &ParticleSource::EmitGlow;
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::TicksWhilePaused;
        s->emitCount = 2500;
        s->maxParticles = 2500;
        s->updateFn = &ParticleSource::UpdateGlow;
        s->elementFn = &ParticleSource::TickGlow;
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
        SetCallbacks(s, &ParticleSource::EmitPortalStar, &ParticleSource::UpdatePortalStar, &ParticleSource::TickNone);
        return "Casting6";
    case ParticleType::Portal:
        SetupPortal(s);
        return "Teleport";
    case ParticleType::Cylinder1: case ParticleType::Cylinder2:
        SetupCastingCircle(s);
        return type == ParticleType::Cylinder1 ? "Casting2" : "Casting1";
    case ParticleType::FireStar: case ParticleType::AcidStar:
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly | ParticleSourceFlag::OnPart;
        s->attachPoint = 7;
        s->emitCount = 1;
        SetCallbacks(s, &ParticleSource::EmitFireStar, &ParticleSource::UpdateFireStar, &ParticleSource::TickNone);
        s->lifetime = 15;
        return type == ParticleType::FireStar ? "FireArrowSmoke" : "AcidRay";
    case ParticleType::Sparks:        // Sparks
        lib::StoreBytes(&s->additiveBlend, 1);
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly | ParticleSourceFlag::OnPart;
        s->attachPoint = 7;
        s->emitCount = 10;
        s->maxParticles = 300;
        SetCallbacks(s, &ParticleSource::EmitSparks, &ParticleSource::UpdateSparks, &ParticleSource::TickNone);
        return "Casting6";
    case ParticleType::VisionStar1: case ParticleType::VisionStar2: case ParticleType::VisionStar3: case ParticleType::VisionStar4:
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly | ParticleSourceFlag::OnPart;
        lib::StoreBytes(&s->additiveBlend, 1);
        s->attachPoint = 2;
        s->emitCount = 3;
        SetCallbacks(s, &ParticleSource::EmitVisionStar, &ParticleSource::UpdateVisionStar, &ParticleSource::TickNone);
        if (type == ParticleType::VisionStar1) s->lifetime = 0;
        else if (type == ParticleType::VisionStar2) s->lifetime = 1;
        else if (type == ParticleType::VisionStar3) s->lifetime = 2;
        else s->lifetime = 3;
        return "Stun";
    case ParticleType::Regeneration:
        lib::StoreBytes(&s->additiveBlend, 1);
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly | ParticleSourceFlag::OnPart;
        s->attachPoint = 7;
        s->emitCount = 2;
        SetCallbacks(s, &ParticleSource::EmitRegeneration, &ParticleSource::UpdateRegeneration, &ParticleSource::TickNone);
        return "Stun";
    case ParticleType::Silence:
        lib::StoreBytes(&s->additiveBlend, 1);
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly | ParticleSourceFlag::OnPart;
        s->attachPoint = 7;
        s->emitCount = 4;
        s->maxParticles = 300;
        SetCallbacks(s, &ParticleSource::EmitSilence, &ParticleSource::UpdateSilence, &ParticleSource::TickNone);
        return "Stun";
    case ParticleType::FeebleMind:        // FeebleMind
        lib::StoreBytes(&s->additiveBlend, 1);
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly | ParticleSourceFlag::OnPart;
        s->attachPoint = 1;
        s->emitCount = 5;
        s->maxParticles = 300;
        SetCallbacks(s, &ParticleSource::EmitFeebleMind, &ParticleSource::UpdateFeebleMind, &ParticleSource::TickNone);
        return "Link";
    case ParticleType::FeetCloud1: case ParticleType::FeetCloud2:
        SetupFeetCloud(s);
        return "VisionFog";
    case ParticleType::Stun:
        lib::StoreBytes(&s->additiveBlend, 1);
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly | ParticleSourceFlag::OnPart;
        s->attachPoint = 0;
        s->emitCount = 1;
        s->maxParticles = 300;
        SetCallbacks(s, &ParticleSource::EmitStun, &ParticleSource::UpdateStun, &ParticleSource::TickNone);
        return "Stun";
    case ParticleType::CurseStars:
        lib::StoreBytes(&s->additiveBlend, 1);
        s->flags |= ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly | ParticleSourceFlag::OnPart;
        s->attachPoint = 7;
        s->emitCount = 8;
        s->maxParticles = 300;
        SetCallbacks(s, &ParticleSource::EmitCurseStars, &ParticleSource::UpdateCurseStars, &ParticleSource::TickNone);
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
void BuildFrames(ParticleSource* s) {
    float step = EI_ONE / s->frameColumns;
    s->frames.resize(s->frameCount);
    SourceFrame* rect = s->frames.data();
    for (int i = 0; i < s->frameCount; ++i, ++rect) {
        float column = static_cast<float>(i % s->frameColumns);
        float row = static_cast<float>(i / s->frameColumns);
        rect->u0 = column * step + EI_UV_INSET;
        rect->v0 = EI_ONE - (row * step + EI_UV_INSET);
        rect->u1 = (column + EI_ONE) * step - EI_UV_INSET;
        rect->v1 = EI_ONE - ((row + EI_ONE) * step - EI_UV_INSET);
    }
}

} // namespace

// Back to the defaults: no position, attachment, callbacks or elements; the texture
// released; unit sizes, a 1000 emission spacing, 10000 frame delay, a one-frame grid.
void ParticleSource::Reset() {
    localPosition = {};
    localRotation = kIdentityRotation;
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
void ParticleSource::Clear() {
    ReleaseAllParticles();
    texture.Reset();
    if (attachedTo)
        attachedTo->RemoveEffectSource(this);
    Reset();
}

// Create. Sets the source up for its type; 1, or for an unknown type the fatal error report's result.
void ParticleSource::Create() {
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
    flags = (flags & ~(ParticleSourceFlag::TicksWhilePaused | ParticleSourceFlag::OnPart)) | ParticleSourceFlag::Emitting;
    maxParticles = 500;
    frameCount = 16;

    const char* textureName = SetupType(this);
    if (!textureName) {
        game::FatalError("CParticle::Create().\nUnknown ETypeID.\n");
        return;
    }

    char name[268];
    std::memcpy(name, textureName, std::strlen(textureName) + 1);
    LoadNamedTexture(texture, static_cast<int>(TextureKind::Effects), name);
    BuildFrames(this);
}

// --- Factory and destructor ----------------------------------------------------------------------

// A source never attached emits distance / emitSpacing tries (each a g_clientRandom draw) from the emission point
// it was allocated with, so the memory comes zeroed.
ParticleSource::ParticleSource(int newType) {
    localRotation.SetIdentity();
    worldRotation.SetIdentity();
    std::memset(static_cast<void*>(&clip), 0, offsetof(ParticleSource, particles) - offsetof(ParticleSource, clip));
    Reset();
    type = newType;
}

// Sources start zeroed: nothing in the constructor or Reset sets the emission point (origin) and the first Tick
// takes the motion from it. The harness zeroes the game's sources alike.
ParticleSource* NewParticleSource(int type) { return new (kZeroed) ParticleSource(type); }

EngineObject* ParticleManager::NewObject(int objectType) {
    return NewParticleSource(objectType);
}

ParticleSource::~ParticleSource() {
    Clear();
    texture.Reset();
    particles.clear();
}

const char* ParticleSource::ClassName() { return "CParticle"; }
ClassId ParticleSource::GetClassId() { return ClassId::ParticleSource; }

// The element callbacks that do nothing one per type that stores one (Stench and the spheres,
// ZoneExit, PortalStar, FireStar, Sparks, VisionStar, Regeneration, Silence, FeebleMind, Stun, CurseStars).
void ParticleSource::TickNone(ParticleElement*) {}
