// The image's UTF-8 file names through the game's file calls, its typing and its text (image_side.h;
// harness_utf8.cpp, harness_utf8.h).
#include "image_side.h"

#include "platform/log.h"

#include "game/console/console.h"
#include "game/app/singletons.h"
#include "game/players/player_messages.h"
#include "engine/core/asset_text.h"
#include "platform/harness_platform.h"
#include "game/ui/ui.h"
#include "game/ui/ui_chat_overlay.h"
#include "platform/seams.h"
#include "platform/platform.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace ei::verify::image::utf8files {

namespace {

// Under the game's saves folder ("\saves\": harness.cpp's SavesPath takes it to the run's harness_saves).
constexpr char kFolder[] = ".\\saves\\utf8 Проверка";
constexpr char kFile[] = ".\\saves\\utf8 Проверка\\файл.sav";
constexpr char kPattern[] = ".\\saves\\utf8 Проверка\\*.sav";
constexpr char kName[] = "файл.sav";
constexpr char kSavedFile[] = "\\utf8 Проверка\\файл.sav";
constexpr char kSavedFolder[] = "\\utf8 Проверка";

// Whether `path` (UTF-8) is on disk under its name as the system spells it (UTF-16 on Windows).
bool OnDisk(const std::string& path) {
    std::error_code error;
    return std::filesystem::exists(platform::NativePath(path), error);
}
constexpr char kContents[] = "U2: Проверка";

std::string RoundTrip(const char* savesDir) {
    if (!platform::CreateFolder(kFolder) && !platform::PathExists(kFolder)) return "the folder was not made";
    {
        platform::File file;
        if (!file.Open(kFile, platform::FileMode::Create)) return "the file was not made";
        file.Write(kContents, sizeof(kContents) - 1);
    }

    const std::vector<platform::FolderEntry> found = platform::FolderEntries(kPattern);
    if (found.empty()) return "the file was not listed";
    if (found[0].name != kName) return "the file was listed as " + found[0].name;

    std::string contents;
    if (!platform::ReadWholeFile(kFile, &contents)) return "the file was not opened again";
    if (contents != std::string(kContents, sizeof(kContents) - 1)) return "the file read back differs";

    // The names on disk (UTF-16 on Windows), in the run's saves folder.
    const std::string saves = savesDir;
    if (!OnDisk(saves + kSavedFile)) return "the file is not on disk under its name";
    if (!platform::RemoveFile(kFile)) return "the file was not deleted";
    if (!platform::RemoveFolder(kFolder)) return "the folder was not removed";
    if (OnDisk(saves + kSavedFolder)) return "the folder is still on disk";
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

std::string Type(void* window, const std::string& typed) {
    if (!window) return "typeunicode: no game window yet";
    unsigned characters = 0;
    for (std::size_t at = 0; at < typed.size();) {
        unsigned c = 0;
        const auto length = static_cast<std::size_t>(text::DecodeUtf8(typed.data() + at, typed.size() - at, &c));
        if (c == 0xFFFFFFFFu) return "typeunicode: the text is not valid UTF-8";
        platform::Event event;
        event.type = platform::EventType::Text;
        event.text = typed.substr(at, length);
        ei::verify::harnessplatform::Queue(event);
        ++characters;
        at += length;
    }
    Log("harness: typeunicode: %u characters handed in as typed text", characters);
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
    const console::ConsoleWindow* console = g_interface.console;
    if (!console) return "consoleline: no console window";
    const std::string& line = console->input;
    if (line != expected || console->caret != static_cast<int>(caret)) {
        char failure[512];
        std::snprintf(failure, sizeof(failure), "consoleline: the line is \"%s\" (%d bytes), caret at byte %d; expected \"%s\" (%u bytes), caret at byte %u",
                      line.c_str(), static_cast<int>(line.size()), console->caret, expected.c_str(), static_cast<unsigned>(expected.size()),
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

bool ValidUtf8(const std::string& line) {
    for (std::size_t at = 0; at < line.size();) {
        unsigned c = 0;
        at += static_cast<std::size_t>(text::DecodeUtf8(line.data() + at, line.size() - at, &c));
        if (c == 0xFFFFFFFFu || (c >= 0xD800 && c <= 0xDFFF)) return false;
    }
    return true;
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
    text::UpperInPlace(upper);
    if (std::strcmp(upper, "ЁЖИК ABC") != 0) return std::string("UpperInPlace gave \"") + upper + "\"";
    // 0xD0 (a lead byte) is 'Ð' and 0xA0 (the trail of "Р") a space in code page 1252's tables.
    if (text::ToLower(0xD0) != 0xD0) return "ToLower changes the byte D0";
    if (text::IsSpace(0xA0) || text::IsAlpha(0xD0)) return "a UTF-8 byte has a character class";
    // The player name filter: a Cyrillic name passes, a cut sequence or a control character does not.
    if (!IsPrintableText("Игрок Ёж")) return "the name filter rejects \"Игрок Ёж\"";
    const char cut[] = {'\xD0', '\0'};
    if (IsPrintableText(cut)) return "the name filter passes a cut UTF-8 sequence";
    if (IsPrintableText("Иг\x01рок")) return "the name filter passes a control character";
    if (IsPrintableText("")) return "the name filter passes an empty name";
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
    ChatOverlay* overlay = g_interface.root;
    if (!overlay) return "chatwrap: the interface has no chat overlay";
    overlay->ClearLines();
    overlay->AddLine(wanted);
    std::string joined;
    int count = 0;
    std::string failure;
    for (const ChatLine& entry : overlay->lines) {
        const std::string& line = entry.name;
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
