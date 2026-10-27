// The UTF-8 build's file names through the game's file calls, and its typing. See harness_utf8.h. The checks are the
// image's (image/harness_utf8_image.cpp); the build without UTF-8 skips them.
#include "harness_utf8.h"

#include "image/image_side.h"

#include "../../src/hook/log.h"
#include "ei/hook.h"

namespace ei::verify::utf8files {

std::string Check(const char* savesDir) {
    if constexpr (EI_UTF8 == 0) {
        (void)savesDir;
        return {};
    } else {
        return image::utf8files::Check(savesDir);
    }
}

} // namespace ei::verify::utf8files

namespace ei::verify::utf8typing {

std::string Type(HWND window, const std::string& typed) {
    if constexpr (EI_UTF8 == 0) {
        (void)window;
        (void)typed;
        Log("harness: typeunicode skipped (the UTF-8 build's)");
        return {};
    } else {
        return image::utf8typing::Type(window, typed);
    }
}

std::string CheckConsoleLine(const std::string& argument) {
    if constexpr (EI_UTF8 == 0) {
        (void)argument;
        Log("harness: consoleline skipped (the UTF-8 build's)");
        return {};
    } else {
        if (!ei::Standalone()) return "consoleline: needs the standalone image";
        return image::utf8typing::CheckConsoleLine(argument);
    }
}

} // namespace ei::verify::utf8typing

namespace ei::verify::utf8text {

std::string Check() {
    if constexpr (EI_UTF8 == 0) {
        Log("harness: utf8text skipped (the UTF-8 build's)");
        return {};
    } else {
        if (!ei::Standalone()) return "utf8text: needs the standalone image";
        return image::utf8text::Check();
    }
}

std::string CheckChatWrap(const std::string& text) {
    if constexpr (EI_UTF8 == 0) {
        (void)text;
        Log("harness: chatwrap skipped (the UTF-8 build's)");
        return {};
    } else {
        if (!ei::Standalone()) return "chatwrap: needs the standalone image";
        return image::utf8text::CheckChatWrap(text);
    }
}

} // namespace ei::verify::utf8text
