#pragma once

// Sockets (docs/notes/platform.md): IPv4 UDP and TCP sockets, host names and addresses, waiting for readiness. The
// calls mirror BSD sockets one to one, each failing the way the system's does (the callers rely on it), so a port
// of the layer maps them straight onto its own sockets.

#include <cstdint>
#include <string>

namespace platform::net {

// A socket: the system's handle, pointer-sized (Winsock's SOCKET); kNoSocket when there is none.
using Socket = std::uintptr_t;
inline constexpr Socket kNoSocket = ~Socket{0};

// An IPv4 address with its port, as the game keeps and sends one: sockaddr_in's 16 bytes (the family, then the port
// and the address in network order, 8 zero bytes). Passed to the system as it is: an address whose family is not
// kIpv4 fails there.
struct Address {
    std::uint16_t family;
    std::uint16_t port;
    std::uint32_t ip;
    std::uint8_t zero[8];
};
static_assert(sizeof(Address) == 16);

inline constexpr std::uint16_t kIpv4 = 2;
// ParseIpv4's answer for a text that is not an IPv4 number (also the broadcast address 255.255.255.255).
inline constexpr std::uint32_t kNoAddress = 0xFFFFFFFFu;

enum class Kind : std::uint8_t { Udp, Tcp };

// Why the last call of this thread failed.
enum class Error : std::uint8_t { None, WouldBlock, AddressInUse, Refused, Other };

// Starts the system's sockets (Winsock 1.1); each Start that succeeded needs a Stop. False when they cannot run.
bool Start();
void Stop();

// A new socket of `kind` (IPv4), blocking; kNoSocket when none could be made.
Socket Open(Kind kind);
void Close(Socket socket);
// Binds `socket` to `address` (ip 0: every address of the host; port 0: any). False when it fails (LastError:
// AddressInUse when another socket holds it).
bool Bind(Socket socket, const Address& address);
// Makes the reads, writes and connects of `socket` return at once (true) or wait (false).
void SetNonBlocking(Socket socket, bool on);
// Lets a UDP socket send to broadcast addresses.
void AllowBroadcast(Socket socket);
// Connects a TCP socket to `address`. A non-blocking socket starts the connect and fails with WouldBlock; Select's
// write group then tells when it is done.
bool Connect(Socket socket, const Address& address);

// Sends `length` bytes to `to` (UDP); the bytes sent, or -1.
int SendTo(Socket socket, const void* data, int length, const Address& to);
// The next datagram (up to `length` bytes; the rest of a longer one is lost) into `data`, its sender into `from`;
// the bytes read, or -1 (nothing waiting on a non-blocking socket, or an error).
int ReceiveFrom(Socket socket, void* data, int length, Address* from);
// Sends `length` bytes on a connected socket; the bytes sent, or -1.
int Send(Socket socket, const void* data, int length);
// Reads up to `length` bytes from a connected socket; the bytes read, 0 when the peer closed it, or -1.
int Receive(Socket socket, void* data, int length);

Error LastError();

// Sockets to wait on (an fd_set). Select leaves in each group only its ready sockets: a group asked again after a
// Select that found none ready is empty.
struct SocketGroup {
    static constexpr int kCapacity = 64;   // FD_SETSIZE

    int count = 0;
    Socket sockets[kCapacity];

    SocketGroup() = default;
    explicit SocketGroup(Socket socket) : count(1) { sockets[0] = socket; }
};

// Waits at most `microseconds` for a socket of `read` to have data (or its peer to close), of `write` to take
// writes (a connect done) or of `failed` to fail (a connect refused); null groups are not asked. The number of ready
// sockets, 0 at the timeout, or -1 on an error, which includes asking only empty groups.
int Select(SocketGroup* read, SocketGroup* write, SocketGroup* failed, std::int32_t microseconds);

// This host's name (gethostname) into `name`, `size` bytes with the terminator; false when it fails or does not fit.
bool HostName(char* name, int size);
// The first IPv4 address of the host `name` (gethostbyname: a name or an IPv4 number) in network order into `ip`;
// false when the host is unknown. Blocks while the name is looked up.
bool LookUp(const char* name, std::uint32_t* ip);
// The name of the host whose address is the `length` bytes at `address` (gethostbyaddr, IPv4: an address is 4
// bytes); empty when there is none.
std::string HostNameAt(const void* address, int length);
// "a.b.c.d" as an address in network order (inet_addr), or kNoAddress.
std::uint32_t ParseIpv4(const char* text);

// A port between the host's byte order and the network's (htons, ntohs).
std::uint16_t ToNetwork(std::uint16_t value);
std::uint16_t FromNetwork(std::uint16_t value);

} // namespace platform::net
