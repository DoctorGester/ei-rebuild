// The server list reply: a compressed run of {address, GameInfoRecord} turned into the
// client base's server entries. See net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "net_coder.h"
#include "net_messages.h"

#include <cstring>

namespace net {

namespace {

// A read the way the buffer helpers inline it: past the end through the overflow handler, beyond `end`
// through the buffer's virtual read, else from memory.
void ReadBytes(GameBuffer* buffer, void* to, unsigned size) {
    if (buffer->cur + size > buffer->high) buffer->ReadPastEnd(to, size);
    else if (buffer->cur + size > buffer->end) buffer->ReadMore(to, size);
    else buffer->CopyOut(to, size);
}


} // namespace

// Reads a server list reply from `in` (rewound): its size, then the LZ-compressed list.
// Each item is a 16-byte address and a GameInfoRecord; it becomes a server entry with the record's
// player counts, kind, password flag and texts, added to the client. A wrong size or
// a short record ends the reading (the entry being filled is not added).
void ClientSession::ReadServerList(GameBuffer* in) {
    GameInfoRecord read;   // its strings freed at the end (description first)
    GameInfoRecord* record = &read;
    GrowingBuffer buffer;
    CoderState coder;
    in->Seek(0);
    std::uint32_t size;
    ReadBytes(in, &size, 4);
    if (in->cur > in->high) in->high = in->cur;
    bool complete = false;
    if (size == static_cast<std::uint32_t>(in->PositionOf(in->high))) {
        coder.Decompress(in, &buffer);
        BufferUpdateHigh(&buffer);
        std::uint8_t* begin = buffer.begin;
        buffer.cur = begin;
        if (begin > buffer.end) buffer.Grow(0);
        for (;;) {
            if (buffer.cur > buffer.high) buffer.high = buffer.cur;
            if (buffer.PositionOf(buffer.cur) >= buffer.PositionOf(buffer.high) || (buffer.flags & 8)) {
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
            std::string* name = &entry->name_;
            if (&record->name != name) *name = record->name;
            std::string* description = &entry->description_;
            if (&record->description != description) *description = record->description;
            AddServer(entry);
        }
    }
    coder.out.End();
}

} // namespace net
