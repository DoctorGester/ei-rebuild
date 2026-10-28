// The game server's shared files (called by the world for the saved zone data): a file
// message split into 64-byte chunk messages, queued on files_. See net_classes.h and docs/notes/network.md.
#include "net_coder.h"
#include "net_messages.h"
#include "core_refcount.h"

#include "lib/crt.h"

#include <cstring>

namespace net {

namespace {

constexpr std::int32_t kChunkSize = static_cast<std::int32_t>(FileChunkMessage::kDataSize);

} // namespace

// Shares `data` (read from its start to its highest position) under `name` unless a file of that name is
// listed already: a file message (chunk count, size, name) with a chunk message (the file, its index, up to
// 64 bytes) per 64 bytes. The vector is first filled with one shared chunk (a copy per element when shared,
// as RefPtr copies are), whose elements are then filled in turn.
void ServerSession::ShareFile(const std::string* name, GameBuffer* data) {
    for (RefNode* node = files_.First(); node != files_.End(); node = node->next)
        if (files_.At(node)->name_ == *name) return;

    auto* file = NewCounted<FileMessage>();

    BufferUpdateHigh(data);
    std::int32_t size = data->PositionOf(data->high);
    std::int32_t chunkCount = (size + kChunkSize - 1) / kChunkSize;
    if (name != &file->name_) file->name_ = *name;
    file->size_ = size;
    file->chunkCount_ = static_cast<std::uint16_t>(chunkCount);

    // Filled with shares of one chunk: each element a clone of it (it is marked copy-on-share by the first share).
    auto& chunks = file->chunks_;
    chunks.resize(static_cast<std::size_t>(chunkCount), SharedRefPtr(NewCounted<FileChunkMessage>()));

    data->Seek(0);
    std::int32_t remaining = size;
    for (std::int32_t i = 0; i < chunkCount; ++i) {
        auto* piece = static_cast<FileChunkMessage*>(chunks[i].ptr);
        piece->file_.Assign(file);
        piece->index_ = static_cast<std::uint16_t>(i);
        unsigned n = static_cast<unsigned>(remaining > kChunkSize ? kChunkSize : remaining);
        std::uint8_t* to = piece->data_;
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
    const RefPtr* value = RefPtrFromRaw(&ref, file);
    files_.Insert(files_.End(), *value);
    ref.ptr = nullptr;
    ref.Destroy();
}

} // namespace net
