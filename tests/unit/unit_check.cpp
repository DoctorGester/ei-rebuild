// ei_unit_check.exe: checks of our code without the game (build.cmd runs it). Exits 1 when any check fails.
#include <cstdio>

int RunSettingsFileChecks();   // settings_file.cpp: the settings file's format and import
int RunHashMapChecks();        // hash_map.cpp: lib::HashMap's order against STLport's hashtable
int RunIniFileChecks();        // ini_file.cpp: the game's .ini reader
int RunIpv4Checks();           // ipv4.cpp: lib::ParseIpv4 against Winsock's inet_addr
int RunMathsChecks();          // maths.cpp: lib/maths.h where the systems' float functions round otherwise
int RunSavePictureChecks();    // save_picture.cpp: the save pictures and their QOI codec
int RunAudioMixChecks();       // audio_mix.cpp: src/audio's mixer on an offline output
int RunDisplaySizesChecks();   // display_sizes.cpp: the resolution row's sizes from a display's modes
int RunBoundedTextChecks();    // bounded_text.cpp: str::BoundedLength and str::CopyInto
int RunTaggedStreamChecks();   // tagged_stream.cpp: ByteStream::ReadTagged's block sizes
int RunGameBufferChecks();     // game_buffer.cpp: the byte buffers, their cursor and the LZ coder
int RunPlatformTextChecks();   // platform_text.cpp: Cyrillic names through the environment and the current folder

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // what was printed survives a crashing check
    const int failures = RunSettingsFileChecks() + RunHashMapChecks() + RunIniFileChecks() + RunIpv4Checks() +
                         RunMathsChecks() + RunSavePictureChecks() + RunAudioMixChecks() +
                         RunDisplaySizesChecks() + RunBoundedTextChecks() + RunTaggedStreamChecks() +
                         RunGameBufferChecks() + RunPlatformTextChecks();
    std::printf("ei_unit_check: %d problems\n", failures);
    return failures ? 1 : 0;
}
