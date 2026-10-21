#pragma once

// MFC 4.2's CFile, CStdioFile and CFileFind (filecore.cpp, filetxt.cpp, filefind.cpp) and the path
// helpers they use, written out for the standalone exe (ticket S8, docs/notes/standalone.md
// section 2). Same layout as the copy linked into game.exe, with its vtables; every body is
// registered at its library address (mfc_file.cpp) and checked in game by verify_mfc_file.cpp until S21d.
// The callers keep going through the forwarders (mfc.h, src/game) until C13.
//
// CFile works on Win32 handles through the game's import table. CStdioFile's stream is the game
// CRT's FILE in the hook build (shared with the exe's remaining CRT code) and, in the standalone
// image, the host CRT's (crt.h: there the game's FILE* is the host's).

#include "mfc_exception.h"
#include "mfc_time.h"
#include "game/game_data.h"

namespace mfc::impl {

// CFileStatus (0x118 bytes): what CFile::GetStatus fills in.
struct CFileStatus {
    CTime created;          // +0x00 m_ctime
    CTime modified;         // +0x04 m_mtime
    CTime accessed;         // +0x08 m_atime
    long size;              // +0x0C m_size
    BYTE attribute;         // +0x10 m_attribute
    BYTE padding;           // +0x11
    char fullName[MAX_PATH];    // +0x12 m_szFullName
};
static_assert(sizeof(CFileStatus) == 0x118);

// CFile (0x10 bytes, vtable 00745DF4). The virtual members are reached through the object's
// vtable where the original dispatches (mfc::CFileVirtuals); the bodies here are the slots'.
struct CFile {
    enum OpenFlags : unsigned {
        modeRead = 0x0000, modeWrite = 0x0001, modeReadWrite = 0x0002, shareCompat = 0x0000,
        shareExclusive = 0x0010, shareDenyWrite = 0x0020, shareDenyRead = 0x0030, shareDenyNone = 0x0040,
        modeNoInherit = 0x0080, modeCreate = 0x1000, modeNoTruncate = 0x2000, typeText = 0x4000,
        typeBinary = 0x8000
    };
    static constexpr HANDLE kNoFile = INVALID_HANDLE_VALUE;   // CFile::hFileNull

    void* vtable;
    HANDLE handle;          // +0x04 m_hFile
    BOOL closeOnDelete;     // +0x08 m_bCloseOnDelete
    CString fileName;       // +0x0C m_strFileName (the full path)

    static void* Vtable() { return ei::GamePointer<void>(0x00745DF4); }

    CFile* Construct();                                         // 00701036
    CFile* ConstructHandle(HANDLE file);                        // 0070106C CFile(int hFile)
    CFile* ConstructOpen(const char* path, unsigned flags);     // 0070108B (throws a CFileException*)
    void Destruct();                                            // 0070112B
    CFile* DeletingDestructor(unsigned flags);                  // 00701050 (slot 1)

    unsigned long GetPosition() const;                          // 007013B9 (slot 3)
    // Slots 4-6: the name part of GetStatus's path (AfxGetFileName), its title (AfxGetFileTitle) and the
    // whole path.
    CString GetFileName() const;                                // 00702C31 (slot 4)
    CString GetFileTitle() const;                               // 00702CB3 (slot 5)
    CString GetFilePath() const;                                // 00702D35 (slot 6)
    // 00702D64 (not virtual): the cleared status with the file's name; with an open file also its times
    // (a missing creation or access time is the modification time), size and attributes. FALSE when
    // the handle's times or size cannot be read.
    BOOL GetStatus(CFileStatus& status) const;
    void SetFilePath(const char* path);                         // 006E6361 (slot 7)
    BOOL Open(const char* path, unsigned flags, CFileException* error);   // 007011E8 (slot 8)
    CFile* Duplicate() const;                                   // 00701173 (slot 9)
    long Seek(long offset, unsigned from);                      // 0070138A (slot 10)
    void SetLength(unsigned long length);                       // 007014B6 (slot 11)
    unsigned long GetLength() const;                            // 007014E2 (slot 12)
    unsigned Read(void* buffer, unsigned count);                // 00701305 (slot 13)
    void Write(const void* buffer, unsigned count);             // 0070133F (slot 14)
    void LockRange(unsigned long position, unsigned long count);    // 00701464 (slot 15)
    void UnlockRange(unsigned long position, unsigned long count);  // 0070148D (slot 16)
    void Abort();                                               // 00701444 (slot 17)
    void Flush();                                               // 007013E2 (slot 18)
    void Close();                                               // 00701403 (slot 19)
    unsigned GetBufferPtr(unsigned command, unsigned count, void** start, void** end);   // 0070150F (slot 20: 0)
};
static_assert(sizeof(CFile) == 0x10);

// CStdioFile (0x14 bytes, vtable 007464F0): a CFile with a CRT stream over its handle.
struct CStdioFile : CFile {
    gamecrt::CrtFile* stream;   // +0x10 m_pStream

    static void* Vtable() { return ei::GamePointer<void>(0x007464F0); }

    CStdioFile* Construct();                                    // 00701B79
    CStdioFile* ConstructOpen(const char* path, unsigned flags);    // 00701BD4 (throws a CFileException*)
    void Destruct();                                            // 00701C6D
    CStdioFile* DeletingDestructor(unsigned flags);             // 00701B8F (slot 1)

    unsigned long GetPosition() const;                          // 00701F8E (slot 3)
    BOOL Open(const char* path, unsigned flags, CFileException* error);   // 00701CB0 (slot 8)
    CFile* Duplicate() const;                                   // 0070203B (slot 9: not supported)
    long Seek(long offset, unsigned from);                      // 00701F56 (slot 10)
    unsigned Read(void* buffer, unsigned count);                // 00701D84 (slot 13)
    void Write(const void* buffer, unsigned count);             // 00701DF0 (slot 14)
    void LockRange(unsigned long position, unsigned long count);    // 00702043 (slots 15 / 16: not supported)
    void Abort();                                               // 00702016 (slot 17)
    void Flush();                                               // 00701FB8 (slot 18)
    void Close();                                               // 00701FE0 (slot 19)
    void WriteString(const char* text);                         // 00701E23 (slot 21)
    BOOL ReadLine(CString& text);                               // 00701E93 (slot 22: ReadString(CString&))
    char* ReadString(char* buffer, unsigned size);              // 00701E4E (slot 23)
};
static_assert(sizeof(CStdioFile) == 0x14);

// CFileFind (0x1C bytes, vtable 00745EE8). The found entries are WIN32_FIND_DATA blocks from
// operator new: FindNextFile swaps the next one in.
struct CFileFind {
    void* vtable;
    WIN32_FIND_DATAA* found;    // +0x04 m_pFoundInfo
    WIN32_FIND_DATAA* next;     // +0x08 m_pNextInfo
    HANDLE context;             // +0x0C m_hContext (null or INVALID_HANDLE_VALUE when none)
    BOOL gotLast;               // +0x10 m_bGotLast
    CString root;               // +0x14 m_strRoot
    char separator;             // +0x18 m_chDirSeparator ('\\')

    static void* Vtable() { return ei::GamePointer<void>(0x00745EE8); }

    CFileFind* Construct();                                     // 006E6370
    void Destruct();                                            // 006E63AD
    CFileFind* DeletingDestructor(unsigned flags);              // 006E6391 (slot 1)
    void Close();                                               // 006E63E9

    CString GetFileName() const;                                // 006E684B (slot 3)
    CString GetFilePath() const;                                // 006E6722 (slot 4)
    CString GetFileTitle() const;                               // 006E67BA (slot 5)
    CString GetFileURL() const;                                 // 006E668E (slot 6)
    CString GetRoot() const;                                    // 006E6707 (slot 7)
    BOOL GetLastWriteTime(CTime* time) const;                   // 006E65C4 (slot 8)
    BOOL GetLastWriteFileTime(FILETIME* time) const;            // 006E6551 (slot 9)
    BOOL GetLastAccessTime(CTime* time) const;                  // 006E6599 (slot 10)
    BOOL GetLastAccessFileTime(FILETIME* time) const;           // 006E652D (slot 11)
    BOOL GetCreationTime(CTime* time) const;                    // 006E65EF (slot 12)
    BOOL GetCreationFileTime(FILETIME* time) const;             // 006E6575 (slot 13)
    BOOL MatchesMask(unsigned long mask) const;                 // 006E6513 (slot 14)
    BOOL IsDots() const;                                        // 006E661A (slot 15)
    BOOL FindFile(const char* pattern, unsigned long flags);    // 006E6433 (slot 16)
    BOOL FindNextFile();                                        // 006E6653 (slot 17)
    BOOL CloseContext();                                        // 006E6429 (slot 18: FindClose's result)
    unsigned long GetLength() const;                            // 006E68AA (the low dword of the size)
};
static_assert(sizeof(CFileFind) == 0x1C);

// AfxFullPath: `path` made absolute into `out` (_MAX_PATH bytes); the case of the last part as on
// disk on a file system that does not store Unicode, upper case where case is not preserved.
// FALSE when the path cannot be resolved (then copied as given) or the volume cannot be queried.
BOOL __stdcall FullPath(char* out, const char* path);          // 00701870
// AfxGetRoot: the root of `path` ("x:\" or "\\server\share\") into `root`.
void __stdcall GetRoot(const char* path, CString& root);        // 00701940

// AfxGetFileName: the part of `path` after its last backslash, '/' or ':' into `name` (at most `max` bytes,
// lstrcpyn); with a null `name` the size it needs (with the terminator), else 0.
unsigned __stdcall AfxGetFileName(const char* path, char* name, unsigned max);      // 0070D0D2
// AfxGetFileTitle: comdlg32's GetFileTitle (through the exe's import slot), AfxGetFileName's answer when
// that fails; with a null `title` the size it needs, else 0.
unsigned __stdcall AfxGetFileTitle(const char* path, char* title, unsigned max);    // 00701AC9

// The classes' CRuntimeClass records (GetRuntimeClass, slot 0).
const void* __fastcall FileRuntimeClass(const void* self);      // 0070BE58
const void* __fastcall StdioFileRuntimeClass(const void* self); // 0070C15B
const void* __fastcall FileFindRuntimeClass(const void* self);  // 0070BE5E

} // namespace mfc::impl
