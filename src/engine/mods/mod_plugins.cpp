// The mods' game patches carried as code (mod_plugins.h).
#include "engine/mods/mod_plugins.h"

#include "engine/mods/mod_folders.h"
#include "engine/data/registry.h"
#include "lib/text_case.h"

#include <string>
#include <string_view>
#include <vector>

namespace modplugins {
namespace {

// Whether the mod's config.reg names `plugin` under [DEV] Plugin (a string or an array of them). Read on each
// question: they come at a new game and a load, and the mod may change between them.
bool ModLoads(std::string_view plugin) {
    const std::string& mod = modfolders::ModPath();
    if (mod.empty()) return false;
    FileRegistry registry;
    if (!registry.Open(mod + "config.reg")) return false;
    std::vector<std::string> plugins;
    registry.GetStringArray("DEV", "Plugin", &plugins);
    for (const std::string& name : plugins)
        if (text::CompareIgnoringCase(name, plugin) == 0) return true;
    return false;
}

} // namespace

bool FourShopClasses() { return ModLoads("fixshops.plg"); }

} // namespace modplugins
