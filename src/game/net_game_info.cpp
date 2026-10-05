// The game announcement record (00401000 / 00401170) and the byte buffer's read helpers
// (00401310 / 00401340). See net_classes.h and docs/notes/unmapped_410_470.md ("Sixth round").
#include "net_classes.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_script.h"

#include <cstring>

namespace net {

namespace {

// The growing buffer's out-of-line string and overflow handling (outside the range):
// 004DA380 writes a length-prefixed string, 004DA200 reads one (at most `maxLength` bytes),
// 004DA170 reads past the end (zeroes the target, sets error bit 3, takes what is left).
__declspec(naked) void __fastcall BufferWriteString(GameBuffer* /*buffer*/, void*, const StringRep* /*s*/) EI_ORIGINAL(0x004DA380)
__declspec(naked) int __fastcall BufferReadString(GameBuffer* /*buffer*/, void*, StringRep* /*s*/, int /*maxLength*/) EI_ORIGINAL(0x004DA200)

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
void Sanitize(StringRep* s) {
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(s->finish - s->start); ++i) {
        if (static_cast<std::uint8_t>(s->start[i]) < 0x20) s->start[i] = '|';
    }
}

} // namespace

// 00401310: copies `size` bytes from the read position and moves it on; returns the buffer.
GameBuffer* __fastcall BufferCopyOut(GameBuffer* buffer, void*, void* to, unsigned size) {
    std::memcpy(to, buffer->cur, size);
    buffer->cur += size;
    return buffer;
}
EI_REPLACE(0x00401310, BufferCopyOut);

// 00401340: reads `size` bytes: past the highest written position through 004DA170, from memory
// when they lie before `end`, otherwise through slot 3. Returns `size` or what the callee left.
unsigned __fastcall BufferRead(GameBuffer* buffer, void*, void* to, unsigned size) {
    std::uint8_t* next = buffer->cur + size;
    if (next > buffer->high) return BufferReadPastEnd(buffer, nullptr, to, size);
    if (next > buffer->end) return static_cast<unsigned>(buffer->ReadMore(to, size));
    std::memcpy(to, buffer->cur, size);
    buffer->cur += size;
    return size;
}
EI_REPLACE(0x00401340, BufferRead);

// 00401000: writes the record: +0x1C, +0x20, the two strings, the bytes +0x18, +0x19, +0x1B,
// +0x1A, then +0x24.
void GameInfoRecord::Write(GameBuffer* buffer) const {
    Put(buffer, &unknown_1C, 4);
    Put(buffer, &unknown_20, 4);
    BufferWriteString(buffer, nullptr, &name);
    BufferWriteString(buffer, nullptr, &description);
    Put(buffer, &players, 1);
    Put(buffer, &maxPlayers, 1);
    Put(buffer, &unknown_1B, 1);
    Put(buffer, &unknown_1A, 1);
    Put(buffer, &magic, 4);
}

// 00401170: reads the record in the same order (strings of at most 0x100 bytes), replaces control
// characters in both strings with '|'; returns 1 unless the buffer ran short (error bit 3).
int GameInfoRecord::Read(GameBuffer* buffer) {
    BufferRead(buffer, nullptr, &unknown_1C, 4);
    BufferRead(buffer, nullptr, &unknown_20, 4);
    BufferReadString(buffer, nullptr, &name, 0x100);
    BufferReadString(buffer, nullptr, &description, 0x100);
    BufferRead(buffer, nullptr, &players, 1);
    BufferRead(buffer, nullptr, &maxPlayers, 1);
    BufferRead(buffer, nullptr, &unknown_1B, 1);
    BufferRead(buffer, nullptr, &unknown_1A, 1);
    BufferRead(buffer, nullptr, &magic, 4);
    Sanitize(&name);
    Sanitize(&description);
    return static_cast<int>((~buffer->flags >> 3) & 1);
}

namespace {
void __fastcall WriteHook(const GameInfoRecord* self, void*, GameBuffer* buffer) { self->Write(buffer); }
int __fastcall ReadHook(GameInfoRecord* self, void*, GameBuffer* buffer) { return self->Read(buffer); }
} // namespace
EI_REPLACE(0x00401000, WriteHook);
EI_REPLACE(0x00401170, ReadHook);

} // namespace net
