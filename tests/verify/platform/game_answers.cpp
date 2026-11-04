// The harness's answers to the platform's seams (src/platform/seams.h): the harness's builds leave the game's own
// (src/platform/seams.cpp; CMakeLists.txt) out and take these. Without the harness (Answering false: no scenario) each
// is the game's answer.
#include "harness_platform.h"

#include "harness_crash.h"
#include "system/harness_system.h"

#include <string>

namespace hp = ei::verify::harnessplatform;
namespace sys = ei::verify::sys;

namespace platform {

// The one seam of the overlay: every file and folder call of the game asks for its path here. A read takes the
// overlay's copy when there is one; a write goes to the overlay (its folders made), Update copying the install's file
// there first, so an install file renamed is copied, never moved away (harness.cpp's MapPath).
std::string SystemPath(std::string_view path, PathUse use) {
    if (!hp::Answering()) return std::string(path);
    return hp::MapPath(path, use);
}

// The game's file writes, watched (harness_crash.h): each failure is logged and the first real one fails the run;
// "diskfull <text>" makes the writes to its files fail as on a full disk.
bool WriteFailsAsFull(std::string_view path) { return hp::Answering() && ei::verify::crash::FailsByStep(path); }

void WriteFailed(std::string_view path, std::intptr_t handle, unsigned count, unsigned long error, bool injected) {
    if (hp::Answering()) ei::verify::crash::WriteFailed(path, sys::FileOfHandle(handle), count, error, injected);
}

// Font smoothing is a setting of the whole desktop: the credits screen (0062B7D0 / 0062BD00) switched it off and
// "restored" a value it never read (off), so a harness game left it off for every program on the machine, and text in
// runs before and after differed (GDI's grey edges or none: the widescreen comparison of two runs). The game's text is
// drawn without smoothing in harness runs, as it looks once the titles have switched it off, whatever the desktop's
// setting and other programs do to it during the run: the interface text asks the setting (ui_font.cpp) and reads it
// as off.
//
// WinMain looks for another game window and, when it finds one, brings it to the front and exits. Harness runs go in
// parallel (run_all.py --jobs), each game in its own output folder, so in harness runs no other window counts. The
// other way (ST1b): a game the user starts without the harness runs the real check, so a harness game's window has a
// title of its own (harness_platform.cpp's CreateGameWindow) and a play launch never finds it.
bool IsolatedRun() { return hp::Answering(); }

} // namespace platform
