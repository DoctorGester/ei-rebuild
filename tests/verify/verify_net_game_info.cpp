// Checks for net_game_info.cpp.
//  - BufferCopyOut / BufferRead (in-memory case): the original on a copy of the buffer header into
//    a scratch target, ours on the real one; same bytes, same new position.
//  - BufferRead's other cases and the record writer / reader: ours only (they reach the buffer's
//    virtual slots or 004DA170, which change the buffer).
#include "net_verify.h"

#include "../../src/game/net_classes.h"

#include <cstring>
#include <vector>

namespace {

using net::GameInfoRecord;

void* s_unused;

// 00401310: MOV EDX,[ESP+8] (4), MOV EAX,ECX (2).
GameBuffer*(__fastcall* s_copyOut)(GameBuffer*, void*, void*, unsigned);
GameBuffer* __fastcall CopyOutVerify(GameBuffer* buffer, void*, void* to, unsigned size) {
    GameBuffer copy = *buffer;
    std::vector<std::uint8_t> scratch(size + 1);
    GameBuffer* expected = s_copyOut(&copy, nullptr, scratch.data(), size);
    GameBuffer* actual = net::BufferCopyOut(buffer, nullptr, to, size);
    bool ok = expected == &copy && actual == buffer && copy.cur == buffer->cur &&
              std::memcmp(scratch.data(), to, size) == 0;
    ei::verify::Check("BufferCopyOut", true, ok, "size %u", size);
    return actual;
}

// 00401340: MOV EAX,[ESP+8] (4), MOV EDX,ECX (2).
unsigned(__fastcall* s_read)(GameBuffer*, void*, void*, unsigned);
unsigned __fastcall ReadVerify(GameBuffer* buffer, void*, void* to, unsigned size) {
    std::uint8_t* next = buffer->cur + size;
    if (next > buffer->high || next > buffer->end) {
        unsigned result = net::BufferRead(buffer, nullptr, to, size);
        ei::verify::Check("BufferRead (slow)", true, true, "");
        return result;
    }
    GameBuffer copy = *buffer;
    std::vector<std::uint8_t> scratch(size + 1);
    unsigned expected = s_read(&copy, nullptr, scratch.data(), size);
    unsigned actual = net::BufferRead(buffer, nullptr, to, size);
    bool ok = expected == actual && copy.cur == buffer->cur && std::memcmp(scratch.data(), to, size) == 0;
    ei::verify::Check("BufferRead", true, ok, "size %u", size);
    return actual;
}

// 00401000: ours only; the magic goes out last.
void __fastcall WriteVerify(const GameInfoRecord* self, void*, GameBuffer* buffer) {
    self->Write(buffer);
    ei::verify::Check("GameInfoRecord::Write", true, true, "");
}

// 00401170: ours only; no control characters remain in the strings.
int __fastcall ReadRecordVerify(GameInfoRecord* self, void*, GameBuffer* buffer) {
    int result = self->Read(buffer);
    bool clean = true;
    for (const net::StringRep* s : {&self->name, &self->description})
        for (const char* p = s->start; p != s->finish; ++p) clean = clean && static_cast<std::uint8_t>(*p) >= 0x20;
    ei::verify::Check("GameInfoRecord::Read", true, clean && (result == 0 || result == 1), "");
    return result;
}

} // namespace

EI_VERIFY(0x00401310, CopyOutVerify, 6, s_copyOut);
EI_VERIFY(0x00401340, ReadVerify, 6, s_read);
EI_VERIFY(0x00401000, WriteVerify, 0, s_unused);
EI_VERIFY(0x00401170, ReadRecordVerify, 0, s_unused);
