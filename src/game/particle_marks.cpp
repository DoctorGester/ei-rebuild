// Particle effects that mark a unit (stars, clouds, sparks around it): each effect's set-up, emission, update
// and element tick.
// More: docs/notes/effects.md, "Particle sources".
#include "game/particle_common.h"

#include "game/core/core_maths.h"
#include "game/camera.h"
#include "game/core/core_maths.h"
#include "game/effects.h"
#include "game/app/singletons.h"
#include "game/objects/object3d.h"
#include "game/core/random.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/vecmath.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

// --- VisionStar1-4 -----------------------------------------------------------------------------------------

namespace {

// The angle VisionStar's satellites turn by, times their rate: 2 pi (pi + pi, a static initialiser's fold).
constexpr float kSpinAngle = /*007A9BF8*/ 6.2831855f;

// The turn per satellite of the star: the rate for the satellite that follows `count` particles (1-4).
float SpinRate(int count) {
    switch (count) {
    case 1: return 0.2f;
    case 2: return 0.4f;
    case 3: return 0.6f;
    default: return 0.8f;
    }
}

} // namespace

// VisionStar: up to five particles. With lifetime 1 they are a star (frame 1) at the
// source and four satellites half a unit out, at the angles 2 pi times 0.2, 0.4, 0.6 and 0.8 (the
// first at angle 0); with another lifetime (used as the frame) just the one particle.
int EmitVisionStar(ParticleSource& s, Particle& p, int) {
    Vector3& offset = p.velocity;  // a satellite's, from the source
    int count = lib::Count(s.particles);
    if (count > 4) return 0;
    if (count > 0 && s.lifetime != 1) return 0;
    p.color = 0xFFFFFF;
    p.frame = s.lifetime;
    p.pos = s.origin;
    p.size = 0.0f;
    if (s.lifetime != 1) return 1;
    if (count == 0) {
        offset.x = lib::Cos(0.0f);
        offset.y = lib::Sin(0.0f);
        offset.z = 0.0f;
    } else {
        float angle = kSpinAngle * SpinRate(count);
        offset.x = lib::Cos(angle);
        offset.y = lib::Sin(angle);
        offset.z = 0.0f;
    }
    float half = 0.5f;
    offset *= half;
    p.pos += offset;
    return 1;
}

// VisionStar: the particles follow the source; with lifetime 1 their offsets turn by
// 0.07 radians per tick (and they flicker less). VisionStar4 sits 0.7 higher. The size
// eases (0.95) towards a random flicker of the source's size; once the source stops emitting (or
// loses its carrier) the source's size is zeroed and the particles fade, dropped below 0.03.
int UpdateVisionStar(ParticleSource& s, Particle& p) {
    Vector3& offset = p.velocity;
    float oldSize = p.size;
    p.color = 0xFFFFFFFF;
    p.pos = s.origin;
    float flicker = 0.3f;
    if (s.lifetime == 1) {
        float c = lib::Cos(0.07f);
        flicker = 0.17f;
        float sn = lib::Sin(0.07f);
        float turnedY = offset.y * c - offset.x * sn;
        float turnedX = c * offset.x + offset.y * sn;
        offset.y = turnedY;
        offset.x = turnedX;
        p.pos += offset;
    }
    if (ParticleTypeOf(s.type) == ParticleType::VisionStar4) p.pos.z = p.pos.z + 0.7f;
    float eased = g_clientRandom.Range(0.1f, 1.9f) * s.size * flicker * 0.05f + oldSize * 0.95f;
    p.size = eased;
    if ((s.flags & ParticleSourceFlag::Emitting) && s.attachedTo) return 1;
    p.size = eased * 0.95f;
    s.size = 0.0f;
    if (p.size < 0.03f) return 0;
    return 1;
}

// --- Regeneration ------------------------------------------------------------------------------------------

// Regeneration: two particles at the source, the first on the carrier's left (-1), the second on its right (1).
int EmitRegeneration(ParticleSource& s, Particle& p, int) {
    float& sideSign = p.velocity.x;
    if (std::ssize(s.particles) > 1) return 0;
    p.color = 0xFFFFFF;
    p.frame = 3;
    p.pos = s.origin;
    p.size = 0.0f;
    sideSign = static_cast<float>(!s.particles.empty() ? 1 : -1);
    return 1;
}

// Regeneration: once the source no longer emits (or has no carrier) its size shrinks by
// a quarter per tick and the particles go below 0.02. Each particle eases (0.7 / 0.3) towards a
// point beside the carrier: its own x axis (from localRotation) scaled by the carrier's radius, the
// particle's side and 0.65; a little above, by 0.4 of the source's size. Its size flickers at random.
int UpdateRegeneration(ParticleSource& s, Particle& p) {
    const float& sideSign = p.velocity.x;
    if (!(s.flags & ParticleSourceFlag::Emitting) || !s.attachedTo) {
        float shrunk = s.size * 0.75f;
        s.size = shrunk;
        if (shrunk < 0.02f) return 0;
    }
    p.color = 0xFFFFFFFF;
    Vector3 target = s.origin;
    if (s.attachedTo) {
        const Object3D* carrier = s.attachedTo;
        float scale = carrier->bounds.radius * sideSign * 0.65f;
        target += carrier->localRotation.AxisX() * scale;
    }
    p.size = (g_clientRandom.Below(0.4f) + 0.8f) * s.size * 0.25f;
    float keep = 0.7f;
    float pull = 0.3f;
    p.pos.x = p.pos.x * keep + target.x * pull;
    p.pos.y = p.pos.y * keep + target.y * pull;
    p.pos.z = p.pos.z * keep + s.size * 0.4f + target.z * pull;
    return 1;
}

// --- Silence -----------------------------------------------------------------------------------------------

// Silence: the first two particles are orbs at the source (state 1) circling in step (the second starts at pi).
// Further ones are wisps (state 0) somewhere along a random orb's last step, thrown out in a random direction
// (inside the unit ball, normalised) by 0.8 of the orb's size.
int EmitSilence(ParticleSource& s, Particle& p, int) {
    if (std::ssize(s.particles) <= 1) {
        float& angle = p.velocity.x;       // around the source
        float& angleStep = p.velocity.y;
        float& tilt = p.aux.x;             // of the circle
        float& tiltStep = p.aux.y;
        float& orbitRadius = p.aux.z;      // kept while the carrier is gone
        p.color = 0xFFFFFF;
        p.frame = 6;
        p.pos = s.origin;
        p.size = 0.0f;
        float start = !s.particles.empty() ? 0.0f : 3.1416f;    // 0 / pi
        angle = start;
        tilt = start;
        angleStep = 0.1f;
        tiltStep = 0.12f;
        orbitRadius = 1.0f;
        p.state = 1;
        return 1;
    }
    float& wispSize = p.velocity.x;
    auto node = s.particles.begin();
    if (g_clientRandom.Next() & 1) ++node;
    const Particle* orb = *node;
    float t = g_clientRandom.Range(0.0f, 1.0f);
    Vector3 start = orb->pos - t * orb->drift;
    Vector3 v = RandomInBall();
    p.size = 0.0f;
    p.pos = v * orb->size * 0.8f + start;
    wispSize = (g_clientRandom.Below(0.4f) + 0.8f) * s.size * 0.08f;
    p.color = 0xFFFFFFFF;
    p.frame = 6;
    p.state = 0;
    p.life = 7;
    return 1;
}

// Silence: once the source stops emitting (or loses its carrier) the orbs shrink the source by 0.85 per tick,
// everything going below 0.02. An orb circles the source on a tilted circle (radius: half the carrier's), half a
// radius up, keeping its step in drift and flickering in size. A wisp shows its size (shrinking by 0.8) for its life.
int UpdateSilence(ParticleSource& s, Particle& p) {
    if (!(s.flags & ParticleSourceFlag::Emitting) || !s.attachedTo) {
        if (p.state) s.size = s.size * 0.85f;
        if (s.size < 0.02f) return 0;
    }
    if (!p.state) {
        float& wispSize = p.velocity.x;
        if (--p.life <= 0) return 0;
        float shrunk = wispSize * 0.8f;
        p.size = wispSize;
        wispSize = shrunk;
        return 1;
    }
    float& angle = p.velocity.x;
    const float& angleStep = p.velocity.y;
    float& tilt = p.aux.x;
    const float& tiltStep = p.aux.y;
    float& orbitRadius = p.aux.z;
    float radius;
    if (s.attachedTo) {
        radius = s.attachedTo->bounds.radius * 0.5f;
        orbitRadius = radius;
    } else {
        radius = orbitRadius;
    }
    float c = lib::Cos(angle);
    p.color = 0xFFFFFFFF;
    float sn = lib::Sin(angle);
    Vector3 circle = {c, sn, lib::Sin(tilt) * 0.5f};
    Vector3 next = circle * (radius / std::sqrt(LengthSquared(circle))) + s.origin;
    next.z = radius * 0.5f + next.z;
    p.drift = next - p.pos;
    p.pos = next;
    angle = angleStep + angle;
    tilt = tiltStep + tilt;
    p.size = (g_clientRandom.Below(0.2f) + 0.9f) * s.size * 0.22f;
    return 1;
}

// --- FeebleMind --------------------------------------------------------------------------------------------

namespace {

// By life.
constexpr std::int32_t kFeebleAlpha[24] = /*0078ABB8*/ {0, 32, 64, 128, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                               255, 255, 255, 255, 255, 255, 255};

} // namespace

// FeebleMind: size * 2 tries per tick; each particle starts one step out from the
// source in a random horizontal direction.
int EmitFeebleMind(ParticleSource& s, Particle& p, int) {
    Vector3& offset = p.velocity;  // from the circle's centre
    float& growingSize = p.aux.z;
    s.emitCount = std::lround(s.size + s.size);
    p.color = 0x00FFFFFF;
    p.frame = static_cast<int>(g_clientRandom.Next() & 15);
    p.pos = s.origin;
    p.size = 0.0f;
    p.life = 20;
    float twoPi = kPi + kPi;
    offset.z = 0.0f;
    float angle = g_clientRandom.Range(0.0f, twoPi);
    offset.x = lib::Cos(angle) * 0.05f;
    offset.y = lib::Sin(angle) * 0.05f;
    p.pos += offset;
    float grow = (g_clientRandom.Below(0.2f) + 0.9f) * s.size * 0.035f;
    growingSize = grow;
    return 1;
}

// FeebleMind: alpha by the life left (dropped past 0); the particle circles: its offset is taken off, turned by 0.07
// radians and grown by 1.1, then put back with a rise of 0.05. The size grows by 1.04 per tick, the frame cycles
// through 16.
int UpdateFeebleMind(ParticleSource&, Particle& p) {
    Vector3& offset = p.velocity;
    float& growingSize = p.aux.z;
    int life = --p.life;
    if (life < 0) return 0;
    float nextSize = growingSize;
    p.color = (static_cast<std::uint32_t>(kFeebleAlpha[life]) << 24) | 0xFFFFFF;
    int frame = (p.frame + 1) & 15;
    p.pos -= offset;
    float c = lib::Cos(0.07f);
    float sn = lib::Sin(0.07f);
    float turnedY = offset.y * c - offset.x * sn;
    float turnedX = c * offset.x + offset.y * sn;
    float grow = 1.1f;
    offset.x = turnedX * grow;
    offset.y = turnedY * grow;
    p.pos += offset;
    p.size = nextSize;
    p.frame = frame;
    p.pos.z += 0.05f;
    growingSize *= 1.04f;
    return 1;
}

// --- FeetCloud1 / 2: puffs at the carrier's feet -----------------------------------------------------------

namespace {

// By life.
constexpr std::int32_t kFeetAlpha[24] = /*0078AC18*/ {0, 32, 64, 128, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                             255, 255, 255, 255, 255, 255, 255};

const char* kLeftFoot = "ll3";

const char* kRightFoot = "rl3";

const char* kBody = "bd";

} // namespace

// FeetCloud1 (blue) / FeetCloud2 (red): one element per foot (ticks nonzero: the left), not placed yet.
void SetupFeetCloud(ParticleSource& s) {
    for (int i = 0; i < 2; ++i) {
        s.elements[i].foot.countdown = -1;
        s.elements[i].ticks = i;
    }
}

// FeetCloud: size * 8 tries per tick; a puff at one of the two feet (the element's position once it is
// tracked, else the source), drifting in a random direction inside the unit disc, rising.
int EmitFeetCloud(ParticleSource& s, Particle& p, int) {
    float& puffSize = p.velocity.x;
    float& ease = p.velocity.y;    // the part of the way to the target per tick
    Vector3& offset = p.aux;       // from the foot
    Vector3& spread = p.drift;     // added to the offset per tick
    s.emitCount = std::lround(s.size * 8.0f);
    p.color = 0x00FFFFFF;
    p.frame = static_cast<int>(g_clientRandom.Next() & 15);
    int foot = static_cast<int>(g_clientRandom.Next() & 1);
    p.state = foot;
    const ParticleElement& e = s.elements[foot];
    if (e.foot.countdown >= 0) p.pos = e.pos;
    else p.pos = s.origin;
    p.size = 0.0f;
    float x, y;
    for (int tries = 10;;) {
        x = g_clientRandom.Range(-1.0f, 1.0f);
        y = g_clientRandom.Range(-1.0f, 1.0f);
        if (LengthSquared2D(y, x) < 1.0f) break;   // integer-sourced: never NaN
        if (!(--tries > 0)) break;
    }
    float scale = e.foot.radius;
    spread = Vector3{x, y, 0.0f} * 0.1f * scale * s.size;
    offset = {};
    ease = 1.0f;
    p.life = 10;
    puffSize = s.size * e.foot.radius * 0.4f;
    p.pos.z = puffSize * 1.2f + p.pos.z;
    return 1;
}

// FeetCloud puffs: alpha by the life left (capped at 8 once the source stops emitting or loses its carrier;
// dropped past 0), the source's tint as the colour. The puff's offset grows by its spread (slowing by 0.9) and the
// puff eases (part of the way, which shrinks by 0.98) towards its foot (element `state`) plus the offset; its size
// shrinks by 0.8.
int UpdateFeetCloud(ParticleSource& s, Particle& p) {
    float& puffSize = p.velocity.x;
    float& ease = p.velocity.y;    // the part of the way to the target per tick
    Vector3& offset = p.aux;       // from the foot
    Vector3& spread = p.drift;     // added to the offset per tick
    int life = --p.life;
    if (life < 0) return 0;
    if (!(s.flags & ParticleSourceFlag::Emitting) || !s.attachedTo) {
        if (life > 8) life = 8;
        p.life = life;
    }
    p.color = (static_cast<std::uint32_t>(kFeetAlpha[life]) << 24) + s.tint;
    offset += spread;
    float pull = ease;
    Vector3 toward = (offset + s.elements[p.state].pos) * pull;
    p.pos = p.pos * (1.0f - pull) + toward;
    p.size = puffSize;
    puffSize *= 0.8f;
    ease *= 0.98f;
    float slow = 0.9f;
    spread *= slow;
    return 1;
}

// FeetCloud feet: on a unit carrier the element sits on its foot (left when ticks is set, else
// right), else on its body, else on its position raised by `above.z`; without one on the source.
void TickFeetCloud(ParticleSource& s, ParticleElement& e) {
    if (s.attachedTo && s.attachedTo->GetClassId() == ClassId::Unit) {
        Object3D* unit = s.attachedTo;
        FigureMesh* part = unit->FindPartByPrefix(e.ticks ? kLeftFoot : kRightFoot);
        if (!part) part = unit->FindPartByPrefix(kBody);
        if (part) {
            e.pos = unit->PartWorldPosition(part);
        } else {
            const Vector3& place = unit->localPosition;
            float raise = unit->bounds.boxMax.z;
            e.pos.x = place.x;
            e.pos.y = place.y;
            e.pos.z = place.z + raise;
        }
    } else {
        e.pos = s.origin;
    }
    e.foot.radius = s.AttachmentOffset().radius;
    e.foot.countdown = 0;
}

// --- Stun --------------------------------------------------------------------------------------------------

// Stun: a star at a random point of the ball of 0.8 times the carrier figure's radius around the source, for
// 20 ticks, with a random size factor. A running unit carrier gets four stars a tick living 5
// ticks, else one; once the carrier is gone (nothing emitted) or it is dead (IsDead) the source stops and lets
// it go.
int EmitStun(ParticleSource& s, Particle& p, int) {
    float& sizeFactor = p.velocity.x;
    auto [offset, radius] = s.AttachmentOffset();
    p.color = 0xFFFFFF;
    p.frame = 3;
    Vector3 v = RandomInBall();
    p.size = 0.0f;
    p.pos = v * (radius * 0.8f) + s.origin;
    p.life = 20;
    sizeFactor = g_clientRandom.Below(0.6f) + 0.7f;
    Unit* unit = UnitOrNull(s.attachedTo);   // stun rides units (the game reads any carrier as one)
    if (!unit) {
        s.Stop();
        return 0;
    }
    if (unit->moveMode == MoveMode::Run) {
        s.emitCount = 4;
        p.life = 5;
    } else {
        s.emitCount = 1;
    }
    if (unit->IsDead()) {
        s.Stop();
    }
    return 1;
}

// Stun stars: dropped past their life; else flickering at a random size of the source's times the star's factor.
// Without a carrier the source stops and is let go.
int UpdateStun(ParticleSource& s, Particle& p) {
    const float& sizeFactor = p.velocity.x;
    if (--p.life < 0) return 0;
    p.color = 0xFFFFFFFF;
    p.size = (g_clientRandom.Below(0.4f) + 0.8f) * s.size * sizeFactor;
    if (!s.attachedTo) {
        s.Stop();
    }
    return 1;
}

// --- CurseStars --------------------------------------------------------------------------------------------

namespace {

// By life.
constexpr std::int32_t kCurseAlpha[24] = /*0078AC98*/ {0, 32, 64, 128, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                              255, 255, 255, 255, 255, 255, 255};

} // namespace

// CurseStars: a star at a random point of the ball of the source's size around the
// source, drifting outwards (a tenth of the size per tick), for 20 ticks.
int EmitCurseStars(ParticleSource& s, Particle& p, int) {
    p.color = 0xFFFFFF;
    p.frame = static_cast<int>(g_clientRandom.Next() & 15);
    Vector3 a = RandomInBall() * s.size;
    p.size = 0.0f;
    p.pos = a + s.origin;
    float length = std::sqrt(LengthSquared(a));
    p.life = 20;
    if (length > 0.0f) {
        p.drift = a * (s.size * 0.1f) / length;
    } else {
        p.drift = {};
    }
    return 1;
}

// CurseStars: alpha by the life left (dropped past 0); a star's size starts
// as a random part of the source's (0.04 of it, flickering) and shrinks by 0.95; it drifts (slowing
// by 0.9). Without a carrier the source stops emitting and is let go (Attach(nullptr)).
int UpdateCurseStars(ParticleSource& s, Particle& p) {
    int life = --p.life;
    if (life < 0) return 0;
    p.color = (static_cast<std::uint32_t>(kCurseAlpha[life]) << 24) | 0xFFFFFF;
    if (p.size == 0.0f) {
        p.size = (g_clientRandom.Below(0.4f) + 0.8f) * s.size * 0.04f;
    } else {
        p.size *= 0.95f;
    }
    p.pos += p.drift;
    float slow = 0.9f;
    p.drift *= slow;
    if (!s.attachedTo) {
        s.Stop();
    }
    return 1;
}

// --- StartTrans --------------------------------------------------------------------------------------------

namespace {

constexpr std::int32_t kTransAlpha[16] = /*0078ACF8*/ {0, 80, 176, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 128, 64};

} // namespace

// StartTrans: the element counts down 30 ticks; one particle a tick to begin with.
void SetupStartTrans(ParticleSource& s) {
    s.elements[0].ticks = 30;
    s.elements[0].trans.count = 1.0f;
}

// StartTrans: the first particle is the glow (state 1, frame 12, 4 ticks); the others
// (15 ticks) start in a random direction at the source's size from it and come back in 18 ticks.
int EmitStartTrans(ParticleSource& s, Particle& p, int) {
    if (s.particles.empty()) {
        p.life = 4;
        p.state = 1;
        p.frame = 12;
        p.pos = s.origin;
    } else {
        p.life = 15;
        p.state = 0;
        p.frame = static_cast<int>(g_clientRandom.Next() % 12);
        Vector3& offset = p.velocity;  // the start, from the source
        float& sparkSize = p.aux.x;
        float lengthSquared;
        Vector3 v = RandomInBall(lengthSquared);
        offset = v / std::sqrt(lengthSquared) * s.size;
        p.pos = offset + s.origin;
        p.drift = -offset / static_cast<float>(p.life + 3);
        sparkSize = s.size * 0.05f;
    }
    p.color = 0xFFFFFF;
    p.size = 0.0f;
    return 1;
}

// StartTrans: the glow flickers at trans.scale while carried, then fades (by the life table, size times 0.6); the
// others come in, cycling four frames at their size, fading out over their last ticks (capped at 4 once the carrier
// is gone).
int UpdateStartTrans(ParticleSource& s, Particle& p) {
    if (p.state != 0) {
        if (!s.attachedTo) {
            int life = --p.life;
            if (life < 0) return 0;
            p.color = (static_cast<std::uint32_t>(kTransAlpha[life]) << 24) | 0xFFFFFF;
            p.size *= 0.6f;
            return 1;
        }
        p.color = 0xFFFFFFFF;
        p.size = (g_clientRandom.Below(0.4f) + 0.8f) * s.elements[0].trans.scale;
        return 1;
    }
    int life = --p.life;
    if (life < 0) return 0;
    if (!s.attachedTo) {
        if (life >= 4) life = 4;
        p.life = life;
    }
    const float& sparkSize = p.aux.x;
    p.frame = (p.frame & ~3) | ((p.frame + 1) & 3);
    p.pos += p.drift;
    p.size = sparkSize;
    p.color = (static_cast<std::uint32_t>(kTransAlpha[p.life]) << 24) | 0xFFFFFF;
    return 1;
}

// StartTrans: once the element's countdown runs out the source stops (Emitting and KeepAlive off) and detaches; the
// glow widens, and the element's trans.count grows by 0.4 a tick and sets the tries per tick (at most 8).
void TickStartTrans(ParticleSource& s, ParticleElement& e) {
    int ticks = e.ticks--;
    if (ticks < 0) {
        s.Stop();
    }
    e.trans.scale = s.size * 0.02f + e.trans.scale;
    e.trans.count += 0.4f;
    int count = std::lround(e.trans.count);
    s.emitCount = count;
    if (!(count < 8)) s.emitCount = 8;
}

// --- Sparks ------------------------------------------------------------------------------------------------

namespace {

// Alpha tables indexed by the ticks left.
constexpr std::int32_t kSparkAlpha[8] = /*0078AB98*/ {0, 64, 128, 255, 255, 255, 255, 255};

} // namespace

// Sparks: the first particle marks the source (state 1); after it the source stops
// emitting and each further one is a spark flying in a random direction (a random point of the
// unit ball, normalised) with a random speed (squared) and a random part of its first step done.
int EmitSparks(ParticleSource& s, Particle& p, int) {
    if (s.particles.empty()) {
        p.color = 0xFFFFFFFF;
        p.pos = s.origin;
        p.size = 0.0f;
        p.frame = 0;
        p.state = 1;
        return 1;
    }
    s.flags &= ~ParticleSourceFlag::Emitting;
    p.color = 0xFFFFFFFF;
    float speed = g_clientRandom.Below(0.6f) + 0.8f;
    float lengthSquared;
    Vector3 v = RandomInBall(lengthSquared);
    Vector3 direction = v / std::sqrt(lengthSquared);
    float k = speed * s.size * speed * 0.2f;
    p.drift = direction * k;
    float t = g_clientRandom.Range(0.0f, 1.0f);
    p.pos = t * p.drift + s.origin;
    p.size = (g_clientRandom.Below(0.4f) + 0.8f) * s.size * 0.2f;
    p.life = 6;
    p.state = 0;
    p.frame = static_cast<int>(g_clientRandom.Next() % 3) + 1;
    return 1;
}

// Sparks: the spare ones (state set) get a random size once and are dropped after; the
// rest fly along their drift, shrinking and slowing, with the alpha from kSparkAlpha.
int UpdateSparks(ParticleSource& s, Particle& p) {
    if (p.state != 0) {
        if (p.size != 0.0f) return 0;
        p.size = (g_clientRandom.Below(0.4f) + 0.8f) * s.size * 0.55f;
        return 1;
    }
    if (--p.life < 0) return 0;
    p.color = (static_cast<std::uint32_t>(kSparkAlpha[p.life]) << 24) | 0xFFFFFF;
    p.pos += p.drift;
    p.size *= 0.9f;
    p.drift *= 0.6f;
    ++p.frame;
    return 1;
}

// --- FireStar / AcidStar -----------------------------------------------------------------------------------

// FireStar: a single flash at the source (the source stops emitting at once) with the source's lifetime as its
// frame.
int EmitFireStar(ParticleSource& s, Particle& p, int) {
    float& flashSize = p.velocity.x;
    s.flags &= ~ParticleSourceFlag::Emitting;
    p.color = 0xFFFFFF;
    p.life = 3;
    p.frame = s.lifetime;
    p.pos = s.origin;
    p.size = 0.0f;
    flashSize = s.size;
    return 1;
}

// FireStar: shown for two ticks at the kept size (shrinking it by 0.9 each time), then dropped.
int UpdateFireStar(ParticleSource&, Particle& p) {
    float& flashSize = p.velocity.x;
    if (--p.life <= 0) return 0;
    float shrunk = flashSize * 0.9f;
    p.color = 0xFFFFFFFF;
    p.size = flashSize;
    flashSize = shrunk;
    return 1;
}

// --- Shower1-4: sparks thrown out in a few random directions -----------------------------------------------

namespace {

constexpr std::int32_t kShowerAlpha[16] = /*0078A6B4*/ {0, 64, 128, 192, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};

// alpha * 8 random unit directions (at least one) in the elements.
void ChooseShowerRays(ParticleSource& s) {
    s.rayCount = std::lround(s.alpha * 8.0f);
    if (s.rayCount < 1) s.rayCount = 1;
    int rays = s.rayCount;
    s.emitCount = rays * 10;
    s.elements.resize(rays);
    for (int i = 0; i < s.rayCount; ++i) {
        float lengthSquared;
        Vector3 v = RandomInBall(lengthSquared);
        s.elements[i].pos = v / std::sqrt(lengthSquared);
        s.elements[i].ray.age = 0;
    }
}

} // namespace

// Shower1-4 (the RainSnow texture): the element's ticks pick the frame; its age waits for the directions.
void SetupShower(ParticleSource& s) {
    s.elements[0].ray.age = -100;
    switch (ParticleTypeOf(s.type)) {
    case ParticleType::Shower1: s.elements[0].ticks = 12; break;
    case ParticleType::Shower2: s.elements[0].ticks = 13; break;
    case ParticleType::Shower3: s.elements[0].ticks = 14; break;
    case ParticleType::Shower4: s.elements[0].ticks = 15; break;
    default: break;
    }
}

// Shower: the first call chooses the directions. A spark leaves the source along a
// random direction (jittered by +-0.03, scaled to the source's size and 1/6 of it per tick, one in
// eight half as fast again), already a random part of its first step out.
int EmitShower(ParticleSource& s, Particle& p, int) {
    float& speedKept = p.velocity.x;   // the speed's factor per tick
    float& sizeKept = p.velocity.y;    // the size's factor per tick
    if (s.elements[0].ray.age < 0) ChooseShowerRays(s);
    p.color = 0xFFFFFFFF;
    p.life = s.lifetime;
    p.frame = s.elements[0].ticks;
    Vector3 jitter = {g_clientRandom.Range(-0.03f, 0.03f), g_clientRandom.Range(-0.03f, 0.03f),
                      g_clientRandom.Range(-0.03f, 0.03f)};
    const ParticleElement& ray = s.elements[g_clientRandom.Next() % static_cast<std::uint32_t>(s.rayCount)];
    p.pos = s.origin;
    p.size = s.typeParam;
    Vector3 d = s.size * (jitter + ray.pos);
    float speed = 1.0f / std::sqrt(LengthSquared(d)) * s.size * 0.16666667f;   // 1/6
    if ((g_clientRandom.Next() & 7) == 0) speed = speed * 1.5f;
    p.drift = speed * d;
    speedKept = 0.9f;
    sizeKept = 0.8f;
    float t = g_clientRandom.Range(0.0f, 1.0f);
    p.pos += t * p.drift;
    return 1;
}

// Shower sparks move, slow down, fall by 0.05 size per tick, shrink and fade by the life table.
int UpdateShower(ParticleSource& s, Particle& p) {
    const float& speedKept = p.velocity.x;
    const float& sizeKept = p.velocity.y;
    int life = --p.life;
    if (life == 0) return 0;
    p.pos += p.drift;
    p.color = (static_cast<std::uint32_t>(ByLife(kShowerAlpha, life, 0)) << 24) | 0xFFFFFF;   // before: kAcidSize's zero tail
    p.size = sizeKept * p.size;
    float keep = speedKept;
    p.drift *= keep;
    p.drift.z -= s.size * 0.05f;
    return 1;
}

// Shower directions age; after 5 ticks the source stops emitting and lets go of its carrier.
void TickShower(ParticleSource& s, ParticleElement& e) {
    if (++e.ray.age > 5) {
        s.flags &= ~ParticleSourceFlag::Emitting;
        s.Attach(nullptr);
    }
}

// --- Falling1-4: leaves or flakes drifting down from the carrier -------------------------------------------

// Falling1-4 (leaves and the like): four to eight gusts.
void SetupFalling(ParticleSource& s) {
    s.elements.resize(static_cast<int>(g_clientRandom.Next() % 5 + 4));
}

// Falling: a leaf at the source blown by a random gust (a random reach up to the gust's, a random direction
// around it (the original's spread is ftol(10 degrees) = 0)), falling and spinning at random rates.
int EmitFalling(ParticleSource& s, Particle& p, int) {
    float& sizeFactor = p.velocity.x;  // per tick
    float& terminal = p.velocity.y;    // the lowest drift.z
    float& gravity = p.velocity.z;     // added to drift.z per tick
    p.color = 0xFFFFFFFF;
    p.frame = 12;
    p.pos = s.origin;
    p.size = g_clientRandom.Range(0.1f, 0.2f);
    const ParticleElement& gust = s.elements[g_clientRandom.Next() % static_cast<std::uint32_t>(std::ssize(s.elements))];
    float reach = gust.wind.reach * 0.5f;
    int spread = static_cast<int>(ei::kRadiansPerDegree * 10.0f);
    float turn = spread * gust.wind.direction;
    float lowReach = -reach;
    float x = g_clientRandom.Range(0.0f, reach - lowReach) + gust.wind.reach + lowReach;
    float lowTurn = -turn;
    float angle = g_clientRandom.Range(0.0f, turn - lowTurn) + gust.wind.direction + lowTurn;
    sizeFactor = 1.0f;
    gravity = -0.12f;
    p.drift.x = lib::Sin(angle) * x;
    p.drift.y = lib::Cos(angle) * x;
    p.drift.z = 0.0f;
    gravity = g_clientRandom.Range(0.0f, 0.12f) + gravity - 0.06f;
    terminal = g_clientRandom.Below(0.4f) - 0.6f;
    return 1;
}

// Falling drops: move by the drift, scale the size, accelerate down to the terminal speed (drift.z = max(drift.z +
// gravity, terminal)); dropped once below z = 0.
int UpdateFalling(ParticleSource&, Particle& p) {
    const float& sizeFactor = p.velocity.x;
    const float& terminal = p.velocity.y;
    const float& gravity = p.velocity.z;
    if (p.pos.z < 0.0f) return 0;
    p.pos += p.drift;
    p.size = sizeFactor * p.size;
    float fall = gravity + p.drift.z;
    p.drift.z = MaxKeepB(fall, terminal);
    return 1;
}

// Falling gusts: a new gust gets a random reach and direction (truncated to whole
// radians); from the second tick on the source stops emitting and lets go of its carrier.
void TickFalling(ParticleSource& s, ParticleElement& e) {
    int age = e.wind.age;
    if (age == 0) {
        e.wind.reach = g_clientRandom.Below(0.2f) + 0.1f;
        float whole = static_cast<float>(static_cast<int>(ei::kRadiansPerDegree * 360.0f));
        e.wind.direction = g_clientRandom.Range(0.0f, whole);
        age = e.wind.age;
    }
    if (age > 0) {
        s.flags &= ~ParticleSourceFlag::Emitting;
        s.Attach(nullptr);
    }
    ++e.wind.age;
}

// --- Bag: eighty coloured balls bouncing around the carrier ------------------------------------------------

namespace {

// A ball's size factor by its age: 0.93 to the power of the age.
constexpr std::array<float, 12> kBagSizes = [] {
    std::array<float, 12> sizes = {};
    float t = 1.0f;
    for (float& s : sizes) {
        s = t;
        t *= 0.93f;
    }
    return sizes;
}();

// A ball's r, g, b by its age.
constexpr std::int32_t kBagColours[36] = {
    251, 254, 143, 252, 227, 149, 252, 201, 154, 255, 176, 154, 255, 155, 163, 252, 152, 176,
    250, 150, 195, 247, 149, 212, 243, 147, 237, 239, 144, 249, 239, 144, 249, 239, 144, 249,
};

// A spark's alpha by its life; a ball's by its life up to 9.
constexpr std::int32_t kBagAlpha[16] = {0, 64, 112, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};

constexpr int kBagGone = -13;

std::uint32_t BagRgb(int kind) {
    const std::int32_t* c = kBagColours + kind * 3;
    return ((static_cast<std::uint32_t>(c[0]) << 8 | static_cast<std::uint32_t>(c[1])) << 8) | static_cast<std::uint32_t>(c[2]);
}

// A random value of the throw box (-0.2 * size to 0.2 * size).
float BagSpread(const ParticleSource& s, float& low, float& high) {
    high = s.size * 0.2f;
    low = s.size * -0.2f;
    return g_clientRandom.Range(0.0f, high - low);
}

} // namespace

// A ball's ticks run down to its next throw (-13: no carrier, the ball is gone).

// Bag: the first kBagBalls particles are the balls (state = their index); later ones are sparks along a random
// ball's last step, in the ball's colour.
int EmitBag(ParticleSource& s, Particle& p, int) {
    if (std::ssize(s.particles) < kBagBalls) {
        std::uint32_t r = g_clientRandom.Next();
        p.state = lib::Count(s.particles);
        p.color = 0xFFFFFF;
        p.life = 15;
        p.frame = static_cast<int>(r & 3);
        p.pos = s.origin;
        p.size = 0.0f;
        return 1;
    }
    const ParticleElement& ball = s.elements[g_clientRandom.Next() % kBagBalls];
    int age = ball.ball.age;
    p.color = BagRgb(age);
    int life = ball.ticks + 1;
    if (life > 5) life = 5;
    p.life = life;
    p.frame = 4;
    p.state = -1;
    float u = g_clientRandom.Range(-1.0f, 0.0f);
    p.pos = u * ball.ball.step + ball.pos;
    p.size = kBagSizes[age] * s.size * 0.1f;
    return 1;
}

// Bag: a spark fades by its life and steps a frame; a ball follows its element (dropped
// once the ball is gone), cycles 4 frames, takes the colour and size of its age, and while the
// ball rests (no last step) its previous position is pinned to the current one.
int UpdateBag(ParticleSource& s, Particle& p) {
    int state = p.state;
    if (state < 0) {
        int life = --p.life;
        if (life <= 0) return 0;
        ++p.frame;
        p.color = (static_cast<std::uint32_t>(kBagAlpha[life]) << 24) + (p.color & 0xFFFFFF);
        return 1;
    }
    ParticleElement* ball = &s.elements[state];
    if (ball->ticks == kBagGone) return 0;
    p.frame = (p.frame + 1) & 3;
    p.pos = ball->pos;
    int age = ball->ball.age;
    p.size = kBagSizes[age] * s.size * 0.15f;
    int fade = p.life > 9 ? 9 : p.life;
    p.color = (static_cast<std::uint32_t>(kBagAlpha[fade]) << 24) | BagRgb(age);
    const Vector3& step = ball->ball.step;
    if (step.x == 0.0f && step.y == 0.0f && step.z == 0.0f) {
        p.prevPos = p.pos;
        p.prevSize = p.size;
        p.prevColor = p.color;
        p.size = s.size * 0.15f;
    }
    return 1;
}

// Bag balls: when a ball's countdown runs out it is thrown again from the source (a random kind 0-2, a random
// offset, a random velocity of 0.2 size) and rests 7-9 ticks; while flying it ages, now and then gets a
// random kick, and moves (its step is kept). Without a carrier the ball is gone.
void TickBag(ParticleSource& s, ParticleElement& e) {
    if (--e.ticks < 0) {
        if (!s.attachedTo) {
            e.ticks = kBagGone;
            return;
        }
        e.ball.age = static_cast<int>(g_clientRandom.Next() % 3);
        e.pos = s.origin;
        float low, high;
        float r = BagSpread(s, low, high);
        e.pos.x = r + e.pos.x + low;
        r = BagSpread(s, low, high);
        e.pos.y = r + low + e.pos.y;
        r = BagSpread(s, low, high);
        e.pos.z = r + e.pos.z + low;
        e.ball.velocity = RandomInBall() * s.size * 0.2f;
        e.ticks = static_cast<int>(g_clientRandom.Next() % 3) + 7;
        e.ball.step = {};
        return;
    }
    ++e.ball.age;
    if (g_clientRandom.Next() % 6 == 0) {
        e.ball.velocity += RandomInBall() * s.size * 0.05f;
    }
    const Vector3& velocity = e.ball.velocity;
    e.pos += velocity;
    e.ball.step = velocity;
}
