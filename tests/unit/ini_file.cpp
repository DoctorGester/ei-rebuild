// IniFile (src/engine/data/ini_file.h), the game's .ini reader, on a file written for it: case, comments, quotes, lines
// without '=', the first of two keys, lists, numbers, the asset code page and a missing file. ei_unit_check
// (unit_check.cpp) and Linux's ei_port_check run these and count their failures. The game's own files (movie.ini, the
// lights tables) read as Windows' profile calls read them (C73 compared every one in the installs).
#include "engine/data/ini_file.h"
#include "../../src/platform/platform.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(cond)) {                                                                    \
            ++g_failures;                                                                 \
            std::printf("  ini file check (line %d): %s\n", __LINE__, #cond);             \
        }                                                                                 \
    } while (0)

constexpr const char* kText =
    "; a comment before any section\r\n"
    "[Game]\r\n"
    "Count = 42\r\n"
    "Name=\"quoted value\"\r\n"
    "Single='x'\r\n"
    "  ; a comment = with an equals sign\r\n"
    "no equals sign here\r\n"
    "count=7\r\n"
    "Bad=abc\r\n"
    "\r\n"
    "[sunlight]\r\n"
    "time00 = 63, 93, 157\r\n"
    "time06=255,248,207\r\n"
    "Text=\xC0\xE1\r\n"
    "[Empty]\r\n"
    "[Last]\n"
    "End=1";

} // namespace

int RunIniFileChecks() {
    g_checks = g_failures = 0;
    const std::string folder = (std::filesystem::temp_directory_path() / "ei_unit_check_ini").string();
    std::filesystem::create_directories(folder);
    const std::string path = folder + "/test.ini";
    std::uint32_t error = 0;
    CHECK(platform::WriteWholeFile(path, kText, &error));

    IniFile ini;
    ini.Load(path);
    CHECK(ini.GetInt("game", "COUNT", 0) == 42);              // case of section and key; the first of two keys
    CHECK(ini.GetString("GAME", "name", "") == "quoted value"); // one pair of quotes stripped
    CHECK(ini.GetString("Game", "Single", "") == "x");
    CHECK(ini.GetString("Game", "absent", "default") == "default");
    CHECK(ini.GetInt("Game", "Bad", 5) == 5);                  // no number: the default
    CHECK(ini.GetInt("Game", "absent", -1) == -1);
    CHECK(ini.GetKeyNames("Game") == (std::vector<std::string>{"Count", "Name", "Single", "count", "Bad"}));
    CHECK(ini.GetKeyNames("sunlight") == (std::vector<std::string>{"time00", "time06", "Text"}));
    CHECK(ini.GetIntArray("sunlight", "time00") == (std::vector<std::uint32_t>{63, 93, 157}));
    CHECK(ini.GetList("sunlight", "time06", ',') == (std::vector<std::string>{"255", "248", "207"}));
    CHECK(ini.GetString("sunlight", "Text", "") == "\xD0\x90\xD0\xB1");   // the asset code page's bytes as UTF-8
    CHECK(ini.GetKeyNames("Empty").empty() && ini.GetList("Empty", "x", ',').empty());
    CHECK(ini.GetString("Last", "End", "") == "1");             // no line end after the last line

    IniFile missing;
    missing.Load(folder + "/missing.ini");
    CHECK(missing.GetString("Game", "Count", "none") == "none" && missing.GetKeyNames("Game").empty());

    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
    std::printf("ei_unit_check: ini file: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
