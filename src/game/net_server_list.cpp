// The server list reply (0041D600): a compressed run of {address, GameInfoRecord} turned into the
// client base's server entries. See net_classes.h and docs/notes/unmapped_410_470.md
// ("Seventh round").
#include "net_classes.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

namespace net {

namespace {

__declspec(naked) void __fastcall StringConstruct(StringRep* /*s*/) EI_ORIGINAL(0x0041F460)
__declspec(naked) void __fastcall StringAssignText(StringRep* /*s*/, void*, const char* /*text*/) EI_ORIGINAL(0x0041F550)
__declspec(naked) void __fastcall StringAssign(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/, void* /*tag*/) EI_ORIGINAL(0x0042A4F0)
__declspec(naked) void __fastcall StringDestroy(StringRep* /*s*/) EI_ORIGINAL(0x0040C720)
__declspec(naked) void __fastcall RecordDestroy(GameInfoRecord* /*record*/) EI_ORIGINAL(0x0041AC30)
__declspec(naked) void __fastcall GrowingBufferConstruct(GameBuffer* /*buffer*/) EI_ORIGINAL(0x004DA7F0)
__declspec(naked) void __fastcall GrowingBufferDestruct(GameBuffer* /*buffer*/) EI_ORIGINAL(0x004DA910)
__declspec(naked) void __fastcall GameBufferGrow(GameBuffer* /*buffer*/, void*, std::uint32_t /*size*/) EI_ORIGINAL(0x004DA5C0)
__declspec(naked) unsigned __fastcall BufferReadPastEnd(GameBuffer* /*buffer*/, void*, void* /*to*/, unsigned /*size*/) EI_ORIGINAL(0x004DA170)
__declspec(naked) void __fastcall CoderConstruct(void* /*coder*/) EI_ORIGINAL(0x0042FCA0)
__declspec(naked) void __fastcall CoderDecompress(void* /*coder*/, void*, GameBuffer* /*in*/, GameBuffer* /*out*/) EI_ORIGINAL(0x00430960)
__declspec(naked) std::uint32_t __fastcall CursorEnd(BufferCursor* /*cursor*/) EI_ORIGINAL(0x0041D930)
// The client base's server entries (0x48 bytes): base constructor, adding one to the list.
__declspec(naked) void __fastcall ServerEntryConstructBase(void* /*entry*/) EI_ORIGINAL(0x004413E0)
__declspec(naked) void __fastcall ClientAddServer(void* /*client*/, void*, void* /*entry*/) EI_ORIGINAL(0x004428A0)
constexpr std::uintptr_t kServerEntryVtable = 0x0073BB28;
constexpr std::uintptr_t kEmptyText = 0x00783D6C;
constexpr std::size_t kCoderSize = 0x2828;

// A read the way the buffer helpers inline it: past the end through 004DA170, beyond `end`
// through slot 3, else from memory (00401310).
void ReadBytes(GameBuffer* buffer, void* to, unsigned size) {
    if (buffer->cur + size > buffer->high) BufferReadPastEnd(buffer, nullptr, to, size);
    else if (buffer->cur + size > buffer->end) ei::VCall<int>(buffer, 3, to, size);
    else BufferCopyOut(buffer, nullptr, to, size);
}

std::int32_t Offset(const GameBuffer& b, const std::uint8_t* p) {
    return b.base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b.begin)) +
           static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(p));
}

} // namespace

// 0041D600: reads a server list reply from `in` (rewound): its size, then the LZ-compressed list.
// Each item is a 16-byte address and a GameInfoRecord; it becomes a server entry (vtable 0073BB28)
// with the record's player counts, two flag bytes and texts, added to the client. A wrong size or
// a short record ends the reading (the entry being filled is not added).
void __fastcall ServerListRead(void* client, void*, GameBuffer* in) {
    alignas(4) std::uint8_t recordBytes[sizeof(GameInfoRecord)];
    auto* record = reinterpret_cast<GameInfoRecord*>(recordBytes);
    StringConstruct(&record->name);
    StringConstruct(&record->description);
    GameBuffer buffer;
    GrowingBufferConstruct(&buffer);
    alignas(4) std::uint8_t coder[kCoderSize];   // CoderState
    CoderConstruct(coder);
    auto* coderCursor = reinterpret_cast<BufferCursor*>(coder + 0x2800);
    ei::VCall<int>(in, 6, 0);   // Seek(0)
    std::uint32_t size;
    ReadBytes(in, &size, 4);
    if (in->cur > in->high) in->high = in->cur;
    bool complete = false;
    if (size == static_cast<std::uint32_t>(Offset(*in, in->high))) {
        CoderDecompress(coder, nullptr, in, &buffer);
        BufferUpdateHigh(&buffer);
        std::uint8_t* begin = buffer.begin;
        buffer.cur = begin;
        if (begin > buffer.end) GameBufferGrow(&buffer, nullptr, 0);
        for (;;) {
            if (buffer.cur > buffer.high) buffer.high = buffer.cur;
            if (Offset(buffer, buffer.cur) >= Offset(buffer, buffer.high) || (buffer.flags & 8)) {
                complete = true;
                break;
            }
            auto* entry = static_cast<std::uint8_t*>(gamecrt::operator_new(0x48));
            std::uint32_t zero = 0;
            std::memcpy(entry + 4, &zero, 4);
            if (entry) {
                ServerEntryConstructBase(entry);
                auto* name = reinterpret_cast<StringRep*>(entry + 0x28);
                auto* description = reinterpret_cast<StringRep*>(entry + 0x38);
                StringConstruct(name);
                StringConstruct(description);
                std::uintptr_t vtable = kServerEntryVtable;
                std::memcpy(entry, &vtable, 4);
                StringAssignText(name, nullptr, reinterpret_cast<const char*>(kEmptyText));
                std::uint32_t four = 4;
                std::memcpy(entry + 0x34, &four, 4);
                StringAssignText(description, nullptr, reinterpret_cast<const char*>(kEmptyText));
                entry[0x44] = 0;
                entry[0x45] = 0;
                entry[0x46] = 0;
            }
            ReadBytes(&buffer, entry + 8, 0x10);
            if (!record->Read(&buffer)) break;
            std::uint32_t kind = record->unknown_1A;
            std::memcpy(entry + 0x34, &kind, 4);
            entry[0x46] = record->unknown_1B;
            entry[0x45] = record->maxPlayers;
            entry[0x44] = record->players;
            auto* name = reinterpret_cast<StringRep*>(entry + 0x28);
            if (&record->name != name) StringAssign(name, nullptr, record->name.start, record->name.finish, entry);
            auto* description = reinterpret_cast<StringRep*>(entry + 0x38);
            if (&record->description != description)
                StringAssign(description, nullptr, record->description.start, record->description.finish, entry);
            ClientAddServer(client, nullptr, entry);
        }
    }
    CursorEnd(coderCursor);
    GrowingBufferDestruct(&buffer);
    if (complete) {
        RecordDestroy(record);
    } else {
        StringDestroy(&record->description);
        StringDestroy(&record->name);
    }
}
EI_REPLACE(0x0041D600, ServerListRead);

} // namespace net
