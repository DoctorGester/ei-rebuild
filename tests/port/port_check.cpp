// Native platform checks with synthetic files only. No installed game files are changed.
#include "platform/callers.h"
#include "platform/platform.h"

#include "lib/float_select.h"
#include "lib/path.h"
#include "lib/rounding.h"

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

int RunSettingsFileChecks();

namespace {
void Check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "port check failed: %s\n", message);
        std::exit(1);
    }
}
}
int main() {
    Check(RunSettingsFileChecks() == 0, "existing settings checks");
    bool observed = false;
    platform::ObserveCallers(
        [&](const void* expected, void* const* callers) {
            Check(expected && callers[0] == expected, "observer captures its caller without a platform frame");
            for (int i = 1; i < 64; ++i) Check(callers[i] == nullptr, "unused caller words are zero");
            observed = true;
        },
        platform::ReturnAddress());
    Check(observed, "caller observer invoked");
    Check(platform::FormatLocalTime(0, "%Y-%m-%d").size() == 10, "format saved timestamp");
    const std::string tooLong(128, 'x');
    Check(platform::FormatLocalTime(0, tooLong.c_str()).empty(), "reject oversized saved timestamp text");
    namespace fs = std::filesystem;
    const fs::path old = fs::current_path();
    std::string temp = (fs::temp_directory_path() / "ei-port-XXXXXX").string();
    Check(mkdtemp(temp.data()) != nullptr, "scratch directory");
    fs::current_path(temp);
    Check(platform::CreateFolder("Res"), "create mixed-case directory");
    platform::File file;
    Check(file.Open("Res/Database.RES", platform::FileMode::Create), "create asset");
    file.Write("archive", 7);
    file.Close();
    Check(file.Open("res\\database.res", platform::FileMode::Read), "fold case and separators");
    char bytes[8] = {};
    Check(file.Read(bytes, 7) == 7 && std::string(bytes) == "archive", "read folded asset");
    Check(file.ReadAt(2, bytes, 3) && file.Position() == 5, "ReadAt moves stream position");
    file.Close();
    platform::MappedFile mapped;
    Check(mapped.Open("RES\\DATABASE.res") && mapped.Size() == 7, "map folded asset");
    Check(std::string(static_cast<const char*>(mapped.Data()), mapped.Size()) == "archive", "mapped bytes");
    mapped.Close();
    Check(!mapped.Open("res\\missing.res"), "missing map fails");
    Check(file.Open("empty", platform::FileMode::Create), "empty file");
    file.Close();
    Check(!mapped.Open("empty"), "empty map fails");
    platform::FileFind find;
    Check(find.FindFile("res\\*.res"), "case-insensitive wildcard");
    Check(!find.FindNext() && find.GetFileName() == "Database.RES" && find.GetLength() == 7,
          "last search result remains readable");
    std::uint32_t error = 0;
    Check(platform::WriteWholeFile("Config\\Test.ini", "[Game]\r\nCount=42\r\nName=\"value\"\r\n", &error),
          "atomic ini write");
    Check(platform::IniInt("game", "count", 0, "config\\test.INI") == 42, "ini case folding");
    Check(platform::IniString("GAME", "name", "", "config/test.ini") == "value", "ini quoting");
    Check(platform::IniString("game", "absent", "default", "config/test.ini") == "default", "ini default");
    Check(!platform::RenameFile("empty", "Res/Database.RES"), "rename refuses existing target");
    Check(platform::CopyFileOver("Res/Database.RES", "res/COPY.res"), "copy file");
    Check(platform::FreeDiskSpace(".") > 0, "free disk space");
    Check(platform::IsLetter(0x0416) && !platform::IsLetter('1'), "Unicode keyboard character classes");

    const std::string instanceName = "port-check-" + std::to_string(getpid());
    Check(platform::FindClassWindow(instanceName.c_str(), "check") == nullptr, "first instance");
    const pid_t child = fork();
    Check(child >= 0, "fork instance check");
    if (child == 0) {
        const auto other = platform::FindClassWindow(instanceName.c_str(), "check");
        std::_Exit(other && platform::WindowProcess(other) == static_cast<std::uint32_t>(getppid()) ? 0 : 1);
    }
    int status = 0;
    Check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "second instance sees lock owner");

    char drive[4], dir[64], name[64], ext[16];
    lib::SplitPath("C:\\Res/sub\\Database.res", drive, dir, name, ext);
    Check(std::string(drive) == "C:" && std::string(dir) == "\\Res/sub\\" && std::string(name) == "Database" &&
              std::string(ext) == ".res",
          "asset path split");
    Check(lib::RoundEven(0.5f) == 0 && lib::RoundEven(1.5f) == 2 && lib::RoundEven(-2.5f) == -2, "round ties to even");
    Check(lib::RoundEven(std::numeric_limits<float>::quiet_NaN()) == INT32_MIN, "round NaN sentinel");
    Check(lib::RoundEven(2147483648.0f) == INT32_MIN, "round overflow sentinel");
    Check(std::bit_cast<std::uint32_t>(MinKeepA(0.0f, -0.0f)) == 0x80000000, "min signed zero");
    const float nan = std::bit_cast<float>(0x7FC01234u);
    Check(std::bit_cast<std::uint32_t>(MinKeepA(nan, 1.0f)) == 0x7FC01234, "min preserves NaN bits");

    using namespace platform::net;
    Check(Start(), "socket start");
    const Socket receiver = Open(Kind::Udp), sender = Open(Kind::Udp);
    Check(receiver != kNoSocket && sender != kNoSocket, "UDP sockets");
    Address address{kIpv4, 0, ParseIpv4("127.0.0.1"), {}};
    Check(Bind(receiver, address), "loopback bind");
    sockaddr_in bound{};
    socklen_t boundSize = sizeof(bound);
    Check(getsockname(static_cast<int>(receiver), reinterpret_cast<sockaddr*>(&bound), &boundSize) == 0, "bound port");
    address.port = bound.sin_port;
    SetNonBlocking(receiver, true);
    Address from{};
    Check(ReceiveFrom(receiver, bytes, 8, &from) == -1 && LastError() == Error::WouldBlock, "nonblocking empty socket");
    Check(SendTo(sender, "packet", 6, address) == 6, "send UDP");
    SocketGroup ready(receiver);
    Check(Select(&ready, nullptr, nullptr, 1000000) == 1 && ready.count == 1, "select UDP");
    Check(ReceiveFrom(receiver, bytes, 8, &from) == 6 && from.family == kIpv4 && from.ip == address.ip,
          "receive address uses game wire layout");
    ready = SocketGroup(receiver);
    Check(Select(&ready, nullptr, nullptr, 1000) == 0 && ready.count == 0, "select clears nonready sockets");
    Close(receiver);
    Close(sender);
    Stop();
    struct Worker {
        platform::Signal finished;
        int value = 0;
    } worker;
    worker.finished.Create();
    platform::Thread thread;
    Check(thread.Start(
              [](void* parameter) -> unsigned {
                  auto& worker = *static_cast<Worker*>(parameter);
                  worker.value = 42;
                  worker.finished.Set();
                  return 0;
              },
              &worker, 0),
          "start ordinary C++ thread callback");
    worker.finished.Wait();
    Check(worker.value == 42, "thread callback receives its parameter and signals completion");
    thread.Close();
    fs::current_path(old);
    // This tree is freshly made by this process and contains no links or game data.
    fs::remove_all(temp);
    std::puts("Native platform and ARM maths checks passed");
}
