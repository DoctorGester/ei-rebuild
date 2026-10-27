// MFC 4.2's resource strings and their formatting, registered at their library addresses (mfc_resource.h).
#include "mfc_resource.h"
#include "crt_locale_data.h"

#include "ei/hook.h"
#include "gameapi.h"
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

int __stdcall AfxLoadString(unsigned id, char* buffer, unsigned size) {
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

void __stdcall AfxFormatStrings(CString& out, const char* format, const char* const* strings, int count) {
    int total = 0;
    for (const char* at = format; *at != '\0';) {
        int index = 0;
        if (Reference(at, index)) {
            at += 2;
            if (index >= count)
                ++total;
            else if (strings[index])
                total += gameapi::lstrlenA(strings[index]);
        } else {
            if (IsLeadByte(*at)) {
                ++total;
                ++at;
            }
            ++at;
            ++total;
        }
    }

    char* to = out.GetBuffer(total);
    for (const char* at = format; *at != '\0';) {
        int index = 0;
        if (Reference(at, index)) {
            at += 2;
            if (index >= count) {
                *to++ = '?';
            } else if (strings[index]) {
                gameapi::lstrcpyA(to, strings[index]);
                to += gameapi::lstrlenA(to);
            }
        } else {
            if (IsLeadByte(*at)) *to++ = *at++;
            *to++ = *at++;
        }
    }
    out.ReleaseBuffer(static_cast<int>(to - static_cast<const char*>(out)));
}

void __stdcall AfxFormatStringsId(CString& out, unsigned formatId, const char* const* strings, int count) {
    char format[kFormatLength];
    if (AfxLoadString(formatId, format, kFormatLength) != 0) AfxFormatStrings(out, format, strings, count);
}

void __stdcall AfxFormatString1(CString& out, unsigned formatId, const char* string) {
    const char* strings[1] = {string};
    AfxFormatStringsId(out, formatId, strings, 1);
}

EI_REWRITTEN(0x00702B92, AfxLoadString);
EI_REWRITTEN(0x0070B1AD, AfxFormatStrings);
EI_REWRITTEN(0x0070B173, AfxFormatStringsId);
EI_REWRITTEN(0x0070B2AC, AfxFormatString1);
} // namespace mfc::impl
