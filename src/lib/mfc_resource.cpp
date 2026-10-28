// MFC 4.2's resource strings and their formatting (mfc_resource.h).
#include "mfc_resource.h"
#include "crt_locale_data.h"

#include "game/game_data.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace mfc::impl {

namespace {

// _istlead: the lead-byte bit of the CRT's multibyte flags (crt_locale_data.h).
constexpr unsigned char kMbLead = 0x4;   // _M1
bool IsLeadByte(char c) { return (libcrt::MultibyteFlags()[static_cast<unsigned char>(c)] & kMbLead) != 0; }

// A "%1".."%9" or "%A".."%Z" at `at`: its index into the strings ('1' is 0, 'A' is 9; '0' is -1).
bool Reference(const char* at, int& index) {
    if (at[0] != '%') return false;
    const char next = at[1];
    if (next >= '0' && next <= '9') {
        index = next - '1';
        return true;
    }
    if (next >= 'A' && next <= 'Z') {
        index = next - 'A' + 9;
        return true;
    }
    return false;
}

constexpr int kFormatLength = 256;

} // namespace

namespace {

// The strings of MFC's table that our code can reach (game.exe's .rsrc held the whole table, which the host no
// longer has). They are loaded by CString::LoadResourceString and AfxFormatString1, which CException::ReportError
// and the exceptions' GetErrorMessage (mfc_exception.cpp) call:
//   0xF006 the unnamed file's name; 0xF020 ReportError's "no error" fallback;
//   0xF021-0xF024 the four static exceptions' messages (not supported, resource, memory, user:
//                 CSimpleException::InitString);
//   0xF1A0-0xF1AE a CFileException's cause (none ... endOfFile: OsErrorToException can answer each);
//   0xF1B3 a CArchiveException's endOfFile (the only cause ThrowArchiveException is called with).
// Every other id (the file dialogs' captions, the validation prompts, the property and mail texts, the other archive
// causes) is loaded by nothing, so it is not here and loads as the empty string.
struct StringEntry {
    unsigned id;
    const char* text;
};
constexpr StringEntry kStrings[] = {
    {0xF006, "an unnamed file"},
    {0xF020, "No error message is available."},
    {0xF021, "An unsupported operation was attempted."},
    {0xF022, "A required resource was unavailable."},
    {0xF023, "Out of memory."},
    {0xF024, "An unknown error has occurred."},
    {0xF1A0, "No error occurred."},
    {0xF1A1, "An unknown error occurred while accessing %1."},
    {0xF1A2, "%1 was not found."},
    {0xF1A3, "%1 contains an invalid path."},
    {0xF1A4, "%1 could not be opened because there are too many open files."},
    {0xF1A5, "Access to %1 was denied."},
    {0xF1A6, "An invalid file handle was associated with %1."},
    {0xF1A7, "%1 could not be removed because it is the current directory."},
    {0xF1A8, "%1 could not be created because the directory is full."},
    {0xF1A9, "Seek failed on %1"},
    {0xF1AA, "A hardware I/O error was reported while accessing %1."},
    {0xF1AB, "A sharing violation occurred while accessing %1."},
    {0xF1AC, "A locking violation occurred while accessing %1."},
    {0xF1AD, "Disk full while accessing %1."},
    {0xF1AE, "An attempt was made to access %1 past its end."},
    {0xF1B3, "An attempt was made to access %1 past its end."},
};

} // namespace

int AfxLoadString(unsigned id, char* buffer, unsigned size) {
    int length = 0;
    if (size != 0) {
        for (const StringEntry& entry : kStrings) {
            if (entry.id != id) continue;
            length = static_cast<int>(std::min<std::size_t>(std::strlen(entry.text), size - 1));
            std::memcpy(buffer, entry.text, static_cast<std::size_t>(length));
            buffer[length] = '\0';
            break;
        }
    }
    if (length == 0) buffer[0] = '\0';
    return length;
}

// The table's strings are short: one fixed buffer holds any of them.
std::string LoadResourceString(unsigned id) {
    char text[kFormatLength];
    AfxLoadString(id, text, kFormatLength);
    return text;
}

std::string AfxFormatString1(unsigned formatId, const char* string) {
    char format[kFormatLength];
    std::string out;
    if (AfxLoadString(formatId, format, kFormatLength) == 0) return out;
    for (const char* at = format; *at != '\0';) {
        int index = 0;
        if (Reference(at, index)) {
            at += 2;
            if (index != 0) out += '?';
            else if (string) out += string;
        } else {
            if (IsLeadByte(*at) && at[1] != '\0') out += *at++;
            out += *at++;
        }
    }
    return out;
}

} // namespace mfc::impl
