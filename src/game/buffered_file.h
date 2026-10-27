#pragma once

// The buffered file streams: a GameBuffer whose block is a 0x400-byte (or larger) window onto a file. `base` is
// the file position of `begin`; `high` marks the end of the bytes known to the window. Used by the quest
// registration. See docs/notes/script.md, "Buffered file streams".

#include "net_coder.h"

#include "lib/crt.h"

#include <cstddef>
#include <cstdint>

// The abstract stream. Flag bits as GameBuffer's: 0 the window holds unwritten bytes, 3 an error.
class BufferedStream : public GameBuffer {
public:
    BufferedStream() {
        base = 0;
        begin = nullptr;
        flags = 0;
    }
    ~BufferedStream() override { ReleaseBlock(); }
    static void operator delete(void* block) { gamecrt::operator_delete(block); }

    void ReserveRead(std::int32_t size) override { ReserveReadWindow(size); }
    void ReserveWrite(std::int32_t size) override { ReserveWriteWindow(size); }
    std::uint32_t Commit() override;
    int ReadMore(void* to, std::uint32_t size) override { return ReadMoreWindow(to, size); }
    int WriteMore(const void* from, std::uint32_t size) override { return WriteMoreWindow(from, size); }
    std::uint8_t* Seek(std::int32_t position) override { return SeekWindow(position); }
    // Moves the cursor to file position `position`: in place inside the window, else Flush and Rebase. Returns
    // the old cursor on the in-window path only; the callers ignore it.
    std::uint8_t* SeekWindow(std::int32_t position);
    // A write the window cannot take in place: one larger than the window goes straight to the file (WriteAt's
    // count); otherwise Rebase, then into the window or through WriteMore again.
    int WriteMoreWindow(const void* from, std::uint32_t size);
    // A read the window cannot serve in place: one larger than the window goes straight to the file; otherwise
    // Rebase, then from the window, or zeroes past `high` (error bit; what is before `high` is still read).
    int ReadMoreWindow(void* to, std::uint32_t size);
    // A new block of size + 0x400 bytes, unless the window is larger, with the
    // old block's bytes (no file access).
    void ReserveWriteWindow(std::int32_t size);
    // Room to read `size` bytes: a window smaller than `size` grows to
    // size + 0x400 (when that is not smaller) and is reloaded at `base`; otherwise Rebase.
    void ReserveReadWindow(std::int32_t size);
    virtual int ReadAt(std::int32_t position, void* to, std::uint32_t size) = 0;
    virtual int WriteAt(std::int32_t position, const void* from, std::uint32_t size) = 0;

    // Writes the window's bytes up to the cursor back when they are unwritten (error bit on a
    // short write); `high` follows the cursor upwards.
    void Flush();
    // Flush, then the block freed (begin = null; the other pointers left).
    void ReleaseBlock();

    // Moves the window to start at the cursor's file position (Flush, then a reload) unless it already does.
    void Rebase();
    // The reload the slots inline: min(capacity, high - cur) bytes at file position `position` into
    // the block (error bit on a short read), the pointers shifted so the cursor keeps its file
    // position, base = position.
    void MoveWindow(std::int32_t position);
    std::int32_t Position() const { return static_cast<std::int32_t>(cur - begin) + base; }
    std::int32_t Capacity() const { return static_cast<std::int32_t>(end - begin); }
};
static_assert(sizeof(BufferedStream) == 0x1C);

// The file stream.
class BufferedFile : public BufferedStream {
public:
    BufferedFile() : file(nullptr) {}
    ~BufferedFile() override { Close(); }

    int ReadAt(std::int32_t position, void* to, std::uint32_t size) override { return ReadAtFile(position, to, size); }
    int WriteAt(std::int32_t position, const void* from, std::uint32_t size) override {
        return WriteAtFile(position, from, size);
    }

    int WriteAtFile(std::int32_t position, const void* from, std::uint32_t size);
    // 0 without a file or when the seek fails.
    int ReadAtFile(std::int32_t position, void* to, std::uint32_t size);
    void Close();
    // Close, flags = `newFlags`, then fopen(path, mode): on success a 0x400-byte
    // window at file position 0 with `high` at the file's size, loaded; on failure error bit 3.
    void Open(const char* path, const char* mode, std::uint32_t newFlags);

    gamecrt::CrtFile* file;
};
static_assert(offsetof(BufferedFile, file) == 0x1C);
static_assert(sizeof(BufferedFile) == 0x20);

