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
            if (found == below.keys.end()) below.keys.emplace_back(key, value);
            else MergeOver(found->second, value);
        }
        return;
    }
    if (IsTableArray(below) && IsTableArray(above)) {
        std::vector<bool> merged(below.items.size(), false);
        for (const RawNode& element : above.items) {
            const std::string_view key = MergeKey(element);
            bool matched = false;
            for (std::size_t i = 0; !key.empty() && i < below.items.size(); ++i) {
                if (merged[i] || MergeKey(below.items[i]) != key) continue;
                MergeOver(below.items[i], element);
                merged[i] = true;
                matched = true;
                break;
            }
            if (!matched) below.items.push_back(element);
        }
        return;
    }
    below = above;
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

constexpr std::string_view kSourceNames[kSourceValues] = {
    "size", "height", "growth", "alpha", "param", "base_size", "age", "stage_age", "stage", "carrier_radius",
    "carrier_z", "attached", "distance", "motion.x", "motion.y", "motion.z", "origin.x", "origin.y", "origin.z",
    "count", "index", "total", "wind.x", "wind.y", "wind.z", "wind_strength", "extent.x", "extent.y", "extent.z",
    "turn_cos", "turn_sin", "emitting", "spread_min.x", "spread_min.y", "spread_min.z", "spread_max.x",
    "spread_max.y", "spread_max.z", "side.x", "side.y", "side.z", "up.x", "up.y", "up.z", "carrier_unit",
    "carrier_running", "carrier_dead", "carrier_axis.x", "carrier_axis.y", "carrier_axis.z", "carrier_bounds_radius",
    "carrier_left_foot.x", "carrier_left_foot.y", "carrier_left_foot.z", "carrier_right_foot.x",
    "carrier_right_foot.y", "carrier_right_foot.z", "carrier_axis_y.x", "carrier_axis_y.y", "carrier_axis_y.z",
    "carrier_unit_radius", "sun.r", "sun.g", "sun.b",
};
// The source values a `set` action may write (the [source] table's numbers).
constexpr std::string_view kSettableSourceNames[] = {
    "size", "height", "growth", "alpha", "param", "spread_min.x", "spread_min.y", "spread_min.z", "spread_max.x",
    "spread_max.y", "spread_max.z", "extent.x", "extent.y", "extent.z",
};
constexpr std::string_view kSettableText = "size, height, growth, alpha, param, spread_min.*, spread_max.*, extent.*";
constexpr std::string_view kAnchorNames[kAnchorValues] = {"index", "count", "age", "ticks", "x", "y", "z",
                                                          "spawn.x", "spawn.y", "spawn.z"};
constexpr std::string_view kParticleNames[kParticleValues] = {
    "age", "life", "t", "x", "y", "z", "vx", "vy", "vz", "size", "alpha", "frame", "anchor", "index",
    "spawn.x", "spawn.y", "spawn.z",
};
constexpr std::string_view kParts[] = {
    "effect_point", "head", "above_head", "left_hand", "right_hand", "left_leg", "right_leg", "body",
    "body00", "body01", "body02", "body03", "body04",
};

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

class Reader {
public:
    Reader(const RawNode& root, std::string_view name, std::vector<std::string>* errors, std::int32_t id)
        : root(root), errors(errors) {
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
        ReadVars(root["vars"], {}, &record->vars);
        ReadEmit();
        for (std::size_t i = 0; i < record->stages.size(); ++i) ReadStage(Element("stage", i), record->stages[i]);
        ReadReactions();
        for (std::size_t i = 0; i < record->anchors.size(); ++i) ReadAnchor(Element("anchor", i), static_cast<int>(i));
        for (std::size_t i = 0; i < record->particles.size(); ++i)
            ReadParticle(Element("particle", i), static_cast<int>(i));
        if (record->particles.empty()) Error(root, "no [[particle]]: the effect emits nothing");
        return fatal ? nullptr : record;
    }

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
            Anchor& a = record->anchors.emplace_back();
            if (!Text((*anchor)["name"], &a.name)) Error(*anchor, "an [[anchor]] without its name");
            DeclareVars((*anchor)["vars"], &a.vars, false, true);
        }
        for (const RawNode* particle : Tables("particle")) {
            ParticleKind& kind = record->particles.emplace_back();
            if (!Text((*particle)["name"], &kind.name)) Error(*particle, "a [[particle]] without its name");
            DeclareVars((*particle)["vars"], &kind.vars, true);
        }
        DeclareVars(root["vars"], &record->vars, false);
    }
    // A particle's or an anchor's var named as one of its own values (`p.z`, `a.age`) would never be read: refused.
    void DeclareVars(const RawNode& table, std::vector<Var>* vars, bool particle, bool anchor = false) {
        for (const auto& [name, value] : table.keys) {
            if ((particle && IndexIn(kParticleNames, name) >= 0) || (anchor && IndexIn(kAnchorNames, name) >= 0))
                Error(value, "var '" + name + "' is the name of the " + (particle ? "particle's" : "anchor's") +
                                 " own value " + (particle ? "p." : "a.") + name);
            vars->push_back({name, {}, {}});
        }
        if (particle && vars->size() > static_cast<std::size_t>(kParticleVars))
            Error(table, "a particle has at most " + std::to_string(kParticleVars) + " vars");
    }

    // ---- expressions ----

    Resolved Resolve(std::string_view name, Context context) const {
        auto value = [](Scope scope, int index) {
            return Resolved{Resolved::Kind::Value, scope, static_cast<std::uint16_t>(index), 0.0f};
        };
        auto constant = [](float v) { return Resolved{Resolved::Kind::Constant, Scope::Source, 0, v}; };
        if (name.starts_with("p.")) {
            if (context.particle < 0) return {};
            const std::string_view field = name.substr(2);
            if (const int i = IndexIn(kParticleNames, field); i >= 0) return value(Scope::Particle, i);
            const auto& vars = record->particles[context.particle].vars;
            for (std::size_t i = 0; i < vars.size(); ++i)
                if (vars[i].name == field) return value(Scope::ParticleVar, static_cast<int>(i));
            return {};
        }
        if (name.starts_with("a.")) {
            if (context.anchor < 0) return {};
            const std::string_view field = name.substr(2);
            if (const int i = IndexIn(kAnchorNames, field); i >= 0) return value(Scope::Anchor, i);
            const auto& vars = record->anchors[context.anchor].vars;
            for (std::size_t i = 0; i < vars.size(); ++i)
                if (vars[i].name == field) return value(Scope::AnchorVar, static_cast<int>(i));
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
                if (input->type == InputType::Vector) {
                    const int axis = field == "x" ? 0 : field == "y" ? 1 : field == "z" ? 2 : -1;
                    return axis < 0 ? Resolved{} : value(Scope::Input, input->slot + axis);
                }
                if (input->type == InputType::Enum) {
                    const int index = IndexIn(input->values, field);
                    return index < 0 ? Resolved{} : constant(static_cast<float>(index));
                }
                return {};
            }
        }
        if (const std::size_t dot = name.find('.'); dot != std::string_view::npos) {
            // An anchor group's field by the anchor's number: `ring.angle[i]`.
            const std::string_view group = name.substr(0, dot), field = name.substr(dot + 1);
            for (std::size_t g = 0; g < record->anchors.size() && g < 256; ++g) {
                if (record->anchors[g].name != group) continue;
                Resolved at{Resolved::Kind::AnchorAt, Scope::Anchor, 0, 0.0f};
                at.group = static_cast<std::uint8_t>(g);
                if (const int i = IndexIn(kAnchorNames, field); i >= 0) {
                    at.index = static_cast<std::uint16_t>(i);
                    return at;
                }
                const auto& vars = record->anchors[g].vars;
                for (std::size_t i = 0; i < vars.size(); ++i) {
                    if (vars[i].name != field) continue;
                    at.scope = Scope::AnchorVar;
                    at.index = static_cast<std::uint16_t>(i);
                    return at;
                }
                return {};
            }
        }
        if (const Input* input = record->FindInput(name))
            return input->type == InputType::Vector || input->type == InputType::List ? Resolved{}
                                                                                      : value(Scope::Input, input->slot);
        for (std::size_t i = 0; i < record->vars.size(); ++i)
            if (record->vars[i].name == name) return value(Scope::SourceVar, static_cast<int>(i));
        if (const int i = IndexIn(kSourceNames, name); i >= 0) return value(Scope::Source, i);
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
    void Fields3(const RawNode& node, Context context, Expr* out) {
        if (!node.Exists()) return;
        if (node.kind != RawNode::Kind::Array || node.items.size() != 3) return Error(node, "[x, y, z] expected");
        for (int i = 0; i < 3; ++i) out[i] = Field(node.items[i], context);
    }

    void ReadVars(const RawNode& table, Context context, std::vector<Var>* vars) {
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
                var.start = Field(spec["start"], context);
                var.tick = Field(spec["tick"], context);
            } else {
                var.start = Field(spec, context);
            }
            if (context.particle >= 0 && !var.afterSpawn && ReadsSpawn(var.start))
                Error(spec, "var '" + var.name + "' reads p.spawn before the spawn draws: give it after_spawn = true");
        }
    }
    static bool ReadsSpawn(const Expr& expr) {
        for (const Instruction& i : expr.code)
            if (i.op == Op::Load && i.scope == Scope::Particle && i.index >= static_cast<int>(ParticleValue::SpawnX) &&
                i.index <= static_cast<int>(ParticleValue::SpawnZ))
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
            Known(*node, {"event", "when", "do"});
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
                const int own = IndexIn(kSettableSourceNames, name);
                const int var = IndexOfVar(name);
                if (input && input->type != InputType::Vector && input->type != InputType::Event) {
                    action.target = input->slot;
                } else if (!input && var >= 0) {
                    action.kind = Action::Kind::SetVar;
                    action.target = var;
                } else if (!input && own >= 0) {
                    action.kind = Action::Kind::SetSource;
                    action.target = IndexIn(kSourceNames, name);
                } else {
                    Error(node, "'" + name + "' is no float, bool or enum input, no var, nor " +
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
    int IndexOfVar(std::string_view name) const {
        for (std::size_t i = 0; i < record->vars.size(); ++i)
            if (record->vars[i].name == name) return static_cast<int>(i);
        return -1;
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

    // A spawn shape (`spawn = { shape, radius, min, max, from, to }`); `known`: the table's keys where it is read.
    void ReadSpawn(const RawNode& spawn, Context context, Spawn* out, std::initializer_list<std::string_view> known) {
        Known(spawn, known);
        Integer(spawn["tries"], &out->tries);
        Fields3(spawn["shift"], context, out->shift);
        static constexpr std::string_view kShapes[] = {"point", "box", "ball", "shell", "disc", "ring", "flat_ball",
                                                       "segment"};
        out->shape = static_cast<SpawnShape>(Choice(spawn["shape"], kShapes, 0));
        out->radius = Field(spawn["radius"], context);
        Fields3(spawn["min"], context, out->boxMin);
        Fields3(spawn["max"], context, out->boxMax);
        Fields3(spawn["from"], context, out->from);
        Fields3(spawn["to"], context, out->to);
    }

    void ReadAnchor(const RawNode& node, int group) {
        Known(node, {"name", "count", "spawn", "ticks", "vars", "x", "y", "z", "radius", "angle", "at_create"});
        Anchor& anchor = record->anchors[group];
        const Context context{group, -1};
        anchor.count = Field(node["count"], {});
        if (anchor.count.Empty()) Error(node, "anchor '" + anchor.name + "': no count");
        ReadSpawn(node["spawn"], context, &anchor.spawn,
                  {"shape", "radius", "min", "max", "from", "to", "tries", "shift"});
        Flag(node["at_create"], &anchor.atCreate);
        anchor.ticks = Field(node["ticks"], context);
        ReadVars(node["vars"], context, &anchor.vars);
        anchor.cylindrical = node["radius"].Exists() || node["angle"].Exists();
        if (anchor.cylindrical && (node["x"].Exists() || node["y"].Exists()))
            Error(node, "anchor '" + anchor.name + "': x / y or radius / angle, not both");
        anchor.x = Field(node["x"], context);
        anchor.y = Field(node["y"], context);
        anchor.z = Field(node["z"], context);
        anchor.radius = Field(node["radius"], context);
        anchor.angle = Field(node["angle"], context);
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
        ReadVars(node["vars"], context, &kind.vars);
        static constexpr std::string_view kPicks[] = {"cycle", "random", "first"};
        kind.pick = static_cast<AnchorPick>(Choice(node["pick"], kPicks, 0));
        kind.anchorIndex = Field(node["anchor_index"], context);
        if (!kind.anchorIndex.Empty()) {
            if (kind.anchor < 0) Error(node, "particle '" + kind.name + "': anchor_index needs at");
            // The anchor is chosen after the lifetime and the vars, so those cannot use it.
            bool early = ReadsAnchor(kind.lifetime);
            for (const Var& var : kind.vars) early = early || (!var.afterSpawn && ReadsAnchor(var.start));
            if (early)
                Error(node, "particle '" + kind.name + "': with anchor_index, the lifetime and the vars before the "
                            "spawn read no a.*");
        }
        static constexpr std::string_view kSpaces[] = {"world", "source", "anchor"};
        kind.space = static_cast<Space>(Choice(node["space"], kSpaces, 0));
        if (kind.space == Space::Anchor && kind.anchor < 0) Error(node, "particle '" + kind.name + "': space anchor needs at");

        const RawNode& spawn = node["spawn"];
        ReadSpawn(spawn, context, &kind.spawn,
                  {"shape", "radius", "min", "max", "from", "to", "tries", "shift", "offset", "on_ground"});
        Fields3(spawn["offset"], context, kind.offset);
        kind.onGround = Field(spawn["on_ground"], context);
        const RawNode& position = node["position"];
        if (position.kind == RawNode::Kind::Table) {
            Known(position, {"start", "tick"});
            Fields3(position["start"], context, kind.position);
            Fields3(position["tick"], context, kind.positionTick);
        } else {
            Fields3(position, context, kind.position);
        }

        Fields3(node["velocity"], context, kind.velocity);
        kind.speed = Field(node["speed"], context);
        kind.drag = Field(node["drag"], context);
        Fields3(node["gravity"], context, kind.gravity);
        kind.wind = Field(node["wind"], context);
        kind.moves = !kind.speed.Empty() || !kind.wind.Empty();
        for (int i = 0; i < 3; ++i) kind.moves = kind.moves || !kind.velocity[i].Empty() || !kind.gravity[i].Empty();

        const RawNode& size = node["size"];
        if (size.kind == RawNode::Kind::Table) {
            Known(size, {"start", "tick", "growth"});
            kind.sizeStart = Field(size["start"], context);
            kind.sizeTick = Field(size["tick"], context);
            kind.growth = Field(size["growth"], context);
            if (!kind.sizeTick.Empty() && !kind.growth.Empty())
                Error(size, "particle '" + kind.name + "': size tick or growth, not both");
        } else {
            kind.sizeStart = Field(size, context);
        }

        const RawNode& color = node["color"];
        if (color.kind == RawNode::Kind::String) {
            std::uint32_t rgb = 0;
            const char* first = color.text.data();
            const auto [end, failed] = std::from_chars(first, first + color.text.size(), rgb, 16);
            if (color.text.size() != 6 || failed != std::errc() || end != first + color.text.size())
                Error(color, "particle '" + kind.name + "': color is \"RRGGBB\" or [r, g, b]");
            kind.rgb = rgb;
        } else if (color.Exists()) {
            Fields3(color, context, kind.rgbTick);
        }
        const RawNode& alpha = node["alpha"];
        if (alpha.kind == RawNode::Kind::Table) {
            Known(alpha, {"start", "tick"});
            kind.alphaStart = Field(alpha["start"], context);
            kind.alphaTick = Field(alpha["tick"], context);
        } else {
            kind.alphaStart = Field(alpha, context);
        }
        const RawNode& frame = node["frame"];
        if (frame.kind == RawNode::Kind::Table) {
            Known(frame, {"start", "delay", "mode", "tick"});
            kind.frameStart = Field(frame["start"], context);
            kind.frameDelay = Field(frame["delay"], context);
            kind.frameTick = Field(frame["tick"], context);
            static constexpr std::string_view kModes[] = {"loop", "hold", "die", "free"};
            kind.frameMode = static_cast<FrameMode>(Choice(frame["mode"], kModes, 0));
        } else {
            kind.frameStart = Field(frame, context);
        }
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
    // A particle's `source = { ... }`: the source's values, the effect's vars and (`a.<var>`) its anchor's vars it sets
    // at each update, in the file's order.
    void ReadWrites(const RawNode& writes, ParticleKind& kind, Context context) {
        if (!writes.Exists()) return;
        if (writes.kind != RawNode::Kind::Table)
            return Error(writes, "particle '" + kind.name + "': source is a table of the values it sets");
        using To = ParticleKind::Write::To;
        for (const auto& [name, value] : writes.keys) {
            if (name == "a" && value.kind == RawNode::Kind::Table) {
                const int group = kind.anchor;
                for (const auto& [var, expr] : value.keys) {
                    int index = -1;
                    if (group >= 0)
                        for (std::size_t i = 0; i < record->anchors[group].vars.size(); ++i)
                            if (record->anchors[group].vars[i].name == var) index = static_cast<int>(i);
                    if (index < 0) {
                        Error(expr, "particle '" + kind.name + "': source sets a.<var>, a var of its anchor (at), not 'a." +
                                        var + "'");
                        continue;
                    }
                    kind.writes.push_back({To::AnchorVar, index, Field(expr, context)});
                }
                continue;
            }
            if (const int var = IndexOfVar(name); var >= 0) {
                kind.writes.push_back({To::Var, var, Field(value, context)});
                continue;
            }
            if (IndexIn(kSettableSourceNames, name) < 0) {
                Error(value, "particle '" + kind.name + "': source sets " + std::string(kSettableText) +
                                 ", an effect var or a.<var>, not '" + name + "'");
                continue;
            }
            kind.writes.push_back({To::Source, IndexIn(kSourceNames, name), Field(value, context)});
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
    return Reader(root, name, errors, id).Run();
}

// ---- The state machine ---------------------------------------------------------------------------------------------

namespace {

int Count(float value) {
    if (!(value > 0.0f)) return 0;
    if (value > static_cast<float>(kMaxAnchors)) return kMaxAnchors;
    return static_cast<int>(std::lround(value));
}

float SourceValueOf(const Env& env, SourceValue which) {
    const float* source = env.scopes[static_cast<int>(Scope::Source)];
    return source ? source[static_cast<int>(which)] : 0.0f;
}

} // namespace

Vector3 SpawnPoint(const Spawn& spawn, const Env& env, IsaacRandom& random) {
    constexpr float kTwoPi = kPi + kPi;
    Vector3 shifted;
    const Vector3* shift = nullptr;
    if (!spawn.shift[0].Empty() || !spawn.shift[1].Empty() || !spawn.shift[2].Empty()) {
        shifted = {spawn.shift[0].Run(env), spawn.shift[1].Run(env), spawn.shift[2].Run(env)};
        shift = &shifted;
    }
    switch (spawn.shape) {
    case SpawnShape::Point: return {};
    case SpawnShape::Box: {
        auto axis = [&](int i, SourceValue low, SourceValue high) {
            const float from = spawn.boxMin[i].Run(env, SourceValueOf(env, low));
            const float to = spawn.boxMax[i].Run(env, SourceValueOf(env, high));
            return random.Range(from, to);
        };
        const float x = axis(0, SourceValue::SpreadMinX, SourceValue::SpreadMaxX);
        const float y = axis(1, SourceValue::SpreadMinY, SourceValue::SpreadMaxY);
        const float z = axis(2, SourceValue::SpreadMinZ, SourceValue::SpreadMaxZ);
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
        const Vector3 from = {spawn.from[0].Run(env), spawn.from[1].Run(env), spawn.from[2].Run(env)};
        const Vector3 to = {spawn.to[0].Run(env, SourceValueOf(env, SourceValue::ExtentX)),
                            spawn.to[1].Run(env, SourceValueOf(env, SourceValue::ExtentY)),
                            spawn.to[2].Run(env, SourceValueOf(env, SourceValue::ExtentZ))};
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
    vars.assign(record->vars.size(), 0.0f);
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
        for (std::size_t i = 0; i < record->vars.size(); ++i) vars[i] = record->vars[i].tick.Run(env, vars[i]);
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
            if (action.target >= 0) inputs[action.target] = action.value.Run(env);
            break;
        case Action::Kind::SetVar: vars[action.target] = action.value.Run(env); break;
        default: break;   // refused at the read
        }
    }
}

bool State::Holds(const Reaction& reaction, IsaacRandom& random) const {
    return reaction.when.Empty() || reaction.when.Run(SourceEnv(&random)) != 0.0f;
}

void State::Create(IsaacRandom& random) {
    const Env env = SourceEnv(&random);
    for (std::size_t i = 0; i < record->vars.size(); ++i)
        if (record->vars[i].atCreate) vars[i] = record->vars[i].start.Run(env);
    MakeAnchors(random, true);
}

void State::Start(IsaacRandom& random) {
    started = true;
    const Env env = SourceEnv(&random);
    for (std::size_t i = 0; i < record->vars.size(); ++i)
        if (!record->vars[i].atCreate) vars[i] = record->vars[i].start.Run(env);
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
            if (action.target >= 0) inputs[action.target] = action.value.Run(SourceEnv(&random));
            break;
        case Action::Kind::SetVar: vars[action.target] = action.value.Run(SourceEnv(&random)); break;
        case Action::Kind::SetSource:
            source[action.target] = action.value.Run(SourceEnv(&random));
            sourceWrites.push_back(action.target);
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

void Place(const Anchor& group, const Env& env, AnchorState& anchor) {
    float* values = anchor.values;
    float x, y;
    if (group.cylindrical) {
        const float radius = group.radius.Run(env);
        const float angle = group.angle.Run(env);
        x = lib::Cos(angle) * radius;
        y = lib::Sin(angle) * radius;
    } else {
        x = group.x.Run(env);
        y = group.y.Run(env);
    }
    const float z = group.z.Run(env);
    values[static_cast<int>(AnchorValue::X)] = x;
    values[static_cast<int>(AnchorValue::Y)] = y;
    values[static_cast<int>(AnchorValue::Z)] = z;
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
        anchor.vars.assign(spec.vars.size(), 0.0f);
        Env env = AnchorEnv(index, &random);
        const Vector3 point = SpawnPoint(spec.spawn, env, random);
        anchor.values[static_cast<int>(AnchorValue::SpawnX)] = point.x;
        anchor.values[static_cast<int>(AnchorValue::SpawnY)] = point.y;
        anchor.values[static_cast<int>(AnchorValue::SpawnZ)] = point.z;
        anchor.values[static_cast<int>(AnchorValue::Ticks)] =
            spec.ticks.Empty() ? -1.0f : static_cast<float>(Count(spec.ticks.Run(env)));
        for (std::size_t v = 0; v < spec.vars.size(); ++v) anchor.vars[v] = spec.vars[v].start.Run(env);
        Place(spec, env, anchor);
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
        for (std::size_t v = 0; v < group.vars.size(); ++v) anchor.vars[v] = group.vars[v].tick.Run(env, anchor.vars[v]);
        Place(group, env, anchor);
        if (ticks == 0.0f) anchor.live = false;
        else ++live[anchor.group];
    }
    for (std::size_t g = 0; g < live.size(); ++g) source[kSourceValues + g] = static_cast<float>(live[g]);
}

} // namespace effectdata
