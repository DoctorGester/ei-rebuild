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

void BufferedStream::ReleaseBlock() {
    Flush();
    if (begin) gamecrt::operator_delete(begin);
    begin = nullptr;
}

BufferedStream* BufferedStream::Construct() {
    base = 0;
    begin = nullptr;
    flags = 0;
    return this;
}

void BufferedStream::DestructBody() { ReleaseBlock(); }

BufferedStream* BufferedStream::DeletingDestruct(unsigned deleteFlags) {
    DestructBody();
    if (deleteFlags & 1) gamecrt::operator_delete(this);
    return this;
}

std::uint8_t* GameBuffer::BeginWrite(std::int32_t size) {
    flags |= kFlagUnwritten;
    // An unsigned compare of the addresses.
    if (reinterpret_cast<std::uintptr_t>(cur) + static_cast<std::uint32_t>(size) > reinterpret_cast<std::uintptr_t>(end))
        ReserveWrite(size);
    return cur;
}

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

// The original leaves EAX as Rebase does; no caller reads it.
std::uint32_t BufferedStream::Commit() {
    Rebase();
    return 0;
}

// --- Slots ---------------------------------------------------------------------------------------

void BufferedStream::ReserveReadWindow(std::int32_t size) {
    std::int32_t capacity = Capacity();
    if (static_cast<std::uint32_t>(size) <= static_cast<std::uint32_t>(capacity)) {
        Rebase();
        return;
    }
    std::int32_t grown = size + static_cast<std::int32_t>(kWindowBytes);
    if (!(grown < capacity)) ::Reallocate(this, grown);
    // The window is reloaded where it starts (with min(capacity, high - cur) bytes).
    std::int32_t position = base;
    Flush();
    MoveWindow(position);
}

void BufferedStream::ReserveWriteWindow(std::int32_t size) {
    std::int32_t grown = size + static_cast<std::int32_t>(kWindowBytes);
    if (!(grown < Capacity())) ::Reallocate(this, grown);
}

int BufferedStream::ReadMoreWindow(void* to, std::uint32_t size) {
    if (size > static_cast<std::uint32_t>(Capacity())) {
        Flush();
        int read = ReadAt(Position(), to, size);
        if (static_cast<std::uint32_t>(read) != size) flags |= kFlagError;
        cur += size;
        Rebase();
        return read;
    }
    Rebase();
    std::uint8_t* next = cur + size;
    if (next > high) {
        flags |= kFlagError;
        std::memset(to, 0, size);
        if (cur < high)
            return static_cast<int>(ReadBytes(to, static_cast<unsigned>(high - cur)));
        cur = high;
        return 0;
    }
    if (next > end) return ReadMore(to, size);
    std::memcpy(to, cur, size);
    cur += size;
    return static_cast<int>(size);
}

int BufferedStream::WriteMoreWindow(const void* from, std::uint32_t size) {
    if (size > static_cast<std::uint32_t>(Capacity())) {
        Flush();
        int written = WriteAt(Position(), from, size);
        if (static_cast<std::uint32_t>(written) != size) flags |= kFlagError;
        cur += size;
        if (cur > high) high = cur;
        Rebase();
        return written;
    }
    Rebase();
    if (cur + size > end) return WriteMore(from, size);
    flags |= kFlagUnwritten;
    std::memcpy(cur, from, size);
    cur += size;
    return static_cast<int>(size);
}

std::uint8_t* BufferedStream::SeekWindow(std::int32_t position) {
    std::uint8_t* old = cur;
    if (cur > high) high = cur;
    std::uint8_t* target = begin + (position - base);
    if (target >= begin && target < end) {
        cur = target;
        return old;
    }
    Flush();
    cur = target;
    Rebase();
    return old;
}

// --- The file ------------------------------------------------------------------------------------

int BufferedFile::ReadAtFile(std::int32_t position, void* to, std::uint32_t size) {
    if (!file) return 0;
    if (libcrt::Fseek(file, position, kSeekSet) != 0) return 0;
    return static_cast<int>(libcrt::Fread(to, 1, size, file));
}

int BufferedFile::WriteAtFile(std::int32_t position, const void* from, std::uint32_t size) {
    if (!file) return 0;
    if (libcrt::Fseek(file, position, kSeekSet) != 0) return 0;
    return static_cast<int>(gamecrt::fwrite(from, 1, size, file));
}

void BufferedFile::Close() {
    ReleaseBlock();
    if (file) libcrt::Fclose(file);
    file = nullptr;
}

void BufferedFile::DestructFile() {
    Close();
    ReleaseBlock();
}

BufferedFile* BufferedFile::DeletingDestructFile(unsigned deleteFlags) {
    DestructFile();
    if (deleteFlags & 1) gamecrt::operator_delete(this);
    return this;
}

void BufferedFile::Open(const char* path, const char* mode, std::uint32_t newFlags) {
    Close();
    flags = newFlags;
    file = libcrt::Fopen(path, mode);
    if (!file) {
        flags |= kFlagError;
        return;
    }
    libcrt::Fseek(file, 0, kSeekEnd);
    long size = libcrt::Ftell(file);
    flags &= ~kFlagUnwritten;
    auto* block = static_cast<std::uint8_t*>(gamecrt::operator_new(kWindowBytes));
    end = block + kWindowBytes;
    begin = block;
    cur = block;
    high = block + size;
    base = 0;
    MoveWindow(0);
}

// --- The fixed view's empty slots ----------------------------------------------------------------

// 004DA580 / 004DA590 (slots 0 / 1 of the view vtable 0073CA64, RET 4): nothing to reserve.
void GameBuffer::ReserveViewImpl(std::int32_t) {}
EI_REPLACE_METHOD(0x004DA580, GameBuffer::ReserveViewImpl);
EI_REPLACE_METHOD(0x004DA590, GameBuffer::ReserveViewImpl);
