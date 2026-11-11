// A data effect's record: the layers' TOML merged, the schema read and its expressions compiled, and the effect's
// state machine. More: docs/notes/effects.md, "Data effects".
#include "engine/particles/particle_record.h"

#include "engine/core/random.h"

#include "lib/maths.h"
#include "lib/toml.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <map>
#include <optional>

namespace effectdata {

// ---- The layers' files merged ---------------------------------------------------------------------------------------

namespace {

const RawNode kMissing;

// The key an array of tables merges its elements by.
std::string_view MergeKey(const RawNode& element) {
    for (const char* key : {"name", "event"}) {
        const RawNode& value = element[key];
        if (value.kind == RawNode::Kind::String) return value.text;
    }
    return {};
}

// `remove = true`: the element or var of that name taken out of the layer below.
bool Removes(const RawNode& node) {
    const RawNode& remove = node["remove"];
    return remove.kind == RawNode::Kind::Bool && remove.flag;
}

// `before = "<name>"`: a new element or var goes before the one of that name (empty: after the last).
std::string_view Before(const RawNode& node) {
    const RawNode& before = node["before"];
    return before.kind == RawNode::Kind::String ? std::string_view(before.text) : std::string_view();
}

// A new element or var as it is placed: without its `before`.
RawNode Marked(const RawNode& node) {
    RawNode placed = node;
    std::erase_if(placed.keys, [](const auto& entry) { return entry.first == "before"; });
    return placed;
}

bool IsTableArray(const RawNode& node) {
    return node.kind == RawNode::Kind::Array && !node.items.empty() &&
           std::all_of(node.items.begin(), node.items.end(),
                       [](const RawNode& item) { return item.kind == RawNode::Kind::Table; });
}

} // namespace

const RawNode& RawNode::operator[](std::string_view key) const {
    for (const auto& [name, value] : keys)
        if (name == key) return value;
    return kMissing;
}

std::string RawNode::Where() const { return line > 0 ? file + ":" + std::to_string(line) : file; }

RawNode Copy(const toml::Value& value, const std::string& file) {
    RawNode node;
    node.file = file;
    node.line = value.Line();
    if (value.IsTable()) {
        node.kind = RawNode::Kind::Table;
        for (int i = 0; i < value.Size(); ++i)
            node.keys.emplace_back(std::string(value.KeyAt(i)), Copy(value.ValueAt(i), file));
    } else if (value.IsArray()) {
        node.kind = RawNode::Kind::Array;
        for (int i = 0; i < value.Size(); ++i) node.items.push_back(Copy(value.At(i), file));
    } else if (value.IsString()) {
        node.kind = RawNode::Kind::String;
        node.text = std::string(value.String());
    } else if (value.IsInt()) {
        node.kind = RawNode::Kind::Integer;
        node.number = static_cast<double>(value.Int());
    } else if (value.IsFloat()) {
        node.kind = RawNode::Kind::Float;
        node.number = value.Float();
    } else if (value.IsBool()) {
        node.kind = RawNode::Kind::Bool;
        node.flag = value.Bool();
    }
    return node;
}

void MergeOver(RawNode& below, const RawNode& above) {
    if (below.kind == RawNode::Kind::Table && above.kind == RawNode::Kind::Table) {
        for (const auto& [key, value] : above.keys) {
            auto found = std::find_if(below.keys.begin(), below.keys.end(),
                                      [&key](const auto& entry) { return entry.first == key; });
            if (found != below.keys.end() && Removes(value)) below.keys.erase(found);
            else if (found != below.keys.end()) MergeOver(found->second, Marked(value));
            else if (!Removes(value)) {
                const std::string_view before = Before(value);
                const auto at = std::find_if(below.keys.begin(), below.keys.end(),
                                             [before](const auto& entry) { return entry.first == before; });
                below.keys.emplace(at, key, Marked(value));
            }
        }
        return;
    }
    const bool keyed = std::any_of(above.items.begin(), above.items.end(),
                                   [](const RawNode& item) { return !MergeKey(item).empty(); });
    if (keyed && IsTableArray(below) && IsTableArray(above)) {
        std::vector<bool> merged(below.items.size(), false);
        for (const RawNode& element : above.items) {
            const std::string_view key = MergeKey(element);
            bool matched = false;
            for (std::size_t i = 0; !key.empty() && i < below.items.size(); ++i) {
                if (merged[i] || MergeKey(below.items[i]) != key) continue;
                matched = true;
                if (Removes(element)) {
                    below.items.erase(below.items.begin() + static_cast<std::ptrdiff_t>(i));
                    merged.erase(merged.begin() + static_cast<std::ptrdiff_t>(i));
                    break;
                }
                MergeOver(below.items[i], Marked(element));
                merged[i] = true;
                break;
            }
            if (matched || Removes(element)) continue;
            const std::string_view before = Before(element);
            std::size_t at = below.items.size();
            for (std::size_t i = 0; !before.empty() && i < below.items.size(); ++i)
                if (MergeKey(below.items[i]) == before) at = std::min(at, i);
            below.items.insert(below.items.begin() + static_cast<std::ptrdiff_t>(at), Marked(element));
            merged.insert(merged.begin() + static_cast<std::ptrdiff_t>(at), true);
        }
        return;
    }
    below = above;
}

namespace {

std::string LowerName(std::string_view name) {
    std::string lower(name);
    for (char& c : lower)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return lower;
}

bool ResolveChain(RawNode& tree, std::vector<std::string>& chain,
                  const std::function<bool(std::string_view, RawNode*)>& layers, std::vector<std::string>* errors) {
    const auto like = std::find_if(tree.keys.begin(), tree.keys.end(),
                                   [](const auto& entry) { return entry.first == "like"; });
    if (like == tree.keys.end()) return true;
    const RawNode& base = like->second;
    if (base.kind != RawNode::Kind::String || base.text.empty()) {
        errors->push_back(base.Where() + ": like takes the name of another effect");
        return false;
    }
    const std::string key = LowerName(base.text);
    if (std::find(chain.begin(), chain.end(), key) != chain.end()) {
        std::string loop;
        for (const std::string& name : chain) loop += name + " -> ";
        errors->push_back(base.Where() + ": like = \"" + base.text + "\" goes round in a circle (" + loop + key + ")");
        return false;
    }
    RawNode resolved;
    if (!layers(base.text, &resolved) || !resolved.Exists()) {
        errors->push_back(base.Where() + ": like = \"" + base.text + "\": no effect of that name");
        return false;
    }
    chain.push_back(key);
    if (!ResolveChain(resolved, chain, layers, errors)) return false;
    tree.keys.erase(like);
    MergeOver(resolved, tree);
    resolved.file = tree.file;
    resolved.line = tree.line;
    tree = std::move(resolved);
    return true;
}

} // namespace

bool ResolveLike(RawNode& tree, std::string_view name,
                 const std::function<bool(std::string_view effect, RawNode* layers)>& layers,
                 std::vector<std::string>* errors) {
    std::vector<std::string> chain{LowerName(name)};
    return ResolveChain(tree, chain, layers, errors);
}

// ---- Ids ------------------------------------------------------------------------------------------------------------

std::int32_t IdOf(std::string_view name) {
    std::uint32_t hash = 0x811C9DC5u;
    for (const char c : name) {
        const char lower = c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
        hash ^= static_cast<std::uint8_t>(lower);
        hash *= 0x01000193u;
    }
    return kDataIdRange | static_cast<std::int32_t>(hash & 0x3FFFFFFFu);
}

const Input* Record::FindInput(std::string_view input) const {
    for (const Input& i : inputs)
        if (i.name == input) return &i;
    return nullptr;
}

// ---- Reading the record ---------------------------------------------------------------------------------------------

namespace {

constexpr int kMaxAnchors = 4096;   // per group

// A value's name, its place in its scope and its lanes (a vector's 3: `origin`, whose `origin.x` is the first).
struct Named {
    std::string_view name;
    int index = 0;
    int width = 1;
};
constexpr int At(SourceValue v) { return static_cast<int>(v); }
constexpr int At(AnchorValue v) { return static_cast<int>(v); }
constexpr int At(ParticleValue v) { return static_cast<int>(v); }

constexpr Named kSourceNames[] = {
    {"size", At(SourceValue::Size)},
    {"height", At(SourceValue::Height)},
    {"growth", At(SourceValue::Growth)},
    {"alpha", At(SourceValue::Alpha)},
    {"param", At(SourceValue::Param)},
    {"base_size", At(SourceValue::BaseSize)},
    {"age", At(SourceValue::Age)},
    {"stage_age", At(SourceValue::StageAge)},
    {"stage", At(SourceValue::Stage)},
    {"carrier_radius", At(SourceValue::CarrierRadius)},
    {"carrier_z", At(SourceValue::CarrierZ)},
    {"attached", At(SourceValue::Attached)},
    {"distance", At(SourceValue::Distance)},
    {"motion", At(SourceValue::MotionX), 3},
    {"origin", At(SourceValue::OriginX), 3},
    {"count", At(SourceValue::Particles)},
    {"index", At(SourceValue::Index)},
    {"total", At(SourceValue::Total)},
    {"wind", At(SourceValue::WindX), 3},
    {"wind_strength", At(SourceValue::WindStrength)},
    {"extent", At(SourceValue::ExtentX), 3},
    {"turn_cos", At(SourceValue::TurnCos)},
    {"turn_sin", At(SourceValue::TurnSin)},
    {"emitting", At(SourceValue::Emitting)},
    {"spread_min", At(SourceValue::SpreadMinX), 3},
    {"spread_max", At(SourceValue::SpreadMaxX), 3},
    {"side", At(SourceValue::SideX), 3},
    {"up", At(SourceValue::UpX), 3},
    {"carrier_unit", At(SourceValue::CarrierUnit)},
    {"carrier_running", At(SourceValue::CarrierRunning)},
    {"carrier_dead", At(SourceValue::CarrierDead)},
    {"carrier_axis", At(SourceValue::CarrierAxisX), 3},
    {"carrier_bounds_radius", At(SourceValue::CarrierBoundsRadius)},
    {"carrier_left_foot", At(SourceValue::CarrierLeftFootX), 3},
    {"carrier_right_foot", At(SourceValue::CarrierRightFootX), 3},
    {"carrier_axis_y", At(SourceValue::CarrierAxisYX), 3},
    {"carrier_unit_radius", At(SourceValue::CarrierUnitRadius)},
    {"sun", At(SourceValue::SunR), 3},
};
// The source values a `set` action and a particle's `source` may write (the [source] table's), and their lanes
// (`spread_min.x` one of them).
constexpr Named kSettableSourceNames[] = {
    {"size", At(SourceValue::Size)},
    {"height", At(SourceValue::Height)},
    {"growth", At(SourceValue::Growth)},
    {"alpha", At(SourceValue::Alpha)},
    {"param", At(SourceValue::Param)},
    {"spread_min", At(SourceValue::SpreadMinX), 3},
    {"spread_max", At(SourceValue::SpreadMaxX), 3},
    {"extent", At(SourceValue::ExtentX), 3},
};
constexpr std::string_view kSettableText = "size, height, growth, alpha, param, spread_min, spread_max, extent";
constexpr Named kAnchorNames[] = {
    {"index", At(AnchorValue::Index)},
    {"count", At(AnchorValue::GroupSize)},
    {"age", At(AnchorValue::Age)},
    {"ticks", At(AnchorValue::Ticks)},
    {"position", At(AnchorValue::PositionX), 3},
    {"spawn", At(AnchorValue::SpawnX), 3},
    {"x", At(AnchorValue::PositionX)},
    {"y", At(AnchorValue::PositionY)},
    {"z", At(AnchorValue::PositionZ)},
};
constexpr Named kParticleNames[] = {
    {"age", At(ParticleValue::Age)},
    {"life", At(ParticleValue::Life)},
    {"t", At(ParticleValue::T)},
    {"position", At(ParticleValue::PositionX), 3},
    {"velocity", At(ParticleValue::VelocityX), 3},
    {"size", At(ParticleValue::Size)},
    {"alpha", At(ParticleValue::Alpha)},
    {"frame", At(ParticleValue::Frame)},
    {"anchor", At(ParticleValue::Anchor)},
    {"index", At(ParticleValue::Index)},
    {"spawn", At(ParticleValue::SpawnX), 3},
    {"color", At(ParticleValue::ColorR), 3},
    {"x", At(ParticleValue::PositionX)},
    {"y", At(ParticleValue::PositionY)},
    {"z", At(ParticleValue::PositionZ)},
    {"vx", At(ParticleValue::VelocityX)},
    {"vy", At(ParticleValue::VelocityY)},
    {"vz", At(ParticleValue::VelocityZ)},
};
constexpr std::string_view kParts[] = {
    "effect_point", "head", "above_head", "left_hand", "right_hand", "left_leg", "right_leg", "body",
    "body00", "body01", "body02", "body03", "body04",
};

template <std::size_t N>
const Named* Find(const Named (&names)[N], std::string_view name) {
    for (const Named& named : names)
        if (named.name == name) return &named;
    return nullptr;
}

// A settable source value by name, or one lane of one (`extent.z`); null when there is none.
std::optional<Named> SettableSource(std::string_view name) {
    if (const Named* whole = Find(kSettableSourceNames, name)) return *whole;
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot + 2 != name.size()) return std::nullopt;
    const Named* vector = Find(kSettableSourceNames, name.substr(0, dot));
    const std::size_t lane = std::string_view("xyz").find(name.back());
    if (!vector || vector->width != 3 || lane == std::string_view::npos) return std::nullopt;
    return Named{name, vector->index + static_cast<int>(lane), 1};
}

template <std::size_t N>
int IndexIn(const std::string_view (&names)[N], std::string_view name) {
    for (std::size_t i = 0; i < N; ++i)
        if (names[i] == name) return static_cast<int>(i);
    return -1;
}

int IndexIn(const std::vector<std::string>& names, std::string_view name) {
    for (std::size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return static_cast<int>(i);
    return -1;
}

const Var* FindVar(const std::vector<Var>& vars, std::string_view name) {
    for (const Var& var : vars)
        if (var.name == name) return &var;
    return nullptr;
}

std::string Joined(std::initializer_list<std::string_view> names) {
    std::string text;
    for (const std::string_view name : names) {
        if (!text.empty()) text += ", ";
        text += name;
    }
    return text;
}

// Where an expression is compiled: the source alone, an anchor group, or a particle kind (with its anchor group).
struct Context {
    int anchor = -1;
    int particle = -1;
};

// The vars' widths a read found, by scope and name ("<scope>:<var>": the effect's "", "a<group>", "p<kind>").
using Widths = std::map<std::string, int, std::less<>>;

class Reader {
public:
    Reader(const RawNode& root, std::string_view name, std::vector<std::string>* errors, std::int32_t id,
           const Widths& known)
        : root(root), errors(errors), widths(known) {
        record = std::make_shared<Record>();
        record->name = std::string(name);
        record->id = id != 0 ? id : IdOf(name);
        record->file = root.file;
    }

    std::shared_ptr<const Record> Run() {
        Known(root, {"replace", "texture", "frames", "columns", "blend", "attach", "part", "flags", "server",
                     "source", "emit", "inputs", "vars", "stage", "on", "anchor", "particle", "tables",
                     "curves"});
        ReadLook();
        ReadServer();
        ReadSource();
        ReadTables();
        ReadInputs();
        Declare();
        ReadVars(root["vars"], {}, &record->vars, "");
        ReadEmit();
        for (std::size_t i = 0; i < record->stages.size(); ++i) ReadStage(Element("stage", i), record->stages[i]);
        ReadReactions();
        for (std::size_t i = 0; i < record->anchors.size(); ++i) ReadAnchor(Element("anchor", i), static_cast<int>(i));
        for (std::size_t i = 0; i < record->particles.size(); ++i)
            ReadParticle(Element("particle", i), static_cast<int>(i));
        if (record->particles.empty()) Error(root, "no [[particle]]: the effect emits nothing");
        return fatal ? nullptr : record;
    }

    // The widths the vars' expressions gave.
    Widths found;

private:
    void Error(const RawNode& at, const std::string& what) {
        errors->push_back(at.Where() + ": " + what);
        fatal = true;
    }
    void Note(const RawNode& at, const std::string& what) { errors->push_back(at.Where() + ": " + what); }

    // An unknown key of `table` is logged (and the effect still loads).
    void Known(const RawNode& table, std::initializer_list<std::string_view> names) {
        for (const auto& [key, value] : table.keys) {
            if (std::find(names.begin(), names.end(), key) == names.end())
                Note(value, "unknown key '" + key + "' (known: " + Joined(names) + ")");
        }
    }

    const RawNode& Element(std::string_view array, std::size_t i) {
        const RawNode& list = root[array];
        return list.kind == RawNode::Kind::Array ? list.items[i] : list;
    }
    // The tables of an array of tables (one table alone counts as one).
    std::vector<const RawNode*> Tables(std::string_view key) {
        std::vector<const RawNode*> out;
        const RawNode& node = root[key];
        if (node.kind == RawNode::Kind::Table) out.push_back(&node);
        else if (node.kind == RawNode::Kind::Array)
            for (const RawNode& item : node.items) {
                if (item.kind == RawNode::Kind::Table) out.push_back(&item);
                else Error(item, "[[" + std::string(key) + "]] holds tables");
            }
        else if (node.Exists()) Error(node, "[[" + std::string(key) + "]] holds tables");
        return out;
    }

    bool Number(const RawNode& node, float* out) {
        if (!node.Exists()) return false;
        if (!node.IsNumber()) {
            Error(node, "a number expected");
            return false;
        }
        *out = static_cast<float>(node.number);
        return true;
    }
    bool Integer(const RawNode& node, int* out) {
        if (node.kind == RawNode::Kind::Integer) {   // exact to the int's ends (a float would round 2147483647 up)
            *out = static_cast<int>(std::clamp(node.number, static_cast<double>(std::numeric_limits<int>::min()),
                                               static_cast<double>(std::numeric_limits<int>::max())));
            return true;
        }
        float value = 0.0f;
        if (!Number(node, &value)) return false;
        *out = static_cast<int>(std::lround(value));
        return true;
    }
    bool Flag(const RawNode& node, bool* out) {
        if (!node.Exists()) return false;
        if (node.kind != RawNode::Kind::Bool) {
            Error(node, "true or false expected");
            return false;
        }
        *out = node.flag;
        return true;
    }
    bool Text(const RawNode& node, std::string* out) {
        if (!node.Exists()) return false;
        if (node.kind != RawNode::Kind::String) {
            Error(node, "a string expected");
            return false;
        }
        *out = node.text;
        return true;
    }
    void Vector3Of(const RawNode& node, Vector3* out) {
        if (!node.Exists()) return;
        if (node.kind != RawNode::Kind::Array || node.items.size() != 3) return Error(node, "[x, y, z] expected");
        Number(node.items[0], &out->x);
        Number(node.items[1], &out->y);
        Number(node.items[2], &out->z);
    }
    // One name of `names` (the enum's values), or an error that lists them.
    template <std::size_t N>
    int Choice(const RawNode& node, const std::string_view (&names)[N], int fallback) {
        std::string text;
        if (!Text(node, &text)) return fallback;
        const int index = IndexIn(names, text);
        if (index >= 0) return index;
        std::string known;
        for (const std::string_view name : names) known += (known.empty() ? "" : ", ") + std::string(name);
        Error(node, "unknown '" + text + "' (known: " + known + ")");
        return fallback;
    }

    // ---- top-level, [server], [source], [tables], [curves] ----

    void ReadLook() {
        Text(root["texture"], &record->texture);
        if (record->texture.empty()) Error(root, "no texture");
        Integer(root["frames"], &record->frames);
        Integer(root["columns"], &record->columns);
        if (record->frames < 1) record->frames = 1;
        if (record->columns < 1) record->columns = 1;
        static constexpr std::string_view kBlends[] = {"alpha", "additive"};
        record->additive = Choice(root["blend"], kBlends, 0) == 1;
        static constexpr std::string_view kAttach[] = {"free", "attached_only"};
        record->attach = static_cast<Attach>(Choice(root["attach"], kAttach, 0));
        if (root["part"].Exists()) record->part = Choice(root["part"], kParts, -1);
        const RawNode& flags = root["flags"];
        if (flags.Exists() && flags.kind != RawNode::Kind::Array) Error(flags, "a list of flags expected");
        static constexpr std::string_view kFlags[] = {"ticks_while_paused", "idles_off_screen", "keep_alive"};
        for (const RawNode& flag : flags.items) {
            switch (Choice(flag, kFlags, -1)) {
            case 0: record->ticksWhilePaused = true; break;
            case 1: record->idlesOffScreen = true; break;
            case 2: record->keepAlive = true; break;
            default: break;
            }
        }
    }
    void ReadServer() {
        const RawNode& server = root["server"];
        Known(server, {"one_shot", "script_lifetime"});
        Integer(server["one_shot"], &record->oneShot);
        Integer(server["script_lifetime"], &record->scriptLifetime);
    }
    void ReadSource() {
        const RawNode& source = root["source"];
        Known(source, {"size", "height", "growth", "alpha", "param", "spread_min", "spread_max", "extent"});
        Number(source["size"], &record->size);
        Number(source["height"], &record->height);
        Number(source["growth"], &record->growth);
        Number(source["alpha"], &record->alpha);
        Number(source["param"], &record->param);
        Vector3Of(source["spread_min"], &record->spreadMin);
        Vector3Of(source["spread_max"], &record->spreadMax);
        Vector3Of(source["extent"], &record->extent);
    }
    void ReadTables() {
        for (const auto& [name, values] : root["tables"].keys) {
            if (values.kind != RawNode::Kind::Array) {
                Error(values, "table '" + name + "': a list of numbers expected");
                continue;
            }
            std::vector<float> table;
            for (const RawNode& value : values.items) {
                float number = 0.0f;
                Number(value, &number);
                table.push_back(number);
            }
            record->tableNames.push_back(name);
            record->tables.push_back(std::move(table));
        }
        for (const auto& [name, points] : root["curves"].keys) {
            Curve curve;
            for (const RawNode& point : points.items) {
                float xy[2] = {};
                if (point.kind != RawNode::Kind::Array || point.items.size() != 2) {
                    Error(point, "curve '" + name + "': [t, value] points expected");
                    continue;
                }
                Number(point.items[0], &xy[0]);
                Number(point.items[1], &xy[1]);
                if (!curve.x.empty() && !(xy[0] >= curve.x.back()))
                    Error(point, "curve '" + name + "': the points go up in t");
                curve.x.push_back(xy[0]);
                curve.y.push_back(xy[1]);
            }
            if (points.kind != RawNode::Kind::Array || curve.x.empty())
                Error(points, "curve '" + name + "': a list of [t, value] points expected");
            record->curveNames.push_back(name);
            record->curves.push_back(std::move(curve));
        }
    }

    // ---- inputs ----

    void ReadInputs() {
        static constexpr std::string_view kTypes[] = {"float", "vector", "bool", "event", "enum", "list"};
        for (const auto& [name, spec] : root["inputs"].keys) {
            Input input;
            input.name = name;
            if (spec.kind != RawNode::Kind::Table) {
                Error(spec, "input '" + name + "': { type = ... } expected");
                continue;
            }
            Known(spec, {"type", "default", "values"});
            input.type = static_cast<InputType>(Choice(spec["type"], kTypes, 0));
            if (!spec["type"].Exists()) Error(spec, "input '" + name + "': no type (" + Joined({"float", "vector", "bool", "event", "enum", "list"}) + ")");
            const RawNode& start = spec["default"];
            switch (input.type) {
            case InputType::Float: Number(start, &input.start.x); break;
            case InputType::Vector: Vector3Of(start, &input.start); break;
            case InputType::Bool: {
                bool on = false;
                Flag(start, &on);
                input.start.x = on ? 1.0f : 0.0f;
                break;
            }
            case InputType::Event:
                if (start.Exists()) Error(start, "input '" + name + "': an event has no default");
                break;
            case InputType::Enum: {
                for (const RawNode& value : spec["values"].items) {
                    std::string text;
                    if (Text(value, &text)) input.values.push_back(text);
                }
                if (input.values.empty()) Error(spec, "input '" + name + "': an enum lists its values");
                std::string chosen;
                if (Text(start, &chosen)) {
                    const int index = IndexIn(input.values, chosen);
                    if (index < 0) Error(start, "input '" + name + "': '" + chosen + "' is none of its values");
                    else input.start.x = static_cast<float>(index);
                }
                break;
            }
            case InputType::List:
                if (start.Exists() && start.kind != RawNode::Kind::Array)
                    Error(start, "input '" + name + "': a list's default is a list of [x, y, z]");
                for (const RawNode& point : start.items) {
                    Vector3 value;
                    Vector3Of(point, &value);
                    input.points.push_back(value);
                }
                input.list = record->lists++;
                break;
            }
            if (input.type != InputType::Enum && spec["values"].Exists())
                Error(spec["values"], "input '" + name + "': only an enum has values");
            input.slot = record->inputValues;
            record->inputValues += input.type == InputType::Vector ? 3 : 1;
            record->inputs.push_back(std::move(input));
        }
    }

    // The names of the stages, groups, kinds and vars, before any expression is compiled (they name each other).
    void Declare() {
        for (const RawNode* stage : Tables("stage")) {
            Stage& s = record->stages.emplace_back();
            if (!Text((*stage)["name"], &s.name)) Error(*stage, "a [[stage]] without its name");
        }
        for (const RawNode* anchor : Tables("anchor")) {
            const std::string scope = "a" + std::to_string(record->anchors.size());
            Anchor& a = record->anchors.emplace_back();
            if (!Text((*anchor)["name"], &a.name)) Error(*anchor, "an [[anchor]] without its name");
            a.varFloats = DeclareVars((*anchor)["vars"], &a.vars, scope, false, true);
        }
        for (const RawNode* particle : Tables("particle")) {
            const std::string scope = "p" + std::to_string(record->particles.size());
            ParticleKind& kind = record->particles.emplace_back();
            if (!Text((*particle)["name"], &kind.name)) Error(*particle, "a [[particle]] without its name");
            if (DeclareVars((*particle)["vars"], &kind.vars, scope, true) > kParticleVars)
                Error(*particle, "a particle's vars hold at most " + std::to_string(kParticleVars) +
                                     " floats (a vector three)");
        }
        record->varFloats = DeclareVars(root["vars"], &record->vars, "", false);
    }
    // The vars of `table`, each at the width a read before found (`scope`: where `known` keeps them), placed one after
    // another; the floats they take. A particle's or an anchor's var named as one of its own values (`p.size`, `a.age`)
    // would never be read: refused.
    int DeclareVars(const RawNode& table, std::vector<Var>* vars, const std::string& scope, bool particle,
                    bool anchor = false) {
        int floats = 0;
        for (const auto& [name, value] : table.keys) {
            if ((particle && Find(kParticleNames, name)) || (anchor && Find(kAnchorNames, name)))
                Error(value, "var '" + name + "' is the name of the " + (particle ? "particle's" : "anchor's") +
                                 " own value " + (particle ? "p." : "a.") + name);
            Var& var = vars->emplace_back();
            var.name = name;
            const auto width = widths.find(scope + ":" + name);
            var.width = width != widths.end() ? width->second : 1;
            var.slot = floats;
            floats += var.width;
        }
        return floats;
    }

    // ---- expressions ----

    Resolved Resolve(std::string_view name, Context context) const {
        auto value = [](Scope scope, int index, int width = 1) {
            Resolved r{Resolved::Kind::Value, scope, static_cast<std::uint16_t>(index), 0.0f};
            r.width = static_cast<std::uint8_t>(width);
            return r;
        };
        auto constant = [](float v) { return Resolved{Resolved::Kind::Constant, Scope::Source, 0, v}; };
        if (name.starts_with("p.")) {
            if (context.particle < 0) return {};
            const std::string_view field = name.substr(2);
            if (const Named* own = Find(kParticleNames, field)) return value(Scope::Particle, own->index, own->width);
            if (const Var* var = FindVar(record->particles[context.particle].vars, field))
                return value(Scope::ParticleVar, var->slot, var->width);
            return {};
        }
        if (name.starts_with("a.")) {
            if (context.anchor < 0) return {};
            const std::string_view field = name.substr(2);
            if (const Named* own = Find(kAnchorNames, field)) return value(Scope::Anchor, own->index, own->width);
            if (const Var* var = FindVar(record->anchors[context.anchor].vars, field))
                return value(Scope::AnchorVar, var->slot, var->width);
            return {};
        }
        if (name.starts_with("anchors.")) {
            for (std::size_t i = 0; i < record->anchors.size(); ++i)
                if (record->anchors[i].name == name.substr(8)) return value(Scope::Source, kSourceValues + static_cast<int>(i));
            return {};
        }
        if (name.starts_with("stage.")) {
            for (std::size_t i = 0; i < record->stages.size(); ++i)
                if (record->stages[i].name == name.substr(6)) return constant(static_cast<float>(i));
            return {};
        }
        if (const std::size_t dot = name.find('.'); dot != std::string_view::npos) {
            if (const Input* input = record->FindInput(name.substr(0, dot))) {
                const std::string_view field = name.substr(dot + 1);
                if (input->type == InputType::List) {
                    if (field == "count") return value(Scope::Input, input->slot);
                    const int axis = field == "x" ? 0 : field == "y" ? 1 : field == "z" ? 2 : -1;
                    if (axis < 0) return {};
                    Resolved list{Resolved::Kind::ListAxis, Scope::Input, static_cast<std::uint16_t>(input->list), 0.0f};
                    list.axis = static_cast<std::uint8_t>(axis);
                    return list;
                }
                if (input->type == InputType::Enum) {
                    const int index = IndexIn(input->values, field);
                    return index < 0 ? Resolved{} : constant(static_cast<float>(index));
                }
                return {};
            }
            // An anchor group's field by the anchor's number: `ring.angle[i]`.
            const std::string_view group = name.substr(0, dot), field = name.substr(dot + 1);
            for (std::size_t g = 0; g < record->anchors.size() && g < 256; ++g) {
                if (record->anchors[g].name != group) continue;
                Resolved at{Resolved::Kind::AnchorAt, Scope::Anchor, 0, 0.0f};
                at.group = static_cast<std::uint8_t>(g);
                if (const Named* own = Find(kAnchorNames, field)) {
                    at.index = static_cast<std::uint16_t>(own->index);
                    at.width = static_cast<std::uint8_t>(own->width);
                    return at;
                }
                if (const Var* var = FindVar(record->anchors[g].vars, field)) {
                    at.scope = Scope::AnchorVar;
                    at.index = static_cast<std::uint16_t>(var->slot);
                    at.width = static_cast<std::uint8_t>(var->width);
                    return at;
                }
                return {};
            }
        }
        if (const Input* input = record->FindInput(name)) {
            if (input->type == InputType::List)
                return {Resolved::Kind::List, Scope::Input, static_cast<std::uint16_t>(input->list), 0.0f};
            return value(Scope::Input, input->slot, input->type == InputType::Vector ? 3 : 1);
        }
        if (const Var* var = FindVar(record->vars, name)) return value(Scope::SourceVar, var->slot, var->width);
        if (const Named* own = Find(kSourceNames, name)) return value(Scope::Source, own->index, own->width);
        if (const int i = IndexIn(record->tableNames, name); i >= 0)
            return {Resolved::Kind::Table, Scope::Source, static_cast<std::uint16_t>(i), 0.0f};
        if (const int i = IndexIn(record->curveNames, name); i >= 0)
            return {Resolved::Kind::Curve, Scope::Source, static_cast<std::uint16_t>(i), 0.0f};
        return {};
    }

    // A number, true / false or an expression's text, compiled in `context`; an empty Expr when not given.
    Expr Field(const RawNode& node, Context context) {
        switch (node.kind) {
        case RawNode::Kind::Missing: return {};
        case RawNode::Kind::Integer:
        case RawNode::Kind::Float: return ConstantExpr(static_cast<float>(node.number));
        case RawNode::Kind::Bool: return ConstantExpr(node.flag ? 1.0f : 0.0f);
        case RawNode::Kind::String: {
            std::string error;
            Expr expr = Compile(node.text, [this, context](std::string_view name) { return Resolve(name, context); },
                                &error);
            if (expr.Empty()) Error(node, "\"" + node.text + "\": " + error);
            for (Instruction& i : expr.code) {
                if (i.op == Op::Load && i.scope == Scope::Source &&
                    (i.index == static_cast<int>(SourceValue::CarrierRadius) ||
                     i.index == static_cast<int>(SourceValue::CarrierZ))) {
                    i.op = Op::LoadMeasured;
                    record->readsCarrier = true;
                }
                if (i.op == Op::Load && i.scope == Scope::Source &&
                    i.index >= static_cast<int>(SourceValue::CarrierUnit) && i.index < kSourceValues)
                    record->readsUnit = true;
            }
            return expr;
        }
        default: Error(node, "a number or an expression expected"); return {};
        }
    }
    // A point: one expression of three lanes (or one, the same in all three) or `[x, y, z]`, three of one lane.
    Expr VectorField(const RawNode& node, Context context) {
        if (!node.Exists()) return {};
        if (node.kind == RawNode::Kind::Array) {
            if (node.items.size() != 3) {
                Error(node, "[x, y, z] expected");
                return {};
            }
            std::vector<Expr> parts;
            for (const RawNode& item : node.items) {
                Expr part = Field(item, context);
                if (part.Empty()) return {};
                if (part.width != 1) Error(item, "[x, y, z] holds three values of one lane each");
                parts.push_back(std::move(part));
            }
            std::string error;
            return effectdata::Joined(parts, &error);
        }
        Expr expr = Field(node, context);
        if (!expr.Empty() && expr.width != 1 && expr.width != 3)
            Error(node, "a point expected (three lanes), not " + std::to_string(expr.width));
        return expr;
    }
    // A field: a value (its start) or a table's `start` and `tick` (its other keys the caller's), a tick alone starting
    // it too; `point`: points (VectorField).
    void StartTick(const RawNode& node, Context context, Expr* start, Expr* tick, bool point = false) {
        auto read = [&](const RawNode& n) { return point ? VectorField(n, context) : Field(n, context); };
        if (node.kind != RawNode::Kind::Table) {
            *start = read(node);
            return;
        }
        *start = read(node["start"]);
        *tick = read(node["tick"]);
        if (start->Empty()) *start = *tick;
    }

    void ReadVars(const RawNode& table, Context context, std::vector<Var>* vars, const std::string& scope) {
        for (std::size_t i = 0; i < table.keys.size() && i < vars->size(); ++i) {
            const RawNode& spec = table.keys[i].second;
            Var& var = (*vars)[i];
            if (spec.kind == RawNode::Kind::Table) {
                if (context.particle >= 0) {
                    Known(spec, {"start", "tick", "after_spawn"});
                    Flag(spec["after_spawn"], &var.afterSpawn);
                } else if (context.anchor >= 0) {
                    Known(spec, {"start", "tick"});
                } else {
                    Known(spec, {"start", "tick", "at_create"});
                    Flag(spec["at_create"], &var.atCreate);
                }
            }
            StartTick(spec, context, &var.start, &var.tick);
            int width = 1;
            for (const Expr* expr : {&var.start, &var.tick})
                if (!expr->Empty()) width = std::max(width, expr->width);
            found[scope + ":" + var.name] = width;
        }
    }
    static bool ReadsSpawn(const Expr& expr) {
        for (const Instruction& i : expr.code)
            if (i.op == Op::Load && i.scope == Scope::Particle && i.index + i.width > At(ParticleValue::SpawnX) &&
                i.index <= At(ParticleValue::SpawnZ))
                return true;
        return false;
    }

    // ---- [emit], stages, reactions ----

    void ReadEmit() {
        const RawNode& emit = root["emit"];
        Known(emit, {"rate", "chance", "emitting", "spacing", "max", "once"});
        record->rate = Field(emit["rate"], {});
        record->chance = Field(emit["chance"], {});
        record->emitting = Field(emit["emitting"], {});
        Number(emit["spacing"], &record->spacing);
        Integer(emit["max"], &record->maxParticles);
        Flag(emit["once"], &record->once);
    }
    int StageIndex(const RawNode& node) {
        std::string name;
        if (!Text(node, &name)) return -1;
        if (name == "end") return kEndStage;
        for (std::size_t i = 0; i < record->stages.size(); ++i)
            if (record->stages[i].name == name) return static_cast<int>(i);
        Error(node, "no stage '" + name + "'");
        return -1;
    }
    int EventSlot(const RawNode& at, const std::string& name) {
        const Input* input = record->FindInput(name);
        if (input && input->type == InputType::Event) return input->slot;
        Error(at, "'" + name + "' is no event input");
        return -1;
    }
    void ReadStage(const RawNode& node, Stage& stage) {
        Known(node, {"name", "ticks", "next", "until", "on", "rate", "chance", "emitting", "enter"});
        stage.ticks = Field(node["ticks"], {});
        stage.next = StageIndex(node["next"]);
        stage.until = Field(node["until"], {});
        stage.untilNext = stage.next;
        if (!stage.until.Empty() && stage.next == -1) Error(node, "stage '" + stage.name + "': until needs a next");
        if (!stage.ticks.Empty() && stage.next == -1) Error(node, "stage '" + stage.name + "': ticks needs a next");
        for (const auto& [event, target] : node["on"].keys) {
            const int slot = EventSlot(target, event);
            const int to = StageIndex(target);
            if (slot >= 0 && to != -1) stage.onEvent.emplace_back(slot, to);
        }
        stage.rate = Field(node["rate"], {});
        stage.chance = Field(node["chance"], {});
        stage.emitting = Field(node["emitting"], {});
        stage.enter = Actions(node["enter"], {});
    }
    void ReadReactions() {
        for (const RawNode* node : Tables("on")) {
            Known(*node, {"name", "event", "when", "do"});
            Reaction& reaction = record->reactions.emplace_back();
            if (!Text((*node)["event"], &reaction.event)) Error(*node, "an [[on]] without its event");
            if (reaction.event == "start") reaction.on = Reaction::On::Start;
            else if (reaction.event == "stop") reaction.on = Reaction::On::Stop;
            else if (reaction.event == "tick") reaction.on = Reaction::On::Tick;
            else if (reaction.event == "emit") reaction.on = Reaction::On::Emit;
            else if (reaction.event == "before_emit") reaction.on = Reaction::On::BeforeEmit;
            else if (reaction.event == "spawn") reaction.on = Reaction::On::Spawn;
            else if (!reaction.event.empty()) {
                reaction.on = Reaction::On::Event;
                reaction.slot = EventSlot((*node)["event"], reaction.event);
            }
            reaction.when = Field((*node)["when"], {});
            reaction.actions = Actions((*node)["do"], {});
            if (reaction.on == Reaction::On::BeforeEmit) record->beforeEmit = true;
            if (reaction.on == Reaction::On::Spawn) {
                record->spawns = true;
                // No emission follows it at once: its particles would be lost.
                for (const Action& action : reaction.actions)
                    if (action.kind == Action::Kind::Emit) Error(*node, "a \"spawn\" reaction does not emit");
                continue;
            }
            if (reaction.on != Reaction::On::Emit && reaction.on != Reaction::On::BeforeEmit) continue;
            // An "emit" reaction's particles are made at once, without reactions of their own (particle_data.cpp).
            const bool emits = reaction.on == Reaction::On::Emit;
            for (const Action& action : reaction.actions) {
                const bool emitsKind = emits && action.kind == Action::Kind::Emit && action.particleKind >= 0;
                if ((action.kind == Action::Kind::Emit && !emitsKind) || action.kind == Action::Kind::Stage ||
                    action.kind == Action::Kind::Effect)
                    Error(*node, "an \"" + reaction.event + "\" reaction sets, adds anchors, stops, lets go or ends" +
                                     (emits ? " and emits a kind (emit with kind)" : "") +
                                     "; it does not change stage or start an effect");
            }
        }
    }
    // The actions of `list`, their values compiled in `context` (a particle's for its on_death).
    std::vector<Action> Actions(const RawNode& list, Context context) {
        std::vector<Action> actions;
        if (!list.Exists()) return actions;
        if (list.kind != RawNode::Kind::Array) {
            Error(list, "a list of actions expected");
            return actions;
        }
        for (const RawNode& node : list.items) {
            Known(node, {"emit", "kind", "at", "set", "value", "stage", "effect", "end", "stop", "stop_emitting",
                         "let_go", "add_anchor", "count", "shake", "time", "decay", "scorch", "alpha"});
            Action action;
            std::string name;
            if (node["emit"].Exists()) {
                action.kind = Action::Kind::Emit;
                action.count = Field(node["emit"], context);
                if (Text(node["kind"], &name)) action.particleKind = KindIndex(node["kind"], name);
                if (Text(node["at"], &name)) action.anchor = GroupIndex(node["at"], name);
            } else if (Text(node["set"], &name)) {
                action.kind = Action::Kind::Set;
                const Input* input = record->FindInput(name);
                const Var* var = FindVar(record->vars, name);
                const std::optional<Named> own = SettableSource(name);
                if (input && input->type != InputType::Event && input->type != InputType::List) {
                    action.target = input->slot;
                    action.width = input->type == InputType::Vector ? 3 : 1;
                } else if (!input && var) {
                    action.kind = Action::Kind::SetVar;
                    action.target = var->slot;
                    action.width = var->width;
                } else if (!input && own) {
                    action.kind = Action::Kind::SetSource;
                    action.target = own->index;
                    action.width = own->width;
                } else {
                    Error(node, "'" + name + "' is no float, vector, bool or enum input, no var, nor " +
                                    std::string(kSettableText));
                }
                action.value = Field(node["value"], context);
                if (action.value.Empty()) Error(node, "set needs a value");
            } else if (node["stage"].Exists()) {
                action.kind = Action::Kind::Stage;
                action.target = StageIndex(node["stage"]);
            } else if (Text(node["effect"], &action.effect)) {
                action.kind = Action::Kind::Effect;
            } else if (node["end"].Exists()) {
                action.kind = Action::Kind::End;
            } else if (node["stop"].Exists()) {
                action.kind = Action::Kind::Stop;
            } else if (node["stop_emitting"].Exists()) {
                action.kind = Action::Kind::StopEmitting;
            } else if (node["let_go"].Exists()) {
                action.kind = Action::Kind::LetGo;
            } else if (Text(node["add_anchor"], &name)) {
                action.kind = Action::Kind::AddAnchor;
                action.anchor = GroupIndex(node["add_anchor"], name);
                action.count = Field(node["count"], context);
                if (action.count.Empty()) action.count = ConstantExpr(1.0f);
            } else if (node["shake"].Exists()) {
                action.kind = Action::Kind::Shake;
                action.value = Field(node["shake"], context);
                action.time = Field(node["time"], context);
                action.decay = Field(node["decay"], context);
                if (action.time.Empty() || action.decay.Empty()) Error(node, "shake needs a time and a decay");
            } else if (node["scorch"].Exists()) {
                action.kind = Action::Kind::Scorch;
                action.value = Field(node["scorch"], context);
                action.alpha = Field(node["alpha"], context);
                if (action.alpha.Empty()) action.alpha = ConstantExpr(1.0f);
            } else {
                Error(node, "an action is emit, set, stage, effect, end, stop, stop_emitting, let_go, add_anchor, "
                            "shake or scorch");
                continue;
            }
            actions.push_back(std::move(action));
        }
        return actions;
    }
    int KindIndex(const RawNode& at, const std::string& name) {
        for (std::size_t i = 0; i < record->particles.size(); ++i)
            if (record->particles[i].name == name) return static_cast<int>(i);
        Error(at, "no [[particle]] '" + name + "'");
        return -1;
    }
    int GroupIndex(const RawNode& at, const std::string& name) {
        for (std::size_t i = 0; i < record->anchors.size(); ++i)
            if (record->anchors[i].name == name) return static_cast<int>(i);
        Error(at, "no [[anchor]] '" + name + "'");
        return -1;
    }

    // ---- anchors and particles ----

    // A spawn: one point expression, or a shape (`spawn = { shape, radius, min, max, from, to }`; `known`: the table's
    // keys where it is read).
    void ReadSpawn(const RawNode& spawn, Context context, Spawn* out, std::initializer_list<std::string_view> known) {
        if (spawn.Exists() && spawn.kind != RawNode::Kind::Table) {
            out->point = VectorField(spawn, context);
            return;
        }
        Known(spawn, known);
        Integer(spawn["tries"], &out->tries);
        out->shift = VectorField(spawn["shift"], context);
        static constexpr std::string_view kShapes[] = {"point", "box", "ball", "shell", "disc", "ring", "flat_ball",
                                                       "segment"};
        out->shape = static_cast<SpawnShape>(Choice(spawn["shape"], kShapes, 0));
        out->radius = Field(spawn["radius"], context);
        out->boxMin = VectorField(spawn["min"], context);
        out->boxMax = VectorField(spawn["max"], context);
        out->from = VectorField(spawn["from"], context);
        out->to = VectorField(spawn["to"], context);
    }

    void ReadAnchor(const RawNode& node, int group) {
        Known(node, {"name", "count", "spawn", "ticks", "vars", "position", "x", "y", "z", "radius", "angle",
                     "at_create"});
        Anchor& anchor = record->anchors[group];
        const Context context{group, -1};
        anchor.count = Field(node["count"], {});
        if (anchor.count.Empty()) Error(node, "anchor '" + anchor.name + "': no count");
        ReadSpawn(node["spawn"], context, &anchor.spawn,
                  {"shape", "radius", "min", "max", "from", "to", "tries", "shift"});
        Flag(node["at_create"], &anchor.atCreate);
        anchor.ticks = Field(node["ticks"], context);
        ReadVars(node["vars"], context, &anchor.vars, "a" + std::to_string(group));
        anchor.cylindrical = node["radius"].Exists() || node["angle"].Exists();
        const bool axes = node["x"].Exists() || node["y"].Exists() || node["z"].Exists();
        if (anchor.cylindrical && (node["x"].Exists() || node["y"].Exists() || node["position"].Exists()))
            Error(node, "anchor '" + anchor.name + "': position or radius / angle, not both");
        if (axes && node["position"].Exists()) Error(node, "anchor '" + anchor.name + "': position or x / y / z, not both");
        if (anchor.cylindrical) {
            anchor.radius = Field(node["radius"], context);
            anchor.angle = Field(node["angle"], context);
            anchor.z = Field(node["z"], context);
        } else if (axes) {
            std::vector<Expr> parts;
            for (const char* axis : {"x", "y", "z"}) {
                Expr part = Field(node[axis], context);
                parts.push_back(part.Empty() ? ConstantExpr(0.0f) : std::move(part));
            }
            std::string error;
            anchor.positionTick = effectdata::Joined(parts, &error);
            anchor.position = anchor.positionTick;
        } else {
            if (node["position"].kind == RawNode::Kind::Table) Known(node["position"], {"start", "tick"});
            StartTick(node["position"], context, &anchor.position, &anchor.positionTick, true);
        }
    }

    void ReadParticle(const RawNode& node, int index) {
        Known(node, {"name", "when", "lifetime", "vars", "at", "pick", "anchor_index", "space", "spawn", "position",
                     "velocity", "speed", "drag", "gravity", "wind", "size", "color", "alpha", "frame", "kill", "drop",
                     "source", "jump", "on_death"});
        ParticleKind& kind = record->particles[index];
        std::string group;
        if (Text(node["at"], &group)) kind.anchor = GroupIndex(node["at"], group);
        const Context context{kind.anchor, index};
        kind.when = Field(node["when"], {});   // before the particle and its anchor are chosen
        kind.lifetime = Field(node["lifetime"], context);
        if (kind.lifetime.Empty()) Error(node, "particle '" + kind.name + "': no lifetime");
        const RawNode& spawn = node["spawn"];
        ReadSpawn(spawn, context, &kind.spawn,
                  {"shape", "radius", "min", "max", "from", "to", "tries", "shift", "offset", "on_ground"});
        ReadVars(node["vars"], context, &kind.vars, "p" + std::to_string(index));
        static constexpr std::string_view kPicks[] = {"cycle", "random", "first"};
        kind.pick = static_cast<AnchorPick>(Choice(node["pick"], kPicks, 0));
        kind.anchorIndex = Field(node["anchor_index"], context);
        if (!kind.anchorIndex.Empty()) {
            if (kind.anchor < 0) Error(node, "particle '" + kind.name + "': anchor_index needs at");
            // The anchor is chosen after the lifetime, the spawn's point and the vars, so those cannot use it.
            bool early = ReadsAnchor(kind.lifetime) || ReadsAnchor(kind.spawn.point);
            for (const Var& var : kind.vars) early = early || (!var.afterSpawn && ReadsAnchor(var.start));
            if (early)
                Error(node, "particle '" + kind.name + "': with anchor_index, the lifetime, the spawn and the vars "
                            "read no a.*");
        }
        static constexpr std::string_view kSpaces[] = {"world", "source", "anchor"};
        kind.space = static_cast<Space>(Choice(node["space"], kSpaces, 0));
        if (kind.space == Space::Anchor && kind.anchor < 0) Error(node, "particle '" + kind.name + "': space anchor needs at");

        if (spawn.kind == RawNode::Kind::Table) {
            kind.offset = VectorField(spawn["offset"], context);
            kind.onGround = Field(spawn["on_ground"], context);
            // The shape draws after the vars: those before it cannot read its point.
            for (const Var& var : kind.vars)
                if (!var.afterSpawn && ReadsSpawn(var.start))
                    Error(node, "particle '" + kind.name + "': var '" + var.name +
                                    "' reads p.spawn before the spawn shape draws: give it after_spawn = true");
        }
        const RawNode& position = node["position"];
        if (position.kind == RawNode::Kind::Table) Known(position, {"start", "tick"});
        StartTick(position, context, &kind.position, &kind.positionTick, true);

        const RawNode& velocity = node["velocity"];
        if (velocity.kind == RawNode::Kind::Table) Known(velocity, {"start", "tick"});
        StartTick(velocity, context, &kind.velocity, &kind.velocityTick, true);
        kind.speed = Field(node["speed"], context);
        kind.drag = Field(node["drag"], context);
        kind.gravity = VectorField(node["gravity"], context);
        kind.wind = Field(node["wind"], context);
        if (!kind.velocityTick.Empty() && (!kind.drag.Empty() || !kind.gravity.Empty() || !kind.wind.Empty()))
            Error(node, "particle '" + kind.name + "': a velocity tick or drag, gravity and wind, not both");

        const RawNode& size = node["size"];
        if (size.kind == RawNode::Kind::Table) Known(size, {"start", "tick", "growth"});
        StartTick(size, context, &kind.sizeStart, &kind.sizeTick);
        kind.growth = Field(size["growth"], context);
        if (!kind.sizeTick.Empty() && !kind.growth.Empty())
            Error(size, "particle '" + kind.name + "': size tick or growth, not both");

        const RawNode& color = node["color"];
        if (color.kind == RawNode::Kind::String && IsHexColour(color.text)) {
            std::uint32_t rgb = 0;
            std::from_chars(color.text.data(), color.text.data() + color.text.size(), rgb, 16);
            kind.rgb = rgb;
        } else if (color.Exists()) {
            kind.color = VectorField(color, context);
        }
        const RawNode& alpha = node["alpha"];
        if (alpha.kind == RawNode::Kind::Table) Known(alpha, {"start", "tick"});
        StartTick(alpha, context, &kind.alphaStart, &kind.alphaTick);
        const RawNode& frame = node["frame"];
        if (frame.kind == RawNode::Kind::Table) {
            Known(frame, {"start", "delay", "mode", "tick"});
            kind.frameDelay = Field(frame["delay"], context);
            static constexpr std::string_view kModes[] = {"loop", "hold", "die", "free"};
            kind.frameMode = static_cast<FrameMode>(Choice(frame["mode"], kModes, 0));
        }
        StartTick(frame, context, &kind.frameStart, &kind.frameTick);
        kind.kill = Field(node["kill"], context);
        kind.drop = Field(node["drop"], context);
        ReadWrites(node["source"], kind, context);
        const RawNode& jump = node["jump"];
        if (jump.kind == RawNode::Kind::Table) {
            Known(jump, {"when", "size"});
            kind.jump = Field(jump["when"], context);
            kind.jumpSize = Field(jump["size"], context);
            if (kind.jump.Empty()) Error(jump, "particle '" + kind.name + "': jump needs its when");
        } else {
            kind.jump = Field(jump, context);
        }
        kind.onDeath = Actions(node["on_death"], context);
        for (const Action& action : kind.onDeath) {
            const bool placed = (action.kind == Action::Kind::Emit && action.particleKind >= 0) ||
                                action.kind == Action::Kind::Effect || action.kind == Action::Kind::Set ||
                                action.kind == Action::Kind::SetVar;
            if (!placed)
                Error(node["on_death"], "particle '" + kind.name + "': on_death emits a kind (emit with kind), "
                                        "starts an effect or sets an input or a var");
        }
    }
    // "RRGGBB": a colour's six hex digits (any other string is an expression).
    static bool IsHexColour(std::string_view text) {
        return text.size() == 6 && std::all_of(text.begin(), text.end(), [](char c) {
                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
               });
    }
    // A particle's `source = { ... }`: the source's values, the effect's vars and (`a.<var>`) its anchor's vars it sets
    // at each update, in the file's order.
    void ReadWrites(const RawNode& writes, ParticleKind& kind, Context context) {
        if (!writes.Exists()) return;
        if (writes.kind != RawNode::Kind::Table)
            return Error(writes, "particle '" + kind.name + "': source is a table of the values it sets");
        using To = ParticleKind::Write::To;
        for (const auto& [name, value] : writes.keys) {
            if (name == "a" && value.kind == RawNode::Kind::Table) {
                for (const auto& [var, expr] : value.keys) {
                    const Var* target = kind.anchor >= 0 ? FindVar(record->anchors[kind.anchor].vars, var) : nullptr;
                    if (!target) {
                        Error(expr, "particle '" + kind.name + "': source sets a.<var>, a var of its anchor (at), not 'a." +
                                        var + "'");
                        continue;
                    }
                    kind.writes.push_back({To::AnchorVar, target->slot, target->width, Field(expr, context)});
                }
                continue;
            }
            if (const Var* var = FindVar(record->vars, name)) {
                kind.writes.push_back({To::Var, var->slot, var->width, Field(value, context)});
                continue;
            }
            const std::optional<Named> own = SettableSource(name);
            if (!own) {
                Error(value, "particle '" + kind.name + "': source sets " + std::string(kSettableText) +
                                 ", an effect var or a.<var>, not '" + name + "'");
                continue;
            }
            kind.writes.push_back({To::Source, own->index, own->width, Field(value, context)});
        }
    }
    static bool ReadsAnchor(const Expr& expr) {
        for (const Instruction& i : expr.code)
            if ((i.op == Op::Load || i.op == Op::LoadMeasured) &&
                (i.scope == Scope::Anchor || i.scope == Scope::AnchorVar))
                return true;
        return false;
    }

    const RawNode& root;
    std::vector<std::string>* errors = nullptr;
    const Widths& widths;     // the vars' widths an earlier read found
    std::shared_ptr<Record> record;
    bool fatal = false;
};

} // namespace

std::shared_ptr<const Record> Read(const RawNode& root, std::string_view name, std::vector<std::string>* errors,
                                   std::int32_t id) {
    if (root.kind != RawNode::Kind::Table) {
        errors->push_back(root.Where() + ": no effect");
        return nullptr;
    }
    // A var takes the widest lanes of its expressions, which may read vars declared after it: the record is read again
    // with the widths found until none widens (a vector var's start widens it at the first read, so most files take
    // one read and vector vars two).
    Widths widths;
    for (int pass = 0;; ++pass) {
        std::vector<std::string> problems;
        Reader reader(root, name, &problems, id, widths);
        std::shared_ptr<const Record> record = reader.Run();
        bool widened = false;
        for (const auto& [var, width] : reader.found) {
            const auto before = widths.find(var);
            if (width > (before != widths.end() ? before->second : 1)) {
                widths[var] = width;
                widened = true;
            }
        }
        if (!widened || pass == 4) {
            errors->insert(errors->end(), problems.begin(), problems.end());
            return record;
        }
    }
}
// ---- The state machine ---------------------------------------------------------------------------------------------

namespace {

int Count(float value) {
    if (!(value > 0.0f)) return 0;
    if (value > static_cast<float>(kMaxAnchors)) return kMaxAnchors;
    return static_cast<int>(std::lround(value));
}

// The source's vector whose x is `x`.
Vector3 SourceVectorOf(const Env& env, SourceValue x) {
    const float* source = env.scopes[static_cast<int>(Scope::Source)];
    if (!source) return {};
    const int at = static_cast<int>(x);
    return {source[at], source[at + 1], source[at + 2]};
}

// The effect's or an anchor's vars `only` picks started (`tick` false) or ticked (an empty tick keeps its var), in
// order: each reads the ones before it as they now are.
template <typename Only>
void Update(const std::vector<Var>& specs, float* vars, const Env& env, bool tick, Only only) {
    for (const Var& var : specs) {
        if (!only(var)) continue;
        const Expr& expr = tick ? var.tick : var.start;
        if (!expr.Empty()) Store(vars + var.slot, var.width, expr.RunLanes(env));
        else if (!tick) Store(vars + var.slot, var.width, Lanes{});
    }
}
bool All(const Var&) { return true; }

} // namespace

Vector3 SpawnPoint(const Spawn& spawn, const Env& env, IsaacRandom& random) {
    constexpr float kTwoPi = kPi + kPi;
    if (!spawn.point.Empty()) return spawn.point.RunVector(env, {});
    const Vector3 shifted = spawn.shift.RunVector(env, {});
    const Vector3* shift = spawn.shift.Empty() ? nullptr : &shifted;
    switch (spawn.shape) {
    case SpawnShape::Point: return {};
    case SpawnShape::Box: {
        const Vector3 low = spawn.boxMin.RunVector(env, SourceVectorOf(env, SourceValue::SpreadMinX));
        const Vector3 high = spawn.boxMax.RunVector(env, SourceVectorOf(env, SourceValue::SpreadMaxX));
        const float x = random.Range(low.x, high.x);
        const float y = random.Range(low.y, high.y);
        const float z = random.Range(low.z, high.z);
        return {x, y, z};
    }
    case SpawnShape::Ball: {
        const Vector3 v = RandomInBall(random, nullptr, spawn.tries, shift);
        return v * spawn.radius.Run(env, 1.0f);
    }
    case SpawnShape::Shell: {
        const Vector3 v = RandomInBall(random, nullptr, spawn.tries, shift);
        const float length = Length(v);
        const float radius = spawn.radius.Run(env, 1.0f);
        return length > 0.0f ? v * (radius / length) : Vector3{radius, 0.0f, 0.0f};
    }
    case SpawnShape::Disc: {
        const float angle = random.Range(0.0f, kTwoPi);
        const float reach = std::sqrt(random.Below(1.0f)) * spawn.radius.Run(env, 1.0f);
        return {lib::Cos(angle) * reach, lib::Sin(angle) * reach, 0.0f};
    }
    case SpawnShape::Ring: {
        const float angle = random.Range(0.0f, kTwoPi);
        const float reach = spawn.radius.Run(env, 1.0f);
        return {lib::Cos(angle) * reach, lib::Sin(angle) * reach, 0.0f};
    }
    case SpawnShape::FlatBall: {
        float x, y;
        for (int tries = 10;;) {
            x = random.Range(-1.0f, 1.0f);
            y = random.Range(-1.0f, 1.0f);
            if (LengthSquared2D(y, x) < 1.0f) break;   // random: never NaN
            if (!(--tries > 0)) break;
        }
        return Vector3{x, y, 0.0f} * spawn.radius.Run(env, 1.0f);
    }
    case SpawnShape::Segment: {
        const Vector3 from = spawn.from.RunVector(env, {});
        const Vector3 to = spawn.to.RunVector(env, SourceVectorOf(env, SourceValue::ExtentX));
        const float t = random.Range(0.0f, 1.0f);
        return from + (to - from) * t;
    }
    }
    return {};
}

State::State(std::shared_ptr<const Record> effect) : record(std::move(effect)) {
    source.assign(kSourceValues + record->anchors.size(), 0.0f);
    inputs.assign(record->inputValues, 0.0f);
    for (const Input& input : record->inputs) {
        inputs[input.slot] = input.start.x;
        if (input.type == InputType::Vector) {
            inputs[input.slot + 1] = input.start.y;
            inputs[input.slot + 2] = input.start.z;
        }
    }
    lists.resize(record->lists);
    for (const Input& input : record->inputs)
        if (input.type == InputType::List) SetList(input.name, input.points);
    pendingEvents.assign(record->inputValues, 0);
    vars.assign(record->varFloats, 0.0f);
}

float State::AnchorAt(const void* context, int group, Scope scope, int field, float number) {
    const State& state = *static_cast<const State*>(context);
    int count = 0;
    for (const AnchorState& anchor : state.anchors) count += anchor.group == group ? 1 : 0;
    if (count == 0) return 0.0f;
    // Truncated and held to the group's ends, as a table's entry.
    int wanted = 0;
    if (number >= static_cast<float>(count - 1)) wanted = count - 1;
    else if (number >= 1.0f) wanted = static_cast<int>(number);
    for (const AnchorState& anchor : state.anchors) {
        if (anchor.group != group || wanted-- > 0) continue;
        if (scope == Scope::AnchorVar) return field < std::ssize(anchor.vars) ? anchor.vars[field] : 0.0f;
        return field < kAnchorValues ? anchor.values[field] : 0.0f;
    }
    return 0.0f;
}

bool State::SetValue(std::string_view input, float value) {
    const Input* found = record->FindInput(input);
    if (!found || found->type == InputType::Vector || found->type == InputType::Event) return false;
    if (found->type == InputType::Bool) value = value != 0.0f ? 1.0f : 0.0f;
    if (found->type == InputType::Enum) {
        if (!(value >= 0.0f) || value >= static_cast<float>(found->values.size())) return false;
        value = std::floor(value);
    }
    inputs[found->slot] = value;
    return true;
}

bool State::SetVector(std::string_view input, float x, float y, float z) {
    const Input* found = record->FindInput(input);
    if (!found || found->type != InputType::Vector) return false;
    inputs[found->slot] = x;
    inputs[found->slot + 1] = y;
    inputs[found->slot + 2] = z;
    return true;
}

bool State::SetEnum(std::string_view input, std::string_view value) {
    const Input* found = record->FindInput(input);
    if (!found || found->type != InputType::Enum) return false;
    const int index = IndexIn(found->values, value);
    if (index < 0) return false;
    inputs[found->slot] = static_cast<float>(index);
    return true;
}

bool State::SetList(std::string_view input, const std::vector<Vector3>& points) {
    const Input* found = record->FindInput(input);
    if (!found || found->type != InputType::List) return false;
    std::vector<float>& list = lists[found->list];
    list.clear();
    for (const Vector3& point : points) list.insert(list.end(), {point.x, point.y, point.z});
    inputs[found->slot] = static_cast<float>(points.size());
    return true;
}

bool State::Fire(std::string_view event) {
    const Input* found = record->FindInput(event);
    if (!found || found->type != InputType::Event) return false;
    ++pendingEvents[found->slot];
    return true;
}

Env State::SourceEnv(IsaacRandom* random) const {
    Env env;
    env.scopes[static_cast<int>(Scope::Source)] = source.data();
    env.scopes[static_cast<int>(Scope::Input)] = inputs.data();
    env.scopes[static_cast<int>(Scope::SourceVar)] = vars.data();
    env.tables = &record->tables;
    env.curves = &record->curves;
    env.lists = &lists;
    env.random = random;
    env.measure = measure;
    env.measureContext = measureContext;
    env.ground = ground;
    env.platform = platform;
    env.anchorAt = AnchorAt;
    env.anchorContext = this;
    env.ball = &ball;
    return env;
}

Env State::AnchorEnv(int index, IsaacRandom* random) const {
    Env env = SourceEnv(random);
    const AnchorState& anchor = anchors[index];
    env.scopes[static_cast<int>(Scope::Anchor)] = anchor.values;
    env.scopes[static_cast<int>(Scope::AnchorVar)] = anchor.vars.data();
    return env;
}

void State::Step(IsaacRandom& random) {
    burst = 0;
    queue.clear();
    effects.clear();
    shakes.clear();
    scorches.clear();
    sourceWrites.clear();
    stopEmitting = false;
    letGo = false;
    source[static_cast<int>(SourceValue::Age)] = static_cast<float>(age);

    // The events fired since the last step are this tick's values.
    for (const Input& input : record->inputs) {
        if (input.type != InputType::Event) continue;
        inputs[input.slot] = static_cast<float>(pendingEvents[input.slot]);
        pendingEvents[input.slot] = 0;
    }

    const bool first = !started;
    if (first) {
        Start(random);
    } else {
        const Env env = SourceEnv(&random);
        Update(record->vars, vars.data(), env, true, All);
        StepAnchors(random);
    }
    if (stopPending) {
        stopPending = false;
        React(Reaction::On::Stop, random);
    }
    const int stageBefore = stage;
    for (const Reaction& reaction : record->reactions) {
        if (reaction.on == Reaction::On::Event && reaction.slot >= 0 && inputs[reaction.slot] > 0.0f &&
            Holds(reaction, random))
            Run(reaction.actions, random);
    }
    React(Reaction::On::Tick, random);
    if (stage >= 0 && stage == stageBefore && !first) {
        const Stage& current = record->stages[stage];
        int to = -1;
        for (const auto& [slot, target] : current.onEvent)
            if (to == -1 && inputs[slot] > 0.0f) to = target;
        if (to == -1 && !current.until.Empty() && current.until.Run(SourceEnv(&random)) != 0.0f) to = current.untilNext;
        if (to == -1 && stageTicks >= 0 && stageAge >= stageTicks) to = current.next;
        if (to != -1) Enter(to, random);
    }

    // This tick's emission: the stage's over [emit]'s.
    const Env env = SourceEnv(&random);
    const Stage* current = stage >= 0 ? &record->stages[stage] : nullptr;
    const Expr& rateExpr = current && !current->rate.Empty() ? current->rate : record->rate;
    const Expr& chanceExpr = current && !current->chance.Empty() ? current->chance : record->chance;
    const Expr& emittingExpr = current && !current->emitting.Empty() ? current->emitting : record->emitting;
    rate = rateExpr.Run(env, 0.0f);
    chance = chanceExpr.Run(env, 100.0f);
    emitting = !ended && emittingExpr.Run(env, 1.0f) != 0.0f;

    source[static_cast<int>(SourceValue::Stage)] = static_cast<float>(stage);
    source[static_cast<int>(SourceValue::StageAge)] = static_cast<float>(stageAge);
    ++age;
    ++stageAge;
}

void State::React(Reaction::On on, IsaacRandom& random) {
    for (const Reaction& reaction : record->reactions)
        if (reaction.on == on && Holds(reaction, random)) Run(reaction.actions, random);
}

void State::Emitted(IsaacRandom& random) { React(Reaction::On::Emit, random); }

void State::BeforeEmission(IsaacRandom& random) { React(Reaction::On::BeforeEmit, random); }

void State::Spawned(IsaacRandom& random) { React(Reaction::On::Spawn, random); }

void State::Died(const ParticleKind& kind, const Env& env, const Vector3& place) {
    for (const Action& action : kind.onDeath) {
        switch (action.kind) {
        case Action::Kind::Emit: {
            const int count = Count(action.count.Run(env));
            for (int n = 0; n < count; ++n) queue.push_back({action.particleKind, -1, true, place});
            break;
        }
        case Action::Kind::Effect: effects.push_back({action.effect, true, place}); break;
        case Action::Kind::Set:
            if (action.target >= 0) Store(&inputs[action.target], action.width, action.value.RunLanes(env));
            break;
        case Action::Kind::SetVar: Store(&vars[action.target], action.width, action.value.RunLanes(env)); break;
        default: break;   // refused at the read
        }
    }
}

bool State::Holds(const Reaction& reaction, IsaacRandom& random) const {
    return reaction.when.Empty() || reaction.when.Run(SourceEnv(&random)) != 0.0f;
}

void State::Create(IsaacRandom& random) {
    Update(record->vars, vars.data(), SourceEnv(&random), false, [](const Var& var) { return var.atCreate; });
    MakeAnchors(random, true);
}

void State::Start(IsaacRandom& random) {
    started = true;
    Update(record->vars, vars.data(), SourceEnv(&random), false, [](const Var& var) { return !var.atCreate; });
    // The anchors made with the source tick from the first tick on; the others are made now.
    if (!anchors.empty()) StepAnchors(random);
    MakeAnchors(random, false);
    React(Reaction::On::Start, random);
    if (!record->stages.empty() && stage == -1) Enter(0, random);
}

void State::Enter(int index, IsaacRandom& random, int depth) {
    if (index == kEndStage) {
        ended = true;
        return;
    }
    if (index < 0 || depth > 8) return;
    stage = index;
    stageAge = 0;
    source[static_cast<int>(SourceValue::Stage)] = static_cast<float>(stage);
    source[static_cast<int>(SourceValue::StageAge)] = 0.0f;
    const Stage& entered = record->stages[index];
    stageTicks = entered.ticks.Empty() ? -1 : Count(entered.ticks.Run(SourceEnv(&random)));
    Run(entered.enter, random, depth + 1);
}

void State::Run(const std::vector<Action>& actions, IsaacRandom& random, int depth) {
    for (const Action& action : actions) {
        switch (action.kind) {
        case Action::Kind::Emit: {
            const int count = Count(action.count.Run(SourceEnv(&random)));
            if (action.anchor >= 0) {
                for (std::size_t i = 0; i < anchors.size(); ++i) {
                    if (anchors[i].group != action.anchor || !anchors[i].live) continue;
                    for (int n = 0; n < count; ++n) queue.push_back({action.particleKind, static_cast<int>(i)});
                }
            } else if (action.particleKind >= 0) {
                for (int n = 0; n < count; ++n) queue.push_back({action.particleKind, -1});
            } else {
                burst += count;
            }
            break;
        }
        case Action::Kind::Set:
            if (action.target >= 0) Store(&inputs[action.target], action.width, action.value.RunLanes(SourceEnv(&random)));
            break;
        case Action::Kind::SetVar:
            Store(&vars[action.target], action.width, action.value.RunLanes(SourceEnv(&random)));
            break;
        case Action::Kind::SetSource:
            Store(&source[action.target], action.width, action.value.RunLanes(SourceEnv(&random)));
            for (int k = 0; k < action.width; ++k) sourceWrites.push_back(action.target + k);
            break;
        case Action::Kind::Stage: Enter(action.target, random, depth); break;
        case Action::Kind::Effect: effects.push_back({action.effect}); break;
        case Action::Kind::End: ended = true; break;
        case Action::Kind::Stop:
            stopRequested = true;
            break;
        case Action::Kind::StopEmitting:
            stopEmitting = true;
            break;
        case Action::Kind::LetGo: letGo = true; break;
        case Action::Kind::AddAnchor:
            if (action.anchor >= 0) AddAnchors(action.anchor, Count(action.count.Run(SourceEnv(&random))), random);
            break;
        case Action::Kind::Shake: {
            const Env env = SourceEnv(&random);
            const float strength = action.value.Run(env);
            const float time = action.time.Run(env);
            shakes.push_back({strength, time, action.decay.Run(env)});
            break;
        }
        case Action::Kind::Scorch: {
            const Env env = SourceEnv(&random);
            const float radius = action.value.Run(env);
            scorches.push_back({radius, action.alpha.Run(env)});
            break;
        }
        }
    }
}

namespace {

// An anchor placed: at its start (`tick` false: its position, else its spawn's point) or by its tick (none: it stays).
void Place(const Anchor& group, const Env& env, AnchorState& anchor, bool tick) {
    Vector3 at;
    if (group.cylindrical) {
        const float radius = group.radius.Run(env);
        const float angle = group.angle.Run(env);
        at = {lib::Cos(angle) * radius, lib::Sin(angle) * radius, group.z.Run(env)};
    } else if (tick) {
        if (group.positionTick.Empty()) return;
        at = group.positionTick.RunVector(env, {});
    } else {
        const float* spawn = anchor.values + static_cast<int>(AnchorValue::SpawnX);
        at = group.position.RunVector(env, {spawn[0], spawn[1], spawn[2]});
    }
    float* values = anchor.values + static_cast<int>(AnchorValue::PositionX);
    values[0] = at.x;
    values[1] = at.y;
    values[2] = at.z;
}

} // namespace

void State::MakeAnchors(IsaacRandom& random, bool atCreate) {
    for (std::size_t g = 0; g < record->anchors.size(); ++g)
        if (record->anchors[g].atCreate == atCreate)
            AddAnchors(static_cast<int>(g), Count(record->anchors[g].count.Run(SourceEnv(&random))), random);
}

void State::AddAnchors(int group, int count, IsaacRandom& random) {
    const Anchor& spec = record->anchors[group];
    int made = 0;
    for (const AnchorState& anchor : anchors) made += anchor.group == group ? 1 : 0;
    count = std::min(count, kMaxAnchors - made);
    const float size = static_cast<float>(made + count);
    for (AnchorState& anchor : anchors)
        if (anchor.group == group) anchor.values[static_cast<int>(AnchorValue::GroupSize)] = size;
    for (int i = 0; i < count; ++i) {
        const int index = static_cast<int>(anchors.size());
        AnchorState& anchor = anchors.emplace_back();
        anchor.group = group;
        anchor.values[static_cast<int>(AnchorValue::Index)] = static_cast<float>(made + i);
        anchor.values[static_cast<int>(AnchorValue::GroupSize)] = size;
        anchor.vars.assign(spec.varFloats, 0.0f);
        Env env = AnchorEnv(index, &random);
        const Vector3 point = SpawnPoint(spec.spawn, env, random);
        anchor.values[static_cast<int>(AnchorValue::SpawnX)] = point.x;
        anchor.values[static_cast<int>(AnchorValue::SpawnY)] = point.y;
        anchor.values[static_cast<int>(AnchorValue::SpawnZ)] = point.z;
        anchor.values[static_cast<int>(AnchorValue::Ticks)] =
            spec.ticks.Empty() ? -1.0f : static_cast<float>(Count(spec.ticks.Run(env)));
        Update(spec.vars, anchor.vars.data(), env, false, All);
        Place(spec, env, anchor, false);
    }
    source[kSourceValues + group] += static_cast<float>(count);
}

void State::StepAnchors(IsaacRandom& random) {
    std::vector<int> live(record->anchors.size(), 0);
    for (std::size_t i = 0; i < anchors.size(); ++i) {
        AnchorState& anchor = anchors[i];
        if (!anchor.live) continue;
        const Anchor& group = record->anchors[anchor.group];
        float* values = anchor.values;
        values[static_cast<int>(AnchorValue::Age)] += 1.0f;
        float& ticks = values[static_cast<int>(AnchorValue::Ticks)];
        if (ticks > 0.0f) ticks -= 1.0f;
        const Env env = AnchorEnv(static_cast<int>(i), &random);
        Update(group.vars, anchor.vars.data(), env, true, All);
        Place(group, env, anchor, true);
        if (ticks == 0.0f) anchor.live = false;
        else ++live[anchor.group];
    }
    for (std::size_t g = 0; g < live.size(); ++g) source[kSourceValues + g] = static_cast<float>(live[g]);
}

} // namespace effectdata
