// The server list reply (0041D600): a compressed run of {address, GameInfoRecord} turned into the
// client base's server entries. See net_classes.h and docs/notes/network.md.
#include "library_containers.h"
#include "net_classes.h"
#include "net_coder.h"
#include "net_messages.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "game_strings.h"

#include <cstring>

namespace net {

namespace {
// The client base's server entries (0x48 bytes): base constructor, adding one to the list.

// A read the way the buffer helpers inline it: past the end through 004DA170, beyond `end`
// through slot 3, else from memory (00401310).
void ReadBytes(GameBuffer* buffer, void* to, unsigned size) {
    if (buffer->cur + size > buffer->high) buffer->ReadPastEnd(to, size);
    else if (buffer->cur + size > buffer->end) buffer->ReadMore(to, size);
    else buffer->CopyOut(to, size);
}

std::int32_t Offset(const GameBuffer& b, const std::uint8_t* p) {
    return b.PositionOf(p);
}

} // namespace

// 0041D600: reads a server list reply from `in` (rewound): its size, then the LZ-compressed list.
// Each item is a 16-byte address and a GameInfoRecord; it becomes a server entry with the record's
// player counts, kind, password flag and texts, added to the client. A wrong size or
// a short record ends the reading (the entry being filled is not added).
void ClientSession::ReadServerListImpl(GameBuffer* in) {
    GameInfoRecord read;   // its strings freed at the end (description first)
    GameInfoRecord* record = &read;
    stlp::StringElement::Default(&record->name);
    stlp::StringElement::Default(&record->description);
    GrowingBuffer buffer;
    CoderState coder;
    coder.Construct();
    in->Seek(0);
    std::uint32_t size;
    ReadBytes(in, &size, 4);
    if (in->cur > in->high) in->high = in->cur;
    bool complete = false;
    if (size == static_cast<std::uint32_t>(Offset(*in, in->high))) {
        coder.Decompress(in, &buffer);
        BufferUpdateHigh(&buffer);
        std::uint8_t* begin = buffer.begin;
        buffer.cur = begin;
        if (begin > buffer.end) buffer.Grow(0);
        for (;;) {
            if (buffer.cur > buffer.high) buffer.high = buffer.cur;
            if (Offset(buffer, buffer.cur) >= Offset(buffer, buffer.high) || (buffer.flags & 8)) {
                complete = true;
                break;
            }
            auto* entry = NewCounted<ServerEntry>();
            ReadBytes(&buffer, &entry->address_, 0x10);
            if (!record->Read(&buffer)) break;
            entry->kind_ = record->kind;
            entry->password_ = record->hasPassword;
            entry->maxPlayers_ = record->maxPlayers;
            entry->players_ = record->players;
            stlp::string* name = &entry->name_;
            if (&record->name != name) name->AssignFrom(record->name);
            stlp::string* description = &entry->description_;
            if (&record->description != description) description->AssignFrom(record->description);
            AddServer(entry);
        }
    }
    coder.out.End();
}
EI_REWRITTEN(0x0041D600, ClientSession::ReadServerListImpl);

} // namespace net
