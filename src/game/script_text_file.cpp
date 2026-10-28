// The script loader's text file reader: a file read in lines of at most 2 KB into one text. A file that does not
// open or a failed read (the game catches the file error) ends with a result of 0 and what was read so far in the
// text.
#include "script_parse.h"

#include "lib/file_io.h"

#include <string>

namespace scriptparse {

namespace {
constexpr unsigned kLineSize = 0x800;     // longer lines arrive in pieces
} // namespace

int ReadTextFile(const std::string& path, std::string* text) {
    text->clear();
    lib::TextFile file;
    if (!file.Open(path, "r")) return 0;
    char line[kLineSize];
    try {
        while (file.ReadString(line, kLineSize)) *text += line;
    } catch (const lib::FileError&) {
        return 0;
    }
    return 1;
}

} // namespace scriptparse
