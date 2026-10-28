#pragma once

// MFC 4.2's CArchive buffer (arccore.cpp, afx.inl) as the game's copy has it: Read, Write, Flush, FillBuffer,
// the count and string-length encodings, the BYTE / WORD / DWORD operators and the CString operators (on a
// std::string) with the conversion of a wide string the loader uses (docs/notes/standalone.md section 2). The bodies
// are in mfc_archive.cpp.

#include "mfc_exception.h"
#include "mfc_file.h"

#include <string>

namespace mfc::impl {

// CArchive (game.exe's 0x44 bytes less m_strFileName, which no code of ours reads; no code of ours builds an
// archive either). The buffer runs from `bufferStart`; loading reads it up to `bufferMax`,
// storing fills it up to there. A file with a buffer of its own (CMemFile; `directBuffer`) hands it
// out through CFile::GetBufferPtr instead.
struct CArchive {
    enum Mode : unsigned { store = 0, load = 1, noFlushOnDelete = 2, noByteSwap = 4 };
    enum BufferCommand : unsigned { bufferRead = 0, bufferWrite = 1, bufferCommit = 2, bufferCheck = 3 };

    void* document;
    BOOL forceFlat;
    BOOL directBuffer;
    unsigned objectSchema;
    unsigned mode;
    BOOL userBuffer;
    unsigned bufferSize;
    CFile* file;
    unsigned char* bufferCur;
    unsigned char* bufferMax;
    unsigned char* bufferStart;
    unsigned mapCount;
    void* objectMap;                // m_pLoadArray / m_pStoreMap
    void* schemaMap;
    unsigned growSize;
    unsigned hashSize;

    bool IsLoading() const { return (mode & load) != 0; }

    unsigned Read(void* buffer, unsigned count);
    void Write(const void* buffer, unsigned count);
    void Flush();
    // Refills the load buffer so that `needed` more bytes than it holds are there (endOfFile when not).
    void FillBuffer(unsigned needed);
    // A count: a WORD below 0xFFFF, else 0xFFFF and a DWORD.
    void WriteCount(unsigned long count);
    unsigned long ReadCount();

    // operator<< / operator>> of BYTE, WORD and DWORD (afx.inl, compiled out of line).
    CArchive* StoreByte(unsigned char value);
    CArchive* StoreWord(unsigned short value);
    CArchive* StoreDword(unsigned long value);
    CArchive* LoadByte(unsigned char& value);
    CArchive* LoadWord(unsigned short& value);
    CArchive* LoadDword(unsigned long& value);
};
static_assert(sizeof(CArchive) == 0x40);

// AfxReadStringLength: a BYTE below 0xFF, else a WORD (0xFFFE: -1, a wide string follows; 0xFFFF:
// a DWORD follows).
unsigned ReadStringLength(CArchive& ar);
// operator<<(CArchive&, const CString&): the length so encoded, then the characters.
CArchive& StoreText(CArchive& ar, const std::string& text);
// operator>>(CArchive&, CString&): a wide string is converted to the ANSI code page (CString::operator=(LPCWSTR):
// at most two bytes a character, up to the first zero byte).
CArchive& LoadText(CArchive& ar, std::string& text);

// _wcstombsz: WideCharToMultiByte into `count` bytes, terminated; the bytes written with the terminator (0 for no
// room).
int WideToMultiByte(char* to, const wchar_t* from, unsigned count);

} // namespace mfc::impl
