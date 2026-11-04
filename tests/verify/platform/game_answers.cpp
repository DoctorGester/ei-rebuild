// The game's calls a harness run answers itself (harness_platform.h): the harness's builds leave the game's own
// definitions out (src/platform/<system>/game_calls.cpp, src/platform/system_path.cpp; CMakeLists.txt) and take these.
// Without the harness (Answering false: no scenario) each is the system's answer, as theirs.
#include "harness_platform.h"

#include "harness_crash.h"
#include "harness_net.h"
#include "harness_pair.h"
#include "system/harness_sockets.h"
#include "system/harness_system.h"

#include <cstring>
#include <string>

namespace hp = ei::verify::harnessplatform;
namespace sys = ei::verify::sys;
namespace sockets = ei::verify::sockets;

// ==== Files ==========================================================================================================

namespace platform {

// The one seam of the overlay (src/platform/paths.h): every file and folder call of the game asks for its path here.
// A read takes the overlay's copy when there is one; a write goes to the overlay (its folders made), Update copying the
// install's file there first, so an install file renamed is copied, never moved away (harness.cpp's MapPath).
std::string SystemPath(std::string_view path, PathUse use) {
    if (!hp::Answering()) return std::string(path);
    return hp::MapPath(path, use);
}

// The game's file writes, watched (harness_crash.h): each failure is logged and the first real one fails the run;
// "diskfull <text>" makes the writes to its files fail as on a full disk.
void File::Write(const void* buffer, unsigned count) {
    if (count == 0) return;
    const bool injected = hp::Answering() && ei::verify::crash::FailsByStep(path);
    unsigned written = 0;
    unsigned long error = sys::kDiskFull;
    if (!injected && sys::WriteHandle(handle, buffer, count, &written, &error)) {
        if (written != count) throw FileError(sys::kDiskFull, path);
        return;
    }
    if (hp::Answering()) ei::verify::crash::WriteFailed(path, sys::FileOfHandle(handle), count, error, injected);
    throw FileError(error, path);
}

} // namespace platform

// ==== The desktop and the other game window ==========================================================================

namespace platform {

// Font smoothing is a setting of the whole desktop: the credits screen (0062B7D0 / 0062BD00) switched it off and
// "restored" a value it never read (off), so a harness game left it off for every program on the machine, and text in
// runs before and after differed (GDI's grey edges or none: the widescreen comparison of two runs). The game's text is
// drawn without smoothing in harness runs, as it looks once the titles have switched it off, whatever the desktop's
// setting and other programs do to it during the run: the interface text asks the setting (ui_font.cpp) and reads it
// as off.
bool FontSmoothing() {
    if (hp::Answering()) return false;
    return sys::FontSmoothing();
}

// WinMain looks for another game window and, when it finds one, brings it to the front and exits. Harness runs go in
// parallel (run_all.py --jobs), each game in its own output folder, so in harness runs no other window counts. The
// other way (ST1b): a game the user starts without the harness runs the real check, so a harness game's window has a
// title of its own (harness_platform.cpp's CreateGameWindow) and a play launch never finds it.
bool BringOtherInstanceForward(const char* name, const char* title) {
    return !hp::Answering() && sys::BringOtherGameForward(name, title);
}

} // namespace platform

// ==== Sockets ========================================================================================================

// The game's sockets on 127.0.0.1 only, and a pair's game on the pair's network (harness_net.h, harness_pair.h).
namespace platform::net {

namespace {

namespace netlocal = ei::verify::netlocal;
namespace pair = ei::verify::pair;

sockaddr_in Native(const Address& address) {
    sockaddr_in native{};
    native.sin_family = AF_INET;
    native.sin_port = address.port;
    native.sin_addr.s_addr = address.ip;
    return native;
}

Address Public(const sockaddr_in& native) { return {kIpv4, native.sin_port, native.sin_addr.s_addr, {}}; }

const sockaddr* Generic(const sockaddr_in& address) { return reinterpret_cast<const sockaddr*>(&address); }

int BindTo(Socket socket, const sockaddr_in& address) {
    return bind(sockets::Of(socket), Generic(address), sizeof(address));
}

int BindLoopback(Socket socket) {
    sockaddr_in loopback{};
    loopback.sin_family = AF_INET;
    loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return BindTo(socket, loopback);
}

bool IsLoopback(std::uint32_t address) { return (ntohl(address) >> 24) == 127; }

int SendNative(Socket socket, const char* data, int length, const sockaddr_in& to) {
    return static_cast<int>(sendto(sockets::Of(socket), data, length, 0, Generic(to), sizeof(to)));
}

} // namespace

void Close(Socket socket) {
    if (netlocal::Paired()) pair::Close(socket);
    sockets::Close(socket);
}

// Every address (INADDR_ANY) becomes 127.0.0.1, the port as asked. A pair's game: the port asked is a virtual port
// on the pair's network (harness_pair.h); the socket itself is bound on 127.0.0.1, any port.
bool Bind(Socket socket, const Address& address) {
    sockaddr_in local = Native(address);
    if (!netlocal::Loopback()) return BindTo(socket, local) == 0;
    if (netlocal::Paired() && netlocal::IsUdp(socket)) {
        if (!pair::Bind(socket, ntohs(local.sin_port))) {
            sockets::SetError(sockets::Error::AddressInUse);
            return false;
        }
        const int result = BindLoopback(socket);
        const int error = sockets::LastCode();
        netlocal::LogBind(socket, "the game's, on the pair's network", result);
        if (result != 0) pair::Close(socket);
        sockets::SetCode(error);
        return result == 0;
    }
    const char* why = "the game's";
    if (local.sin_addr.s_addr == htonl(INADDR_ANY)) {
        local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        why = "the game's, asked on every address";
    }
    const int result = BindTo(socket, local);
    const int error = sockets::LastCode();
    netlocal::LogBind(socket, why, result);
    sockets::SetCode(error);
    return result == 0;
}

// A socket not bound yet is bound to 127.0.0.1 first (the system would bind it on every address); a destination
// outside loopback (the lobby's broadcast) becomes 127.0.0.1 on the same port; a pair's game sends on the pair's
// network.
int SendTo(Socket socket, const void* data, int length, const Address& to) {
    sockaddr_in destination = Native(to);
    const char* bytes = static_cast<const char*>(data);
    if (!netlocal::Loopback()) return SendNative(socket, bytes, length, destination);
    if (sockets::Unbound(socket))
        netlocal::LogBind(socket, "before its first datagram, which would bind it on every address", BindLoopback(socket));
    if (netlocal::Paired() && netlocal::IsUdp(socket)) {
        const bool broadcast = (ntohl(destination.sin_addr.s_addr) & 0xFF) == 0xFF;
        return pair::SendTo(socket, bytes, length, ntohs(destination.sin_port), broadcast);   // the pair's network
    }
    if (!IsLoopback(destination.sin_addr.s_addr)) {
        netlocal::Redirected(destination.sin_addr.s_addr, destination.sin_port);
        destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }
    return SendNative(socket, bytes, length, destination);
}

// A pair's game: its UDP sockets read the pair's mailbox.
int ReceiveFrom(Socket socket, void* data, int length, Address* from) {
    sockaddr_in sender{};
    int received;
    if (netlocal::Paired() && netlocal::IsUdp(socket)) {
        received = pair::RecvFrom(socket, static_cast<char*>(data), length, &sender);
    } else {
        sockets::Length senderLength = sizeof(sender);
        received = static_cast<int>(recvfrom(sockets::Of(socket), static_cast<char*>(data), length, 0,
                                             reinterpret_cast<sockaddr*>(&sender), &senderLength));
    }
    if (received >= 0 && from) *from = Public(sender);
    return received;
}

// A pair's game: a TCP link to the fake master is the harness's (SV5d4).
bool Connect(Socket socket, const Address& address) {
    const sockaddr_in native = Native(address);
    if (netlocal::Paired() && !netlocal::IsUdp(socket) && pair::MasterConnect(socket, ntohs(native.sin_port))) {
        sockets::SetError(sockets::Error::WouldBlock);   // as a nonblocking connect under way
        return false;
    }
    return connect(sockets::Of(socket), Generic(native), sizeof(native)) == 0;
}

int Send(Socket socket, const void* data, int length) {
    if (netlocal::Paired() && pair::MasterSend(socket, static_cast<const char*>(data), length)) return length;
#if defined(_WIN32)
    constexpr int kFlags = 0;
#else
    constexpr int kFlags = MSG_NOSIGNAL;
#endif
    return static_cast<int>(send(sockets::Of(socket), static_cast<const char*>(data), length, kFlags));
}

int Receive(Socket socket, void* data, int length) {
    int received = 0;
    if (netlocal::Paired() && pair::MasterRecv(socket, static_cast<char*>(data), length, &received)) return received;
    return static_cast<int>(recv(sockets::Of(socket), static_cast<char*>(data), length, 0));
}

int Select(SocketGroup* read, SocketGroup* write, SocketGroup* failed, std::int32_t microseconds) {
    int ready = 0;
    if (netlocal::Paired() && pair::MasterSelect(read, write, failed, &ready)) return ready;
    return sockets::Select(read, write, failed, microseconds);
}

} // namespace platform::net
