// ParticleSource set-up: the types' names, Create (from the effect's record), Reset and Clear.
// More: docs/notes/effects.md, "Particle sources".
#include "game/core/core_transform.h"
#include "game/effects/effects.h"
#include "game/objects/map_object.h"
#include "game/effects/particle_data.h"
#include "game/models/texture.h"

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

constexpr int kFirstType = static_cast<int>(ParticleType::FireBall);
constexpr int kTypeCount = static_cast<int>(ParticleType::Transform) - kFirstType + 1;

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
    if (effectdata::g_effects.FindById(type)) return type;
    if (g_unknownTypes.insert(type).second)
        ei::Log("particles: unknown particle type 0x%X, no effect shown (once per type)", static_cast<unsigned>(type));
    return kNoParticleType;
}

std::string_view ParticleTypeName(int type) {
    const int index = type - kFirstType;
    return index >= 0 && index < kTypeCount ? kTypeNames[index] : std::string_view();
}

int ParticleTypeByName(std::string_view name) {
    for (std::size_t i = 0; i < std::size(kTypeNames); ++i)
        if (text::CompareIgnoringCase(kTypeNames[i], name) == 0) return kFirstType + static_cast<int>(i);
    return kNoParticleType;
}

// Back to the defaults: no position or attachment; the texture released; unit sizes, a 1000 emission
// spacing, a one-frame grid.
void ParticleSource::Reset() {
    localPosition = {};
    localRotation = kIdentityRotation;
    attachedTo = nullptr;
    emitChance = 0;
    emitCount = 0;
    lifetime = -1;
    frameCount = 1;
    frameColumns = 1;
    texture.Reset();
    windDirection = {};
    extent = {};
    windStrength = 0.0f;
    additiveBlend = false;
    data.reset();
    alpha = 1.0f;
    typeParam = 1.0f;
    size = 1.0f;
    baseSize = 1.0f;
    spreadMin = {};
    spreadMax = {};
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

// Create. Sets the source up from its effect's record: an original type's or a mod's (CreateSource makes known types
// only).
void ParticleSource::Create() {
    Reset();
    boundsMin = {kBoundsFar, kBoundsFar, kBoundsFar};
    boundsMax = {-kBoundsFar, -kBoundsFar, -kBoundsFar};
    std::shared_ptr<const effectdata::Record> record = effectdata::g_effects.FindById(type);
    const std::string textureName = record->texture;
    effectdata::SetUp(*this, std::move(record));
    LoadNamedTexture(texture, static_cast<int>(TextureKind::Effects), textureName.c_str());
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
