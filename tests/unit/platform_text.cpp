// The Windows platform's text boundary (src/platform/win32): a Cyrillic name that goes in as UTF-8 comes out as the
// same UTF-8 from an environment variable (EI_GAME_ROOT, the harness's folders) and from the current folder (ticket
// U6: the narrow environment call turned a Cyrillic install folder into question marks). ei_unit_check
// (unit_check.cpp) runs it and counts the failures.
#include "../../src/platform/platform.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

int failures = 0;

void Expect(const char* what, const std::string& got, const std::string& want) {
    if (got == want) return;
    ++failures;
    std::printf("ei_unit_check: platform text: %s gave '%s', wanted '%s'\n", what, got.c_str(), want.c_str());
}

} // namespace

int RunPlatformTextChecks() {
    failures = 0;
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / L"ei_unit_Игры Злые";
    const std::u8string u8 = folder.u8string();
    const std::string utf8(u8.begin(), u8.end());
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error) {
        ++failures;
        std::printf("ei_unit_check: platform text: cannot make %s\n", utf8.c_str());
        return failures;
    }

    _wputenv_s(L"EI_UNIT_PLATFORM_TEXT", folder.wstring().c_str());
    Expect("an environment variable", platform::EnvironmentVariable("EI_UNIT_PLATFORM_TEXT"), utf8);
    _wputenv_s(L"EI_UNIT_PLATFORM_TEXT", L"");
    Expect("an unset variable", platform::EnvironmentVariable("EI_UNIT_PLATFORM_TEXT"), "");

    const std::string before = platform::CurrentFolder();
    std::uint32_t code = 0;
    if (!platform::SetCurrentFolder(utf8, &code)) {
        ++failures;
        std::printf("ei_unit_check: platform text: cannot enter %s (error %u)\n", utf8.c_str(), code);
    } else {
        Expect("the current folder", platform::CurrentFolder(), utf8);
        platform::SetCurrentFolder(before, &code);
    }
    std::filesystem::remove_all(folder, error);
    return failures;
}
