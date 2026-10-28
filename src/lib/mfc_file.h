#pragma once

// MFC 4.2's CFile, CStdioFile and CFileFind and the path helpers they use; the tables are compiled in
// game.exe's slot order (mfc_file.cpp). See docs/notes/standalone.md, "The image's source files".

#include "mfc_exception.h"
#include "mfc_time.h"
#include "game/game_data.h"

#include <cstdio>
#include <string>

namespace mfc::impl {

// CFileStatus (0x118 bytes): what CFile::GetStatus fills in.
struct CFileStatus {
    CTime created;
    CTime modified;
    CTime accessed;
    long size;
    BYTE attribute;
    BYTE padding;
    char fullName[MAX_PATH];
};
static_assert(sizeof(CFileStatus) == 0x118);

// CFile. The virtual members are reached through the object's vtable where game.exe dispatches
// (mfc::CFileVirtuals); the bodies here are the slots'. The destructor is not virtual: the table is the
// `vtable` field, and the deleting destructor its slot 1.
struct CFile {
    enum OpenFlags : unsigned {
        modeRead = 0x0000, modeWrite = 0x0001, modeReadWrite = 0x0002, shareCompat = 0x0000,
        shareExclusive = 0x0010, shareDenyWrite = 0x0020, shareDenyRead = 0x0030, shareDenyNone = 0x0040,
        modeNoInherit = 0x0080, modeCreate = 0x1000, modeNoTruncate = 0x2000, typeText = 0x4000,
        typeBinary = 0x8000
    };
    static constexpr HANDLE kNoFile = INVALID_HANDLE_VALUE;   // CFile::hFileNull

    void* vtable;
    HANDLE handle = kNoFile;
    BOOL closeOnDelete = FALSE;
    std::string fileName;   // m_strFileName (the full path)

    static void* Vtable();

    CFile();
    explicit CFile(HANDLE file);                    // CFile(int hFile)
    CFile(const char* path, unsigned flags);        // (throws a CFileException*)
    // Closes the file when it owns the handle.
    ~CFile();
    CFile(const CFile&) = delete;
    CFile& operator=(const CFile&) = delete;
    CFile* DeletingDestructor(unsigned flags);

    unsigned long GetPosition() const;
    // Slots 4-6: the name part of GetStatus's path (AfxGetFileName), its title (AfxGetFileTitle) and the
    // whole path.
    std::string GetFileName() const;
    std::string GetFileTitle() const;
    std::string GetFilePath() const;
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

// CStdioFile: a CFile with a CRT stream over its handle.
struct CStdioFile : CFile {
    std::FILE* stream = nullptr;

    static void* Vtable();

    CStdioFile();
    CStdioFile(const char* path, unsigned flags);   // (throws a CFileException*)
    // Closes the stream when it owns the handle.
    ~CStdioFile();
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
    BOOL ReadLine(std::string& text);                           // (slot 22: ReadString(CString&), into a string)
    char* ReadString(char* buffer, unsigned size);
};

// CFileFind. The found entries are WIN32_FIND_DATA blocks from operator new: FindNextFile swaps the next
// one in. The destructor is not virtual (as CFile's).
struct CFileFind {
    void* vtable;
    WIN32_FIND_DATAA* found = nullptr;
    WIN32_FIND_DATAA* next = nullptr;
    HANDLE context = nullptr;   // m_hContext (null or INVALID_HANDLE_VALUE when none)
    BOOL gotLast = FALSE;
    std::string root;
    char separator = '\\';      // m_chDirSeparator

    static void* Vtable();

    CFileFind();
    ~CFileFind();
    CFileFind(const CFileFind&) = delete;
    CFileFind& operator=(const CFileFind&) = delete;
    CFileFind* DeletingDestructor(unsigned flags);
    void Close();

    std::string GetFileName() const;
    std::string GetFilePath() const;
    std::string GetFileTitle() const;
    std::string GetFileURL() const;
    std::string GetRoot() const;
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

// AfxFullPath: `path` made absolute into `out` (_MAX_PATH bytes); the case of the last part as on
// disk on a file system that does not store Unicode, upper case where case is not preserved.
// FALSE when the path cannot be resolved (then copied as given) or the volume cannot be queried.
BOOL FullPath(char* out, const char* path);

// AfxGetFileName: the part of `path` after its last backslash, '/' or ':' into `name` (at most `max` bytes,
// lstrcpyn); with a null `name` the size it needs (with the terminator), else 0.
unsigned AfxGetFileName(const char* path, char* name, unsigned max);
// AfxGetFileTitle: comdlg32's GetFileTitle (through the exe's import slot), AfxGetFileName's answer when
// that fails; with a null `title` the size it needs, else 0.
unsigned AfxGetFileTitle(const char* path, char* title, unsigned max);

} // namespace mfc::impl
