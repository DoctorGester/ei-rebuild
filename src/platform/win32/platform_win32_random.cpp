// Legacy file-based entropy for Windows game seeds.
#include "platform/platform.h"

#include "lib/crt.h"

#include <cstring>
#include <string>

namespace platform {
namespace {
// The search strings: the drive's files and the sub-directory suffix.
constexpr const char* kSeedSearchPattern = "C:\\*.*";
const char* SubdirectoryPattern() { return "\\*.*"; }

constexpr unsigned long kMinimumFileSize = 0x800;
constexpr int kSeedBytes = 0x400;

constexpr int kPathBuffer = 0x11C;

// rand() % 512 as the game computes it (rand() is never negative).
int RandomBelow512() { return libcrt::Rand() & 0x1FF; }

int FindSeedFile(char* out, const char* pattern, int target, int* counter) {
    platform::FileFind finder;
    bool more = finder.FindFile(pattern);
    while (more) {
        more = finder.FindNext();
        if (finder.IsDots()) continue;
        if (finder.IsSystem() || finder.IsHidden()) continue;
        if (finder.IsDirectory()) {
            int found;
            {
                std::string path = finder.GetFilePath();
                std::string sub = path + SubdirectoryPattern();
                found = FindSeedFile(out, sub.c_str(), target, counter);
            }
            if (found == 1) return 1;
            continue;
        }
        if (*counter >= target) {
            if (finder.GetLength() >= kMinimumFileSize) {
                std::string path = finder.GetFilePath();
                const char* text = path.c_str();
                std::memcpy(out, text, std::strlen(text) + 1);   // strcpy, unbounded as in the game
                return 1;
            }
            --*counter;
        }
        ++*counter;
    }
    return 0;
}

}
// Retry failed or all-zero reads. File choice and mixing retain the game's rand() draws.
void RandomWords(std::span<std::uint32_t, 256> words) {
    char path[kPathBuffer];
    libcrt::Srand(platform::Milliseconds());
    int ok = 0;
    while (!ok) {
        int counter = 0;
        if (!FindSeedFile(path, kSeedSearchPattern, RandomBelow512() + 1, &counter)) {
            int r = libcrt::Rand();
            int files = counter;
            counter = 0;
            if (!FindSeedFile(path, kSeedSearchPattern, r % (files - 1) + 1, &counter)) continue;
        }
        ok = 1;
        platform::File file;
        if (file.Open(path, platform::FileMode::Read)) {
            libcrt::Srand(platform::Milliseconds());
            long offset = kSeedBytes - RandomBelow512();
            if (!file.ReadAt(offset, words.data(), kSeedBytes)) ok = 0;
            file.Close();
        } else {
            ok = 0;
        }
        bool zero = true;
        for (std::uint32_t word : words)
            if (word != 0) {
                zero = false;
                break;
            }
        if (zero) {
            ok = 0;
            platform::Sleep(10);
        }
        for (std::uint32_t& word : words) word ^= static_cast<std::uint32_t>(libcrt::Rand());
    }
}
}
