#pragma once

// The buffered file streams: a GameBuffer whose block is a 0x400-byte (or
// larger) window onto a file. `base` is the file position of `begin`; `high` marks the end of the
// file's bytes known to the window. Every read and write of the file goes through slots 7 / 8,
// which seek to an absolute position first. Used by the quest registration (it reads the
// "quest.reg" image through it). See docs/notes/script.md, "Buffered file streams".

#include "net_coder.h"

#include "lib/crt.h"

#include <cstddef>
#include <cstdint>

// The abstract stream (slots 7 / 8 pure). 0x1C bytes. Flag bits as GameBuffer's: 0 the window holds unwritten
// bytes, 3 an error. BufferedStream and BufferedFile are real virtual classes: the compiler makes their
// vtables. The slots' bodies keep their names (registered at their addresses, rewrite-guide rule 4); the
// overrides call them.
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
    // Moves the cursor to file position `position`: inside the window in place,
    // else Flush and Rebase. Returns the cursor as it was before (the in-window path's result; the other
    // path leaves no meaningful value and its callers ignore it).
    std::uint8_t* SeekWindow(std::int32_t position);
    // A write the window cannot take in place: one larger than the window goes straight to the file (returns
    // slot 8's count); otherwise Rebase, then into the window (unwritten bit) or through slot 4 again.
    int WriteMoreWindow(const void* from, std::uint32_t size);
    // A read the window cannot serve in place: one larger than the window goes
    // straight to the file (returns slot 7's count); otherwise Rebase, then from the window, or zeroes
    // past `high` (error bit; what is left before `high` is still read).
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
    // BeginWrite is GameBuffer's: the quest file loader calls it on a growing buffer.

    // Moves the window so that it starts at the cursor's file position (Flush,
    // then a reload there) unless it already does.
    void Rebase();
    // The reload the slots inline: min(capacity, high - cur) bytes at file position `position` into
    // the block (error bit on a short read), the pointers shifted so the cursor keeps its file
    // position, base = position.
    void MoveWindow(std::int32_t position);
    std::int32_t Position() const { return static_cast<std::int32_t>(cur - begin) + base; }
    std::int32_t Capacity() const { return static_cast<std::int32_t>(end - begin); }
};
static_assert(sizeof(BufferedStream) == 0x1C);

// The file stream. 0x20 bytes.
class BufferedFile : public BufferedStream {
public:
    // The constructor the quest file loader inlines: BufferedStream's, no file.
    BufferedFile() : file(nullptr) {}
    ~BufferedFile() override { Close(); }

    int ReadAt(std::int32_t position, void* to, std::uint32_t size) override { return ReadAtFile(position, to, size); }
    int WriteAt(std::int32_t position, const void* from, std::uint32_t size) override {
        return WriteAtFile(position, from, size);
    }

    int WriteAtFile(std::int32_t position, const void* from, std::uint32_t size);
    // Fseek to `position` from the start, then fread / fwrite
    // of `size` bytes; 0 without a file or when the seek fails.
    int ReadAtFile(std::int32_t position, void* to, std::uint32_t size);
    void Close();
    // Close, flags = `newFlags`, then fopen(path, mode): on success a 0x400-byte
    // window at file position 0 with `high` at the file's size, loaded; on failure error bit 3.
    void Open(const char* path, const char* mode, std::uint32_t newFlags);

    gamecrt::CrtFile* file;
};
static_assert(offsetof(BufferedFile, file) == 0x1C);
static_assert(sizeof(BufferedFile) == 0x20);

