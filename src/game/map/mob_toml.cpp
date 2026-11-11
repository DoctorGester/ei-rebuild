// A .mob's chunks but its scripts as TOML and back (mob_toml.h; docs/notes/mods.md, "Map objects as TOML"). Each record
// type lists its fields once (`Fields(record, visit)`), which the writer, the reader and the comparison all walk.
#include "game/map/mob_toml.h"

#include "engine/data/record_names.h"
#include "engine/data/resource.h"
#include "lib/crt.h"
#include "lib/text_case.h"
#include "lib/toml.h"
#include "platform/platform.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iterator>
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
// A code written by its name where it has one (`names`), else as its number.
template <class E>
struct Named {
    E& value;
    std::span<const std::pair<E, std::string_view>> names;
};

// An integer code by its name (dbtoml's names: docs/notes/mods.md, "Readable values"), else as its number.
struct Coded {
    std::int32_t& value;
    dbtoml::Names names;
};
// A fixed-meaning array as an inline table by element name (dbtoml::Table's rules: the elements that differ from a
// new record's zero written, those given read, the others kept).
template <class T, std::size_t N>
struct Elements {
    T (&values)[N];
    std::span<const std::string_view, N> names;
};
// The owner's player number by the player's name, where the file's [players] names it (`names` that differ from the
// defaults, each name of one player); else the number. A name is any of the 32 players' ("Player-3" too).
struct Side {
    std::uint8_t& value;
};
// The player names of a file (null: none known, numbers only).
using PlayerNames = std::vector<std::string>;

// A lock's mask (LockKey::Opens): a key opens it when they share a bit, and either both have "key" and the ids match
// (the quest items' keys: Item::GetUse), or they share one of the three level bits and the key's level is at least the
// lock's. The game's only other key is the Use skill's, {science, level: the science}; no key carries science_2 or
// science_3, so a lock of only those opens with none. A bit above them by its number (none has one).
constexpr dbtoml::Name kLockBits[] = {{0x1, "science"}, {0x2, "science_2"}, {0x4, "science_3"}, {0x8, "key"}};

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
std::string Text(dbtoml::Bits field) { return dbtoml::ValueText(field); }
std::string Text(Side field) { return toml::Int(field.value); }
template <class T, std::size_t N>
std::string Text(Elements<T, N> field) {
    const std::string unset = Text(T{});
    std::vector<std::pair<std::string, std::string>> pairs;
    for (std::size_t i = 0; i < N; ++i) {
        std::string text = Text(field.values[i]);
        if (text != unset) pairs.emplace_back(std::string(field.names[i]), std::move(text));
    }
    return toml::InlineTable(pairs);
}
// A side's name in `names` when exactly one player has it and it is not the default "Player-n".
std::string SideText(Side field, const PlayerNames* names) {
    const mapspec::ZoneDiplomacy defaults;
    if (names && field.value < names->size() && field.value < defaults.names.size() &&
        (*names)[field.value] != defaults.names[field.value] &&
        std::count(names->begin(), names->end(), (*names)[field.value]) == 1)
        return Text((*names)[field.value]);
    return Text(field);
}
std::string Text(Coded field) {
    const std::string_view name = dbtoml::NameOf(field.names, field.value);
    return name.empty() ? toml::Int(field.value) : toml::String(name);
}
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
// A player's number, or (with the file's names) its name.
bool ReadSide(const toml::Value& value, Side field, const PlayerNames* names) {
    if (value.IsString()) {
        if (!names) return false;
        const auto found = std::find(names->begin(), names->end(), value.String());
        if (found == names->end()) return false;
        field.value = static_cast<std::uint8_t>(found - names->begin());
        return true;
    }
    return ReadValue(value, field.value);
}
template <class T, std::size_t N>
bool ReadValue(const toml::Value& value, Elements<T, N> field) {
    if (!value.IsTable()) return false;
    T read[N];
    std::copy(std::begin(field.values), std::end(field.values), std::begin(read));
    for (int k = 0; k < value.Size(); ++k) {
        const auto name = std::find(field.names.begin(), field.names.end(), value.KeyAt(k));
        if (name == field.names.end() || !ReadValue(value.ValueAt(k), read[name - field.names.begin()])) return false;
    }
    std::copy(std::begin(read), std::end(read), std::begin(field.values));
    return true;
}
bool ReadValue(const toml::Value& value, Coded field) {
    std::int64_t code = 0;
    if (!dbtoml::ReadCode(value, field.names, code)) return false;
    field.value = static_cast<std::int32_t>(code);
    return true;
}
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
void Image(std::string& out, Coded field) { Image(out, field.value); }
void Image(std::string& out, dbtoml::Bits field) { Image(out, field.value); }
void Image(std::string& out, Side field) { Image(out, field.value); }
template <class T, std::size_t N>
void Image(std::string& out, Elements<T, N> field) {
    for (const T& value : field.values) Image(out, value);
}
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
// A side by the player's name (`names`, the file's).
class ChangedVisit {
public:
    ChangedVisit(toml::Writer& writer, Images defaults, std::string_view skip, const PlayerNames* names)
        : writer(writer), defaults(std::move(defaults)), skip(skip), names(names) {}

    template <class T>
    void operator()(std::string_view key, T&& field) {
        std::string image;
        Image(image, field);
        const bool same = next < defaults.size() && defaults[next].second == image;
        ++next;
        if (key == skip || (same && key != "id")) return;
        if constexpr (std::is_same_v<std::remove_cvref_t<T>, Side>) writer.Pair(key, SideText(field, names));
        else writer.Pair(key, Text(field));
    }

private:
    toml::Writer& writer;
    Images defaults;
    std::string_view skip;
    const PlayerNames* names = nullptr;
    std::size_t next = 0;
};

// The names a field takes, for a message ("" for a field without names).
template <class T>
std::string ValidNames(const T&) {
    return {};
}
std::string ValidNames(const Coded& field) { return dbtoml::NameList(field.names); }
template <class T, std::size_t N>
std::string ValidNames(const Elements<T, N>& field) {
    return dbtoml::KeyList(field.names);
}
template <class E>
std::string ValidNames(const Named<E>& field) {
    std::string list;
    for (const auto& [code, name] : field.names) list += (list.empty() ? "" : ", ") + std::string(name);
    return list;
}
// The sides a map object takes: a player's number, or a name of the file's [players] names.
std::string ValidSides(const PlayerNames* names) {
    std::string list = "a player number 0..255";
    if (names)
        for (const auto& name : *names)
            if (!name.empty()) list += ", " + name;
    return list;
}

// Finds the field of one key and reads the value into it (a side by the names of `players`).
class KeyVisit {
public:
    KeyVisit(std::string_view key, const toml::Value& value, const PlayerNames* players)
        : key(key), value(value), players(players) {}

    template <class T>
    void operator()(std::string_view name, T&& field) {
        if (found || name != key) return;
        found = true;
        if constexpr (std::is_same_v<std::remove_cvref_t<T>, Side>) {
            read = ReadSide(value, field, players);
            if (!read) valid = ValidSides(players);
        } else {
            read = ReadValue(value, field);
            if (!read) valid = ValidNames(field);
        }
    }

    std::string_view key;
    toml::Value value;
    const PlayerNames* players = nullptr;
    bool found = false;   // the record has the key
    bool read = false;    // and the value was of a type it takes
    std::string valid;    // the names the field takes, after a value it refused
};

// Each key of the table `value` read into the record's field of that name, but those of `others`; false when `value`
// is not a table, or a key is unknown, has a name its field does not have or a value of a type it does not take
// (`bad`: that key, with the valid names; `line`: its line). A side by the names of `players`.
template <class Record>
bool ReadKeys(const toml::Value& value, Record& record, std::string* bad = nullptr,
              std::span<const std::string_view> others = {}, int* line = nullptr,
              const PlayerNames* players = nullptr) {
    if (line) *line = value.Line();
    if (!value.IsTable()) {
        if (bad) *bad = "not a table";
        return false;
    }
    bool all = true;
    for (int k = 0; k < value.Size(); ++k) {
        bool other = false;
        for (std::string_view name : others) other = other || name == value.KeyAt(k);
        if (other) continue;
        KeyVisit visit(value.KeyAt(k), value.ValueAt(k), players);
        Fields(record, visit);
        if (visit.found && visit.read) continue;
        if (bad && all) {
            *bad = std::string(value.KeyAt(k)) + (visit.found ? " has a value it does not take" : " is unknown");
            if (!visit.valid.empty()) *bad += " (one of: " + visit.valid + ")";
        }
        if (line && all) *line = value.ValueAt(k).Line();
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
    visit("mask", dbtoml::Bits{lock.mask, kLockBits, ~0xFu, 0, "a bit number 4..31"});
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
    visit("mode_speeds", Elements<float, 4>{stats.modeSpeeds, recordnames::kModeSpeeds});
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
    visit("damage_weights", Elements<std::uint8_t, 7>{stats.damageWeights, recordnames::kDamageTypes});
    visit("protection_scale", stats.protectionScale);
    visit("perception", Elements<float, 6>{stats.perception, recordnames::kSenses});
    visit("conspicuity", Elements<float, 6>{stats.conspicuity, recordnames::kSenses});
    visit("attributes", Elements<std::uint8_t, 3>{stats.attributes, recordnames::kAttributes});
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
    visit("class", Coded{spec.parentId, recordnames::kObjectClasses});
    visit("position", spec.position);
    visit("rotation", spec.rotation);
    visit("scale", spec.scale);
    visit("side", Side{spec.side});
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
        visit("unread_sides", spec.trap.sides);
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

// The fields of `record` that differ from `defaults`' (and its id) written, a side by the player's name in `names`.
template <class Record>
void WriteChanged(toml::Writer& writer, Record& record, Record& defaults, std::string_view skip = {},
                  const PlayerNames* names = nullptr) {
    ChangedVisit visit(writer, ImagesOf(defaults), skip, names);
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

// A record that fails to load: logged with its line and left out.
std::unique_ptr<Spec> LeftOut(const std::string& path, int line, const std::string& what) {
    Warn(path, line, what + "; the record is left out");
    return nullptr;
}

// A unit's behaviour records: as many as the array has (at most five). False (with `bad` and its `line`) when one
// does not load.
bool ReadBehaviours(const toml::Value& list, mapspec::UnitSpec& unit, std::string& bad, int& line) {
    line = list.Line();
    if (!list.IsArray()) {
        bad = "behaviour is not an array of tables";
        return false;
    }
    if (list.Size() > kBehaviourCount) {
        bad = "more than five behaviour records";
        return false;
    }
    unit.behaviours = list.Size();
    for (int i = 0; i < unit.behaviours; ++i) {
        if (!ReadKeys(list.At(i), unit.behaviour[i], &bad, {}, &line)) {
            bad = "behaviour: " + bad;
            return false;
        }
    }
    return true;
}

std::unique_ptr<Spec> ReadRecord(const toml::Value& item, const std::string& path, const PlayerNames& players) {
    std::string kindName;
    if (!toml::Read(item["kind"], &kindName)) {
        return LeftOut(path, item.Line(), "a record without its kind");
    }
    std::unique_ptr<Spec> spec;
    for (const auto& [kind, name] : kKinds)
        if (name == kindName) spec = NewSpec(kind);
    if (!spec) {
        std::string kinds;
        for (const auto& [kind, name] : kKinds) kinds += (kinds.empty() ? "" : ", ") + std::string(name);
        return LeftOut(path, item.Line(), "unknown kind " + kindName + " (one of: " + kinds + ")");
    }
    constexpr std::string_view kApart[] = {"kind", "stats", "behaviour"};
    std::string bad;
    int line = 0;
    if (!ReadKeys(item, *spec, &bad, kApart, &line, &players)) return LeftOut(path, line, bad);
    if (item["stats"].Exists() || item["behaviour"].Exists()) {
        if (spec->kind != SpecKind::Unit) return LeftOut(path, item.Line(), "stats and behaviour are a unit's");
        auto& unitSpec = static_cast<mapspec::UnitSpec&>(*spec);
        if (item["stats"].Exists() && !ReadKeys(item["stats"], unitSpec.record.stats, &bad, {}, &line))
            return LeftOut(path, line, "stats: " + bad);
        if (item["behaviour"].Exists() && !ReadBehaviours(item["behaviour"], unitSpec, bad, line))
            return LeftOut(path, line, bad);
    }
    return spec;
}

// The relations by name (DiplomacyRelation): the keys of [players] that list the pairs of each.
constexpr std::pair<std::int32_t, std::string_view> kRelations[] = {{0, "allied"}, {1, "neutral"}, {2, "hostile"}};
constexpr int kPlayers = 32;

// A player number of the table, from a TOML integer or a key's text.
bool ReadPlayer(std::string_view text, int& player) {
    player = 0;
    if (text.empty() || text.size() > 2) return false;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        player = player * 10 + (c - '0');
    }
    return player < kPlayers;
}
bool ReadPlayer(const toml::Value& value, int& player) {
    if (!value.IsInt() || value.Int() < 0 || value.Int() >= kPlayers) return false;
    player = static_cast<int>(value.Int());
    return true;
}

// `names = { 3 = "Golems" }` and `hostile = [[0, 3], [3, 0]]` over the defaults (ZoneDiplomacy's: "Player-n",
// neutral, allied with itself).
void ReadPlayers(const toml::Value& players, mapspec::ZoneDiplomacy& diplomacy, const std::string& path) {
    for (int k = 0; k < players.Size(); ++k) {
        const std::string_view key = players.KeyAt(k);
        const toml::Value value = players.ValueAt(k);
        if (key == "names") {
            if (!value.IsTable()) Warn(path, value.Line(), "players: names is not a table of player numbers");
            for (int i = 0; i < value.Size(); ++i) {
                int player = 0;
                std::string name;
                if (!ReadPlayer(value.KeyAt(i), player) || !ReadValue(value.ValueAt(i), name))
                    Warn(path, value.ValueAt(i).Line(), "players: names: " + std::string(value.KeyAt(i)) +
                                                            " is not a player number 0..31 with a name");
                else diplomacy.names[static_cast<std::size_t>(player)] = name;
            }
            continue;
        }
        const auto* relation = std::find_if(std::begin(kRelations), std::end(kRelations),
                                            [&](const auto& entry) { return entry.second == key; });
        if (relation == std::end(kRelations)) {
            Warn(path, value.Line(), "players: unknown key " + std::string(key) + " (one of: names, allied, neutral, hostile)");
            continue;
        }
        for (int i = 0; i < value.Size() || !value.IsArray(); ++i) {
            const toml::Value pair = value.At(i);
            int player = 0;
            int other = 0;
            if (!value.IsArray() || pair.Size() != 2 || !ReadPlayer(pair.At(0), player) || !ReadPlayer(pair.At(1), other)) {
                Warn(path, value.Line(), "players: " + std::string(key) + " is not a list of [player, other] pairs of 0..31");
                break;
            }
            diplomacy.relations[player][other] = relation->first;
        }
    }
}

// The [players] table of the names and relations that differ from the defaults (nothing when none does).
void WritePlayers(toml::Writer& writer, const mapspec::ZoneDiplomacy& diplomacy) {
    const mapspec::ZoneDiplomacy defaults;
    std::vector<std::pair<std::string, std::string>> names;
    for (std::size_t i = 0; i < diplomacy.names.size() && i < defaults.names.size(); ++i)
        if (diplomacy.names[i] != defaults.names[i]) names.emplace_back(std::to_string(i), Text(diplomacy.names[i]));
    std::vector<std::pair<std::string_view, std::string>> relations;
    for (const auto& [code, name] : kRelations) {
        std::vector<std::string> pairs;
        for (int i = 0; i < kPlayers; ++i)
            for (int j = 0; j < kPlayers; ++j)
                if (diplomacy.relations[i][j] == code && defaults.relations[i][j] != code)
                    pairs.push_back(toml::Array({toml::Int(i), toml::Int(j)}));
        if (!pairs.empty()) relations.emplace_back(name, toml::Array(pairs));
    }
    if (names.empty() && relations.empty()) return;
    writer.Table("players");
    if (!names.empty()) writer.Pair("names", toml::InlineTable(names));
    for (const auto& [name, pairs] : relations) writer.Pair(name, pairs);
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
    WritePlayers(writer, contents.diplomacy);
    // The records in the order read, each once: the one of its id that byId holds.
    const mapspec::MobRecords& records = contents.records;
    for (const std::unique_ptr<Spec>& spec : records.read) {
        if (records.byId.find(spec->id)->second != spec.get()) continue;
        const std::unique_ptr<Spec> defaults = NewSpec(spec->kind);
        if (!defaults) continue;
        writer.ArrayTable("object");
        writer.Pair("kind", toml::String(KindName(spec->kind)));
        WriteChanged(writer, *spec, *defaults, {}, &contents.diplomacy.names);
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
    // The players first: the objects' sides may name them.
    if (root["players"].Exists()) ReadPlayers(root["players"], contents.diplomacy, path);
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
                int line = 0;
                if (ReadKeys(value.At(i), contents.weather.emplace_back(), &bad, {}, &line)) continue;
                contents.weather.pop_back();
                Warn(path, line, "weather: " + bad + "; the record is left out");
            }
        } else if (key == "players") {
            continue;
        } else if (key == "object") {
            for (int i = 0; i < value.Size(); ++i)
                if (std::unique_ptr<Spec> spec = ReadRecord(value.At(i), path, contents.diplomacy.names))
                    contents.records.Add(std::move(spec));
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
