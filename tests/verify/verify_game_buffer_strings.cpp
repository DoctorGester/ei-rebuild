// Checks for the byte buffer's strings (game_buffer_strings.cpp). Scenarios: net_lobby (game
// announcements carry the game's name and map), world_client / newgame (the messages' strings).
#include "verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/net_coder.h"
#include "lib/stlp.h"

#include <cstring>

// 004DA200: SUB ESP,8; PUSH EBX; MOV EBX,ECX (6 bytes). When the whole string lies before `end`
// and `high` (no slot calls that could move the block), the original runs first on a byte copy of
// the buffer with a scratch string; result, cursor offset, flags and the text are compared.
// Otherwise ours only.
static bool (__fastcall* s_readString)(GameBuffer*, void*, net::StringRep*, int);
static bool __fastcall ReadStringVerify(GameBuffer* buffer, void*, net::StringRep* s, int maxLength) {
    const std::uint8_t* at = buffer->cur;
    const std::uint8_t* limit = buffer->end < buffer->high ? buffer->end : buffer->high;
    bool inPlace = at + 1 <= limit;
    std::uint32_t word = 0;
    std::uint32_t header = 1;
    if (inPlace) {
        word = at[0];
        if (word & 1) {
            header = 4;
            inPlace = at + 4 <= limit;
            if (inPlace) std::memcpy(&word, at, 4);
        }
    }
    const int length = static_cast<int>(word) >> 1;
    inPlace = inPlace && at + header + length <= limit;
    if (!inPlace) return buffer->ReadString(s, maxLength);

    GameBuffer copy(*buffer);
    stlp::string scratch(s->start, s->finish);
    bool expected = s_readString(&copy, nullptr, reinterpret_cast<net::StringRep*>(&scratch), maxLength);
    bool actual = buffer->ReadString(s, maxLength);
    ei::verify::Check("ReadString", expected, actual, "%d");
    ei::verify::Check("ReadString cursor", copy.cur - copy.begin, buffer->cur - buffer->begin, "%d");
    ei::verify::Check("ReadString flags", copy.flags, buffer->flags, "%08X");
    bool same = scratch.size() == static_cast<std::size_t>(s->finish - s->start) &&
                std::memcmp(scratch.data(), s->start, scratch.size()) == 0 && *s->finish == 0;
    ei::verify::Check("ReadString text", true, same, "length %d", length);
    return actual;
}
EI_VERIFY(0x004DA200, ReadStringVerify, 6, s_readString);

// 004DA380 (ours only): the header and the characters end up where the cursor was (the block may
// have moved), and the cursor is past them.
static void (__fastcall* s_writeString)(GameBuffer*, void*, const net::StringRep*);
static void __fastcall WriteStringVerify(GameBuffer* buffer, void*, const net::StringRep* s) {
    const auto offset = buffer->cur - buffer->begin;
    const auto length = static_cast<std::uint32_t>(s->finish - s->start);
    buffer->WriteString(s);
    const std::uint32_t header = length >= 0x80 ? 4 : 1;
    const std::uint8_t* at = buffer->begin + offset;
    std::uint32_t word = 0;
    std::memcpy(&word, at, header);
    const std::uint32_t expectedWord = header == 4 ? length * 2 + 1 : (length * 2) & 0xFF;
    ei::verify::Check("WriteString header", expectedWord, word, "%08X");
    ei::verify::Check("WriteString text", 0, std::memcmp(at + header, s->start, length), "%d");
    ei::verify::Check("WriteString cursor", static_cast<long>(offset + header + length),
                      static_cast<long>(buffer->cur - buffer->begin), "%ld");
}
EI_VERIFY(0x004DA380, WriteStringVerify, 0, s_writeString);
