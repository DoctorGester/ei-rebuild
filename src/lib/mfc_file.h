#pragma once

// MFC 4.2's CFile, CStdioFile and CFileFind (filecore.cpp, filetxt.cpp, filefind.cpp) and the path
// helpers they use, written out for the standalone exe (docs/notes/standalone.md
// section 2). Same layout as the copy linked into game.exe; the tables are compiled (mfc_file.cpp's
// FileTable, StdioFileTable and FileFindTable) in place, with the
// slots in game.exe's order. Every body is registered at its library address (mfc_file.cpp).
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

// CFile (0x10 bytes). The virtual members are reached through the object's
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

    static void* Vtable();

    CFile* Construct();
    CFile* ConstructHandle(HANDLE file);                        // CFile(int hFile)
    CFile* ConstructOpen(const char* path, unsigned flags);     // (throws a CFileException*)
    void Destruct();
    CFile* DeletingDestructor(unsigned flags);

    unsigned long GetPosition() const;
    // Slots 4-6: the name part of GetStatus's path (AfxGetFileName), its title (AfxGetFileTitle) and the
    // whole path.
    CString GetFileName() const;
    CString GetFileTitle() const;
    CString GetFilePath() const;
    // The cleared status with the file's name; with an open file also its times
    // (a missing creation or access time is the modification time), size and attributes. FALSE when
    // the handle's times or size cannot be read.
    BOOL GetStatus(CFileStatus& status) const;
    void SetFilePath(const char* path);
    BOOL Open(const char* path, unsigned flags, CFileException* error);
    CFile* Duplicate() const;
    long Seek(long offset, unsigned from);
    void SetLength(unsigned long length);
    unsigned long GetLength() const;
    unsigned Read(void* buffer, unsigned count);
    void Write(const void* buffer, unsigned count);
    void LockRange(unsigned long position, unsigned long count);
    void UnlockRange(unsigned long position, unsigned long count);
    void Abort();
    void Flush();
    void Close();
    unsigned GetBufferPtr(unsigned command, unsigned count, void** start, void** end);   // (slot 20: 0)
};
static_assert(sizeof(CFile) == 0x10);

// CStdioFile (0x14 bytes): a CFile with a CRT stream over its handle.
struct CStdioFile : CFile {
    gamecrt::CrtFile* stream;   // +0x10 m_pStream

    static void* Vtable();

    CStdioFile* Construct();
    CStdioFile* ConstructOpen(const char* path, unsigned flags);    // (throws a CFileException*)
    void Destruct();
    CStdioFile* DeletingDestructor(unsigned flags);

    unsigned long GetPosition() const;
    BOOL Open(const char* path, unsigned flags, CFileException* error);
    CFile* Duplicate() const;                                   // (slot 9: not supported)
    long Seek(long offset, unsigned from);
    unsigned Read(void* buffer, unsigned count);
    void Write(const void* buffer, unsigned count);
    void LockRange(unsigned long position, unsigned long count);    // (slots 15 / 16: not supported)
    void Abort();
    void Flush();
    void Close();
    void WriteString(const char* text);
    BOOL ReadLine(CString& text);                               // (slot 22: ReadString(CString&))
    char* ReadString(char* buffer, unsigned size);
};
static_assert(sizeof(CStdioFile) == 0x14);

// CFileFind (0x1C bytes). The found entries are WIN32_FIND_DATA blocks from
// operator new: FindNextFile swaps the next one in.
struct CFileFind {
    void* vtable;
    WIN32_FIND_DATAA* found;    // +0x04 m_pFoundInfo
    WIN32_FIND_DATAA* next;     // +0x08 m_pNextInfo
    HANDLE context;             // +0x0C m_hContext (null or INVALID_HANDLE_VALUE when none)
    BOOL gotLast;               // +0x10 m_bGotLast
    CString root;               // +0x14 m_strRoot
    char separator;             // +0x18 m_chDirSeparator ('\\')

    static void* Vtable();

    CFileFind* Construct();
    void Destruct();
    CFileFind* DeletingDestructor(unsigned flags);
    void Close();

    CString GetFileName() const;
    CString GetFilePath() const;
    CString GetFileTitle() const;
    CString GetFileURL() const;
    CString GetRoot() const;
    BOOL GetLastWriteTime(CTime* time) const;
    BOOL GetLastWriteFileTime(FILETIME* time) const;
    BOOL GetLastAccessTime(CTime* time) const;
    BOOL GetLastAccessFileTime(FILETIME* time) const;
    BOOL GetCreationTime(CTime* time) const;
    BOOL GetCreationFileTime(FILETIME* time) const;
    BOOL MatchesMask(unsigned long mask) const;
    BOOL IsDots() const;
    BOOL FindFile(const char* pattern, unsigned long flags);
    BOOL FindNextFile();
    BOOL CloseContext();                                        // (slot 18: FindClose's result)
    unsigned long GetLength() const;                            // (the low dword of the size)
};
static_assert(sizeof(CFileFind) == 0x1C);

// AfxFullPath: `path` made absolute into `out` (_MAX_PATH bytes); the case of the last part as on
// disk on a file system that does not store Unicode, upper case where case is not preserved.
// FALSE when the path cannot be resolved (then copied as given) or the volume cannot be queried.
BOOL __stdcall FullPath(char* out, const char* path);
// AfxGetRoot: the root of `path` ("x:\" or "\\server\share\") into `root`.
void __stdcall GetRoot(const char* path, CString& root);

// AfxGetFileName: the part of `path` after its last backslash, '/' or ':' into `name` (at most `max` bytes,
// lstrcpyn); with a null `name` the size it needs (with the terminator), else 0.
unsigned __stdcall AfxGetFileName(const char* path, char* name, unsigned max);
// AfxGetFileTitle: comdlg32's GetFileTitle (through the exe's import slot), AfxGetFileName's answer when
// that fails; with a null `title` the size it needs, else 0.
unsigned __stdcall AfxGetFileTitle(const char* path, char* title, unsigned max);

// The classes' CRuntimeClass records (GetRuntimeClass).
const void* __fastcall FileRuntimeClass(const void* self);
const void* __fastcall StdioFileRuntimeClass(const void* self);
const void* __fastcall FileFindRuntimeClass(const void* self);

} // namespace mfc::impl
