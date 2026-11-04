// The other game on Linux and macOS (platform.h, BringOtherInstanceForward): SDL cannot list another process's windows,
// so the first game holds an advisory lock named by its name and window title and a second one finds it held (that
// window cannot be raised). An isolated run (seams.h) counts none.
#include "platform/platform.h"

#include "platform/seams.h"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/file.h>
#include <unistd.h>

namespace platform {

namespace {

int g_instanceLock = -1;
pid_t g_instanceOwner = 0;

// Whether another game of the same name and window title holds the lock; while none does, this one holds it.
bool OtherInstanceRunning(const char* name, const char* title) {
    if (g_instanceOwner == getpid() && g_instanceLock >= 0) return false;
    if (g_instanceLock >= 0) close(g_instanceLock);   // a forked child must acquire its own lock
    g_instanceLock = -1;
    std::uint64_t hash = 14695981039346656037ull;
    for (const char* part : {name, title}) {
        for (; *part; ++part) hash = (hash ^ static_cast<unsigned char>(*part)) * 1099511628211ull;
        hash = (hash ^ 0xFFu) * 1099511628211ull;
    }
    const auto path = std::filesystem::temp_directory_path() /
                      ("ei-rebuild-" + std::to_string(getuid()) + "-" + std::to_string(hash) + ".lock");
    const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        g_instanceLock = fd;
        g_instanceOwner = getpid();
        return false;
    }
    const int error = errno;
    close(fd);
    return error == EWOULDBLOCK || error == EAGAIN;
}

} // namespace

bool BringOtherInstanceForward(const char* name, const char* title) {
    return !IsolatedRun() && OtherInstanceRunning(name, title);
}

} // namespace platform
