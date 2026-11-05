// The layered path map (src/platform/path_layers.h; docs/notes/mods.md) on a game folder and a mod folder of its own
// in the temporary folder: full paths, the mod's files read and written in place of the game's (not its root's files
// or archives), its saves apart, a folder search merged. ei_unit_check (unit_check.cpp) and the POSIX port check
// run it; it sets the program's mod, so it runs last.
#include "../../src/platform/path_layers.h"
#include "../../src/platform/platform.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

int failures = 0;

void Expect(const char* what, const std::string& got, const std::string& want) {
    if (got == want) return;
    ++failures;
    std::printf("ei_unit_check: path layers: %s gave '%s', wanted '%s'\n", what, got.c_str(), want.c_str());
}

void Write(const fs::path& file, const char* text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// A path in the game's form.
std::string Game(const fs::path& path) {
    std::string text = path.string();
    std::replace(text.begin(), text.end(), '/', '\\');
    return text;
}

} // namespace

int RunPathLayersChecks() {
    failures = 0;
    std::error_code error;
    const fs::path root = fs::temp_directory_path() / "ei_unit_path_layers";
    fs::remove_all(root, error);
    const fs::path game = root / "game", mod = game / "starter" / "Mods" / "Astral";
    Write(game / "config" / "ai.reg", "game");
    Write(game / "config" / "keyboard.ini", "game");
    Write(game / "res" / "texts.res", "game");
    Write(game / "maps" / "zone1.mob", "game");
    Write(game / "maps" / "zone2.mob", "game");
    Write(game / "readme.txt", "game");
    Write(mod / "Config" / "ai.reg", "mod");
    Write(mod / "Res" / "texts.res", "mod");
    Write(mod / "Maps" / "zone2.mob", "mod");
    Write(mod / "Maps" / "bz1h.mob", "mod");
    Write(mod / "readme.txt", "mod");
    Write(mod / "config.reg", "mod");
    const std::string g = Game(game), m = Game(mod);

    Expect("a full path", platform::FullGamePath(g + "\\maps\\.\\..\\config\\\\ai.reg"), g + "\\config\\ai.reg");
    std::string_view rest;
    Expect("a path below", platform::PathBelow(g + "\\Maps\\zone1.mob", g, &rest) ? std::string(rest) : "(not)",
           "Maps\\zone1.mob");
    Expect("a path beside", platform::PathBelow(g + "x\\a", g, &rest) ? std::string(rest) : "(not)", "(not)");

    const std::string before = platform::CurrentFolder();
    std::uint32_t code = 0;
    platform::SetCurrentFolder(g, &code);
    Expect("the current folder's path", platform::FullGamePath(".\\maps\\zone1.mob"), g + "\\maps\\zone1.mob");
    if (!platform::UseMod(g, m)) {
        ++failures;
        std::printf("ei_unit_check: path layers: UseMod refused %s\n", m.c_str());
    } else {
        Expect("the mod's folder", platform::ModFolder(), m);
        Expect("the mod's file", platform::SystemPath(".\\config\\ai.reg", platform::PathUse::Read), m + "\\config\\ai.reg");
        Expect("a file the mod lacks", platform::SystemPath(".\\config\\keyboard.ini", platform::PathUse::Read),
               ".\\config\\keyboard.ini");
        Expect("an archive", platform::SystemPath(".\\res\\texts.res", platform::PathUse::Read), ".\\res\\texts.res");
        Expect("a root file", platform::SystemPath(".\\readme.txt", platform::PathUse::Read), ".\\readme.txt");
        Expect("a write of the mod's file", platform::SystemPath("config\\ai.reg", platform::PathUse::Write),
               m + "\\config\\ai.reg");
        Expect("a write of the game's file", platform::SystemPath("config\\keyboard.ini", platform::PathUse::Write),
               "config\\keyboard.ini");
        Expect("a save", platform::SystemPath(".\\saves\\current\\info.sav", platform::PathUse::Write),
               g + "\\saves\\Astral\\current\\info.sav");
        Expect("the saves' folder made", fs::is_directory(game / "saves" / "Astral") ? "yes" : "no", "yes");
        std::string mapped = platform::SystemPath(m + "\\Maps\\zone2.mob", platform::PathUse::Read);
        Expect("a path in the mod", mapped, m + "\\Maps\\zone2.mob");
        std::string names;
        for (const platform::FolderEntry& entry : platform::FolderEntries(".\\maps\\*.mob")) {
            if (entry.IsDots()) continue;
            // POSIX's entries keep the pattern's "." (game\.\maps\...): the same file.
            const std::string path = platform::FullGamePath(entry.path);
            names += entry.name + (path == g + "\\maps\\" + entry.name ? "" : "(" + path + ")") + " ";
            if (entry.name == "zone2.mob" && entry.size != 3) names += "(not the mod's) ";
        }
        Expect("the merged search", names, "bz1h.mob zone1.mob zone2.mob ");
        names.clear();
        for (const platform::FolderEntry& entry : platform::FolderEntries(".\\*.*"))
            if (!entry.IsDots()) names += entry.name + " ";
        Expect("the root's search", names, "Config Maps readme.txt Res saves starter ");
    }
    platform::SetCurrentFolder(before, &code);
    fs::remove_all(root, error);
    return failures;
}
