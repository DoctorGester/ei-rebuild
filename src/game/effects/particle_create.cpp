// ParticleSource set-up: the per-type spec table, Create, Reset and Clear.
// More: docs/notes/effects.md, "Particle sources".
#include "game/core/core_transform.h"
#include "game/effects/effects.h"
#include "game/app/singletons.h"
#include "game/objects/object3d.h"
#include "game/effects/particle_common.h"
#include "game/effects/particle_data.h"
#include "game/models/texture.h"
#include "game/world/world.h"

#include "lib/containers.h"
#include "lib/text_case.h"
#include "platform/log.h"

#include <array>
#include <cstdint>
#include <iterator>
#include <set>

namespace {

constexpr float kFrameInset = 0.001953125f;   // the frame rectangles are inset by 1/512

// The rotation Reset gives a source: the identity (a static initialiser's stores).
constexpr Quaternion kIdentityRotation = /*007A9C30*/ {1.0f, 0.0f, 0.0f, 0.0f};

// A source type's parameters: Create starts every source from the defaults here (Reset's, with Create's own
// emission chance, particle limit and 16 frames) and takes its type's entry.
struct ParticleSpec {
    ParticleType type = ParticleType::FireBall;
    const char* texture = nullptr;          // kind 6 (TextureKind::Effects), by name
    ParticleSourceFlag flags = {};          // beside Emitting
    AttachPoint attachPoint = AttachPoint::EffectPoint;
    bool additiveBlend = false;
    int emitChance = 100;
    int emitCount = 0;
    int maxParticles = 500;
    int lifetime = -1;
    int frameCount = 16;
    int frameDelay = 10000;
    float emitSpacing = 1000.0f;
    float size = 1.0f;
    float height = 0.0f;
    float growth = 0.0f;
    float alpha = 1.0f;
    float typeParam = 1.0f;
    std::uint32_t tint = 0;
    int elementCount = 0;                   // made zeroed; the type's Setup fills them
    Vector3 spreadMin;
    Vector3 spreadMax;
    Vector3 extent;
};

constexpr ParticleSourceFlag kCarried = ParticleSourceFlag::Emitting | ParticleSourceFlag::AttachedOnly;
constexpr ParticleSourceFlag kOnPart = ParticleSourceFlag::AttachedOnly | ParticleSourceFlag::OnPart;
constexpr ParticleSourceFlag kCarriedOnPart = kCarried | ParticleSourceFlag::OnPart;
constexpr ParticleSourceFlag kPausedToo = ParticleSourceFlag::Emitting | ParticleSourceFlag::TicksWhilePaused;
constexpr int kEndless = 0x7FFFFFFF;    // an emission or particle count without a limit

using enum ParticleType;

constexpr ParticleSpec kPoison = {.texture = "PoisonCloud", .emitChance = 50, .emitCount = 1,
                                  .maxParticles = kEndless, .lifetime = 0xCA4, .frameCount = 15, .frameDelay = 1};
constexpr ParticleSpec kMushroom = {.texture = "ChangeHero", .emitCount = 10, .maxParticles = 4000, .frameDelay = 1,
                                    .height = 1.0f};
constexpr ParticleSpec kShower = {.texture = "RainSnow", .flags = kOnPart, .attachPoint = AttachPoint::Body,
                                  .emitCount = 10, .maxParticles = 300, .lifetime = 6, .frameDelay = 15,
                                  .elementCount = 1, .spreadMin = {-0.2f, -0.2f, -0.2f},
                                  .spreadMax = {0.2f, 0.2f, 0.2f}};
constexpr ParticleSpec kModifier = {.texture = "Stun", .flags = kCarried, .emitCount = 80, .lifetime = 14,
                                    .frameDelay = 2};
constexpr ParticleSpec kCasting = {.texture = "FireSmoke", .flags = kCarriedOnPart, .emitCount = 20, .lifetime = 5,
                                   .frameDelay = 5};
constexpr ParticleSpec kCastSwirl = {.texture = "Casting2", .flags = kCarried, .emitCount = 10, .maxParticles = 600,
                                     .lifetime = 2, .frameDelay = 1, .spreadMin = {-0.04f, -0.04f, -0.04f},
                                     .spreadMax = {0.04f, 0.04f, 0.04f}};
constexpr ParticleSpec kCylinder = {.texture = "Casting2", .flags = kCarried, .emitCount = 30, .maxParticles = 600,
                                    .frameDelay = 1, .elementCount = 2};
constexpr ParticleSpec kGroundDust = {.texture = "ShapeChange", .flags = kPausedToo, .emitCount = 128,
                                      .maxParticles = 128, .elementCount = 1};
constexpr ParticleSpec kMark = {.texture = "Stun", .flags = kCarriedOnPart, .attachPoint = AttachPoint::Body,
                                .additiveBlend = true, .emitCount = 1, .maxParticles = 300};

// The entry of `base` for `type`, with `change` applied to it.
template <class Change>
constexpr ParticleSpec Like(const ParticleSpec& base, ParticleType type, Change change) {
    ParticleSpec spec = base;
    spec.type = type;
    change(spec);
    return spec;
}

constexpr ParticleSpec Like(const ParticleSpec& base, ParticleType type) {
    return Like(base, type, [](ParticleSpec&) {});
}

// One entry per type still in code, in the order of ParticleType; the others are data (assets/effects,
// ParticleSource::Create).
constexpr ParticleSpec kSpecs[] = {
    {.type = FireBlast, .texture = "Explosion", .emitCount = 60, .maxParticles = 600, .lifetime = 15,
     .frameDelay = 1, .size = 0.6f, .height = 1.0f, .growth = 0.8f, .elementCount = 15,
     .spreadMin = {-0.02f, -0.02f, -0.02f}, .spreadMax = {0.02f, 0.02f, 0.02f}},
    {.type = Healing, .texture = "Healing", .flags = ParticleSourceFlag::AttachedOnly, .additiveBlend = true,
     .emitCount = kEndless, .lifetime = 8, .frameDelay = 1, .size = 0.5f},
    Like(kPoison, PoisonFog),
    Like(kPoison, PoisonCloud),
    {.type = Geyser, .texture = "Fireworks", .additiveBlend = true, .emitChance = 88, .emitCount = 20,
     .frameDelay = 100, .height = 0.3f, .elementCount = 3, .spreadMin = {-0.1f, -0.1f, -0.02f},
     .spreadMax = {0.1f, 0.1f, 0.02f}},
    {.type = Tornado, .texture = "Tornado", .emitCount = 4, .maxParticles = 800, .frameDelay = 1, .height = 0.2f,
     .elementCount = 6},
    {.type = Casting, .texture = "FireSmoke", .flags = kCarried, .emitCount = kEndless, .frameDelay = 1},
    Like(kMushroom, Mushroom),
    Like(kShower, Shower1),
    {.type = Link, .texture = "Link", .emitChance = 80, .emitCount = 10, .lifetime = 45, .frameCount = 15,
     .frameDelay = 3, .spreadMin = {-0.2f, -0.2f, -0.2f}, .spreadMax = {0.2f, 0.2f, 0.2f},
     .extent = {-10.0f, 0.0f, 0.0f}},
    {.type = ClayRing, .texture = "ClayRing", .additiveBlend = true, .emitCount = 80, .lifetime = 15,
     .frameDelay = 2, .elementCount = 10},
    Like(kModifier, Modifier1),
    Like(kModifier, Modifier2),
    Like(kModifier, Modifier3),
    Like(kModifier, Modifier4),
    Like(kModifier, Modifier5),
    Like(kModifier, Modifier6),
    Like(kModifier, Modifier7),
    Like(kModifier, Modifier8),
    Like(kModifier, Modifier9),
    Like(kModifier, Modifier10),
    Like(kModifier, Modifier11),
    Like(kModifier, Modifier12),
    Like(kCasting, CastingFire),
    Like(kCasting, CastingElectricity, [](ParticleSpec& spec) {
        spec.texture = "Casting8";
        spec.frameDelay = 1;
    }),
    Like(kCasting, CastingAcid, [](ParticleSpec& spec) { spec.texture = "Casting7"; }),
    Like(kCasting, CastingDivination, [](ParticleSpec& spec) {
        spec.texture = "Casting4";
        spec.attachPoint = AttachPoint::AboveHead;
        spec.additiveBlend = true;
        spec.emitCount = 6;
        spec.frameDelay = 1;
    }),
    Like(kCastSwirl, CastingIllusion),
    Like(kCastSwirl, CastingDomination, [](ParticleSpec& spec) {
        spec.texture = "Casting3";
        spec.lifetime = 1;
    }),
    Like(kCastSwirl, CastingEnchantment, [](ParticleSpec& spec) { spec.texture = "Casting6"; }),
    Like(kCastSwirl, CastingHealing, [](ParticleSpec& spec) {
        spec.texture = "Casting1";
        spec.lifetime = 1;
    }),
    {.type = LightningBlast, .texture = "TendriLight", .additiveBlend = true, .emitChance = 88, .emitCount = 250,
     .maxParticles = 600, .lifetime = 15, .frameCount = 15, .frameDelay = 1, .spreadMin = {-0.2f, -0.2f, -0.2f},
     .spreadMax = {0.2f, 0.2f, 0.2f}},
    Like(kShower, Shower2),
    Like(kShower, Shower3),
    Like(kShower, Shower4),
    {.type = ShapeChange, .texture = "ShapeChange", .flags = kPausedToo, .emitCount = 2500, .maxParticles = 2500},
    Like(kGroundDust, GroundDust1),
    Like(kGroundDust, GroundDust2, [](ParticleSpec& spec) {
        spec.emitCount = 256;
        spec.maxParticles = 256;
    }),
    {.type = Bag, .texture = "Bag", .flags = kCarried, .emitCount = 100, .maxParticles = 800,
     .elementCount = kBagBalls},
    Like(kCylinder, Cylinder1),
    Like(kCylinder, Cylinder2, [](ParticleSpec& spec) { spec.texture = "Casting1"; }),
    Like(kMark, Silence, [](ParticleSpec& spec) { spec.emitCount = 4; }),
    {.type = CurseHolder, .texture = "Tornado", .flags = kOnPart, .attachPoint = AttachPoint::Body, .emitCount = 4,
     .maxParticles = 800, .height = 0.2f, .tint = 0x19000000, .elementCount = 6},
    Like(kMushroom, Transform),
};

constexpr int kTypeCount = static_cast<int>(Transform) - static_cast<int>(FireBall) + 1;

constexpr bool SpecsInTypeOrder() {
    for (int i = 1; i < std::ssize(kSpecs); ++i) {
        if (kSpecs[i].type <= kSpecs[i - 1].type) return false;
    }
    return true;
}
static_assert(SpecsInTypeOrder());

// Each type's entry in kSpecs, -1 for a type on data.
constexpr std::array<int, kTypeCount> kSpecIndex = [] {
    std::array<int, kTypeCount> index{};
    index.fill(-1);
    for (int i = 0; i < std::ssize(kSpecs); ++i)
        index[static_cast<int>(kSpecs[i].type) - static_cast<int>(FireBall)] = i;
    return index;
}();

// The types' names, in the order of ParticleType from FireBall.
constexpr std::string_view kTypeNames[] = {
    "FireBall", "CampFire", "FireBlast", "Fire", "Smoke", "VulcanSmoke", "Healing", "PoisonFog", "PoisonCloud",
    "Geyser", "Tornado", "Casting", "Nuke", "Banshee", "Mushroom", "Shower1", "Tendril", "FireArrow", "AcidRay",
    "BlueGas", "Link", "SphereAcid", "SphereElectricity", "SphereFire", "ClayRing", "Teleport", "Antimagic",
    "Modifier1", "Modifier2", "Modifier3", "Modifier4", "Modifier5", "Modifier6", "Modifier7", "Modifier8",
    "Modifier9", "Modifier10", "Modifier11", "Modifier12", "CastingFire", "CastingElectricity", "CastingAcid",
    "CastingDivination", "CastingIllusion", "CastingDomination", "CastingEnchantment", "CastingHealing",
    "TeleportRing", "LightningBlast", "Shower2", "Shower3", "Shower4", "Falling1", "Falling2", "Falling3", "Falling4",
    "ZoneExit", "ShapeChange", "GroundDust1", "GroundDust2", "Bag", "PortalStar", "Portal", "Cylinder1", "Cylinder2",
    "FireStar", "AcidStar", "Sparks", "VisionStar1", "VisionStar2", "VisionStar3", "Regeneration", "Silence",
    "FeebleMind", "FeetCloud1", "FeetCloud2", "VisionStar4", "Stun", "RikArrowSmoke", "CurseStars", "CurseHolder",
    "StartTrans", "Transform",
};
static_assert(std::size(kTypeNames) == kTypeCount);

// The spec of `type`, or null for a type on data or an unknown type.
const ParticleSpec* FindSpec(int type) {
    const int index = type - static_cast<int>(FireBall);
    if (index < 0 || index >= kTypeCount || kSpecIndex[index] < 0) return nullptr;
    return &kSpecs[kSpecIndex[index]];
}

// The types whose elements start other than zeroed, or whose set-up draws.
void SetupElements(ParticleSource& s) {
    switch (ParticleTypeOf(s.type)) {
    case FireBlast: SetupFireBlast(s); break;
    case Tornado: SetupTornado(s); break;
    case CurseHolder: SetupCurseHolder(s); break;
    case Shower1: case Shower2: case Shower3: case Shower4: SetupShower(s); break;
    case ClayRing: SetupClayRing(s); break;
    case GroundDust1: case GroundDust2: SetupGroundDust(s); break;
    case Cylinder1: case Cylinder2: SetupCylinder(s); break;
    default: break;
    }
}

// The frames are cells of a grid `columns` wide, row 0 at the top (v = 1), each inset by 1/512.
void BuildFrames(ParticleSource* s) {
    float step = 1.0f / s->frameColumns;
    s->frames.resize(s->frameCount);
    SourceFrame* rect = s->frames.data();
    for (int i = 0; i < s->frameCount; ++i, ++rect) {
        float column = static_cast<float>(i % s->frameColumns);
        float row = static_cast<float>(i / s->frameColumns);
        rect->u0 = column * step + kFrameInset;
        rect->v0 = 1.0f - (row * step + kFrameInset);
        rect->u1 = (column + 1.0f) * step - kFrameInset;
        rect->v1 = 1.0f - ((row + 1.0f) * step - kFrameInset);
    }
}

// The unknown types logged so far.
std::set<int> g_unknownTypes;

} // namespace

int ShownParticleType(int type) {
    if (FindSpec(type) || effectdata::g_effects.FindById(type)) return type;
    if (g_unknownTypes.insert(type).second)
        ei::Log("particles: unknown particle type 0x%X, no effect shown (once per type)", static_cast<unsigned>(type));
    return kNoParticleType;
}

std::string_view ParticleTypeName(int type) {
    const int index = type - static_cast<int>(FireBall);
    return index >= 0 && index < kTypeCount ? kTypeNames[index] : std::string_view();
}

int ParticleTypeByName(std::string_view name) {
    for (std::size_t i = 0; i < std::size(kTypeNames); ++i)
        if (text::CompareIgnoringCase(kTypeNames[i], name) == 0) return static_cast<int>(FireBall) + static_cast<int>(i);
    return kNoParticleType;
}

// Back to the defaults: no position, attachment or elements; the texture released; unit sizes, a 1000 emission
// spacing, 10000 frame delay, a one-frame grid.
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
    windStrength = 0.0f;
    additiveBlend = false;
    tint = 0;
    rayCount = 0;
    glowKind = GlowKind::Green;
    lib::FreeStorage(elements);
    data.reset();
    alpha = 1.0f;
    typeParam = 1.0f;
    size = 1.0f;
    baseSize = 1.0f;
    spreadMin = {};
    spreadMax = {};
    emitState = 0;
    height = 0.0f;
    growth = 0.0f;
    attachPoint = AttachPoint::EffectPoint;
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

// Create. Sets the source up for its type from its spec, or from its data effect's record: a mod's effect, or an
// original type on data (CreateSource makes known types only).
void ParticleSource::Create() {
    Reset();
    boundsMin = {kBoundsFar, kBoundsFar, kBoundsFar};
    boundsMax = {-kBoundsFar, -kBoundsFar, -kBoundsFar};
    const ParticleSpec* spec = FindSpec(type);
    if (!spec) {
        std::shared_ptr<const effectdata::Record> record = effectdata::g_effects.FindById(type);
        const std::string textureName = record->texture;
        effectdata::SetUp(*this, std::move(record));
        LoadNamedTexture(texture, static_cast<int>(TextureKind::Effects), textureName.c_str());
        BuildFrames(this);
        return;
    }
    flags = (flags & ~(ParticleSourceFlag::TicksWhilePaused | ParticleSourceFlag::OnPart)) |
            ParticleSourceFlag::Emitting | spec->flags;
    attachPoint = spec->attachPoint;
    additiveBlend = spec->additiveBlend;
    emitChance = spec->emitChance;
    emitCount = spec->emitCount;
    maxParticles = spec->maxParticles;
    lifetime = spec->lifetime;
    frameCount = spec->frameCount;
    frameColumns = 4;
    frameDelay = spec->frameDelay;
    emitSpacing = spec->emitSpacing;
    size = spec->size;
    height = spec->height;
    growth = spec->growth;
    alpha = spec->alpha;
    typeParam = spec->typeParam;
    tint = spec->tint;
    spreadMin = spec->spreadMin;
    spreadMax = spec->spreadMax;
    extent = spec->extent;
    elements.resize(spec->elementCount);
    SetupElements(*this);
    LoadNamedTexture(texture, static_cast<int>(TextureKind::Effects), spec->texture);
    BuildFrames(this);
}

// --- Factory and destructor ----------------------------------------------------------------------

// A source never attached emits distance / emitSpacing tries (each a g_particleRandom draw) from the emission point
// it was allocated with, so the memory comes zeroed.
ParticleSource::ParticleSource(int newType) {
    localRotation.SetIdentity();
    worldRotation.SetIdentity();
    Reset();
    type = newType;
}

ParticleSource* NewParticleSource(int type) { return new ParticleSource(type); }

ParticleManager* ParticleSource::Manager() const { return static_cast<ParticleManager*>(owner); }

EngineObject* ParticleManager::NewObject(int objectType) {
    return NewParticleSource(objectType);
}

ParticleSource::~ParticleSource() { Clear(); }

const char* ParticleSource::ClassName() { return "CParticle"; }
ClassId ParticleSource::GetClassId() { return ClassId::ParticleSource; }
