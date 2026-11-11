// A mod's content hash (src/engine/mods/mod_hash.h) on a mod folder of its own in the temporary folder: the files it
// counts, a zip of the same folder hashing as the folder, a counted file's change changing it and an uncounted one's
// not. ei_unit_check (unit_check.cpp) and the POSIX port check run it.
#include "../../src/engine/mods/mod_hash.h"
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

void Expect(const char* what, bool got) {
    if (got) return;
    ++failures;
    std::printf("ei_unit_check: mod hash: %s\n", what);
}

using Files = std::vector<std::pair<std::string, std::string>>;   // a path below the mod ('/' between parts), bytes

void WriteFolder(const fs::path& folder, const Files& files) {
    for (const auto& [name, bytes] : files) {
        const fs::path file = folder / fs::path(name).make_preferred();
        fs::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << bytes;
    }
}

// The zip `file` of `files` below its one top folder `top`.
void WriteZip(const fs::path& file, const std::string& top, const Files& files) {
    mz_zip_archive zip{};
    mz_zip_writer_init_heap(&zip, 0, 0);
    for (const auto& [name, bytes] : files) {
        const std::string entry = top + "/" + name;
        mz_zip_writer_add_mem(&zip, entry.c_str(), bytes.data(), bytes.size(),
                              static_cast<mz_uint>(MZ_DEFAULT_COMPRESSION));
    }
    void* made = nullptr;
    size_t size = 0;
    mz_zip_writer_finalize_heap_archive(&zip, &made, &size);
    std::ofstream(file, std::ios::binary).write(static_cast<const char*>(made), static_cast<std::streamsize>(size));
    mz_zip_writer_end(&zip);
}

// A path in the game's form.
std::string Game(const fs::path& path) {
    std::string text = path.string();
    std::replace(text.begin(), text.end(), '/', '\\');
    return text;
}

} // namespace

int RunModHashChecks() {
    failures = 0;
    Expect("config.reg counts", modhash::Counted("config.reg") && modhash::Counted("config.reg.toml"));
    Expect("a map's file counts", modhash::Counted("maps\\zone1.mob") && modhash::Counted("maps\\zone1\\heights.png"));
    Expect("ai.reg counts, a light's .ini not", modhash::Counted("config\\ai.reg") && !modhash::Counted("config\\lights.ini"));
    Expect("the databases and texts count", modhash::Counted("res\\database.res") &&
                                                modhash::Counted("res\\databaselmp\\items.idb.toml") &&
                                                modhash::Counted("res\\textslmp.res") && modhash::Counted("res\\texts\\texts.toml"));
    Expect("the data effects count", modhash::Counted("res\\effects\\showcase_aura.effect.toml"));
    Expect("the spell scripts count", modhash::Counted("res\\spells\\chain_spark.lua"));
    Expect("looks and notes do not count", !modhash::Counted("res\\textures.res") &&
                                               !modhash::Counted("res\\figures\\a.glb") && !modhash::Counted("readme.txt") &&
                                               !modhash::Counted("movies\\intro.bik"));

    std::error_code error;
    const fs::path root = fs::temp_directory_path() / "ei_unit_mod_hash";
    fs::remove_all(root, error);
    const Files files = {{"config.reg.toml", "[MOD]\nTitle = \"T\"\n"}, {"readme.txt", "notes"},
                         {"Maps/zone1.mob", "map"}, {"Maps/zone1/map.toml", "name = \"Zone1\""},
                         {"Config/ai.reg", "ai"}, {"Config/tutorial.ini", "seen"},
                         {"Res/database/units.udb.toml", "units"}, {"Res/textures/a.png", "pixels"}};
    WriteFolder(root / "Folder", files);
    fs::create_directories(root / "Zip", error);
    WriteZip(root / "Zip" / "Mod.zip", "Mod", files);
    modhash::Count count;
    const std::uint64_t folder = modhash::ContentHash(Game(root / "Folder"), &count);
    Expect("the counted files", count.files == 5);
    Expect("the zip of the folder hashes as the folder", modhash::ContentHash(Game(root / "Zip" / "Mod.zip" / "Mod")) == folder);

    WriteFolder(root / "Folder", {{"readme.txt", "other notes"}, {"Res/textures/a.png", "other pixels"}});
    Expect("an uncounted file's change keeps the hash", modhash::ContentHash(Game(root / "Folder")) == folder);
    WriteFolder(root / "Folder", {{"Maps/zone1.mob", "maq"}});
    Expect("a counted file's change changes it", modhash::ContentHash(Game(root / "Folder")) != folder);
    fs::remove_all(root, error);
    return failures;
}
