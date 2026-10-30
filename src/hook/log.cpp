// ei-rebuild.log and the output folder (log.h).
#include "log.h"

#include "platform/platform.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>

namespace ei {

namespace {

// One Log line at a time. Constant-initialised: static constructors (the harness, the image's import table) log first.
std::mutex g_logMutex;

// UTF-8 text as the system's path.
std::filesystem::path SystemPath(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

// The folder `OutputPath` puts its files in (log.h).
std::string OutputFolder() {
    const std::string folder = platform::EnvironmentVariable("EI_OUT_DIR");
    if (!folder.empty()) return folder;
    const std::string program = platform::ProgramPath();
    return program.substr(0, program.find_last_of("\\/"));
}

// The file stays open for the whole run and each line is flushed as it is written (a crash loses
// nothing). Opening and closing it per line made the antivirus scan it on every close: about 10,000
// scans per harness run, most of its scanning during the suite.
std::ofstream& LogFile() {
    static std::ofstream file(SystemPath(OutputPath("ei-rebuild.log")), std::ios::app);
    return file;
}

// "[hh:mm:ss.mmm] ": the local time of day.
std::string Stamp() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    const std::string time = platform::FormatLocalTime(ms / 1000, "%H:%M:%S");
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "[%s.%03d] ", time.c_str(), static_cast<int>(ms % 1000));
    return stamp;
}

} // namespace

std::string OutputPath(const char* name) {
    const std::filesystem::path folder = SystemPath(OutputFolder());
    std::error_code error;
    std::filesystem::create_directory(folder, error);
    const std::u8string path = (folder / name).u8string();
    return std::string(path.begin(), path.end());
}

void Log(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    va_list measured;
    va_copy(measured, arguments);
    const int size = std::vsnprintf(nullptr, 0, format, measured);
    va_end(measured);
    std::string line = Stamp();
    const std::size_t text = line.size();
    line.resize(text + (size > 0 ? size : 0) + 1);
    if (size > 0) std::vsnprintf(line.data() + text, size + 1, format, arguments);
    va_end(arguments);
    line.back() = '\n';
    const std::lock_guard lock(g_logMutex);
    std::ofstream& file = LogFile();
    file.write(line.data(), static_cast<std::streamsize>(line.size()));
    file.flush();
}

} // namespace ei
