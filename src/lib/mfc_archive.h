#pragma once

// MFC 4.2's CArchive buffer (arccore.cpp, afx.inl) as the game's copy has it: Read, Write, Flush,
// FillBuffer, the count and string-length encodings, the BYTE / WORD / DWORD operators and the
// CString operators, with CString's assignment from a wide string the loader uses
// (docs/notes/standalone.md section 2). Same layout as the exe's; every body is registered at its
// library address (mfc_archive.cpp).

#include "mfc_exception.h"
#include "mfc_file.h"

namespace mfc::impl {

// CArchive (0x44 bytes). The buffer runs from `bufferStart`; loading reads it up to `bufferMax`,
// storing fills it up to there. A file with a buffer of its own (CMemFile; `directBuffer`) hands it
// out through CFile::GetBufferPtr instead.
struct CArchive {
    enum Mode : unsigned { store = 0, load = 1, noFlushOnDelete = 2, noByteSwap = 4 };
    enum BufferCommand : unsigned { bufferRead = 0, bufferWrite = 1, bufferCommit = 2, bufferCheck = 3 };

    void* document;                 // +0x00 m_pDocument
    BOOL forceFlat;                 // +0x04
    BOOL directBuffer;              // +0x08 m_bDirectBuffer
    unsigned objectSchema;          // +0x0C
    CString fileName;               // +0x10 m_strFileName
    unsigned mode;                  // +0x14 m_nMode
    BOOL userBuffer;                // +0x18
    unsigned bufferSize;            // +0x1C m_nBufSize
    CFile* file;                    // +0x20 m_pFile
    unsigned char* bufferCur;       // +0x24 m_lpBufCur
    unsigned char* bufferMax;       // +0x28 m_lpBufMax
    unsigned char* bufferStart;     // +0x2C m_lpBufStart
    unsigned mapCount;              // +0x30
    void* objectMap;                // +0x34 m_pLoadArray / m_pStoreMap
    void* schemaMap;                // +0x38
    unsigned growSize;              // +0x3C
    unsigned hashSize;              // +0x40

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
static_assert(sizeof(CArchive) == 0x44);

// AfxReadStringLength: a BYTE below 0xFF, else a WORD (0xFFFE: -1, a wide string follows; 0xFFFF:
// a DWORD follows).
unsigned __stdcall ReadStringLength(CArchive& ar);
// operator<<(CArchive&, const CString&): the length so encoded, then the characters.
CArchive& __stdcall StoreText(CArchive& ar, const CString& text);
// operator>>(CArchive&, CString&): a wide string is converted to the ANSI code page.
CArchive& __stdcall LoadText(CArchive& ar, CString& text);

// CString::operator=(LPCWSTR) is CString::AssignWide (mfc_string.h).
// _wcstombsz: WideCharToMultiByte into `count` bytes, terminated; the bytes written with the
// terminator (0 for no room).
int __cdecl WideToMultiByte(char* to, const wchar_t* from, unsigned count);

} // namespace mfc::impl
