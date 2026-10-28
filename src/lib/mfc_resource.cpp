// MFC 4.2's resource strings and their formatting (mfc_resource.h).
#include "mfc_resource.h"

#include "game/game_data.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace mfc::impl {

namespace {

constexpr int kFormatLength = 256;

// The strings of MFC's table that our code can reach (game.exe's .rsrc held the whole table, which the host no
// longer has), loaded by CString::LoadResourceString, which CException::ReportError and the simple exceptions'
// GetErrorMessage (mfc_exception.cpp) call: 0xF020 ReportError's "no error" fallback; 0xF022-0xF024 the three static
// exceptions' messages (resource, memory, user: CSimpleException::InitString). Every other id (the file and archive
// exceptions' causes, the file dialogs' captions, the validation prompts, the property and mail texts) is loaded by
// nothing, so it is not here and loads as the empty string.
struct StringEntry {
    unsigned id;
    const char* text;
};
constexpr StringEntry kStrings[] = {
    {0xF020, "No error message is available."},
    {0xF022, "A required resource was unavailable."},
    {0xF023, "Out of memory."},
    {0xF024, "An unknown error has occurred."},
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


} // namespace mfc::impl
