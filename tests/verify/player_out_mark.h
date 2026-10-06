#pragma once

// A mark in a player connection's outgoing messages (CPlayerServer::out_ and lastPosition_), for
// wrappers that run a message writer twice (ours, then the original) and compare what each wrote.
// The connection is passed untyped so that callers need not include player_messages.h
// (verify_player_out_mark.cpp).

#include <cstdint>
#include <vector>

struct PlayerOutMark {
    std::uint8_t header[0x28];   // the CMemoryStream, copied raw (rewrite guide: novtable copies)
    float lastPosition[3];
};

PlayerOutMark PlayerOutTake(const void* server);
// The bytes written since `mark`; false when the buffer moved or grew (the run cannot be repeated).
bool PlayerOutSince(const void* server, const PlayerOutMark& mark, std::vector<std::uint8_t>* bytes);
// Puts the stream's header and the last position back as they were at `mark`.
void PlayerOutRestore(void* server, const PlayerOutMark& mark);
