#include "hook/log.h"
#include "platform/platform.h"

#include "ei/hook.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <filesystem>
#include <mutex>

namespace ei {
namespace {
void (*g_failure)(const char*);
std::mutex& LogMutex() {
    static auto* mutex = new std::mutex;
    return *mutex;
}
std::string OutputFolder() {
    const std::string folder = platform::EnvironmentVariable("EI_OUT_DIR");
    return folder.empty() ? std::filesystem::path(platform::ProgramPath()).parent_path().string() : folder;
}
FILE* LogFile() {
    static FILE* file = [] {
        std::error_code error;
        const auto folder = OutputFolder();
        std::filesystem::create_directories(folder, error);
        return std::fopen((std::filesystem::path(folder) / "ei-rebuild.log").c_str(), "a");
    }();
    return file;
}
void Stamp(FILE* file) {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&time, &local);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::fprintf(file, "[%02d:%02d:%02d.%03d] ", local.tm_hour, local.tm_min, local.tm_sec, static_cast<int>(ms));
}
}
bool Standalone() { return true; }
const char* OwnModuleName() { return "ei-rebuild"; }
void SetMissingCodeHandler(void (*handler)(const char*)) { g_failure = handler; }
void ReportImageFailure(const char* what) {
    Log("standalone: %s", what);
    if (g_failure) g_failure(what);
}
void DescribeCode(std::uintptr_t address, char* out, std::size_t size) {
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(address), &info))
        std::snprintf(out, size, "%s+%zx", info.dli_fname,
                      static_cast<std::size_t>(address - reinterpret_cast<std::uintptr_t>(info.dli_fbase)));
    else std::snprintf(out, size, "%zx", static_cast<std::size_t>(address));
}
[[noreturn]] void ReportNoCode(std::uintptr_t address, std::uintptr_t caller) {
    Log("standalone: missing code at %zx, called from %zx", static_cast<std::size_t>(address),
        static_cast<std::size_t>(caller));
    platform::EndProcess(3);
}
void OutputPath(const char* name, char* out) {
    // The shared harness API reserves 260 bytes for this Windows-shaped output parameter.
    const std::string path = (std::filesystem::path(OutputFolder()) / name).string();
    std::snprintf(out, 260, "%s", path.c_str());
}
void Log(const char* format, ...) {
    std::lock_guard lock(LogMutex());
    FILE* file = LogFile();
    if (!file) return;
    Stamp(file);
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fflush(file);
}
void LogLines(const char* lines) {
    while (*lines) {
        const char* end = std::strchr(lines, '\n');
        if (!end) end = lines + std::strlen(lines);
        Log("%.*s", static_cast<int>(end - lines), lines);
        lines = *end ? end + 1 : end;
    }
}
}
