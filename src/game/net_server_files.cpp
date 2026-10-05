// The game server's shared files (00419A00, called by the world for the saved zone data): a file
// message split into 64-byte chunk messages, queued on +0x2598. See net_classes.h and
// docs/notes/unmapped_410_470.md ("Seventh round").
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

namespace net {

namespace {

__declspec(naked) void __fastcall StringConstruct(StringRep* /*s*/) EI_ORIGINAL(0x0041F460)
__declspec(naked) void __fastcall StringAssign(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/, const char* /*tag*/) EI_ORIGINAL(0x0042A4F0)
// STLport vector<RefPtr> (library): constructor with an allocator, erase(first, last),
// insert(pos, n, value).
__declspec(naked) void __fastcall ChunkVectorConstruct(void* /*vector*/, void*, const void* /*allocator*/) EI_ORIGINAL(0x0041FF90)
__declspec(naked) void __fastcall ChunkVectorErase(void* /*vector*/, void*, RefPtr* /*first*/, RefPtr* /*last*/) EI_ORIGINAL(0x00421D90)
__declspec(naked) void __fastcall ChunkVectorInsert(void* /*vector*/, void*, RefPtr* /*pos*/, std::size_t /*n*/, const RefPtr* /*value*/) EI_ORIGINAL(0x00426CB0)
__declspec(naked) void __fastcall RefNull(void* /*ref*/) EI_ORIGINAL(0x0041FFC0)
// The files list (+0x2598): a counted pointer from a raw one, end, insert.
__declspec(naked) RefPtr* __fastcall FileRefFromRaw(RefPtr* /*ref*/, void* /*object*/) EI_ORIGINAL(0x00427FE0)
__declspec(naked) void** __fastcall FilesEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00420550)
__declspec(naked) void** __fastcall FilesInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x004227D0)
__declspec(naked) unsigned __fastcall BufferReadPastEnd(GameBuffer* /*buffer*/, void*, void* /*to*/, unsigned /*size*/) EI_ORIGINAL(0x004DA170)

constexpr std::uintptr_t kFileVtable = 0x0073BD18;    // 0x2C bytes
constexpr std::uintptr_t kChunkVtable = 0x0073BD30;   // 0x54 bytes
constexpr std::int32_t kChunkSize = 0x40;

void SetVtable(void* p, std::uintptr_t v) { std::memcpy(p, &v, 4); }

// The file message.
struct FileMessage {
    Message header;
    std::uint16_t chunkCount;   // +0x0C
    std::uint16_t padding;
    std::int32_t size;          // +0x10
    StringRep name;             // +0x14
    RefPtr* chunks;             // +0x20 vector<RefPtr> of chunk messages
    RefPtr* chunksEnd;
    RefPtr* chunksEos;
};
static_assert(offsetof(FileMessage, name) == 0x14);
static_assert(sizeof(FileMessage) == 0x2C);

} // namespace

// 00419A00: shares `data` (read from its start to its highest position) under `name` unless a file
// of that name is listed already: a file message (vtable 0073BD18: chunk count, size, name) with a
// chunk message (vtable 0073BD30: the file, its index, up to 64 bytes) per 64 bytes. The vector is
// first filled with one shared chunk (a copy per element when shared, as RefPtr copies are),
// whose elements are then filled in turn.
void ServerSession::ShareFile(const StringRep* name, GameBuffer* data) {
    void* files = &files_;
    void** sentinel = static_cast<void**>(files_);
    const std::size_t length = static_cast<std::size_t>(name->finish - name->start);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
        auto* other = static_cast<FileMessage*>(node[2]);
        if (static_cast<std::size_t>(other->name.finish - other->name.start) == length &&
            std::memcmp(other->name.start, name->start, length) == 0)
            return;
    }

    auto* file = static_cast<FileMessage*>(gamecrt::operator_new(sizeof(FileMessage)));
    file->header.refHeader = 0;
    if (file) {
        MessageConstructBase(&file->header);
        StringConstruct(&file->name);
        std::uint8_t allocator = 0;
        ChunkVectorConstruct(&file->chunks, nullptr, &allocator);
        SetVtable(file, kFileVtable);
    }

    BufferUpdateHigh(data);
    std::int32_t size = data->base - static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(data->begin)) +
                        static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(data->high));
    std::int32_t chunkCount = (size + kChunkSize - 1) / kChunkSize;
    if (name != &file->name) StringAssign(&file->name, nullptr, name->start, name->finish, name->start);
    file->size = size;
    file->chunkCount = static_cast<std::uint16_t>(chunkCount);

    auto* chunk = static_cast<std::uint8_t*>(gamecrt::operator_new(0x54));
    std::uint32_t zero = 0;
    std::memcpy(chunk + 4, &zero, 4);
    if (chunk) {
        MessageConstructBase(reinterpret_cast<Message*>(chunk));
        RefNull(chunk + 0xC);
        SetVtable(chunk, kChunkVtable);
    }
    RefPtr filler;
    filler.Share(reinterpret_cast<CRefObject*>(chunk));
    std::uint32_t have = static_cast<std::uint32_t>(file->chunksEnd - file->chunks);
    if (static_cast<std::uint32_t>(chunkCount) < have)
        ChunkVectorErase(&file->chunks, nullptr, file->chunks + chunkCount, file->chunksEnd);
    else
        ChunkVectorInsert(&file->chunks, nullptr, file->chunksEnd, static_cast<std::uint32_t>(chunkCount) - have, &filler);
    filler.Destroy();

    ei::VCall<int>(data, 6, 0);   // Seek(0)
    std::int32_t remaining = size;
    for (std::int32_t i = 0; i < chunkCount; ++i) {
        auto* piece = reinterpret_cast<std::uint8_t*>(file->chunks[i].ptr);
        reinterpret_cast<RefPtr*>(piece + 0xC)->Assign(reinterpret_cast<CRefObject*>(file));
        std::uint16_t index = static_cast<std::uint16_t>(i);
        std::memcpy(reinterpret_cast<std::uint8_t*>(file->chunks[i].ptr) + 0x10, &index, 2);
        unsigned n = static_cast<unsigned>(remaining > kChunkSize ? kChunkSize : remaining);
        std::uint8_t* to = reinterpret_cast<std::uint8_t*>(file->chunks[i].ptr) + 0x12;
        if (data->cur + n > data->high) {
            BufferReadPastEnd(data, nullptr, to, n);
        } else if (data->cur + n > data->end) {
            ei::VCall<int>(data, 3, to, n);
        } else {
            std::memcpy(to, data->cur, n);
            data->cur += n;
        }
        remaining -= kChunkSize;
    }

    RefPtr ref;
    const RefPtr* value = FileRefFromRaw(&ref, file);
    void* end;
    FilesEnd(files, nullptr, &end);
    void* inserted;
    FilesInsert(files, nullptr, &inserted, end, value);
    ref.ptr = nullptr;
    ref.Destroy();
}
namespace {
void __fastcall ShareFileHook(ServerSession* self, void*, const StringRep* name, GameBuffer* data) { self->ShareFile(name, data); }
} // namespace
EI_REPLACE(0x00419A00, ShareFileHook);

} // namespace net
