// WinMain's start-up branches a normal run does not take. See harness_startup.h.
#include "harness_startup.h"

#include "../../src/hook/log.h"
#include "image/image_side.h"
#include "platform/platform.h"
#include "platform/win32/platform_win32.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ei::verify::startup {
namespace {

using ei::Log;

constexpr const char* kNoDiscLabel = "A-I23 NO DISC";
constexpr const char* kDiscLabel = "EICD2";   // config\harness\registry.reg's "CD2 Label"

int g_discPrompts = -1;        // "discprompt <n>": the boxes to answer; -1 without the line
int g_discPromptsShown = 0;    // the second disc's boxes the game showed
bool g_wrongLabelAnswered = false;   // the last label answered was kNoDiscLabel
bool g_answeringLabels = false;      // the disc check's labels are the harness's (from AtWindowClass)

// "CD-ROM Path" as "<the run's game folder>.\": a disc folder whose text differs from the data folder's. Each read
// of the settings file's (game/settings.h) "CD-ROM Path" gets ".\".
void DiscPathRead(const char* name, std::string& value) {
    if (_stricmp(name, "CD-ROM Path") == 0) value += ".\\";
}

} // namespace

bool ParseStep(const std::string& verb, const std::string& argument) {
    if (verb != "discprompt") return false;
    g_discPrompts = std::atoi(argument.c_str());
    return true;
}

void AtWindowClass() {
    static bool done = false;
    if (done || g_discPrompts < 0) return;
    done = true;
    image::AddSettingsReadFilter(&DiscPathRead);
    g_answeringLabels = true;
    Log("harness: discprompt: %d box(es) of the second disc to answer", g_discPrompts);
}

int AnswerMessageBox(const char* caption) {
    if (!g_wrongLabelAnswered || !caption || std::strcmp(caption, "Warning") != 0) return 0;
    g_wrongLabelAnswered = false;
    ++g_discPromptsShown;
    Log("harness: discprompt: box %d of the second disc answered OK", g_discPromptsShown);
    return IDOK;
}

std::string DiscPromptsStep(const std::string& argument) {
    const int expected = std::atoi(argument.c_str());
    if (g_discPromptsShown == expected) return {};
    char text[128];
    std::snprintf(text, sizeof(text), "discprompts: the game asked for the second disc %d time(s), not %d",
                  g_discPromptsShown, expected);
    return text;
}

} // namespace ei::verify::startup

// The disc check's root ("<drive>\.", app_main.cpp DriveRoot) gets the harness's label (game_calls.cpp's call in the
// game's builds); every other root the volume's own.
std::string platform::VolumeLabel(const char* root) {
    using namespace ei::verify::startup;
    const std::size_t length = std::strlen(root);
    if (!g_answeringLabels || length < 2 || std::strcmp(root + length - 2, "\\.") != 0) {
        wchar_t label[MAX_PATH + 1] = {};
        if (!GetVolumeInformationW(text::Wide(root).c_str(), label, MAX_PATH + 1, nullptr, nullptr, nullptr, nullptr, 0))
            return {};
        return text::Narrow(label);
    }
    const char* label = g_discPromptsShown < g_discPrompts ? kNoDiscLabel : kDiscLabel;
    g_wrongLabelAnswered = label == kNoDiscLabel;
    ei::Log("harness: discprompt: the volume of %s answered \"%s\"", root, label);
    return label;
}
