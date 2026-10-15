// ei_vectors.exe: replays recorded function vectors (vectors.h) against our code, without the game.
//
// usage: ei_vectors [folder] [--verbose]
//   folder: where the .vec files are, searched recursively (default tests/vectors/data).
// Each file's records go to the check registered under its function name, each under the x87
// control word recorded with the call; fixtures (function names starting with '@') run first. Exits
// 1 when a record differs, a file has no check or a different payload size, or no file is found.
#include "vectors.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace ei::vectors {

namespace {
Check* g_checks = nullptr;
}

CheckRegistrar::CheckRegistrar(const char* function, std::uint32_t payloadSize, CheckFn run)
    : entry{function, payloadSize, run, g_checks} {
    g_checks = &entry;
}

Check* Checks() { return g_checks; }

} // namespace ei::vectors

namespace {

using namespace ei::vectors;

void SetControlWord(std::uint16_t cw) {
    __asm fldcw cw
}

const Check* FindCheck(const char* function) {
    for (const Check* c = Checks(); c; c = c->next)
        if (std::strcmp(c->function, function) == 0) return c;
    return nullptr;
}

constexpr int kShownFailures = 5;

// The function name of a vector file (empty when it is not one).
std::string FunctionOf(const std::filesystem::path& path) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return std::string();
    FileHeader header{};
    bool ok = std::fread(&header, sizeof(header), 1, f) == 1 && header.magic == kMagic && header.version == kVersion;
    std::fclose(f);
    header.function[sizeof(header.function) - 1] = 0;
    return ok ? std::string(header.function) : std::string();
}

// Replays one file; returns the number of problems (failed records, or 1 for a bad file).
int ReplayFile(const std::filesystem::path& path, const std::filesystem::path& root, bool verbose,
               std::vector<std::string>& seen) {
    std::string shown = std::filesystem::relative(path, root).generic_string();
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
        std::printf("%s: cannot open\n", shown.c_str());
        return 1;
    }
    FileHeader header{};
    if (std::fread(&header, sizeof(header), 1, f) != 1 || header.magic != kMagic || header.version != kVersion) {
        std::printf("%s: not a version %u vector file\n", shown.c_str(), kVersion);
        std::fclose(f);
        return 1;
    }
    header.function[sizeof(header.function) - 1] = 0;
    const Check* check = FindCheck(header.function);
    if (!check || check->payloadSize != header.payloadSize) {
        if (check)
            std::printf("%s: %s payloads are %u bytes, the check takes %u (0: variable)\n", shown.c_str(),
                        header.function, header.payloadSize, check->payloadSize);
        else
            std::printf("%s: no check registered for %s\n", shown.c_str(), header.function);
        std::fclose(f);
        return 1;
    }
    seen.push_back(header.function);
    const bool variable = header.payloadSize == kVariableSize;
    const bool fixture = IsFixture(header.function);
    const std::uint16_t initial = ControlWord();
    std::vector<unsigned char> payload;
    std::uint32_t failures = 0, read = 0;
    for (; read < header.count; ++read) {
        RecordHeader rh;
        std::uint32_t size = header.payloadSize;
        bool complete = std::fread(&rh, sizeof(rh), 1, f) == 1 && (!variable || std::fread(&size, sizeof(size), 1, f) == 1);
        if (complete) {
            payload.resize(size);
            complete = size == 0 || std::fread(payload.data(), size, 1, f) == 1;
        }
        if (!complete) {
            std::printf("%s: file ends after %u of %u records\n", shown.c_str(), read, header.count);
            ++failures;
            break;
        }
        char detail[512] = "";
        // Exceptions stay masked, as in the game (an unmasked one there would have crashed it).
        if (!fixture) ResetGameData();
        SetControlWord(static_cast<std::uint16_t>(rh.controlWord | 0x3F));
        bool ok = check->run(payload.data(), size, detail, sizeof(detail));
        SetControlWord(initial);
        if (!ok && ++failures <= kShownFailures)
            std::printf("  %s record %u (control word %04X): %s\n", header.function, read, rh.controlWord, detail);
    }
    std::fclose(f);
    if (fixture) KeepGameData();
    if (failures || verbose)
        std::printf("%s: %s, %u records, %u failed\n", shown.c_str(), header.function, read, failures);
    return static_cast<int>(failures);
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // what was printed survives a crashing check
    std::filesystem::path root = EI_VECTORS_DATA;
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verbose") == 0 || std::strcmp(argv[i], "-v") == 0)
            verbose = true;
        else
            root = argv[i];
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        std::printf("ei_vectors: %s is not a folder\n", root.string().c_str());
        return 1;
    }
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec))
        if (entry.is_regular_file() && entry.path().extension() == ".vec") files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    // Fixtures first (their game data stays for the other checks).
    std::stable_partition(files.begin(), files.end(), [](const std::filesystem::path& p) {
        std::string function = FunctionOf(p);
        return !function.empty() && IsFixture(function.c_str());
    });
    int problems = 0;
    std::vector<std::string> seen;
    for (const auto& path : files) {
        problems += ReplayFile(path, root, verbose, seen);
        // The other checks read what the fixtures set up: after a failed one they would only mislead (or
        // crash).
        if (problems && IsFixture(FunctionOf(path).c_str())) {
            std::printf("ei_vectors: fixture %s failed; the other checks were not run\n", FunctionOf(path).c_str());
            return 1;
        }
    }
    int checks = 0, unused = 0;
    for (const Check* c = Checks(); c; c = c->next) {
        ++checks;
        if (std::find(seen.begin(), seen.end(), c->function) == seen.end()) {
            ++unused;
            if (verbose) std::printf("no vectors for %s\n", c->function);
        }
    }
    std::printf("ei_vectors: %zu files, %d checks (%d without vectors), %d problems\n", files.size(), checks, unused,
                problems);
    return problems || files.empty() ? 1 : 0;
}
