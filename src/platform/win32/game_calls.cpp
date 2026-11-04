// The platform calls a harness run answers itself (its window and desktop answers, its file write
// watch, its sockets on 127.0.0.1): the harness's builds leave this file out and take its platform backend's
// definitions of the same functions (tests/verify/platform; CMakeLists.txt; docs/notes/platform.md, "The harness's
// answers"). Each is the system's call. The paths of the file calls go through one mapping of their own
// (src/platform/paths.h, system_path.cpp).
#include <winsock2.h>   // before anything includes windows.h (and with it winsock.h)

#include "platform/platform.h"

#include "platform/win32/platform_win32.h"

#include <windows.h>

#include <string>

#pragma comment(lib, "ws2_32.lib")

// ==== Files ==========================================================================================================

namespace platform {

void File::Write(const void* buffer, unsigned count) {
    if (count == 0) return;
    DWORD written;
    if (!::WriteFile(handle ? handle : INVALID_HANDLE_VALUE, buffer, count, &written, nullptr))
        throw FileError(::GetLastError(), path);
    if (written != count) throw FileError(ERROR_DISK_FULL, path);
}

} // namespace platform

// ==== The desktop and the other game window ==========================================================================

namespace platform {

bool FontSmoothing() {
    BOOL smoothing = FALSE;
    if (!SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &smoothing, 0)) return true;
    return smoothing != FALSE;
}

bool BringOtherInstanceForward(const char* name, const char* title) {
    return win32::BringForward(FindWindowW(text::Wide(name).c_str(), text::Wide(title).c_str()));
}

} // namespace platform

// ==== Sockets ========================================================================================================

namespace platform::net {

namespace {

constexpr int kAddressLength = sizeof(sockaddr_in);

const sockaddr* Native(const Address& address) { return reinterpret_cast<const sockaddr*>(&address); }

// `set`'s sockets as an fd_set (or null for none).
fd_set* ToNative(const SocketGroup* set, fd_set* native) {
    if (!set) return nullptr;
    native->fd_count = static_cast<u_int>(set->count);
    for (int i = 0; i < set->count; ++i) native->fd_array[i] = set->sockets[i];
    return native;
}

// The sockets select left in `native` back into `set`.
void FromNative(const fd_set& native, SocketGroup* set) {
    if (!set) return;
    set->count = static_cast<int>(native.fd_count);
    for (int i = 0; i < set->count; ++i) set->sockets[i] = native.fd_array[i];
}

} // namespace

void Close(Socket socket) { closesocket(socket); }

bool Bind(Socket socket, const Address& address) { return bind(socket, Native(address), kAddressLength) == 0; }

bool Connect(Socket socket, const Address& address) {
    return connect(socket, Native(address), kAddressLength) == 0;
}

int SendTo(Socket socket, const void* data, int length, const Address& to) {
    return sendto(socket, static_cast<const char*>(data), length, 0, Native(to), kAddressLength);
}

int ReceiveFrom(Socket socket, void* data, int length, Address* from) {
    int fromLength = kAddressLength;
    return recvfrom(socket, static_cast<char*>(data), length, 0, reinterpret_cast<sockaddr*>(from), &fromLength);
}

int Send(Socket socket, const void* data, int length) { return send(socket, static_cast<const char*>(data), length, 0); }

int Receive(Socket socket, void* data, int length) { return recv(socket, static_cast<char*>(data), length, 0); }

// The count is ignored by Winsock.
int Select(SocketGroup* read, SocketGroup* write, SocketGroup* failed, std::int32_t microseconds) {
    fd_set sets[3];
    const timeval timeout = {microseconds / 1000000, microseconds % 1000000};
    const int ready = select(0, ToNative(read, &sets[0]), ToNative(write, &sets[1]), ToNative(failed, &sets[2]), &timeout);
    FromNative(sets[0], read);
    FromNative(sets[1], write);
    FromNative(sets[2], failed);
    return ready;
}

} // namespace platform::net
