#pragma once

// The game databases as TOML (units.udb, perks.pdb, spells.sdb, items.idb, quests.qdb, Levers.ldb; docs/notes/mods.md,
// "Databases as TOML"). A record type's fields are listed once, by a function `Fields(record, visit)` beside the
// record's Serialize that calls `visit(key, field)` for each field (`visit.Unread(key, field)` for the bytes no code
// uses, written only when not zero); from that list a table's records are written (WriteRecords) and a TOML file's
// records merged over them by their key (MergeRecords). Strings are the asset code page's in memory, UTF-8 in TOML.

#include "engine/core/asset_text.h"
#include "lib/crt.h"
#include "lib/toml.h"
#include "lib/vecmath.h"

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
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

// ---- Named values (docs/notes/mods.md, "Readable values") --------------------------------------------------------

// A code's name, or the name of a mask's bits.
struct Name {
    std::int64_t value = 0;
    std::string_view name;
};
using Names = std::span<const Name>;

// "a, b, c": the names a field takes, for the messages.
std::string NameList(Names names);
// The name of `value` (empty when it has none).
std::string_view NameOf(Names names, std::int64_t value);
// The value named `name`; false when no name is it.
bool ValueOf(Names names, std::string_view name, std::int64_t& value);

// A code (an integer or an enum field) by its name; a value no name has is written as its number, which the reader
// also takes.
template <class T>
struct Enum {
    T& value;
    Names names;
};
// The bits `mask` of an unsigned field as a code by name (the codes shifted down to the mask's lowest bit).
struct Code {
    std::uint32_t& value;
    std::uint32_t mask = 0;
    Names names;
};
// A mask's bits as a list. A name stands for the bits of its value (one or several: written when all are set, in the
// list's order, so "any" = 0x7F before the single weapons). A bit of `numbered` no name covers is the number the code
// gives it: bit n is `n + firstNumber` (a weapon type 7 for the weapon bit 1 << 7, a trader class 6 for bit 5),
// `numbers` saying what the numbers are in the messages. The bits outside the names and `numbered` are other fields'.
struct Bits {
    std::uint32_t& value;
    Names names;
    std::uint32_t numbered = 0;
    int firstNumber = 0;
    std::string_view numbers;
};
// The bits `mask` of an unsigned field, unshifted, as a number: the bits of a mask that no code reads (an `unread...`
// field beside the named bits).
struct Masked {
    std::uint32_t& value;
    std::uint32_t mask = 0;
};
// Unsigned fields holding floats' bits, as a list of the floats (the bytes no code reads, kept as data).
template <std::size_t N>
struct FloatBitsValues {
    std::uint32_t (&values)[N];
};
// A number that holds 0 or 1: false / true (another number stays a number).
template <class T>
struct Bool {
    T& value;
};
// A fixed-meaning array as an inline table by element name (`{ sight = 8.5, near = 2.8 }`): the elements whose
// value differs from a new record's are written; the reader sets the elements given and keeps the others. A
// vector is made as long as the names when it is read.
template <class T>
struct Table {
    T* values = nullptr;
    std::span<const std::string_view> names;
};
template <class T>
struct VectorTable {
    std::vector<T>& values;
    std::span<const std::string_view> names;
};

// All bits of the names.
constexpr std::uint32_t NamedBits(Names names) {
    std::uint32_t bits = 0;
    for (const Name& name : names) bits |= static_cast<std::uint32_t>(name.value);
    return bits;
}

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
template <class T>
std::string ValueText(Enum<T> field) {
    const std::string_view name = NameOf(field.names, static_cast<std::int64_t>(field.value));
    return name.empty() ? toml::Int(static_cast<std::int64_t>(field.value)) : toml::String(name);
}
inline std::string ValueText(Code field) {
    const std::uint32_t code = (field.value & field.mask) >> std::countr_zero(field.mask);
    const std::string_view name = NameOf(field.names, code);
    return name.empty() ? toml::Int(code) : toml::String(name);
}
inline std::string ValueText(Bits field) {
    std::uint32_t left = field.value & NamedBits(field.names);
    std::vector<std::string> items;
    for (const Name& name : field.names) {
        const auto bits = static_cast<std::uint32_t>(name.value);
        if (bits == 0 || (left & bits) != bits) continue;
        items.push_back(toml::String(name.name));
        left &= ~bits;
    }
    left |= field.value & field.numbered & ~NamedBits(field.names);
    for (int bit = 0; bit < 32; ++bit)
        if (left & (1u << bit)) items.push_back(toml::Int(bit + field.firstNumber));
    return toml::Array(items);
}
inline std::string ValueText(Masked field) { return toml::Int(field.value & field.mask); }
template <std::size_t N>
std::string ValueText(FloatBitsValues<N> field) {
    std::vector<std::string> items;
    for (std::uint32_t bits : field.values) items.push_back(toml::Float(std::bit_cast<float>(bits)));
    return toml::Array(items);
}
template <class T>
std::string ValueText(Bool<T> field) {
    if (field.value == T{0} || field.value == T{1}) return toml::Bool(field.value != T{0});
    return toml::Int(static_cast<std::int64_t>(field.value));
}
template <class T>
std::string ValueText(Table<T> field);
template <class T>
std::string ValueText(VectorTable<T> field);

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

// A field that is written only in some records (an animation key's parts by its kind): `Skipped()` true leaves it out.
template <class T>
concept MaySkip = requires(const T& field) {
    { field.Skipped() } -> std::convertible_to<bool>;
};

// A record's fields as (key, value text) pairs, in the list's order.
class PairWriter {
public:
    template <class T>
    void operator()(std::string_view key, T&& field) {
        if constexpr (MaySkip<std::remove_cvref_t<T>>)
            if (field.Skipped()) return;
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
// The elements that differ from a new element.
template <class T>
std::string TableText(const T* values, std::size_t count, std::span<const std::string_view> names) {
    const std::string unset = ValueText(T{});
    std::vector<std::pair<std::string, std::string>> pairs;
    for (std::size_t i = 0; i < count && i < names.size(); ++i) {
        std::string text = ValueText(values[i]);
        if (text != unset) pairs.emplace_back(std::string(names[i]), std::move(text));
    }
    return toml::InlineTable(pairs);
}
template <class T>
std::string ValueText(Table<T> field) {
    return TableText(field.values, field.names.size(), field.names);
}
template <class T>
std::string ValueText(VectorTable<T> field) {
    return TableText(field.values.data(), field.values.size(), field.names);
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
inline bool IsUnset(Masked field) { return (field.value & field.mask) == 0; }
template <std::size_t N>
bool IsUnset(FloatBitsValues<N> field) {
    return IsUnset(field.values);
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

// A name of `names` or (a value no name has) a number.
inline bool ReadCode(const toml::Value& value, Names names, std::int64_t& code) {
    if (value.IsString()) return ValueOf(names, value.String(), code);
    if (!value.IsInt()) return false;
    code = value.Int();
    return true;
}
template <class T>
bool ReadValue(const toml::Value& value, Enum<T> field) {
    std::int64_t code = 0;
    if (!ReadCode(value, field.names, code)) return false;
    field.value = static_cast<T>(code);
    return true;
}
inline bool ReadValue(const toml::Value& value, Code field) {
    std::int64_t code = 0;
    const int shift = std::countr_zero(field.mask);
    if (!ReadCode(value, field.names, code) || code < 0 || (static_cast<std::uint64_t>(code) << shift & ~std::uint64_t{field.mask}) != 0)
        return false;
    field.value = (field.value & ~field.mask) | static_cast<std::uint32_t>(code << shift);
    return true;
}
inline bool ReadValue(const toml::Value& value, Bits field) {
    if (!value.IsArray()) return false;
    std::uint32_t bits = 0;
    for (int i = 0; i < value.Size(); ++i) {
        const toml::Value item = value.At(i);
        std::int64_t named = 0;
        if (item.IsString()) {
            if (!ValueOf(field.names, item.String(), named)) return false;
            bits |= static_cast<std::uint32_t>(named);
            continue;
        }
        const std::int64_t bit = item.IsInt() ? item.Int() - field.firstNumber : -1;
        if (bit < 0 || bit > 31 || (field.numbered & (1u << bit)) == 0) return false;
        bits |= 1u << bit;
    }
    field.value = (field.value & ~(NamedBits(field.names) | field.numbered)) | bits;
    return true;
}
inline bool ReadValue(const toml::Value& value, Masked field) {
    std::uint32_t read = 0;
    if (!toml::Read(value, &read) || (read & ~field.mask) != 0) return false;
    field.value = (field.value & ~field.mask) | read;
    return true;
}
template <std::size_t N>
bool ReadValue(const toml::Value& value, FloatBitsValues<N> field) {
    float read[N] = {};
    if (!ReadValue(value, read)) return false;
    for (std::size_t i = 0; i < N; ++i) field.values[i] = std::bit_cast<std::uint32_t>(read[i]);
    return true;
}
template <class T>
bool ReadValue(const toml::Value& value, Bool<T> field) {
    if (value.IsBool()) field.value = value.Bool() ? T{1} : T{0};
    else if (value.IsInt()) field.value = static_cast<T>(value.Int());
    else return false;
    return true;
}
// The elements given, by name (each read as the element's type), the others kept.
template <class T>
bool ReadTable(const toml::Value& value, T* values, std::span<const std::string_view> names) {
    if (!value.IsTable()) return false;
    bool all = true;
    for (int k = 0; k < value.Size(); ++k) {
        std::size_t i = 0;
        while (i < names.size() && names[i] != value.KeyAt(k)) ++i;
        all = i < names.size() && ReadValue(value.ValueAt(k), values[i]) && all;
    }
    return all;
}
template <class T>
bool ReadValue(const toml::Value& value, Table<T> field) {
    return ReadTable(value, field.values, field.names);
}
template <class T>
bool ReadValue(const toml::Value& value, VectorTable<T> field) {
    if (!value.IsTable()) return false;
    if (field.values.size() < field.names.size()) field.values.resize(field.names.size());
    return ReadTable(value, field.values.data(), field.names);
}

// The names a field takes, for a message ("" for a field without names).
template <class T>
std::string ValidNames(const T&) {
    return {};
}
template <class T>
std::string ValidNames(const Enum<T>& field) {
    return NameList(field.names);
}
inline std::string ValidNames(const Code& field) { return NameList(field.names); }
inline std::string ValidNames(const Bits& field) {
    return field.numbers.empty() ? NameList(field.names) : NameList(field.names) + "; or " + std::string(field.numbers);
}
std::string KeyList(std::span<const std::string_view> names);
template <class T>
std::string ValidNames(const Table<T>& field) {
    return KeyList(field.names);
}
template <class T>
std::string ValidNames(const VectorTable<T>& field) {
    return KeyList(field.names);
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
        if (!read) valid = ValidNames(field);
    }

    // The message of a value the field does not take.
    std::string Refused() const {
        std::string text = std::string(key) + " has a value it does not take";
        if (!valid.empty()) text += " (one of: " + valid + ")";
        return text;
    }
    template <class T>
    void Unread(std::string_view name, T&& field) {
        (*this)(name, field);
    }

    std::string_view key;
    toml::Value value;
    bool found = false;   // the record has the key
    bool read = false;    // and the value was of a type it takes
    std::string valid;    // the names the field takes, after a value it refused
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

struct NoNewRecord {
    template <class Record>
    void operator()(Record&, std::size_t) const {}
};

// Every record of `records` as an array of tables `table`: its key `key` and each field whose text differs from a
// new record's (the type's defaults, `onNew(record, index)` as MergeRecords makes one at that index), so a reader
// that starts from a new record reads it back.
template <class Record, class OnNew = NoNewRecord>
void WriteRecords(toml::Writer& writer, std::string_view table, const std::vector<Record>& records,
                  std::string_view key, OnNew onNew = {}) {
    for (std::size_t i = 0; i < records.size(); ++i) {
        Record unset{};
        onNew(unset, i);
        PairWriter defaults;
        Fields(unset, defaults);
        writer.ArrayTable(table);
        PairWriter pairs;
        Fields(const_cast<Record&>(records[i]), pairs);
        for (const auto& [name, value] : pairs.pairs) {
            bool same = false;
            for (const auto& [unsetName, unsetValue] : defaults.pairs)
                if (unsetName == name) same = unsetValue == value;
            if (name == key || !same) writer.Pair(name, value);
        }
    }
}

// Each record of the TOML array of tables `table` merged into `records` by its key (`key`, compared with the record's
// `keyOf(record)` exactly, as the tables' indexes compare): the first record of the key that this file has not merged
// yet takes the fields given and keeps the others; a key no record has left is a new record (the type's defaults and
// the fields given) at the end, so every record keeps its index. A record that fails to load is logged ("<what>:
// <path> line n: ...") and left out whole (a record of the game's keeps what it had, and counts as merged): one
// without its key, with an unknown key, a name its field does not have (the message lists the valid ones) or a value
// of a type the field does not take. `onNew(record, index)` finishes a new record before the fields are read (a place
// that is its own field).
template <class Record, class KeyOf, class OnNew = NoNewRecord>
void MergeRecords(const toml::Value& table, std::vector<Record>& records, const char* key, KeyOf keyOf,
                  const char* what, const std::string& path, OnNew onNew = {}) {
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
        const bool added = index == records.size();
        // The fields read into a copy first (some record types cannot be assigned): the record changes only when
        // every field loads, read again into it.
        auto readInto = [&item](Record& record, std::string& refused, int& line) {
            for (int k = 0; k < item.Size() && refused.empty(); ++k) {
                FieldReader reader(item.KeyAt(k), item.ValueAt(k));
                Fields(record, reader);
                if (!reader.found) refused = "unknown key " + std::string(item.KeyAt(k));
                else if (!reader.read) refused = reader.Refused();
                line = item.ValueAt(k).Line();
            }
        };
        Record copy = added ? Record{} : records[index];
        if (added) onNew(copy, index);
        std::string refused;
        int line = item.Line();
        readInto(copy, refused, line);
        if (!added) merged[index] = true;
        if (!refused.empty()) {
            game::Warning("%s: %s line %d: %s; the record is left out", what, path, line, refused);
            continue;
        }
        if (added) {
            records.push_back(copy);
            merged.push_back(true);
        } else {
            readInto(records[index], refused, line);
        }
    }
}

// The table `table` (each key a name of `names`, its value a string) merged into `values` at the names' values; an
// unknown name or a value that is not a string is logged and left out (a missing table: nothing).
void MergeNamedStrings(const toml::Value& table, Names names, std::string* values, const char* what,
                       const std::string& path);

// Logs each top-level key of `root` that is neither one of `tables` nor `replace`.
void CheckTables(const toml::Value& root, std::initializer_list<std::string_view> tables, const char* what,
                 const std::string& path);

} // namespace dbtoml
