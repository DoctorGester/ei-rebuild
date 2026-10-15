// Checks for the three functions with an MFC TRY / CATCH (A-E2; src/lib/mfc_try.h): the script
// loader's text file reader 004D5880 (script_text_file.cpp) and CDib::Read 006D5910 / Write
// 006D5AC0 (dib_file.cpp). Each wrapper compares with the original:
// - ReadTextFile: ours into the caller's text, then the original into a scratch text; result and
//   text.
// - CDib::Write: the original writes, the file is put back to where it was, ours writes; results
//   and the file's position afterwards.
// - CDib::Read: ours into the caller's bitmap, the file put back, the original into a scratch
//   bitmap (freed again), the file left where ours ended; results, the position, and (after a
//   success) the header, the bits and the fields. Ours reads (and allocates) first (SV2h1).
//
// No scenario reaches them (no script text files and no .bmp textures are loaded, and screenshots
// are not taken), so a start-up check drives them through their addresses in a temporary folder:
// a missing text file (the game's CStdioFile constructor throws a CFileException*, which the
// rewrite's handler must catch: the proof that this compiler's handlers catch what the game's MFC
// throws, and that the thread's TRY chain is left as it was), a text file with a long line, three
// bitmaps written and read back (16, 24, 32 bits) and one 8 bit file with a colour table.
// The handlers of CDib (CATCH(CException): a message box) are not driven.
#include "verify.h"

#include "../../src/game/dib.h"
#include "../../src/game/script_parse.h"
#include "../../src/lib/mfc.h"
#include "../../src/lib/mfc_try.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma warning(disable : 4740) // naked forwarders

namespace {

// The functions through their addresses: the wrappers below when the hooks are installed.
__declspec(naked) int __fastcall ReadTextFileAt(const char*, mfc::CString*) EI_ORIGINAL(0x004D5880)
__declspec(naked) int __fastcall DibReadAt(CDib*, void*, mfc::CFile*) EI_ORIGINAL(0x006D5910)
__declspec(naked) int __fastcall DibWriteAt(CDib*, void*, mfc::CFile*) EI_ORIGINAL(0x006D5AC0)

mfc::CFileVirtuals* Virtuals(mfc::CFile* file) { return reinterpret_cast<mfc::CFileVirtuals*>(file); }

bool SameText(const mfc::CString& a, const mfc::CString& b) {
    const int length = scriptparse::Length(a);
    return length == scriptparse::Length(b) && std::memcmp(static_cast<const char*>(a), static_cast<const char*>(b), length) == 0;
}

// 004D5880: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes).
int(__fastcall* s_readTextFile)(const char*, mfc::CString*);

int __fastcall ReadTextFileVerify(const char* path, mfc::CString* text) {
    const int actual = scriptparse::ReadTextFile(path, text);
    mfc::CString expectedText;
    const int expected = s_readTextFile(path, &expectedText);
    ei::verify::Check("ReadTextFile", true, expected == actual && SameText(expectedText, *text),
                      "%s: result %d / %d, %d / %d characters", path, expected, actual, scriptparse::Length(expectedText),
                      scriptparse::Length(*text));
    return actual;
}
EI_VERIFY(0x004D5880, ReadTextFileVerify, 5, s_readTextFile);

// 006D5AC0: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes).
int(__fastcall* s_dibWrite)(CDib*, void*, mfc::CFile*);

int __fastcall DibWriteVerify(CDib* self, void*, mfc::CFile* file) {
    const unsigned long start = Virtuals(file)->GetPosition();
    const int expected = s_dibWrite(self, nullptr, file);
    const unsigned long expectedEnd = Virtuals(file)->GetPosition();
    Virtuals(file)->Seek(static_cast<long>(start), FILE_BEGIN);
    const int actual = self->Write(file);
    const unsigned long actualEnd = Virtuals(file)->GetPosition();
    ei::verify::Check("CDib::Write", true, expected == actual && expectedEnd == actualEnd, "result %d / %d, written %lu / %lu", expected,
                      actual, expectedEnd - start, actualEnd - start);
    return actual;
}
EI_VERIFY(0x006D5AC0, DibWriteVerify, 5, s_dibWrite);

// Two bitmaps read from the same file: the fields but for the addresses, the header and the bits by
// content, a palette in both or in neither.
bool SameRead(const CDib& a, const CDib& b) {
    if (!a.header || !b.header || !a.bits || !b.bits) return false;
    if (a.imageSize != b.imageSize || a.colourCount != b.colourCount || a.maskBytes != b.maskBytes || a.headerOwner != b.headerOwner ||
        a.bitsOwner != b.bitsOwner || (a.palette != nullptr) != (b.palette != nullptr) || a.bitmap != b.bitmap)
        return false;
    if (reinterpret_cast<const std::uint8_t*>(a.colours) - reinterpret_cast<const std::uint8_t*>(a.header) !=
        reinterpret_cast<const std::uint8_t*>(b.colours) - reinterpret_cast<const std::uint8_t*>(b.header))
        return false;
    if (std::memcmp(a.header, b.header, sizeof(BITMAPINFOHEADER)) != 0) return false;
    return std::memcmp(a.bits, b.bits, a.imageSize) == 0;
}

// 006D5910: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes).
int(__fastcall* s_dibRead)(CDib*, void*, mfc::CFile*);

int __fastcall DibReadVerify(CDib* self, void*, mfc::CFile* file) {
    const unsigned long start = Virtuals(file)->GetPosition();
    const int actual = self->Read(file);
    const unsigned long actualEnd = Virtuals(file)->GetPosition();
    Virtuals(file)->Seek(static_cast<long>(start), FILE_BEGIN);
    CDib scratch;
    std::memset(&scratch, 0, sizeof(scratch));
    scratch.Construct();
    const int expected = s_dibRead(&scratch, nullptr, file);
    const unsigned long expectedEnd = Virtuals(file)->GetPosition();
    if (expectedEnd != actualEnd) Virtuals(file)->Seek(static_cast<long>(actualEnd), FILE_BEGIN);
    bool same = expected == actual && expectedEnd == actualEnd;
    if (same && actual) same = SameRead(scratch, *self);
    ei::verify::Check("CDib::Read", true, same, "result %d / %d, read %lu / %lu, image %u / %u bytes, %d / %d colours", expected, actual,
                      expectedEnd - start, actualEnd - start, scratch.imageSize, self->imageSize, scratch.colourCount, self->colourCount);
    scratch.Destruct();
    return actual;
}
EI_VERIFY(0x006D5910, DibReadVerify, 5, s_dibRead);

// ---- The start-up check ----------------------------------------------------------------------------

// The top of the thread's TRY chain: what a new link finds before it.
const mfc::ExceptionLink* TryChainTop() {
    mfc::ExceptionLink probe;
    return probe.previous;
}

bool WriteWholeFile(const char* path, const void* data, std::size_t size) {
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr);
    CloseHandle(file);
    return ok && written == size;
}

std::vector<std::uint8_t> ReadWholeFile(const char* path) {
    std::vector<std::uint8_t> data;
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return data;
    data.resize(GetFileSize(file, nullptr));
    DWORD read = 0;
    if (!data.empty() && (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) || read != data.size())) data.clear();
    CloseHandle(file);
    return data;
}

constexpr unsigned kCreateReadWrite = 0x1002;   // CFile::modeCreate | CFile::modeReadWrite
constexpr unsigned kOpenRead = 0x0000;          // CFile::modeRead

void TextFileCheck(const char* folder) {
    char path[MAX_PATH];
    std::snprintf(path, sizeof(path), "%smissing.txt", folder);
    {
        const mfc::ExceptionLink* top = TryChainTop();
        mfc::CString text("not read yet");
        const int result = ReadTextFileAt(path, &text);
        ei::verify::Check("ReadTextFile (missing file: the throw is caught)", true,
                          result == 0 && scriptparse::Length(text) == 0 && TryChainTop() == top, "result %d, %d characters, chain %p / %p",
                          result, scriptparse::Length(text), TryChainTop(), top);
    }
    // Three lines: a short one, one longer than the reader's buffer (it arrives in pieces), one
    // without a line end. Text mode: "\r\n" is read as "\n".
    const std::string longLine(5000, 'x');
    const std::string onDisk = "first line\r\n" + longLine + "\r\nlast, no line end";
    const std::string expected = "first line\n" + longLine + "\nlast, no line end";
    std::snprintf(path, sizeof(path), "%slines.txt", folder);
    if (!WriteWholeFile(path, onDisk.data(), onDisk.size())) {
        ei::verify::Check("ReadTextFile (three lines)", true, false, "cannot write %s", path);
        return;
    }
    {
        mfc::CString text;
        const int result = ReadTextFileAt(path, &text);
        const int length = scriptparse::Length(text);
        ei::verify::Check("ReadTextFile (three lines)", true,
                          result == 1 && length == static_cast<int>(expected.size()) &&
                              std::memcmp(static_cast<const char*>(text), expected.data(), expected.size()) == 0,
                          "result %d, %d characters of %u", result, length, static_cast<unsigned>(expected.size()));
    }
    DeleteFileA(path);
}

// A bitmap of `bitCount` bits written through the game's writer and read back through its reader.
void BitmapRoundTrip(const char* folder, int bitCount) {
    constexpr int kWidth = 8, kHeight = 3;   // rows of 8 pixels need no padding at any depth
    char name[64];
    std::snprintf(name, sizeof(name), "CDib round trip (%d bits)", bitCount);
    char path[MAX_PATH];
    std::snprintf(path, sizeof(path), "%sbitmap%d.bmp", folder, bitCount);

    CDib written;
    std::memset(&written, 0, sizeof(written));
    if (bitCount == 16) written.ConstructSized(kWidth, kHeight, bitCount, 0xF800, 0x07E0, 0x001F);
    else written.ConstructSized(kWidth, kHeight, bitCount, 0x00FF0000, 0x0000FF00, 0x000000FF);
    written.AllocateBits();
    const std::uint32_t bytes = written.imageSize;
    for (std::uint32_t i = 0; i < bytes; ++i) written.bits[i] = static_cast<std::uint8_t>(i * 7 + bitCount);

    mfc::CFile file;
    if (!file.Open(path, kCreateReadWrite, nullptr)) {
        ei::verify::Check(name, true, false, "cannot create %s", path);
        written.Destruct();
        return;
    }
    const int writeResult = DibWriteAt(&written, nullptr, &file);
    const unsigned long size = Virtuals(&file)->GetPosition();
    const unsigned long headerBytes = sizeof(BITMAPINFOHEADER) + written.maskBytes;
    Virtuals(&file)->Seek(0, FILE_BEGIN);

    CDib read;
    std::memset(&read, 0, sizeof(read));
    read.Construct();
    const int readResult = DibReadAt(&read, nullptr, &file);
    file.Close();

    // The file: the file header, the bitmap's header with its masks, the bits.
    const std::vector<std::uint8_t> onDisk = ReadWholeFile(path);
    BITMAPFILEHEADER fileHeader{};
    bool fileOk = onDisk.size() == sizeof(fileHeader) + headerBytes + bytes;
    if (fileOk) {
        std::memcpy(&fileHeader, onDisk.data(), sizeof(fileHeader));
        fileOk = fileHeader.bfType == 0x4D42 && fileHeader.bfSize == onDisk.size() && fileHeader.bfReserved1 == 0 &&
                 fileHeader.bfReserved2 == 0 && fileHeader.bfOffBits == sizeof(fileHeader) + headerBytes &&
                 std::memcmp(onDisk.data() + sizeof(fileHeader), written.header, headerBytes) == 0 &&
                 std::memcmp(onDisk.data() + fileHeader.bfOffBits, written.bits, bytes) == 0;
    }
    const bool readOk = readResult == 1 && read.header && read.bits && read.imageSize == bytes && read.colourCount == 0 &&
                        std::memcmp(read.header, written.header, headerBytes) == 0 && std::memcmp(read.bits, written.bits, bytes) == 0;
    ei::verify::Check(name, true, writeResult == 1 && size == onDisk.size() && fileOk && readOk,
                      "write %d, %lu bytes (file %u, as expected: %d); read %d, image %u of %u bytes", writeResult, size,
                      static_cast<unsigned>(onDisk.size()), fileOk, readResult, read.imageSize, bytes);
    read.Destruct();
    written.Destruct();
    DeleteFileA(path);
}

// An 8 bit file with its colour table: `coloursUsed` 0 stands for all 256.
void PaletteBitmapCheck(const char* folder, DWORD coloursUsed) {
    constexpr int kWidth = 8, kHeight = 2;
    const int colours = coloursUsed ? static_cast<int>(coloursUsed) : 256;
    char name[64];
    std::snprintf(name, sizeof(name), "CDib::Read (8 bits, biClrUsed %lu)", coloursUsed);
    char path[MAX_PATH];
    std::snprintf(path, sizeof(path), "%spalette%lu.bmp", folder, coloursUsed);

    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER header{};
    std::vector<std::uint8_t> data(sizeof(fileHeader) + sizeof(header) + colours * sizeof(RGBQUAD) + kWidth * kHeight);
    fileHeader.bfType = 0x4D42;
    fileHeader.bfSize = static_cast<DWORD>(data.size());
    fileHeader.bfOffBits = static_cast<DWORD>(data.size() - kWidth * kHeight);
    header.biSize = sizeof(header);
    header.biWidth = kWidth;
    header.biHeight = kHeight;
    header.biPlanes = 1;
    header.biBitCount = 8;
    header.biClrUsed = coloursUsed;   // biSizeImage stays 0: the reader works it out
    std::memcpy(data.data(), &fileHeader, sizeof(fileHeader));
    std::memcpy(data.data() + sizeof(fileHeader), &header, sizeof(header));
    for (std::size_t i = sizeof(fileHeader) + sizeof(header); i < data.size(); ++i) data[i] = static_cast<std::uint8_t>(i * 13);
    if (!WriteWholeFile(path, data.data(), data.size())) {
        ei::verify::Check(name, true, false, "cannot write %s", path);
        return;
    }
    mfc::CFile file;
    if (!file.Open(path, kOpenRead, nullptr)) {
        ei::verify::Check(name, true, false, "cannot open %s", path);
        DeleteFileA(path);
        return;
    }
    CDib read;
    std::memset(&read, 0, sizeof(read));
    read.Construct();
    const int result = DibReadAt(&read, nullptr, &file);
    file.Close();
    const bool ok = result == 1 && read.header && read.bits && read.imageSize == kWidth * kHeight && read.colourCount == colours &&
                    read.palette != nullptr && std::memcmp(read.header, data.data() + sizeof(fileHeader), fileHeader.bfOffBits - sizeof(fileHeader)) == 0 &&
                    std::memcmp(read.bits, data.data() + fileHeader.bfOffBits, kWidth * kHeight) == 0;
    ei::verify::Check(name, true, ok, "result %d, image %u bytes, %d colours, palette %p", result, read.imageSize, read.colourCount,
                      read.palette);
    read.Destruct();
    DeleteFileA(path);
}

void MfcTryStartupCheck() {
    char folder[MAX_PATH];
    char temp[MAX_PATH];
    if (!GetTempPathA(sizeof(temp), temp)) return;
    std::snprintf(folder, sizeof(folder), "%sei_verify_mfc_try_%lu\\", temp, GetCurrentProcessId());
    CreateDirectoryA(folder, nullptr);
    TextFileCheck(folder);
    for (int bitCount : {16, 24, 32}) BitmapRoundTrip(folder, bitCount);
    PaletteBitmapCheck(folder, 0);
    PaletteBitmapCheck(folder, 4);
    RemoveDirectoryA(folder);
}
EI_STARTUP_CHECK(MfcTryStartupCheck);

} // namespace
