// The game server's shared files (00419A00, called by the world for the saved zone data): a file
// message split into 64-byte chunk messages, queued on +0x2598. See net_classes.h and
// docs/notes/network.md.
#include "net_coder.h"
#include "net_messages.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/stlp_vector_refs.h"
#include "originals_library.h"
#include "originals_script.h"

#include <cstring>

namespace net {

namespace {
__declspec(naked) void __fastcall RefNull(RefPtr* /*ref*/) EI_ORIGINAL(0x0041FFC0)

constexpr std::int32_t kChunkSize = static_cast<std::int32_t>(FileChunkMessage::kDataSize);

} // namespace

// 00419A00: shares `data` (read from its start to its highest position) under `name` unless a file
// of that name is listed already: a file message (chunk count, size, name) with a chunk message
// (the file, its index, up to 64 bytes) per 64 bytes. The vector is
// first filled with one shared chunk (a copy per element when shared, as RefPtr copies are),
// whose elements are then filled in turn.
void ServerSession::ShareFile(const stlp::string* name, GameBuffer* data) {
    for (RefNode* node = files_.First(); node != files_.End(); node = node->next)
        if (static_cast<FileMessage*>(node->data.ptr)->name_ == *name) return;

    auto* file = NewMessageMemory<FileMessage>();
    if (file) {
        MessageConstructBase(file);
        stlp::StringElement::Default(&file->name_);
        file->chunks_.ConstructEmpty();
        ei::SetVtable(file, FileMessage::kVtableAddress);
    }

    BufferUpdateHigh(data);
    std::int32_t size = data->base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(data->begin)) +
                        static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(data->high));
    std::int32_t chunkCount = (size + kChunkSize - 1) / kChunkSize;
    if (name != &file->name_) file->name_.AssignFrom(*name);
    file->size_ = size;
    file->chunkCount_ = static_cast<std::uint16_t>(chunkCount);

    auto* chunk = NewMessageMemory<FileChunkMessage>();
    if (chunk) {
        MessageConstructBase(chunk);
        RefNull(&chunk->file_);
        ei::SetVtable(chunk, FileChunkMessage::kVtableAddress);
    }
    RefPtr filler;
    filler.Share(chunk);
    RefVector& chunks = file->chunks_;
    std::uint32_t have = static_cast<std::uint32_t>(chunks.size());
    if (static_cast<std::uint32_t>(chunkCount) < have)
        chunks.EraseRange<stlp::SharedRefElement>(chunks.begin() + chunkCount, chunks.end());
    else
        chunks.FillInsert<stlp::SharedRefElement>(chunks.end(), static_cast<std::uint32_t>(chunkCount) - have, filler);
    filler.Destroy();

    data->Seek(0);
    std::int32_t remaining = size;
    for (std::int32_t i = 0; i < chunkCount; ++i) {
        static_cast<FileChunkMessage*>(chunks.begin()[i].ptr)->file_.Assign(file);
        static_cast<FileChunkMessage*>(chunks.begin()[i].ptr)->index_ = static_cast<std::uint16_t>(i);
        unsigned n = static_cast<unsigned>(remaining > kChunkSize ? kChunkSize : remaining);
        std::uint8_t* to = static_cast<FileChunkMessage*>(chunks.begin()[i].ptr)->data_;
        if (data->cur + n > data->high) {
            data->ReadPastEnd(to, n);
        } else if (data->cur + n > data->end) {
            data->ReadMore(to, n);
        } else {
            std::memcpy(to, data->cur, n);
            data->cur += n;
        }
        remaining -= kChunkSize;
    }

    RefPtr ref;
    const RefPtr* value = RefPtrFromRaw(&ref, file);   // 00427FE0
    files_.Insert(files_.End(), *value);
    ref.ptr = nullptr;
    ref.Destroy();
}
EI_REPLACE_METHOD(0x00419A00, ServerSession::ShareFile);

} // namespace net
