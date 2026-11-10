#pragma once

// The game databases as TOML (units.udb, perks.pdb, spells.sdb, items.idb, quests.qdb, Levers.ldb; docs/notes/mods.md,
// "Databases as TOML"). A record type's fields are listed once, by a function `Fields(record, visit)` beside the
// record's Serialize that calls `visit(key, field)` for each field (`visit.Unread(key, field)` for the bytes no code
// uses, written only when not zero); from that list a table's records are written (WriteRecords) and a TOML file's
// records merged over them by their key (MergeRecords). Strings are the asset code page's in memory, UTF-8 in TOML.

#include "game/core/asset_text.h"
#include "lib/crt.h"
#include "lib/toml.h"
#include "lib/vecmath.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace dbtoml {

// An unsigned field written in hexadecimal (flags and masks).
struct Hex {
    std::uint32_t& value;
};
// An unsigned field holding a signed number, or a float's bits (a perk's values).
struct Signed {
    std::uint32_t& value;
};
struct FloatBits {
    std::uint32_t& value;
};
// A char array holding a C string (a spell modifier's code): written up to its first zero, read back zero-filled.
template <std::size_t N>
struct Chars {
    char (&value)[N];
};

// ---- Values written --------------------------------------------------------------------------------------------

inline std::string ValueText(const std::string& value) { return toml::String(text::FromAssetText(value)); }
inline std::string ValueText(std::int32_t value) { return toml::Int(value); }
inline std::string ValueText(std::uint32_t value) { return toml::Int(value); }
inline std::string ValueText(std::uint8_t value) { return toml::Int(value); }
inline std::string ValueText(float value) { return toml::Float(value); }
inline std::string ValueText(Hex field) { return toml::Hex(field.value); }
inline std::string ValueText(Signed field) { return toml::Int(static_cast<std::int32_t>(field.value)); }
inline std::string ValueText(FloatBits field) { return toml::Float(std::bit_cast<float>(field.value)); }
template <std::size_t N>
std::string ValueText(Chars<N> field) {
    std::size_t length = 0;
    while (length < N && field.value[length] != '\0') ++length;
    return toml::String(text::FromAssetText(std::string_view(field.value, length)));
}
template <class E>
    requires std::is_enum_v<E>
std::string ValueText(E value) {
    return toml::Int(static_cast<std::int64_t>(value));
}
inline std::string ValueText(const Vector3& value) {
    return toml::Array({ValueText(value.x), ValueText(value.y), ValueText(value.z)});
}

class PairWriter;
// A record type with a field list (a nested record: an inline table).
template <class T>
concept HasFields = requires(T& record, PairWriter& visit) { Fields(record, visit); };

template <class T, std::size_t N>
std::string ValueText(const T (&values)[N]);
template <class T, std::size_t N>
std::string ValueText(const std::array<T, N>& values);
template <class T>
std::string ValueText(const std::vector<T>& values);
template <HasFields T>
std::string ValueText(const T& record);

// A record's fields as (key, value text) pairs, in the list's order.
class PairWriter {
public:
    template <class T>
    void operator()(std::string_view key, T&& field) {
        pairs.emplace_back(std::string(key), ValueText(field));
    }
    template <class T>
    void Unread(std::string_view key, T&& field);

    std::vector<std::pair<std::string, std::string>> pairs;
};

template <class T, std::size_t N>
std::string ValueText(const T (&values)[N]) {
    std::vector<std::string> items;
    for (const T& value : values) items.push_back(ValueText(value));
    return toml::Array(items);
}
template <class T, std::size_t N>
std::string ValueText(const std::array<T, N>& values) {
    std::vector<std::string> items;
    for (const T& value : values) items.push_back(ValueText(value));
    return toml::Array(items);
}
template <class T>
std::string ValueText(const std::vector<T>& values) {
    std::vector<std::string> items;
    for (const T& value : values) items.push_back(ValueText(value));
    return toml::Array(items);
}
template <HasFields T>
std::string ValueText(const T& record) {
    PairWriter writer;
    Fields(const_cast<T&>(record), writer);
    return toml::InlineTable(writer.pairs);
}

// ---- Zero values (the unread fields left out) --------------------------------------------------------------------

inline bool IsUnset(const std::string& value) { return value.empty(); }
inline bool IsUnset(float value) { return std::bit_cast<std::uint32_t>(value) == 0; }
template <class T>
    requires std::is_integral_v<T> || std::is_enum_v<T>
bool IsUnset(T value) {
    return value == T{};
}
template <class T, std::size_t N>
bool IsUnset(const T (&values)[N]) {
    for (const T& value : values)
        if (!IsUnset(value)) return false;
    return true;
}
template <class T>
bool IsUnset(const std::vector<T>& values) {
    return values.empty();
}

template <class T>
void PairWriter::Unread(std::string_view key, T&& field) {
    if (!IsUnset(field)) (*this)(key, field);
}

// ---- Values read -----------------------------------------------------------------------------------------------

// `value` into `field` when it is of a type the field takes (toml::Read's rules: an integer for a float, a float for an
// integer); false (the field left as it was) otherwise.
inline bool ReadValue(const toml::Value& value, std::string& field) {
    std::string read;
    if (!toml::Read(value, &read)) return false;
    field = text::ToAssetText(read);
    return true;
}
inline bool ReadValue(const toml::Value& value, std::int32_t& field) { return toml::Read(value, &field); }
inline bool ReadValue(const toml::Value& value, std::uint32_t& field) { return toml::Read(value, &field); }
inline bool ReadValue(const toml::Value& value, std::uint8_t& field) { return toml::Read(value, &field); }
inline bool ReadValue(const toml::Value& value, float& field) { return toml::Read(value, &field); }
inline bool ReadValue(const toml::Value& value, Hex field) { return toml::Read(value, &field.value); }
inline bool ReadValue(const toml::Value& value, Signed field) { return toml::Read(value, &field.value); }
inline bool ReadValue(const toml::Value& value, FloatBits field) {
    float read = 0.0f;
    if (!toml::Read(value, &read)) return false;
    field.value = std::bit_cast<std::uint32_t>(read);
    return true;
}
template <std::size_t N>
bool ReadValue(const toml::Value& value, Chars<N> field) {
    std::string read;
    if (!ReadValue(value, read)) return false;
    for (std::size_t i = 0; i < N; ++i) field.value[i] = i < read.size() ? read[i] : '\0';
    return true;
}
template <class E>
    requires std::is_enum_v<E>
bool ReadValue(const toml::Value& value, E& field) {
    std::int32_t read = 0;
    if (!toml::Read(value, &read)) return false;
    field = static_cast<E>(read);
    return true;
}

template <class T, std::size_t N>
bool ReadValue(const toml::Value& value, T (&field)[N]);
template <class T, std::size_t N>
bool ReadValue(const toml::Value& value, std::array<T, N>& field);
template <class T>
bool ReadValue(const toml::Value& value, std::vector<T>& field);
template <HasFields T>
bool ReadValue(const toml::Value& value, T& record);

// An array of three numbers.
inline bool ReadValue(const toml::Value& value, Vector3& field) {
    Vector3 read;
    if (value.Size() != 3 || !ReadValue(value.At(0), read.x) || !ReadValue(value.At(1), read.y) ||
        !ReadValue(value.At(2), read.z))
        return false;
    field = read;
    return true;
}

// Finds the field of one key in a record's list and reads the value into it.
class FieldReader {
public:
    FieldReader(std::string_view key, const toml::Value& value) : key(key), value(value) {}

    template <class T>
    void operator()(std::string_view name, T&& field) {
        if (found || name != key) return;
        found = true;
        read = ReadValue(value, field);
    }
    template <class T>
    void Unread(std::string_view name, T&& field) {
        (*this)(name, field);
    }

    std::string_view key;
    toml::Value value;
    bool found = false;   // the record has the key
    bool read = false;    // and the value was of a type it takes
};

// Each key of the table `value` read into the record's field of that name; false when `value` is not a table, a key
// is unknown or of a type its field does not take.
template <class Record>
bool ReadKeys(const toml::Value& value, Record& record) {
    if (!value.IsTable()) return false;
    bool all = true;
    for (int k = 0; k < value.Size(); ++k) {
        FieldReader reader(value.KeyAt(k), value.ValueAt(k));
        Fields(record, reader);
        all = all && reader.found && reader.read;
    }
    return all;
}

// `count` elements in order (the rest zero); false when `value` is not an array, holds more, or an element was of a
// type it does not take.
// `field` set to its type's default (each element of an array).
template <class T>
void Clear(T& field) {
    if constexpr (std::is_array_v<T>) {
        for (auto& element : field) Clear(element);
    } else {
        field = T{};
    }
}
template <class T>
bool ReadElements(const toml::Value& value, T* elements, std::size_t count) {
    if (!value.IsArray()) return false;
    bool all = value.Size() <= static_cast<int>(count);
    for (std::size_t i = 0; i < count; ++i) {
        Clear(elements[i]);
        if (static_cast<int>(i) < value.Size()) all = ReadValue(value.At(static_cast<int>(i)), elements[i]) && all;
    }
    return all;
}
template <class T, std::size_t N>
bool ReadValue(const toml::Value& value, T (&field)[N]) {
    return ReadElements(value, field, N);
}
template <class T, std::size_t N>
bool ReadValue(const toml::Value& value, std::array<T, N>& field) {
    return ReadElements(value, field.data(), N);
}
template <class T>
bool ReadValue(const toml::Value& value, std::vector<T>& field) {
    if (!value.IsArray()) return false;
    field.assign(static_cast<std::size_t>(value.Size()), T{});
    bool all = true;
    for (int i = 0; i < value.Size(); ++i) all = ReadValue(value.At(i), field[static_cast<std::size_t>(i)]) && all;
    return all;
}
// A nested record (an inline table): the keys given, the others kept.
template <HasFields T>
bool ReadValue(const toml::Value& value, T& record) {
    return ReadKeys(value, record);
}

// ---- Tables ----------------------------------------------------------------------------------------------------

// Every record of `records` as an array of tables `table`.
template <class Record>
void WriteRecords(toml::Writer& writer, std::string_view table, const std::vector<Record>& records) {
    for (const Record& record : records) {
        writer.ArrayTable(table);
        PairWriter pairs;
        Fields(const_cast<Record&>(record), pairs);
        for (const auto& [key, value] : pairs.pairs) writer.Pair(key, value);
    }
}

// Each record of the TOML array of tables `table` merged into `records` by its key (`key`, compared with the record's
// `keyOf(record)` exactly, as the tables' indexes compare): the first record of the key that this file has not merged
// yet takes the fields given and keeps the others; a key no record has left is a new record (the type's defaults and
// the fields given) at the end, so every record keeps its index. `what` names the database in the messages: a record
// without its key is left out, an unknown key or a value of a type its field does not take is logged and skipped.
template <class Record, class KeyOf>
void MergeRecords(const toml::Value& table, std::vector<Record>& records, const char* key, KeyOf keyOf,
                  const char* what, const std::string& path) {
    if (table.Exists() && !table.IsArray()) {
        game::Warning("%s: %s line %d: not an array of tables", what, path, table.Line());
        return;
    }
    std::vector<bool> merged(records.size());
    for (int i = 0; i < table.Size(); ++i) {
        const toml::Value item = table.At(i);
        std::string name;
        if (!ReadValue(item[key], name)) {
            game::Warning("%s: %s line %d: a record without its %s", what, path, item.Line(), key);
            continue;
        }
        std::size_t index = 0;
        while (index < records.size() && (merged[index] || keyOf(records[index]) != name)) ++index;
        if (index == records.size()) {
            records.emplace_back();
            merged.push_back(false);
        }
        merged[index] = true;
        Record& record = records[index];
        for (int k = 0; k < item.Size(); ++k) {
            FieldReader reader(item.KeyAt(k), item.ValueAt(k));
            Fields(record, reader);
            if (!reader.found)
                game::Warning("%s: %s line %d: unknown key %s", what, path, item.ValueAt(k).Line(),
                              std::string(item.KeyAt(k)));
            else if (!reader.read)
                game::Warning("%s: %s line %d: %s has a value it does not take", what, path, item.ValueAt(k).Line(),
                              std::string(item.KeyAt(k)));
        }
    }
}

// Logs each top-level key of `root` that is neither one of `tables` nor `replace`.
void CheckTables(const toml::Value& root, std::initializer_list<std::string_view> tables, const char* what,
                 const std::string& path);

} // namespace dbtoml
