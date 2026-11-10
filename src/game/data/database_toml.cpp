// The game databases as TOML: what the field lists' readers and writers share (database_toml.h).
#include "game/data/database_toml.h"

#include "game/data/resource.h"

namespace dbtoml {

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
