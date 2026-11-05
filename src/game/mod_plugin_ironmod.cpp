// IronMod's fixfslpatei.plg as code (mod_plugin_ironmod.h).
#include "mod_plugin_ironmod.h"

#include "mod_folders.h"
#include "registry.h"
#include "script.h"
#include "lib/text_case.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace modplugins {
namespace {

constexpr std::string_view kPlugin = "fixfslpatei.plg";

int g_consoleScripts = 0;

// The commands patches.txt renamed in the keyword table (a letter or more made '0': "GiveMoney" became "GiveM0ne0").
constexpr ScriptFunction kHidden[] = {
    ScriptFunction::SetPlayer,         ScriptFunction::SetDiplomacy,   ScriptFunction::RemoveUnitFromServer,
    ScriptFunction::KillUnit,          ScriptFunction::InflictDamage,  ScriptFunction::GodMode,
    ScriptFunction::GiveUnitSpell,     ScriptFunction::GiveUnitQuestItem, ScriptFunction::GiveStrength,
    ScriptFunction::GiveSkill,         ScriptFunction::GiveQuestItem,  ScriptFunction::GiveMoney,
    ScriptFunction::GiveItem,          ScriptFunction::GiveIntelligence, ScriptFunction::GiveDexterity,
    ScriptFunction::CastSpellUnit,     ScriptFunction::CastSpellPoint, ScriptFunction::AddMob,
};

// Whether the mod's config.reg names the plugin under [DEV] Plugin (a string or an array of them). Read on each
// question: the questions are few (a key's press, the console's lines, the bindings' load and save) and the mod may
// change between them.
bool IronMod() {
    const std::string& mod = modfolders::ModPath();
    if (mod.empty()) return false;
    FileRegistry registry;
    if (!registry.Open(mod + "config.reg")) return false;
    std::vector<std::string> plugins;
    registry.GetStringArray("DEV", "Plugin", &plugins);
    return std::ranges::any_of(plugins, [](const std::string& name) { return text::CompareIgnoringCase(name, kPlugin) == 0; });
}

} // namespace

const char* KeyBindingsFile() { return IronMod() ? "config\\kbdinput.ini" : "config\\keyboard.ini"; }

const char* CheatWordReply() { return IronMod() ? "NoCheating" : "Activated!"; }

bool QuickSavesOff() { return IronMod(); }

ConsoleScript::ConsoleScript() { ++g_consoleScripts; }

ConsoleScript::~ConsoleScript() { --g_consoleScripts; }

bool HidesKeyword(ScriptFunction id) {
    if (g_consoleScripts == 0) return false;
    return std::ranges::find(kHidden, id) != std::end(kHidden) && IronMod();
}

} // namespace modplugins
