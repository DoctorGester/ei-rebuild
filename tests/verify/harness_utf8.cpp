// The image's UTF-8 file names, typing and text checks. See harness_utf8.h. The checks are the image's
// (image/harness_utf8_image.cpp).
#include "harness_utf8.h"

#include "image/image_side.h"

namespace ei::verify::utf8files {

std::string Check(const char* savesDir) {
    return image::utf8files::Check(savesDir);
}

} // namespace ei::verify::utf8files

namespace ei::verify::utf8typing {

std::string Type(HWND window, const std::string& typed) {
    return image::utf8typing::Type(window, typed);
}

std::string CheckConsoleLine(const std::string& argument) {
    return image::utf8typing::CheckConsoleLine(argument);
}

} // namespace ei::verify::utf8typing

namespace ei::verify::utf8text {

std::string Check() {
    return image::utf8text::Check();
}

std::string CheckChatWrap(const std::string& text) {
    return image::utf8text::CheckChatWrap(text);
}

} // namespace ei::verify::utf8text
