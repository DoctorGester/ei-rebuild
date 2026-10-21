// The script loader's text file reader (004D5880; A-E2): a file read line by line through MFC's
// CStdioFile inside TRY / CATCH(CFileException) (lib/mfc_try.h). The file is opened by the
// constructor, which throws when it cannot; so does a failed read. Either ends with a result of 0
// and what was read so far in the text.
#include "script_parse.h"
#include "game_strings.h"

#include "console.h"
#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/mfc_try.h"

namespace scriptparse {

namespace {
constexpr unsigned kOpenFlags = 0x4020;   // CFile::typeText | CFile::shareDenyWrite, for reading
constexpr unsigned kLineSize = 0x800;     // longer lines arrive in pieces
// The empty text the result starts from (a zero byte of the exe).
const char* NoText() { return ""; }
} // namespace

int __fastcall ReadTextFile(const char* path, mfc::CString* text) {
    int result;
    mfc::ExceptionLink link;
    try {
        *text = NoText();
        result = 1;
        console::CStdioFile file(path, kOpenFlags);
        char line[kLineSize];
        while (file.ReadString(line, kLineSize)) *text = *text + mfc::CString(line);
        file.Close();
    } catch (CFileException* caught) {
        result = 0;
        link.exception = caught;
    }
    return result;
}
EI_REWRITTEN(0x004D5880, ReadTextFile);

} // namespace scriptparse
