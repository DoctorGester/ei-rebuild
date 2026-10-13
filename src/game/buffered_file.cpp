// The buffered file streams (004DA580-004DB9C0); see buffered_file.h.
#include "buffered_file.h"

#include "net_classes.h"   // BufferRead (00401340)

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace {

// Flag bits (as GameBuffer's): 0 the window holds unwritten bytes, 3 a short read or write.
constexpr std::uint32_t kFlagUnwritten = 1;
constexpr std::uint32_t kFlagError = 8;
// The first window of an opened file, and what the slots add when a window grows.
constexpr std::uint32_t kWindowBytes = 0x400;
// fseek origins.
constexpr int kSeekSet = 0;
constexpr int kSeekEnd = 2;

// The block replaced by one of `capacity` bytes holding the old capacity's bytes; the pointers move
// with it (inlined in slots 0 and 1).
void Reallocate(BufferedStream* self, std::int32_t capacity) {
    auto* block = static_cast<std::uint8_t*>(gamecrt::operator_new(static_cast<std::uint32_t>(capacity)));
    if (self->begin) {
        std::memcpy(block, self->begin, static_cast<std::uint32_t>(self->Capacity()));
        gamecrt::operator_delete(self->begin);
    }
    std::uint8_t* old = self->begin;
    self->begin = block;
    self->cur = block + (self->cur - old);
    self->high = block + (self->high - old);
    self->end = block + capacity;
}

} // namespace

// --- The window ----------------------------------------------------------------------------------

void BufferedStream::Flush() {
    if (!(flags & kFlagUnwritten)) return;
    flags &= ~kFlagUnwritten;
    if (!begin) return;
    std::uint32_t size = static_cast<std::uint32_t>(cur - begin);
    if (static_cast<std::uint32_t>(WriteAt(base, begin, size)) != size) flags |= kFlagError;
    if (cur > high) high = cur;
}
EI_REPLACE_METHOD(0x004DAAF0, BufferedStream::Flush);

void BufferedStream::ReleaseBlock() {
    Flush();
    if (begin) gamecrt::operator_delete(begin);
    begin = nullptr;
}
EI_REPLACE_METHOD(0x004DAC80, BufferedStream::ReleaseBlock);

BufferedStream* BufferedStream::Construct() {
    base = 0;
    ei::SetVtable(this, kVtableAddress);
    begin = nullptr;
    flags = 0;
    return this;
}
EI_REPLACE_METHOD(0x00580DD0, BufferedStream::Construct);

void BufferedStream::DestructBody() {
    ei::SetVtable(this, kVtableAddress);
    ReleaseBlock();
    ei::SetVtable(this, kBaseVtableAddress);
}
EI_REPLACE_METHOD(0x00580DF0, BufferedStream::DestructBody);

BufferedStream* BufferedStream::DeletingDestruct(unsigned deleteFlags) {
    DestructBody();
    if (deleteFlags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00580E40, BufferedStream::DeletingDestruct);

std::uint8_t* BufferedStream::BeginWrite(std::int32_t size) {
    flags |= kFlagUnwritten;
    // An unsigned compare of the addresses.
    if (reinterpret_cast<std::uintptr_t>(cur) + static_cast<std::uint32_t>(size) > reinterpret_cast<std::uintptr_t>(end))
        ReserveWrite(size);
    return cur;
}
EI_REPLACE_METHOD(0x00580EE0, BufferedStream::BeginWrite);

void BufferedStream::MoveWindow(std::int32_t position) {
    std::int32_t room = Capacity();
    std::int32_t left = static_cast<std::int32_t>(high - cur);
    std::int32_t size = room < left ? room : left;
    if (ReadAt(position, begin, static_cast<std::uint32_t>(size)) != size) flags |= kFlagError;
    std::int32_t shift = base - position;
    cur += shift;
    high += shift;
    base = position;
}

void BufferedStream::Rebase() {
    std::int32_t position = Position();
    if (base == position) return;
    Flush();
    MoveWindow(position);
}

void __fastcall BufferedStreamCommit(BufferedStream* self) { self->Rebase(); }
EI_REPLACE(0x004DB040, BufferedStreamCommit);

// --- Slots ---------------------------------------------------------------------------------------

void __fastcall BufferedStreamReserveRead(BufferedStream* self, void*, std::int32_t size) {
    std::int32_t capacity = self->Capacity();
    if (static_cast<std::uint32_t>(size) <= static_cast<std::uint32_t>(capacity)) {
        self->Rebase();
        return;
    }
    std::int32_t grown = size + static_cast<std::int32_t>(kWindowBytes);
    if (!(grown < capacity)) Reallocate(self, grown);
    // The window is reloaded where it starts (with min(capacity, high - cur) bytes).
    std::int32_t position = self->base;
    self->Flush();
    self->MoveWindow(position);
}
EI_REPLACE(0x004DAE30, BufferedStreamReserveRead);

void __fastcall BufferedStreamReserveWrite(BufferedStream* self, void*, std::int32_t size) {
    std::int32_t grown = size + static_cast<std::int32_t>(kWindowBytes);
    if (!(grown < self->Capacity())) Reallocate(self, grown);
}
EI_REPLACE(0x004DAFC0, BufferedStreamReserveWrite);

int __fastcall BufferedStreamReadMore(BufferedStream* self, void*, void* to, std::uint32_t size) {
    if (size > static_cast<std::uint32_t>(self->Capacity())) {
        self->Flush();
        int read = self->ReadAt(self->Position(), to, size);
        if (static_cast<std::uint32_t>(read) != size) self->flags |= kFlagError;
        self->cur += size;
        self->Rebase();
        return read;
    }
    self->Rebase();
    std::uint8_t* next = self->cur + size;
    if (next > self->high) {
        self->flags |= kFlagError;
        std::memset(to, 0, size);
        if (self->cur < self->high)
            return static_cast<int>(net::BufferRead(self, nullptr, to, static_cast<unsigned>(self->high - self->cur)));
        self->cur = self->high;
        return 0;
    }
    if (next > self->end) return self->ReadMore(to, size);
    std::memcpy(to, self->cur, size);
    self->cur += size;
    return static_cast<int>(size);
}
EI_REPLACE(0x004DB200, BufferedStreamReadMore);

int __fastcall BufferedStreamWriteMore(BufferedStream* self, void*, const void* from, std::uint32_t size) {
    if (size > static_cast<std::uint32_t>(self->Capacity())) {
        self->Flush();
        int written = self->WriteAt(self->Position(), from, size);
        if (static_cast<std::uint32_t>(written) != size) self->flags |= kFlagError;
        self->cur += size;
        if (self->cur > self->high) self->high = self->cur;
        self->Rebase();
        return written;
    }
    self->Rebase();
    if (self->cur + size > self->end) return self->WriteMore(from, size);
    self->flags |= kFlagUnwritten;
    std::memcpy(self->cur, from, size);
    self->cur += size;
    return static_cast<int>(size);
}
EI_REPLACE(0x004DB420, BufferedStreamWriteMore);

std::uint8_t* __fastcall BufferedStreamSeek(BufferedStream* self, void*, std::int32_t position) {
    std::uint8_t* old = self->cur;
    if (self->cur > self->high) self->high = self->cur;
    std::uint8_t* target = self->begin + (position - self->base);
    if (target >= self->begin && target < self->end) {
        self->cur = target;
        return old;
    }
    self->Flush();
    self->cur = target;
    self->Rebase();
    return old;
}
EI_REPLACE(0x004DB0F0, BufferedStreamSeek);

// --- The file ------------------------------------------------------------------------------------

int __fastcall BufferedFileReadAt(BufferedFile* self, void*, std::int32_t position, void* to, std::uint32_t size) {
    if (!self->file) return 0;
    if (gamecrt::fseek(self->file, position, kSeekSet) != 0) return 0;
    return static_cast<int>(gamecrt::fread(to, 1, size, self->file));
}
EI_REPLACE(0x004DB5E0, BufferedFileReadAt);

int __fastcall BufferedFileWriteAt(BufferedFile* self, void*, std::int32_t position, const void* from,
                                   std::uint32_t size) {
    if (!self->file) return 0;
    if (gamecrt::fseek(self->file, position, kSeekSet) != 0) return 0;
    return static_cast<int>(gamecrt::fwrite(from, 1, size, self->file));
}
EI_REPLACE(0x004DB630, BufferedFileWriteAt);

void BufferedFile::Close() {
    ReleaseBlock();
    if (file) gamecrt::fclose(file);
    file = nullptr;
}
EI_REPLACE_METHOD(0x004DB680, BufferedFile::Close);

void BufferedFile::DestructFile() {
    ei::SetVtable(this, kFileVtableAddress);
    Close();
    ei::SetVtable(this, kVtableAddress);
    ReleaseBlock();
    ei::SetVtable(this, kBaseVtableAddress);
}
EI_REPLACE_METHOD(0x00580E60, BufferedFile::DestructFile);

BufferedFile* BufferedFile::DeletingDestructFile(unsigned deleteFlags) {
    DestructFile();
    if (deleteFlags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00580EC0, BufferedFile::DeletingDestructFile);

void BufferedFile::Open(const char* path, const char* mode, std::uint32_t newFlags) {
    Close();
    flags = newFlags;
    file = gamecrt::fopen(path, mode);
    if (!file) {
        flags |= kFlagError;
        return;
    }
    gamecrt::fseek(file, 0, kSeekEnd);
    long size = gamecrt::ftell(file);
    flags &= ~kFlagUnwritten;
    auto* block = static_cast<std::uint8_t*>(gamecrt::operator_new(kWindowBytes));
    end = block + kWindowBytes;
    begin = block;
    cur = block;
    high = block + size;
    base = 0;
    MoveWindow(0);
}
EI_REPLACE_METHOD(0x004DB700, BufferedFile::Open);

// --- The fixed view's empty slots ----------------------------------------------------------------

// 004DA580 / 004DA590 (slots 0 / 1 of the view vtable 0073CA64, RET 4): nothing to reserve.
void __fastcall BufferViewReserve(GameBuffer*, void*, std::int32_t) {}
EI_REPLACE(0x004DA580, BufferViewReserve);
EI_REPLACE(0x004DA590, BufferViewReserve);
