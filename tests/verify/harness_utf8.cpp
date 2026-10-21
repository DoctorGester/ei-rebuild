// The UTF-8 build's file names through the game's file calls, and its typing. See harness_utf8.h.
#include "harness_utf8.h"

#include "../../src/game/console.h"
#include "../../src/game/system_text.h"
#include "../../src/game/ui.h"
#include "../../src/hook/log.h"
#include "harness_objects.h"
#include "lib/gameapi.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ei::verify::utf8files {

namespace {

// Under the game's saves folder ("\saves\": harness.cpp's SavesPath takes it to the run's harness_saves).
constexpr char kFolder[] = ".\\saves\\utf8 Проверка";
constexpr char kFile[] = ".\\saves\\utf8 Проверка\\файл.sav";
constexpr char kPattern[] = ".\\saves\\utf8 Проверка\\*.sav";
constexpr char kName[] = "файл.sav";
constexpr wchar_t kWideFile[] = L"\\utf8 Проверка\\файл.sav";
constexpr wchar_t kWideFolder[] = L"\\utf8 Проверка";
constexpr char kContents[] = "U2: Проверка";

std::string RoundTrip(const char* savesDir) {
    if (!gameapi::CreateDirectoryA(kFolder, nullptr) && gameapi::GetLastError() != ERROR_ALREADY_EXISTS)
        return "the folder was not made";
    HANDLE file = gameapi::CreateFileA(kFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return "the file was not made";
    DWORD written = 0;
    const BOOL wrote = gameapi::WriteFile(file, kContents, sizeof(kContents) - 1, &written, nullptr);
    gameapi::CloseHandle(file);
    if (!wrote || written != sizeof(kContents) - 1) return "the file was not written";

    WIN32_FIND_DATAA found{};
    HANDLE find = gameapi::FindFirstFileA(kPattern, &found);
    if (find == INVALID_HANDLE_VALUE) return "the file was not listed";
    gameapi::FindClose(find);
    if (std::strcmp(found.cFileName, kName) != 0) return std::string("the file was listed as ") + found.cFileName;

    file = gameapi::CreateFileA(kFile, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return "the file was not opened again";
    char contents[64] = {};
    DWORD read = 0;
    gameapi::ReadFile(file, contents, sizeof(contents) - 1, &read, nullptr);
    gameapi::CloseHandle(file);
    if (read != sizeof(kContents) - 1 || std::memcmp(contents, kContents, read) != 0) return "the file read back differs";

    // The names on disk, as UTF-16, in the run's saves folder.
    const std::wstring saves = text::Wide(savesDir);
    if (GetFileAttributesW((saves + kWideFile).c_str()) == INVALID_FILE_ATTRIBUTES)
        return "the file is not on disk under its UTF-16 name";
    if (!gameapi::DeleteFileA(kFile)) return "the file was not deleted";
    if (!gameapi::RemoveDirectoryA(kFolder)) return "the folder was not removed";
    if (GetFileAttributesW((saves + kWideFolder).c_str()) != INVALID_FILE_ATTRIBUTES) return "the folder is still on disk";
    return {};
}

} // namespace

std::string Check(const char* savesDir) {
    if constexpr (!text::kUtf8) {
        (void)savesDir;
        return {};
    } else {
        std::string failure = RoundTrip(savesDir);
        if (failure.empty()) Log("harness: utf8 file names: a Cyrillic folder and file made, listed, read and removed");
        else failure = "utf8 file names: " + failure;
        return failure;
    }
}

} // namespace ei::verify::utf8files

namespace ei::verify::utf8typing {

std::string Type(HWND window, const std::string& typed) {
    if constexpr (!text::kUtf8) {
        (void)window;
        Log("harness: typeunicode skipped (the UTF-8 build's)");
        return {};
    } else {
        if (!window) return "typeunicode: no game window yet";
        const std::wstring wide = text::Wide(typed);
        for (wchar_t unit : wide) {
            if (!PostMessageW(window, WM_CHAR, static_cast<WPARAM>(unit), 1))
                return "typeunicode: WM_CHAR was not posted";
        }
        Log("harness: typeunicode: %u UTF-16 units posted as WM_CHAR", static_cast<unsigned>(wide.size()));
        return {};
    }
}

std::string CheckConsoleLine(const std::string& argument) {
    if constexpr (!text::kUtf8) {
        (void)argument;
        Log("harness: consoleline skipped (the UTF-8 build's)");
        return {};
    } else {
        char* rest = nullptr;
        const long characters = std::strtol(argument.c_str(), &rest, 10);
        if (rest == argument.c_str() || characters < 0) return "consoleline needs <characters> <text>";
        const std::string expected = *rest == ' ' ? std::string(rest + 1) : std::string(rest);
        // The caret's byte offset: the bytes of the first <characters> characters (continuation bytes 10xxxxxx
        // belong to the character before).
        std::size_t caret = 0;
        for (long n = 0; n < characters && caret < expected.size(); ++n) {
            ++caret;
            while (caret < expected.size() && (static_cast<unsigned char>(expected[caret]) & 0xC0) == 0x80) ++caret;
        }
        const console::ConsoleWindow* console = HarnessInterface()->console_;
        if (!console) return "consoleline: no console window";
        const mfc::CString& input = console->input_;
        const std::string line(static_cast<const char*>(input), static_cast<std::size_t>(input.GetLength()));
        if (line != expected || console->caret_ != static_cast<int>(caret)) {
            char failure[512];
            std::snprintf(failure, sizeof(failure), "consoleline: the line is \"%s\" (%d bytes), caret at byte %d; expected \"%s\" (%u bytes), caret at byte %u",
                          line.c_str(), input.GetLength(), console->caret_, expected.c_str(), static_cast<unsigned>(expected.size()),
                          static_cast<unsigned>(caret));
            return failure;
        }
        Log("harness: consoleline: \"%s\" in UTF-8 (%u bytes, %d characters), caret at character %ld (byte %u)",
            line.c_str(), static_cast<unsigned>(line.size()), text::CharacterCount(line), characters,
            static_cast<unsigned>(caret));
        return {};
    }
}

} // namespace ei::verify::utf8typing
