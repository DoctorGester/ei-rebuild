// The platform calls a harness run answers itself (its disc check, its window and desktop answers, its file write
// watch, its sockets on 127.0.0.1): the harness's builds leave this file out and take tests/verify's definitions of the
// same functions (CMakeLists.txt; docs/notes/platform.md, "The harness's answers"). Each is a platform function whose
// body is the system call, or the call itself (platform_win32.h) where platform_win32.cpp's function does more around
// it. The paths of the file calls go through one mapping of their own (src/platform/paths.h, system_path.cpp).
#include <winsock2.h>   // before anything includes windows.h (and with it winsock.h)

#include "platform/platform.h"

#include "platform/win32/platform_win32.h"

#include <windows.h>

#include <string>

#pragma comment(lib, "ws2_32.lib")

// ==== Files ==========================================================================================================

namespace platform {

std::string VolumeLabel(const char* root) {
    wchar_t label[MAX_PATH + 1] = {};
    if (!GetVolumeInformationW(text::Wide(root).c_str(), label, MAX_PATH + 1, nullptr, nullptr, nullptr, nullptr, 0))
        return {};
    return text::Narrow(label);
}

namespace win32 {

BOOL WriteFile(HANDLE file, const void* data, DWORD size, DWORD* written, std::string_view) {
    return ::WriteFile(file, data, size, written, nullptr);
}

} // namespace win32

} // namespace platform

// ==== The desktop and the other game window ==========================================================================

namespace platform {

bool FontSmoothing() {
    BOOL smoothing = FALSE;
    if (!SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &smoothing, 0)) return true;
    return smoothing != FALSE;
}

NativeWindow FindClassWindow(const char* className, const char* title) {
    return FindWindowW(text::Wide(className).c_str(), text::Wide(title).c_str());
}

} // namespace platform

// ==== Sockets ========================================================================================================

namespace platform::net {

namespace {

constexpr int kAddressLength = sizeof(sockaddr_in);

const sockaddr* Native(const Address& address) { return reinterpret_cast<const sockaddr*>(&address); }

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

} // namespace platform::net

namespace platform::win32 {

int Select(fd_set* read, fd_set* write, fd_set* failed, const timeval* timeout) {
    return select(0, read, write, failed, timeout);
}

} // namespace platform::win32
