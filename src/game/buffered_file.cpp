// The buffered file streams; see buffered_file.h.
#include "buffered_file.h"

#include "net_classes.h"

#include "lib/crt.h"
#include "lib/file_io.h"
#include "lib/static_object.h"

#include <cstdlib>
#include <cstring>
#include <unordered_set>

namespace {

// Flag bits (as GameBuffer's): 0 the window holds unwritten bytes, 3 a short read or write.
constexpr std::uint32_t kFlagUnwritten = 1;
constexpr std::uint32_t kFlagError = 8;
// The first window of an opened file, and what the slots add when a window grows.
constexpr std::uint32_t kWindowBytes = 0x400;
constexpr int kSeekSet = 0;
constexpr int kSeekEnd = 2;

// The block replaced by one of `capacity` bytes holding the old capacity's bytes; the pointers move with it.
void Reallocate(BufferedStream* self, std::int32_t capacity) {
    auto* block = gamecrt::operator_new_array<std::uint8_t>(static_cast<std::uint32_t>(capacity));
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

std::uint8_t* GameBuffer::BeginWrite(std::int32_t size) {
    flags |= kFlagUnwritten;
    if (cur + static_cast<std::uint32_t>(size) > end) ReserveWrite(size);
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

// No caller reads the result.
std::uint32_t BufferedStream::Commit() {
    Rebase();
    return 0;
}

// --- The window's slow paths ---------------------------------------------------------------------

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
    if (std::fseek(file, position, kSeekSet) != 0) return 0;
    return static_cast<int>(std::fread(to, 1, size, file));
}

int BufferedFile::WriteAtFile(std::int32_t position, const void* from, std::uint32_t size) {
    if (!file) return 0;
    if (std::fseek(file, position, kSeekSet) != 0) return 0;
    return static_cast<int>(std::fwrite(from, 1, size, file));
}

void BufferedFile::Close() {
    ReleaseBlock();
    if (file) std::fclose(file);
    file = nullptr;
}

void BufferedFile::Open(const char* path, const char* mode, std::uint32_t newFlags) {
    Close();
    flags = newFlags;
    file = lib::OpenFile(path, mode);
    if (!file) {
        flags |= kFlagError;
        return;
    }
    std::fseek(file, 0, kSeekEnd);
    long size = std::ftell(file);
    flags &= ~kFlagUnwritten;
    auto* block = gamecrt::operator_new_array<std::uint8_t>(kWindowBytes);
    end = block + kWindowBytes;
    begin = block;
    cur = block;
    high = block + size;
    base = 0;
    MoveWindow(0);
}

// A hash_set<int> that nothing of ours reads: built by its static initialiser, destroyed by its exit function.
using UnreadSet = std::unordered_set<int>;
constinit lib::StaticObject<UnreadSet> g_unreadSet = /*007AB8D8*/ {};
namespace {
void DestroyUnreadSet() { g_unreadSet.Destroy(); }
} // namespace

void ConstructUnreadSet() {
    g_unreadSet.Construct();
    std::atexit(DestroyUnreadSet);
}
