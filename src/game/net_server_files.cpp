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
// STLport vector<RefPtr> (library): constructor with an allocator, erase(first, last),
// insert(pos, n, value).
__declspec(naked) void __fastcall ChunkVectorConstruct(RefVector* /*vector*/, void*, const void* /*allocator*/) EI_ORIGINAL(0x0041FF90)
__declspec(naked) void __fastcall RefNull(RefPtr* /*ref*/) EI_ORIGINAL(0x0041FFC0)
// The files list (+0x2598): end, insert (the counted pointer is RefPtrFromRaw, core_refcount.h).
__declspec(naked) void** __fastcall FilesEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00420550)
__declspec(naked) void** __fastcall FilesInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x004227D0)

constexpr std::int32_t kChunkSize = static_cast<std::int32_t>(FileChunkMessage::kDataSize);

} // namespace

// 00419A00: shares `data` (read from its start to its highest position) under `name` unless a file
// of that name is listed already: a file message (chunk count, size, name) with a chunk message
// (the file, its index, up to 64 bytes) per 64 bytes. The vector is
// first filled with one shared chunk (a copy per element when shared, as RefPtr copies are),
// whose elements are then filled in turn.
void ServerSession::ShareFile(const StringRep* name, GameBuffer* data) {
    void* files = &files_;
    void** sentinel = static_cast<void**>(files_);
    const std::size_t length = static_cast<std::size_t>(name->finish - name->start);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
        auto* other = static_cast<FileMessage*>(node[2]);
        if (static_cast<std::size_t>(other->name_.finish - other->name_.start) == length &&
            std::memcmp(other->name_.start, name->start, length) == 0)
            return;
    }

    auto* file = NewMessageMemory<FileMessage>();
    if (file) {
        MessageConstructBase(file);
        StringInitEmpty(&file->name_);
        std::uint8_t allocator = 0;
        ChunkVectorConstruct(&file->chunks_, nullptr, &allocator);
        ei::SetVtable(file, FileMessage::kVtableAddress);
    }

    BufferUpdateHigh(data);
    std::int32_t size = data->base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(data->begin)) +
                        static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(data->high));
    std::int32_t chunkCount = (size + kChunkSize - 1) / kChunkSize;
    if (name != &file->name_) StringAssignTaggedOriginal(&file->name_, nullptr, name->start, name->finish, name->start);
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
    void* end;
    FilesEnd(files, nullptr, &end);
    void* inserted;
    FilesInsert(files, nullptr, &inserted, end, value);
    ref.ptr = nullptr;
    ref.Destroy();
}
EI_REPLACE_METHOD(0x00419A00, ServerSession::ShareFile);

} // namespace net
