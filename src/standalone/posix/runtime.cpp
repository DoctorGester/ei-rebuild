#include "hook/log.h"
#include "platform/platform.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <mutex>

namespace ei {
namespace {
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
}
