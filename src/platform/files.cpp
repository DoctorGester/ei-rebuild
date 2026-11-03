// platform.h's folder calls on std::filesystem, for every system: each path through SystemPath, then the backend's
// NativePath (paths.h). The file objects, the folder search and the whole files are each backend's.
#include "platform/platform.h"

#include "platform/paths.h"

#include <filesystem>
#include <system_error>

namespace platform {

namespace {

namespace fs = std::filesystem;

fs::path Native(std::string_view path, PathUse use) { return NativePath(SystemPath(path, use)); }

} // namespace

bool CreateFolder(std::string_view path) {
    std::error_code error;
    return fs::create_directory(Native(path, PathUse::Write), error);
}

bool RemoveFile(std::string_view path) {
    const fs::path file = Native(path, PathUse::Write);
    std::error_code error;
    return !fs::is_directory(file, error) && fs::remove(file, error);
}

bool RemoveFolder(std::string_view path) {
    const fs::path folder = Native(path, PathUse::Write);
    std::error_code error;
    return fs::is_directory(folder, error) && fs::remove(folder, error);
}

// As MoveFile: an existing target is kept, and the move fails.
bool RenameFile(std::string_view from, std::string_view to) {
    const fs::path target = Native(to, PathUse::Write);
    std::error_code error;
    if (fs::exists(target, error)) return false;
    fs::rename(Native(from, PathUse::Update), target, error);
    return !error;
}

bool CopyFileOver(std::string_view from, std::string_view to) {
    std::error_code error;
    return fs::copy_file(Native(from, PathUse::Read), Native(to, PathUse::Write), fs::copy_options::overwrite_existing,
                         error);
}

bool PathExists(std::string_view path) {
    std::error_code error;
    return fs::exists(Native(path, PathUse::Read), error);
}

bool IsFile(std::string_view path) {
    std::error_code error;
    const fs::file_status status = fs::status(Native(path, PathUse::Read), error);
    return fs::exists(status) && !fs::is_directory(status);
}

double FreeDiskSpace(std::string_view path) {
    std::error_code error;
    const fs::space_info space = fs::space(Native(path, PathUse::Read), error);
    return error ? 0.0 : static_cast<double>(space.available);
}

} // namespace platform
