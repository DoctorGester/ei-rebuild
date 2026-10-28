// The image's UTF-8 file names through the game's file calls, its typing and its text (image_side.h;
// harness_utf8.cpp, harness_utf8.h).
#include "image_side.h"

#include "../../../src/hook/log.h"

#include "ei/hook.h"
#include "game/console.h"
#include "game/game_singletons.h"
#include "game/player_messages.h"
#include "game/system_text.h"
#include "game/ui.h"
#include "lib/gameapi.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace ei::verify::image::utf8files {

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
    std::string failure = RoundTrip(savesDir);
    if (failure.empty()) Log("harness: utf8 file names: a Cyrillic folder and file made, listed, read and removed");
    else failure = "utf8 file names: " + failure;
    return failure;
}

} // namespace ei::verify::image::utf8files

namespace ei::verify::image::utf8typing {

std::string Type(HWND window, const std::string& typed) {
    if (!window) return "typeunicode: no game window yet";
    const std::wstring wide = text::Wide(typed);
    for (wchar_t unit : wide) {
        if (!PostMessageW(window, WM_CHAR, static_cast<WPARAM>(unit), 1))
            return "typeunicode: WM_CHAR was not posted";
    }
    Log("harness: typeunicode: %u UTF-16 units posted as WM_CHAR", static_cast<unsigned>(wide.size()));
    return {};
}

std::string CheckConsoleLine(const std::string& argument) {
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
    const console::ConsoleWindow* console = TheInterface()->console_;
    if (!console) return "consoleline: no console window";
    const std::string& line = console->input_;
    if (line != expected || console->caret_ != static_cast<int>(caret)) {
        char failure[512];
        std::snprintf(failure, sizeof(failure), "consoleline: the line is \"%s\" (%d bytes), caret at byte %d; expected \"%s\" (%u bytes), caret at byte %u",
                      line.c_str(), static_cast<int>(line.size()), console->caret_, expected.c_str(), static_cast<unsigned>(expected.size()),
                      static_cast<unsigned>(caret));
        return failure;
    }
    Log("harness: consoleline: \"%s\" in UTF-8 (%u bytes, %d characters), caret at character %ld (byte %u)",
        line.c_str(), static_cast<unsigned>(line.size()), text::CharacterCount(line), characters,
        static_cast<unsigned>(caret));
    return {};
}

} // namespace ei::verify::image::utf8typing

namespace ei::verify::image::utf8text {

namespace {

bool ValidUtf8(const std::string& text) {
    if (text.empty()) return true;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0;
}

// The first failed expectation of the case and class checks, or empty.
std::string CaseFailure() {
    struct Pair {
        const char* a;
        const char* b;
        int sign;   // the compare's expected sign
    };
    // "Р" (D0 A0) and "Ѡ" (D1 A0) share a trail: lower-cased, D1 80 and D1 A1.
    const Pair compares[] = {
        {"Жук и ЁЛКА", "жУК И ёлка", 0}, {"Привет, Мир", "ПРИВЕТ, мир", 0}, {"Р", "Ѡ", -1},
        {"абв", "АБГ", -1},             {"Ґанок", "ґАНОК", 0},           {"Straße É", "STRAßE é", 0},
    };
    for (const Pair& p : compares) {
        const int got = text::CompareIgnoringCase(p.a, p.b);
        if ((got > 0) - (got < 0) != p.sign)
            return std::string("CompareIgnoringCase(\"") + p.a + "\", \"" + p.b + "\") is " + std::to_string(got) +
                   ", expected the sign " + std::to_string(p.sign);
    }
    if (text::CompareIgnoringCase("Привет мир", "ПРИВЕТ МИР", 12) != 0) return "12 bytes of \"Привет\" compare different";
    if (text::CompareIgnoringCase("ЖУК", "жук", 6, false) != 0) return "6 bytes of \"ЖУК\" and \"жук\" compare different";
    std::string lower = "ПРИВЕТ Ёж Ґ ABC";
    str::MakeLower(lower);
    if (lower != "привет ёж ґ abc") return "str::MakeLower gave \"" + lower + "\"";
    char upper[] = "ёжик abc";
    text::utf8::CharUpperA(upper);
    if (std::strcmp(upper, "ЁЖИК ABC") != 0) return std::string("CharUpperA gave \"") + upper + "\"";
    // 0xD0 (a lead byte) is 'Ð' and 0xA0 (the trail of "Р") a space in code page 1252's tables.
    if (text::ToLower(0xD0) != 0xD0) return "ToLower changes the byte D0";
    if (text::IsSpace(0xA0) || text::IsAlpha(0xD0)) return "a UTF-8 byte has a character class";
    // The player name filter: a Cyrillic name passes, a cut sequence or a control character does not.
    if (!IsPrintableText("Игрок Ёж", 0x3F, 1)) return "the name filter rejects \"Игрок Ёж\"";
    const char cut[] = {'\xD0', '\0'};
    if (IsPrintableText(cut, 0x3F, 1)) return "the name filter passes a cut UTF-8 sequence";
    if (IsPrintableText("Иг\x01рок", 0x3F, 1)) return "the name filter passes a control character";
    if (IsPrintableText("", 0x3F, 1)) return "the name filter passes an empty name";
    return {};
}

} // namespace

std::string Check() {
    std::string failure = CaseFailure();
    if (!failure.empty()) return "utf8text: " + failure;
    Log("harness: utf8text: Cyrillic case-insensitive compares, case changes and the name filter as expected");
    return {};
}

std::string CheckChatWrap(const std::string& wanted) {
    CChatOverlay* overlay = TheInterface()->root_;
    if (!overlay) return "chatwrap: the interface has no chat overlay";
    overlay->ClearLines();
    overlay->AddLine(wanted.c_str(), 0);
    std::string joined;
    int count = 0;
    std::string failure;
    for (auto* node = overlay->lines_.First(); node != overlay->lines_.End(); node = node->next) {
        const std::string line(node->data.name.c_str(), static_cast<std::size_t>(node->data.name.size()));
        if (!ValidUtf8(line) && failure.empty()) failure = "chatwrap: line " + std::to_string(count) + " is not valid UTF-8: \"" + line + "\"";
        Log("harness: chatwrap: line %d: \"%s\" (%d characters)", count, line.c_str(), text::CharacterCount(line));
        joined += line;
        ++count;
    }
    overlay->ClearLines();
    if (!failure.empty()) return failure;
    if (count < 2) return "chatwrap: the text was not broken into lines (" + std::to_string(count) + ")";
    if (joined != wanted) return "chatwrap: the lines joined give \"" + joined + "\"";
    Log("harness: chatwrap: %d lines, each valid UTF-8", count);
    return {};
}

} // namespace ei::verify::image::utf8text
