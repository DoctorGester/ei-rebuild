// The game announcement record and the byte buffer's read helpers
//. See net_classes.h and docs/notes/network.md.
#include "net_classes.h"

#include "ei/hook.h"
#include "net_coder.h"

#include <cstring>

namespace net {

namespace {

// A value written the way the record writer inlines it: in place (marking the buffer written)
// when it fits before `end`, otherwise through slot 4.
void Put(GameBuffer* buffer, const void* value, unsigned size) {
    if (buffer->cur + size <= buffer->end) {
        buffer->flags |= 1;
        std::memcpy(buffer->cur, value, size);
        buffer->cur += size;
    } else {
        buffer->WriteMore(value, size);
    }
}

// Control characters (below 0x20) of a received string become '|'.
void Sanitize(std::string* s) {
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(s->size()); ++i) {
        if (static_cast<std::uint8_t>((*s)[i]) < 0x20) (*s)[i] = '|';
    }
}

} // namespace

} // namespace net

// Copies `size` bytes from the read position and moves it on; returns the buffer.
GameBuffer* GameBuffer::CopyOut(void* to, unsigned size) {
    std::memcpy(to, cur, size);
    cur += size;
    return this;
}

EI_REWRITTEN(0x00401310, GameBuffer::CopyOut);
// Reads `size` bytes: past the highest written position through the overflow handler, from memory
// when they lie before `end`, otherwise through slot 3. Returns `size` or what the callee left.
unsigned GameBuffer::ReadBytes(void* to, unsigned size) {
    std::uint8_t* next = cur + size;
    if (next > high) return ReadPastEnd(to, size);
    if (next > end) return static_cast<unsigned>(ReadMore(to, size));
    std::memcpy(to, cur, size);
    cur += size;
    return size;
}

namespace net {
EI_REWRITTEN(0x00401340, GameBuffer::ReadBytes);

void GameInfoRecord::Write(GameBuffer* buffer) const {
    Put(buffer, &masterKey, 4);
    Put(buffer, &masterToken, 4);
    buffer->WriteString(&name);
    buffer->WriteString(&description);
    Put(buffer, &players, 1);
    Put(buffer, &maxPlayers, 1);
    Put(buffer, &hasPassword, 1);
    Put(buffer, &kind, 1);
    Put(buffer, &magic, 4);
}

// Reads the record in the same order (strings of at most 0x100 bytes), replaces control
// characters in both strings with '|'; returns 1 unless the buffer ran short (error bit 3).
int GameInfoRecord::Read(GameBuffer* buffer) {
    buffer->ReadBytes(&masterKey, 4);
    buffer->ReadBytes(&masterToken, 4);
    buffer->ReadString(&name, 0x100);
    buffer->ReadString(&description, 0x100);
    buffer->ReadBytes(&players, 1);
    buffer->ReadBytes(&maxPlayers, 1);
    buffer->ReadBytes(&hasPassword, 1);
    buffer->ReadBytes(&kind, 1);
    buffer->ReadBytes(&magic, 4);
    Sanitize(&name);
    Sanitize(&description);
    return static_cast<int>((~buffer->flags >> 3) & 1);
}

EI_REPLACE_METHOD(0x00401000, GameInfoRecord::Write);
EI_REWRITTEN(0x00401170, GameInfoRecord::Read);

} // namespace net
