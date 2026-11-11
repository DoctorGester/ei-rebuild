// Data effects in the game: the registry (Res\effects through the layers), the particle side of the runtime and the
// typed inputs. More: docs/notes/effects.md, "Data effects".
#include "game/effects/particle_data.h"

#include "game/effects/effects.h"
#include "game/effects/particle_common.h"

#include "game/core/random.h"
#include "game/data/resource.h"
#include "game/objects/map_object.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include "lib/crt.h"
#include "lib/maths.h"
#include "lib/text_case.h"
#include "lib/toml.h"
#include "lib/vecmath.h"
#include "platform/log.h"
#include "platform/platform.h"

#include "effect_files/builtin.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace effectdata {

Registry g_effects;

// ---- The registry ---------------------------------------------------------------------------------------------------

namespace {

constexpr std::string_view kFolder = "Res\\effects";
constexpr std::string_view kExtension = ".effect.toml";

std::string Lower(std::string_view text) {
    std::string lower(text);
    text::LowerInPlace(lower.data(), lower.size());
    return lower;
}

// The built-in file of the effect `name` (assets/effects, any case), or null.
const builtin_effects::File* Builtin(std::string_view name) {
    for (const builtin_effects::File& file : builtin_effects::kFiles)
        if (text::CompareIgnoringCase(file.name, name) == 0) return &file;
    return nullptr;
}

} // namespace

std::string_view BuiltinText(std::string_view name) {
    const builtin_effects::File* file = Builtin(name);
    return file ? std::string_view(reinterpret_cast<const char*>(file->bytes), file->size) : std::string_view();
}

Registry::Entry& Registry::EntryOf(std::string_view name) {
    const std::string key = Lower(name);
    auto found = byName.find(key);
    if (found != byName.end()) return found->second;
    Entry& entry = byName[key];
    entry.name = std::string(name);
    return entry;
}

void Registry::Load(Entry& entry) {
    entry.loaded = true;
    const std::string key = Lower(entry.name);
    const builtin_effects::File* builtin = Builtin(key);
    const int original = ParticleTypeByName(key);
    if (original != kNoParticleType && !builtin) {   // an original type whose effect is still code
        if (!FindRecordFile(std::string(kFolder) + "\\" + entry.name + ".effect", false).tomls.empty())
            game::Warning("effects: %s is an original effect still in code; its file is left out", entry.name);
        return;
    }
    const std::int32_t id = original != kNoParticleType ? original : IdOf(key);
    if (auto other = idNames.find(id); other != idNames.end() && other->second != key) {
        game::Warning("effects: %s and %s share the id 0x%X; %s is left out", other->second, key,
                      static_cast<unsigned>(id), key);
        return;
    }
    const RecordFile file = FindRecordFile(std::string(kFolder) + "\\" + entry.name + ".effect", false);
    if (file.tomls.empty() && !builtin) return;
    RawNode merged;
    std::string from = file.tomls.empty() ? std::string() : file.tomls.front().path;
    if (builtin && !file.replaced) {
        const std::string path = std::string("assets\\effects\\") + builtin->name + std::string(kExtension);
        toml::Document document;
        if (!document.Parse(BuiltinText(key), path)) {
            game::Warning("effects: %s", document.Error());
            return;
        }
        merged = Copy(document.Root(), path);
        if (from.empty()) from = path;
    }
    MergeTomls(file, [&merged](const toml::Value& root, const std::string& path) {
        const RawNode layer = Copy(root, path);
        if (!merged.Exists()) merged = layer;
        else MergeOver(merged, layer);
    });
    std::vector<std::string> errors;
    entry.record = Read(merged, entry.name, &errors, id);
    for (const std::string& error : errors) game::Warning("effects: %s", error);
    if (!entry.record) {
        game::Warning("effects: %s does not load; it shows nothing", entry.name);
        return;
    }
    idNames[id] = key;
    ei::Log("effects: %s (0x%X) from %s: %zu particle kinds, %zu stages, %zu inputs, %zu anchor groups",
            entry.name.c_str(), static_cast<unsigned>(id), from.c_str(), entry.record->particles.size(),
            entry.record->stages.size(), entry.record->inputs.size(), entry.record->anchors.size());
}

std::shared_ptr<const Record> Registry::Find(std::string_view name) {
    if (name.empty()) return nullptr;
    Entry& entry = EntryOf(name);
    if (!entry.loaded) Load(entry);
    return entry.record;
}

void Registry::Scan() {
    scanned = true;
    for (const platform::FolderLayer& layer : platform::FilesByLayer(kFolder)) {
        for (const platform::LayerFile& file : layer.files) {
            const std::string lower = Lower(file.name);
            if (lower.find('\\') != std::string::npos || lower.size() <= kExtension.size() ||
                !lower.ends_with(kExtension))
                continue;
            const std::string key = lower.substr(0, lower.size() - kExtension.size());
            if (ParticleTypeByName(key) != kNoParticleType) continue;   // an original type's: found by its type
            const std::int32_t id = IdOf(key);
            auto [at, added] = idNames.emplace(id, key);
            if (!added && at->second != key) {
                game::Warning("effects: %s and %s share the id 0x%X; %s is left out", at->second, key,
                              static_cast<unsigned>(id), key);
                continue;
            }
            if (added) EntryOf(file.name.substr(0, file.name.size() - kExtension.size()));
        }
    }
}

std::shared_ptr<const Record> Registry::FindById(std::int32_t id) {
    if (!IsDataId(id)) {
        const std::string_view name = ParticleTypeName(id);
        return !name.empty() && Builtin(name) ? Find(name) : nullptr;
    }
    auto found = idNames.find(id);
    if (found == idNames.end() && !scanned) {
        Scan();
        found = idNames.find(id);
    }
    if (found == idNames.end()) return nullptr;
    const std::string key = found->second;   // Find may add to idNames
    return Find(byName.at(key).name);
}

void Registry::Reset() {
    byName.clear();
    idNames.clear();
    scanned = false;
}

// ---- The particle side ------------------------------------------------------------------------------------------------

namespace {

constexpr float kTwoPi = kPi + kPi;
static_assert(sizeof(Particle::vars) / sizeof(float) == kParticleVars);

float& Value(std::vector<float>& values, SourceValue which) { return values[static_cast<int>(which)]; }

int Truncated(float value, int low, int high) {
    if (!(value > static_cast<float>(low))) return low;
    if (value >= static_cast<float>(high)) return high;
    return static_cast<int>(value);
}

std::uint8_t Channel(float value) { return static_cast<std::uint8_t>(Truncated(value, 0, 255)); }

Vector3 AnchorPosition(const State& state, int anchor) {
    if (anchor < 0) return {};
    const float* values = state.anchors[anchor].values;
    return {values[static_cast<int>(AnchorValue::X)], values[static_cast<int>(AnchorValue::Y)],
            values[static_cast<int>(AnchorValue::Z)]};
}

// The carrier's radius and centre height, at their first read in a tick (State::measure): AttachmentOffset measures the
// carrier's bounds again, so only when an expression reads them.
void MeasureCarrier(void* context) {
    ParticleSource& s = *static_cast<ParticleSource*>(context);
    State& state = *s.data;
    if (state.measured) return;
    state.measured = true;
    const ParticleSource::Attachment carrier = s.AttachmentOffset();
    Value(state.source, SourceValue::CarrierRadius) = carrier.radius;
    Value(state.source, SourceValue::CarrierZ) = carrier.offset.z;
}

// The carrier's unit values (SourceValue::CarrierUnit...): its feet as ParticleSource::PartPoint finds a part.
void WriteCarrier(ParticleSource& s, std::vector<float>& v) {
    MapObject* carrier = s.attachedTo;
    Unit* unit = UnitOrNull(carrier);
    Value(v, SourceValue::CarrierUnit) = unit ? 1.0f : 0.0f;
    Value(v, SourceValue::CarrierRunning) = unit && unit->moveMode == MoveMode::Run ? 1.0f : 0.0f;
    Value(v, SourceValue::CarrierDead) = unit && unit->IsDead() ? 1.0f : 0.0f;
    const Vector3 axis = carrier ? carrier->localRotation.AxisX() : Vector3{};
    Value(v, SourceValue::CarrierAxisX) = axis.x;
    Value(v, SourceValue::CarrierAxisY) = axis.y;
    Value(v, SourceValue::CarrierAxisZ) = axis.z;
    Value(v, SourceValue::CarrierBoundsRadius) = carrier ? carrier->bounds.radius : 0.0f;
    const Vector3 left = unit ? s.PartPoint(unit, "ll3") : s.origin;
    const Vector3 right = unit ? s.PartPoint(unit, "rl3") : s.origin;
    Value(v, SourceValue::CarrierLeftFootX) = left.x;
    Value(v, SourceValue::CarrierLeftFootY) = left.y;
    Value(v, SourceValue::CarrierLeftFootZ) = left.z;
    Value(v, SourceValue::CarrierRightFootX) = right.x;
    Value(v, SourceValue::CarrierRightFootY) = right.y;
    Value(v, SourceValue::CarrierRightFootZ) = right.z;
}

// The values that change when the source lets go of its carrier.
void WriteAttachment(ParticleSource& s, State& state) {
    std::vector<float>& v = state.source;
    Value(v, SourceValue::Attached) = s.attachedTo ? 1.0f : 0.0f;
    Value(v, SourceValue::OriginX) = s.origin.x;
    Value(v, SourceValue::OriginY) = s.origin.y;
    Value(v, SourceValue::OriginZ) = s.origin.z;
    if (state.record->readsUnit) WriteCarrier(s, v);
}

// A source value (SourceValue: size, height, growth, alpha, param) written back to the source.
void WriteBack(ParticleSource& s, int which, float value) {
    switch (static_cast<SourceValue>(which)) {
    case SourceValue::Size: s.size = value; break;
    case SourceValue::Height: s.height = value; break;
    case SourceValue::Growth: s.growth = value; break;
    case SourceValue::Alpha: s.alpha = value; break;
    case SourceValue::Param: s.typeParam = value; break;
    default: break;
    }
}

// What the actions of a step or of an "emit" reaction did to the source: the `set` source values written back, a
// `stop`'s Stop, a `stop_emitting`, a `let_go`; after a Stop or a let go the attachment's values are written again,
// so the rest of the tick (the particles' updates, the emission) sees the source free.
void ApplyActions(ParticleSource& s, State& state) {
    for (const int written : state.sourceWrites) WriteBack(s, written, state.source[written]);
    state.sourceWrites.clear();
    const bool detached = state.stopRequested || state.letGo;
    if (state.stopRequested) {
        state.stopRequested = false;
        s.Stop();
    }
    if (state.stopEmitting) {
        state.stopEmitting = false;
        s.flags &= ~ParticleSourceFlag::Emitting;
    }
    if (state.letGo) {
        state.letGo = false;
        s.Attach(nullptr);
    }
    if (detached) WriteAttachment(s, state);
}

// The source's values its expressions read, as the tick starts.
void WriteSource(ParticleSource& s, State& state) {
    std::vector<float>& v = state.source;
    Value(v, SourceValue::Size) = s.size;
    Value(v, SourceValue::Height) = s.height;
    Value(v, SourceValue::Growth) = s.growth;
    Value(v, SourceValue::Alpha) = s.alpha;
    Value(v, SourceValue::Param) = s.typeParam;
    Value(v, SourceValue::BaseSize) = s.baseSize;
    state.measured = false;   // the carrier: MeasureCarrier at the first read
    Value(v, SourceValue::Attached) = s.attachedTo ? 1.0f : 0.0f;
    Value(v, SourceValue::Distance) = s.distance;
    Value(v, SourceValue::MotionX) = s.motion.x;
    Value(v, SourceValue::MotionY) = s.motion.y;
    Value(v, SourceValue::MotionZ) = s.motion.z;
    Value(v, SourceValue::OriginX) = s.origin.x;
    Value(v, SourceValue::OriginY) = s.origin.y;
    Value(v, SourceValue::OriginZ) = s.origin.z;
    Value(v, SourceValue::Particles) = static_cast<float>(s.particles.size());
    Value(v, SourceValue::Index) = 0.0f;
    Value(v, SourceValue::Total) = static_cast<float>(state.total);
    Value(v, SourceValue::WindX) = s.windDirection.x;
    Value(v, SourceValue::WindY) = s.windDirection.y;
    Value(v, SourceValue::WindZ) = s.windDirection.z;
    Value(v, SourceValue::WindStrength) = s.windStrength;
    Value(v, SourceValue::ExtentX) = s.extent.x;
    Value(v, SourceValue::ExtentY) = s.extent.y;
    Value(v, SourceValue::ExtentZ) = s.extent.z;
    Value(v, SourceValue::TurnCos) = s.turnCos;
    Value(v, SourceValue::TurnSin) = s.turnSin;
    Value(v, SourceValue::Emitting) = lib::Has(s.flags, ParticleSourceFlag::Emitting) ? 1.0f : 0.0f;
    Value(v, SourceValue::SpreadMinX) = s.spreadMin.x;
    Value(v, SourceValue::SpreadMinY) = s.spreadMin.y;
    Value(v, SourceValue::SpreadMinZ) = s.spreadMin.z;
    Value(v, SourceValue::SpreadMaxX) = s.spreadMax.x;
    Value(v, SourceValue::SpreadMaxY) = s.spreadMax.y;
    Value(v, SourceValue::SpreadMaxZ) = s.spreadMax.z;
    Value(v, SourceValue::SideX) = s.side.x;
    Value(v, SourceValue::SideY) = s.side.y;
    Value(v, SourceValue::SideZ) = s.side.z;
    Value(v, SourceValue::UpX) = s.up.x;
    Value(v, SourceValue::UpY) = s.up.y;
    Value(v, SourceValue::UpZ) = s.up.z;
    if (state.record->readsUnit) WriteCarrier(s, v);
}

// A particle's own values (`p.*`) as its update or emission reads them.
struct ParticleValues {
    float values[kParticleValues] = {};
    float& operator[](ParticleValue which) { return values[static_cast<int>(which)]; }
};

void WriteParticle(const Particle& p, ParticleValues& v) {
    v[ParticleValue::Age] = static_cast<float>(p.age);
    v[ParticleValue::Life] = static_cast<float>(p.life);
    v[ParticleValue::T] = p.age + p.life > 0 ? static_cast<float>(p.age) / static_cast<float>(p.age + p.life) : 1.0f;
    v[ParticleValue::X] = p.pos.x;
    v[ParticleValue::Y] = p.pos.y;
    v[ParticleValue::Z] = p.pos.z;
    v[ParticleValue::VX] = p.velocity.x;
    v[ParticleValue::VY] = p.velocity.y;
    v[ParticleValue::VZ] = p.velocity.z;
    v[ParticleValue::Size] = p.size;
    v[ParticleValue::Alpha] = static_cast<float>(p.color >> 24);
    v[ParticleValue::Frame] = static_cast<float>(p.frame);
    v[ParticleValue::Anchor] = static_cast<float>(p.state);
}

Env ParticleEnv(const State& state, Particle& p, ParticleValues& values) {
    Env env = p.state >= 0 ? state.AnchorEnv(p.state, &g_particleRandom) : state.SourceEnv(&g_particleRandom);
    env.scopes[static_cast<int>(Scope::Particle)] = values.values;
    env.scopes[static_cast<int>(Scope::ParticleVar)] = p.vars;
    return env;
}

// The first kind whose `when` holds (-1: none).
int ChooseKind(const State& state) {
    const Env env = state.SourceEnv(&g_particleRandom);
    for (std::size_t i = 0; i < state.record->particles.size(); ++i) {
        const Expr& when = state.record->particles[i].when;
        if (when.Empty() || when.Run(env) != 0.0f) return static_cast<int>(i);
    }
    return -1;
}

// One of the live anchors of `group` for a new particle (-1: none live).
int ChooseAnchor(const State& state, int group, AnchorPick pick) {
    std::vector<int> live;
    for (std::size_t i = 0; i < state.anchors.size(); ++i)
        if (state.anchors[i].group == group && state.anchors[i].live) live.push_back(static_cast<int>(i));
    if (live.empty()) return -1;
    const std::uint32_t n = static_cast<std::uint32_t>(live.size());
    const std::uint32_t at = pick == AnchorPick::Random ? g_particleRandom.Next() % n : static_cast<std::uint32_t>(state.total) % n;
    return live[at];
}

// The spawn's offset from its centre, by shape (the draws in the order x, y, z; a ball's as RandomInBall's).
Vector3 SpawnOffset(const ParticleSource& s, const ParticleKind& kind, const Env& env) {
    switch (kind.shape) {
    case SpawnShape::Point: return {};
    case SpawnShape::Box: {
        auto axis = [&](int i, float low, float high) {
            const float from = kind.boxMin[i].Run(env, low);
            const float to = kind.boxMax[i].Run(env, high);
            return g_particleRandom.Range(from, to);
        };
        const float x = axis(0, s.spreadMin.x, s.spreadMax.x);
        const float y = axis(1, s.spreadMin.y, s.spreadMax.y);
        const float z = axis(2, s.spreadMin.z, s.spreadMax.z);
        return {x, y, z};
    }
    case SpawnShape::Ball: {
        const Vector3 v = RandomInBall();
        return v * kind.radius.Run(env, 1.0f);
    }
    case SpawnShape::Shell: {
        const Vector3 v = RandomInBall();
        const float length = Length(v);
        const float radius = kind.radius.Run(env, 1.0f);
        return length > 0.0f ? v * (radius / length) : Vector3{radius, 0.0f, 0.0f};
    }
    case SpawnShape::Disc: {
        const float angle = g_particleRandom.Range(0.0f, kTwoPi);
        const float reach = std::sqrt(g_particleRandom.Below(1.0f)) * kind.radius.Run(env, 1.0f);
        return {lib::Cos(angle) * reach, lib::Sin(angle) * reach, 0.0f};
    }
    case SpawnShape::Ring: {
        const float angle = g_particleRandom.Range(0.0f, kTwoPi);
        const float reach = kind.radius.Run(env, 1.0f);
        return {lib::Cos(angle) * reach, lib::Sin(angle) * reach, 0.0f};
    }
    case SpawnShape::FlatBall: {
        float x, y;
        for (int tries = 10;;) {
            x = g_particleRandom.Range(-1.0f, 1.0f);
            y = g_particleRandom.Range(-1.0f, 1.0f);
            if (LengthSquared2D(y, x) < 1.0f) break;   // random: never NaN
            if (!(--tries > 0)) break;
        }
        return Vector3{x, y, 0.0f} * kind.radius.Run(env, 1.0f);
    }
    }
    return {};
}

std::uint32_t Rgb(const ParticleKind& kind, const Env& env, std::uint32_t current) {
    if (kind.rgbTick[0].Empty() && kind.rgbTick[1].Empty() && kind.rgbTick[2].Empty()) return current & 0xFFFFFFu;
    const std::uint32_t r = Channel(kind.rgbTick[0].Run(env, static_cast<float>((current >> 16) & 0xFF)));
    const std::uint32_t g = Channel(kind.rgbTick[1].Run(env, static_cast<float>((current >> 8) & 0xFF)));
    const std::uint32_t b = Channel(kind.rgbTick[2].Run(env, static_cast<float>(current & 0xFF)));
    return r << 16 | g << 8 | b;
}

// The live anchor of `group` numbered `number` (wrapped by the live count; -1: none live).
int NumberedAnchor(const State& state, int group, float number) {
    std::vector<int> live;
    for (std::size_t i = 0; i < state.anchors.size(); ++i)
        if (state.anchors[i].group == group && state.anchors[i].live) live.push_back(static_cast<int>(i));
    if (live.empty()) return -1;
    const int n = static_cast<int>(live.size());
    const int at = Truncated(number, std::numeric_limits<int>::min() / 2, std::numeric_limits<int>::max() / 2) % n;
    return live[at < 0 ? at + n : at];
}

// What EmitKind made.
enum class Emitted { None, Kept, Dropped };

// A particle of kind `kindIndex` (-1: the regular choice) at anchor `anchor` (-1: chosen by the kind) into `p`; the
// order of its parts is the order of its draws: lifetime, vars, the anchor by its number, the spawn shape, the vars
// after it, position, velocity, size, colour, frame, then `drop`.
Emitted EmitKind(ParticleSource& s, Particle& p, int kindIndex, int anchor, int index) {
    State& state = *s.data;
    if (kindIndex < 0) kindIndex = ChooseKind(state);
    if (kindIndex < 0) return Emitted::None;
    const ParticleKind& kind = state.record->particles[kindIndex];
    const bool numbered = !kind.anchorIndex.Empty();
    if (numbered) anchor = -1;
    else if (kind.anchor >= 0 && (anchor < 0 || state.anchors[anchor].group != kind.anchor))
        anchor = ChooseAnchor(state, kind.anchor, kind.pick);
    if (kind.anchor >= 0 && anchor < 0 && !numbered) return Emitted::None;
    if (kind.anchor < 0) anchor = -1;

    p.kind = kindIndex;
    p.state = anchor;
    p.age = 0;
    p.velocity = {};
    p.drift = {};
    for (float& var : p.vars) var = 0.0f;
    p.frame = 0;
    p.frameTimer = 0;
    p.color = 0xFFFFFFFFu;
    p.size = s.size;
    p.pos = s.origin;

    ParticleValues values;
    values[ParticleValue::Anchor] = static_cast<float>(anchor);
    values[ParticleValue::Index] = static_cast<float>(index);
    Env env = ParticleEnv(state, p, values);
    p.life = Truncated(kind.lifetime.Run(env, 1.0f), 1, std::numeric_limits<int>::max());
    values[ParticleValue::Life] = static_cast<float>(p.life);
    for (std::size_t i = 0; i < kind.vars.size(); ++i)
        if (!kind.vars[i].afterSpawn) p.vars[i] = kind.vars[i].start.Run(env);
    if (numbered) {
        anchor = NumberedAnchor(state, kind.anchor, kind.anchorIndex.Run(env));
        if (anchor < 0) return Emitted::None;
        p.state = anchor;
        values[ParticleValue::Anchor] = static_cast<float>(anchor);
        env = ParticleEnv(state, p, values);
    }

    const Vector3 centre = anchor >= 0 ? s.origin + AnchorPosition(state, anchor) : s.origin;
    Vector3 offset = SpawnOffset(s, kind, env);
    values[ParticleValue::SpawnX] = offset.x;
    values[ParticleValue::SpawnY] = offset.y;
    values[ParticleValue::SpawnZ] = offset.z;
    for (std::size_t i = 0; i < kind.vars.size(); ++i)
        if (kind.vars[i].afterSpawn) p.vars[i] = kind.vars[i].start.Run(env);
    if (!kind.offset[0].Empty() || !kind.offset[1].Empty() || !kind.offset[2].Empty())
        offset = offset + Vector3{kind.offset[0].Run(env), kind.offset[1].Run(env), kind.offset[2].Run(env)};
    const bool placed = kind.shape != SpawnShape::Point || !kind.offset[0].Empty() || !kind.offset[1].Empty() ||
                        !kind.offset[2].Empty();
    if (!kind.position[0].Empty()) {
        const float x = kind.position[0].Run(env);
        const float y = kind.position[1].Run(env);
        p.pos = {x, y, kind.position[2].Run(env)};
    } else {
        p.pos = placed ? offset + centre : centre;
    }
    if (!kind.onGround.Empty())
        p.pos.z = g_clientWorld.RawSurfaceHeightAt(p.pos.x, p.pos.y) + kind.onGround.Run(env);
    if (kind.space == Space::Source) p.drift = p.pos - s.origin;
    else if (kind.space == Space::Anchor) p.drift = p.pos - centre;
    WriteParticle(p, values);

    const bool velocityGiven = !kind.velocity[0].Empty() || !kind.velocity[1].Empty() || !kind.velocity[2].Empty();
    p.velocity = {kind.velocity[0].Run(env), kind.velocity[1].Run(env), kind.velocity[2].Run(env)};
    if (!kind.speed.Empty()) {
        const float length = Length(offset);
        const float speed = kind.speed.Run(env);
        if (length > 0.0f) {
            const Vector3 outward = offset * speed / length;
            p.velocity = velocityGiven ? p.velocity + outward : outward;
        }
    }
    WriteParticle(p, values);

    p.size = kind.sizeStart.Run(env, s.size);
    const std::uint32_t alpha = Channel(kind.alphaStart.Run(env, 255.0f));
    p.color = alpha << 24 | Rgb(kind, env, kind.rgb);
    p.frame = Truncated(kind.frameStart.Run(env, 0.0f), 0, s.frameCount - 1);
    p.frameTimer = Truncated(kind.frameDelay.Run(env, 0.0f), 0, std::numeric_limits<int>::max());
    if (!kind.drop.Empty() && kind.drop.Run(env) != 0.0f) return Emitted::Dropped;
    ++state.total;
    return Emitted::Kept;
}

// After a particle of the emission: the "emit" reactions and what their actions did.
void AfterEmission(ParticleSource& s, State& state) {
    state.Emitted(g_particleRandom);
    ApplyActions(s, state);
    if (state.ended) s.flags &= ~(ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive);
    state.sawEmitting = lib::Has(s.flags, ParticleSourceFlag::Emitting);
    Value(state.source, SourceValue::Emitting) = state.sawEmitting ? 1.0f : 0.0f;
}

} // namespace

void SetUp(ParticleSource& s, std::shared_ptr<const Record> record) {
    const Record& r = *record;
    ParticleSourceFlag flags = ParticleSourceFlag::Emitting;
    if (r.keepAlive) flags |= ParticleSourceFlag::KeepAlive;
    if (r.attach == Attach::AttachedOnly) flags |= ParticleSourceFlag::AttachedOnly;
    if (r.part >= 0) flags |= ParticleSourceFlag::OnPart;
    if (r.ticksWhilePaused) flags |= ParticleSourceFlag::TicksWhilePaused;
    if (r.idlesOffScreen) flags |= ParticleSourceFlag::IdlesOffScreen;
    s.flags = (s.flags & ~(ParticleSourceFlag::TicksWhilePaused | ParticleSourceFlag::OnPart)) | flags;
    s.attachPoint = r.part >= 0 ? static_cast<AttachPoint>(r.part) : AttachPoint::EffectPoint;
    s.additiveBlend = r.additive;
    s.emitChance = 100;
    s.emitCount = 0;
    s.maxParticles = r.maxParticles;
    s.lifetime = -1;
    s.frameCount = r.frames;
    s.frameColumns = r.columns;
    s.emitSpacing = r.spacing;
    s.size = r.size;
    s.height = r.height;
    s.growth = r.growth;
    s.alpha = r.alpha;
    s.typeParam = r.param;
    s.spreadMin = r.spreadMin;
    s.spreadMax = r.spreadMax;
    s.extent = r.extent;
    s.data = std::make_unique<State>(std::move(record));
    s.data->measure = MeasureCarrier;
    s.data->measureContext = &s;
    if (std::any_of(r.vars.begin(), r.vars.end(), [](const Var& var) { return var.atCreate; })) {
        WriteSource(s, *s.data);
        s.data->Create(g_particleRandom);
    }
}

void TickData(ParticleSource& s) {
    State& state = *s.data;
    const bool emittingNow = lib::Has(s.flags, ParticleSourceFlag::Emitting);
    if (state.sawEmitting && !emittingNow && !state.ended) state.Stopped();
    WriteSource(s, state);
    state.Step(g_particleRandom);
    ApplyActions(s, state);
    int tries = state.emitting ? Truncated(state.rate, 0, std::numeric_limits<int>::max() / 2) : 0;
    s.emitCount = tries + state.burst;
    s.emitChance = Truncated(state.chance, 0, 100);
    if (state.ended) s.flags &= ~(ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive);
    state.sawEmitting = lib::Has(s.flags, ParticleSourceFlag::Emitting);
    Value(state.source, SourceValue::Emitting) = state.sawEmitting ? 1.0f : 0.0f;

    for (const std::string& name : state.effects) {
        int type = kNoParticleType;
        if (std::shared_ptr<const Record> sub = g_effects.Find(name)) type = sub->id;
        else type = ParticleTypeByName(name);
        if (type == kNoParticleType) {
            game::Warning("effects: %s: no effect %s to start", state.record->name, name);
            continue;
        }
        ParticleSource* child = s.Manager()->CreateSource(type);
        if (!child) continue;
        child->localPosition = s.origin;
        child->Attach(nullptr);
    }
}

int EmitData(ParticleSource& s, Particle& p, int index) {
    State& state = *s.data;
    Value(state.source, SourceValue::Index) = static_cast<float>(index);
    Value(state.source, SourceValue::Particles) = static_cast<float>(s.particles.size());
    Value(state.source, SourceValue::Total) = static_cast<float>(state.total);
    const Emitted made = EmitKind(s, p, -1, -1, index);
    if (made == Emitted::Kept && state.record->once) {
        s.flags &= ~ParticleSourceFlag::Emitting;
        state.sawEmitting = false;
    }
    if (made != Emitted::None) AfterEmission(s, state);
    return made == Emitted::Kept ? 1 : 0;
}

void EmitQueued(ParticleSource& s) {
    State& state = *s.data;
    ParticleManager* pool = s.Manager();
    int index = 0;
    for (const Queued& queued : state.queue) {
        if (std::ssize(s.particles) >= s.maxParticles) break;
        Particle* p = pool->AcquireParticle();
        Value(state.source, SourceValue::Particles) = static_cast<float>(s.particles.size());
        Value(state.source, SourceValue::Total) = static_cast<float>(state.total);
        const Emitted made = EmitKind(s, *p, queued.kind, queued.anchor, index++);
        if (made == Emitted::Kept) s.AddParticle(p);
        else pool->ReleaseParticle(p);
        if (made != Emitted::None) AfterEmission(s, state);
    }
    state.queue.clear();
}

int UpdateData(ParticleSource& s, Particle& p) {
    State& state = *s.data;
    if (p.kind < 0 || p.kind >= std::ssize(state.record->particles)) return 0;
    const ParticleKind& kind = state.record->particles[p.kind];
    --p.life;
    ++p.age;
    if (p.life <= 0) return 0;
    ParticleValues values;
    WriteParticle(p, values);
    const Env env = ParticleEnv(state, p, values);
    // The frame first: a particle past its last frame in `die` mode ends before it moves, as the originals' (its timer
    // then stays run out, so the update a dead particle still gets steps no frame).
    if (!kind.frameTick.Empty()) {
        p.frame = Truncated(kind.frameTick.Run(env), 0, std::numeric_limits<int>::max());
    } else if (p.frameTimer > 0 && --p.frameTimer == 0) {
        if (++p.frame >= s.frameCount && kind.frameMode == FrameMode::Die) return 0;
        p.frameTimer = Truncated(kind.frameDelay.Run(env, 0.0f), 0, std::numeric_limits<int>::max());
    }
    if (p.frame >= s.frameCount) {
        switch (kind.frameMode) {
        case FrameMode::Loop: p.frame %= s.frameCount; break;
        case FrameMode::Hold: p.frame = s.frameCount - 1; break;
        case FrameMode::Die: return 0;
        }
    }
    for (std::size_t i = 0; i < kind.vars.size(); ++i) p.vars[i] = kind.vars[i].tick.Run(env, p.vars[i]);

    // The move: by the position's tick when given (all three from this tick's values), else by the velocity.
    const bool ticked = !kind.positionTick[0].Empty();
    Vector3 next;
    if (ticked) {
        next.x = kind.positionTick[0].Run(env);
        next.y = kind.positionTick[1].Run(env);
        next.z = kind.positionTick[2].Run(env);
    }
    switch (kind.space) {
    case Space::World:
        if (ticked) p.pos = next;
        else if (kind.moves) p.pos = p.pos + p.velocity;
        break;
    case Space::Source:
        p.drift = ticked ? next : p.drift + p.velocity;
        p.pos = s.origin + p.drift;
        break;
    case Space::Anchor:
        p.drift = ticked ? next : p.drift + p.velocity;
        p.pos = s.origin + AnchorPosition(state, p.state) + p.drift;
        break;
    }
    if (!kind.drag.Empty()) p.velocity = p.velocity * kind.drag.Run(env);
    if (!kind.gravity[0].Empty() || !kind.gravity[1].Empty() || !kind.gravity[2].Empty())
        p.velocity = p.velocity + Vector3{kind.gravity[0].Run(env), kind.gravity[1].Run(env), kind.gravity[2].Run(env)};
    if (!kind.wind.Empty()) p.velocity = p.velocity + s.windDirection * (s.windStrength * kind.wind.Run(env));

    if (!kind.sizeTick.Empty()) p.size = kind.sizeTick.Run(env);
    else if (!kind.growth.Empty()) p.size = p.size * kind.growth.Run(env);
    const std::uint32_t alpha = kind.alphaTick.Empty() ? p.color >> 24 : Channel(kind.alphaTick.Run(env));
    p.color = alpha << 24 | Rgb(kind, env, p.color);
    // The source's values this particle sets: the next particles' updates and the emission read them.
    for (const auto& [which, value] : kind.sourceWrites) {
        const float written = value.Run(env);
        state.source[which] = written;
        WriteBack(s, which, written);
    }

    if (!kind.kill.Empty() && kind.kill.Run(env) != 0.0f) return 0;
    return 1;
}

// ---- The inputs -----------------------------------------------------------------------------------------------------

bool SetInput(ParticleSource& s, std::string_view input, float value) { return s.data && s.data->SetValue(input, value); }

bool SetInput(ParticleSource& s, std::string_view input, const Vector3& value) {
    return s.data && s.data->SetVector(input, value.x, value.y, value.z);
}

bool SetInput(ParticleSource& s, std::string_view input, std::string_view value) {
    return s.data && s.data->SetEnum(input, value);
}

bool Fire(ParticleSource& s, std::string_view event) { return s.data && s.data->Fire(event); }

} // namespace effectdata
