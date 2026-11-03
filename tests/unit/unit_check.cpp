// ei_unit_check.exe: checks of our code without the game (build.cmd runs it). Exits 1 when any check fails.
#include <cstdio>

int RunSettingsFileChecks();   // settings_file.cpp: the settings file's format and import
int RunHashMapChecks();        // hash_map.cpp: lib::HashMap's order against STLport's hashtable
int RunIniFileChecks();        // ini_file.cpp: the game's .ini reader

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // what was printed survives a crashing check
    const int failures = RunSettingsFileChecks() + RunHashMapChecks() + RunIniFileChecks();
    std::printf("ei_unit_check: %d problems\n", failures);
    return failures ? 1 : 0;
}
