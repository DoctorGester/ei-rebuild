// The UTF-8 build's file names through the game's file calls. See harness_utf8.h.
#include "harness_utf8.h"

#include "../../src/game/system_text.h"
#include "../../src/hook/log.h"
#include "lib/gameapi.h"

#include <windows.h>

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
