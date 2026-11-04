// Native platform checks with synthetic files only. No installed game files are changed.
#include "platform/platform.h"
#include "video/bink_file.h"

#include "lib/bit_flags.h"
#include "lib/calendar.h"
#include "lib/caller.h"
#include "lib/float_select.h"
#include "lib/ipv4.h"
#include "lib/rounding.h"

#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

int RunSettingsFileChecks();
int RunIniFileChecks();
int RunIpv4Checks();
int RunMathsChecks();
int RunSavePictureChecks();
int RunAudioMixChecks();

namespace {
void Check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "port check failed: %s\n", message);
        std::exit(1);
    }
}
// The first word of EI_CALLER_FRAME() is the function's return address, EI_RETURN_ADDRESS() (the observers scan on).
[[gnu::noinline]] bool CallerCaptured() { return EI_CALLER_FRAME()[0] == EI_RETURN_ADDRESS(); }
}
int main() {
    // An unscoped enum uses the built-in operator, not the opt-in flag operators (Apple Clang's substitution order).
    enum Plain : std::uint32_t { kPlain = 1 };
    const std::uint32_t word = 2;
    Check((kPlain | word) == 3 && (kPlain & word) == 0, "plain enum bit operations");
    Check(RunSettingsFileChecks() == 0, "existing settings checks");
    Check(RunIniFileChecks() == 0, "ini file checks");
    Check(RunIpv4Checks() == 0, "IPv4 text as Winsock read it");
    Check(RunMathsChecks() == 0, "maths functions as on Windows");
    Check(RunSavePictureChecks() == 0, "save pictures and QOI");
    Check(RunAudioMixChecks() == 0, "the audio mixer on an offline output");
    Check(CallerCaptured(), "caller frame starts at the return address");
    Check(lib::FormatLocalTime(0, "%Y-%m-%d").size() == 10, "format saved timestamp");
    const std::string tooLong(128, 'x');
    Check(lib::FormatLocalTime(0, tooLong.c_str()).empty(), "reject oversized saved timestamp text");
    Check(lib::LocalDateTime(86400 * 365).year == 1971, "local date of a time");
    namespace fs = std::filesystem;
    const fs::path old = fs::current_path();
    std::string temp = (fs::temp_directory_path() / "ei-port-XXXXXX").string();
    Check(mkdtemp(temp.data()) != nullptr, "scratch directory");
    fs::current_path(temp);
    Check(platform::CreateFolder("Movies"), "create movie directory");
    {
        // A one-frame Bink container: an 8x8 picture and eight video bytes, no sound track.
        constexpr std::uint32_t words[] = {0x694B4942, 52, 1, 8, 1, 8, 8, 15, 1, 0, 0, 53, 60, 0, 0};
        std::array<std::uint8_t, sizeof(words)> bytes{};
        for (std::size_t i = 0; i < std::size(words); ++i)
            for (int byte = 0; byte < 4; ++byte)
                bytes[i * 4 + byte] = static_cast<std::uint8_t>(words[i] >> (byte * 8));
        platform::File file;
        Check(file.Open("Movies/progres.bik", platform::FileMode::Create), "create loading movie fixture");
        file.Write(bytes.data(), static_cast<unsigned>(bytes.size()));
        file.Close();
        bink::File movie;
        Check(movie.Open("movies\\PROGRES.BIK"), "open loading movie through the platform's path rules");
        Check(movie.Width() == 8 && movie.Height() == 8 && movie.FrameCount() == 1 && movie.Keyframe(0),
              "read loading movie header and keyframe index");
        bink::FramePackets packet;
        Check(movie.ReadFrame(0, packet) && packet.videoBytes == 8 && !packet.audio, "read loading movie video packet");
        Check(!movie.ReadFrame(1, packet), "reject loading movie frame past the end");
        Check(file.Open("Movies/truncated.bik", platform::FileMode::Create), "create truncated movie fixture");
        file.Write(bytes.data(), 44);
        file.Close();
        bink::File truncated;
        Check(!truncated.Open("movies\\TRUNCATED.BIK"), "reject truncated movie index");
        bink::File missing;
        Check(!missing.Open("movies\\missing.bik"), "missing loading movie fails without throwing");
    }
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
    const std::vector<platform::FolderEntry> found = platform::FolderEntries("res\\*.res");
    Check(found.size() == 1 && found[0].name == "Database.RES" && found[0].size == 7 && !found[0].folder,
          "case-insensitive wildcard");
    Check(platform::IsFile("res\\DATABASE.res") && platform::PathExists("RES") && !platform::IsFile("res"),
          "files and folders with folded case");
    std::uint32_t error = 0;
    Check(platform::WriteWholeFile("Config\\Test.ini", "[Game]\r\nCount=42\r\nName=\"value\"\r\n", &error),
          "atomic ini write");
    std::string written;
    Check(platform::ReadWholeFile("config\\test.INI", &written) && written.size() == 32, "ini read back with folded case");
    Check(!platform::RenameFile("empty", "Res/Database.RES"), "rename refuses existing target");
    Check(platform::CopyFileOver("Res/Database.RES", "res/COPY.res"), "copy file");
    Check(platform::FreeDiskSpace(".") > 0, "free disk space");
    Check(platform::IsLetter(0x0416) && !platform::IsLetter('1'), "Unicode keyboard character classes");

    const std::string instanceName = "port-check-" + std::to_string(getpid());
    Check(!platform::BringOtherInstanceForward(instanceName.c_str(), "check"), "first instance");
    const pid_t child = fork();
    Check(child >= 0, "fork instance check");
    if (child == 0) std::_Exit(platform::BringOtherInstanceForward(instanceName.c_str(), "check") ? 0 : 1);
    int status = 0;
    Check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "second instance sees lock owner");

    Check(lib::RoundEven(0.5f) == 0 && lib::RoundEven(1.5f) == 2 && lib::RoundEven(-2.5f) == -2, "round ties to even");
    Check(lib::RoundEven(std::numeric_limits<float>::quiet_NaN()) == INT32_MIN, "round NaN sentinel");
    Check(lib::RoundEven(2147483648.0f) == INT32_MIN, "round overflow sentinel");
    Check(std::bit_cast<std::uint32_t>(MinKeepA(0.0f, -0.0f)) == 0x80000000, "min signed zero");
    const float nan = std::bit_cast<float>(0x7FC01234u);
    Check(std::bit_cast<std::uint32_t>(MinKeepA(nan, 1.0f)) == 0x7FC01234, "min preserves NaN bits");

    using namespace platform::net;
    const Socket receiver = Open(Kind::Udp), sender = Open(Kind::Udp);
    Check(receiver != kNoSocket && sender != kNoSocket, "UDP sockets");
    Address address{kIpv4, 0, lib::ParseIpv4("127.0.0.1"), {}};
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
    int value = 0;
    std::thread thread([&value] {
        platform::MakeThreadTimeCritical();
        value = 42;
    });
    thread.join();
    Check(value == 42, "a thread at the highest priority runs");
    fs::current_path(old);
    // This tree is freshly made by this process and contains no links or game data.
    fs::remove_all(temp);
    std::puts("Native platform and ARM maths checks passed");
}
