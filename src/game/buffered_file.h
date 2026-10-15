#pragma once

// The buffered file streams (004DA580-004DB9C0): a GameBuffer whose block is a 0x400-byte (or
// larger) window onto a file. `base` is the file position of `begin`; `high` marks the end of the
// file's bytes known to the window. Every read and write of the file goes through slots 7 / 8,
// which seek to an absolute position first. Used by the quest registration (005809D0 reads the
// "quest.reg" image through it). See docs/notes/script.md, "Buffered file streams".

#include "net_coder.h"

#include "lib/crt.h"

#include <cstddef>
#include <cstdint>

// The abstract stream (vtable 0073F8C8: constructor 00580DD0, destructor body 00580DF0, slots 7 / 8
// pure). 0x1C bytes. Flag bits as GameBuffer's: 0 the window holds unwritten bytes, 3 an error.
class __declspec(novtable) BufferedStream : public GameBuffer {
public:
    // 004DB0F0 (slot 6, RET 4). Moves the cursor to file position `position`: inside the window in place,
    // else Flush and Rebase. Returns the cursor as it was before (EAX of the in-window path; the other
    // path leaves no meaningful value and its callers ignore it).
    std::uint8_t* SeekWindow(std::int32_t position);
    // 004DB420 (slot 4, RET 8). A write the window cannot take in place: one larger than the window
    // goes straight to the file (returns slot 8's count); otherwise Rebase, then into the window
    // (unwritten bit) or through slot 4 again.
    int WriteMoreWindow(const void* from, std::uint32_t size);
    // 004DB200 (slot 3, RET 8). A read the window cannot serve in place: one larger than the window goes
    // straight to the file (returns slot 7's count); otherwise Rebase, then from the window, or zeroes
    // past `high` (error bit; what is left before `high` is read through 00401340).
    int ReadMoreWindow(void* to, std::uint32_t size);
    // 004DAFC0 (slot 1, RET 4). A new block of size + 0x400 bytes, unless the window is larger, with the
    // old block's bytes (no file access).
    void ReserveWriteWindow(std::int32_t size);
    // The slots of both vtables (0073F8C8, 0073F8A4), registered at their addresses (rewrite-guide rule 4).
    // 004DAE30 (slot 0, RET 4). Room to read `size` bytes: a window smaller than `size` grows to
    // size + 0x400 (when that is not smaller) and is reloaded at `base`; otherwise Rebase.
    void ReserveReadWindow(std::int32_t size);
    virtual int ReadAt(std::int32_t position, void* to, std::uint32_t size);           // 7
    virtual int WriteAt(std::int32_t position, const void* from, std::uint32_t size);  // 8

    // 004DAAF0. Writes the window's bytes up to the cursor back when they are unwritten (error bit on a
    // short write); `high` follows the cursor upwards.
    void Flush();
    // 004DAC80. Flush, then the block freed (begin = null; the other pointers left).
    void ReleaseBlock();
    // 00580DD0 (the quest registration 005809D0). Constructor: this vtable, no block, base 0,
    // flags 0 (the other pointers are left). Returns this.
    BufferedStream* Construct();
    // 00580DF0. Destructor body: this vtable, ReleaseBlock, then the view base's vtable.
    void DestructBody();
    // 00580E40 (RET 4): DestructBody, then operator delete when bit 0 of `flags` is set.
    BufferedStream* DeletingDestruct(unsigned deleteFlags);
    // BeginWrite (00580EE0) is GameBuffer's: the quest file loader calls it on a growing buffer.

    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073F8C8);
    static constexpr std::uintptr_t kBaseVtableAddress = std::uintptr_t(0x0073CA48);
    // 004DB040 (slot 2). Moves the window so that it starts at the cursor's file position (Flush,
    // then a reload there) unless it already does.
    void Rebase();
    // The reload the slots inline: min(capacity, high - cur) bytes at file position `position` into
    // the block (error bit on a short read), the pointers shifted so the cursor keeps its file
    // position, base = position.
    void MoveWindow(std::int32_t position);
    // The cursor's file position.
    std::int32_t Position() const { return static_cast<std::int32_t>(cur - begin) + base; }
    std::int32_t Capacity() const { return static_cast<std::int32_t>(end - begin); }
};
static_assert(sizeof(BufferedStream) == 0x1C);

// 004DB040 (slot 2). Rebase.
void __fastcall BufferedStreamCommit(BufferedStream* self);

// The file stream (vtable 0073F8A4, destructor body 00580E60). 0x20 bytes.
class __declspec(novtable) BufferedFile : public BufferedStream {
public:
    int WriteAtFile(std::int32_t position, const void* from, std::uint32_t size);
    // 004DB5E0 / 004DB630 (slots 7 / 8, RET 0xC). fseek to `position` from the start, then fread / fwrite
    // of `size` bytes; 0 without a file or when the seek fails.
    int ReadAtFile(std::int32_t position, void* to, std::uint32_t size);
    // 004DB680. Flush, the block freed, the file closed.
    void Close();
    // 004DB700 (RET 0xC). Close, flags = `newFlags`, then fopen(path, mode): on success a 0x400-byte
    // window at file position 0 with `high` at the file's size, loaded; on failure error bit 3.
    void Open(const char* path, const char* mode, std::uint32_t newFlags);
    // 00580E60. Destructor body: this vtable, Close, then BufferedStream's.
    void DestructFile();
    // 00580EC0 (RET 4): DestructFile, then operator delete when bit 0 of `flags` is set.
    BufferedFile* DeletingDestructFile(unsigned deleteFlags);

    static constexpr std::uintptr_t kFileVtableAddress = std::uintptr_t(0x0073F8A4);

    gamecrt::CrtFile* file;   // +0x1C
};
static_assert(offsetof(BufferedFile, file) == 0x1C);
static_assert(sizeof(BufferedFile) == 0x20);


// 004DA580 / 004DA590 (slots 0 / 1 of the fixed view, vtable 0073CA64; RET 4): nothing to reserve.
void __fastcall BufferViewReserve(GameBuffer* self, void*, std::int32_t size);
