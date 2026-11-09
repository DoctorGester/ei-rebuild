// The layered path map (src/platform/path_layers.h; docs/notes/mods.md) on a game folder and a mod folder of its own
// in the temporary folder: full paths, the mod's files read and written in place of the game's (not its root's files
// or archives), its saves apart, a folder search merged; the same for a mod shipped as a zip (zip_folders.h), read
// in place. ei_unit_check (unit_check.cpp) and the POSIX port check run it; it sets the program's mod, so it runs last.
#include "../../src/platform/path_layers.h"
#include "../../src/platform/platform.h"
#include "miniz/miniz.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
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

// The zip `file` made of `entries` (name, bytes; a name ending in '/' a folder): deflated, a .res stored.
void WriteZip(const fs::path& file, const std::vector<std::pair<std::string, std::string>>& entries) {
    mz_zip_archive zip{};
    mz_zip_writer_init_heap(&zip, 0, 0);
    for (const auto& [name, bytes] : entries) {
        const bool stored = name.ends_with(".res") || name.ends_with("/");
        mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(),
                              stored ? MZ_NO_COMPRESSION : MZ_DEFAULT_COMPRESSION);
    }
    void* made = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&zip, &made, &size);
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary).write(static_cast<const char*>(made), static_cast<std::streamsize>(size));
    mz_zip_writer_end(&zip);
}

// A mod shipped as a zip (zip_folders.h) over the game folder `game` (`g` in the game's form): one of its mod folder
// (a top folder holding config.reg) and one with its files at its root.
void ZipChecks(const fs::path& game, const std::string& g) {
    const std::string zip = g + "\\starter\\Mods\\Astral.zip", folder = zip + "\\Astral";
    WriteZip(game / "starter" / "Mods" / "Astral.zip",
             {{"Astral/", ""}, {"Astral/config.reg", "zip"}, {"Astral/Config/ai.reg", "zip mod"},
              {"Astral/Res/texts.res", "zip archive"}, {"Astral/Maps/zone2.mob", "zip map"},
              {"Astral/Maps/bz9.mob", "z"}, {"Astral/Maps/Sub/note.txt", "a note"}, {"Astral/readme.txt", "zip"}});
    if (!platform::SwitchMod(g, zip)) {
        ++failures;
        std::printf("ei_unit_check: path layers: SwitchMod refused %s\n", zip.c_str());
        return;
    }
    Expect("the zip mod's folder", platform::ModFolder(), folder);
    Expect("the zip mod's name", platform::ModName(), "Astral");
    Expect("the zip's file", platform::SystemPath(".\\config\\ai.reg", platform::PathUse::Read), folder + "\\config\\ai.reg");
    Expect("a file the zip lacks", platform::SystemPath(".\\config\\keyboard.ini", platform::PathUse::Read),
           ".\\config\\keyboard.ini");
    Expect("a zip's archive", platform::SystemPath(".\\res\\texts.res", platform::PathUse::Read), ".\\res\\texts.res");
    Expect("a zip's root file", platform::SystemPath(".\\readme.txt", platform::PathUse::Read), ".\\readme.txt");
    Expect("a zip mod's save", platform::SystemPath(".\\saves\\current\\info.sav", platform::PathUse::Write),
           g + "\\saves\\Astral\\current\\info.sav");
    std::string bytes;
    Expect("the zip's file read whole", platform::ReadWholeFile(".\\config\\ai.reg", &bytes) ? bytes : "(failed)", "zip mod");
    Expect("the zip's archive read whole",
           platform::ReadWholeFile(folder + "\\RES\\Texts.res", &bytes) ? bytes : "(failed)", "zip archive");
    platform::File file;
    char text[8] = {};
    std::string got = "(no open)";
    if (file.Open(".\\maps\\zone2.mob", platform::FileMode::Read)) {
        got = std::to_string(file.Size()) + " " + std::to_string(file.Seek(4, platform::SeekFrom::Begin));
        got += " " + std::to_string(file.Read(text, sizeof(text))) + " " + text;
        got += file.ReadAt(0, text, 3) ? " " + std::string(text, 3) : " (no read)";
        got += file.ReadAt(5, text, 3) ? " (read past the end)" : "";
    }
    Expect("the zip's file", got, "7 4 3 map zip");
    platform::MappedFile mapped;
    Expect("the zip's file mapped",
           mapped.Open(".\\maps\\bz9.mob") ? std::string(static_cast<const char*>(mapped.Data()), mapped.Size()) : "(no)",
           "z");
    Expect("a write into the zip", platform::File().Open(".\\config\\ai.reg", platform::FileMode::Create) ? "opened" : "refused",
           "refused");
    Expect("a zip's file", platform::IsFile(".\\maps\\Sub\\NOTE.txt") ? "yes" : "no", "yes");
    Expect("a zip's folder", platform::PathExists(".\\maps\\sub") && !platform::IsFile(".\\maps\\sub") ? "yes" : "no", "yes");
    std::string names;
    for (const platform::FolderEntry& entry : platform::FolderEntries(".\\maps\\*.mob")) {
        if (entry.IsDots()) continue;
        names += entry.name + (entry.path == g + "\\maps\\" + entry.name ? "" : "(" + entry.path + ")") + " ";
        if (entry.name == "zone2.mob" && entry.size != 7) names += "(not the zip's) ";
    }
    Expect("the search merged with the zip's", names, "bz9.mob zone1.mob zone2.mob ");
    names.clear();
    const std::vector<platform::FolderLayer> layers = platform::FilesByLayer(".\\maps");
    for (const platform::LayerFile& each : layers.front().files) names += each.name + " ";
    Expect("the zip's files by layer", names, "bz9.mob Sub\\note.txt zone2.mob ");

    const std::string flat = g + "\\starter\\Mods\\Flat.zip";
    WriteZip(game / "starter" / "Mods" / "Flat.zip", {{"config.reg", "flat"}, {"Config/ai.reg", "flat mod"}});
    Expect("a switch to a zip of files", platform::SwitchMod(g, flat) ? "yes" : "no", "yes");
    Expect("its folder", platform::ModFolder(), flat);
    Expect("its name", platform::ModName(), "Flat");
    Expect("its file", platform::ReadWholeFile(".\\config\\ai.reg", &bytes) ? bytes : "(failed)", "flat mod");
    Expect("a zip's root", platform::ModRoot(flat), flat);
    Expect("a folder mod's root", platform::ModRoot(g + "\\starter\\Mods\\Astral"), g + "\\starter\\Mods\\Astral");
    Expect("no zip", platform::ModRoot(g + "\\starter\\Mods\\None.zip"), "");
}

} // namespace

int RunPathLayersChecks() {
    failures = 0;
    std::error_code error;
    const fs::path root = fs::temp_directory_path() / "ei_unit_path_layers";
    fs::remove_all(root, error);
    // The game folder reached through a symbolic link where the system makes one (macOS's temporary folder in /var is
    // in /private/var): its paths keep the link's.
    fs::create_directories(root / "real", error);
    fs::create_directory_symlink(root / "real", root / "link", error);
    const fs::path game = (error ? root / "real" : root / "link") / "game", mod = game / "starter" / "Mods" / "Astral";
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
    if (!platform::SwitchMod(g, m)) {
        ++failures;
        std::printf("ei_unit_check: path layers: SwitchMod refused %s\n", m.c_str());
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
            names += entry.name + (entry.path == g + "\\maps\\" + entry.name ? "" : "(" + entry.path + ")") + " ";
            if (entry.name == "zone2.mob" && entry.size != 3) names += "(not the mod's) ";
        }
        Expect("the merged search", names, "bz1h.mob zone1.mob zone2.mob ");
        names.clear();
        for (const platform::FolderEntry& entry : platform::FolderEntries(".\\*.*"))
            if (!entry.IsDots()) names += entry.name + " ";
        Expect("the root's search", names, "Config Maps readme.txt Res saves starter ");
        // A switch to the game's own files, then back to the mod.
        Expect("a switch to no mod", platform::SwitchMod(g, "") ? "yes" : "no", "yes");
        Expect("no mod's folder", platform::ModFolder(), "");
        Expect("the game's file again", platform::SystemPath(".\\config\\ai.reg", platform::PathUse::Read),
               ".\\config\\ai.reg");
        Expect("a missing mod refused", platform::SwitchMod(g, g + "\\starter\\Mods\\None") ? "yes" : "no", "no");
        Expect("no mod after a refusal", platform::ModFolder(), "");
        Expect("a switch back", platform::SwitchMod(g, m) ? "yes" : "no", "yes");
        Expect("the mod's file again", platform::SystemPath(".\\config\\ai.reg", platform::PathUse::Read),
               m + "\\config\\ai.reg");
        ZipChecks(game, g);
        platform::SwitchMod(g, "");
    }
    platform::SetCurrentFolder(before, &code);
    fs::remove_all(root, error);
    return failures;
}
