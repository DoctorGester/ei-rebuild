// The image's UTF-8 file names, typing and text checks. See harness_utf8.h. The checks are the image's
// (image/harness_utf8_image.cpp); game.exe has none.
#include "harness_utf8.h"

#include "image/image_side.h"

#include "ei/hook.h"

namespace ei::verify::utf8files {

std::string Check(const char* savesDir) {
    if (!ei::Standalone()) return "utf8files: needs the standalone image";
    return image::utf8files::Check(savesDir);
}

} // namespace ei::verify::utf8files

namespace ei::verify::utf8typing {

std::string Type(HWND window, const std::string& typed) {
    if (!ei::Standalone()) return "typeunicode: needs the standalone image";
    return image::utf8typing::Type(window, typed);
}

std::string CheckConsoleLine(const std::string& argument) {
    if (!ei::Standalone()) return "consoleline: needs the standalone image";
    return image::utf8typing::CheckConsoleLine(argument);
}

} // namespace ei::verify::utf8typing

namespace ei::verify::utf8text {

std::string Check() {
    if (!ei::Standalone()) return "utf8text: needs the standalone image";
    return image::utf8text::Check();
}

std::string CheckChatWrap(const std::string& text) {
    if (!ei::Standalone()) return "chatwrap: needs the standalone image";
    return image::utf8text::CheckChatWrap(text);
}

} // namespace ei::verify::utf8text
