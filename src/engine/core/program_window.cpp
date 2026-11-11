// The program's window and title, its message boxes, and lib/crt.h's fatal errors and warnings. See program_window.h.
#include "engine/core/program_window.h"

#include "lib/crt.h"
#include "platform/log.h"
#include "platform/platform.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

char g_windowTitle[256] = /*0079BB24*/ {""};
platform::NativeWindow g_mainWindow = /*0079BC28*/ {};

void ShowErrorBox(std::string_view text) {
    platform::CursorShape cursor = platform::CurrentCursor();
    platform::ShowMessageBox(g_mainWindow, text, std::string_view(g_windowTitle), platform::MessageButtons::Ok,
                             platform::MessageIcon::Error);
    platform::SetCursor(cursor);
}

void ShowLibraryMessageBox(std::string_view text) {
    platform::ShowMessageBox(nullptr, text, platform::ProgramPath(), platform::MessageButtons::Ok,
                             platform::MessageIcon::Warning);
}

void game::FatalExit(const char* format, ...) {
    char text[0x400];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    ei::Log("fatal: %s", text);
    ShowErrorBox(text);
    std::exit(-1);
}

void game::Warning(const char* format, ...) {
    char text[0x400];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    ei::Log("warning: %s", text);
}
