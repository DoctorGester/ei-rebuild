// The server list reply (0041D600): a compressed run of {address, GameInfoRecord} turned into the
// client base's server entries. See net_classes.h and docs/notes/network.md.
#include "library_containers.h"
#include "net_classes.h"
#include "net_coder.h"
#include "net_messages.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_script.h"
#include "game_strings.h"

#include <cstring>

namespace net {

namespace {
__declspec(naked) void __fastcall CoderConstruct(void* /*coder*/) EI_ORIGINAL(0x0042FCA0)
__declspec(naked) void __fastcall CoderDecompress(void* /*coder*/, void*, GameBuffer* /*in*/, GameBuffer* /*out*/) EI_ORIGINAL(0x00430960)
// The client base's server entries (0x48 bytes): base constructor, adding one to the list.
constexpr std::size_t kCoderSize = 0x2828;

// A read the way the buffer helpers inline it: past the end through 004DA170, beyond `end`
// through slot 3, else from memory (00401310).
void ReadBytes(GameBuffer* buffer, void* to, unsigned size) {
    if (buffer->cur + size > buffer->high) buffer->ReadPastEnd(to, size);
    else if (buffer->cur + size > buffer->end) buffer->ReadMore(to, size);
    else buffer->CopyOut(to, size);
}

std::int32_t Offset(const GameBuffer& b, const std::uint8_t* p) {
    return b.base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(b.begin)) +
           static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(p));
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
    GameBuffer buffer;
    static_cast<GameBuffer*>(&buffer)->ConstructGrowing();
    alignas(4) std::uint8_t coder[kCoderSize];   // CoderState
    CoderConstruct(coder);
    auto* coderCursor = reinterpret_cast<BufferCursor*>(coder + 0x2800);
    in->Seek(0);
    std::uint32_t size;
    ReadBytes(in, &size, 4);
    if (in->cur > in->high) in->high = in->cur;
    bool complete = false;
    if (size == static_cast<std::uint32_t>(Offset(*in, in->high))) {
        CoderDecompress(coder, nullptr, in, &buffer);
        BufferUpdateHigh(&buffer);
        std::uint8_t* begin = buffer.begin;
        buffer.cur = begin;
        if (begin > buffer.end) static_cast<GameBuffer*>(&buffer)->Grow(0);
        for (;;) {
            if (buffer.cur > buffer.high) buffer.high = buffer.cur;
            if (Offset(buffer, buffer.cur) >= Offset(buffer, buffer.high) || (buffer.flags & 8)) {
                complete = true;
                break;
            }
            auto* entry = NewMessageMemory<ServerEntry>();
            if (entry) {
                entry->ConstructBase();
                stlp::StringElement::Default(&entry->name_);
                stlp::StringElement::Default(&entry->description_);
                ei::SetVtable(entry, ServerEntry::kVtableAddress);
                entry->name_.Assign(game::QuestionMarkText());
                entry->kind_ = 4;
                entry->description_.Assign(game::QuestionMarkText());
                entry->players_ = 0;
                entry->maxPlayers_ = 0;
                entry->password_ = 0;
            }
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
    coderCursor->End();
    GrowingBufferDestruct(&buffer);
}
EI_REPLACE_METHOD(0x0041D600, ClientSession::ReadServerListImpl);

} // namespace net
