// The game server's shared files (called by the world for the saved zone data): a file
// message split into 64-byte chunk messages, queued on files. See net_classes.h and docs/notes/network.md.
#include "net_coder.h"
#include "net_messages.h"
#include "core_refcount.h"

#include <cstring>

namespace net {

namespace {

constexpr std::int32_t kChunkSize = static_cast<std::int32_t>(FileChunkMessage::kDataSize);

} // namespace

// Shares `data` (read from its start to its highest position) under `name` unless a file of that name is
// listed already: a file message (chunk count, size, name) with a chunk message (the file, its index, up to
// 64 bytes) per 64 bytes. The vector is first filled with one shared chunk (a copy per element when shared,
// as SharedRef copies are), whose elements are then filled in turn.
void ServerSession::ShareFile(const std::string* fileName, GameBuffer* data) {
    for (auto node = files.begin(); node != files.end(); ++node)
        if (node->Get()->name == *fileName) return;

    auto* file = NewCounted<FileMessage>();

    BufferUpdateHigh(data);
    std::int32_t size = data->PositionOf(data->high);
    std::int32_t chunkCount = (size + kChunkSize - 1) / kChunkSize;
    if (fileName != &file->name) file->name = *fileName;
    file->size = size;
    file->chunkCount = static_cast<std::uint16_t>(chunkCount);

    // Filled with shares of one chunk: each element a clone of it (it is marked copy-on-share by the first share).
    auto& chunks = file->chunks;
    chunks.resize(static_cast<std::size_t>(chunkCount), SharedRef<RefObject>(NewCounted<FileChunkMessage>()));

    data->Seek(0);
    std::int32_t remaining = size;
    for (std::int32_t i = 0; i < chunkCount; ++i) {
        auto* piece = static_cast<FileChunkMessage*>(chunks[i].Get());
        piece->file.Assign(file);
        piece->index = static_cast<std::uint16_t>(i);
        unsigned n = static_cast<unsigned>(remaining > kChunkSize ? kChunkSize : remaining);
        std::uint8_t* to = piece->data;
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

    files.emplace_back(file);
}

} // namespace net
