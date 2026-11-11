// The game databases as TOML: what the field lists' readers and writers share (database_toml.h).
#include "engine/data/database_toml.h"

#include "engine/data/resource.h"

namespace dbtoml {

std::string NameList(Names names) {
    std::string list;
    for (const Name& name : names) {
        if (!list.empty()) list += ", ";
        list += name.name;
    }
    return list;
}

std::string KeyList(std::span<const std::string_view> names) {
    std::string list;
    for (std::string_view name : names) {
        if (!list.empty()) list += ", ";
        list += name;
    }
    return list;
}

std::string_view NameOf(Names names, std::int64_t value) {
    for (const Name& name : names)
        if (name.value == value) return name.name;
    return {};
}

bool ValueOf(Names names, std::string_view name, std::int64_t& value) {
    for (const Name& entry : names)
        if (entry.name == name) {
            value = entry.value;
            return true;
        }
    return false;
}

void CheckTables(const toml::Value& root, std::initializer_list<std::string_view> tables, const char* what,
                 const std::string& path) {
    for (int i = 0; i < root.Size(); ++i) {
        const std::string_view key = root.KeyAt(i);
        bool known = IsReplaceKey(key, root.ValueAt(i));
        for (std::string_view table : tables) known = known || key == table;
        if (!known)
            game::Warning("%s: %s line %d: unknown table %s", what, path, root.ValueAt(i).Line(), std::string(key));
    }
}

} // namespace dbtoml
