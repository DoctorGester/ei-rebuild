// The platform calls a harness run answers itself (its saves and install overlay, its sockets on 127.0.0.1, its disc
// check, its window and desktop answers): the harness's builds leave this file out and take tests/verify's
// definitions of the same functions (CMakeLists.txt; docs/notes/platform.md, "The harness's answers"). Each is a
// platform function whose body is the system call, or the call itself (platform_win32.h) where platform_win32.cpp's
// function does more around it.
#include <winsock2.h>   // before anything includes windows.h (and with it winsock.h)

#include "platform/platform.h"

#include "platform/win32/platform_win32.h"

#include <windows.h>

#include <string>

#pragma comment(lib, "ws2_32.lib")

// ==== Folders and files ==============================================================================================

namespace platform {

bool CreateFolder(const char* path) { return CreateDirectoryW(text::Wide(path).c_str(), nullptr) != FALSE; }

bool RemoveFile(const char* path) { return DeleteFileW(text::Wide(path).c_str()) != FALSE; }

bool RenameFile(const char* from, const char* to) {
    return MoveFileW(text::Wide(from).c_str(), text::Wide(to).c_str()) != FALSE;
}

bool CopyFileOver(const char* from, const char* to) {
    return CopyFileW(text::Wide(from).c_str(), text::Wide(to).c_str(), FALSE) != FALSE;
}

bool RemoveFolder(const char* path) { return RemoveDirectoryW(text::Wide(path).c_str()) != FALSE; }

bool FileExists(const std::string& path) {
    return GetFileAttributesW(text::Wide(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string VolumeLabel(const char* root) {
    wchar_t label[MAX_PATH + 1] = {};
    if (!GetVolumeInformationW(text::Wide(root).c_str(), label, MAX_PATH + 1, nullptr, nullptr, nullptr, nullptr, 0))
        return {};
    return text::Narrow(label);
}

namespace win32 {

HANDLE OpenFile(const std::string& path, DWORD access, DWORD disposition) {
    const HANDLE handle = CreateFileW(text::Wide(path).c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    return handle == INVALID_HANDLE_VALUE ? nullptr : handle;
}

BOOL WriteFile(HANDLE file, const void* data, DWORD size, DWORD* written) {
    return ::WriteFile(file, data, size, written, nullptr);
}

HANDLE FindFirst(const std::string& pattern, WIN32_FIND_DATAW* found) {
    return FindFirstFileW(text::Wide(pattern).c_str(), found);
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
