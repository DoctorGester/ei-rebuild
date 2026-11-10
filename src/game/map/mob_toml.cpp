// A .mob's chunks but its scripts as TOML and back (mob_toml.h; docs/notes/mods.md, "Map objects as TOML"). Each record
// type lists its fields once (`Fields(record, visit)`), which the writer, the reader and the comparison all walk.
#include "game/map/mob_toml.h"

#include "game/data/resource.h"
#include "lib/crt.h"
#include "lib/text_case.h"
#include "lib/toml.h"
#include "platform/platform.h"

#include <array>
#include <bit>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace mobtoml {

namespace {

using mapspec::AlertMode;
using mapspec::Behaviour;
using mapspec::BehaviourType;
using mapspec::Patrol;
using mapspec::Reaction;
using mapspec::Spec;
using mapspec::SpecKind;
using mapspec::Waypoint;
using unitrecord::UnitRecordStats;

constexpr int kBehaviourCount = static_cast<int>(std::extent_v<decltype(mapspec::UnitSpec::behaviour)>);

using Images = std::vector<std::pair<std::string, std::string>>;

// ---- Fields of another type than their own --------------------------------------------------------------------------

// A byte that holds 0 or 1: false or true.
struct Flag {
    std::uint8_t& value;
};
// One bit of a flag set, as true or false.
struct Bit {
    std::uint32_t& value;
    std::uint32_t bit = 0;
};
// An unsigned mask, in hexadecimal.
struct Hex {
    std::uint32_t& value;
};
// A code written by its name where it has one (`names`), else as its number.
template <class E>
struct Named {
    E& value;
    std::span<const std::pair<E, std::string_view>> names;
};

constexpr std::pair<BehaviourType, std::string_view> kBehaviourTypes[] = {
    {BehaviourType::None, "none"},     {BehaviourType::Guard, "guard"}, {BehaviourType::Patrol, "patrol"},
    {BehaviourType::Sentry, "sentry"}, {BehaviourType::Party, "party"}, {BehaviourType::GuardAlarm, "guard_alarm"},
};
constexpr std::pair<Reaction, std::string_view> kReactions[] = {
    {Reaction::Fight, "fight"},
    {Reaction::Revenge, "revenge"},
    {Reaction::Watch, "watch"},
    {Reaction::WatchFearful, "watch_fearful"},
};
constexpr std::pair<AlertMode, std::string_view> kAlertModes[] = {
    {AlertMode::OnSight, "on_sight"},
    {AlertMode::OnSeen, "on_seen"},
    {AlertMode::OnSightOrMode, "on_sight_or_mode"},
};
constexpr std::pair<SpecKind, std::string_view> kKinds[] = {
    {SpecKind::Object, "object"}, {SpecKind::Unit, "unit"},   {SpecKind::Flame, "flame"},
    {SpecKind::Trap, "trap"},     {SpecKind::Lever, "lever"}, {SpecKind::Light, "light"},
    {SpecKind::Sound, "sound"},   {SpecKind::Particle, "particle"},
};

// A new record of `kind` with its defaults (null for a kind no .mob holds).
std::unique_ptr<Spec> NewSpec(SpecKind kind) {
    switch (kind) {
    case SpecKind::Object: return std::make_unique<mapspec::ObjectSpec>();
    case SpecKind::Unit: return std::make_unique<mapspec::UnitSpec>();
    case SpecKind::Flame: return std::make_unique<mapspec::FlameSpec>();
    case SpecKind::Trap: return std::make_unique<mapspec::TrapSpec>();
    case SpecKind::Lever: return std::make_unique<mapspec::LeverSpec>();
    case SpecKind::Light: return std::make_unique<mapspec::LightSpec>();
    case SpecKind::Sound: return std::make_unique<mapspec::SoundSpec>();
    case SpecKind::Particle: return std::make_unique<mapspec::ParticleSpec>();
    default: return nullptr;
    }
}

// ---- The field lists (declared here, defined below) --------------------------------------------------------------

template <class V> void Fields(TrapCircle& circle, V& visit);
template <class V> void Fields(LockKey& lock, V& visit);
template <class V> void Fields(Waypoint& waypoint, V& visit);
template <class V> void Fields(Patrol& patrol, V& visit);
template <class V> void Fields(Behaviour& behaviour, V& visit);
template <class V> void Fields(UnitRecordStats& stats, V& visit);
template <class V> void Fields(mapspec::MobWeather& weather, V& visit);
template <class V> void Fields(Spec& record, V& visit);

class ImageVisit;
// A type with a field list: a nested record (an inline table).
template <class T>
concept HasFields = requires(T& record, ImageVisit& visit) { Fields(record, visit); };

// ---- Values as TOML text ---------------------------------------------------------------------------------------------

std::string Text(std::int32_t value) { return toml::Int(value); }
std::string Text(std::uint32_t value) { return toml::Int(value); }
std::string Text(std::int16_t value) { return toml::Int(value); }
std::string Text(std::uint16_t value) { return toml::Int(value); }
std::string Text(std::uint8_t value) { return toml::Int(value); }
std::string Text(float value) { return toml::Float(value); }
std::string Text(const std::string& value) { return toml::String(value); }
std::string Text(const Vector2& value) { return toml::Array({Text(value.x), Text(value.y)}); }
std::string Text(const Vector3& value) { return toml::Array({Text(value.x), Text(value.y), Text(value.z)}); }
std::string Text(const Quaternion& value) {
    return toml::Array({Text(value.w), Text(value.x), Text(value.y), Text(value.z)});
}
std::string Text(Flag field) { return toml::Bool(field.value != 0); }
std::string Text(Bit field) { return toml::Bool((field.value & field.bit) != 0); }
std::string Text(Hex field) { return toml::Hex(field.value); }
template <class E>
std::string Text(Named<E> field) {
    for (const auto& [value, name] : field.names)
        if (value == field.value) return toml::String(name);
    return toml::Int(static_cast<std::int64_t>(field.value));
}
template <class T, std::size_t N> std::string Text(const T (&values)[N]);
template <class T> std::string Text(const std::vector<T>& values);
template <HasFields T> std::string Text(const T& record);

// ---- Values read from TOML -------------------------------------------------------------------------------------------

// Each true when `value` is of a type the field takes (toml::Read's rules), else the field keeps what it had.
bool ReadValue(const toml::Value& value, std::int32_t& field) { return toml::Read(value, &field); }
bool ReadValue(const toml::Value& value, std::uint32_t& field) { return toml::Read(value, &field); }
bool ReadValue(const toml::Value& value, std::uint8_t& field) { return toml::Read(value, &field); }
bool ReadValue(const toml::Value& value, float& field) { return toml::Read(value, &field); }
bool ReadValue(const toml::Value& value, std::string& field) { return toml::Read(value, &field); }
template <class T>
    requires std::is_same_v<T, std::int16_t> || std::is_same_v<T, std::uint16_t>
bool ReadValue(const toml::Value& value, T& field) {
    std::int32_t read = 0;
    if (!toml::Read(value, &read)) return false;
    field = static_cast<T>(read);
    return true;
}
// The vectors: an array of their components, all of them.
bool ReadValue(const toml::Value& value, Vector2& field) {
    Vector2 read;
    if (value.Size() != 2 || !ReadValue(value.At(0), read.x) || !ReadValue(value.At(1), read.y)) return false;
    field = read;
    return true;
}
bool ReadValue(const toml::Value& value, Vector3& field) {
    Vector3 read;
    if (value.Size() != 3 || !ReadValue(value.At(0), read.x) || !ReadValue(value.At(1), read.y) ||
        !ReadValue(value.At(2), read.z))
        return false;
    field = read;
    return true;
}
bool ReadValue(const toml::Value& value, Quaternion& field) {
    Quaternion read;
    if (value.Size() != 4 || !ReadValue(value.At(0), read.w) || !ReadValue(value.At(1), read.x) ||
        !ReadValue(value.At(2), read.y) || !ReadValue(value.At(3), read.z))
        return false;
    field = read;
    return true;
}
bool ReadValue(const toml::Value& value, Flag field) {
    if (value.IsBool()) field.value = value.Bool() ? 1 : 0;
    else if (value.IsInt()) field.value = static_cast<std::uint8_t>(value.Int());
    else return false;
    return true;
}
bool ReadValue(const toml::Value& value, Bit field) {
    if (!value.IsBool()) return false;
    if (value.Bool()) field.value |= field.bit;
    else field.value &= ~field.bit;
    return true;
}
bool ReadValue(const toml::Value& value, Hex field) { return toml::Read(value, &field.value); }
template <class E>
bool ReadValue(const toml::Value& value, Named<E> field) {
    if (value.IsString()) {
        for (const auto& [code, name] : field.names)
            if (name == value.String()) {
                field.value = code;
                return true;
            }
        return false;
    }
    std::int32_t read = 0;
    if (!toml::Read(value, &read)) return false;
    field.value = static_cast<E>(read);
    return true;
}
template <class T, std::size_t N> bool ReadValue(const toml::Value& value, T (&field)[N]);
template <class T> bool ReadValue(const toml::Value& value, std::vector<T>& field);
template <HasFields T> bool ReadValue(const toml::Value& value, T& record);

// ---- Values as bytes (the comparison's) ------------------------------------------------------------------------------

template <class T>
    requires std::is_arithmetic_v<T> || std::is_enum_v<T>
void Image(std::string& out, T value) {
    const auto bytes = std::bit_cast<std::array<char, sizeof(T)>>(value);
    out.append(bytes.data(), bytes.size());
}
void Image(std::string& out, const std::string& value) {
    Image(out, static_cast<std::uint32_t>(value.size()));
    out += value;
}
void Image(std::string& out, const Vector2& value) {
    Image(out, value.x);
    Image(out, value.y);
}
void Image(std::string& out, const Vector3& value) {
    Image(out, value.x);
    Image(out, value.y);
    Image(out, value.z);
}
void Image(std::string& out, const Quaternion& value) {
    Image(out, value.w);
    Image(out, value.x);
    Image(out, value.y);
    Image(out, value.z);
}
void Image(std::string& out, Flag field) { Image(out, field.value); }
void Image(std::string& out, Bit field) { Image(out, field.value); }
void Image(std::string& out, Hex field) { Image(out, field.value); }
template <class E>
void Image(std::string& out, Named<E> field) {
    Image(out, field.value);
}
template <class T, std::size_t N> void Image(std::string& out, const T (&values)[N]);
template <class T> void Image(std::string& out, const std::vector<T>& values);
template <HasFields T> void Image(std::string& out, const T& record);

// ---- The visits ------------------------------------------------------------------------------------------------------

// Each field's image by its key.
class ImageVisit {
public:
    template <class T>
    void operator()(std::string_view key, T&& field) {
        std::string image;
        Image(image, field);
        images.emplace_back(std::string(key), std::move(image));
    }

    Images images;
};

// Each field as a (key, value text) pair.
class PairVisit {
public:
    template <class T>
    void operator()(std::string_view key, T&& field) {
        pairs.emplace_back(std::string(key), Text(field));
    }

    std::vector<std::pair<std::string, std::string>> pairs;
};

// Each field written whose image differs from the default record's (`defaults`: the same type's images, in the same
// order), and "id" always; `skip` left out (written apart).
class ChangedVisit {
public:
    ChangedVisit(toml::Writer& writer, Images defaults, std::string_view skip)
        : writer(writer), defaults(std::move(defaults)), skip(skip) {}

    template <class T>
    void operator()(std::string_view key, T&& field) {
        std::string image;
        Image(image, field);
        const bool same = next < defaults.size() && defaults[next].second == image;
        ++next;
        if (key == skip || (same && key != "id")) return;
        writer.Pair(key, Text(field));
    }

private:
    toml::Writer& writer;
    Images defaults;
    std::string_view skip;
    std::size_t next = 0;
};

// Finds the field of one key and reads the value into it.
class KeyVisit {
public:
    KeyVisit(std::string_view key, const toml::Value& value) : key(key), value(value) {}

    template <class T>
    void operator()(std::string_view name, T&& field) {
        if (found || name != key) return;
        found = true;
        read = ReadValue(value, field);
    }

    std::string_view key;
    toml::Value value;
    bool found = false;   // the record has the key
    bool read = false;    // and the value was of a type it takes
};

// Each key of the table `value` read into the record's field of that name, but those of `others`; false when `value`
// is not a table, or a key is unknown or of a type its field does not take (`bad`: that key).
template <class Record>
bool ReadKeys(const toml::Value& value, Record& record, std::string* bad = nullptr,
              std::span<const std::string_view> others = {}) {
    if (!value.IsTable()) return false;
    bool all = true;
    for (int k = 0; k < value.Size(); ++k) {
        bool other = false;
        for (std::string_view name : others) other = other || name == value.KeyAt(k);
        if (other) continue;
        KeyVisit visit(value.KeyAt(k), value.ValueAt(k));
        Fields(record, visit);
        if (visit.found && visit.read) continue;
        if (bad && all) *bad = std::string(value.KeyAt(k)) + (visit.found ? " has a value it does not take" : " is unknown");
        all = false;
    }
    return all;
}

// ---- The generic values ----------------------------------------------------------------------------------------------

template <class T, std::size_t N>
std::string Text(const T (&values)[N]) {
    std::vector<std::string> items;
    for (const T& value : values) items.push_back(Text(value));
    return toml::Array(items);
}
template <class T>
std::string Text(const std::vector<T>& values) {
    std::vector<std::string> items;
    for (const T& value : values) items.push_back(Text(value));
    return toml::Array(items);
}
template <HasFields T>
std::string Text(const T& record) {
    PairVisit visit;
    Fields(const_cast<T&>(record), visit);
    return toml::InlineTable(visit.pairs);
}

// At most `N` elements, the rest zero.
template <class T, std::size_t N>
bool ReadValue(const toml::Value& value, T (&field)[N]) {
    if (!value.IsArray() || value.Size() > static_cast<int>(N)) return false;
    T read[N] = {};
    for (int i = 0; i < value.Size(); ++i)
        if (!ReadValue(value.At(i), read[i])) return false;
    for (std::size_t i = 0; i < N; ++i) {
        if constexpr (std::is_array_v<T>) {
            for (std::size_t k = 0; k < std::extent_v<T>; ++k) field[i][k] = read[i][k];
        } else {
            field[i] = read[i];
        }
    }
    return true;
}
template <class T>
bool ReadValue(const toml::Value& value, std::vector<T>& field) {
    if (!value.IsArray()) return false;
    std::vector<T> read(static_cast<std::size_t>(value.Size()));
    for (int i = 0; i < value.Size(); ++i)
        if (!ReadValue(value.At(i), read[static_cast<std::size_t>(i)])) return false;
    field = std::move(read);
    return true;
}
// A nested record: the keys given, the others its type's defaults.
template <HasFields T>
bool ReadValue(const toml::Value& value, T& record) {
    T read{};
    if (!ReadKeys(value, read)) return false;
    record = std::move(read);
    return true;
}

template <class T, std::size_t N>
void Image(std::string& out, const T (&values)[N]) {
    for (const T& value : values) Image(out, value);
}
template <class T>
void Image(std::string& out, const std::vector<T>& values) {
    Image(out, static_cast<std::uint32_t>(values.size()));
    for (const T& value : values) Image(out, value);
}
template <HasFields T>
void Image(std::string& out, const T& record) {
    ImageVisit visit;
    Fields(const_cast<T&>(record), visit);
    for (const auto& [key, image] : visit.images) out += image;
}

// ---- The field lists -------------------------------------------------------------------------------------------------

template <class V>
void Fields(TrapCircle& circle, V& visit) {
    visit("x", circle.x);
    visit("y", circle.y);
    visit("radius", circle.radius);
}

template <class V>
void Fields(LockKey& lock, V& visit) {
    visit("mask", Hex{lock.mask});
    visit("id", lock.id);
    visit("level", lock.level);
}

template <class V>
void Fields(Waypoint& waypoint, V& visit) {
    visit("position", waypoint.position);
    visit("time", waypoint.time);
    visit("look_wait", waypoint.lookWait);
    visit("stop_first", waypoint.stopFirst);
}

template <class V>
void Fields(Patrol& patrol, V& visit) {
    visit("position", patrol.position);
    visit("waypoints", patrol.waypoints);
}

// The chunks' values (alarmDelay has none: it keeps its default).
template <class V>
void Fields(Behaviour& behaviour, V& visit) {
    visit("type", Named<BehaviourType>{behaviour.type, kBehaviourTypes});
    visit("alarm", Named<Reaction>{behaviour.alarm, kReactions});
    visit("alert_mode", Named<AlertMode>{behaviour.alertMode, kAlertModes});
    visit("alarm_kind", behaviour.alarmKind);
    visit("enabled", behaviour.enabled);
    visit("guard", behaviour.guard);
    visit("guard_radius", behaviour.guardRadius);
    visit("call_radius", behaviour.callRadius);
    visit("loop", behaviour.loop);
    visit("always_updated", Bit{behaviour.flags, static_cast<std::uint32_t>(BehaviourFlag::AlwaysUpdated)});
    visit("unread", behaviour.unreadChunk);
    visit("patrol", behaviour.patrols);
}

template <class V>
void Fields(UnitRecordStats& stats, V& visit) {
    visit("hit_points", stats.hitPoints);
    visit("max_hit_points", stats.maxHitPoints);
    visit("mana", stats.mana);
    visit("max_mana", stats.maxMana);
    visit("speed", stats.speed);
    visit("rate", stats.rate);
    visit("mode_speeds", stats.modeSpeeds);
    visit("sight_cone", stats.sightCone);
    visit("peripheral_range", stats.peripheralRange);
    visit("side_cone", stats.sideCone);
    visit("reach_height_scale", stats.reachHeightScale);
    visit("size_class", stats.sizeClass);
    visit("size_class_small", stats.sizeClassSmall);
    visit("reach", stats.reach);
    visit("attack", stats.attack);
    visit("defence", stats.defence);
    visit("combat_speed", stats.combatSpeed);
    visit("damage", stats.damage);
    visit("damage_spread", stats.damageSpread);
    visit("damage_weights", stats.damageWeights);
    visit("protection_scale", stats.protectionScale);
    visit("perception", stats.perception);
    visit("conspicuity", stats.conspicuity);
    visit("attributes", stats.attributes);
    visit("unread_after_sizes", stats.unreadAfterSizes);
    visit("unread_after_weights", stats.unreadAfterWeights);
    visit("unread_after_protection", stats.unreadAfterProtection);
    visit("unread_tail", stats.unreadTail);
}

template <class V>
void Fields(mapspec::MobWeather& weather, V& visit) {
    visit("wind", weather.wind);
    visit("wind_strength", weather.windStrength);
    visit("time", weather.time);
}

// The fields of the object records' own chunks (B0xx).
template <class V>
void ObjectFields(mapspec::ObjectSpec& spec, V& visit) {
    visit("id", spec.id);
    visit("name", spec.name);
    visit("model", spec.model);
    visit("texture", spec.texture);
    visit("primary_texture", spec.primaryTexture);
    visit("skin_texture", spec.skinTexture);
    visit("parent_iname", spec.parentIname);
    visit("parent_id", spec.parentId);
    visit("position", spec.position);
    visit("rotation", spec.rotation);
    visit("scale", spec.scale);
    visit("side", spec.side);
    visit("player", spec.player);
    visit("shadow", Flag{spec.shadow});
    visit("visible", Flag{spec.visible});
    visit("parts", spec.parts);
    visit("comment", spec.comment);
}

// A record's fields by its kind: those its chunks set (a unit's statistics and behaviour records apart).
template <class V>
void Fields(Spec& record, V& visit) {
    using List = unitrecord::UnitRecord::List;
    switch (record.kind) {
    case SpecKind::Object: ObjectFields(static_cast<mapspec::ObjectSpec&>(record), visit); break;
    case SpecKind::Flame: {
        auto& spec = static_cast<mapspec::FlameSpec&>(record);
        ObjectFields(spec, visit);
        visit("volume", spec.volume);
        visit("offset", spec.offset);
        visit("sound", spec.sound);
        break;
    }
    case SpecKind::Trap: {
        auto& spec = static_cast<mapspec::TrapSpec&>(record);
        ObjectFields(spec, visit);
        visit("spell", spec.trap.spell);
        visit("sides", Hex{spec.trap.sides});
        visit("period", spec.trap.period);
        visit("single_target", Flag{spec.trap.singleTarget});
        visit("area", spec.trap.area);
        visit("points", spec.trap.points);
        break;
    }
    case SpecKind::Lever: {
        auto& spec = static_cast<mapspec::LeverSpec&>(record);
        ObjectFields(spec, visit);
        visit("lock", spec.lever.lock);
        visit("state", spec.lever.state);
        visit("state_count", spec.lever.stateCount);
        visit("cycling", Flag{spec.lever.cycling});
        visit("direct_use_blocked", Flag{spec.lever.directUseBlocked});
        visit("keeps_map_flag", Flag{spec.lever.keepsMapFlag});
        break;
    }
    case SpecKind::Unit: {
        auto& spec = static_cast<mapspec::UnitSpec&>(record);
        ObjectFields(spec, visit);
        visit("prototype", spec.prototype);
        visit("apply_stats", spec.record.applyStats);
        visit("carried", spec.record.lists[List::Carried]);
        visit("quick_items", spec.record.lists[List::QuickItems]);
        visit("spells", spec.record.lists[List::Spells]);
        visit("weapons", spec.record.lists[List::Weapons]);
        visit("armour", spec.record.lists[List::Armour]);
        break;
    }
    case SpecKind::Light: {
        auto& spec = static_cast<mapspec::LightSpec&>(record);
        visit("id", spec.id);
        visit("name", spec.name);
        visit("position", spec.position);
        visit("colour", spec.colour);
        visit("radius", spec.radius);
        visit("shadows", Flag{spec.shadows});
        visit("comment", spec.comment);
        break;
    }
    case SpecKind::Sound: {
        auto& spec = static_cast<mapspec::SoundSpec&>(record);
        visit("id", spec.id);
        visit("name", spec.name);
        visit("position", spec.position);
        visit("sounds", spec.sounds);
        visit("volume", spec.volume);
        visit("radius", spec.radius);
        visit("min_interval", spec.minInterval);
        visit("max_interval", spec.maxInterval);
        visit("ambient", Flag{spec.ambient});
        visit("unread", Flag{spec.unreadChunk});
        visit("comment", spec.comment);
        break;
    }
    case SpecKind::Particle: {
        auto& spec = static_cast<mapspec::ParticleSpec&>(record);
        visit("id", spec.id);
        visit("name", spec.name);
        visit("position", spec.position);
        visit("type", spec.type);
        visit("scale", spec.scale);
        visit("comment", spec.comment);
        break;
    }
    default: break;
    }
}

template <class Record>
Images ImagesOf(Record& record) {
    ImageVisit visit;
    Fields(record, visit);
    return visit.images;
}

// The fields of `record` that differ from `defaults`' (and its id) written.
template <class Record>
void WriteChanged(toml::Writer& writer, Record& record, Record& defaults, std::string_view skip = {}) {
    ChangedVisit visit(writer, ImagesOf(defaults), skip);
    Fields(record, visit);
}

// ---- Writing ---------------------------------------------------------------------------------------------------------

void WriteUnitParts(toml::Writer& writer, mapspec::UnitSpec& unit, mapspec::UnitSpec& defaults) {
    if (ImagesOf(unit.record.stats) != ImagesOf(defaults.record.stats)) {
        writer.Table("object.stats");
        WriteChanged(writer, unit.record.stats, defaults.record.stats);
    }
    for (int i = 0; i < unit.behaviours && i < kBehaviourCount; ++i) {
        writer.ArrayTable("object.behaviour");
        Behaviour& behaviour = unit.behaviour[i];
        WriteChanged(writer, behaviour, defaults.behaviour[i], "patrol");
        for (Patrol& patrol : behaviour.patrols) {
            writer.ArrayTable("object.behaviour.patrol");
            writer.Pair("position", Text(patrol.position));
            std::vector<std::string> waypoints;
            for (const Waypoint& waypoint : patrol.waypoints) waypoints.push_back(Text(waypoint));
            writer.Pair("waypoints", toml::Array(waypoints, true));
        }
    }
}

// ---- Reading ---------------------------------------------------------------------------------------------------------

void Warn(const std::string& path, int line, const std::string& what) {
    game::Warning("map objects: %s line %d: %s", path, line, what);
}

// A unit's behaviour records: as many as the array has (at most five).
void ReadBehaviours(const toml::Value& list, mapspec::UnitSpec& unit, const std::string& path) {
    if (!list.IsArray()) {
        Warn(path, list.Line(), "behaviour is not an array of tables");
        return;
    }
    if (list.Size() > kBehaviourCount) Warn(path, list.Line(), "more than five behaviour records: the rest left out");
    unit.behaviours = list.Size() < kBehaviourCount ? list.Size() : kBehaviourCount;
    for (int i = 0; i < unit.behaviours; ++i) {
        std::string bad;
        if (!ReadKeys(list.At(i), unit.behaviour[i], &bad)) Warn(path, list.At(i).Line(), "behaviour: " + bad);
    }
}

std::unique_ptr<Spec> ReadRecord(const toml::Value& item, const std::string& path) {
    std::string kindName;
    if (!toml::Read(item["kind"], &kindName)) {
        Warn(path, item.Line(), "a record without its kind");
        return nullptr;
    }
    std::unique_ptr<Spec> spec;
    for (const auto& [kind, name] : kKinds)
        if (name == kindName) spec = NewSpec(kind);
    if (!spec) {
        Warn(path, item.Line(), "unknown kind " + kindName);
        return nullptr;
    }
    constexpr std::string_view kApart[] = {"kind", "stats", "behaviour"};
    std::string bad;
    if (!ReadKeys(item, *spec, &bad, kApart)) Warn(path, item.Line(), bad);
    const bool unit = spec->kind == SpecKind::Unit;
    if (item["stats"].Exists() || item["behaviour"].Exists()) {
        if (!unit) {
            Warn(path, item.Line(), "stats and behaviour are a unit's");
        } else {
            auto& unitSpec = static_cast<mapspec::UnitSpec&>(*spec);
            if (item["stats"].Exists() && !ReadKeys(item["stats"], unitSpec.record.stats, &bad))
                Warn(path, item["stats"].Line(), "stats: " + bad);
            if (item["behaviour"].Exists()) ReadBehaviours(item["behaviour"], unitSpec, path);
        }
    }
    return spec;
}

void ReadPlayers(const toml::Value& players, mapspec::ZoneDiplomacy& diplomacy, const std::string& path) {
    for (int k = 0; k < players.Size(); ++k) {
        const std::string_view key = players.KeyAt(k);
        const toml::Value value = players.ValueAt(k);
        bool read = false;
        if (key == "names") read = ReadValue(value, diplomacy.names);
        else if (key == "relations") read = ReadValue(value, diplomacy.relations);
        else Warn(path, value.Line(), "players: unknown key " + std::string(key));
        if (!read && (key == "names" || key == "relations"))
            Warn(path, value.Line(), "players: " + std::string(key) + " has a value it does not take");
    }
}

} // namespace

std::string_view KindName(SpecKind kind) {
    for (const auto& [code, name] : kKinds)
        if (code == kind) return name;
    return {};
}

std::string Write(const mapspec::MobContents& contents, std::string_view script) {
    toml::Writer writer;
    writer.Comment("A .mob's map objects (docs/notes/mods.md, \"Map objects as TOML\"); its script stays in the .mob.");
    if (!script.empty()) writer.Pair("script", toml::String(script));
    writer.Pair("applies_relations", toml::Bool(contents.appliesRelations));
    for (const mapspec::MobWeather& weather : contents.weather) {
        writer.ArrayTable("weather");
        PairVisit visit;
        Fields(const_cast<mapspec::MobWeather&>(weather), visit);
        for (const auto& [key, value] : visit.pairs) writer.Pair(key, value);
    }
    writer.Table("players");
    writer.Pair("names", Text(contents.diplomacy.names));
    std::vector<std::string> rows;
    for (const auto& row : contents.diplomacy.relations) rows.push_back(Text(row));
    writer.Pair("relations", toml::Array(rows, true));
    // The records in the order read, each once: the one of its id that byId holds.
    const mapspec::MobRecords& records = contents.records;
    for (const std::unique_ptr<Spec>& spec : records.read) {
        if (records.byId.find(spec->id)->second != spec.get()) continue;
        const std::unique_ptr<Spec> defaults = NewSpec(spec->kind);
        if (!defaults) continue;
        writer.ArrayTable("object");
        writer.Pair("kind", toml::String(KindName(spec->kind)));
        WriteChanged(writer, *spec, *defaults);
        if (spec->kind == SpecKind::Unit)
            WriteUnitParts(writer, static_cast<mapspec::UnitSpec&>(*spec), static_cast<mapspec::UnitSpec&>(*defaults));
    }
    return writer.text;
}

std::optional<mapspec::MobContents> Read(std::string_view text, const std::string& path) {
    toml::Document document;
    if (!document.Parse(text, path)) {
        game::Warning("map objects: %s", document.Error());
        return std::nullopt;
    }
    mapspec::MobContents contents;
    const toml::Value root = document.Root();
    for (int k = 0; k < root.Size(); ++k) {
        const std::string_view key = root.KeyAt(k);
        const toml::Value value = root.ValueAt(k);
        if (IsReplaceKey(key, value)) continue;
        if (key == "script") {
            if (!value.IsString()) Warn(path, value.Line(), "script is not a file name");
            contents.scriptFile = value.String();
            continue;
        }
        if (key == "applies_relations") {
            if (!value.IsBool()) Warn(path, value.Line(), "applies_relations is not true or false");
            contents.appliesRelations = value.IsBool() ? value.Bool() : contents.appliesRelations;
        } else if (key == "weather") {
            for (int i = 0; i < value.Size(); ++i) {
                std::string bad;
                if (!ReadKeys(value.At(i), contents.weather.emplace_back(), &bad))
                    Warn(path, value.At(i).Line(), "weather: " + bad);
            }
        } else if (key == "players") {
            ReadPlayers(value, contents.diplomacy, path);
        } else if (key == "object") {
            for (int i = 0; i < value.Size(); ++i)
                if (std::unique_ptr<Spec> spec = ReadRecord(value.At(i), path)) contents.records.Add(std::move(spec));
        } else {
            Warn(path, value.Line(), "unknown key " + std::string(key));
        }
    }
    return contents;
}

std::optional<mapspec::MobContents> Find(const std::string& path) {
    const RecordFile file = FindRecordFile(path, false);
    for (const TomlFile& toml : file.tomls)
        if (std::optional<mapspec::MobContents> contents = Read(toml.text, toml.path)) return contents;
    return std::nullopt;
}

std::string ScriptFile(const std::string& path) {
    const RecordFile file = FindRecordFile(path, false);
    for (const TomlFile& toml : file.tomls) {
        toml::Document document;
        if (!document.Parse(toml.text, toml.path)) continue;
        const toml::Value root = document.Root();
        for (int k = 0; k < root.Size(); ++k)
            if (root.KeyAt(k) == "script") return std::string(root.ValueAt(k).String());
        return {};
    }
    return {};
}

std::optional<ScriptLuaFile> FindScriptLua(const std::string& path, std::string_view name) {
    const std::size_t slash = path.find_last_of("\\/");
    const std::string folder = slash == std::string::npos ? std::string(".") : path.substr(0, slash);
    const std::string mob = path.substr(slash + 1);
    std::string lua(name);
    if (lua.empty()) {
        const std::size_t dot = mob.find_last_of('.');
        lua = mob.substr(0, dot) + ".lua";
    }
    for (const platform::FolderLayer& layer : platform::FilesByLayer(folder, {mob, lua})) {
        bool hasMob = false;
        for (const platform::LayerFile& file : layer.files) {
            if (text::CompareIgnoringCase(file.name, lua) != 0) {
                hasMob = true;
                continue;
            }
            ScriptLuaFile found;
            found.path = file.path;
            found.name = lua;
            if (platform::ReadLayerFile(file.path, &found.text)) return found;
        }
        if (hasMob) break;
    }
    return std::nullopt;
}

std::string ScriptLuaName(std::string_view mob, std::string_view lua) {
    const std::size_t slash = mob.find_last_of("\\/");
    return "Maps\\" + (slash == std::string_view::npos ? std::string() : std::string(mob.substr(0, slash + 1))) +
           std::string(lua);
}

std::vector<std::pair<std::string, std::string>> FieldImages(const Spec& spec) {
    Spec& record = const_cast<Spec&>(spec);
    Images images = ImagesOf(record);
    std::string kind;
    Image(kind, record.kind);
    images.emplace_back("kind", kind);
    if (record.kind != SpecKind::Unit) return images;
    auto& unit = static_cast<mapspec::UnitSpec&>(record);
    for (auto& [key, image] : ImagesOf(unit.record.stats)) images.emplace_back("stats." + key, std::move(image));
    std::string count;
    Image(count, unit.behaviours);
    images.emplace_back("behaviours", count);
    for (int i = 0; i < kBehaviourCount; ++i)
        for (auto& [key, image] : ImagesOf(unit.behaviour[i]))
            images.emplace_back("behaviour " + std::to_string(i) + "." + key, std::move(image));
    return images;
}

} // namespace mobtoml
