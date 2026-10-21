#pragma once

// WinMain's start-up branches a normal run does not take (verify build; A-I23, docs/notes/app.md).
//
// Scenario line (read when the scenario loads, before WinMain):
//   discprompt <n>   the disc check asks for the second disc <n> times, each box answered OK. WinMain
//                    (00456440, app_main.cpp CheckDrives) compares the disc folder (the registry's "CD-ROM
//                    Path") with the data folder and, when they differ, asks while the disc folder's volume
//                    label is not the options' "CD2 Label". The run's "CD-ROM Path" becomes the game folder
//                    written as "<folder>.\" (the same files, another text), and GetVolumeInformationA (the
//                    game's import slot) answers a label no disc has until the game has shown <n> boxes,
//                    then "EICD2" (the registry snapshot's CD2 Label), so start-up goes on as usual.
// Step:
//   discprompts <n>  require that the game showed the second disc's box <n> times
//
// The slots are taken at WinMain's FindWindowA (its window class step, before the registry is read), where
// the run's game paths (src/hook/paths.cpp) already answer "CD-ROM Path".

#include <string>

namespace ei::verify::startup {

// Consumes a "discprompt" line; false for any other verb.
bool ParseStep(const std::string& verb, const std::string& argument);

// WinMain's FindWindowA (the harness's own answer, harness.cpp): takes the slots the lines asked for.
void AtWindowClass();

// A message box the start-up lines expect: its answer (IDOK), or 0 when it is not one of theirs.
int AnswerMessageBox(const char* caption);

// The "discprompts <n>" step: an error message, empty when the count matches.
std::string DiscPromptsStep(const std::string& argument);

} // namespace ei::verify::startup
