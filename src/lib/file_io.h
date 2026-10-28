#pragma once

// Files through the game's import slots (CreateFileA, FindFirstFileA, GetFileAttributesA, ...): a mod's hook in the
// import table and the harness's save / install redirection apply to every file the game opens or lists.
//   - File: a binary file on a Win32 handle (the game's CFile uses);
//   - TextFile: a text file read and written by lines on a host CRT stream (its CStdioFile uses);
//   - FileFind: a folder search (its CFileFind uses);
//   - OpenFile / FileExists: fopen and _access.
// A read, write or seek that fails throws a FileError, where MFC threw a CFileException: what the game caught it
// catches; an uncaught one ends the game as before (a write to a full disk).

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

struct _WIN32_FIND_DATAA;

namespace lib {

// A failed file operation: the Win32 (or CRT) error code and the file's path, both in what().
class FileError : public std::runtime_error {
public:
    FileError(unsigned long osError, std::string_view path);
    unsigned long osError;
};

// A binary file. Open's flags are MFC's CFile open flags, which the callers pass on (the save code's and the chunk
// readers' numbers): access in bits 0-1, sharing in bits 4-6, create (truncating unless noTruncate); the type bits
// are ignored. Closed by the destructor.
class File {
public:
    enum Flags : unsigned {
        modeRead = 0x0000, modeWrite = 0x0001, modeReadWrite = 0x0002, shareExclusive = 0x0010, shareDenyWrite = 0x0020,
        shareDenyRead = 0x0030, shareDenyNone = 0x0040, modeCreate = 0x1000, modeNoTruncate = 0x2000,
        typeText = 0x4000, typeBinary = 0x8000
    };

    File() = default;
    // Opens `path`; a FileError when it cannot.
    File(std::string_view path, unsigned flags);
    ~File();
    File(const File&) = delete;
    File& operator=(const File&) = delete;

    // False when CreateFileA fails. A file still open is closed first.
    bool Open(std::string_view path, unsigned flags);
    void Close();
    bool IsOpen() const { return handle_ != nullptr; }
    // The Win32 handle (null when closed).
    void* Handle() const { return handle_; }

    // The bytes read (fewer at the end of the file).
    unsigned Read(void* buffer, unsigned count);
    // A FileError also when fewer bytes were written.
    void Write(const void* buffer, unsigned count);
    // `from` FILE_BEGIN, FILE_CURRENT or FILE_END; the new position.
    long Seek(long offset, unsigned from);
    unsigned long GetPosition() const;
    unsigned long GetLength() const;

private:
    void* handle_ = nullptr;
    std::string path_;   // as opened (for FileError)
};

// A text file: lines with "\r\n" on disk, "\n" in memory. Closed by the destructor.
class TextFile {
public:
    TextFile() = default;
    ~TextFile();
    TextFile(const TextFile&) = delete;
    TextFile& operator=(const TextFile&) = delete;

    // `mode` "r" (reading) or "w" (created or truncated): OpenFile's text modes.
    bool Open(std::string_view path, const char* mode);
    void Close();

    // The next line without its '\n' into `line`; false at the end of the file (also when the last piece read
    // nothing after a line of exactly 128 characters, as MFC's ReadString(CString&)). The line ends at a zero byte
    // of each 128-character piece.
    bool ReadLine(std::string& line);
    // fgets: a line with its '\n' into `buffer` (at most size - 1 characters); null at the end of the file.
    char* ReadString(char* buffer, unsigned size);
    void WriteString(std::string_view text);

private:
    std::FILE* stream_ = nullptr;
    std::string path_;
};

// A folder search as MFC's CFileFind: FindFile starts it, then each FindNext moves to the next entry and returns
// false when that entry is the last one (it is still there to read). The paths are the search pattern's folder made
// absolute, a separator and the entry's name.
class FileFind {
public:
    FileFind();
    ~FileFind();
    FileFind(const FileFind&) = delete;
    FileFind& operator=(const FileFind&) = delete;

    // Whether `pattern` matches anything (no entry is current yet).
    bool FindFile(std::string_view pattern);
    bool FindNext();
    void Close();

    std::string GetFileName() const;
    std::string GetFilePath() const;
    // The name without its extension.
    std::string GetTitle() const;
    // The low dword of the size.
    unsigned long GetLength() const;
    // Whether the entry has one of the attribute bits `mask`.
    bool MatchesMask(unsigned long mask) const;
    // "." or "..".
    bool IsDots() const;

private:
    void* context_ = nullptr;                    // the search's handle (null when none)
    std::unique_ptr<_WIN32_FIND_DATAA> next_;    // the entry read ahead
    // The current entry.
    bool hasFound_ = false;
    std::string name_;
    unsigned long attributes_ = 0;
    unsigned long size_ = 0;
    std::string root_;
};

// fopen: `mode` is r, w or a, then optionally + and b or t (text when neither). The file is opened by
// CreateFileA through its slot, shared for reading and writing, then becomes a host stream; null when
// either fails.
std::FILE* OpenFile(const std::string& path, const char* mode);

// Whether `path` names a file or folder (GetFileAttributesA through its slot; the original's _access
// with mode 0 or 4).
bool FileExists(const std::string& path);

} // namespace lib
