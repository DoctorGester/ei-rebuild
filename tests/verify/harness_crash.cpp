// Failed writes and the "diskfull" test step (harness_crash.h; ticket X40).
#include "harness_crash.h"
#include "system/harness_system.h"

#include "../../src/platform/log.h"
#include "lib/text_case.h"

#include <atomic>
#include <cstdio>
#include <mutex>

namespace ei::verify::crash {

namespace {

std::string g_failText;   // the "diskfull" text, lower case
std::mutex g_failedLock;
std::string g_failedWrite;   // the first write that failed for real (not "diskfull"), as the run's failure line

std::string Lower(std::string_view text) {
    std::string lower(text);
    text::LowerInPlace(lower.data(), lower.size());
    return lower;
}

// The folder of `file` (either separator), for its disk's free space.
std::string FolderOf(const std::string& file) {
    const std::size_t slash = file.find_last_of("\\/");
    return slash == std::string::npos ? file : file.substr(0, slash + 1);
}

} // namespace

void FailWritesTo(const char* text) {
    g_failText = Lower(text);
    Log("harness: diskfull: writes to files whose path contains '%s' fail from now on", g_failText.c_str());
}

bool FailsByStep(std::string_view path) { return !g_failText.empty() && Lower(path).find(g_failText) != std::string::npos; }

void WriteFailed(std::string_view path, const std::string& target, unsigned size, unsigned long error, bool injected) {
    const std::string name(path);
    const long long freeMb = sys::FreeMegabytes(FolderOf(target.empty() ? name : target));
    const bool redirected = !target.empty() && text::CompareIgnoringCase(name, target) != 0;
    char line[3 * sys::kMaxPath];
    std::snprintf(line, sizeof(line), "a file write failed: %u bytes to %s%s%s: error %lu (%s); %lld MB free on its disk",
                  size, name.c_str(), redirected ? " -> " : "", redirected ? target.c_str() : "", error,
                  sys::ErrorText(error).c_str(), freeMb);
    static std::atomic<int> logged{0};
    if (++logged <= 10) ei::Log("harness: %s%s", injected ? "diskfull: " : "", line);
    if (injected) return;
    std::lock_guard<std::mutex> lock(g_failedLock);
    if (g_failedWrite.empty()) g_failedWrite = line;
}

std::string FailedWrite() {
    std::lock_guard<std::mutex> lock(g_failedLock);
    return g_failedWrite;
}

} // namespace ei::verify::crash
