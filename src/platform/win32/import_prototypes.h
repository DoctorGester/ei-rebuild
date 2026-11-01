#pragma once

// The prototypes of the imports in platform/win32/import_table.h that the Windows SDK headers we compile against declare
// differently: Winsock 1.1 by ordinal (WSOCK32.dll). tools/gen_iat.py types ImportTable's member of each `using`
// here's name with it; gameapi.h calls them through the slot. A Winsock SOCKET is a std::uintptr_t (pointer-sized,
// as the SDK's), its addresses and sets untyped.

#include <cstdint>

namespace ei::import_fn {

using select = int(__stdcall*)(int count, void* read, void* write, void* except, const void* timeout);
using connect = int(__stdcall*)(std::uintptr_t s, const void* address, int length);
using recvfrom = int(__stdcall*)(std::uintptr_t s, void* data, int length, int flags, void* from, int* fromLength);
using send = int(__stdcall*)(std::uintptr_t s, const void* data, int length, int flags);
using socket = std::uintptr_t(__stdcall*)(int family, int type, int protocol);
using recv = int(__stdcall*)(std::uintptr_t s, void* data, int length, int flags);
using bind = int(__stdcall*)(std::uintptr_t s, const void* address, int length);
using setsockopt = int(__stdcall*)(std::uintptr_t s, int level, int option, const void* value, int length);
using closesocket = int(__stdcall*)(std::uintptr_t s);
using WSACleanup = int(__stdcall*)();
using WSAStartup = int(__stdcall*)(std::uint16_t version, void* data);
using gethostname = int(__stdcall*)(char* name, int length);
using gethostbyaddr = void*(__stdcall*)(const void* address, int length, int type);
using ntohs = std::uint16_t(__stdcall*)(std::uint16_t v);
using inet_addr = std::uint32_t(__stdcall*)(const char* text);
using gethostbyname = void*(__stdcall*)(const char* name);
using htons = std::uint16_t(__stdcall*)(std::uint16_t v);
using ioctlsocket = int(__stdcall*)(std::uintptr_t s, long command, std::uint32_t* argument);
using sendto = int(__stdcall*)(std::uintptr_t s, const void* data, int length, int flags, const void* to, int toLength);

} // namespace ei::import_fn
