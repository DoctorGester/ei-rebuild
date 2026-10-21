// MFC 4.2's resource strings and their formatting, registered at their library addresses
// (mfc_resource.h).
#include "mfc_resource.h"

#include "ei/hook.h"
#include "gameapi.h"
#include "mfc_state.h"
#include "game/game_data.h"

namespace mfc::impl {

namespace {

// The game CRT's multibyte flags (_mbctype + 1); _istlead is the lead-byte bit.
const unsigned char* MultibyteFlags() { return ei::GamePointer<const unsigned char>(0x007CC181); }
constexpr unsigned char kMbLead = 0x4;   // _M1
bool IsLeadByte(char c) { return (MultibyteFlags()[static_cast<unsigned char>(c)] & kMbLead) != 0; }

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

int __stdcall AfxLoadString(unsigned id, char* buffer, unsigned size) {
    const int length = gameapi::LoadStringA(AfxGetModuleState()->resources, id, buffer, static_cast<int>(size));
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

} // namespace mfc::impl
