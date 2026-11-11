// Data effects in the game: the registry (Res\effects through the layers), the particle side of the runtime and the
// typed inputs. More: docs/notes/effects.md, "Data effects".
#include "engine/particles/particle_data.h"

#include "engine/particles/particle_source.h"
#include "engine/particles/particle_host.h"

#include "engine/core/engine_globals.h"
#include "engine/core/random.h"
#include "engine/data/resource.h"
#include "engine/scene/light.h"
#include "engine/scene/scene_renderer.h"

#include "lib/crt.h"
#include "lib/float_select.h"
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
#include <utility>
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

namespace {

// The effect `name`'s files merged into `merged`, bottom first: its built-in file (unless a mod's says replace = true),
// then the mods' files; `from` names the first. False when no layer has it (or its built-in file does not parse).
bool MergedLayers(std::string_view name, RawNode* merged, std::string* from) {
    const builtin_effects::File* builtin = Builtin(name);
    const RecordFile file = FindRecordFile(std::string(kFolder) + "\\" + std::string(name) + ".effect", false);
    if (file.tomls.empty() && !builtin) return false;
    *merged = RawNode();
    if (from) *from = file.tomls.empty() ? std::string() : file.tomls.front().path;
    if (builtin && !file.replaced) {
        const std::string path = std::string("assets\\effects\\") + builtin->name + std::string(kExtension);
        toml::Document document;
        if (!document.Parse(BuiltinText(name), path)) {
            game::Warning("effects: %s", document.Error());
            return false;
        }
        *merged = Copy(document.Root(), path);
        if (from && from->empty()) *from = path;
    }
    MergeTomls(file, [merged](const toml::Value& root, const std::string& path) {
        const RawNode layer = Copy(root, path);
        if (!merged->Exists()) *merged = layer;
        else MergeOver(*merged, layer);
    });
    return merged->Exists();
}

} // namespace

bool ResolveLike(RawNode& tree, std::string_view name, std::vector<std::string>* errors) {
    return ResolveLike(
        tree, name, [](std::string_view effect, RawNode* layers) { return MergedLayers(effect, layers, nullptr); },
        errors);
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
    const int original = ParticleTypeByName(key);
    const std::int32_t id = original != kNoParticleType ? original : IdOf(key);
    if (auto other = idNames.find(id); other != idNames.end() && other->second != key) {
        game::Warning("effects: %s and %s share the id 0x%X; %s is left out", other->second, key,
                      static_cast<unsigned>(id), key);
        return;
    }
    RawNode merged;
    std::string from;
    if (!MergedLayers(entry.name, &merged, &from)) return;
    std::vector<std::string> errors;
    if (!ResolveLike(merged, entry.name, &errors)) {
        for (const std::string& error : errors) game::Warning("effects: %s", error);
        game::Warning("effects: %s does not load; it shows nothing", entry.name);
        return;
    }
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
        return !name.empty() ? Find(name) : nullptr;
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

void Registry::Put(std::string_view name, std::shared_ptr<const Record> record) {
    Entry& entry = EntryOf(name);
    entry.loaded = true;
    if (record) idNames[record->id] = Lower(name);
    entry.record = std::move(record);
}

// ---- The particle side ------------------------------------------------------------------------------------------------

namespace {

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

// The carrier's radius and centre height, at their first read in a tick (State::measure): particlehost::MeasureCarrier
// measures the carrier's bounds again, so only when an expression reads them.
void MeasureCarrier(void* context) {
    ParticleSource& s = *static_cast<ParticleSource*>(context);
    State& state = *s.data;
    if (state.measured) return;
    state.measured = true;
    const particlehost::Measure carrier = particlehost::MeasureCarrier(s.attachedTo);
    Value(state.source, SourceValue::CarrierRadius) = carrier.radius;
    Value(state.source, SourceValue::CarrierZ) = carrier.offset.z;
}

// The values that change when the source lets go of its carrier.
void WriteAttachment(ParticleSource& s, State& state) {
    std::vector<float>& v = state.source;
    Value(v, SourceValue::Attached) = s.attachedTo ? 1.0f : 0.0f;
    Value(v, SourceValue::OriginX) = s.origin.x;
    Value(v, SourceValue::OriginY) = s.origin.y;
    Value(v, SourceValue::OriginZ) = s.origin.z;
    if (state.record->readsUnit) particlehost::WriteCarrier(s, v);
}

// A source value (SourceValue: size, height, growth, alpha, param, the spread box, the extent) written back to the
// source.
void WriteBack(ParticleSource& s, int which, float value) {
    switch (static_cast<SourceValue>(which)) {
    case SourceValue::Size: s.size = value; break;
    case SourceValue::Height: s.height = value; break;
    case SourceValue::Growth: s.growth = value; break;
    case SourceValue::Alpha: s.alpha = value; break;
    case SourceValue::Param: s.typeParam = value; break;
    case SourceValue::SpreadMinX: s.spreadMin.x = value; break;
    case SourceValue::SpreadMinY: s.spreadMin.y = value; break;
    case SourceValue::SpreadMinZ: s.spreadMin.z = value; break;
    case SourceValue::SpreadMaxX: s.spreadMax.x = value; break;
    case SourceValue::SpreadMaxY: s.spreadMax.y = value; break;
    case SourceValue::SpreadMaxZ: s.spreadMax.z = value; break;
    case SourceValue::ExtentX: s.extent.x = value; break;
    case SourceValue::ExtentY: s.extent.y = value; break;
    case SourceValue::ExtentZ: s.extent.z = value; break;
    default: break;
    }
}

// What the actions of a step or of an "emit" reaction did to the source: the `set` source values written back, the
// camera's shakes, a `stop`'s Stop, a `stop_emitting`, a `let_go`; after a Stop or a let go the attachment's values
// are written again, so the rest of the tick (the particles' updates, the emission) sees the source free.
void ApplyActions(ParticleSource& s, State& state) {
    for (const int written : state.sourceWrites) WriteBack(s, written, state.source[written]);
    state.sourceWrites.clear();
    for (const ShakeRequest& shake : state.shakes) particlehost::ShakeCamera(s.origin, shake.time, shake.strength, shake.decay);
    state.shakes.clear();
    // The angle is the client's generator's (the original drew it there).
    for (const ScorchRequest& scorch : state.scorches) {
        const float angle = ScaleWord(g_clientRandom.Next(), 6.3f);
        const float alpha = MinKeepA(scorch.alpha, 1.0f);
        particlehost::AddScorchMark(s.origin.x, s.origin.y, scorch.radius, scorch.radius, angle, alpha);
    }
    state.scorches.clear();
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
    const Light* sun = g_sceneRenderer.sun;
    Value(v, SourceValue::SunR) = sun ? sun->color[0] : 0.0f;
    Value(v, SourceValue::SunG) = sun ? sun->color[1] : 0.0f;
    Value(v, SourceValue::SunB) = sun ? sun->color[2] : 0.0f;
    if (state.record->readsUnit) particlehost::WriteCarrier(s, v);
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
    switch (pick) {
    case AnchorPick::Random: return live[g_particleRandom.Next() % n];
    case AnchorPick::First: return live[0];
    case AnchorPick::Cycle: break;
    }
    return live[static_cast<std::uint32_t>(state.total) % n];
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

// A particle of kind `kindIndex` (-1: the regular choice) at anchor `anchor` (-1: chosen by the kind) into `p`, its
// spawn's centre `place` when given (a dying particle's); the order of its parts is the order of its draws: lifetime,
// vars, the anchor by its number, the spawn shape, the vars after it, position, velocity, size, colour, frame, then
// `drop`.
Emitted EmitKind(ParticleSource& s, Particle& p, int kindIndex, int anchor, int index, const Vector3* place = nullptr) {
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

    const Vector3 centre = place ? *place : anchor >= 0 ? s.origin + AnchorPosition(state, anchor) : s.origin;
    Vector3 offset = SpawnPoint(kind.spawn, env, g_particleRandom);
    values[ParticleValue::SpawnX] = offset.x;
    values[ParticleValue::SpawnY] = offset.y;
    values[ParticleValue::SpawnZ] = offset.z;
    for (std::size_t i = 0; i < kind.vars.size(); ++i)
        if (kind.vars[i].afterSpawn) p.vars[i] = kind.vars[i].start.Run(env);
    if (!kind.offset[0].Empty() || !kind.offset[1].Empty() || !kind.offset[2].Empty())
        offset = offset + Vector3{kind.offset[0].Run(env), kind.offset[1].Run(env), kind.offset[2].Run(env)};
    const bool placed = kind.spawn.shape != SpawnShape::Point || !kind.offset[0].Empty() || !kind.offset[1].Empty() ||
                        !kind.offset[2].Empty();
    if (!kind.position[0].Empty()) {
        const float x = kind.position[0].Run(env);
        const float y = kind.position[1].Run(env);
        p.pos = {x, y, kind.position[2].Run(env)};
    } else {
        p.pos = placed ? offset + centre : centre;
    }
    if (!kind.onGround.Empty())
        p.pos.z = particlehost::GroundHeight(p.pos.x, p.pos.y) + kind.onGround.Run(env);
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
    // Past the grid it stays (the update's step wraps or holds it by the mode; drawn as the first frame meanwhile).
    p.frame = Truncated(kind.frameStart.Run(env, 0.0f), 0, std::numeric_limits<int>::max());
    p.frameTimer = Truncated(kind.frameDelay.Run(env, 0.0f), 0, std::numeric_limits<int>::max());
    if (!kind.drop.Empty() && kind.drop.Run(env) != 0.0f) return Emitted::Dropped;
    ++state.total;
    return Emitted::Kept;
}

// What the actions of an emission's reactions did, at once (the emission's later tries and particles see it).
void ApplyEmissionActions(ParticleSource& s, State& state) {
    ApplyActions(s, state);
    if (state.ended) s.flags &= ~(ParticleSourceFlag::Emitting | ParticleSourceFlag::KeepAlive);
    state.sawEmitting = lib::Has(s.flags, ParticleSourceFlag::Emitting);
    Value(state.source, SourceValue::Emitting) = state.sawEmitting ? 1.0f : 0.0f;
}

// Before a particle of the emission: the "before_emit" reactions.
void BeforeEmission(ParticleSource& s, State& state) {
    if (!state.record->beforeEmit) return;
    state.BeforeEmission(g_particleRandom);
    ApplyEmissionActions(s, state);
}

// The particles an "emit" reaction asked for, made at once (before the emission's next try) without reactions of
// their own, so a reaction never feeds itself; `p.index` counts them.
void EmitReactionParticles(ParticleSource& s, State& state) {
    if (state.queue.empty()) return;
    const std::vector<Queued> queued = std::move(state.queue);
    state.queue.clear();
    ParticleManager* pool = s.Manager();
    int index = 0;
    for (const Queued& entry : queued) {
        if (std::ssize(s.particles) >= s.maxParticles) break;
        Particle* p = pool->AcquireParticle();
        Value(state.source, SourceValue::Particles) = static_cast<float>(s.particles.size());
        Value(state.source, SourceValue::Total) = static_cast<float>(state.total);
        const Emitted made = EmitKind(s, *p, entry.kind, entry.anchor, index++, entry.placed ? &entry.place : nullptr);
        if (made == Emitted::Kept) s.AddParticle(p);
        else pool->ReleaseParticle(p);
    }
}

// After a particle of the emission: the "emit" reactions, then the particles they asked for.
void AfterEmission(ParticleSource& s, State& state) {
    state.Emitted(g_particleRandom);
    ApplyEmissionActions(s, state);
    EmitReactionParticles(s, state);
}

// The effects the actions started: free sources of their own, at the source's origin or at their place (those of a
// source on its own clock made by its clock's owner, g_startOwnClock).
void StartEffects(ParticleSource& s, State& state) {
    for (const Started& started : state.effects) {
        int type = kNoParticleType;
        if (std::shared_ptr<const Record> sub = g_effects.Find(started.name)) type = sub->id;
        else type = ParticleTypeByName(started.name);
        if (type == kNoParticleType) {
            game::Warning("effects: %s: no effect %s to start", state.record->name, started.name);
            continue;
        }
        ParticleSource* child = lib::Has(s.flags, ParticleSourceFlag::OwnClock) && g_startOwnClock
                                    ? g_startOwnClock(*s.Manager(), type)
                                    : s.Manager()->CreateSource(type);
        if (!child) continue;
        child->localPosition = started.placed ? started.place : s.origin;
        child->Attach(nullptr);
    }
    state.effects.clear();
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
    s.data->ground = particlehost::GroundHeight;
    s.data->platform = particlehost::PlatformPlane;
    if (std::any_of(r.vars.begin(), r.vars.end(), [](const Var& var) { return var.atCreate; }) ||
        std::any_of(r.anchors.begin(), r.anchors.end(), [](const Anchor& anchor) { return anchor.atCreate; })) {
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
    StartEffects(s, state);
}

void Spawned(ParticleSource& s) {
    if (!s.data || !s.data->record->spawns) return;
    State& state = *s.data;
    WriteSource(s, state);
    state.Spawned(g_particleRandom);
    ApplyEmissionActions(s, state);
    StartEffects(s, state);
}

int EmitData(ParticleSource& s, Particle& p, int index) {
    State& state = *s.data;
    Value(state.source, SourceValue::Index) = static_cast<float>(index);
    Value(state.source, SourceValue::Particles) = static_cast<float>(s.particles.size());
    Value(state.source, SourceValue::Total) = static_cast<float>(state.total);
    BeforeEmission(s, state);
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
    // Moved out first: the "emit" reactions of these particles queue their own (EmitReactionParticles).
    const std::vector<Queued> queue = std::move(state.queue);
    state.queue.clear();
    for (const Queued& queued : queue) {
        if (std::ssize(s.particles) >= s.maxParticles) break;
        Particle* p = pool->AcquireParticle();
        Value(state.source, SourceValue::Particles) = static_cast<float>(s.particles.size());
        Value(state.source, SourceValue::Total) = static_cast<float>(state.total);
        BeforeEmission(s, state);
        const Emitted made = EmitKind(s, *p, queued.kind, queued.anchor, index++, queued.placed ? &queued.place : nullptr);
        if (made == Emitted::Kept) s.AddParticle(p);
        else pool->ReleaseParticle(p);
        if (made != Emitted::None) AfterEmission(s, state);
    }
}

namespace {

// One particle of `kind` moved (0: it dies).
int UpdateKind(ParticleSource& s, State& state, const ParticleKind& kind, Particle& p) {
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
        case FrameMode::Free: break;   // kept past the grid (drawn as the first frame)
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
    // The values this particle sets (the source's, the effect's vars, its anchor's): the next particles' updates and the
    // emission read them.
    for (const ParticleKind::Write& write : kind.writes) {
        const float written = write.value.Run(env);
        switch (write.to) {
        case ParticleKind::Write::To::Source:
            state.source[write.index] = written;
            WriteBack(s, write.index, written);
            break;
        case ParticleKind::Write::To::Var: state.vars[write.index] = written; break;
        case ParticleKind::Write::To::AnchorVar:
            if (p.state >= 0) state.anchors[p.state].vars[write.index] = written;
            break;
        }
    }

    if (!kind.kill.Empty() && kind.kill.Run(env) != 0.0f) return 0;
    // A jump: drawn at its new place this tick, not moving there from the old.
    if (!kind.jump.Empty() && kind.jump.Run(env) != 0.0f) {
        p.prevPos = p.pos;
        p.prevColor = p.color;
        p.prevSize = kind.jumpSize.Run(env, p.size);
    }
    return 1;
}

} // namespace

int UpdateData(ParticleSource& s, Particle& p) {
    State& state = *s.data;
    if (p.kind < 0 || p.kind >= std::ssize(state.record->particles)) return 0;
    const ParticleKind& kind = state.record->particles[p.kind];
    const int alive = UpdateKind(s, state, kind, p);
    // Its death (the first: a dead particle's update still runs once, "Kept"): its on_death actions, the particles at
    // this tick's emission, the effects at once.
    if (!alive && !p.dead && !kind.onDeath.empty()) {
        ParticleValues values;
        WriteParticle(p, values);
        state.Died(kind, ParticleEnv(state, p, values), p.pos);
        StartEffects(s, state);
    }
    return alive;
}

// ---- The inputs -----------------------------------------------------------------------------------------------------

bool SetInput(ParticleSource& s, std::string_view input, float value) { return s.data && s.data->SetValue(input, value); }

bool SetInput(ParticleSource& s, std::string_view input, const Vector3& value) {
    return s.data && s.data->SetVector(input, value.x, value.y, value.z);
}

bool SetInput(ParticleSource& s, std::string_view input, std::string_view value) {
    return s.data && s.data->SetEnum(input, value);
}

bool SetInput(ParticleSource& s, std::string_view input, const std::vector<Vector3>& points) {
    return s.data && s.data->SetList(input, points);
}

bool Fire(ParticleSource& s, std::string_view event) { return s.data && s.data->Fire(event); }

} // namespace effectdata
