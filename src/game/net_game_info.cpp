// The game announcement record (00401000 / 00401170) and the byte buffer's read helpers
// (00401310 / 00401340). See net_classes.h and docs/notes/network.md.
#include "net_classes.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "net_coder.h"
#include "originals_script.h"

#include <cstring>

namespace net {

namespace {

// The growing buffer's out-of-line string and overflow handling (outside the range):
// 004DA200 reads a length-prefixed string (at most `maxLength` bytes),
// 004DA170 reads past the end (zeroes the target, sets error bit 3, takes what is left).

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
void Sanitize(stlp::string* s) {
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(s->size()); ++i) {
        if (static_cast<std::uint8_t>((*s)[i]) < 0x20) (*s)[i] = '|';
    }
}

} // namespace

} // namespace net

// 00401310: copies `size` bytes from the read position and moves it on; returns the buffer.
GameBuffer* GameBuffer::CopyOut(void* to, unsigned size) {
    std::memcpy(to, cur, size);
    cur += size;
    return this;
}

// 00401340: reads `size` bytes: past the highest written position through 004DA170, from memory
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

// 00401000: writes the record: +0x1C, +0x20, the two strings, the bytes +0x18, +0x19, +0x1B,
// +0x1A, then +0x24.
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

// 00401170: reads the record in the same order (strings of at most 0x100 bytes), replaces control
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
