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
#include "engine/particles/particle_data.h"
#include "engine/models/texture.h"
#include "game/objects/map_object.h"
#include "game/world/camera.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include "lib/maths.h"
#include "lib/bit_flags.h"
#include "lib/text_case.h"
#include "platform/log.h"
#include "platform/platform.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <format>
#include <iterator>
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

// A new effect's file (NewEffect): docs/modding.md's first example, which every install's textures can show.
constexpr std::string_view kTemplate = R"toml(texture = "Teleport"          # an effects texture (Res\textures), 16 frames in 4 columns
blend = "additive"

[inputs]                      # the values the game (or a script) sets
charge = { type = "float", default = 0.0 }

[emit]
rate = "2 + charge * 6"       # particles per tick: any value can be an expression

[[particle]]
name = "spark"
lifetime = "range(10, 20)"    # ticks; range() draws a random number
position = "on_ground(origin + disc() * size, 0.05)"   # a random point of a disc round the source, on the ground
velocity = "vec3(0, 0, 0.03)"
size = { start = 0.2, tick = "p.size * 0.95" }   # its start, then each tick from its last value
color = "FFD890"
alpha = { tick = "255 * (1 - p.t)" }   # p.t: the particle's age, 0 to 1
frame = { start = "pick(16)", delay = 1 }
)toml";

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

// A name an effect file can have: letters, digits, '_' and '-'.
bool FileStem(std::string_view name) {
    if (name.empty()) return false;
    for (const char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    return true;
}

// effectdata::g_startOwnClock while the viewer is open: its source's sub-effects.
ParticleSource* StartOwnClock(ParticleManager& manager, int type) { return g_viewer.StartChild(manager, type); }

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
    std::vector<EffectEntry> kept;   // the new effects, kept through the scan
    for (EffectEntry& entry : entries)
        if (!entry.madeText.empty()) kept.push_back(std::move(entry));
    entries.clear();
    for (int type = kFirstType; type <= kLastType; ++type) {
        entries.push_back({std::string(ParticleTypeName(type)), type, Origin::Builtin});
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
    for (EffectEntry& entry : kept) {
        const int index = Find(entry.name);
        if (index >= 0) entries[static_cast<std::size_t>(index)].madeText = std::move(entry.madeText);
        else entries.push_back(std::move(entry));
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
    if (!entry.madeText.empty()) {
        document.Start(entry.name, entry.madeText);
        hasDocument = true;
    } else {
        hasDocument = document.Load(entry.name, entry.id);
    }
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
            !entry.madeText.empty()           ? "made in the viewer"
            : entry.origin == Origin::Builtin ? "built-in data"
                                              : "a mod's data");
}

bool Viewer::NewEffect(std::string_view name) {
    if (!FileStem(name)) {
        status = "a new effect's name is its file's: letters, digits, _ and -";
        return false;
    }
    if (Find(name) >= 0 || ParticleTypeByName(name) != kNoParticleType) {
        status = "there is an effect named " + std::string(name) + " already";
        return false;
    }
    EffectEntry entry{std::string(name), effectdata::IdOf(name), Origin::Mod};
    entry.madeText = std::format("# {}: a new effect, made in the effect viewer from its template. The schema: "
                                 "docs/modding.md, \"Effects\".\n",
                                 name) +
                     std::string(kTemplate);
    entries.push_back(std::move(entry));
    Pick(static_cast<int>(entries.size()) - 1);
    return true;
}

// ---- Playing ---------------------------------------------------------------------------------------------------------

namespace {

// A source of the viewer's let go: it goes back to the manager's spares for the game's sources.
void LetGo(ParticleSource* s) {
    s->StopAndClear();
    s->flags &= ~ParticleSourceFlag::OwnClock;
    s->Release();
}

// One tick of a source on the viewer's clock (the viewer's generator swapped in); false when it is done.
bool TickOwn(ParticleSource& s) {
    const bool alive = s.RunTick();
    s.flags |= ParticleSourceFlag::OwnClock;   // kept whatever the type's tick wrote into its flags
    return alive;
}

} // namespace

void Viewer::Drop() {
    for (ParticleSource* child : children) LetGo(child);
    children.clear();
    if (!source) return;
    LetGo(source);
    source = nullptr;
}

ParticleSource* Viewer::StartChild(ParticleManager& owner, int type) {
    const int shown = ShownParticleType(type);
    if (shown == kNoParticleType) return nullptr;
    ParticleSource* child = owner.CreateObjectAs<ParticleSource>(shown, -1);
    if (!child) return nullptr;
    child->AddRef();
    child->flags |= ParticleSourceFlag::OwnClock;
    children.push_back(child);
    return child;
}

void Viewer::Restart(int target) {
    Drop();
    manager = g_clientWorld.ClientParticleManager();
    tick = 0;
    sourceEnded = false;
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
    // The sub-effects started before this tick first (those it starts now begin at the next), then the source.
    const std::size_t before = children.size();
    std::vector<ParticleSource*> kept;
    for (std::size_t i = 0; i < before; ++i) {
        ParticleSource* child = children[i];   // by index: a child's own sub-effects join the list as it ticks
        if (TickOwn(*child)) kept.push_back(child);
        else LetGo(child);
    }
    if (!sourceEnded && !TickOwn(*source)) sourceEnded = true;
    kept.insert(kept.end(), children.begin() + static_cast<std::ptrdiff_t>(before), children.end());
    children = std::move(kept);
    std::swap(g_particleRandom, random);
    ++tick;
    furthest = std::max(furthest, tick);
    ended = sourceEnded && children.empty();
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
    const auto hold = [](ParticleSource& s) {
        for (Particle* p : s.particles) {
            p->prevPos = p->pos;
            p->prevSize = p->size;
            p->prevColor = p->color;
            p->prevFrame = p->frame;
        }
    };
    hold(*source);
    for (ParticleSource* child : children) hold(*child);
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
    // A made effect's text as typed: a pick again comes back to it.
    if (picked >= 0 && !entries[static_cast<std::size_t>(picked)].madeText.empty())
        entries[static_cast<std::size_t>(picked)].madeText = document.text;
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
        effectdata::g_startOwnClock = &StartOwnClock;
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
    if (w[1] == "children" && Number(w[2], &wanted)) {
        const auto have = static_cast<float>(v.children.size());
        return have >= wanted ? "" : "effectviewer: " + std::to_string(v.children.size()) + " sub-effects, wanted at least " + w[2];
    }
    if (w[1] == "subparticles" && Number(w[2], &wanted)) {
        std::size_t have = 0;
        for (const ParticleSource* child : v.children) have += child->particles.size();
        return static_cast<float>(have) == wanted
                   ? ""
                   : "effectviewer: " + std::to_string(have) + " particles in the sub-effects, wanted " + w[2];
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
    if (verb == "new" && w.size() > 1) {
        if (v.entries.empty()) v.Scan();
        return v.NewEffect(w[1]) ? "" : "effectviewer: " + v.status;
    }
    if (verb == "append" && w.size() > 1) {
        // The rest of the line added to the document's text as typed in its Text tab; "\n" starts a new line.
        if (!v.hasDocument) return "effectviewer: the effect has no file";
        std::string added(line.substr(line.find("append") + 6));
        added.erase(0, added.find_first_not_of(' '));
        for (std::size_t at = added.find("\\n"); at != std::string::npos; at = added.find("\\n", at + 1)) added.replace(at, 2, "\n");
        if (!v.document.text.empty() && v.document.text.back() != '\n') v.document.text += '\n';
        v.document.text += added + "\n";
        v.Edited(false);
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
