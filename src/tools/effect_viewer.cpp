// The effect viewer's playing side (effect_viewer.h): the effects listed, the source made in the client world's particle
// manager but ticked by the viewer (its own generator, so every replay draws the same), its place, its timeline of
// changes, the hooks into the main loop and the harness's commands. docs/notes/effects.md, "The effect viewer".
#include "tools/effect_viewer.h"

#include "tools/effect_viewer_internal.h"
#include "tools/imgui_layer.h"

#include "game/app/app.h"
#include "game/app/singletons.h"
#include "game/app/tool_hooks.h"
#include "game/effects/effects.h"
#include "game/effects/particle_data.h"
#include "game/models/texture.h"
#include "game/objects/map_object.h"
#include "game/scene/camera.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include "lib/maths.h"
#include "lib/text_case.h"
#include "platform/log.h"
#include "platform/platform.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <utility>

namespace tools::effect_viewer {

Viewer g_viewer;

namespace {

constexpr int kFirstType = static_cast<int>(ParticleType::FireBall);
constexpr int kLastType = static_cast<int>(ParticleType::Transform);
constexpr std::string_view kEffectsFolder = "Res\\effects";
constexpr std::string_view kExtension = ".effect.toml";
constexpr int kLoopPause = 15;            // client ticks between an ended effect and its start again
constexpr float kNearbyRange = 40.0f;     // the units listed around the camera's focus
constexpr std::size_t kMostUnits = 24;
constexpr float kDegrees = kPi / 180.0f;

std::string Lower(std::string_view text) {
    std::string lower(text);
    text::LowerInPlace(lower.data(), lower.size());
    return lower;
}

bool SameText(std::string_view a, std::string_view b) { return text::CompareIgnoringCase(a, b) == 0; }

std::vector<std::string> Words(std::string_view line) {
    std::vector<std::string> words;
    std::size_t at = 0;
    while (at < line.size()) {
        while (at < line.size() && line[at] == ' ') ++at;
        std::size_t end = at;
        while (end < line.size() && line[end] != ' ') ++end;
        if (end > at) words.emplace_back(line.substr(at, end - at));
        at = end;
    }
    return words;
}

bool Number(std::string_view text, float* value) {
    const char* end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(text.data(), end, *value);
    return error == std::errc() && stop == end;
}

std::string DefaultSaveFolder() {
    const std::string mod = platform::ModFolder();
    const std::string game = app::GameFolder();
    if (!mod.empty() && (game.empty() || !SameText(std::string_view(mod).substr(0, game.size()), game))) return mod;
    const std::string user = platform::UserDataFolder();
    return user.empty() ? std::string() : user + "ei-rebuild-effects";
}

MadeValues MadeOf(const ParticleSource& s) {
    MadeValues m;
    m.texture = s.texture ? s.texture->name : std::string();
    m.frames = s.frameCount;
    m.columns = s.frameColumns;
    m.frameDelay = s.frameDelay;
    m.emitCount = s.emitCount;
    m.emitChance = s.emitChance;
    m.maxParticles = s.maxParticles;
    m.lifetime = s.lifetime;
    m.elements = static_cast<int>(s.elements.size());
    m.emitSpacing = s.emitSpacing;
    m.size = s.size;
    m.height = s.height;
    m.growth = s.growth;
    m.alpha = s.alpha;
    m.param = s.typeParam;
    m.baseSize = s.baseSize;
    m.spreadMin = s.spreadMin;
    m.spreadMax = s.spreadMax;
    m.extent = s.extent;
    m.additive = s.additiveBlend;
    m.flags = static_cast<std::uint32_t>(s.flags);
    m.attachPoint = static_cast<int>(s.attachPoint);
    return m;
}

// ---- The hooks (app/tool_hooks.h) ------------------------------------------------------------------------------------

bool TakeEvent(const platform::Event& event) { return g_viewer.open && imgui_layer::Feed(event); }

void ClientTicked() { g_viewer.ClientTicked(); }

void WorldsEnding() {
    g_viewer.Drop();
    g_viewer.manager = nullptr;
}

void DrawOverlay() {
    Viewer& v = g_viewer;
    if (!v.open) return;
    if (v.entries.empty()) v.Scan();
    if (!v.pendingPick.empty() && g_clientWorld.ClientParticleManager()) {
        const std::string name = std::exchange(v.pendingPick, std::string());
        const int index = v.Find(name);
        if (index >= 0) v.Pick(index);
        else v.status = "no effect named " + name;
    }
    if (!imgui_layer::BeginFrame()) return;
    DrawPanel(v);
    imgui_layer::EndFrame();
}

} // namespace

// ---- The effects -----------------------------------------------------------------------------------------------------

void Viewer::Scan() {
    entries.clear();
    for (int type = kFirstType; type <= kLastType; ++type) {
        const std::string name(ParticleTypeName(type));
        entries.push_back({name, type, effectdata::BuiltinText(name).empty() ? Origin::Code : Origin::Builtin});
    }
    for (const platform::FolderLayer& layer : platform::FilesByLayer(kEffectsFolder)) {
        for (const platform::LayerFile& file : layer.files) {
            const std::string lower = Lower(file.name);
            if (lower.find('\\') != std::string::npos || lower.size() <= kExtension.size() || !lower.ends_with(kExtension))
                continue;
            const std::string stem = file.name.substr(0, file.name.size() - kExtension.size());
            if (ParticleTypeByName(stem) != kNoParticleType || Find(stem) >= 0) continue;
            entries.push_back({stem, effectdata::IdOf(stem), Origin::Mod});
        }
    }
}

int Viewer::Find(std::string_view name) const {
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (SameText(entries[i].name, name)) return static_cast<int>(i);
    return -1;
}

void Viewer::Pick(int index) {
    if (index < 0 || index >= static_cast<int>(entries.size())) return;
    picked = index;
    timeline.clear();
    furthest = 0;
    playing = true;
    const EffectEntry& entry = entries[static_cast<std::size_t>(index)];
    hasDocument = entry.origin != Origin::Code && document.Load(entry.name, entry.id);
    if (hasDocument && document.record) effectdata::g_effects.Put(entry.name, document.record);
    if (!placed) {
        placed = true;
        if (Unit* unit = PartyUnit()) {
            onUnit = true;
            unitId = unit->id;
        } else {
            freeAt = g_camera.focus;
        }
    }
    Restart(0);
    status.clear();
    ei::Log("effect viewer: %s (0x%X, %s)", entry.name, static_cast<unsigned>(entry.id),
            entry.origin == Origin::Code ? "code" : entry.origin == Origin::Builtin ? "built-in data" : "a mod's data");
}

// ---- Playing ---------------------------------------------------------------------------------------------------------

void Viewer::Drop() {
    if (!source) return;
    source->StopAndClear();
    source->flags &= ~ParticleSourceFlag::OwnClock;   // it goes back to the spares for the game's sources
    source->Release();
    source = nullptr;
}

void Viewer::Restart(int target) {
    Drop();
    manager = g_clientWorld.ClientParticleManager();
    tick = 0;
    ended = false;
    endedTicks = 0;
    if (!manager || picked < 0) return;
    const EffectEntry& entry = entries[static_cast<std::size_t>(picked)];
    const int shown = ShownParticleType(entry.id);
    if (shown == kNoParticleType) {
        status = entry.name + " does not load: it shows nothing";
        return;
    }
    // The source's set-up may draw (a data effect's at_create vars): from the viewer's generator too.
    random = start;
    std::swap(g_particleRandom, random);
    source = manager->CreateObjectAs<ParticleSource>(shown, -1);
    std::swap(g_particleRandom, random);
    if (!source) return;
    source->AddRef();
    made = MadeOf(*source);
    // The viewer's clock ticks it (Step), never the manager's TickSources.
    source->flags |= ParticleSourceFlag::OwnClock;
    Unit* carrier = onUnit ? Carrier() : nullptr;
    source->localPosition = carrier ? Vector3{} : freeAt;
    source->ScaleSize(size / source->baseSize);
    source->Attach(nullptr);
    if (carrier) source->Attach(carrier);
    while (tick < target && !ended) Step();
    if (!playing) Freeze();
}

void Viewer::Step() {
    if (!source || ended) return;
    for (const Change& change : timeline)
        if (change.tick == tick) Make(change, false);
    std::swap(g_particleRandom, random);
    const bool alive = source->RunTick();
    std::swap(g_particleRandom, random);
    source->flags |= ParticleSourceFlag::OwnClock;   // kept whatever the type's tick wrote into its flags
    ++tick;
    furthest = std::max(furthest, tick);
    if (!alive) ended = true;
}

void Viewer::Make(const Change& change, bool record) {
    if (!source) return;
    switch (change.kind) {
    case ChangeKind::StartEmitting: source->StartEmitting(); break;
    case ChangeKind::StopEmitting: source->StopEmitting(); break;
    case ChangeKind::Stop: source->Stop(); break;
    case ChangeKind::Value: effectdata::SetInput(*source, change.name, change.value.x); break;
    case ChangeKind::Vector: effectdata::SetInput(*source, change.name, change.value); break;
    case ChangeKind::Fire: effectdata::Fire(*source, change.name); break;
    case ChangeKind::Alpha: source->alpha = change.value.x; break;
    case ChangeKind::Param: source->typeParam = change.value.x; break;
    case ChangeKind::Extent: source->extent = change.value; break;
    case ChangeKind::Turn:
        source->turnCos = lib::Cos(change.value.x * kDegrees);
        source->turnSin = lib::Sin(change.value.x * kDegrees);
        break;
    }
    if (!record) return;
    // A value dragged through many frames of one tick is one change: its last value.
    const bool value = change.kind != ChangeKind::StartEmitting && change.kind != ChangeKind::StopEmitting &&
                       change.kind != ChangeKind::Stop && change.kind != ChangeKind::Fire;
    if (value && !timeline.empty() && timeline.back().tick == tick && timeline.back().kind == change.kind &&
        timeline.back().name == change.name) {
        timeline.back().value = change.value;
        return;
    }
    Change kept = change;
    kept.tick = tick;
    timeline.push_back(kept);
}

void Viewer::Freeze() {
    if (!source) return;
    for (Particle* p : source->particles) {
        p->prevPos = p->pos;
        p->prevSize = p->size;
        p->prevColor = p->color;
        p->prevFrame = p->frame;
    }
}

void Viewer::ClientTicked() {
    if (!open || !source || !playing || g_clientWorld.paused) return;
    if (ended) {
        if (loop && ++endedTicks >= kLoopPause) Restart(0);
        return;
    }
    Step();
}

// ---- The place -------------------------------------------------------------------------------------------------------

Unit* Viewer::Carrier() const {
    if (!onUnit || unitId < 0) return nullptr;
    Unit* unit = UnitOrNull(g_clientWorld.ObjectWithId(unitId));
    return unit && !(unit->flags & MapObjectFlag::Removed) ? unit : nullptr;
}

std::vector<Unit*> Viewer::NearbyUnits() const {
    std::vector<std::pair<float, Unit*>> found;
    const Vector3 focus = g_camera.focus;
    for (MapObject* object : g_clientWorld.objects) {
        Unit* unit = UnitOrNull(object);
        if (!unit || (unit->flags & MapObjectFlag::Removed)) continue;
        const bool party = (unit->flags & MapObjectFlag::InParty) != 0;
        const float distance = Length(unit->localPosition - focus);
        if (!party && !(distance < kNearbyRange)) continue;
        found.emplace_back(party ? -1.0f : distance, unit);
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Unit*> units;
    for (const auto& [distance, unit] : found) {
        if (units.size() == kMostUnits) break;
        units.push_back(unit);
    }
    return units;
}

Unit* Viewer::PartyUnit() const {
    for (MapObject* object : g_clientWorld.objects) {
        Unit* unit = UnitOrNull(object);
        if (unit && (unit->flags & MapObjectFlag::InParty) && !(unit->flags & MapObjectFlag::Removed)) return unit;
    }
    return nullptr;
}

void Viewer::PlaceOnUnit(int id) {
    placed = true;
    onUnit = true;
    unitId = id;
    Restart(tick);
}

void Viewer::PlaceFree(const Vector3& at) {
    placed = true;
    onUnit = false;
    freeAt = at;
    Restart(tick);
}

// ---- The document ----------------------------------------------------------------------------------------------------

void Viewer::Edited(bool fromTree) {
    if (!hasDocument) return;
    const bool loaded = fromTree ? document.ApplyTree() : document.Apply();
    if (!loaded) {
        status = "not played: " + (document.problems.empty() ? std::string("the file does not load") : document.problems.front());
        return;
    }
    effectdata::g_effects.Put(document.name, document.record);
    status.clear();
    Restart(tick);
}

void Viewer::Save() {
    if (!hasDocument) return;
    const std::string written = document.Save(saveFolder);
    status = written.starts_with("error: ") ? written : "saved " + written;
    ei::Log("effect viewer: %s", status);
}

// ---- The viewer from outside -----------------------------------------------------------------------------------------

void Open(std::string_view effect) {
    Viewer& v = g_viewer;
    if (!v.open) {
        imgui_layer::Start();
        app::g_toolHooks = {&TakeEvent, &ClientTicked, &DrawOverlay, &WorldsEnding};
        IsaacRandom seed;
        for (int i = 0; i < 256; ++i) seed.memory[i] = static_cast<std::uint32_t>(i) * 0x9E3779B9u;
        v.start.InitFrom(seed);
        v.saveFolder = DefaultSaveFolder();
        v.open = true;
        ei::Log("effect viewer: open; edited effects are saved into %s", v.saveFolder);
    }
    if (!effect.empty()) v.pendingPick = std::string(effect);
}

namespace {

// The node at `path` (keys and array indices joined by '.') of `root`, made when its last part is missing from a
// table; null when the path leads nowhere.
effectdata::RawNode* NodeAt(effectdata::RawNode& root, std::string_view path) {
    effectdata::RawNode* node = &root;
    while (!path.empty()) {
        const std::size_t dot = path.find('.');
        const std::string part(path.substr(0, dot));
        path = dot == std::string_view::npos ? std::string_view() : path.substr(dot + 1);
        if (node->kind == effectdata::RawNode::Kind::Array) {
            const int index = std::atoi(part.c_str());
            if (index < 0 || index >= static_cast<int>(node->items.size())) return nullptr;
            node = &node->items[static_cast<std::size_t>(index)];
        } else if (node->kind == effectdata::RawNode::Kind::Table) {
            auto found = std::find_if(node->keys.begin(), node->keys.end(), [&part](const auto& k) { return k.first == part; });
            if (found == node->keys.end()) {
                if (!path.empty()) return nullptr;
                node->keys.emplace_back(part, effectdata::RawNode{});
                found = std::prev(node->keys.end());
            }
            node = &found->second;
        } else {
            return nullptr;
        }
    }
    return node;
}

// `value` written into `node`: a number keeps an integer an integer, "true" / "false" a bool, else text.
void SetNode(effectdata::RawNode& node, std::string_view value) {
    float number = 0.0f;
    if (value == "true" || value == "false") {
        node.kind = effectdata::RawNode::Kind::Bool;
        node.flag = value == "true";
    } else if (Number(value, &number)) {
        const bool integral = value.find_first_of(".eE") == std::string_view::npos;
        if (node.kind != effectdata::RawNode::Kind::Float)
            node.kind = integral ? effectdata::RawNode::Kind::Integer : effectdata::RawNode::Kind::Float;
        node.number = number;
    } else {
        node.kind = effectdata::RawNode::Kind::String;
        node.text = std::string(value);
    }
}

std::string ExpectStep(const Viewer& v, const std::vector<std::string>& w) {
    if (w.size() < 3) return "effectviewer expect needs what and a value";
    float wanted = 0.0f;
    if (w[1] == "particles" && Number(w[2], &wanted)) {
        const float have = v.source ? static_cast<float>(v.source->particles.size()) : 0.0f;
        return have >= wanted ? "" : "effectviewer: " + std::to_string(static_cast<int>(have)) + " particles, wanted at least " + w[2];
    }
    if (w[1] == "tick" && Number(w[2], &wanted))
        return v.tick == static_cast<int>(wanted) ? "" : "effectviewer: at tick " + std::to_string(v.tick) + ", wanted " + w[2];
    if (w[1] == "stage") {
        if (!v.source || !v.source->data || v.source->data->stage < 0) return "effectviewer: no stage";
        const std::string& stage = v.source->data->record->stages[static_cast<std::size_t>(v.source->data->stage)].name;
        return stage == w[2] ? "" : "effectviewer: in stage " + stage + ", wanted " + w[2];
    }
    if (w[1] == "problems" && Number(w[2], &wanted)) {
        const auto have = static_cast<int>(v.document.problems.size());
        return have == static_cast<int>(wanted) ? "" : "effectviewer: " + std::to_string(have) + " problems, wanted " + w[2];
    }
    if (w[1] == "status") {
        std::string rest;
        for (std::size_t i = 2; i < w.size(); ++i) rest += (i > 2 ? " " : "") + w[i];
        return v.status.find(rest) != std::string::npos ? "" : "effectviewer: status '" + v.status + "', wanted '" + rest + "'";
    }
    return "effectviewer expect: unknown " + w[1];
}

} // namespace

std::string Command(std::string_view line) {
    Viewer& v = g_viewer;
    const std::vector<std::string> w = Words(line);
    if (w.empty()) return "effectviewer needs a command";
    ei::Log("effect viewer: command %s", std::string(line));
    const std::string& verb = w[0];
    if (verb == "open") {
        Open(w.size() > 1 ? w[1] : std::string());
        return "";
    }
    if (!v.open) return "effectviewer: not open";
    float a = 0.0f, b = 0.0f, c = 0.0f;
    if (verb == "pick" && w.size() > 1) {
        if (v.entries.empty()) v.Scan();
        const int index = v.Find(w[1]);
        if (index < 0) return "effectviewer: no effect named " + w[1];
        v.Pick(index);
        return "";
    }
    if (verb == "place" && w.size() > 1) {
        if (w[1] == "party") {
            Unit* unit = v.PartyUnit();
            if (!unit) return "effectviewer: no unit of the party";
            v.PlaceOnUnit(unit->id);
        } else if (w[1] == "unit" && w.size() > 2 && Number(w[2], &a)) {
            v.PlaceOnUnit(static_cast<int>(a));
        } else if (w[1] == "focus") {
            Vector3 at = g_camera.focus;
            if (w.size() > 4 && Number(w[2], &a) && Number(w[3], &b) && Number(w[4], &c)) at += Vector3{a, b, c};
            v.PlaceFree(at);
        } else if (w[1] == "free" && w.size() > 4 && Number(w[2], &a) && Number(w[3], &b) && Number(w[4], &c)) {
            v.PlaceFree({a, b, c});
        } else {
            return "effectviewer place: party | unit <id> | focus [dx dy dz] | free <x> <y> <z>";
        }
        return "";
    }
    if (verb == "play") {
        v.playing = true;
        return "";
    }
    if (verb == "pause") {
        v.playing = false;
        v.Freeze();
        return "";
    }
    if (verb == "loop" && w.size() > 1) {
        v.loop = w[1] == "on";
        return "";
    }
    if (verb == "step") {
        const int count = w.size() > 1 && Number(w[1], &a) ? static_cast<int>(a) : 1;
        for (int i = 0; i < count; ++i) v.Step();
        if (!v.playing) v.Freeze();
        return "";
    }
    if (verb == "scrub" && w.size() > 1 && Number(w[1], &a)) {
        v.Restart(static_cast<int>(a));
        return "";
    }
    if (verb == "restart") {
        v.Restart(0);
        return "";
    }
    if (verb == "size" && w.size() > 1 && Number(w[1], &a)) {
        v.size = a;
        v.Restart(v.tick);
        return "";
    }
    if (verb == "emit" && w.size() > 1) {
        v.Make({0, w[1] == "start" ? ChangeKind::StartEmitting : ChangeKind::StopEmitting, {}, {}}, true);
        return "";
    }
    if (verb == "stop") {
        v.Make({0, ChangeKind::Stop, {}, {}}, true);
        return "";
    }
    if (verb == "fire" && w.size() > 1) {
        v.Make({0, ChangeKind::Fire, w[1], {}}, true);
        return "";
    }
    if (verb == "input" && w.size() > 2) {
        if (w.size() > 4 && Number(w[2], &a) && Number(w[3], &b) && Number(w[4], &c)) {
            v.Make({0, ChangeKind::Vector, w[1], {a, b, c}}, true);
        } else if (Number(w[2], &a)) {
            v.Make({0, ChangeKind::Value, w[1], {a, 0.0f, 0.0f}}, true);
        } else {
            const effectdata::Input* input = v.source && v.source->data ? v.source->data->record->FindInput(w[1]) : nullptr;
            if (!input) return "effectviewer: no input " + w[1];
            const auto found = std::find(input->values.begin(), input->values.end(), w[2]);
            if (found == input->values.end()) return "effectviewer: input " + w[1] + " has no value " + w[2];
            v.Make({0, ChangeKind::Value, w[1], {static_cast<float>(found - input->values.begin()), 0.0f, 0.0f}}, true);
        }
        return "";
    }
    if (verb == "field" && w.size() > 2) {
        if (!v.hasDocument) return "effectviewer: the effect has no file";
        effectdata::RawNode* node = NodeAt(v.document.tree, w[1]);
        if (!node) return "effectviewer: no field " + w[1];
        std::string value;
        for (std::size_t i = 2; i < w.size(); ++i) value += (i > 2 ? " " : "") + w[i];
        SetNode(*node, value);
        v.Edited(true);
        return "";
    }
    if (verb == "tab" && w.size() > 1) {
        v.documentTab = w[1] == "text" ? 1 : 0;
        return "";
    }
    if (verb == "folder" && w.size() > 1) {
        // "out" names a folder in the run's output folder (ei::OutputPath), so a scenario saves nowhere else.
        v.saveFolder = w[1] == "out" ? ei::OutputPath(w.size() > 2 ? w[2].c_str() : "viewer_mod") : w[1];
        return "";
    }
    if (verb == "save") {
        v.Save();
        return v.status.starts_with("error: ") ? v.status : "";
    }
    if (verb == "expect") return ExpectStep(v, w);
    return "effectviewer: unknown command " + verb;
}

} // namespace tools::effect_viewer
